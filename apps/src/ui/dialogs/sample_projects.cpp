// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/dialogs/sample_projects.hpp"
#include "io/input_migration.hpp"
#include "io/project_io.hpp"

#include <QApplication>
#include <QDateTime>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QUuid>
#include <QVBoxLayout>
#include <stdexcept>

#include "ui/shell/main_window.hpp"
namespace workbench {
namespace {
int sampleCategory(const QString& id) {
  return id == "stereo" || id == "structured" ||
                 id == "synthetic-stereo-part" || id == "synthetic-structured-part"
             ? 2 : 1;
}
}
QVector<SampleProject> sampleProjects() {
  QVector<SampleProject> samples{
      {"count", QStringLiteral("区域分割与数量判定"), "getting-started",
       "count.mvproj",
       QStringLiteral("区域分割 → 区域形态学 → 区域筛选 → 区域特征 → 公差判定"),
       QStringLiteral("1. 点击“连续运行”，处理两张已选图。\n2. "
                      "点选分割、筛选和特征节点，查看区域与数量。\n3. "
                      "把判定上限由 3 改为 4，观察第二张图从 NG 变为 OK。"),
       QStringLiteral("three-parts.png：3 个目标，OK。four-parts.png：4 "
                      "个目标，NG。小亮点应被面积筛选移除。")},
      {"brightness", QStringLiteral("移动工件的亮度检测"), "brightness", "brightness.mvproj",
       QStringLiteral("图像输入 → 区域分割定位 → 灰度统计 → 公差判定"),
       QStringLiteral("点击连续运行。查看两张图中分割区域如何跟随移动的工件，再查看区域内的平均亮度。"),
       QStringLiteral("自建灰度图的目标均值分别为 180 和 125，判定区间 160–200，依次得到 OK、NG。")},
      {"processing", QStringLiteral("图像处理与正逆仿射"),
       "image-calibration-review", "image-processing.mvproj",
       QStringLiteral("灰度形态学 → 仿射变换 → 梯度提取 → 图像增强"),
       QStringLiteral(
           "1. 按 F5 运行当前数据。\n2. "
           "选择仿射变换，调整旋转角、中心或正向/逆向。\n3. 选择梯度提取，切换 "
           "Sobel 与高斯导数；用左右图层和卷帘查看输入/结果。"),
       QStringLiteral("自建彩色图案经过旋转及梯度处理；切换方法会实时重算。逆向"
                      "使用当前参数矩阵的逆，不能恢复裁切像素。")},
      {"measurement", QStringLiteral("匹配定位与孔径、孔边距"),
       "measurement-review", "measurement.mvproj",
       QStringLiteral("形状匹配 → 跟随 ROI → 边缘提取 → 轮廓筛选 → 线/圆拟合 → "
                      "测量 → 判定"),
       QStringLiteral(
           "1. 点击“连续运行”，比较原位、旋转平移、孔径超差三张图。\n2. "
           "点选匹配、边缘、圆拟合和几何测量，沿连线查看每一步。\n3. "
           "在工作台的节点算子中选同一节点调试，参数与工作流互通。"),
       QStringLiteral("前两张孔径/孔边距通过；第三张孔径 "
                      "NG。已附冻结模板，无需重新训练；默认长度为 px。")},
      {"ellipse", QStringLiteral("椭圆拟合与质量检查"),
       "second-batch-review", "ellipse.mvproj",
       QStringLiteral("边缘提取 → 轮廓处理 → 轮廓筛选 → 椭圆拟合 → 公差判定"),
       QStringLiteral("1. 按 F5 运行当前数据，选中椭圆拟合。\n2. "
                      "开启拟合质量，调整显示容差，查看采样点和残差。\n3. "
                      "降低最大 RMS，观察有拟合证据但不放行下游的情况。"),
       QStringLiteral("拟合长轴直径约 110 px，判定范围 105–115 "
                      "px。质量显示颜色取决于用户设定的容差。")},
      {"scale", QStringLiteral("像素到毫米的比例换算"),
       "third-batch-review", "calibration.mvproj",
       QStringLiteral("定位与拟合 → 几何测量 → 像素比例换算 → 公差判定"),
       QStringLiteral("1. 按 F5 运行当前数据，选择像素比例换算节点。\n2. "
                      "查看参考像素长度 100 px、实际长度 10 mm 的换算。\n3. "
                      "查看判定节点的单位和上下限，切换图像重新运行。"),
       QStringLiteral("原位支架孔径约 4.8 "
                      "mm。仅演示固定成像平面的等比例换算；完整相机去畸变/"
                      "平面标定使用工具菜单的相机标定。")},
      {"stereo", QStringLiteral("双目重建与二维三维联动"), "stereo-review",
       "stereo.mvproj",
       QStringLiteral("视差过滤 → 双目重建 / 校正左图分割 → 区域提取点云 → 三维测量"),
       QStringLiteral("1. 点击“连续运行”，两组输入分别约为 2 m、1 m 平面。\n2. "
                      "选择双目重建，在输出下拉框查看左右校正图、视差、质量、XY"
                      "Z 与有效点。\n3. 在点云选取节点的校正左图/深度图画 "
                      "ROI，或调整分割阈值观察区域 B 选点。\n4. 在视差过滤中调整代价和连通面积。\n5. 数据菜单可导出点云；迁移工程时在“另存工程”勾选复制原始数据。"),
       QStringLiteral("两组自建解析输入，三维点与测量为 mm，XYZ 图为 "
                      "m。已有冻结双目标定，无需训练。深度范围不是相对平面的高"
                      "度；这是算法入门示例，不是实际设备标定。")},
      {"structured", QStringLiteral("结构光解码与三维重建"), "structured-review", "structured.mvproj",
       QStringLiteral("有序曝光 → 结构光解码 → 结构光重建 → 区域选点 → 三维测量"),
       QStringLiteral("1. 点击“连续运行”，两组 1 m／2 m 平面各含 20 帧完整曝光。\n2. 点选结构光解码，在视图下拉切换投影行列坐标、有效域和拒绝原因。\n3. 点选结构光重建，比较二维低反光区域与三维孔洞。\n4. 点选点云选取画 ROI，或调整二维分割，通过同坐标区域选点。\n5. 工具菜单生成投影图案，数据菜单导入实际曝光，迁移工程时在“另存工程”勾选复制原始数据。"),
       QStringLiteral("自建 1 m／2 m 平面：相机深度中位数分别约 1000／2000 mm，低反光矩形没有三维点。示例不是实拍或设备标定。")}};
  const auto root=QCoreApplication::applicationDirPath()+"/studio-examples/";
  const QVector<SampleProject> release{
    {"synthetic-stereo-part",QStringLiteral("机加工件双目重建"),"synthetic-stereo-part","synthetic-stereo-part.mvproj",
     QStringLiteral("同步左右图 → 双目校正 → 视差计算 → 点云"),
     QStringLiteral("F5，查看视差、深度与点云，并对照数据目录中相机坐标系的深度真值。"),
     QStringLiteral("可区分凸台、加强筋、底座三档深度，基准分别为 463、489、517 mm。")},
    {"synthetic-structured-part",QStringLiteral("机加工件结构光重建"),"synthetic-structured-part","synthetic-structured-part.mvproj",
     QStringLiteral("有序投影曝光 → 解码 → 点云"),
     QStringLiteral("F5，旋转点云观察孔与加强筋，并切换投影坐标、解码有效域和深度。"),
     QStringLiteral("data/synthetic/optical-rig/part 中提供深度、两个投影坐标和前景掩膜真值。")}
  };
  for(const auto& s:release)if(QFileInfo::exists(root+s.bundle+'/'+s.project))samples.append(s);
  return samples;
}
QString bundledSampleProject(const QString& id) {
  for (const auto& sample : sampleProjects()) {
    if (sample.id != id) continue;
    const auto path=QCoreApplication::applicationDirPath()+"/studio-examples/"+sample.bundle+'/'+sample.project;
    if (!QFileInfo::exists(path)) throw std::runtime_error("示例资源不完整，请恢复应用旁的 studio-examples 目录");
    return path;
  }
  throw std::runtime_error("示例工程不存在");
}
bool isBundledSamplePath(const QString& path) {
  if(path.isEmpty())return false;
  const auto root=QFileInfo(QCoreApplication::applicationDirPath()+"/studio-examples").canonicalFilePath();
  if(root.isEmpty())return false;
  auto actual=QFileInfo(path).canonicalFilePath();
  if(actual.isEmpty())actual=QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
  constexpr auto pathCase=Qt::CaseInsensitive;
#else
  constexpr auto pathCase=Qt::CaseSensitive;
#endif
  return actual.compare(root,pathCase)==0 || actual.startsWith(root+'/',pathCase);
}
bool MainWindow::openExampleProject(const QString& id) {
  if(busy())return false;
  QString path;
  if(!guard(QStringLiteral("打开示例"),[&]{path=bundledSampleProject(id);}))return false;
  return openPath(path,true,true);
}
QString createSampleProject(const QString& id, const QString& parentDirectory) {
  SampleProject sample;
  for (const auto& item : sampleProjects())
    if (item.id == id) sample = item;
  if (sample.id.isEmpty()) throw std::runtime_error("示例工程不存在");
  const QDir source(QCoreApplication::applicationDirPath() +
                    "/studio-examples/" + sample.bundle);
  if (!QFileInfo::exists(source.filePath(sample.project)))
    throw std::runtime_error(
        "示例资源不完整，请重新构建或恢复应用旁的 studio-examples 目录");
  const QDir parent(parentDirectory);
  if (parentDirectory.isEmpty() || !parent.exists())
    throw std::runtime_error("请选择存在的工程目录");
  const auto builtins =
      QFileInfo(QCoreApplication::applicationDirPath() + "/studio-examples")
          .canonicalFilePath();
  const auto canonicalParent =
      QFileInfo(parent.absolutePath()).canonicalFilePath();
#ifdef Q_OS_WIN
  constexpr auto pathCase = Qt::CaseInsensitive;
#else
  constexpr auto pathCase = Qt::CaseSensitive;
#endif
  if (canonicalParent.compare(builtins, pathCase) == 0 ||
      canonicalParent.startsWith(builtins + "/", pathCase))
    throw std::runtime_error("请选择内置示例目录之外的位置保存副本");
  QTemporaryDir staging(parent.filePath(".mvkit-example-XXXXXX"));
  if (!staging.isValid())
    throw std::runtime_error("无法在所选目录创建示例工程");
  QDirIterator files(source.absolutePath(),
                     QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
                     QDirIterator::Subdirectories);
  while (files.hasNext()) {
    files.next();
    const auto file = files.fileInfo();
    if (file.isSymLink()) throw std::runtime_error("示例资源包含不支持的链接");
    const auto relative = source.relativeFilePath(file.absoluteFilePath());
    // The source README links to other repository tutorials. Each independent
    // copy gets its own self-contained guide instead.
    if (relative == "README.md") continue;
    const auto destination = QDir(staging.path()).filePath(relative);
    if (!QDir().mkpath(QFileInfo(destination).absolutePath()) ||
        !QFile::copy(file.absoluteFilePath(), destination))
      throw std::runtime_error("复制示例图像或模型失败");
  }
  const auto copiedProject=QDir(staging.path()).filePath(sample.project);
  const auto originalProject=source.filePath(sample.project);
  const auto portable=rebaseProjectResources(explicitTutorialInputs(readProjectConfiguration(originalProject),originalProject),originalProject,copiedProject);
  writeProjectWithOriginalData(portable,copiedProject);
  const auto guide =
      ("# " + sample.title + "\n\n" + sample.flow + "\n\n## 操作步骤\n\n" +
       sample.steps + "\n\n## 预期结果\n\n" + sample.expected + "\n")
          .toUtf8();
  QSaveFile readme(QDir(staging.path()).filePath("README.md"));
  if (!readme.open(QIODevice::WriteOnly) ||
      readme.write(guide) != guide.size() || !readme.commit())
    throw std::runtime_error("示例操作说明保存失败");
  const auto name = "MVKit-" + id + "-" +
                    QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss") +
                    "-" +
                    QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  if (!QDir().rename(staging.path(), parent.filePath(name)))
    throw std::runtime_error("示例工程目录提交失败");
  staging.setAutoRemove(false);
  return parent.filePath(name + "/" + sample.project);
}
void MainWindow::showExampleProjects() {
  if (busy()) return;
  auto* dialog = new QDialog(this);
  dialog->setObjectName("exampleProjectsDialog");
  dialog->setWindowTitle(QStringLiteral("示例工程"));
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setModal(true);
  dialog->resize(900, 530);
  auto* outer = new QVBoxLayout(dialog);
  outer->setContentsMargins(20, 20, 20, 16);
  outer->setSpacing(14);
  auto* intro = new QLabel(
      QStringLiteral("选择示例直接打开 · 可自由调整参数与 ROI，保存时另存工程"),
      dialog);
  intro->setWordWrap(true);
  outer->addWidget(intro);
  auto* body = new QHBoxLayout;
  auto* list = new QListWidget(dialog);
  list->setObjectName("exampleProjectsList");
  list->setMinimumWidth(300);
  list->setUniformItemSizes(true);
  auto* details = new QTextBrowser(dialog);
  details->setObjectName("exampleProjectGuide");
  details->setOpenExternalLinks(false);
  auto* navigation = new QVBoxLayout;
  auto* topic = new QComboBox(dialog);
  topic->setAccessibleName(QStringLiteral("示例主题"));
  topic->addItems({QStringLiteral("全部示例"), QStringLiteral("二维检测与测量"),
                   QStringLiteral("三维重建与测量")});
  navigation->addWidget(topic);
  navigation->addWidget(list);
  body->addLayout(navigation, 2);
  body->addWidget(details, 3);
  outer->addLayout(body, 1);
  const auto catalog = sampleProjects();
  for (const auto& sample : catalog) {
    auto* item = new QListWidgetItem(sample.title, list);
    const auto& id = sample.id;
    const int group = sampleCategory(id);
    item->setData(Qt::UserRole, group);
    item->setSizeHint(QSize(0, 40));
  }
  const auto updateFilter = [=] {
    const auto group = topic->currentIndex();
    int first = -1;
    for (int row = 0; row < list->count(); ++row) {
      auto* item = list->item(row);
      item->setHidden(group && item->data(Qt::UserRole).toInt() != group);
      if (!item->isHidden() && first < 0) first = row;
    }
    list->setCurrentRow(first);
    if (first < 0) details->setPlainText(QStringLiteral("本机未部署此主题的示例数据。"));
  };
  connect(topic, QOverload<int>::of(&QComboBox::currentIndexChanged), dialog, updateFilter);
  connect(list, &QListWidget::currentRowChanged, dialog, [=](int row) {
    if (row < 0 || row >= catalog.size()) return;
    const auto& s = catalog[row];
    const auto origin = s.id.startsWith("synthetic-")
        ? QStringLiteral("自建合成 · 提供几何真值")
        : QStringLiteral("随包自建基础练习");
    details->setHtml("<h3>" + s.title.toHtmlEscaped() + "</h3><p>" + origin + "</p><p>" +
                     s.flow.toHtmlEscaped() + "</p><h4>操作步骤</h4><p>" +
                     s.steps.toHtmlEscaped().replace("\n", "<br><br>") +
                     "</p><h4>预期结果</h4><p>" + s.expected.toHtmlEscaped() +
                     "</p>");
  });
  updateFilter();
  auto* actions = new QHBoxLayout;
  auto* status = new QLabel(
      QStringLiteral("包含示例图像和所需模板；无需另行训练。"), dialog);
  status->setWordWrap(true);
  actions->addWidget(status, 1);
  auto* create = new QPushButton(QStringLiteral("打开示例"), dialog);
  create->setProperty("primary", true);
  create->setObjectName("openExampleProject");
  create->setEnabled(list->currentRow()>=0);
  connect(list, &QListWidget::currentRowChanged, create, [create](int row){create->setEnabled(row >= 0);});
  auto* close = new QPushButton(QStringLiteral("关闭"), dialog);
  actions->addWidget(create);
  actions->addWidget(close);
  outer->addLayout(actions);
  connect(close, &QPushButton::clicked, dialog, &QDialog::reject);
  connect(create, &QPushButton::clicked, dialog, [=] {
    if (persistSettings_ && !confirmDiscard()) return;
    const int row = list->currentRow();
    if (row < 0 || row >= catalog.size()) return;
    try {
      if (openExampleProject(catalog[row].id))
        dialog->accept();
      else
        status->setText(lastError());
    } catch (const std::exception& e) {
      status->setText(QString::fromUtf8(e.what()));
    }
  });
  connect(list,&QListWidget::itemDoubleClicked,create,[create]{create->click();});
  dialog->show();
}
}  // namespace workbench
