// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QFile>
#include <QSaveFile>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "operators/calibration_tools.hpp"
#include "model/spatial_data.hpp"
#include "model/workflow_ports.hpp"
#include <stdexcept>
#include "ui/shell/main_window.hpp"
#include "ui/widgets/parameter_spin_box.hpp"
#include "ui/widgets/parameter_grid.hpp"
#include "ui/widgets/source_choice.hpp"
#include "runtime/worker_protocol.hpp"
#include "ui/views/workflow_view.hpp"
namespace workbench {
void MainWindow::buildGeometryUi() {
  geometryInputPanel_ = new QWidget(operatorControls_);
  auto *input = new QVBoxLayout(geometryInputPanel_);
  input->setContentsMargins(0, 0, 0, 0);
  auto *note = new QLabel(
      QStringLiteral(
          "输入按连线限定；对象编号从 1 开始。独立筛选/拟合测试使用当前图像 "
          "Canny 轮廓；测量和判定请连接工作流输入。"),
      geometryInputPanel_);
  note->setObjectName("geometryInputHint");
  note->setWordWrap(true);
  note->setProperty("class", "secondaryText");
  geometryA_ = new SourceChoice(geometryInputPanel_);
  geometryA_->setObjectName("geometryInputA");
  input->addWidget(geometryA_);
  geometryB_ = new SourceChoice(geometryInputPanel_);
  geometryB_->setObjectName("geometryInputB");
  input->addWidget(geometryB_);
  input->addWidget(note);
  qobject_cast<QVBoxLayout *>(operatorControls_->findChild<QWidget*>("rangeDetails")->layout())
      ->insertWidget(0, geometryInputPanel_);
  for (bool second : {false, true})
    connect(
        second ? geometryB_ : geometryA_,
        QOverload<int>::of(&QComboBox::activated), this,
        [this, second](int index) {
          if (syncing_ || runQueue_.active() || !nodeContext() || selectedStep_ < 0)
            return;
          auto *box = second ? geometryB_ : geometryA_;
          bindNamedInput(selectedStep_, second ? "ObjectB" : "ObjectA", box->itemData(index).toInt(),
                         box->itemData(index, Qt::UserRole+1).toString());
        });
  for (auto tool : {Algorithm::ContourFilter,
                    Algorithm::LineFit,
                    Algorithm::CircleFit,
                    Algorithm::Measure,
                    Algorithm::Judge,
                    Algorithm::ContourProcess,
                    Algorithm::EllipseFit,
                    Algorithm::RegionMorph,
                    Algorithm::RegionFilter,
                    Algorithm::RegionFeatures,
                    Algorithm::RegionBoolean,
                    Algorithm::ScaleCalibration,
                    Algorithm::ImageMorph,
                    Algorithm::ImageAffine,
                    Algorithm::ImageGradient,
                    Algorithm::ImageEnhance,
                    Algorithm::CalibrationDetect,
                    Algorithm::ImageRectify,
                    Algorithm::PlaneMeasure,
                    Algorithm::StereoRectify,
                    Algorithm::StereoDisparity,
                    Algorithm::StereoReconstruct,
                    Algorithm::CloudSelect,
                    Algorithm::CloudMeasure,
                    Algorithm::StereoImage,
                    Algorithm::DisparityFilter,
                    Algorithm::StructuredDecode,
                    Algorithm::StructuredReconstruct,
                    Algorithm::StructuredImage, Algorithm::ImageInput, Algorithm::SequenceInput,
                    Algorithm::MonoCalibration,Algorithm::StereoCalibration,Algorithm::StructuredCalibration,
                    Algorithm::GrayStats, Algorithm::Polar, Algorithm::SphereFit, Algorithm::PlaneFit}) {
    auto *grid = new ParameterGrid;
    auto *setup = new ParameterGrid;
    setup->setObjectName(QString("inputParameters_%1").arg(int(tool)));
    auto *extra = new ParameterGrid;
    extra->setObjectName(QString("advancedParameters_%1").arg(int(tool)));
    auto destination = [&](const char *key) {
      const QString name = QLatin1String(key);
      if (sourceTool(tool) || name == "objectA" || name == "objectB" || name == "field" ||
          name == "pointA" || name == "pointB" || name == "multiView")
        return setup;
      if (name == "qualityTolerance" || name == "borderMargin" ||
          name == "sort" || name == "take" || name == "relativeGap" ||
          name == "shift" || name == "surface" || name == "edge" ||
          tool == Algorithm::ContourProcess && name == "angle" ||
          tool == Algorithm::ImageAffine &&
              (name == "inverse" || name == "interpolation" ||
               name == "centerX" || name == "centerY"))
        return extra;
      return grid;
    };
    if (calibrationConsumer(tool)) {
      auto *model = new SourceChoice(setup);
      model->setObjectName(QString("calibrationSource_%1").arg(int(tool)));
      setup->addField(QStringLiteral("标定模型来源"), model);
      connect(model, QOverload<int>::of(&QComboBox::activated), this,
              [this, model](int row) {
                if (syncing_ || runQueue_.active() || !nodeContext())
                  return;
                EditGuard edit(*this, QStringLiteral("选择标定模型来源"));
                setInputBinding(editableRange(), "Calibration", {model->itemData(row).toInt(), model->itemData(row).toInt() >= 0 ? QString("result") : QString()});
                rangeChanged();
              });
    }
    auto real = [&](const char *key, const QString &title, double lo, double hi,
                    double def, int decimals = 3) {
      auto *field = new ParameterDoubleSpinBox(grid);
      field->setObjectName(QString("geometry_%1_%2").arg(int(tool)).arg(key));
      field->setRange(lo, hi);
      field->setDecimals(decimals);
      field->setValue(def);
      field->setKeyboardTracking(false);
      field->setButtonSymbols(QAbstractSpinBox::NoButtons);
      field->setProperty("default", def);
      field->setProperty("tool", int(tool));
      geometryFields_.append({field, key});
      field->setProperty("inputParameter", destination(key) == setup);
      destination(key)->addField(title, field);
      connect(field, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
              &MainWindow::parametersChanged);
    };
    auto choice = [&](const char *key, const QString &title,
                      const QStringList &names,
                      const QStringList &values = QStringList{}) {
      auto *field = new QComboBox(grid);
      field->setObjectName(QString("geometry_%1_%2").arg(int(tool)).arg(key));
      for (int i = 0; i < names.size(); ++i)
        field->addItem(names[i],
                       values.isEmpty() ? QVariant(i) : QVariant(values[i]));
      field->setProperty("tool", int(tool));
      field->setProperty(
          "default", values.isEmpty() ? QVariant(0) : QVariant(values.first()));
      geometryFields_.append({field, key});
      field->setProperty("inputParameter", destination(key) == setup);
      destination(key)->addField(title, field);
      connect(field, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
              &MainWindow::parametersChanged);
    };
    if (sourceTool(tool)) {
      if(tool==Algorithm::ImageInput) choice("inputMode",QStringLiteral("输入类型"),{QStringLiteral("单图"),QStringLiteral("多视图组"),QStringLiteral("XYZ 坐标图")});
      if(tool==Algorithm::ImageInput) choice("xyzUnit",QStringLiteral("XYZ 原始单位"),{QStringLiteral("m（转换为 mm）"),QStringLiteral("mm")});
      auto* binding = new QLineEdit(grid);
      binding->setObjectName(QString("geometry_%1_binding").arg(int(tool)));
      binding->setReadOnly(true);
      binding->setProperty("tool",int(tool)); binding->setProperty("default",QString());
      geometryFields_.append({binding,"binding"});
      binding->hide(); // Stable ownership is managed by the node, not a user-editable column name.
      connect(binding, &QLineEdit::editingFinished, this, &MainWindow::parametersChanged);
      auto* configure = new QPushButton(QStringLiteral("管理此输入的数据…"),grid);
      configure->setObjectName(QString("manageSource_%1").arg(int(tool)));
      setup->addField(QStringLiteral("输入数据"), configure);
      connect(configure,&QPushButton::clicked,this,&MainWindow::showSampleInputs);
      if(tool==Algorithm::SequenceInput) {
        auto* browse=new QPushButton(QStringLiteral("浏览当前图像序列…"),grid);
        setup->addField(QStringLiteral("序列预览"), browse);
        connect(browse,&QPushButton::clicked,this,[this]{showStructuredSequence();});
      }
      auto* sourceNote = new QLabel(QStringLiteral("数据属于当前输入轨；联合流程按数据身份同步取数。改节点名称不改变数据绑定。一条图像序列或一组视图各作为一次输入。"),grid);
      sourceNote->setWordWrap(true);
      setup->addField(QStringLiteral("绑定说明"), sourceNote);
    } else if (calibrationTool(tool)) {
      auto* board=new QPushButton(QStringLiteral("选择标定板…"),grid);
      board->setObjectName(QString("calibrationLoad_%1").arg(int(tool)));
      setup->addField(QStringLiteral("标定板"), board);
      connect(board,&QPushButton::clicked,this,[this] {
        const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("选择标定板描述"),{},QStringLiteral("标定板 (*.cpd *.descr)"));
        if(!path.isEmpty())setCalibrationBoard(path);
      });
      auto* info=new QLabel(grid);info->setObjectName(QString("calibrationInfo_%1").arg(int(tool)));info->setWordWrap(true);
      setup->addField(QStringLiteral("已选标定板"), info);
      real("focusMM",QStringLiteral("相机初始焦距 / mm"),.1,500,12);
      real("pitchUM",QStringLiteral("相机像元尺寸 / µm"),.1,100,3.45);
      if(tool!=Algorithm::MonoCalibration) {
        real("secondFocusMM",tool==Algorithm::StereoCalibration?QStringLiteral("右相机焦距 / mm"):QStringLiteral("投影仪焦距 / mm"),.1,500,12);
        real("secondPitchUM",tool==Algorithm::StereoCalibration?QStringLiteral("右相机像元 / µm"):QStringLiteral("投影仪像元 / µm"),.1,100,3.45);
      } else real("planeView",QStringLiteral("世界平面姿态编号"),1,64,1,0);
      if(tool==Algorithm::StructuredCalibration) {
        real("projectorWidth",QStringLiteral("投影宽 / px"),8,8192,1280,0);
        real("projectorHeight",QStringLiteral("投影高 / px"),8,8192,720,0);
        real("stripeWidth",QStringLiteral("最小条纹宽 / px"),2,256,16,0);
        real("minContrast",QStringLiteral("最小曝光对比度"),0,255,10,0);
        auto* patterns=new QPushButton(QStringLiteral("生成双方向标定图案…"),grid);
        setup->addField(QStringLiteral("投影图案"), patterns);
        connect(patterns,&QPushButton::clicked,this,[this] {
          const auto path=QFileDialog::getExistingDirectory(this,QStringLiteral("标定图案输出目录"));if(path.isEmpty())return;
          guard(QStringLiteral("生成标定图案"),[&]{const auto o=document_.parameters.options;
            generateStructuredPatterns({{"width",o.value("projectorWidth").toInt(1280)},{"height",o.value("projectorHeight").toInt(720)},
              {"stripe",o.value("stripeWidth").toInt(16)},{"orientation","both"},{"type","gray_code_and_phase_shift"}},path);
          },true);
        });
      }
      auto* save=new QPushButton(QStringLiteral("导出本次标定模型…"),grid);
      save->setObjectName(QString("calibrationExport_%1").arg(int(tool)));
      setup->addField(QStringLiteral("标定结果"), save);
      connect(save,&QPushButton::clicked,this,[this,tool] {
        if(busy()||!nodeContext()||selectedStep_<0||activeImage_<0)return;
        guard(QStringLiteral("导出标定模型"),[&]{
          const auto outputs=images_[activeImage_].workflowResult.nodeOutputs;
          if(selectedStep_>=outputs.size()||outputs[selectedStep_].state!=NodeState::Success||outputs[selectedStep_].calibrationResult.isEmpty())
            throw std::runtime_error("请先成功运行此标定节点");
          const auto filter=tool==Algorithm::MonoCalibration?QStringLiteral("单目标定 (*.mvcalib)"):
              tool==Algorithm::StereoCalibration?QStringLiteral("双目标定 (*.mvstereo)"):QStringLiteral("结构光模型 (*.mvslm)");
          const auto path=QFileDialog::getSaveFileName(this,QStringLiteral("导出标定模型"),{},filter);if(path.isEmpty())return;
          QSaveFile file(path);const auto bytes=outputs[selectedStep_].calibrationResult;
          if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())throw std::runtime_error("保存标定模型失败");
        },true);
      });
      auto* hint=new QLabel(tool==Algorithm::MonoCalibration?QStringLiteral("当前序列须包含同一相机的 3–64 张不同姿态标定图。一条序列就是一组标定数据，单次运行只求解当前序列，不汇总其他数据。校正单张图像时，另接图像输入与本节点模型。"):
          QStringLiteral("汇总所连接输入轨的 3–64 个已选完整数据。双目每组须配对，结构光每组须包含双方向完整曝光。结果可连到对应校正/解码节点，也可导出后在其他工程加载。"),grid);
      hint->setWordWrap(true);
      setup->addField(QStringLiteral("运行方式"), hint);
    } else if (tool == Algorithm::StructuredDecode) {
      real("minContrast", QStringLiteral("最小曝光对比度"), 0, 255, 10, 0);
      real("minAmplitude", QStringLiteral("最小相移幅值"), 0, 255, 2);
    } else if (tool == Algorithm::StructuredImage) {
      choice("component", QStringLiteral("图像分量"),
             {QStringLiteral("相机参考图"), QStringLiteral("投影列坐标"), QStringLiteral("投影行坐标"),
              QStringLiteral("曝光对比度"), QStringLiteral("归一化对比度"), QStringLiteral("解码有效域"), QStringLiteral("拒绝原因")});
    } else if (tool == Algorithm::StructuredReconstruct) {
      auto* description = new QLabel(QStringLiteral("使用模型内相机/投影仪标定；三维点和测量为 mm，XYZ 图为 m。输入须为结构光解码，ROI 在点云选取中设置。"), grid);
      description->setWordWrap(true);
      setup->addField(QStringLiteral("数据说明"), description);
    } else if (tool == Algorithm::DisparityFilter) {
      real("maxCost", QStringLiteral("匹配代价上限 · 越小越严格"), 0, 1e9, 32);
      real("minArea", QStringLiteral("最小有效连通面积 / px²"), 0, 1000000, 5, 0);
    } else if (tool == Algorithm::StereoImage) {
      choice("component", QStringLiteral("输出图像"),
             {QStringLiteral("校正左图"), QStringLiteral("校正右图")});
    } else if (tool == Algorithm::StereoDisparity) {
      real("minDisparity", QStringLiteral("最小视差 / px"), -512, 512, -64, 0);
      real("maxDisparity", QStringLiteral("最大视差 / px"), -512, 512, 0, 0);
      real("surface", QStringLiteral("表面平滑"), 0, 255, 6, 0);
      real("edge", QStringLiteral("边缘平滑"), 0, 255, 35, 0);
    } else if (tool == Algorithm::GrayStats) {
      choice("mode", QStringLiteral("判定用统计量"), {QStringLiteral("均值"), QStringLiteral("标准差"),
             QStringLiteral("最小值"), QStringLiteral("最大值"), QStringLiteral("极差")});
    } else if (tool == Algorithm::Polar) {
      real("centerX", QStringLiteral("圆心列 / px（-1 居中）"), -1, 100000, -1);
      real("centerY", QStringLiteral("圆心行 / px（-1 居中）"), -1, 100000, -1);
      real("inner", QStringLiteral("起始半径 / px"), 0, 100000, 0);
      real("outer", QStringLiteral("结束半径 / px（0 自动）"), 0, 100000, 0);
      real("start", QStringLiteral("起始角 / °"), -360, 360, 0);
      real("end", QStringLiteral("结束角 / °"), -360, 360, 360);
      real("outWidth", QStringLiteral("角向像素数"), 2, 4096, 360, 0);
      real("outHeight", QStringLiteral("径向像素数"), 2, 4096, 128, 0);
      choice("interpolation", QStringLiteral("插值"), {QStringLiteral("最近邻"), QStringLiteral("双线性")});
    } else if (tool == Algorithm::PlaneFit) {
      choice("mode", QStringLiteral("判定用残差"), {QStringLiteral("均方根 RMS"), QStringLiteral("最大绝对值"), QStringLiteral("峰谷差")});
    } else if (tool == Algorithm::SphereFit) {
      real("minRadius", QStringLiteral("最小球半径 / mm"), .001, 1e6, .01);
      real("maxRadius", QStringLiteral("最大球半径 / mm"), .001, 1e6, 10000);
      choice("mode", QStringLiteral("判定用尺寸"), {QStringLiteral("半径"), QStringLiteral("直径"), QStringLiteral("拟合 RMS")});
    } else if (tool == Algorithm::CloudSelect) {
      choice("selection", QStringLiteral("选取方式"),
             {QStringLiteral("手绘范围与深度"), QStringLiteral("区域 B 与深度")});
      real("minZ", QStringLiteral("最小深度 / mm"), 0, 1e9, 0);
      real("maxZ", QStringLiteral("最大深度 / mm"), 0, 1e9, 100000);
    } else if (tool == Algorithm::CloudMeasure) {
      choice("mode", QStringLiteral("测量方式"),
             {QStringLiteral("相机深度范围"), QStringLiteral("三维两点距离"),QStringLiteral("相机深度中位数"),
              QStringLiteral("相对基准平面的最大垂距 · A 被测点云 / B 平面或基准点云")});
      real("pointA", QStringLiteral("点 A 编号"), 1, 1000000, 1, 0);
      real("pointB", QStringLiteral("点 B 编号"), 1, 1000000, 2, 0);
    } else if (tool == Algorithm::StereoRectify ||
               tool == Algorithm::StereoReconstruct) {
      auto *hint = new QLabel(
          tool == Algorithm::StereoRectify
              ? QStringLiteral("连接左图像 A 与右图像 B；选择固定双目标定，或沿用当前数据关联标定。使用完整原始图像。")
              : QStringLiteral(
                    "使用校正参数与视差重建，XYZ 为 m，三维点和测量显示 mm。"),
          grid);
      hint->setWordWrap(true);
      hint->setObjectName(QString("captureInputHelp_%1").arg(int(tool)));
      setup->addField(QStringLiteral("数据说明"), hint);
    } else if (tool == Algorithm::ImageMorph) {
      choice("operation", QStringLiteral("处理操作"),
             {QStringLiteral("灰度腐蚀"), QStringLiteral("灰度膨胀"),
              QStringLiteral("灰度开运算"), QStringLiteral("灰度闭运算"),
              QStringLiteral("白顶帽"), QStringLiteral("黑顶帽")});
      real("width", QStringLiteral("窗口宽 · 奇数"), 1, 101, 5, 0);
      real("height", QStringLiteral("窗口高 · 奇数"), 1, 101, 5, 0);
    } else if (tool == Algorithm::ImageAffine) {
      choice("inverse", QStringLiteral("变换方向"),
             {QStringLiteral("正向"), QStringLiteral("逆向 · 同一矩阵求逆")});
      choice("interpolation", QStringLiteral("插值"),
             {QStringLiteral("最近邻"), QStringLiteral("双线性")});
      real("tx", QStringLiteral("X 平移 / px"), -1e6, 1e6, 0);
      real("ty", QStringLiteral("Y 平移 / px"), -1e6, 1e6, 0);
      real("rotation", QStringLiteral("顺时针旋转 / °"), -180, 180, 0);
      real("shear", QStringLiteral("X 切变"), -10, 10, 0);
      real("scaleX", QStringLiteral("X 缩放"), -100, 100, 1);
      real("scaleY", QStringLiteral("Y 缩放"), -100, 100, 1);
      real("centerX", QStringLiteral("旋转/缩放中心 X / px"), -1e6, 1e6, 0);
      real("centerY", QStringLiteral("旋转/缩放中心 Y / px"), -1e6, 1e6, 0);
    } else if (tool == Algorithm::ImageGradient) {
      choice("method", QStringLiteral("提取方法"),
             {QStringLiteral("Sobel · 绝对幅值"),
              QStringLiteral("Sobel · 欧氏幅值"), QStringLiteral("高斯 X 导数"),
              QStringLiteral("高斯 Y 导数"), QStringLiteral("高斯拉普拉斯")});
      real("sigma", QStringLiteral("高斯平滑系数"), .3, 10, 1.2);
    } else if (tool == Algorithm::ImageEnhance) {
      choice(
          "operation", QStringLiteral("增强方法"),
          {QStringLiteral("线性灰度变换"), QStringLiteral("局部锐化 · 7×7")});
      real("gain", QStringLiteral("灰度增益"), 0, 100, 1);
      real("offset", QStringLiteral("灰度偏移"), -65535, 65535, 0);
      real("factor", QStringLiteral("锐化强度"), 0, 10, 1);
    } else if (tool == Algorithm::CalibrationDetect ||
               tool == Algorithm::ImageRectify ||
               tool == Algorithm::PlaneMeasure) {
      auto *load = new QPushButton(tool == Algorithm::CalibrationDetect
                                       ? QStringLiteral("选择标定板…")
                                       : QStringLiteral("加载标定文件…"),
                                   grid);
      load->setObjectName(QString("calibrationLoad_%1").arg(int(tool)));
      setup->addField(QStringLiteral("标定资源"), load);
      auto *label = new QLabel(grid);
      label->setWordWrap(true);
      label->setObjectName(QString("calibrationInfo_%1").arg(int(tool)));
      setup->addField(QStringLiteral("当前资源"), label);
      connect(load, &QPushButton::clicked, this, [this, tool] {
        auto path = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择标定资源"), {},
            tool == Algorithm::CalibrationDetect
                ? QStringLiteral("标定板 (*.cpd *.descr)")
                : QStringLiteral("Studio 标定 (*.mvcalib)"));
        if (!path.isEmpty()) {
          if (tool == Algorithm::CalibrationDetect)
            setCalibrationBoard(path);
          else
            loadCalibrationArtifact(path);
        }
      });
      if (tool == Algorithm::CalibrationDetect) {
        real("focusMM", QStringLiteral("初始焦距 / mm"), .1, 500, 12);
        real("pitchUM", QStringLiteral("像元尺寸 / µm"), .1, 100, 3.45);
      }
      if (tool == Algorithm::ImageRectify) {
        choice("mode", QStringLiteral("校正方式"),
               {QStringLiteral("去畸变 · 原尺寸"), QStringLiteral("世界平面")});
        choice("interpolation", QStringLiteral("平面插值"),
               {QStringLiteral("最近邻"), QStringLiteral("双线性")});
        real("outWidth", QStringLiteral("平面宽 / px"), 1, 8192, 640, 0);
        real("outHeight", QStringLiteral("平面高 / px"), 1, 8192, 640, 0);
        real("mmPerPixel", QStringLiteral("平面比例 / mm/px"), .000001, 100, .1,
             6);
      }
      if (tool == Algorithm::PlaneMeasure) {
        real("objectA", QStringLiteral("对象 A 编号"), 1, 10000, 1, 0);
        real("objectB", QStringLiteral("对象 B 编号"), 1, 10000, 1, 0);
      }
    } else if (tool == Algorithm::RegionMorph) {
      choice("operation", QStringLiteral("操作"),
             {QStringLiteral("开运算"), QStringLiteral("闭运算"),
              QStringLiteral("膨胀"), QStringLiteral("腐蚀"),
              QStringLiteral("填孔"), QStringLiteral("合并后连通")});
      choice("shape", QStringLiteral("结构元素"),
             {QStringLiteral("圆形"), QStringLiteral("矩形")});
      real("radius", QStringLiteral("圆半径 / px"), .5, 100, 2, 1);
      real("width", QStringLiteral("矩形宽 · 奇数"), 1, 201, 5, 0);
      real("height", QStringLiteral("矩形高 · 奇数"), 1, 201, 5, 0);
    } else if (tool == Algorithm::RegionFilter) {
      real("minArea", QStringLiteral("最小面积 / px²"), 0, 1e12, 0);
      real("maxArea", QStringLiteral("最大面积 / px²"), 0, 1e12, 1e12);
      real("minWidth", QStringLiteral("最小宽度 / px"), 0, 1e12, 0);
      real("maxWidth", QStringLiteral("最大宽度 / px"), 0, 1e12, 1e12);
      real("minHeight", QStringLiteral("最小高度 / px"), 0, 1e12, 0);
      real("maxHeight", QStringLiteral("最大高度 / px"), 0, 1e12, 1e12);
      real("minCircularity", QStringLiteral("最小圆度"), 0, 1, 0);
      real("maxCircularity", QStringLiteral("最大圆度"), 0, 1, 1);
      choice("sort", QStringLiteral("排序"),
             {QStringLiteral("输入顺序"), QStringLiteral("面积降序"),
              QStringLiteral("从左到右"), QStringLiteral("从上到下")});
      real("take", QStringLiteral("保留前 N 个 · 0 全部"), 0, 10000, 0, 0);
    } else if (tool == Algorithm::RegionFeatures) {
      choice("mode", QStringLiteral("输出特征"),
             {QStringLiteral("集合数量 · 空集合为零"),
              QStringLiteral("逐区域面积"), QStringLiteral("逐区域宽度"),
              QStringLiteral("逐区域高度"), QStringLiteral("逐区域圆度"),
              QStringLiteral("质心列"), QStringLiteral("质心行"), QStringLiteral("旋转包围框长边"),
              QStringLiteral("旋转包围框短边"), QStringLiteral("旋转包围框方向 / °")});
    } else if (tool == Algorithm::RegionBoolean) {
      choice("operation", QStringLiteral("集合操作"),
             {QStringLiteral("合并 A 并重新连通"),
              QStringLiteral("A 与 B 并集"), QStringLiteral("A 与 B 交集"),
              QStringLiteral("A 减 B"), QStringLiteral("A 与 B 对称差")});
    } else if (tool == Algorithm::ScaleCalibration) {
      real("pixelLength", QStringLiteral("参考像素长度 / px"), .000001, 1e9,
           100, 6);
      real("realLength", QStringLiteral("参考实际长度 / mm"), .000001, 1e9, 10,
           6);
    } else if (tool == Algorithm::ContourFilter) {
      real("minLength", QStringLiteral("最小长度 / px"), 0, 1e9, 0);
      real("maxLength", QStringLiteral("最大长度 / px"), 0, 1e9, 1e9);
      real("borderMargin", QStringLiteral("排除范围边缘 / px · 0 关闭"), 0, 20,
           0, 1);
      choice("closed", QStringLiteral("闭合性"),
             {QStringLiteral("全部"), QStringLiteral("闭合（端点 ≤ 2 px）"),
              QStringLiteral("开放")});
      choice("sort", QStringLiteral("排序"),
             {QStringLiteral("保持输入顺序"), QStringLiteral("长度降序"),
              QStringLiteral("从左到右"), QStringLiteral("从上到下")});
      real("take", QStringLiteral("保留前 N 个 · 0 全部"), 0, 10000, 0, 0);
    } else if (tool == Algorithm::ContourProcess) {
      choice("operation", QStringLiteral("处理操作"),
             {QStringLiteral("平滑"), QStringLiteral("分段 · 线与圆弧"),
              QStringLiteral("相邻合并"), QStringLiteral("共线合并"),
              QStringLiteral("共圆合并")});
      real("smooth", QStringLiteral("平滑窗口 · 奇数点"), 3, 101, 5, 0);
      real("distance", QStringLiteral("距离阈值 / px"), .01, 1e6, 2);
      real("relativeGap", QStringLiteral("相对间距上限"), 0, 100, .1);
      real("shift", QStringLiteral("偏移 / 半径与圆心差 / px"), 0, 1e6, 2);
      real("angle", QStringLiteral("角度阈值 / °"), 0, 180, 10, 1);
    } else if (tool == Algorithm::LineFit || tool == Algorithm::CircleFit ||
               tool == Algorithm::EllipseFit) {
      choice("method", QStringLiteral("拟合方法"),
             {QStringLiteral("最小二乘"), QStringLiteral("Huber 稳健"),
              QStringLiteral("Tukey 稳健")});
      real("maxRms", QStringLiteral("全部点 RMS 上限 / px"), 0, 1e6, 3);
      real("qualityTolerance", QStringLiteral("采样点显示容差 / px"), .001, 1e6,
           1);
      if (tool == Algorithm::CircleFit || tool == Algorithm::EllipseFit)
        real("minArc", QStringLiteral("最小圆弧覆盖 / °"), 0, 360, 60, 1);
    } else {
      real("objectA", QStringLiteral("对象 A 编号"), 1, 10000, 1, 0);
      if (tool == Algorithm::Measure) {
        real("objectB", QStringLiteral("对象 B 编号"), 1, 10000, 1, 0);
        choice("mode", QStringLiteral("测量定义"),
               {QStringLiteral("圆直径 · A 圆"),
                QStringLiteral("圆心到直线 · A 圆 B 线"),
                QStringLiteral("圆周到直线 · A 圆 B 线"),
                QStringLiteral("中心距 · A/B 中心"),
                QStringLiteral("直线锐夹角 · A/B 线"),
                QStringLiteral("圆周最短间隙 · A/B 圆")});
      } else {
        choice("field", QStringLiteral("判定指标"),
               {QStringLiteral("主测量值")}, {"value"});
        real("lower", QStringLiteral("下限 · 含端点"), -1e12, 1e12, 0);
        real("upper", QStringLiteral("上限 · 含端点"), -1e12, 1e12, 100);
      }
    }
    if (captureTool(tool)) {
      auto* file = new QPushButton(tool == Algorithm::StereoRectify ? QStringLiteral("选择双目标定…") : QStringLiteral("选择结构光模型…"), grid);
      file->setObjectName(QString("captureModel_%1").arg(int(tool)));
      setup->addField(QStringLiteral("固定资源"), file);
      auto* path=new QLabel(grid);
      path->setObjectName(QString("captureModelPath_%1").arg(int(tool)));
      path->setTextFormat(Qt::PlainText);path->setWordWrap(true);
      path->setTextInteractionFlags(Qt::TextSelectableByMouse);
      setup->addField(QStringLiteral("模型路径"), path);
      connect(file,&QPushButton::clicked,this,[this,tool] {
        if (busy()) return;
        const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("选择标定／模型"),{}, tool==Algorithm::StereoRectify ? "MVKit (*.mvstereo)" : "MVKit (*.mvslm)");
        if (path.isEmpty()) return;
        guard(QStringLiteral("加载输入模型"),[&] {
          QFile source(path);
          if (!source.open(QIODevice::ReadOnly) || source.size()>65536) throw std::runtime_error("无法读取标定／模型");
          const auto bytes=source.readAll();
          if (tool==Algorithm::StereoRectify) stereoCalibration(bytes); else structuredModelInfo(bytes);
          EditGuard edit(*this,QStringLiteral("更换输入模型"));
          if (nodeContext())
            setInputBinding(editableRange(), "Calibration", {-1, {}});
          document_.parameters.calibrationArtifact=bytes;
          document_.parameters.calibrationName=QFileInfo(path).fileName();
          document_.parameters.calibrationConfig["sourcePath"]=QFileInfo(path).absoluteFilePath();
          parametersChanged();
        },true);
      });
    }
    parameterInputForms_.append(setup);
    parameterAdvancedForms_.append(extra);
    auto* inputsLayout = qobject_cast<QVBoxLayout*>(
        operatorControls_->findChild<QWidget*>("rangeDetails")->layout());
    inputsLayout->insertWidget(inputsLayout->indexOf(rangePanel_) + 1, setup);
    parameterForms_.append(grid);
    parameterLayout_->addWidget(grid);
  }
  geometryInputPanel_->hide();
}
void MainWindow::syncGeometryUi() {
  if (!geometryInputPanel_) return;
  const bool prior = syncing_;
  syncing_ = true;
  const auto &p = document_.parameters;
  QString workflowModel;
  if (auto *model = findChild<QComboBox *>(
          QString("calibrationSource_%1").arg(int(p.algorithm)))) {
    const QSignalBlocker block(model);
    model->clear();
    model->addItem(QStringLiteral("请选择标定模型来源"), -2);
    if (!p.calibrationArtifact.isEmpty())
      model->addItem(QStringLiteral("文件 · ") + p.calibrationName, -1);
    if (nodeContext() && selectedStep_ >= 0) {
      const auto names = nodeNames();
      for (int source : workflowAncestors(workflowLinks_, selectedStep_))
        if (source >= 0 &&
            calibrationLink(workflow_[source].algorithm, p.algorithm))
          model->addItem(names.value(source) + QStringLiteral(" → 标定模型"),
                         source);
      const int source = rangeForNode(selectedStep_).calibrationSource;
      showSourceSelection(model, source, -2);
      if (source >= 0)
        workflowModel =
            QStringLiteral("工作流标定模型：") + names.value(source);
    } else
      showSourceSelection(model, -1, -2);
    model->setEnabled(nodeContext() && !runQueue_.active());
  }
  const bool geometryTool = typedInputTool(p.algorithm);
  geometryInputPanel_->setVisible(optionTool(p.algorithm));
  geometryInputPanel_->findChild<QLabel *>("geometryInputHint")
      ->setText(
          nodeContext()
              ? QStringLiteral("输入来源由连线限定；对象编号从 1 开始。")
          : p.algorithm == Algorithm::Measure || p.algorithm == Algorithm::Judge
              ? QStringLiteral(
                    "请在工作流中连接类型化数据，或从流程节点列表调试。")
              : QStringLiteral(
                    "独立测试：当前图像的 Canny 轮廓；请用 ROI 限定范围。"));
  if (int(p.algorithm) >= int(Algorithm::RegionMorph))
    geometryInputPanel_->findChild<QLabel *>("geometryInputHint")
        ->setText(
            p.algorithm == Algorithm::ScaleCalibration
                ? QStringLiteral("等比例平面标定：相同工作平面、镜头和分辨率。"
                                 "仅换算 px 长度为 mm；不校正透视或畸变。")
            : nodeContext()
                ? QStringLiteral("区域输入按连线选择。尺寸为轴对齐包围框，单位 "
                                 "px；圆度无单位。")
                : QStringLiteral("独立测试：使用当前图像灰度分割（105–255）得到"
                                 "区域。集合运算需工作流 A/B。"));
  if (p.algorithm >= Algorithm::ImageMorph) {
    const QString hint =
        (calibrationTool(p.algorithm) || p.algorithm >= Algorithm::GrayStats) ? operatorDefinition(p.algorithm).description
        : p.algorithm >= Algorithm::StructuredDecode
            ? QStringLiteral("结构光采集组按完整投影顺序处理。解码输出投影坐标、质量与有效域；重建使用模型内标定，二维区域和点云共享相机像素坐标。请在点云选取中设置 ROI。")
        : p.algorithm == Algorithm::DisparityFilter
            ? QStringLiteral("SGM 平均最优路径代价，越小越优，不是置信概率。过滤只删除无效视差，不补洞；结果表显示有效比例与原因。")
        : p.algorithm >= Algorithm::StereoRectify
            ? (p.algorithm == Algorithm::CloudSelect
                   ? QStringLiteral(
                         "在同坐标参考图或深度图绘制固定 "
                         "ROI，或连接对应相机像素坐标的区域 B；深度区间为 mm。")
               : p.algorithm == Algorithm::StereoImage
                   ? QStringLiteral("从 A 提取校正图像供二维算子使用。左、右图像素坐标系独立；右图区域不能直接选取左图点云。")
               : p.algorithm == Algorithm::CloudMeasure && p.options.value("mode").toInt() == 3
                   ? QStringLiteral("A 选择被测点云，B 选择同一坐标系中的平面拟合结果或基准点云。输出 A 中各点到基准平面的最大绝对垂距（mm）。")
               : p.algorithm == Algorithm::CloudMeasure
                   ? QStringLiteral("深度范围是相机 Z "
                                    "的最大最小差，不是相对基准平面的高度。点编"
                                    "号属于本次点云。")
                   : QStringLiteral("使用采集组及具名类型输入；结果均为本组本次"
                                    "重算，切换观察输出不重算。"))
        : p.algorithm == Algorithm::ImageAffine
            ? QStringLiteral(
                  "固定画布，域外裁切；逆向使用当前正向参数的精确逆矩阵，无法恢"
                  "复裁切像素。输出建立新坐标系。")
        : p.algorithm == Algorithm::CalibrationDetect
            ? QStringLiteral(
                  "原始 byte "
                  "相机图像；描述文件单位为米。标记叠加显示检测中心，不冒充亚像"
                  "素轮廓。先识别整板，ROI "
                  "筛选输出标记中心；多图标定请使用工具菜单。")
        : p.algorithm == Algorithm::ImageRectify ||
                p.algorithm == Algorithm::PlaneMeasure
            ? QStringLiteral(
                  "仅使用原始相机像素与同规格标定。平面测量假设被测点在保存的世"
                  "界平面；仿射或已校正数据拒绝重复映射。")
            : QStringLiteral(
                  "保留原始类型与有效域。导数可有负数，显示映射仅用于查看；Sobe"
                  "l 当前支持 3×3。");
    geometryInputPanel_->findChild<QLabel *>("geometryInputHint")
        ->setText(hint);
    if (auto *label = findChild<QLabel *>(
            QString("calibrationInfo_%1").arg(int(p.algorithm)))) {
      if (p.algorithm == Algorithm::CalibrationDetect || calibrationTool(p.algorithm))
        label->setText(
            QFileInfo(p.calibrationConfig["boardPath"].toString()).fileName());
      else if (!workflowModel.isEmpty())
        label->setText(workflowModel);
      else if (p.calibrationArtifact.isEmpty())
        label->setText(QStringLiteral("未加载 · 使用工具菜单完成相机标定"));
      else {
        try {
          const auto model = readCalibrationArtifact(p.calibrationArtifact);
          label->setText(QStringLiteral("%1×%2 · RMS %3 px · 标定文件已冻结")
                             .arg(model["width"].toInt())
                             .arg(model["height"].toInt())
                             .arg(model["rms"].toDouble(), 0, 'f', 3));
        } catch (...) {
          label->setText(QStringLiteral("标定资源无效"));
        }
      }
    }
  }
  bool repeated = false;
  int currentPose = -1;
  if (nodeContext() && selectedStep_ >= 0)
    for (int source : workflowAncestors(workflowLinks_, selectedStep_))
      if (source >= 0 && rangeForNode(source).iteratePose) {
        repeated = true;
        currentPose = rangeForNode(source).poseSource;
      }
  if (repeated)
    geometryInputPanel_->findChild<QLabel *>("geometryInputHint")
        ->setText(QStringLiteral("逐目标执行：对象编号在各目标内独立计算；定位"
                                 "输入自动取当前目标。"));
  for (const auto &pair : geometryFields_) {
    auto *widget = pair.first;
    if (widget->property("tool").toInt() != int(p.algorithm)) continue;
    if (p.algorithm == Algorithm::CloudMeasure && (pair.second == "pointA" || pair.second == "pointB"))
      static_cast<ParameterGrid*>(widget->parentWidget()->parentWidget())->setFieldVisible(widget, p.options.value("mode").toInt() == 1);
    if (p.algorithm == Algorithm::RegionMorph) {
      const int op = p.options.value("operation").toInt();
      const bool rect = p.options.value("shape").toInt() == 1;
      const bool visible = pair.second == "operation" ||
                           op < 4 && (pair.second == "shape" ||
                                      (pair.second == "radius" ? !rect : rect));
      widget->parentWidget()->setVisible(visible);
    }
    const auto settings = nodeContext() && selectedStep_ >= 0
                              ? rangeForNode(selectedStep_)
                              : NodeSettings{};
    const bool automatic =
        repeated &&
        (pair.second == "objectA" && settings.dataA == currentPose ||
         pair.second == "objectB" && settings.dataB == currentPose);
    widget->setEnabled(!automatic && !runQueue_.active());
    if (pair.second == "objectA" || pair.second == "objectB")
      widget->setToolTip(
          automatic
              ? QStringLiteral("定位输入使用当前目标，不使用此编号")
              : QStringLiteral("对象编号从 1 开始，逐目标时在各目标内部计算"));
    const auto value = p.options.contains(pair.second)
                           ? p.options[pair.second].toVariant()
                           : widget->property("default");
    if (auto *field = qobject_cast<QDoubleSpinBox *>(widget))
      field->setValue(value.toDouble());
    else if (auto *line = qobject_cast<QLineEdit *>(widget))
      line->setText(value.toString());
    else if (auto *choice = qobject_cast<QComboBox *>(widget))
      choice->setCurrentIndex(choice->findData(value));
  }
  for (bool second : {false, true}) {
    auto *box = second ? geometryB_ : geometryA_;
    const QSignalBlocker block(box);
    box->setVisible(geometryTool && nodeContext() &&
                    (!second || needsSecondInput(p)));
    box->setEnabled(!runQueue_.active());
    box->clear();
    if (nodeContext() && selectedStep_ >= 0)
      fillNamedInput(box, selectedStep_, second ? "ObjectB" : "ObjectA");
  }
  if (p.algorithm == Algorithm::Judge) {
    auto* field = findChild<QComboBox*>(QString("geometry_%1_field").arg(int(Algorithm::Judge)));
    const QSignalBlocker block(field);
    const QString selected = p.options.value("field").toString("value");
    ResultObject object;
    bool live = false;
    bool missingObject = false;
    const int source = nodeContext() && selectedStep_ >= 0 ? rangeForNode(selectedStep_).dataA : -2;
    if (source >= 0 && source < workflow_.size()) {
      object = numericalOutputSchema(workflow_[source]);
      if (activeImage_ >= 0 && images_[activeImage_].workflowResultVersion == images_[activeImage_].version) {
        const auto& outputs = images_[activeImage_].workflowResult.nodeOutputs;
        if (source < outputs.size() && outputs[source].state == NodeState::Success) {
          const int index = p.options.value("objectA").toInt(1) - 1;
          live = true;
          missingObject = index < 0 || index >= outputs[source].objects.size();
          object = missingObject ? ResultObject{} : outputs[source].objects[index];
        }
      }
    }
    if (source >= 0 && inputBinding(rangeForNode(selectedStep_), "ObjectA").output.startsWith("value:")) {
      const auto key = inputBinding(rangeForNode(selectedStep_), "ObjectA").output.mid(6);
      object.unit = fieldUnit(object, key);
      object.values = {{"value", object.values.value(key)}};
    }
    field->clear();
    auto keys = object.values.keys();
    if (keys.removeOne("value")) keys.prepend("value");
    for (const auto& key : keys) {
      const auto unit = fieldUnit(object, key);
      field->addItem(fieldLabel(key) + (unit.isEmpty() ? QString() : " / " + unit), key);
      field->setItemData(field->count()-1, key, Qt::ToolTipRole);
    }
    int row = field->findData(selected);
    if (row < 0) {
      const auto text = source < 0 ? QStringLiteral("先选择输入 A")
          : missingObject ? QStringLiteral("对象编号不存在")
          : QStringLiteral("指标已失效：%1").arg(fieldLabel(selected));
      field->insertItem(0, text, selected);
      row = 0;
      field->setItemData(0, QColor("#c95757"), Qt::ForegroundRole);
    }
    field->setCurrentIndex(row);
    field->setEnabled(!runQueue_.active() && source >= 0 && !keys.isEmpty());
    field->setToolTip(live ? QStringLiteral("来自当前输入 A 所选对象的实际输出。失效指标须重新选择。")
                           : QStringLiteral("运行前显示所选上游参数对应的输出；运行后按对象实际结果更新。"));
  }
  if(p.algorithm==Algorithm::StereoRectify)if(auto* help=findChild<QLabel*>(QString("captureInputHelp_%1").arg(int(p.algorithm))))
    help->setText(QStringLiteral("分别选择左、右图像输出，可以来自同一多视输入，也可以来自两个图像处理节点；在此节点选择双目标定。"));
  if(p.algorithm==Algorithm::ImageInput)if(auto* mode=findChild<QComboBox*>("geometry_36_inputMode")) {
    bool populated=false;const auto key=p.options.value("binding").toString();for(int i=0;i<images_.size();++i){const auto& d=i==activeImage_?document_:images_[i].document;populated|=d.sourceInputs.contains(key)&&!d.sourceInputs[key].frames.isEmpty();}
    mode->setEnabled(!populated&&!runQueue_.active());mode->setToolTip(populated?QStringLiteral("先移除此输入的数据，再切换输入类型"):QStringLiteral("单图、多视图组或浮点 XYZ 坐标图"));
    if(auto* unit=findChild<QComboBox*>("geometry_36_xyzUnit"))unit->setEnabled(xyzInput(p));
  }
  if(sourceTool(p.algorithm))resetParameters_->hide();
  if (sourceTool(p.algorithm))
    geometryInputPanel_->findChild<QLabel*>("geometryInputHint")->setText(QStringLiteral("按当前数据绑定输入；在数据资源栏配置每个数据的文件。"));
  if (captureTool(p.algorithm)) {
    const auto source=p.calibrationConfig["sourcePath"].toString();
    const bool frozen=!p.calibrationArtifact.isEmpty();
    const auto text=!workflowModel.isEmpty()?workflowModel:source.isEmpty()
        ? QStringLiteral("请选择模型文件") : QDir::toNativeSeparators(source);
    if(auto* label=findChild<QLabel*>(QString("captureModelPath_%1").arg(int(p.algorithm)))) {
      label->setText(text);label->setToolTip(frozen?text+QStringLiteral("\n已冻结入工程；运行不依赖来源文件仍然存在。") : text);
    }
    if(auto* button=findChild<QPushButton*>(QString("captureModel_%1").arg(int(p.algorithm))))
      button->setToolTip(text);
  }
  syncing_ = prior;
}
void MainWindow::bindGeometryInput(int target, int source, bool second) {
  // Compatibility entry point for existing commands and older automation.
  // Once a concrete output is known, use the same transaction as the named UI.
  if (target >= 0 && target < workflow_.size() && source >= 0 && source < workflow_.size()) {
    const auto slot = second ? QString("ObjectB") : QString("ObjectA");
    const auto choices = compatibleOutputs(workflow_[source], workflow_[target], slot);
    QString preferred;
    const auto tool = workflow_[target].algorithm;
    if (tool == Algorithm::StereoRectify || tool == Algorithm::StereoDisparity) preferred = second ? "right" : "left";
    else if (tool == Algorithm::DisparityFilter) preferred = second ? "score" : "disparity";
    else if (tool == Algorithm::StereoReconstruct) preferred = second ? "geometry" : "disparity";
    else preferred = "result";
    for (const auto& port : choices) if (port.key == preferred) {
      bindNamedInput(target, slot, source, port.key); return;
    }
    if (choices.size() == 1) { bindNamedInput(target, slot, source, choices.first().key); return; }
  }
  if (runQueue_.active() || target < 0 || target >= workflow_.size() || source < 0 ||
      source >= workflow_.size() ||
      !acceptsData(workflow_[target].algorithm,
                   outputType(workflow_[source]),
                   second))
    return;
  if((captureTool(workflow_[target].algorithm)||calibrationTool(workflow_[target].algorithm)) && !sourceTool(workflow_[source].algorithm)) return;
  auto links = workflowLinks_;
  if (captureTool(workflow_[target].algorithm)) {
    const auto prior=rangeForNode(target);
    const int old=second ? prior.dataB : prior.dataA;
    if(old>=0 && old!=(second ? prior.dataA : prior.dataB)) links[target].removeAll(old);
  }
  if (!workflowAncestors(links, target).contains(source)) links[target].append(source);
  Document graph;
  graph.workflow = workflow_;
  graph.workflowInputs = workflowInputs_;
  graph.workflowLinks = links;
  try {
    workflowOrder(graph, true);
  } catch (...) {
    return;
  }
  EditGuard edit(*this, QStringLiteral("绑定类型化输入"));
  if(!confirmInputMerge(source,target))return;
  if(workflow_[target].algorithm==Algorithm::StereoRectify && !second) {
    const bool multi=multiViewInput(workflow_[source]);
    workflow_[target].options["multiView"]=multi?1:0;
    if(multi) { const auto previous=links[target];
      for(int upstream:previous)if(upstream>=0&&upstream!=source&&sourceTool(workflow_[upstream].algorithm))links[target].removeAll(upstream);
      sharedRanges_[target].dataB=-2;
      for(auto& item:images_)if(target<item.document.nodeSettings.size())item.document.nodeSettings[target].dataB=-2;
      if(target<document_.nodeSettings.size())document_.nodeSettings[target].dataB=-2;
    }
  }
  workflowLinks_ = links;
  (second ? sharedRanges_[target].dataB : sharedRanges_[target].dataA) = source;
  // Data bindings are node configuration; no per-image silent overrides.
  for (auto &image : images_)
    if (target < image.document.nodeSettings.size())
      (second ? image.document.nodeSettings[target].dataB
              : image.document.nodeSettings[target].dataA) = source;
  if (target < document_.nodeSettings.size())
    (second ? document_.nodeSettings[target].dataB
            : document_.nodeSettings[target].dataA) = source;
  selectedStep_ = target;
  if (second && workflow_[target].algorithm == Algorithm::CloudSelect)
    workflow_[target].options["selection"] = 1;
  workflowChanged();
  syncGeometryUi();
}
}  // namespace workbench
