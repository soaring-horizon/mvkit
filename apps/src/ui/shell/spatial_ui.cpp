// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include <QAction>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSettings>
#include <QToolButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "ui/shell/main_window.hpp"
#include "ui/widgets/parameter_spin_box.hpp"
#include "ui/views/model_view.hpp"
#include "ui/widgets/responsive_widgets.hpp"
#include "model/spatial_data.hpp"
namespace workbench {
bool MainWindow::addStereoPairs(const QStringList& left,
                                const QStringList& right,
                                const QString& calibration) {
  if (busy() || left.isEmpty() || left.size() != right.size()) return false;
  if(calibration.isEmpty() && activeInputNode()>=0 && multiViewInput(workflow_[activeInputNode()]))return guard(QStringLiteral("添加多视输入"),[&]{QVector<SourceData> cells;QStringList names;
    loadResources(QStringLiteral("读取多视图组…"),[&](const std::atomic_bool& cancelled){for(int i=0;i<left.size();++i){if(cancelled.load())return;cells.append(loadMultiViewData({left[i],right[i]},{"left","right"},&cancelled));names.append(QFileInfo(left[i]).completeBaseName()+" / "+QFileInfo(right[i]).completeBaseName());}});
    if(!importSourceSamples(cells,names))throw std::runtime_error("未添加输入数据");
  },true);
  return guard(QStringLiteral("导入双目图组"), [&] {
    QVector<ImageResource> loaded;
    const auto parameters=workbenchParameters_;
    loadResources(QStringLiteral("读取左右图…"),[&loaded,left,right,calibration,parameters](const std::atomic_bool& cancelled){
    const auto artifact = calibration.isEmpty() ? QByteArray() : readStereoCalibration(calibration);
    const auto profile = artifact.isEmpty() ? QJsonObject() : stereoCalibration(artifact);
    for (int i = 0; i < left.size(); ++i) {
      if(cancelled.load())return;
      auto a = loadImage(left[i]), b = loadImage(right[i]);
      if (a.preview.size() != b.preview.size() ||
          a.preview.width() * a.preview.height() > 1000000)
        throw std::runtime_error("左右图须同尺寸，首版每组最多 100 万像素");
      if (a.imagePath == b.imagePath)
        throw std::runtime_error("左右图不能引用同一个文件");
      for (const auto* image : {&a.image, &b.image}) {
        if (image->Channels() != 1 && image->Channels() != 3)
          throw std::runtime_error("双目图像要求灰度或 RGB");
        for (size_t channel = 0; channel < image->Channels(); ++channel)
          if (image->Type(channel) != mv::MvPixelType::UInt8)
            throw std::runtime_error("双目输入须为原始 byte 图像");
      }
      const auto ca = profile["leftCamera"].toArray(),
                 cb = profile["rightCamera"].toArray();
      if (!artifact.isEmpty() && (a.preview.width() != ca[7].toInt() ||
          a.preview.height() != ca[8].toInt() ||
          b.preview.width() != cb[7].toInt() ||
          b.preview.height() != cb[8].toInt()))
        throw std::runtime_error("图像规格与双目标定不匹配");
      a.captureId = QUuid::createUuid().toString(QUuid::WithoutBraces);
      a.rightPath = b.imagePath;
      a.rightImage = b.image;
      a.rightPreview = b.preview;
      a.rightPixelKey = b.pixelKey;
      a.stereoArtifact = artifact;
      a.captureName = QFileInfo(a.imagePath).completeBaseName() + " / " +
                      QFileInfo(b.imagePath).completeBaseName();
      a.stereoName = QFileInfo(calibration).completeBaseName();
      a.parameters = parameters;
      loaded.append({a.captureId, std::move(a), {}, 0, true});
    }
    });
    EditGuard edit(*this, QStringLiteral("导入双目图组"));
    storeActive();
    const int first = images_.size();
    for (auto& resource : loaded) images_.append(std::move(resource));
    activateImage(first);
    view2d_->setChecked(true);
    view3d_->setChecked(true);
    updateViews();
    setDirty(true);
  });
}
void MainWindow::showStereoImport() {
  if (busy() || activeInputNode()<0 || !multiViewInput(workflow_[activeInputNode()])) return;
  auto* dialog = new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setModal(true);
  dialog->setObjectName("stereoImageImport");
  dialog->setWindowTitle(QStringLiteral("添加双目图像"));
  dialog->resize(800, 440);
  auto* layout = new QVBoxLayout(dialog);
  auto* guide =
      new QLabel(QStringLiteral("每行是一组同一时刻的左、右图。请核对配对顺序；"
                                "标定在流程的“双目校正”节点配置，此处只添加图像。"),
                 dialog);
  guide->setWordWrap(true);
  layout->addWidget(guide);
  auto left = std::make_shared<QStringList>(),
       right = std::make_shared<QStringList>();
  auto* row = new QHBoxLayout;
  auto* l = new QPushButton(QStringLiteral("选择左图…"), dialog);
  auto* r = new QPushButton(QStringLiteral("选择右图…"), dialog);
  l->setObjectName("chooseStereoLeft"); r->setObjectName("chooseStereoRight");
  row->addWidget(l);
  row->addWidget(r);
  layout->addLayout(row);
  auto* table = new QTableWidget(dialog);
  table->setColumnCount(2);
  table->setHorizontalHeaderLabels(
      {QStringLiteral("左相机"), QStringLiteral("右相机")});
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  layout->addWidget(table, 1);
  auto* status = new QLabel(dialog);
  status->setWordWrap(true);
  layout->addWidget(status);
  auto* buttons = new QHBoxLayout;
  buttons->addStretch();
  auto* apply = new QPushButton(QStringLiteral("导入图组"), dialog);
  apply->setProperty("primary", true);
  apply->setObjectName("addStereoImages");
  auto* cancel = new QPushButton(QStringLiteral("取消"), dialog);
  buttons->addWidget(apply);
  buttons->addWidget(cancel);
  layout->addLayout(buttons);
  auto update = [=] {
    table->setRowCount(std::max(left->size(), right->size()));
    for (int i = 0; i < table->rowCount(); ++i)
      for (int col = 0; col < 2; ++col) {
        auto path = (col ? right : left)->value(i);
        auto* item = new QTableWidgetItem(QFileInfo(path).fileName());
        item->setToolTip(path);
        table->setItem(i, col, item);
      }
    apply->setEnabled(!left->isEmpty() && left->size() == right->size());
    status->setText(QStringLiteral("左图 %1 · 右图 %2 · 标定请在双目校正节点配置")
                        .arg(left->size())
                        .arg(right->size()));
  };
  for (bool isRight : {false, true})
    connect(isRight ? r : l, &QPushButton::clicked, dialog, [=] {
      auto files = QFileDialog::getOpenFileNames(
          dialog,
          isRight ? QStringLiteral("选择右相机图像")
                  : QStringLiteral("选择左相机图像"),
          {}, QStringLiteral("图像 (*.png *.bmp *.tif *.tiff *.jpg)"));
      if (!files.isEmpty()) *(isRight ? right : left) = files;
      update();
    });
  connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
  connect(apply, &QPushButton::clicked, dialog, [=] {
    bool installed=false;
    guard(QStringLiteral("添加多视输入"),[&]{QVector<SourceData> cells;QStringList names;
      loadResources(QStringLiteral("读取多视图组…"),[&](const std::atomic_bool& cancelled){for(int i=0;i<left->size();++i){if(cancelled.load())return;cells.append(loadMultiViewData({(*left)[i],(*right)[i]},{"left","right"},&cancelled));names.append(QFileInfo((*left)[i]).completeBaseName()+" / "+QFileInfo((*right)[i]).completeBaseName());}});
      installed=importSourceSamples(cells,names);
    },true);
    if(installed)dialog->accept();else status->setText(lastError());
  });
  update();
  dialog->show();
}
void MainWindow::setupSpatialUi() {
  if (persistSettings_) {
    QSettings settings;
    depthScaleMode_ = std::clamp(settings.value("view/depthMode", 0).toInt(), 0, 2);
    const double lower = settings.value("view/depthLowerM", 0).toDouble();
    const double upper = settings.value("view/depthUpperM", 2).toDouble();
    if (std::isfinite(lower) && std::isfinite(upper) && lower < upper && lower >= -1e6 && upper <= 1e6)
      manualDepthScale_ = {lower, upper, true};
  }
  spatialChoice_ = new QComboBox(nodeOutputPanel_);
  spatialChoice_->setObjectName("spatialOutputChoice");
  spatialChoice_->setToolTip(
      QStringLiteral("查看具名输出；切换显示不重新计算"));
  auto* layout = qobject_cast<QVBoxLayout*>(nodeOutputPanel_->layout());
  layout->removeWidget(nodeView_);
  spatialViews_ = new ViewSplitter(Qt::Horizontal, nodeOutputPanel_);
  spatialViews_->setObjectName("spatialOutputViews");
  spatialViews_->addWidget(nodeView_);
  nodeModelView_ = new ModelView(spatialViews_);
  nodeModelView_->setObjectName("nodeModelView");
  nodeModelView_->setCompact(true);
  spatialViews_->addWidget(nodeModelView_);
  layout->addWidget(spatialViews_, 1);
  nodeModelView_->hide();
  spatialWorkbenchChoice_ = new QComboBox(imagePane_);
  spatialWorkbenchChoice_->setObjectName("spatialWorkbenchOutput");
  for (bool workbench : {false, true}) {
    auto* parent = workbench ? views_->parentWidget() : nodeOutputPanel_;
    auto* choice = workbench ? spatialWorkbenchChoice_ : spatialChoice_;
    auto* header = new SpatialToolbar(choice, parent);
    auto* color = header->color;
    color->setObjectName(workbench ? "spatialWorkbenchColor" : "spatialColor");
    auto* scale = header->scale;
    scale->setObjectName(workbench ? "workbenchDepthScaleButton" : "depthScaleButton");
    scale->setText(QStringLiteral("色阶"));
    scale->setAccessibleName(QStringLiteral("深度色阶范围"));
    if (workbench) workbenchDepthScaleButton_ = scale; else depthScaleButton_ = scale;
    connect(scale, &QToolButton::clicked, this, [this] {
      const auto key = (workflowMode_ ? spatialChoice_ : spatialWorkbenchChoice_)->currentData().toString();
      if (spatialView_ && spatialView_->rasters.value(key).semantic == "residual") showResidualDisplay();
      else showDepthScale();
    });
    auto* legend = header->legend;
    legend->setObjectName(workbench ? "spatialWorkbenchLegend" : "spatialLegend");
    if (workbench) { spatialWorkbenchHeader_ = header; spatialWorkbenchColor_ = color; spatialWorkbenchLegend_ = legend; }
    else { spatialHeader_ = header; spatialColor_ = color; spatialLegend_ = legend; }
    qobject_cast<QVBoxLayout*>(parent->layout())->insertWidget(0, header);
    header->hide();
    connect(color, QOverload<int>::of(&QComboBox::activated), this, [this, workbench](int index) {
      const QSignalBlocker block(workbench ? spatialColor_ : spatialWorkbenchColor_);
      (workbench ? spatialColor_ : spatialWorkbenchColor_)->setCurrentIndex(index);
      selectSpatialRaster();
    });
  }
  for (auto* choice : {spatialChoice_, spatialWorkbenchChoice_}) {
    choice->hide();
    connect(
        choice, QOverload<int>::of(&QComboBox::activated), this,
        [this, choice] {
          if (choice == spatialChoice_) {
            QSignalBlocker block(spatialWorkbenchChoice_);
            spatialWorkbenchChoice_->setCurrentIndex(choice->currentIndex());
          } else {
            QSignalBlocker block(spatialChoice_);
            spatialChoice_->setCurrentIndex(choice->currentIndex());
          }
          const int viewed=viewNodeIndex();
          if(viewed>=0&&viewed<workflow_.size()&&sourceTool(workflow_[viewed].algorithm))
            inputPreviewViews_[workflow_[viewed].options["binding"].toString()]=choice->currentData().toString();
          selectSpatialRaster();
        });
  }
  for (auto* viewer : {view_, nodeView_})
    connect(viewer, &ImageView::pixelPicked, this, [this](int row, int col) {
      if (!spatialView_ || activeImageView()->pixelSourceAt(row, col) ==
                               ImageView::PixelSource::None)
        return;
      auto* c = workflowMode_ ? spatialChoice_ : spatialWorkbenchChoice_;
      if (c->currentData().toString() == "right") return;
      modelView_->highlightPixel(row, col);
      nodeModelView_->highlightPixel(row, col);
      activeImageView()->setLinkedPixel(row, col);
    });
  for (auto* viewer : {modelView_, nodeModelView_}) {
    viewer->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(
        viewer, &QWidget::customContextMenuRequested, this,
        [this, viewer](QPoint at) {
          if (!spatialView_ || spatialView_->points.isEmpty() || busy())
            return;
          int ordinal = viewer->property("selectedOrdinal").toInt();
          QMenu menu(viewer);
          if (document_.parameters.algorithm == Algorithm::CloudMeasure &&
              document_.parameters.options.value("mode").toInt() == 1 &&
              !viewPinnedElsewhere() && ordinal >= 1) {
          auto* a =
              menu.addAction(QStringLiteral("已选点 %1 → 测量 A").arg(ordinal));
          auto* b =
              menu.addAction(QStringLiteral("已选点 %1 → 测量 B").arg(ordinal));
          for (bool second : {false, true})
            connect(second ? b : a, &QAction::triggered, this,
                    [this, ordinal, second] {
                      auto* field = findChild<QDoubleSpinBox*>(
                          second ? "geometry_30_pointB" : "geometry_30_pointA");
                      if (field) field->setValue(ordinal);
                    });
            menu.addSeparator();
          }
          auto* save = menu.addAction(QStringLiteral("导出完整点云 / mm…"));
          connect(save, &QAction::triggered, this, [this] {
            const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出点云"),
                "point-cloud.ply", QStringLiteral("PLY 点云 (*.ply)"));
            if (!path.isEmpty()) exportObservedPointCloud(path);
          });
          menu.exec(viewer->mapToGlobal(at));
        });
  }
  for (auto* viewer : {modelView_, nodeModelView_})
    connect(viewer, &ModelView::pixelPicked, this, [this](int row, int col) {
      if (!spatialView_) return;
      const auto key = spatialView_->rasters.contains("fitResidual")
                           ? QString("fitResidual") : spatialView_->rasters.contains("depth")
                           ? QString("depth")
                           : QString("left");
      int index = spatialChoice_->findData(key);
      spatialChoice_->setCurrentIndex(index);
      spatialWorkbenchChoice_->setCurrentIndex(index);
      selectSpatialRaster();
      activeImageView()->setLinkedPixel(row, col);
    });
}
void MainWindow::refreshSpatialView(
    const std::shared_ptr<const SpatialData>& payload) {
  if (!spatialChoice_) return;
  bool keepChoice = spatialView_ && payload &&
                    spatialView_->primary == payload->primary;
  bool changed = spatialView_ != payload;
  spatialView_ = payload;
  if (changed) {
    spatialPreviews_.clear();
    auto key =
        keepChoice ? spatialChoice_->currentData().toString() : QString();
    const int viewed=viewNodeIndex();
    if(viewed>=0&&viewed<workflow_.size()&&sourceTool(workflow_[viewed].algorithm))
      key=inputPreviewViews_.value(workflow_[viewed].options["binding"].toString(),key);
    for (auto* c : {spatialChoice_, spatialWorkbenchChoice_}) {
      QSignalBlocker block(c);
      c->clear();
      if (payload)
        for (auto it = payload->rasters.begin(); it != payload->rasters.end();
             ++it)
          c->addItem(spatialDisplayLabel(*it), it.key());
      int preferred = c->findData(key);
      if (preferred < 0 && payload) preferred = c->findData(payload->primary);
      c->setCurrentIndex(preferred);
      c->setVisible(bool(payload) &&
                    (c == spatialChoice_ ? workflowMode_ : !workflowMode_));
    }
    view_->clearLinkedPixel();
    nodeView_->clearLinkedPixel();
  }
  spatialChoice_->setVisible(payload && workflowMode_);
  spatialWorkbenchChoice_->setVisible(payload && !workflowMode_);
  spatialHeader_->setVisible(payload && workflowMode_);
  spatialWorkbenchHeader_->setVisible(payload && !workflowMode_);
  modelView_->setProperty("captureView", !document_.captureId.isEmpty());
  nodeModelView_->setProperty("captureView", !document_.captureId.isEmpty());
  modelView_->setSpatial(payload);
  nodeModelView_->setSpatial(payload);
  if (payload) selectSpatialRaster();
  updateViews();
}
void MainWindow::selectSpatialRaster() {
  if (!spatialView_) return;
  auto* choice = workflowMode_ ? spatialChoice_ : spatialWorkbenchChoice_;
  auto key = choice->currentData().toString();
  if (!spatialView_->rasters.contains(key)) return;
  const auto& r = spatialView_->rasters[key];
  auto* target = activeImageView();
  const auto oldKey = target->property("spatialRaster").toString();
  const bool same =
      target->property("spatialGroup").toString() == spatialView_->group &&
      target->property("spatialFrame").toString() == r.frame;
  if (oldKey != key && (oldKey == "right" || key == "right")) {
    target->clearLinkedPixel();
    modelView_->highlightPixel(-1, -1);
    nodeModelView_->highlightPixel(-1, -1);
  }
  target->setProperty("spatialGroup", spatialView_->group);
  target->setProperty("spatialFrame", r.frame);
  target->setProperty("spatialRaster", key);
  const bool intensity = r.semantic == "intensity";
  const bool color = (workflowMode_ ? spatialColor_ : spatialWorkbenchColor_)->currentIndex() == 0;
  const bool depth = r.semantic == "depth" && r.unit == "m";
  const bool residual = r.semantic == "residual";
  int ready = 0, selected = 0;
  const auto scale = depth && depthScaleMode_ != 0 ? depthScaleRange(depthScaleMode_, &ready, &selected) : ScalarRange{};
  const auto displayKey = key + (color ? "/color" : "/gray") + (scale.available
      ? QString("/%1/%2").arg(scale.lower, 0, 'g', 17).arg(scale.upper, 0, 'g', 17) : QString());
  if (!spatialPreviews_.contains(displayKey)) {
    if (spatialPreviews_.size() >= 24) spatialPreviews_.clear();
    spatialPreviews_[displayKey] = spatialPreview(*spatialView_, key, color, scale);
  }
  auto display = spatialPreviews_[displayKey];
  if (residual) {
    int outside = 0;
    const auto& image = r.image;
    for (const auto& point : spatialView_->points) {
      const auto* row = reinterpret_cast<const float*>(static_cast<const char*>(image.Data()) + point.row * image.Stride());
      const bool exceeds = std::abs(row[point.column]) > residualTolerance_;
      outside += exceeds;
      if (residualOnlyOutside_ && !exceeds) display.image.setPixel(point.column, point.row, qRgba(0, 0, 0, 0));
      else if (exceeds && !residualOnlyOutside_) display.image.setPixelColor(point.column, point.row, QColor("#ed476f"));
    }
    display.legend += QStringLiteral(" · |残差| > %1 mm：%2 / %3 点%4")
        .arg(residualTolerance_, 0, 'g', 6).arg(outside).arg(spatialView_->points.size())
        .arg(residualOnlyOutside_ ? QStringLiteral(" · 仅看超差") : QStringLiteral(" · 超差为红色"));
    target->setProperty("residualOutsideCount", outside);
    target->setProperty("residualToleranceMM", residualTolerance_);
  }
  for (auto* viewer : {modelView_, nodeModelView_})
    viewer->setResidualDisplay(residual ? display.image : QImage{}, residualTolerance_, residualOnlyOutside_);
  const auto modeLabel = depthScaleMode_ == 1 ? QStringLiteral("统一") : depthScaleMode_ == 2 ? QStringLiteral("手动") : QStringLiteral("自动");
  for (auto* button : {depthScaleButton_, workbenchDepthScaleButton_}) {
    button->setText(residual ? QStringLiteral("残差显示") : modeLabel);
    button->setAccessibleName(residual ? QStringLiteral("残差显示设置") : QStringLiteral("深度色阶范围"));
    button->setToolTip(depthScaleMode_ == 1
        ? QStringLiteral("跨组统一：已选 %1 组，当前节点 %2 组有有效新结果。%3\n点击调整深度色阶，原始数值和点云保持。")
              .arg(selected).arg(ready).arg(scale.available ? QString() : QStringLiteral("暂用当前自动范围。"))
        : QStringLiteral("深度色阶：%1。点击设置自动、跨组统一或手动 mm 范围。").arg(modeLabel));
    if (residual) button->setToolTip(QStringLiteral("设置绝对残差容差；可仅查看超差点。只影响显示，不删除点或重新拟合。"));
  }
  for (auto* header : {spatialHeader_, spatialWorkbenchHeader_})
    static_cast<SpatialToolbar*>(header)->setDisplayModes(intensity, depth, residual);
  for (auto* legend : {spatialLegend_, spatialWorkbenchLegend_}) {
    legend->setText(residual ? display.legend : r.semantic == "reason" && key == "decodeReason" ? QStringLiteral("移动鼠标查看拒绝原因") : display.legend.section(QStringLiteral(" · "), 0, 0));
    legend->setToolTip(display.legend);
  }
  const auto reference = spatialReferenceKey(*spatialView_);
  const auto background = spatialView_->rasters.constFind(reference);
  target->setImage(
      intensity ? makePreview(r.image).image
                : background != spatialView_->rasters.cend() ? makePreview(background->image).image : QImage{},
      same);
  target->setResultImage(intensity ? QImage{} : display.image,
                         intensity ? QPainterPath{} : display.domain);
  target->setPixelText(intensity ? QStringLiteral("Row — · Col — · 原始图像")
                                 : QStringLiteral("Row — · Col — · 结果图像"));
  target->setProperty("spatialColor", color && !intensity);
  target->setProperty("spatialLegend", display.legend);
  target->setProperty("depthScaleMode", depthScaleMode_);
  target->setProperty("depthScaleGroups", ready);
  if (depth) {
    const auto shown = scale.available ? scale : cachedSpatialRange(spatialView_);
    target->setProperty("depthScaleLowerMM", shown.lower * 1000);
    target->setProperty("depthScaleUpperMM", shown.upper * 1000);
  }
  const int viewed=viewNodeIndex();
  const bool calibrationMarks=viewed>=0&&viewed<workflow_.size()&&workflow_[viewed].algorithm==Algorithm::StereoCalibration;
  auto objects = key == "right"&&!calibrationMarks ? QVector<ResultObject>{}
                                : (nodeContext() ? resultForNode(viewed).objects : result_.objects);
  if(calibrationMarks) {
    // Keep result-table indices stable while hiding the other camera's marks.
    for(auto& object:objects)if(object.type!="Point"||int(object.values.value("cameraIndex"))!=(key=="right"?1:0)) {
      object={};object.type="Value";
    }
  } else objects.erase(std::remove_if(objects.begin(),objects.end(),[](const ResultObject& object){
    return object.type=="Value"&&object.overlay.isEmpty();
  }),objects.end());
  target->setResults(objects);
  target->setImageName((r.label==QStringLiteral("左目")||r.label==QStringLiteral("右目"))?r.label+" · "+QFileInfo(r.frame).fileName():spatialDisplayLabel(r));
  choice->setToolTip(
      spatialDisplayLabel(r) +
      (r.unit == "m" ? QStringLiteral("\n显示单位 mm；底层 XYZ 图原始单位 m，数据与导出不变。\n") : QString()) +
        ((r.label==QStringLiteral("左目")||r.label==QStringLiteral("右目"))
             ? QStringLiteral(" · 原始相机像素坐标\n视图切换不重新计算\n")
             : key == "right"
           ? QStringLiteral(" · 校正右图像素坐标\n不关联左图像素或三维点\n")
           : reference == "depth" ? QStringLiteral(" · 原始 XYZ 像素坐标\n视图切换不重新计算\n")
           : reference == "reference" ? QStringLiteral(" · 结构光相机像素坐标\n视图切换不重新计算\n")
           : QStringLiteral(" · 校正左相机坐标\n视图切换不重新计算\n")) +
      r.frame);
  target->setLayerVisible(ImageView::Layer::Original, true);
  target->setLayerVisible(ImageView::Layer::Processed, !intensity);
  target->setRoiEditingEnabled(document_.parameters.algorithm ==
                                   Algorithm::CloudSelect &&
                               key != "right" && !viewPinnedElsewhere());
  nodeInputImage_ = r.image;
  nodeInputDomain_ = imageDomainPath(r.image);
  nodeProcessedImage_ = {};
}
void MainWindow::showResidualDisplay() {
  auto* dialog = new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setObjectName("residualDisplayDialog");
  dialog->setWindowTitle(QStringLiteral("拟合残差显示"));
  auto* layout = new QVBoxLayout(dialog);
  auto* help = new QLabel(QStringLiteral("平面残差沿拟合单位法向；球面残差在球外为正、球内为负。\n超差按 |残差| > 容差判断，只影响显示，不改变点云、拟合或判定结果。"), dialog);
  help->setWordWrap(true); layout->addWidget(help);
  auto* row = new QHBoxLayout;
  row->addWidget(new QLabel(QStringLiteral("绝对残差容差 / mm"), dialog));
  auto* tolerance = new ParameterDoubleSpinBox(dialog);
  tolerance->setObjectName("residualTolerance");
  tolerance->setRange(0, 1e12); tolerance->setDecimals(6);
  tolerance->setValue(residualTolerance_); tolerance->setKeyboardTracking(false);
  row->addWidget(tolerance); layout->addLayout(row);
  auto* only = new QCheckBox(QStringLiteral("仅查看超差点"), dialog);
  only->setObjectName("residualOnlyOutside"); only->setChecked(residualOnlyOutside_);
  layout->addWidget(only);
  auto* buttons = new QHBoxLayout;
  buttons->addStretch();
  auto* cancel = new QPushButton(QStringLiteral("取消"), dialog);
  auto* apply = new QPushButton(QStringLiteral("应用"), dialog);
  apply->setObjectName("applyResidualDisplay");
  buttons->addWidget(cancel); buttons->addWidget(apply); layout->addLayout(buttons);
  connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
  connect(apply, &QPushButton::clicked, dialog, [this, dialog, tolerance, only] {
    residualTolerance_ = tolerance->value(); residualOnlyOutside_ = only->isChecked();
    selectSpatialRaster(); dialog->accept();
  });
  dialog->setModal(true); dialog->resize(440, 210); dialog->show();
}
}  // namespace workbench
