// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/shell/main_window.hpp"
#include "model/workflow_ports.hpp"
#include "ui/widgets/parameter_spin_box.hpp"
#include <QTemporaryDir>
#include <QStylePainter>
#include "model/metadata_limits.hpp"
#include "ui/dialogs/sample_projects.hpp"
#include "ui/widgets/filmstrip_scroll.hpp"
#include "ui/widgets/add_data_button.hpp"
#include "ui/widgets/loading_task.hpp"
#include "ui/widgets/node_view_dock.hpp"
#include "ui/widgets/studio_controls.hpp"
#include "runtime/run_snapshot.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QStandardPaths>
#include <QCheckBox>
#include <QGridLayout>
#include <QCloseEvent>
#include <QShowEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoStack>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>

#include "ui/style/appearance.hpp"
#include "ui/style/operator_icons.hpp"
#include "ui/views/model_view.hpp"
#include "operators/operator_registry.hpp"
#include "ui/widgets/parameter_grid.hpp"
#include "ui/widgets/responsive_widgets.hpp"
#include "operators/range_model.hpp"
#include "ui/widgets/source_choice.hpp"
#include "model/spatial_data.hpp"
#include "ui/style/window_chrome.hpp"
#include "ui/views/workflow_view.hpp"
namespace workbench {
namespace {
// Library presets share the persisted algorithm identity.
constexpr int inputModeRole = Qt::UserRole + 1;
class OperatorSectionDelegate final : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;
  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    if (index.parent().isValid() || index.data(Qt::UserRole).isValid()) {
      QStyledItemDelegate::paint(painter, option, index);
      return;
    }
    QStyleOptionViewItem section(option);
    initStyleOption(&section, index);
    auto* style = section.widget->style();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(index.data(Qt::BackgroundRole).value<QBrush>());
    painter->drawRoundedRect(section.rect.adjusted(0, 2, 0, -2), 4, 4);
    section.icon.paint(
        painter, style->subElementRect(QStyle::SE_ItemViewItemDecoration,
                                       &section, section.widget));
    const auto text = style->subElementRect(QStyle::SE_ItemViewItemText,
                                            &section, section.widget);
    painter->setFont(section.font);
    painter->setPen(index.data(Qt::ForegroundRole).value<QBrush>().color());
    painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                      section.fontMetrics.elidedText(
                          section.text, Qt::ElideRight, text.width()));
    painter->restore();
  }
};
// Internal drag stays inside Studio: retaining the press owner also handles
// fast drags across dock boundaries without relying on an OS/OLE drag loop.
class OperatorLibrary final : public QTreeWidget {
 public:
  OperatorLibrary(WorkflowView* target, QWidget* parent,
                  std::function<void(QPointF)> createMultiView)
      : QTreeWidget(parent), target_(target),
        createMultiView_(std::move(createMultiView)) {
    setItemDelegate(new OperatorSectionDelegate(this));
  }
  void refreshSections() {
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    auto font = this->font();
    font.setPointSizeF(std::max(10.0, font.pointSizeF()));
    font.setWeight(QFont::DemiBold);
    for (int i = 0; i < topLevelItemCount(); ++i) {
      auto* group = topLevelItem(i);
      group->setFont(0, font);
      group->setForeground(0, QColor(dark ? "#d2ddeb" : "#3c4b61"));
      group->setBackground(0, QColor(dark ? "#2a3342" : "#edf1f6"));
    }
  }

 protected:
  void changeEvent(QEvent* event) override {
    QTreeWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::StyleChange)
      refreshSections();
  }
  void mousePressEvent(QMouseEvent* event) override {
    QTreeWidget::mousePressEvent(event);
    auto* item = itemAt(event->pos());
    tool_ = event->button() == Qt::LeftButton && item &&
                    item->data(0, Qt::UserRole).isValid()
                ? item->data(0, Qt::UserRole).toInt()
                : -1;
    multiView_ = tool_ == int(Algorithm::ImageInput) && item->data(0, inputModeRole).toBool();
    press_ = event->globalPos();
    if (ghost_) ghost_->hide();
    dragging_ = false;
  }
  void mouseMoveEvent(QMouseEvent* event) override {
    if (!dragEnabled() || tool_ < 0 || !(event->buttons() & Qt::LeftButton)) {
      QTreeWidget::mouseMoveEvent(event);
      return;
    }
    if (!dragging_ && (event->globalPos() - press_).manhattanLength() <
                          QApplication::startDragDistance())
      return;
    dragging_ = true;
    if (!ghost_) {
      ghost_ = new QLabel(window());
      ghost_->setAttribute(Qt::WA_TransparentForMouseEvents);
      ghost_->setStyleSheet(
          "background:#6876de;color:white;border-radius:5px;padding:8px "
          "14px;");
    }
    ghost_->setText(multiView_ ? QStringLiteral("多视输入")
                              : operatorDefinition(static_cast<Algorithm>(tool_)).name);
    ghost_->adjustSize();
    ghost_->move(window()->mapFromGlobal(event->globalPos()) +
                 QPoint(14, 14));
    ghost_->show();
    ghost_->raise();
    event->accept();
  }
  void mouseReleaseEvent(QMouseEvent* event) override {
    if (!dragging_) {
      tool_ = -1;
      QTreeWidget::mouseReleaseEvent(event);
      return;
    }
    const int tool = tool_;
    dragging_ = false;
    tool_ = -1;
    ghost_->hide();
    setState(NoState);
    const auto point = target_->viewport()->mapFromGlobal(event->globalPos());
    if (target_->isVisible() && target_->isEnabled() &&
        target_->viewport()->rect().contains(point)) {
      if (multiView_) createMultiView_(target_->mapToScene(point));
      else emit target_->nodeAdded(tool, target_->mapToScene(point));
    }
    event->accept();
  }
  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Escape && dragging_) {
      dragging_ = false;
      tool_ = -1;
      if (ghost_) ghost_->hide();
      setState(NoState);
      event->accept();
      return;
    }
    QTreeWidget::keyPressEvent(event);
  }

 private:
  WorkflowView* target_;
  std::function<void(QPointF)> createMultiView_;
  bool multiView_ = false;
  QLabel* ghost_ = nullptr;
  QPoint press_;
  int tool_ = -1;
  bool dragging_ = false;
};
class OutputNodeChoice final : public QComboBox {
 public:
  explicit OutputNodeChoice(QWidget* parent) : QComboBox(parent) {
    connect(this, &QComboBox::currentTextChanged, this, [this](const QString& label) {
      setToolTip(label + QStringLiteral("\n选择流程运行及导出的结果节点"));
      updateGeometry();
    });
  }
  QSize sizeHint() const override {
    return {widthForLabel(currentText()), QComboBox::sizeHint().height()};
  }
  QSize minimumSizeHint() const override {
    auto size = sizeHint();
    size.setWidth(qMin(size.width(), widthForLabel(QStringLiteral("输出 · 结构光解码"))));
    return size;
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QStyleOptionComboBox option;
    initStyleOption(&option);
    QStylePainter painter(this);
    painter.drawComplexControl(QStyle::CC_ComboBox, option);
    option.currentText = fontMetrics().elidedText(option.currentText, Qt::ElideRight,
                                                  qMax(0, labelWidth(option)));
    painter.drawControl(QStyle::CE_ComboBoxLabel, option);
  }

 private:
  int labelWidth(const QStyleOptionComboBox& option) const {
    const QRect field = style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                QStyle::SC_ComboBoxEditField, this);
    return field.width() - 2 * (style()->pixelMetric(QStyle::PM_FocusFrameHMargin, &option, this) + 1);
  }
  int widthForLabel(const QString& text) const {
    QStyleOptionComboBox option;
    initStyleOption(&option);
    option.rect.setWidth(256);
    return fontMetrics().horizontalAdvance(text) + option.rect.width() - labelWidth(option);
  }
};
QLabel* wrappingLabel(QWidget* parent) {
  auto* l = new QLabel(parent);
  l->setWordWrap(true);
  l->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return l;
}
QTableWidget* table(QWidget* parent, const QStringList& heads) {
  auto* t = new QTableWidget(0, heads.size(), parent);
  t->setHorizontalHeaderLabels(heads);
  t->setEditTriggers(QAbstractItemView::NoEditTriggers);
  t->setSelectionBehavior(QAbstractItemView::SelectRows);
  t->setSelectionMode(QAbstractItemView::SingleSelection);
  t->setAlternatingRowColors(true);
  t->verticalHeader()->hide();
  t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  t->setShowGrid(false);
  return t;
}
void cell(QTableWidget* t, int r, int c, const QString& value) {
  t->setItem(r, c, new QTableWidgetItem(value));
}
QString imageId() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
void showCalibrationDetails(QTableWidget* table, const Result& result, bool marks) {
  const QSignalBlocker block(table);
  table->setSortingEnabled(false);
  table->clearContents();
  table->setColumnCount(3);
  table->setHorizontalHeaderLabels(marks
      ? QStringList{QStringLiteral("姿态与设备"), QStringLiteral("标记编号"), QStringLiteral("残差 / px")}
      : QStringList{QStringLiteral("姿态与设备"), QStringLiteral("RMS / px"), QStringLiteral("标记点数")});
  QVector<int> indices;
  for (int i = 0; i < result.objects.size(); ++i) {
    const auto& object = result.objects[i];
    if (marks ? object.type == "Point" : object.values.contains("points")) indices.append(i);
  }
  table->setRowCount(indices.size());
  for (int row = 0; row < indices.size(); ++row) {
    const auto& object = result.objects[indices[row]];
    const QString name = marks ? QStringLiteral("姿态 %1 · 相机 %2")
        .arg(int(object.values.value("viewIndex")) + 1, 2, 10, QChar('0'))
        .arg(int(object.values.value("cameraIndex")) + 1) : object.label;
    cell(table, row, 0, name);
    table->item(row, 0)->setData(Qt::UserRole, indices[row]);
    for (int col : {1, 2}) table->setItem(row, col, new QTableWidgetItem);
    table->item(row, 1)->setData(Qt::DisplayRole, object.measure);
    table->item(row, 2)->setData(Qt::DisplayRole, object.values.value(marks ? "residual" : "points"));
  }
  table->setSortingEnabled(true);
  table->clearSelection();
}
QString operatorSummary(Algorithm tool) {
  switch (tool) {
    case Algorithm::Filter:
      return QStringLiteral("平滑降噪 · 方法可配置");
    case Algorithm::Gray:
      return QStringLiteral("灰度转换与反相");
    case Algorithm::Regions:
      return QStringLiteral("灰度范围、连通区域与面积筛选");
    case Algorithm::Edges:
      return QStringLiteral("Canny 亚像素轮廓提取");
    case Algorithm::ShapeMatch:
      return QStringLiteral("基于轮廓的模板定位");
    case Algorithm::NccMatch:
      return QStringLiteral("基于灰度相关的模板定位");
    default:
      return operatorDefinition(tool).description;
  }
}
}  // namespace
MainWindow::MainWindow(bool persistSettings, QWidget* parent,
                       const QString& workerProgram, bool startInWorkflow)
    : QMainWindow(parent),
      execution_(this, workerProgram),
      persistSettings_(persistSettings) {
  setObjectName("workbenchWindow");
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                 Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint);
  setWindowIcon(studioIcon("studio"));
  resize(1600, 900);
  setMinimumSize(1100, 760);
  setDockOptions(AllowNestedDocks | AllowTabbedDocks | AnimatedDocks);
  autoPreview_ = new QTimer(this);
  autoPreview_->setSingleShot(true);
  autoPreview_->setInterval(160);
  connect(autoPreview_, &QTimer::timeout, this, [this] {
    if (runQueue_.active() || !document_.hasImage()) return;
    if (execution_.busy()) execution_.cancel();
    startExecution(-1);
  });
  editHistory_ = new QUndoStack(this);
  editHistory_->setObjectName("editHistory");
  editHistory_->setUndoLimit(100);
  buildUi();
  buildMenus();
  setupSpatialUi();
  connect(editHistory_, &QUndoStack::indexChanged, this, [this] {
    setDirty(!editHistory_->isClean());
    updateActions();
  });
  syncParameters();
  defaultLayout_ = saveState(4);
  syncViewTools();
  if (persistSettings_) {
    QSettings s;
    restoreGeometry(s.value("window/geometry").toByteArray());
    const auto layout=s.contains("window/layout-v6")
        ? s.value("window/layout-v6").toByteArray()
        : migrateLegacyLayout(s.value("window/layout-v5",s.value("window/layout-v4")).toByteArray());
    if(!layout.isEmpty())restoreState(layout,4);
    for (int mode = 0; mode < 2; ++mode) {
      const auto key = QString("window/mode-%1/").arg(mode);
      modeLayouts_[mode] = s.contains(key+"docks-v6")
          ? s.value(key+"docks-v6").toByteArray()
          : migrateLegacyLayout(s.value(key+"docks-v5",s.value(key+"docks")).toByteArray());
      modeViewSplits_[mode] = s.value(key + "views").toByteArray();
      modeSpatialSplits_[mode] = s.value(key + "spatial").toByteArray();
    }
    nodeOutputDock_->setVisible(workflowMode_);
  }
  connect(
      &execution_, &ExecutionService::completed, this,
      [this](quint64 revision, const Result& result) {
        Q_UNUSED(revision);
        progress_->hide();
        int owner = -1;
        for (int i = 0; i < images_.size(); ++i)
          if (images_[i].id == pendingImage_ &&
              (pendingWorkflow_ ? images_[i].version
                                : images_[i].baseVersion) == pendingVersion_)
            owner = i;
        if (owner < 0) {
          log(QStringLiteral("忽略过期任务：所属图像已移除或配置已更改。"));
          taskStatus_->setText(QStringLiteral("结果已作废"));
        } else if (!result.error.isEmpty()) {
          applyTrainedModels(result);
          // Preserve successful branches and expose failed/blocked nodes.
          if (!result.nodeOutputs.isEmpty()) {
            images_[owner].workflowResult = result;
            images_[owner].workflowResultVersion = images_[owner].version;
            if (owner == activeImage_) {
              showResult(result);
              emit resultApplied();
            }
          } else if (owner == activeImage_) {
            // Root/input failures may have no node outputs; still replace the
            // public/current result instead of leaving a successful stale one.
            showResult(result);
            emit resultApplied();
          }
          taskStatus_->setText(QStringLiteral("执行失败"));
          lastError_ = result.error;
          log(QStringLiteral("算法执行：") + result.error);
          setOperatorStatus(QStringLiteral("报错"), "error", result.error);
          setResultState(QStringLiteral("执行失败"), "error");
        } else {
          applyTrainedModels(result);
          if (!pendingWorkflow_) {
            images_[owner].result = result;
            images_[owner].resultVersion = images_[owner].baseVersion;
          }
          if (!result.nodeOutputs.isEmpty()) {
            images_[owner].workflowResult = result;
            images_[owner].workflowResultVersion = images_[owner].version;
          }
          if (owner == activeImage_) {
            showResult(result);
            emit resultApplied();
          }
          setOperatorStatus(QStringLiteral("执行成功"), "ok");
          log(QStringLiteral("%1 · %2 个对象 · %3 ms")
                  .arg(result.pipeline)
                  .arg(result.objects.size())
                  .arg(result.algorithmMs, 0, 'f', 2));
        }
        if (owner >= 0) {
          auto& entry = images_[owner];
          entry.deliveryResult = result;
          entry.deliveryUtc =
              QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
          entry.deliveryWorkflow = pendingWorkflow_;
          entry.deliveryVersion = pendingVersion_;
          entry.deliveryProvenance = pendingProvenance_;
        }
        if (owner >= 0)
          images_[owner].batchState = result.error.isEmpty() ? 3 : 4;
        if (runQueue_.active()) {
          if(runQueue_.advance(pendingQueueGeneration_,!result.error.isEmpty()))startNextImage();
        } else {
          refreshFilm();
          updateActions();
        }
      });
  updateActions();
  refreshProperties();
  refreshFilm();
  setDirty(false);
  refreshContext();
  connect(&execution_, &ExecutionService::nodeStarted, this,
          [this](quint64 revision, int node) {
            if (revision != revision_ || !pendingWorkflow_) return;
            workflowView_->setRunning(node);
            setOperatorStatus(QStringLiteral("正在运行"), "running",
                              nodeNames().value(node));
          });
  refreshWorkflow();
  setWorkflowMode(startInWorkflow);
  log(QStringLiteral("MV Studio 已就绪 · Qt %1 · 修改参数自动预览，F5 "
                     "运行当前，Ctrl+F5 单次运行，Shift+F5 连续运行。")
          .arg(qVersion()));
}
void MainWindow::buildUi() {
  auto* shell = new QWidget(this);
  shell->setObjectName("appHeader");
  auto* shellLayout = new QVBoxLayout(shell);
  shellLayout->setContentsMargins(0, 0, 0, 0);
  shellLayout->setSpacing(0);
  constexpr int titleBarHeight = 60;
  auto* brand = new WindowChrome(shell);
  brand->setFixedHeight(titleBarHeight);
  auto* brandLayout = new QHBoxLayout(brand);
  brandLayout->setContentsMargins(12, 0, 8, 0);
  brandLayout->setSpacing(6);
  auto* identity = new QWidget(brand);
  identity->setObjectName("brandIdentity");
  identity->setFixedHeight(titleBarHeight);
  auto* identityLayout = new QHBoxLayout(identity);
  identityLayout->setContentsMargins(0, 0, 4, 0);
  identityLayout->setSpacing(6);
  auto* logo = new QLabel(QStringLiteral("MV"), identity);
  logo->setObjectName("brandLogo");
  logo->setAlignment(Qt::AlignCenter);
  logo->setFixedSize(30, 30);
  identityLayout->addWidget(logo);
  auto* title = new QLabel("MV Studio", identity);
  title->setObjectName("brandTitle");
  identityLayout->addWidget(title);
  brandLayout->addWidget(identity);
  auto* divider = new QFrame(brand);
  divider->setObjectName("brandDivider");
  divider->setFixedSize(1, 34);
  brandLayout->addWidget(divider);
  auto* metadata = new QWidget(brand);
  auto* metadataLayout = new QVBoxLayout(metadata);
  metadataLayout->setContentsMargins(0, 1, 0, 1);
  metadataLayout->setSpacing(0);
  auto* info = new QWidget(metadata);
  info->setObjectName("projectInfo");
  info->setFixedHeight(26);
  auto* infoLayout = new QHBoxLayout(info);
  // Match QMenuBar::item's left padding so the visible text aligns too.
  infoLayout->setContentsMargins(8, 0, 0, 0);
  infoLayout->setSpacing(6);
  projectCaption_ = new QLabel(QStringLiteral("未命名工程"), brand);
  projectCaption_->setObjectName("projectCaption");
  projectCaption_->setTextFormat(Qt::RichText);
  projectCaption_->setSizePolicy(QSizePolicy::Ignored,
                                 QSizePolicy::Preferred);
  infoLayout->addWidget(projectCaption_, 1);
  metadataLayout->addWidget(info);
  mainMenu_ = new QMenuBar(metadata);
  mainMenu_->setObjectName("mainMenu");
  mainMenu_->setNativeMenuBar(false);
  mainMenu_->setFixedHeight(24);
  metadataLayout->addWidget(mainMenu_);
  brandLayout->addWidget(metadata, 1);
  auto* modeSegment = new QWidget(brand);
  modeSegment->setObjectName("modeSwitcher");
  modeSegment->setFixedHeight(38);
  auto* modeLayout = new QHBoxLayout(modeSegment);
  modeLayout->setContentsMargins(3, 3, 3, 3);
  modeLayout->setSpacing(2);
  workbenchMode_ = new QToolButton(modeSegment);
  workbenchMode_->setObjectName("workbenchMode");
  workbenchMode_->setText(QStringLiteral("工作台"));
  workbenchMode_->setToolTip(
      QStringLiteral("独立算子试验，配置与工作流节点分别保存"));
  workbenchMode_->setIcon(studioIcon("window"));
  workbenchMode_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  workbenchMode_->setCheckable(true);
  workbenchMode_->setChecked(true);
  modeLayout->addWidget(workbenchMode_);
  workflowModeButton_ = new QToolButton(modeSegment);
  workflowModeButton_->setObjectName("workflowMode");
  workflowModeButton_->setText(QStringLiteral("工作流"));
  workflowModeButton_->setToolTip(
      QStringLiteral("组织和调试具体节点实例、输入连线与整条流程"));
  workflowModeButton_->setIcon(studioIcon("flow"));
  workflowModeButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  workflowModeButton_->setCheckable(true);
  modeLayout->insertWidget(0, workflowModeButton_);
  for (auto* button : {workbenchMode_, workflowModeButton_}) {
    button->setFixedSize(88, 30);
    button->setIconSize(QSize(16, 16));
  }
  brandLayout->addWidget(modeSegment, 0, Qt::AlignVCenter);
  auto* theme = studioIconButton("sun", QStringLiteral("切换明暗主题"), brand);
  theme->setObjectName("themeButton");
  brandLayout->addWidget(theme);
  connect(theme, &QToolButton::clicked, this, [this] {
    dark_ = !dark_;
    applyAppearance(*qApp, dark_);
  });
  connect(workbenchMode_, &QToolButton::clicked, this,
          [this] { setWorkflowMode(false); });
  connect(workflowModeButton_, &QToolButton::clicked, this,
          [this] { setWorkflowMode(true); });
  auto windowButton = [&](const char* name, const QString& label,
                          const char* icon) {
    auto* button = studioIconButton(icon, label, brand);
    button->setObjectName(name);
    button->setFixedSize(36, 32);
    brandLayout->addWidget(button);
    return button;
  };
  auto* minimize =
      windowButton("minimizeWindow", QStringLiteral("最小化"), "minimize");
  maximizeButton_ = windowButton("maximizeWindow",
                                 QStringLiteral("最大化 / 还原"), "maximize");
  auto* close =
      windowButton("closeWindow", QStringLiteral("关闭程序"), "close");
  connect(minimize, &QToolButton::clicked, this, &QWidget::showMinimized);
  connect(maximizeButton_, &QToolButton::clicked, this, [this] {
    if (isMaximized())
      showNormal();
    else
      showMaximized();
  });
  connect(close, &QToolButton::clicked, this, &QWidget::close);
  shellLayout->addWidget(brand);
  mainToolbar_ = new CommandToolbar(QStringLiteral("主工具栏"), shell);
  mainToolbar_->setObjectName("mainToolbar");
  mainToolbar_->setMovable(false);
  mainToolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  mainToolbar_->setIconSize(QSize(20, 20));
  mainToolbar_->setFixedHeight(54);
  shellLayout->addWidget(mainToolbar_);
  setMenuWidget(shell);
  auto* center = new QWidget(this);
  auto* layout = new QVBoxLayout(center);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  imagePane_ = new QWidget(center);
  imagePane_->setObjectName("imagePane");
  auto* paneLayout = new QVBoxLayout(imagePane_);
  paneLayout->setContentsMargins(0, 0, 0, 0);
  paneLayout->setSpacing(0);
  auto* paneHeader = new QWidget(imagePane_);
  paneHeader->setObjectName("viewHeader");
  paneHeader->setFixedHeight(40);
  auto* paneRow = new QHBoxLayout(paneHeader);
  paneRow->setContentsMargins(0, 0, 8, 0);
  centerTab_ = new QTabBar(paneHeader);
  centerTab_->setObjectName("centerTabs");
  centerTab_->setExpanding(false);
  centerTab_->addTab(studioIcon("view-2d"), QStringLiteral("2D视图"));
  paneRow->addWidget(centerTab_);
  paneRow->addStretch();
  auto* backToFlow=new QToolButton(paneHeader);backToFlow->setObjectName("returnToWorkflow");
  backToFlow->setText(QStringLiteral("返回流程"));backToFlow->hide();paneRow->addWidget(backToFlow);
  connect(backToFlow,&QToolButton::clicked,this,[this]{
    const auto id=debugNodeId_;setWorkflowMode(true);
    const int node=workflowIds_.indexOf(id);if(node>=0)workflowView_->focusNode(node);
  });
  auto* header = new QWidget(paneHeader);
  canvasHeader_ = header;
  header->setObjectName("workflowHeader");
  auto* row = new QHBoxLayout(header);
  row->setContentsMargins(0, 0, 0, 0);
  imageTitle_ = new QLabel(header);
  imageTitle_->hide();
  workflowOutputChoice_ = new OutputNodeChoice(header);
  workflowOutputChoice_->setObjectName("workflowOutputChoice");
  workflowOutputChoice_->setToolTip(
      QStringLiteral("选择整条流程运行及导出的结果节点；查看节点不会改变此设置"));
  row->addWidget(workflowOutputChoice_);
  connect(workflowOutputChoice_, QOverload<int>::of(&QComboBox::activated),
          this, [this](int row) {
            EditGuard edit(*this, QStringLiteral("指定流程输出"));
            workflowOutput_ = workflowOutputChoice_->itemData(row).toInt();
            workflowChanged();
          });
  auto* graphFit =
      new QPushButton(studioIcon("fit"), QString(), header);
  graphFit->setObjectName("workflowFitButton");
  graphFit->setToolTip(QStringLiteral("适应流程"));
  graphFit->setAccessibleName(QStringLiteral("适应流程"));
  graphFit->setFixedSize(36,32);
  graphFit->setIconSize(QSize(20,20));
  nodeSearch_ = new SourceChoice(header, true);
  nodeSearch_->setObjectName("workflowNodeSearch");
  nodeSearch_->lineEdit()->setPlaceholderText(QStringLiteral("查找节点…"));
  nodeSearch_->setMaximumWidth(190);
  row->addWidget(nodeSearch_);
  row->addWidget(graphFit);
  connect(nodeSearch_, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
    const auto id = nodeSearch_->itemData(index).toString();
    const int node = workflowIds_.indexOf(id);
    if (node >= 0) workflowView_->focusNode(node);
    nodeSearch_->setCurrentIndex(-1);
  });
  auto* arrangement = studioIconButton("layout", QStringLiteral("流程布局"), header);
  arrangement->setObjectName("workflowNavigation");
  arrangement->setPopupMode(QToolButton::InstantPopup);
  auto* navigation = new QMenu(arrangement);
  navigation->setObjectName("workflowNavigationMenu");
  auto* focus = navigation->addAction(QStringLiteral("适应所选节点"));
  focus->setObjectName("fitSelectedNodes");
  connect(focus, &QAction::triggered, this, [this] { workflowView_->fitSelection(); });
  navigation->addSeparator();
  for (const auto& pair : QVector<QPair<QString, WorkflowView::Arrangement>>{
           {QStringLiteral("左对齐"), WorkflowView::Arrangement::Left},
           {QStringLiteral("顶对齐"), WorkflowView::Arrangement::Top},
           {QStringLiteral("水平等距"), WorkflowView::Arrangement::Horizontal},
           {QStringLiteral("垂直等距"), WorkflowView::Arrangement::Vertical}}) {
    auto* action = navigation->addAction(pair.first);
    action->setData(int(pair.second));
    connect(action, &QAction::triggered, this, [this, pair] { workflowView_->arrangeSelection(pair.second); });
  }
  connect(navigation, &QMenu::aboutToShow, this, [this, navigation] {
    const int count = workflowView_->selectedNodes().size();
    for (auto* action : navigation->actions())
      if (action->data().isValid()) action->setEnabled(count >= (action->data().toInt() >= 2 ? 3 : 2));
  });
  navigation->addSeparator();
  auto* overview = navigation->addAction(QStringLiteral("超界总览"));
  overview->setToolTip(QStringLiteral("画布超出窗口时显示流程总览"));
  overview->setCheckable(true);
  overview->setChecked(true);
  connect(overview, &QAction::toggled, this, [this](bool value) { workflowView_->setOverviewEnabled(value); });
  arrangement->setMenu(navigation);
  row->addWidget(arrangement);
  paneRow->addWidget(header);
  header->hide();
  paneLayout->addWidget(paneHeader);
  centerPages_ = new QStackedWidget(center);
  view_ = new ImageView(centerPages_);
  centerPages_->addWidget(view_);
  workflowView_ = new WorkflowView(centerPages_);
  centerPages_->addWidget(workflowView_);
  views_ = new ViewSplitter(Qt::Horizontal, center);
  views_->setObjectName("imageViews");
  paneLayout->addWidget(centerPages_, 1);
  views_->addWidget(imagePane_);
  modelView_ = new ModelView(views_);
  modelView_->setObjectName("workbenchModelView");
  views_->addWidget(modelView_);
  connect(modelView_, &ModelView::loadRequested, this,
          &MainWindow::loadModel);
  modelView_->hide();
  views_->setChildrenCollapsible(false);
  layout->addWidget(views_, 1);
  connect(graphFit, &QPushButton::clicked, this,
          [this] { workflowView_->fitGraph(); });
  auto* film = new QWidget(center);
  film->setObjectName("filmstrip");
  auto* filmLayout = new QVBoxLayout(film);
  filmLayout->setContentsMargins(10, 6, 10, 6);
  filmLayout->setSpacing(5);
  auto* head = new QHBoxLayout;
  auto* label = new QLabel(QStringLiteral("数据资源"), film);label->setObjectName("dataResourceTitle");
  head->addWidget(label);
  filmCount_ = new QLabel(film);
  filmCount_->setObjectName("imageCount");
  head->addWidget(filmCount_);
  inputTrackChoice_=new QComboBox(film);inputTrackChoice_->setObjectName("inputTrackChoice");
  inputTrackChoice_->setMaximumWidth(280);inputTrackChoice_->setMinimumWidth(120);
  inputTrackChoice_->setSizeAdjustPolicy(QComboBox::AdjustToContents);head->addWidget(inputTrackChoice_);
  connect(inputTrackChoice_,QOverload<int>::of(&QComboBox::activated),this,[this](int row){
    const int node=inputTrackChoice_->itemData(row).toInt();if(node<0||node>=workflow_.size())return;
    if(!workflowMode_){if(!nodeContext())workbenchParameters_=document_.parameters;debugNodeId_=workflowIds_[node];}
    activeInputKey_=workflow_[node].options.value("binding").toString();selectedStep_=node;
    document_.parameters=workflow_[node];syncParameters();refreshWorkflow();
  });
  head->addStretch();
  for (bool selected : {true, false}) {
    auto* b = new QPushButton(
        studioIcon(selected ? "check-all" : "selection-clear"), QString(), film);
    const auto selectionTitle=selected ? QStringLiteral("全选") : QStringLiteral("清除选择");
    b->setToolTip(selectionTitle);b->setAccessibleName(selectionTitle);b->setFixedSize(36,32);
    b->setObjectName(selected ? "selectAllImages" : "deselectAllImages");
    head->addWidget(b);
    connect(b, &QPushButton::clicked, this,
            [this, selected] { selectImages(selected); });
  }
  QPushButton* add = new AddDataButton(film);
  auto* remove =
      new QPushButton(studioIcon("minus"), QString(), film);
  auto* clear =
      new QPushButton(studioIcon("trash"), QString(), film);
  add->setObjectName("addImagesButton");
  remove->setObjectName("removeImagesButton");
  clear->setObjectName("clearImagesButton");
  add->setToolTip(QStringLiteral("添加图像；右下角选择其他数据类型"));add->setAccessibleName(QStringLiteral("添加图像"));
  remove->setToolTip(QStringLiteral("移除当前轨的选中数据"));remove->setAccessibleName(QStringLiteral("移除当前轨的选中数据"));
  clear->setToolTip(QStringLiteral("清空当前输入轨"));clear->setAccessibleName(QStringLiteral("清空当前输入轨"));
  for(auto* button:{add,remove,clear})button->setFixedSize(36,32);
  head->addWidget(add);
  auto* resourceManager = new QPushButton(studioIcon("layers"),QString(), film);
  resourceManager->setToolTip(QStringLiteral("资源管理"));resourceManager->setAccessibleName(QStringLiteral("资源管理"));resourceManager->setFixedSize(36,32);
  resourceManager->setObjectName("resourceManagerButton");
  connect(resourceManager, &QPushButton::clicked, this, &MainWindow::showCaptureManager);
  head->addWidget(remove);
  head->addWidget(clear);
  head->addWidget(resourceManager);
  filmLayout->addLayout(head);
  filmScroll_ = new FilmstripScroll(film);
  filmScroll_->setObjectName("filmstripScroll");
  filmScroll_->setWidgetResizable(true);
  filmScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  filmScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  filmScroll_->horizontalScrollBar()->setSingleStep(147);
  auto refreshVisibleFilm = [this] {
    if (images_.size() <= 64 || filmScroll_->property("rebuildingFilm").toBool()) return;
    const int start=std::max(0,filmScroll_->horizontalScrollBar()->value()/147-2);
    const int end=std::min(images_.size(),(filmScroll_->horizontalScrollBar()->value()+filmScroll_->viewport()->width()+146)/147+2);
    if (start==filmScroll_->property("firstFilm").toInt() && end==filmScroll_->property("lastFilm").toInt()) return;
    if (filmScroll_->property("refreshFilmPending").toBool()) return;
    filmScroll_->setProperty("refreshFilmPending",true);
    QTimer::singleShot(0,this,[this]{filmScroll_->setProperty("refreshFilmPending",false);refreshFilm();});
  };
  connect(filmScroll_->horizontalScrollBar(),&QScrollBar::valueChanged,this,refreshVisibleFilm);
  connect(filmScroll_->horizontalScrollBar(),&QScrollBar::rangeChanged,this,refreshVisibleFilm);
  filmScroll_->setFixedHeight(106);
  filmLayout->addWidget(filmScroll_);
  layout->addWidget(film);
  setCentralWidget(center);
  auto* addMenu = new QMenu(add);
  addMenu->setObjectName("addDataMenu");
  static_cast<AddDataButton*>(add)->setDataMenu(addMenu);
  connect(remove, &QPushButton::clicked, this, [this] { removeImages(); });
  connect(clear, &QPushButton::clicked, this, [this] { removeImages(true); });
  buildImageLayers(view_);
  connect(view_, &ImageView::roiChanged, this, &MainWindow::applyDocumentRoi);
  auto* resources = new QDockWidget(QStringLiteral("算子"), this);
  resources->setObjectName("resourcesDock");
  resources->setMinimumWidth(180);
  auto* libraryPage = new QWidget(resources);
  auto* libLayout = new QVBoxLayout(libraryPage);
  libLayout->setContentsMargins(6, 6, 6, 6);
  auto* operatorsTitle = new QLabel(QStringLiteral("算子"), libraryPage);
  operatorsTitle->setObjectName("operatorsHeading");
  libLayout->addWidget(operatorsTitle);
  auto* search = new QLineEdit(libraryPage);
  search->setObjectName("operatorSearch");
  search->setPlaceholderText(QStringLiteral("搜索算子或功能…"));
  libLayout->addWidget(search);
  library_ = new OperatorLibrary(workflowView_, libraryPage, [this](QPointF position) {
    addWorkflowNode(Algorithm::ImageInput, position, -1, -3, true);
  });
  library_->setDragEnabled(workflowMode_);
  library_->setDefaultDropAction(Qt::CopyAction);
  library_->setObjectName("operatorLibrary");
  library_->setHeaderHidden(true);
  library_->setIndentation(0);
  library_->setRootIsDecorated(false);
  library_->setExpandsOnDoubleClick(false);
  library_->setIconSize(QSize(16, 16));
  recentGroup_ = new QTreeWidgetItem(library_, {QStringLiteral("最近使用")});
  recentGroup_->setHidden(true);
  libLayout->addWidget(library_);
  QMap<QString, QTreeWidgetItem*> categories;
  using A = Algorithm;
  for (const auto id : {A::ImageInput, A::SequenceInput, A::Gray, A::Filter, A::ImageEnhance,
       A::ImageMorph, A::ImageGradient, A::ImageAffine, A::Polar, A::ShapeMatch, A::NccMatch,
       A::Regions, A::RegionMorph, A::RegionBoolean, A::RegionFilter, A::RegionFeatures,
       A::Edges, A::ContourProcess, A::ContourFilter, A::LineFit, A::CircleFit, A::EllipseFit,
       A::Measure, A::GrayStats, A::ScaleCalibration, A::PlaneMeasure, A::Judge, A::CalibrationDetect,
       A::MonoCalibration, A::StereoCalibration, A::StructuredCalibration, A::ImageRectify,
       A::StereoRectify, A::StereoDisparity, A::DisparityFilter, A::StereoReconstruct,
       A::StructuredDecode, A::StructuredReconstruct, A::CloudSelect, A::PlaneFit, A::SphereFit, A::CloudMeasure}) {
    const auto& op = operatorDefinition(id);
    if (!categories.contains(op.category))
      categories[op.category] = new QTreeWidgetItem(library_, {op.category});
    auto* item = new QTreeWidgetItem(categories[op.category], {op.name});
    item->setData(0, Qt::UserRole, static_cast<int>(op.id));
    item->setToolTip(0, op.description);
    item->setIcon(0, studioIcon(operatorIconName(op.id)));
    if (op.id == Algorithm::ImageInput) {
      auto* multi = new QTreeWidgetItem(categories[op.category], {QStringLiteral("多视输入")});
      multi->setData(0, Qt::UserRole, int(Algorithm::ImageInput));
      multi->setData(0, inputModeRole, true);
      multi->setToolTip(0, QStringLiteral("一组左右相机图像作为一个数据；拖入后可直接添加双目数据，标定在双目校正节点配置。"));
      multi->setIcon(0, studioIcon(operatorIconName(Algorithm::StereoImage)));
    }
  }
  library_->expandAll();
  auto sectionIcon = [this](QTreeWidgetItem* item) {
    item->setIcon(
        0, studioIcon(item->isExpanded() ? "chevronDown" : "chevronRight"));
  };
  for (int i = 0; i < library_->topLevelItemCount(); ++i) {
    auto* group = library_->topLevelItem(i);
    group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
    sectionIcon(group);
  }
  static_cast<OperatorLibrary*>(library_)->refreshSections();
  connect(library_, &QTreeWidget::itemExpanded, this, sectionIcon);
  connect(library_, &QTreeWidget::itemCollapsed, this, sectionIcon);
  buildNodeLibrary(libraryPage, resources);
  addDockWidget(Qt::LeftDockWidgetArea, resources);
  connect(search, &QLineEdit::textChanged, this, &MainWindow::refreshOperatorSearch);
  connect(
      library_, &QTreeWidget::itemClicked, this,
      [this](QTreeWidgetItem* item, int) {
        if (!item->data(0, Qt::UserRole).isValid()) {
          item->setExpanded(!item->isExpanded());
          return;
        }
        if (item->data(0, Qt::UserRole).isValid()) {
          if (!workflowMode_) {
            selectBasicOperator(
                static_cast<Algorithm>(item->data(0, Qt::UserRole).toInt()));
            return;
          }
          if (workflowMode_) selectedStep_ = -3;
          algorithm_->setCurrentIndex(algorithm_->findData(item->data(0, Qt::UserRole)));
          if (item->data(0, Qt::UserRole).toInt() == int(Algorithm::ImageInput)) {
            document_.parameters.options["inputMode"] = item->data(0, inputModeRole).toBool() ? 1 : 0;
            syncParameters();
          }
          if (workflowMode_) refreshWorkflow();
        }
      });
  nodeOutputDock_ = new NodeViewDock(QStringLiteral("视图"), this);
  nodeOutputDock_->setObjectName("nodeOutputDock");
  auto* nodeTabs = new QTabWidget(nodeOutputDock_);
  nodeViewTabs_ = nodeTabs;
  nodeTabs->tabBar()->setElideMode(Qt::ElideRight);
  nodeTabs->tabBar()->setMinimumWidth(0);
  nodeTabs->setObjectName("nodeOutputTabs");
  nodeOutputPanel_ = new QWidget(nodeTabs);
  nodeOutputPanel_->setAutoFillBackground(true);
  auto* previewLayout = new QVBoxLayout(nodeOutputPanel_);
  previewLayout->setContentsMargins(0, 0, 0, 0);
  previewLayout->setSpacing(0);
  nodeView_ = new ImageView(nodeOutputPanel_);
  nodeView_->setObjectName("nodeOutputView");
  nodeView_->setRoiEditingEnabled(true);
  nodeView_->setMinimumSize(180, 120);
  nodeView_->setEmptyText(QStringLiteral("运行流程后查看节点输出"));
  previewLayout->addWidget(nodeView_, 1);
  buildImageLayers(nodeView_);
  buildImageNavigation(nodeView_);
  connect(nodeView_, &ImageView::roiChanged, this,
          &MainWindow::applyDocumentRoi);
  nodeTabs->addTab(nodeOutputPanel_, QStringLiteral("视图"));
  auto* typeCorner = new QWidget(nodeTabs);
  typeCorner->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
  auto* typeLayout = new QHBoxLayout(typeCorner);
  typeLayout->setContentsMargins(0, 0, 8, 0);
  nodeOutputType_ = new QLabel(typeCorner);
  nodeOutputType_->setObjectName("outputType");
  nodeOutputType_->setMinimumWidth(54);
  nodeOutputType_->setAlignment(Qt::AlignCenter);
  typeLayout->setSpacing(2);
  typeLayout->addWidget(nodeOutputType_);
  pinView_ = studioIconButton("pin", QStringLiteral("固定观察此节点"), typeCorner);
  pinView_->setObjectName("pinNodeView");
  pinView_->setCheckable(true);
  compareSource_ = studioIconButton("compare", QStringLiteral("选择对比来源"), typeCorner);
  compareSource_->setObjectName("compareSourceButton");
  compareSource_->setPopupMode(QToolButton::InstantPopup);
  compareSource_->hide();
  auto* comparisons = new QMenu(compareSource_);
  auto* comparisonGroup = new QActionGroup(comparisons);
  for (bool input : {true, false}) {
    auto* action = comparisons->addAction(input ? QStringLiteral("节点输入 ↔ 输出")
                                               : QStringLiteral("原始图像 ↔ 输出"));
    action->setObjectName(input ? "compareNodeInputAction" : "compareOriginalAction");
    action->setCheckable(true);
    action->setChecked(input);
    comparisonGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, input] {
      compareNodeInput_ = input;
      refreshNodeOutput();
    });
  }
  compareSource_->setMenu(comparisons);
  floatView_ = studioIconButton("maximize", QStringLiteral("弹出并放大视图"), typeCorner);
  floatView_->setObjectName("floatNodeView");
  for (auto* button : {pinView_, compareSource_, floatView_}) {
    button->setFixedSize(26, 28);
    button->setIconSize(QSize(16, 16));
    button->setStyleSheet("QToolButton{padding:3px;min-height:0;min-width:0;}");
    typeLayout->addWidget(button);
  }
  connect(pinView_, &QToolButton::clicked, this, [this](bool checked) {
    pinnedViewNode_ = checked ? workflowIds_.value(viewNodeIndex()) : QString();
    refreshNodeOutput();
  });
  connect(floatView_, &QToolButton::clicked, this, [this] {
    setNodeViewFloating(!nodeOutputDock_->isFloating());
  });
  connect(nodeOutputDock_, &QDockWidget::topLevelChanged, this,
          [this] { refreshViewIdentity(); });
  nodeTabs->setCornerWidget(typeCorner, Qt::TopRightCorner);
  nodeOutputDock_->setWidget(nodeTabs);
  addDockWidget(Qt::RightDockWidgetArea, nodeOutputDock_);

  auto* output = new NodeViewDock(QStringLiteral("参数与结果"), this);
  outputDock_ = output;
  output->setObjectName("outputDock");
  auto* tabs = new QTabWidget(output);
  outputTabs_ = tabs;
  tabs->setObjectName("outputTabs");
  operatorScroll_ = new QScrollArea(tabs);
  operatorScroll_->setObjectName("parameterScroll");
  operatorScroll_->setWidgetResizable(true);
  operatorScroll_->setFrameShape(QFrame::NoFrame);
  operatorScroll_->setMinimumHeight(100);
  operatorControls_ = new QWidget(operatorScroll_);
  operatorControls_->setMinimumWidth(270);
  operatorControls_->setAutoFillBackground(true);
  buildParameters(operatorControls_);
  operatorScroll_->setWidget(operatorControls_);
  tabs->addTab(operatorScroll_,QStringLiteral("参数"));
  auto* grip = new QWidget(resources);
  grip->setObjectName("dockGrip");grip->setFixedHeight(7);
  grip->setToolTip(QStringLiteral("拖动调整面板，双击浮动"));
  resources->setTitleBarWidget(grip);
  auto* page = new QWidget(tabs);
  auto* outLayout = new QVBoxLayout(page);
  outLayout->setContentsMargins(8, 4, 8, 5);
  summary_ = wrappingLabel(page);
  summary_->setObjectName("secondaryText");
  summary_->setText(QStringLiteral("尚未运行"));
  auto* resultHeader = new QHBoxLayout;
  resultState_ = new QLabel(QStringLiteral("尚未运行"), page);
  resultState_->setObjectName("resultState");
  resultState_->setProperty("state", "empty");
  resultHeader->addWidget(resultState_);
  resultHeader->addWidget(summary_, 1);
  outLayout->addLayout(resultHeader);
  calibrationDetails_ = new QComboBox(page);
  calibrationDetails_->setObjectName("calibrationResultDetails");
  calibrationDetails_->setAccessibleName(QStringLiteral("标定结果明细"));
  calibrationDetails_->addItems({QStringLiteral("各姿态重投影误差"), QStringLiteral("当前姿态标记点")});
  calibrationDetails_->hide();
  outLayout->addWidget(calibrationDetails_);
  connect(calibrationDetails_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int page) {
            showCalibrationDetails(results_, inspectedResult_, page == 1);
            objectInfo_->setText(QStringLiteral("选择一行查看明细"));
          });
  results_ = table(page, {QStringLiteral("对象"), QStringLiteral("类型"),
                          QStringLiteral("测量值")});
  results_->setObjectName("resultsTable");
  outLayout->addWidget(results_, 1);
  objectInfo_ = wrappingLabel(page);
  objectInfo_->setObjectName("secondaryText");
  objectInfo_->setText(QStringLiteral("选中对象后查看坐标详情"));
  outLayout->addWidget(objectInfo_);
  tabs->addTab(page, QStringLiteral("结果"));
  variables_ = table(tabs, {QStringLiteral("变量"), QStringLiteral("类型"),
                            QStringLiteral("数量")});
  variables_->setObjectName("variablesTable");
  tabs->addTab(variables_, QStringLiteral("变量"));
  logs_ = new QPlainTextEdit(tabs);
  logs_->setObjectName("executionLog");
  logs_->setReadOnly(true);
  logs_->setMaximumBlockCount(2000);
  tabs->addTab(logs_, QStringLiteral("日志"));
  output->setWidget(tabs);
  addDockWidget(Qt::RightDockWidgetArea, output);
  splitDockWidget(nodeOutputDock_,output,Qt::Vertical);
  resizeDocks({resources,nodeOutputDock_,output},{218,340,340},Qt::Horizontal);
  nodeOutputDock_->hide();
  resizeDocks({output}, {725}, Qt::Vertical);
  connect(results_, &QTableWidget::itemSelectionChanged, this, [this] {
    int i = selectedResultObject();
    if (!viewPinnedElsewhere())
      (workflowMode_ ? nodeView_ : view_)->selectResult(i);
    if (i >= 0 && i < inspectedResult_.objects.size()) {
      const auto& o = inspectedResult_.objects[i];
      objectInfo_->setText(
          (o.type == "Value" || o.type == "Plane3D") && o.overlay.isEmpty()
              ? o.label + " · " + QString::number(o.measure, 'g', 10) + " " + o.unit
              : QStringLiteral("对象 %1 · Row %2 · Column %3\n角度 %4°")
              .arg(i + 1)
              .arg(o.center.y(), 0, 'f', 3)
              .arg(o.center.x(), 0, 'f', 3)
              .arg(o.angle, 0, 'f', 3));
      if (!o.values.isEmpty()) {
        QStringList values;
        for (auto it = o.values.begin(); it != o.values.end(); ++it)
          values << it.key() + " = " + QString::number(it.value(), 'g', 8);
        objectInfo_->setText(objectInfo_->text() + "\n" + values.join(" · "));
      }
    }
  });
  results_->setSortingEnabled(true);
  results_->sortItems(0, Qt::AscendingOrder);
  results_->setToolTip(QStringLiteral("点击列标题排序；双击对象定位到图像，编号不随排序变化"));
  connect(results_, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
    if (!viewPinnedElsewhere()) activeImageView()->focusResult(selectedResultObject());
  });
  for (auto* image : {view_, nodeView_}) {
    connect(image, &ImageView::resultPicked, this, [this](int index) { selectResultObject(index); });
    connect(image, &ImageView::resultActivated, this, [this](int index) { selectResultObject(index, true); });
  }
  imageInfo_ = new QLabel(this);
  imageInfo_->setObjectName("imageMetadata");
  imageInfo_->setToolTip(
      QStringLiteral("当前输入图像的尺寸、通道数、原始像素类型及显示映射"));
  view_->setPixelText(QStringLiteral("Row — · Col — · 原始图像"));
  operatorStatus_ = new QLabel(QStringLiteral("算子状态 · 就绪"), this);
  operatorStatus_->setObjectName("operatorStatus");
  operatorStatus_->setTextFormat(Qt::PlainText);
  operatorStatus_->setSizePolicy(QSizePolicy::Ignored,
                                 QSizePolicy::Preferred);
  workbenchTiming_ = new QLabel(this);
  workbenchTiming_->setObjectName("workbenchTiming");
  workbenchTiming_->setToolTip(QStringLiteral("当前算子的执行耗时"));
  workbenchTiming_->hide();
  zoomStatus_ = new QLabel(this);
  zoomStatus_->setObjectName("imageZoomStatus");
  taskStatus_ = new QLabel(QStringLiteral("就绪"), this);
  taskStatus_->setObjectName("taskStatus");
  taskStatus_->setMinimumWidth(150);
  progress_ = new QProgressBar(this);
  progress_->setRange(0, 0);
  progress_->setMaximumWidth(90);
  progress_->hide();
  statusBar()->addWidget(imageInfo_);
  statusBar()->addWidget(operatorStatus_, 1);
  locateIssue_ = new QToolButton(this);
  locateIssue_->setObjectName("locateOperatorIssue");
  locateIssue_->setText(QStringLiteral("定位问题"));
  locateIssue_->hide();
  statusBar()->addWidget(locateIssue_);
  connect(locateIssue_, &QToolButton::clicked, this, &MainWindow::locateIssue);
  statusBar()->addPermanentWidget(zoomStatus_);
  statusBar()->addPermanentWidget(progress_);
  statusBar()->addPermanentWidget(taskStatus_);
  statusBar()->addPermanentWidget(workbenchTiming_);
  connect(view_, &ImageView::cursorPixel, this,
          [this](int row, int col) { samplePixel(view_, row, col); });
  for(auto* target:{view_,nodeView_})connect(target,&ImageView::zoomChanged,this,[this,target](double scale){
    if(target!=activeImageView())return;
    zoomStatus_->setText(QStringLiteral("图像 %1%").arg(scale*100,0,'f',0));
    zoomStatus_->setVisible(view2d_ && view2d_->isChecked() && target->hasImage());
  });
  connect(workflowView_, &WorkflowView::stepSelected, this, [this](int i) {
    if (i < -2 || i >= workflow_.size()) return;
    ++editNavigation_;
    selectedStep_ = i;
    if (i >= 0) {
      document_.parameters = workflow_[i];
      syncParameters();
    }
    roiPurpose_->setCurrentIndex(0);
    refreshWorkflow();
    if (workflowMode_ && viewNodeIndex() == i && spatialView_ &&
        !spatialView_->points.isEmpty() && !view3d_->isChecked()) {
      view3d_->setChecked(true);
      updateViews();
    }
  });
  connect(workflowView_, &WorkflowView::nodeAdded, this,
          [this](int tool, QPointF position) {
            addWorkflowNode(static_cast<Algorithm>(tool), position);
          });
  connect(workflowView_, &WorkflowView::nodeInserted, this,
          [this](int tool, QPointF position, int target, int source) {
            addWorkflowNode(static_cast<Algorithm>(tool), position, target, source);
          });
  connect(workflowView_, &WorkflowView::nodeRemoved, this,
          &MainWindow::removeWorkflowNode);
  connect(workflowView_, &WorkflowView::nodeDebugRequested, this,
          [this](int index) {
            if (index >= 0 && index < workflowIds_.size())
              debugNode(workflowIds_[index]);
          });
  connect(workflowView_, &WorkflowView::nodeRenamed, this,
          &MainWindow::requestNodeRename);
  connect(workflowView_, &WorkflowView::inputChanged, this,
          &MainWindow::connectWorkflowNode);
  connect(workflowView_, &WorkflowView::namedInputRequested, this, &MainWindow::chooseNamedInput);
  connect(workflowView_, &WorkflowView::namedInputSelected, this, &MainWindow::bindNamedInput);
  connect(workflowView_, &WorkflowView::connectionRewired, this,
          &MainWindow::rewireWorkflowConnection);
  connect(workflowView_, &WorkflowView::inputSelected, this,
          &MainWindow::selectWorkflowInput);
  connect(workflowView_, &WorkflowView::typedInputSelected, this,
          [this](int target, const QString& type, int source) {
            if (runQueue_.active() || target < 0 || target >= workflow_.size())
              return;
            Document graph;
            graph.workflow = workflow_;
            graph.workflowLinks = workflowLinks_;
            graph.workflowInputs = workflowInputs_;
            if (type == "ObjectA" || type == "ObjectB") {
              if (source == -1 && captureTool(workflow_[target].algorithm)) { connectWorkflowNode(target,source); return; }
              bindGeometryInput(target, source, type == "ObjectB");
              return;
            }
            if (!workflowObjectSources(graph, target, type).contains(source))
              return;
            EditGuard edit(*this, QStringLiteral("选择数据来源"));
            selectedStep_ = target;
            document_.parameters = workflow_[target];
            auto& s = editableRange();
            if (type == "ObjectA" || type == "ObjectB") {
              if (source == -1 && captureTool(workflow_[target].algorithm)) { connectWorkflowNode(target,source); return; }
              bindGeometryInput(target, source, type == "ObjectB");
              return;
            }
            if (type == "Region") {
              s.scope = ScopeMode::Region;
              s.regionSource = source;
            } else {
              s.poseSource = source;
              s.poseReferenceSet = false;
            }
            rangeChanged();
            refreshWorkflow();
          });
  connect(workflowView_, &WorkflowView::typedConnectionRemoved, this,
          [this](int target, const QString& role, int source) {
            if (runQueue_.active() || target < 0 || target >= workflow_.size())
              return;
            EditGuard edit(*this, QStringLiteral("断开类型化输入"));
            auto clear = [&](NodeSettings& setting) {
              auto& index = role == "ObjectA" ? setting.dataA : setting.dataB;
              if (index == source) index = -2;
              if (setting.inputs.contains(role)) setting.inputs[role].node = -2;
            };
            clear(sharedRanges_[target]);
            for (auto& image : images_)
              if (target < image.document.nodeSettings.size())
                clear(image.document.nodeSettings[target]);
            if (target < document_.nodeSettings.size())
              clear(document_.nodeSettings[target]);
            const auto settings = document_.rangeOverrides.value(target)
                ? document_.nodeSettings.value(target) : sharedRanges_.value(target);
            if (settings.dataA != source && settings.dataB != source)
              workflowLinks_[target].removeAll(source);
            selectedStep_ = target;
            workflowChanged();
          });
  connect(workflowView_, &WorkflowView::connectionRemoved, this,
          &MainWindow::disconnectWorkflowNode);
  connect(workflowView_, &WorkflowView::dependencyAdded, this,
          [this](int target, int source) {
            if (runQueue_.active() || target < 0 || target >= workflow_.size() ||
                source < -1 || source >= workflow_.size())
              return;
            Document graph;
            graph.workflow = workflow_;
            graph.workflowInputs = workflowInputs_;
            graph.workflowLinks = workflowLinks_;
            if (graph.workflowLinks[target].contains(source)) return;
            graph.workflowLinks[target].append(source);
            try {
              workflowOrder(graph, true);
            } catch (const std::exception& error) {
              setOperatorStatus(QStringLiteral("依赖无效"), "error",
                                QString::fromUtf8(error.what()));
              return;
            }
            EditGuard edit(*this, QStringLiteral("添加依赖连线"));
            workflowLinks_ = graph.workflowLinks;
            workflowChanged();
          });
  connect(nodeView_, &ImageView::cursorPixel, this,
          [this](int row, int col) { samplePixel(nodeView_, row, col); });
  connect(workflowView_, &WorkflowView::editRejected, this,
          [this](const QString& reason) {
            setOperatorStatus(QStringLiteral("连线无效"), "error", reason);
          });
  connect(workflowView_, &WorkflowView::nodeMoveStarted, this, [this] {
    nodeMoveEdit_ = std::make_unique<EditGuard>(*this, QStringLiteral("移动节点"));
  });
  connect(workflowView_, &WorkflowView::nodeMoveFinished, this,
          [this] { nodeMoveEdit_.reset(); });
  connect(workflowView_, &WorkflowView::nodeMoved, this,
          [this](int index, QPointF position) {
            if (workflowPositions_.size() != workflow_.size() + 1) {
              workflowPositions_.clear();
              for (int i = 0; i <= workflow_.size(); ++i)
                workflowPositions_.append(
                    QPointF(45 + i % 3 * 240, 65 + i / 3 * 140));
            }
            workflowPositions_[index + 1] = position;
            setDirty(true);
          });
}
void MainWindow::buildImageLayers(ImageView* target) {
  auto* layerRail = new QFrame(target);
  layerRail->setObjectName("layerTools");
  auto* rail = new QVBoxLayout(layerRail);
  rail->setContentsMargins(4, 4, 4, 4);
  rail->setSpacing(3);
  const QStringList names{QStringLiteral("原始图像"),
                          QStringLiteral("结果图像"), QStringLiteral("ROI"),
                          QStringLiteral("结果元素")};
  const QStringList icons{"image", "result", "rect", "layers"};
  for (int i = 0; i < names.size(); ++i) {
    auto* b = studioIconButton(icons[i], names[i], layerRail);
    b->setObjectName("layerButton" + QString::number(i));
    b->setCheckable(true);
    rail->addWidget(b);
    connect(b, &QToolButton::toggled, this, [target, i](bool checked) {
      target->setLayerVisible(static_cast<ImageView::Layer>(i), checked);
    });
  }
  auto* compare = studioIconButton(
      "compare",
      QStringLiteral("卷帘对比：拖动图中分隔线，左侧原图 / 右侧结果"),
      layerRail);
  compare->setObjectName("compareButton");
  compare->setCheckable(true);
  rail->addWidget(compare);
  auto* quality =
      studioIconButton("points",
                 QStringLiteral("拟合质量：选中拟合对象后显示采样点，绿色为容差"
                                "内，橙色为容差外；不代表 SDK 的内点/剔除点"),
                 layerRail);
  quality->setObjectName("fitQualityToggle");
  quality->setCheckable(true);
  rail->insertWidget(rail->indexOf(compare),quality);
  connect(quality, &QToolButton::toggled, target,
          &ImageView::setQualityVisible);
  connect(compare, &QToolButton::toggled, target, &ImageView::setCompare);
  connect(compare, &QToolButton::clicked, this, [this, target] {
    if (workflowMode_ && target == nodeView_) refreshNodeOutput();
  });
  connect(target, &ImageView::displayStateChanged, this,
          &MainWindow::syncViewTools);
  connect(target, &ImageView::toolChanged, this, &MainWindow::syncViewTools);
}
void MainWindow::buildImageNavigation(ImageView* target) {
  auto* rail = new QFrame(target);
  rail->setObjectName("viewTools");
  auto* layout = new QVBoxLayout(rail);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(3);
  for (const auto& pair : QVector<QPair<QString, ImageView::Tool>>{
           {"select", ImageView::Tool::Select},
           {"pan", ImageView::Tool::Pan},
           {"rect", ImageView::Tool::Rectangle},
           {"circle", ImageView::Tool::Circle}}) {
    auto* button = studioIconButton(pair.first,
                              pair.second == ImageView::Tool::Pan
                                  ? QStringLiteral("平移")
                              : pair.second == ImageView::Tool::Rectangle
                                  ? QStringLiteral("矩形 ROI")
                              : pair.second == ImageView::Tool::Circle
                                  ? QStringLiteral("圆形 ROI")
                                  : QStringLiteral("选择 / 调整 ROI"),
                              rail);
    QAction* shared=nullptr;
    if(target==view_) {
      shared=pair.second==ImageView::Tool::Select ? selectAction_
             : pair.second==ImageView::Tool::Pan ? panAction_
             : pair.second==ImageView::Tool::Rectangle ? rectangleAction_ : circleAction_;
      button->setDefaultAction(shared);
    }
    button->setObjectName("imageTool" + QString::number(int(pair.second)));
    button->setProperty("imageTool", int(pair.second));
    button->setCheckable(true);
    if (pair.second == ImageView::Tool::Rectangle ||
        pair.second == ImageView::Tool::Circle) {
      auto* menu = new QMenu(button);
      auto* action = menu->addAction(pair.second == ImageView::Tool::Rectangle
                                         ? QStringLiteral("旋转矩形 ROI")
                                         : QStringLiteral("环形 ROI"));
      action->setObjectName(pair.second == ImageView::Tool::Rectangle
                                ? "rotatedRoiAction"
                                : "annulusRoiAction");
      connect(action, &QAction::triggered, target, [target, pair] {
        target->setTool(pair.second == ImageView::Tool::Rectangle
                            ? ImageView::Tool::RotatedRectangle
                            : ImageView::Tool::Annulus);
      });
      button->setMenu(menu);
      button->setPopupMode(QToolButton::MenuButtonPopup);
    }
    layout->addWidget(button);
    if(!shared)connect(button, &QToolButton::clicked, target,
                      [target, pair] { target->setTool(pair.second); });
  }
  auto* fit = studioIconButton("fit", QStringLiteral("适应"), rail);
  layout->addWidget(fit);
  connect(fit, &QToolButton::clicked, target, &ImageView::fitImage);
  auto* actual = studioIconButton("actual",QStringLiteral("原始大小 1:1"),rail);
  actual->setObjectName("actualSizeButton");
  actual->setText("1:1");
  actual->setIcon({});
  layout->addWidget(actual);
  connect(actual, &QToolButton::clicked, target, &ImageView::actualSize);
}
void MainWindow::buildParameters(QWidget* panel) {
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(13, 12, 13, 12);
  layout->setSpacing(10);
  layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
  auto* title = new QHBoxLayout;
  operatorTitle_ = new QLabel(QStringLiteral("区域分割"), panel);
  operatorTitle_->setObjectName("sectionTitle");
  operatorTitle_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  title->addWidget(operatorTitle_, 1);
  layout->addLayout(title);
  auto* rangeHeading = new QToolButton(panel);
  rangeHeading->setText(QStringLiteral("输入与范围 / 资源"));
  rangeHeading->setObjectName("rangeDetailsToggle");
  rangeHeading->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  rangeHeading->setCheckable(true);
  rangeHeading->setChecked(true);
  rangeHeading->setArrowType(Qt::DownArrow);
  layout->addWidget(rangeHeading);
  auto* rangeDetails = new QWidget(panel);
  rangeDetails->setObjectName("rangeDetails");
  auto* rangeLayout = new QVBoxLayout(rangeDetails);
  rangeLayout->setContentsMargins(0, 0, 0, 0);
  rangeLayout->setSpacing(8);
  buildRangeUi(rangeLayout, rangeDetails);
  layout->addWidget(rangeDetails);
  connect(rangeHeading, &QToolButton::toggled, this, [rangeHeading, rangeDetails](bool open) {
    rangeHeading->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    rangeDetails->setVisible(open);
  });
  parameterInput_ = wrappingLabel(panel);
  parameterInput_->hide();
  operatorDescription_ = wrappingLabel(panel);
  operatorDescription_->setObjectName("secondaryText");
  operatorDescription_->hide();
  layout->insertWidget(1, operatorDescription_);
  algorithm_ = new QComboBox(panel);
  algorithm_->setObjectName("algorithmSelector");
  for (const auto& op : operatorRegistry()) algorithm_->addItem(op.name, int(op.id));
  algorithm_->hide();
  layout->addWidget(algorithm_);
  parameterPages_ = new QWidget(panel);
  parameterLayout_ = new QVBoxLayout(parameterPages_);
  parameterLayout_->setContentsMargins(0, 0, 0, 0);
  parameterLayout_->setSpacing(8);
  auto* core = new QHBoxLayout;
  auto *coreHeading = new QLabel(QStringLiteral("基础参数"), parameterPages_);
  coreHeading->setProperty("coreHeading", true);
  coreHeading->setObjectName("parameterGroupHeading");
  core->addWidget(coreHeading, 1);
  resetParameters_ = new QToolButton(parameterPages_);
  resetParameters_->setObjectName("resetOperatorParameters");
  resetParameters_->setText(QStringLiteral("恢复默认"));
  resetParameters_->setToolTip(QStringLiteral("只恢复当前算子的处理参数，不改变 ROI、模板或连线"));
  core->addWidget(resetParameters_);
  parameterLayout_->addLayout(core);
  connect(resetParameters_, &QToolButton::clicked, this, &MainWindow::resetOperatorParameters);
  auto real = [this](const char* name, double lo, double hi, double value,
                     int decimals, QWidget* parent) {
    auto* box = new ParameterDoubleSpinBox(parent);
    box->setObjectName(name);
    box->setRange(lo, hi);
    box->setDecimals(decimals);
    box->setValue(value);
    box->setKeyboardTracking(false);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &MainWindow::parametersChanged);
    return box;
  };
  auto integer = [this](const char* name, int lo, int hi, int value,
                        QWidget* parent) {
    auto* box = new ParameterSpinBox(parent);
    box->setObjectName(name);
    box->setRange(lo, hi);
    box->setValue(value);
    box->setKeyboardTracking(false);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    connect(box, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &MainWindow::parametersChanged);
    return box;
  };
  auto* regions = new ParameterGrid;
  thresholdMethod_ = new QComboBox(regions);
  thresholdMethod_->addItems({QStringLiteral("固定灰度范围"), QStringLiteral("全局自动阈值"), QStringLiteral("局部动态阈值")});
  thresholdPolarity_ = new QComboBox(regions);
  thresholdPolarity_->addItems({QStringLiteral("亮目标"), QStringLiteral("暗目标")});
  for (auto* box : {thresholdMethod_, thresholdPolarity_})
    connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::parametersChanged);
  thresholdWindow_ = integer("localWindow", 3, 255, 31, regions);
  thresholdWindow_->setSingleStep(2);
  thresholdOffset_ = real("localOffset", 0, 65535, 8, 2, regions);
  regions->addField(QStringLiteral("阈值方法"), thresholdMethod_);
  regions->addField(QStringLiteral("前景极性"), thresholdPolarity_);
  regions->addField(QStringLiteral("局部均值窗口 / px"), thresholdWindow_);
  regions->addField(QStringLiteral("相对均值偏移"), thresholdOffset_);
  minimum_ = real("minimumGray", -2147483648., 2147483647., 105, 3, regions);
  maximum_ = real("maximumGray", -2147483648., 2147483647., 255, 3, regions);
  opening_ = real("openingRadius", 0, 100, 1.5, 2, regions);
  area_ = real("minimumArea", 0, 1e12, 150, 1, regions);
  regions->addField(QStringLiteral("灰度下限"), minimum_);
  regions->addField(QStringLiteral("灰度上限"), maximum_);
  regions->addField(QStringLiteral("开运算半径 / px"), opening_);
  regions->addField(QStringLiteral("最小面积 / px²"), area_);
  parameterForms_.append(regions);
  parameterLayout_->addWidget(regions);
  auto* edges = new ParameterGrid;
  sigma_ = real("cannyAlpha", .3, 10, 1.2, 2, edges);
  low_ = integer("cannyLow", 0, 65535, 18, edges);
  high_ = integer("cannyHigh", 0, 65535, 40, edges);
  edges->addField(QStringLiteral("平滑系数"), sigma_);
  edges->addField(QStringLiteral("低阈值"), low_);
  edges->addField(QStringLiteral("高阈值"), high_);
  parameterForms_.append(edges);
  parameterLayout_->addWidget(edges);
  auto* filtering = new ParameterGrid;
  filterType_ = new QComboBox(filtering);
  filterType_->setObjectName("filterMethod");
  filterType_->addItems({QStringLiteral("均值滤波"),
                         QStringLiteral("高斯滤波"),
                         QStringLiteral("中值滤波")});
  mask_ = integer("filterSize", 3, 31, 7, filtering);
  mask_->setSingleStep(2);
  filtering->addField(QStringLiteral("滤波方法"), filterType_);
  filtering->addField(QStringLiteral("窗口尺寸 / px"), mask_);
  parameterForms_.append(filtering);
  parameterLayout_->addWidget(filtering);
  auto* gray = new ParameterGrid;
  grayType_ = new QComboBox(gray);
  grayType_->setObjectName("grayMethod");
  grayType_->addItems(
      {QStringLiteral("转为灰度"), QStringLiteral("灰度反相")});
  gray->addField(QStringLiteral("处理方法"), grayType_);
  parameterForms_.append(gray);
  parameterLayout_->addWidget(gray);
  auto* matching = new ParameterGrid;
  templatePath_ = new QLineEdit(matching);
  templatePath_->setObjectName("templatePath");
  templatePath_->setPlaceholderText(QStringLiteral("请在模板页设置参考图像"));
  auto* browse = new QPushButton(QStringLiteral("选择模板…"), matching);
  score_ = real("matchScore", 0, 1, .7, 3, matching);
  matches_ = integer("matchCount", 1, 100, 1, matching);
  angleStart_ = real("angleStart", -180, 180, -10, 1, matching);
  angleExtent_ = real("angleExtent", 0, 360, 20, 1, matching);
  templatePath_->hide();
  browse->hide();
  matching->addField(QStringLiteral("最低得分"), score_);
  matching->addField(QStringLiteral("匹配数量"), matches_);
  matching->addField(QStringLiteral("起始角度 / °"), angleStart_);
  matching->addField(QStringLiteral("角度范围 / °"), angleExtent_);
  parameterForms_.append(matching);
  parameterLayout_->addWidget(matching);
  connect(browse, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getOpenFileName(
        this, QStringLiteral("模板图像"), {},
        QStringLiteral("图像 (*.png *.jpg *.bmp *.tif *.tiff)"));
    if (!path.isEmpty()) templatePath_->setText(path);
  });
  connect(templatePath_, &QLineEdit::textChanged, this,
          [this](const QString& path) {
            if (!syncing_ && !path.isEmpty()) setReferenceImage(path);
          });
  connect(filterType_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &MainWindow::parametersChanged);
  connect(grayType_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &MainWindow::parametersChanged);
  buildGeometryUi();
  layout->addWidget(parameterPages_);
  auto* note = wrappingLabel(panel);
  note->setObjectName("secondaryText");
  note->setText(QStringLiteral(
      "F5 运行当前数据；Shift+F5 运行所选。自动预览可在执行菜单关闭。"));
  layout->addWidget(note);
  validation_ = wrappingLabel(panel);
  validation_->setObjectName("validationMessage");
  layout->addWidget(validation_);
  auto* advanced = new QToolButton(panel);
  advanced->setText(QStringLiteral("高级参数"));
  advanced->setObjectName("advancedParameters");
  advanced->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  advanced->setArrowType(Qt::RightArrow);
  advanced->setCheckable(true);
  layout->addWidget(advanced);
  advancedPages_ = new QWidget(panel);
  advancedPages_->setObjectName("advancedParameterPages");
  auto *advancedLayout = new QVBoxLayout(advancedPages_);
  advancedLayout->setContentsMargins(0, 0, 0, 0);
  for (auto *page : parameterAdvancedForms_)
    advancedLayout->addWidget(page);
  advancedPages_->hide();
  layout->addWidget(advancedPages_);
  advancedDetails_ = wrappingLabel(panel);
  advancedDetails_->setObjectName("secondaryText");
  advancedDetails_->hide();
  layout->addWidget(advancedDetails_);
  connect(advanced, &QToolButton::toggled, this, [this, advanced](bool open) {
    advanced->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    advancedDetails_->setVisible(open);
    advancedPages_->setVisible(open);
  });
  layout->addStretch();
  connect(algorithm_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int index) {
            if (index < 0) return;
            showParameterPage(algorithm_->itemData(index).toInt());
            operatorTitle_->setText(operatorRegistry()[index].name);
            operatorDescription_->setText(
                operatorSummary(operatorRegistry()[index].id));
            advancedDetails_->setText(
                QStringLiteral(
                    "输出类型：%1\n图像和 ROI "
                    "使用原图坐标。参数改变使旧结果失效，自动更新当前图像。")
                    .arg(operatorRegistry()[index].output));
            parametersChanged();
          });
  showParameterPage(0);
}

void MainWindow::showParameterPage(int index) {
  const int position = algorithm_->findData(index);
  const int current =
      std::clamp(position >= 5 ? position - 1 : position, 0, parameterForms_.size() - 1);
  for (int i = 0; i < parameterForms_.size(); ++i)
    parameterForms_[i]->setVisible(i == current);
  for (int i = 0; i < parameterInputForms_.size(); ++i) {
    const bool selected = i == current - 5;
    auto *inputs = static_cast<ParameterGrid *>(parameterInputForms_[i]);
    auto *advanced = static_cast<ParameterGrid *>(parameterAdvancedForms_[i]);
    inputs->setVisible(selected && inputs->hasFields());
    advanced->setVisible(selected && advanced->hasFields());
  }
  const bool configurable =
      static_cast<ParameterGrid *>(parameterForms_[current])->hasFields();
  parameterPages_->findChild<QLabel *>("parameterGroupHeading")
      ->setVisible(configurable);
  resetParameters_->setVisible(configurable);
}

void MainWindow::parametersChanged() {
  if (syncing_) return;
  EditGuard edit(*this, QStringLiteral("修改参数"),
                 editMergeKey(sender() ? sender()->objectName() : QString("parameters")));
  auto& p = document_.parameters;
  if (optionTool(p.algorithm)) {
    for (const auto& field : geometryFields_)
      if (field.first->property("tool").toInt() == int(p.algorithm)) {
        if (auto* box = qobject_cast<QDoubleSpinBox*>(field.first))
          p.options[field.second] = box->value();
        else if (auto* integerBox = qobject_cast<QSpinBox*>(field.first))
          p.options[field.second] = integerBox->value();
        else if (auto* line = qobject_cast<QLineEdit*>(field.first))
          p.options[field.second] = line->text().trimmed().left(128);
        else if (auto* choice = qobject_cast<QComboBox*>(field.first))
          p.options[field.second] =
              choice->currentData().isValid()
                  ? QJsonValue::fromVariant(choice->currentData())
                  : QJsonValue(choice->currentIndex());
      }
  }
  p.algorithm = static_cast<Algorithm>(algorithm_->currentData().toInt());
  if (p.algorithm == Algorithm::Regions) {
    p.options["thresholdMethod"] = thresholdMethod_->currentIndex();
    p.options["dark"] = thresholdPolarity_->currentIndex();
    p.options["localWindow"] = thresholdWindow_->value();
    p.options["localOffset"] = thresholdOffset_->value();
  }
  p.minimum = minimum_->value();
  p.maximum = maximum_->value();
  p.opening = opening_->value();
  p.minimumArea = area_->value();
  p.sigma = sigma_->value();
  p.edgeLow = low_->value();
  p.edgeHigh = high_->value();
  p.filter = filterType_->currentIndex();
  p.mask = mask_->value();
  p.gray = grayType_->currentIndex();
  p.templatePath = templatePath_->text();
  p.score = score_->value();
  p.matches = matches_->value();
  p.angleStart = angleStart_->value();
  p.angleExtent = angleExtent_->value();
  if (nodeContext() && selectedStep_ < 0) {
    updateActions();
    return;
  }
  if (nodeContext() && selectedStep_ >= 0 &&
      selectedStep_ < workflow_.size()) {
    if(sourceTool(p.algorithm) && outputType(workflow_[selectedStep_])!=outputType(p)) {
      const auto oldName=operatorDefinition(p.algorithm).name;
      if(workflowAliases_.value(selectedStep_)==oldName || workflowAliases_.value(selectedStep_)==QStringLiteral("多视输入"))workflowAliases_[selectedStep_]=QString();
    }
    const bool captureModeChanged = p.algorithm == Algorithm::StereoRectify &&
        workflow_[selectedStep_].options.value("multiView").toInt() != p.options.value("multiView").toInt();
    workflow_[selectedStep_] = p;
    if (captureModeChanged) {
      workflowChanged();
      updateParameterGuidance();
      return;
    }
    workflowAliases_=nodeNames();
    for (auto& image : images_) ++image.version;
    refreshWorkflow();
  }
  if (!nodeContext())
    document_.roi = effectiveRange().scope == ScopeMode::Manual
                        ? effectiveRange().roi
                        : Roi{};
  invalidate();
  setDirty(true);
  if (!nodeContext()) updateRangeView();
  syncGeometryUi();
  updateParameterGuidance();
  updateActions();
}
void MainWindow::syncParameters() {
  syncing_ = true;
  const auto& p = document_.parameters;
  algorithm_->setCurrentIndex(algorithm_->findData(static_cast<int>(p.algorithm)));
  showParameterPage(static_cast<int>(p.algorithm));
  operatorTitle_->setText(multiViewInput(p) ? QStringLiteral("多视输入") : operatorDefinition(p.algorithm).name);
  operatorDescription_->setText(operatorSummary(p.algorithm));
  advancedDetails_->setText(
      QStringLiteral(
          "输出：%1\n图像和 ROI "
          "使用原图坐标。参数改变后重新处理当前图像。")
          .arg(outputSummary(p)));
  thresholdMethod_->setCurrentIndex(p.options.value("thresholdMethod").toInt());
  thresholdPolarity_->setCurrentIndex(p.options.value("dark").toInt());
  thresholdWindow_->setValue(p.options.value("localWindow").toInt(31));
  thresholdOffset_->setValue(p.options.value("localOffset").toDouble(8));
  minimum_->setValue(p.minimum);
  maximum_->setValue(p.maximum);
  opening_->setValue(p.opening);
  area_->setValue(p.minimumArea);
  sigma_->setValue(p.sigma);
  low_->setValue(p.edgeLow);
  high_->setValue(p.edgeHigh);
  filterType_->setCurrentIndex(p.filter);
  mask_->setValue(p.mask);
  grayType_->setCurrentIndex(p.gray);
  templatePath_->setText(p.templatePath);
  score_->setValue(p.score);
  matches_->setValue(p.matches);
  angleStart_->setValue(p.angleStart);
  angleExtent_->setValue(p.angleExtent);
  syncing_ = false;
  syncGeometryUi();
  updateParameterGuidance();
  refreshProperties();
}
void MainWindow::invalidate() {
  ++revision_;
  if(std::any_of(workflow_.begin(),workflow_.end(),[](const Parameters& p){return calibrationTool(p.algorithm);})) {
    for(auto& image:images_){++image.version;image.workflowResult={};image.workflowResultVersion=0;}
    result_={};
  }
  if (!nodeContext()) {
    result_ = {};
    view_->setResults({});
    view_->setResultImage({});
  }
  if (activeImage_ >= 0) {
    if (nodeContext())
      ++images_[activeImage_].version;
    else
      ++images_[activeImage_].baseVersion;
    images_[activeImage_].document = document_;
    if (nodeContext())
      images_[activeImage_].document.parameters = workbenchParameters_;
  }
  summary_->setText(result_.objects.isEmpty() && result_.preview.isNull()
                        ? QStringLiteral("尚未运行")
                    : nodeContext()
                        ? QStringLiteral("等待重新计算")
                        : QStringLiteral("参数已更改 · 显示上次快照"));
  taskStatus_->setText(execution_.busy()
                           ? QStringLiteral("计算中 · 配置已变化")
                           : QStringLiteral("待运行"));
  setResultState(QStringLiteral("配置已更改"), "stale");
  refreshExecutionTiming(true);
  if (nodeContext()) refreshNodeOutput();
  refreshProperties();
  refreshFilm();
  schedulePreview();
  updateActions();
}
void MainWindow::storeActive() {
  if (activeImage_ >= 0 && activeImage_ < images_.size()) {
    images_[activeImage_].document = document_;
    if (nodeContext())
      images_[activeImage_].document.parameters = workbenchParameters_;
    if (!nodeContext()) images_[activeImage_].result = result_;
  }
}
bool MainWindow::activateImage(int index) {
  if ((execution_.busy() || runQueue_.active()) && !batchSwitch_) return false;
  if (index < 0 || index >= images_.size()) return false;
  ++editNavigation_;
  autoPreview_->stop();
  storeActive();
  activeImage_ = index;
  document_ = images_[index].document;
  if ((document_.rightImage.CountObj() || !document_.structuredFrames.isEmpty()) &&
      !property("captureViewsInitialized").toBool()) {
    setProperty("captureViewsInitialized", true);
    view2d_->setChecked(true);
    view3d_->setChecked(true);
    updateViews();
  }
  if (document_.nodeSettings.isEmpty())
    document_.nodeSettings.resize(workflow_.size());
  document_.rangeOverrides.resize(workflow_.size());
  document_.workbenchRanges.resize(toolSlotCount);
  document_.workbenchOverrides.resize(toolSlotCount);
  workbenchParameters_ = document_.parameters;
  if (!nodeContext())
    document_.roi = effectiveRange().scope == ScopeMode::Manual
                        ? effectiveRange().roi
                        : Roi{};
  if (nodeContext() && !workflow_.isEmpty())
    document_.parameters =
        workflow_[std::clamp(selectedStep_, 0, workflow_.size() - 1)];
  result_ = nodeContext() ? images_[index].workflowResultVersion ==
                                    images_[index].version
                                ? images_[index].workflowResult
                                : Result{}
            : images_[index].resultVersion == images_[index].baseVersion
                ? images_[index].result
                : Result{};
  ++revision_;
  view_->setImage(document_.preview);
  view_->setRoi(document_.roi);
  view_->setImageName(QFileInfo(document_.imagePath).fileName());
  view_->setPixelText(QStringLiteral("Row — · Col — · 原始图像"));
  syncParameters();
  showResult(result_);
  if (trainingView()) updateRangeView();
  if (nodeContext()) refreshWorkflow();
  refreshProperties();
  refreshFilm();
  updateActions();
  return true;
}
void MainWindow::activateImageAndRun(int index) {
  if (!activateImage(index)) return;
  // Internal project/batch activation can restore a cache. An explicit
  // thumbnail click is a new inspection and must produce fresh data/time.
  if (nodeContext()) {
    images_[index].workflowResult = {};
    workflowView_->resetExecution();
  } else {
    images_[index].result = {};
  }
  showResult({});
  refreshExecutionTiming(true);
  run();
}
void MainWindow::installDocument(Document document, const QString& project) {
  debugNodeId_.clear();
  QVector<ImageResource> loaded{
      {imageId(), std::move(document), {}, 0, true}};
  if (project.isEmpty()) {
    sharedRanges_ = QVector<NodeSettings>(workflow_.size());
    workbenchRanges_ = QVector<NodeSettings>(toolSlotCount);
    models_.clear();
  } else
    migrateRanges(loaded, workflow_, 0, project);
  images_.clear();
  lastSingleImage_.clear();
  activeImage_ = -1;
  document_ = {};
  result_ = {};
  projectPath_ = project;
  copyOriginalDataOnSave_ = false;
  images_ = std::move(loaded);
  normalizeModelOwnership();
  activateImage(0);
  resetEditHistory();
  setDirty(false);
  pendingOperator_.clear();
  refreshExecutionTiming(true);
  setOperatorStatus(QStringLiteral("就绪"),"idle");
  setResultState(QStringLiteral("尚未运行"),"idle");
  taskStatus_->setText(QStringLiteral("就绪"));
}
bool MainWindow::addImages(const QStringList& paths) {
  if (loading_ || execution_.busy() || runQueue_.active()) return false;
  return guard(QStringLiteral("添加图像"), [&] {
    QVector<ImageResource> loaded;
    const auto parameters=nodeContext() ? workbenchParameters_ : document_.parameters;
    loadResources(QStringLiteral("读取图像及校验像素…"),[&loaded,paths,parameters](const std::atomic_bool& cancelled){
      for(const auto& path:paths) {
        if(cancelled.load())return;
        auto doc=loadImage(path);doc.parameters=parameters;
        loaded.append({imageId(),std::move(doc),{},0,true});
      }
    });
    if(loaded.isEmpty())return;
    EditGuard edit(*this,QStringLiteral("添加图像"));
    storeActive();
    for (auto& resource : loaded) images_.append(std::move(resource));
    activateImage(images_.size() - loaded.size());
    setDirty(true);
    log(QStringLiteral("已添加 %1 张图像").arg(loaded.size()));
  });
}
void MainWindow::removeImages(bool clear) {
  if (images_.isEmpty() || execution_.busy() || runQueue_.active()) return;
  if(activeInputNode()>=0) {
    const auto key=workflow_[activeInputNode()].options.value("binding").toString();QVector<int> rows;
    for(int row:trackSamples())if(clear||images_[row].checked)rows.append(row);
    if(rows.isEmpty() && trackSamples().contains(activeImage_))rows.append(activeImage_);
    SourceData empty;empty.sequence=workflow_[activeInputNode()].algorithm==Algorithm::SequenceInput;
    setSampleInputs(rows,key,empty);syncInputTrack();return;
  }
  EditGuard edit(*this, QStringLiteral("移除图像"));
  autoPreview_->stop();
  const auto currentParameters = document_.parameters;
  storeActive();
  QString active = images_[activeImage_].id;
  bool selected = false;
  for (const auto& i : images_) selected |= i.checked;
  for (int i = images_.size() - 1; i >= 0; --i)
    if (clear || images_[i].checked || (!selected && i == activeImage_))
      images_.remove(i);
  activeImage_ = -1;
  document_ = {};
  document_.parameters = currentParameters;
  result_ = {};
  ++revision_;
  if (images_.isEmpty()) {
    view_->setImage({});
    showResult({});
    view_->setImageName({});
    view_->setPixelText(QStringLiteral("Row — · Col —"));
    refreshFilm();
    refreshProperties();
    updateActions();
  } else {
    int index = 0;
    for (int i = 0; i < images_.size(); ++i)
      if (images_[i].id == active) index = i;
    activateImage(index);
  }
  setDirty(true);
}
void MainWindow::selectImages(bool selected) {
  if (loading_ || execution_.busy() || runQueue_.active()) return;
  lastSingleImage_.clear();
  for (int row:trackSamples()) images_[row].checked = selected;
  if(std::any_of(workflow_.begin(),workflow_.end(),[](const Parameters& p){return calibrationTool(p.algorithm);}))invalidate();
  refreshFilm();
  updateActions();
}
void MainWindow::refreshFilm() {
  const auto visible=trackSamples();const int input=activeInputNode();
  const int total=visible.size();
  constexpr int cardWidth=112, cardGap=7, cardStride=cardWidth+cardGap;
  filmScroll_->setProperty("rebuildingFilm",true);
  auto* bar=filmScroll_->horizontalScrollBar();
  const QSignalBlocker block(bar);
  const int page=std::max(1,filmScroll_->viewport()->width());
  const int minimumWidth=int(std::min<qint64>(QWIDGETSIZE_MAX,std::max<qint64>(0,qint64(total)*cardStride-cardGap)));
  const int width=std::max(page,minimumWidth);
  const int maximum=std::max(0,width-page);
  int scroll=std::clamp(bar->value(),0,maximum);
  const auto current=activeImage_>=0 && activeImage_<images_.size() ? images_[activeImage_].id : QString();
  if (filmScroll_->property("currentFilm").toString()!=current && activeImage_>=0) {
    const int left=visible.indexOf(activeImage_)*cardStride,right=left+cardWidth;
    if(left<scroll)scroll=std::max(0,left-7);
    else if(right>scroll+page)scroll=std::min(maximum,right-page+7);
  }
  filmScroll_->setProperty("currentFilm",current);
  auto* contents=filmScroll_->widget();
  if(!contents){contents=new QWidget;filmScroll_->setWidget(contents);}
  delete contents->layout();
  // Keep the scroll area and its scrollbar stable while replacing only visible
  // cards. Offscreen widgets do not accumulate when a long filmstrip is dragged.
  const auto previous=contents->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly);
  for(auto* child:previous){child->hide();child->setParent(nullptr);child->deleteLater();}
  contents->setMinimumSize(minimumWidth,92);contents->resize(width,92);
  bar->setRange(0,maximum);bar->setPageStep(page);bar->setValue(scroll);
  const auto flowGroup=inputGroupNodes();
  const bool judgementConfigured=nodeContext()
      ? std::any_of(workflow_.begin(),workflow_.end(),[&](const Parameters& p){const int node=int(&p-workflow_.constData());return p.algorithm==Algorithm::Judge && (activeInputNode()<0 || flowGroup.contains(node));})
      : document_.parameters.algorithm==Algorithm::Judge;
  bool calibrationPoses = false;
  for (int node : flowGroup)
    if ((workflow_[node].algorithm == Algorithm::StructuredCalibration || workflow_[node].algorithm == Algorithm::StereoCalibration)
        && input >= 0 && rangeForNode(node).dataA == input) calibrationPoses = true;
  const bool virtualized=total>64;
  const int first=virtualized ? std::max(0,scroll/cardStride-2) : 0;
  const int last=virtualized ? std::min(total,(scroll+page+cardStride-1)/cardStride+2) : total;
  filmScroll_->setProperty("firstFilm",first);filmScroll_->setProperty("lastFilm",last);
  const bool locked = execution_.busy() || runQueue_.active();
  int selected = 0;for(int row:visible)selected+=images_[row].checked;
  for (int slot = first; slot < last; ++slot) {
    const int i=visible[slot];
    const auto& resource = images_[i];
    auto* frame = new QFrame(contents);
    frame->setObjectName("filmFrame");
    frame->setFixedSize(cardWidth, 92);
    frame->move(slot*cardStride,0);
    auto* box = new QVBoxLayout(frame);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(3);
    auto* thumb = new QPushButton(frame);
    thumb->setObjectName("thumbnail" + QString::number(i));
    thumb->setProperty("filmPreview", true);
    thumb->setProperty("current", i == activeImage_);
    thumb->setProperty("running",
                       resource.id == pendingImage_ && execution_.busy());
    SourceData cell;
    if(input>=0)cell=resolveSourceData(i==activeImage_?document_:resource.document,workflow_[input].options.value("binding").toString());
    QImage thumbnail=input>=0 ? (cell.frames.isEmpty()?QImage():makePreview(cell.frames.first()).image) : resource.document.preview;
    if(!cell.views.isEmpty() && cell.frames.size()>=2) {
      thumbnail=QImage(128,56,QImage::Format_RGB32);thumbnail.fill(Qt::black);QPainter painter(&thumbnail);
      painter.drawImage(QRect(0,0,63,56),makePreview(cell.frames[0]).image);painter.drawImage(QRect(65,0,63,56),makePreview(cell.frames[1]).image);
      painter.setPen(Qt::white);painter.drawText(3,13,"L");painter.drawText(68,13,"R");
    } else if (input<0 && resource.document.rightImage.CountObj() && resource.document.structuredFrames.isEmpty()) {
      thumbnail = QImage(128, 56, QImage::Format_RGB32);
      thumbnail.fill(Qt::black);
      QPainter p(&thumbnail);
      p.drawImage(QRect(0, 0, 63, 56), resource.document.preview);
      p.drawImage(QRect(65, 0, 63, 56), resource.document.rightPreview);
      p.setPen(Qt::white);
      p.drawText(3, 13, "L");
      p.drawText(68, 13, "R");
    }
    thumb->setIcon(QIcon(QPixmap::fromImage(thumbnail)));
    thumb->setIconSize(QSize(cardWidth-12, 56));
    thumb->setFixedHeight(61);
    thumb->setEnabled(!locked);
    thumb->setAccessibleName(
        QStringLiteral("显示图像 %1")
            .arg(QFileInfo(resource.document.imagePath).fileName()));
    thumb->setToolTip(
        locked ? QStringLiteral("运行期间不能切换图像，请先停止")
               : QStringLiteral(
                     "点击切换并重新处理此图像；下方名称区域独立选择"));
    thumb->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(thumb,&QPushButton::customContextMenuRequested,this,[this,thumb,i](QPoint point) {
      if (busy()) return;
      QMenu menu(thumb);
      if (!images_[i].document.structuredFrames.isEmpty() || activeInputNode()>=0 && workflow_[activeInputNode()].algorithm==Algorithm::SequenceInput)
        menu.addAction(QStringLiteral("浏览图像序列…"),this,[this,i]{showStructuredSequence(i);});
      menu.addAction(QStringLiteral("资源管理…"),this,[this,i]{showResourceManager({},i);});
      menu.exec(thumb->mapToGlobal(point));
    });
    DeliveryRecord record;
    record.utc=resource.deliveryUtc; record.result=resource.deliveryResult;
    record.current=!record.utc.isEmpty() && resource.deliveryVersion==(resource.deliveryWorkflow ? resource.version : resource.baseVersion);
    const bool running=resource.id==pendingImage_ && execution_.busy();
    const auto state=deliveryState(record);
    bool judgedNode=!nodeContext();if(nodeContext())for(int node:flowGroup)if(workflow_[node].algorithm==Algorithm::Judge && node<record.result.nodeOutputs.size())judgedNode|=record.result.nodeOutputs[node].state==NodeState::Success;
    if(nodeContext() && activeInputNode()<0)judgedNode=true;
    const bool judged=judgementConfigured && judgedNode && resource.deliveryWorkflow==nodeContext() &&
        !running && (state=="OK" || state=="NG");
    auto* badge=new QLabel(judged ? state : QString(),thumb);
    badge->setObjectName("sampleVerdict"+QString::number(i));
    badge->setProperty("filmVerdict",true);badge->setProperty("state",judged ? state : QString());
    badge->setAttribute(Qt::WA_TransparentForMouseEvents);
    badge->setAccessibleName(judged ? QStringLiteral("检测结果：")+state : QString());
    badge->adjustSize();badge->move(3,3);badge->setVisible(judged);
    thumb->setAccessibleDescription(QStringLiteral("处理状态：")+(running ? QStringLiteral("运行中") : state));
    box->addWidget(thumb);
    auto* selection = new QToolButton(frame);
    selection->setObjectName("imageSelect" + QString::number(i));
    selection->setCheckable(true);
    selection->setChecked(resource.checked);
    selection->setText(
        resource.document.captureId.isEmpty()
            ? QFileInfo(resource.document.imagePath).fileName()
            : !resource.document.captureName.isEmpty() ? resource.document.captureName
            : QFileInfo(resource.document.imagePath).completeBaseName() +
                  " / " +
                  QFileInfo(resource.document.rightPath).completeBaseName());
    if(input>=0&&!cell.paths.isEmpty())selection->setText((cell.sequence?QStringLiteral("%1 帧 · ").arg(cell.frames.size()):QString())+QFileInfo(cell.paths.first()).fileName());
    const auto caption=selection->text();
    const auto kind=input>=0?(multiViewInput(workflow_[input])?Algorithm::StereoImage:workflow_[input].algorithm)
        :!resource.document.structuredFrames.isEmpty()?Algorithm::SequenceInput:resource.document.rightImage.CountObj()?Algorithm::StereoImage:Algorithm::ImageInput;
    selection->setIcon(studioIcon(operatorIconName(kind)));
    selection->setProperty("inputIcon",operatorIconName(kind));
    selection->setProperty("inputBinding",input>=0?workflow_[input].options["binding"].toString():QString());
    selection->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    selection->setProperty("batchSelection", true);
    selection->ensurePolished();
    QString visibleCaption = caption;
    if (calibrationPoses) {
      visibleCaption = QStringLiteral("姿态 %1").arg(slot + 1, 2, 10, QChar('0'));
      if (cell.sequence) visibleCaption += QStringLiteral(" · %1帧").arg(cell.frames.size());
    }
    selection->setText(selection->fontMetrics().elidedText(visibleCaption,Qt::ElideMiddle,cardWidth-30));
    selection->setFixedHeight(28);
    selection->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    selection->setAccessibleName(
        QStringLiteral("选择图像 %1")
            .arg(QFileInfo(resource.document.imagePath).fileName()));
    selection->setToolTip((input>=0?nodeNames().value(input)+"\n":QString())+caption+"\n"+QStringLiteral(
        "点击名称区域选择 / 取消选择，绿色边框表示已选中；不切换当前图像"));
    selection->setEnabled(!locked);
    box->addWidget(selection);
    frame->show();
    connect(thumb, &QPushButton::clicked, this,
            [this, i] { activateImageAndRun(i); });
    connect(selection, &QToolButton::toggled, this, [this, i](bool checked) {
      if (i >= images_.size() || execution_.busy() || runQueue_.active()) return;
      images_[i].checked = checked;
      if(std::any_of(workflow_.begin(),workflow_.end(),[](const Parameters& p){return calibrationTool(p.algorithm);}))invalidate();
      lastSingleImage_.clear();
      refreshFilm();
      updateActions();
    });
  }
  if (visible.isEmpty()) {
    auto* label =
        new QLabel(input<0 ? QStringLiteral("先添加输入节点") : QStringLiteral("此输入暂无数据 · 点击 + 添加"), contents);
    label->setObjectName("emptySampleHint");label->setTextFormat(Qt::PlainText);label->setWordWrap(true);
    label->setAlignment(Qt::AlignCenter);
    auto* emptyLayout=new QVBoxLayout(contents);emptyLayout->setContentsMargins(8,0,8,0);
    emptyLayout->addWidget(label);label->show();
  }
  filmScroll_->setProperty("rebuildingFilm",false);
  filmCount_->setText(
      QStringLiteral("%1 项 · 已选 %2 项").arg(total).arg(selected));
  if (spatialView_ && depthScaleMode_ == 1 && !busy()) selectSpatialRaster();
  for (const char* name : {"removeImagesButton", "clearImagesButton",
                           "selectAllImages", "deselectAllImages", "resourceManagerButton"})
    findChild<QPushButton*>(name)->setEnabled(!images_.isEmpty() && !locked);
  findChild<QPushButton*>("addImagesButton")->setEnabled(!locked && input>=0);
  findChild<QPushButton*>("addImagesButton")->setToolTip(input<0?QStringLiteral("先添加输入节点"):QStringLiteral("添加到：")+nodeNames().value(input));
  findChild<QPushButton*>("addImagesButton")->setAccessibleName(input<0?QStringLiteral("添加输入数据"):QStringLiteral("添加到：")+nodeNames().value(input));
  if(auto* types=findChild<QToolButton*>("addDataTypesButton"))types->setEnabled(!locked && input>=0);
}
bool MainWindow::busy() const {
  return loading_ || execution_.busy() || runQueue_.active() || autoPreview_->isActive();
}
void MainWindow::schedulePreview() {
  // Calibration is a dataset solve. Parameter/sample selection edits invalidate
  // its model, but only an explicit run starts the expensive aggregate job.
  if(nodeContext()) {
    const auto group=inputGroupNodes();
    for(int i=0;i<workflow_.size();++i)if(calibrationTool(workflow_[i].algorithm)&&(activeInputNode()<0||group.contains(i)))return;
  }
  if(activeInputNode()<0 && std::any_of(workflow_.begin(),workflow_.end(),[](const Parameters& p){return sourceTool(p.algorithm);}))return;
  if(activeInputNode()>=0 && !trackSamples(true).contains(activeImage_))return;
  if ((autoPreviewAction_ && !autoPreviewAction_->isChecked()) || runQueue_.active() || !document_.hasImage() ||
      (nodeContext() && (workflow_.isEmpty() || !workflowMode_ && selectedStep_ < 0)))
    return;
  autoPreview_->start();
  taskStatus_->setText(QStringLiteral("更新预览…"));
}
bool MainWindow::run() {
  if (loading_ || runQueue_.active()) return false;
  autoPreview_->stop();
  return startExecution(-1);
}
bool MainWindow::startExecution(int throughStep) {
  if(activeInputNode()<0 && std::any_of(workflow_.begin(),workflow_.end(),[](const Parameters& p){return sourceTool(p.algorithm);}))return false;
  if(activeInputNode()>=0 && !trackSamples(true).contains(activeImage_))return false;
  if (loading_ || execution_.busy() || runQueue_.active()) return false;
  if (nodeContext() && !workflowMode_ &&
      (selectedStep_ < 0 || selectedStep_ >= workflow_.size()))
    return guard(
        QStringLiteral("节点调试"),
        [] { throw std::runtime_error("节点已删除，请重新选择流程节点"); },
        true);
  auto snapshot = document_;
  snapshot.runWorkflow = nodeContext();
  snapshot.workflowTarget =
      !workflowMode_ && nodeContext() ? selectedStep_ : -1;
  Q_UNUSED(throughStep);
  snapshot.workflow = workflow_;
  snapshot.workflowInputs = workflowInputs_;
  snapshot.workflowLinks = workflowLinks_;
  snapshot.workflowOutput = activeInputNode()>=0 ? inputGroupTarget() : workflowOutput_;
  if(activeInputNode()>=0)snapshot.workflowScope=inputGroupNodes();
  prepareSnapshot(snapshot);
  storeActive();
  return launchSnapshot(std::move(snapshot), activeImage_);
}
namespace {
void validateSnapshot(const Document& snapshot) {
  if (!snapshot.hasImage()) throw std::runtime_error("请先打开图像");
  validateRoi(snapshot.roi, snapshot.preview.width(),
              snapshot.preview.height());
  const auto validateTemplate = [&](const Parameters& p) {
    if ((p.algorithm == Algorithm::ShapeMatch ||
         p.algorithm == Algorithm::NccMatch) &&
        p.templatePath.isEmpty() && snapshot.roi.kind == RoiKind::None)
      throw std::runtime_error("请选择模板图像，或绘制模板 ROI");
  };
  if (!snapshot.runWorkflow && !snapshot.modelsRequired)
    validateTemplate(snapshot.parameters);
  if (!snapshot.runWorkflow)
    snapshot.parameters.validate();
  else {
    if (snapshot.workflow.isEmpty())
      throw std::runtime_error("流程没有处理节点");
    workflowOrder(snapshot, true);
  }
}
}  // namespace
bool MainWindow::launchSnapshot(Document snapshot, int owner) {
  refreshExecutionTiming(true);
  return guard(
      QStringLiteral("运行"),
      [&] {
        pendingOperator_ =
            snapshot.runWorkflow
                ? snapshot.workflowTarget >= 0
                      ? QStringLiteral("节点调试 · %1")
                            .arg(nodeNames().value(snapshot.workflowTarget))
                      : QStringLiteral("流程")
                : operatorDefinition(snapshot.parameters.algorithm).name;
        validateSnapshot(snapshot);
        if (owner < 0 || owner >= images_.size())
          throw std::runtime_error("图像资源已移除");
        pendingImage_ = images_[owner].id;
        pendingWorkflow_ = snapshot.runWorkflow;
        pendingVersion_ = pendingWorkflow_ ? images_[owner].version
                                           : images_[owner].baseVersion;
        if (snapshot.runWorkflow) {
          images_[owner].workflowResult = {};
          workflowView_->resetExecution();
          if (owner == activeImage_) refreshNodeOutput();
        }
        images_[owner].deliveryUtc.clear();
        const auto frozen=freezeRunSnapshot(snapshot,pendingImage_,pendingVersion_);
        pendingProvenance_=frozen.provenance;pendingQueueGeneration_=runQueue_.generation();
        if (!execution_.start(*frozen.document, revision_))
          throw std::runtime_error("计算任务尚未结束");
        if (snapshot.runWorkflow)
          for (const auto& step : snapshot.workflow)
            rememberTool(step.algorithm, multiViewInput(step));
        else
          rememberTool(snapshot.parameters.algorithm, multiViewInput(snapshot.parameters));
        progress_->setRange(runQueue_.active() ? 0 : 0,
                            runQueue_.active() ? runQueue_.items().size() : 0);
        progress_->setValue(runQueue_.index());
        progress_->show();
        images_[owner].batchState = 2;
        setResultState(QStringLiteral("运行中"), "running");
        setOperatorStatus(QStringLiteral("正在运行"), "running");
        taskStatus_->setText(runQueue_.active() ? QStringLiteral("运行 %1 / %2")
                                                .arg(runQueue_.index() + 1)
                                                .arg(runQueue_.items().size())
                                          : QStringLiteral("计算当前数据…"));
        refreshFilm();
        updateActions();
      },
      true);
}
bool MainWindow::runSelected() { return beginBatch(false); }
bool MainWindow::runNext() { return beginBatch(true); }
bool MainWindow::beginBatch(bool single) {
  if (loading_ || execution_.busy() || runQueue_.active()) return false;
  autoPreview_->stop();
  storeActive();
  pendingOperator_ =
      nodeContext() ? QStringLiteral("流程")
                    : operatorDefinition(document_.parameters.algorithm).name;
  return guard(
      QStringLiteral("运行所选"),
      [&] {
        QStringList selected;for(int row:trackSamples(true))if(images_[row].checked)selected<<images_[row].id;
        const auto planned=planRunSamples(selected,lastSingleImage_,single);
        const QSet<QString> wanted(planned.begin(),planned.end());
        QVector<BatchItem> items;
        for (const auto& image : images_)
          if (wanted.contains(image.id)) {
            auto snapshot = image.document;
            snapshot.parameters =
                nodeContext() ? workbenchParameters_ : document_.parameters;
            snapshot.runWorkflow = nodeContext();
            snapshot.workflowTarget =
                !workflowMode_ && nodeContext() ? selectedStep_ : -1;
            snapshot.workflow = workflow_;
            snapshot.workflowInputs = workflowInputs_;
            snapshot.workflowLinks = workflowLinks_;
            snapshot.workflowOutput = activeInputNode()>=0 ? inputGroupTarget() : workflowOutput_;
  if(activeInputNode()>=0)snapshot.workflowScope=inputGroupNodes();
            prepareSnapshot(snapshot);
            items.append({image.id, std::move(snapshot)});
          }
        if(single)lastSingleImage_=items.first().id;else lastSingleImage_.clear();
        if (items.size() > 1 && !items.first().snapshot.calibrationInputs.isEmpty()) {
          batchCalibrationDirectory_ = std::make_shared<QTemporaryDir>();
          if (batchCalibrationDirectory_->isValid())
            for (auto& item : items) item.snapshot.batchCalibrationDirectory = batchCalibrationDirectory_->path();
        }
        runQueue_.begin(std::move(items));
        for (auto& image : images_) image.batchState = wanted.contains(image.id) ? 1 : 0;
        startNextImage();
      },
      true);
}
void MainWindow::startNextImage() {
  while (runQueue_.index() < runQueue_.items().size()) {
    const auto item = runQueue_.items()[runQueue_.index()];
    int owner = -1;
    for (int i = 0; i < images_.size(); ++i)
      if (images_[i].id == item.id) owner = i;
    if (owner >= 0) {
      if (!item.snapshot.runWorkflow)
        images_[owner].document.parameters = item.snapshot.parameters;
      if (item.snapshot.runWorkflow)
        ++images_[owner].version;
      else
        ++images_[owner].baseVersion;
      batchSwitch_ = true;
      activateImage(owner);
      batchSwitch_ = false;
      if (launchSnapshot(item.snapshot, owner)) return;
      images_[owner].batchState = 4;
    }
    runQueue_.advance(runQueue_.generation(),true);
  }
  const int count = runQueue_.items().size();
  QMap<QString,int> verdicts;
  for(const auto& item:runQueue_.items())for(const auto& image:images_)if(item.id==image.id) {
    DeliveryRecord r;r.utc=image.deliveryUtc;r.result=image.deliveryResult;
    r.current=!r.utc.isEmpty() && image.deliveryVersion==(image.deliveryWorkflow ? image.version : image.baseVersion);
    ++verdicts[deliveryState(r)];break;
  }
  bool hasJudgement = false;
  for (const auto& item : runQueue_.items()) {
    const auto& d = item.snapshot;
    if (!d.runWorkflow) hasJudgement |= d.parameters.algorithm == Algorithm::Judge;
    else {
      auto scope = d.workflowScope;
      if (d.workflowTarget >= 0) {
        scope = workflowAncestors(workflowConnections(d), d.workflowTarget);
        scope.append(d.workflowTarget);
      }
      for (int i = 0; i < d.workflow.size(); ++i)
        if ((scope.isEmpty() || scope.contains(i)) && d.workflow[i].algorithm == Algorithm::Judge)
          hasJudgement = true;
    }
  }
  runQueue_.complete();
  batchCalibrationDirectory_.reset();
  refreshInputChoices();
  syncGeometryUi();
  progress_->hide();
  view_->setRoiEditingEnabled(true);
  view_->setTool(ImageView::Tool::Select);
  const int input = activeInputNode();
  const bool groups = input >= 0 && (multiViewInput(workflow_[input]) || workflow_[input].algorithm == Algorithm::SequenceInput);
  QString status = QStringLiteral("运行完成 · %1/%2 %3执行成功")
      .arg(count - runQueue_.errors()).arg(count).arg(groups ? QStringLiteral("组") : QStringLiteral("项"));
  if (runQueue_.errors()) status += QStringLiteral(" · %1 失败").arg(runQueue_.errors());
  if (hasJudgement) status += QStringLiteral(" · OK %1 / NG %2 / 未判定 %3")
      .arg(verdicts["OK"]).arg(verdicts["NG"]).arg(verdicts[QStringLiteral("未判定")]);
  taskStatus_->setText(status);
  log(taskStatus_->text());
  refreshFilm();
  updateActions();
}
bool MainWindow::loadModel(const QString& path) {
  return guard(QStringLiteral("导入三维模型"), [&] {
    if(!document_.captureId.isEmpty())throw std::runtime_error("当前双目视图由节点输出驱动，请在普通单图工程中浏览独立点云模型");
    (workflowMode_ && nodeModelView_ ? nodeModelView_ : modelView_)->load(path);
    view3d_->setChecked(true);
    updateViews();
  });
}
void MainWindow::updateViews() {
  if (!view2d_->isChecked() && !view3d_->isChecked()) {
    auto* action = qobject_cast<QAction*>(sender());
    (action == view2d_ ? view3d_ : view2d_)->setChecked(true);
  }
  imagePane_->setVisible(workflowMode_ || view2d_->isChecked());
  canvasHeader_->setVisible(workflowMode_);
  modelView_->setVisible(!workflowMode_ && view3d_->isChecked());
  if (nodeModelView_) {
    nodeModelView_->setVisible(workflowMode_ && view3d_->isChecked());
    nodeView_->setVisible(view2d_->isChecked());
  }
  syncViewTools();
}
void MainWindow::showResult(const Result& result) {
  view_->setPixelText(QStringLiteral("Row — · Col —"));
  result_ = result;
  view_->setResults(result.objects);
  view_->setResultImage(result.preview, imageDomainPath(result.outputImage));
  view_->setLayerVisible(ImageView::Layer::Processed,
                         !result.preview.isNull());
  if (nodeContext())
    refreshNodeOutput();
  else {
    inspectResult(result);
    refreshSpatialView(result.spatial);
  }
}
void MainWindow::inspectResult(const Result& result) {
  inspectedResult_ = result;
  const QSignalBlocker selectionBlock(results_);
  results_->setSortingEnabled(false);
  const bool perTarget = !result.instances.isEmpty();
  const bool imageOnly = !result.preview.isNull() && result.objects.isEmpty();
  const bool calibration = !perTarget && !result.calibrationResult.isEmpty() &&
      !result.objects.isEmpty() && result.objects.first().values.contains("views");
  calibrationDetails_->setVisible(calibration);
  const bool marks = calibration && calibrationDetails_->currentIndex() == 1;
  const bool scalarRows = result.spatial && !result.objects.isEmpty() &&
      std::all_of(result.objects.begin(), result.objects.end(), [](const ResultObject& object) {
        return object.type == "Value" || object.type == "Plane3D";
      });
  results_->setColumnCount(perTarget ? 4 : 3);
  results_->clearContents();
  results_->setRowCount(calibration ? 0 : imageOnly ? 1 : result.objects.size());
  results_->setHorizontalHeaderLabels(
      {QStringLiteral("对象"), QStringLiteral("类型"),
       result.measureName.isEmpty() ? QStringLiteral("测量值")
                                    : result.measureName});
  if (scalarRows) results_->setHorizontalHeaderLabels(
      {QStringLiteral("名称"), QStringLiteral("单位"), QStringLiteral("数值")});
  for (int i = 0; !imageOnly && !calibration && i < result.objects.size(); ++i) {
    const auto& o = result.objects[i];
    auto* number = new QTableWidgetItem;
    number->setData(Qt::DisplayRole, scalarRows ? QVariant(o.label) : QVariant(i + 1));
    number->setData(Qt::UserRole, i);
    results_->setItem(i, 0, number);
    cell(results_, i, 1,
         scalarRows ? o.unit : o.type +
             (o.verdict >= 0 ? (o.verdict ? " · OK" : " · NG") : QString()));
    if (o.verdict >= 0)
      results_->item(i, 1)->setForeground(
          QColor(o.verdict ? "#399780" : "#cb6666"));
    auto* measure = new QTableWidgetItem;
    measure->setData(Qt::DisplayRole, o.measure);
    measure->setData(Qt::UserRole, i);
    results_->setItem(i, 2, measure);
    if (perTarget) {
      cell(results_, i, 3, QString::number(o.instance));
      results_->item(i, 3)->setData(Qt::DisplayRole, o.instance);
    }
  }
  if (calibration) showCalibrationDetails(results_, result, marks);
  if (imageOnly) {
    cell(results_, 0, 0, QStringLiteral("结果图"));
    results_->item(0, 0)->setData(Qt::UserRole, -1);
    cell(results_, 0, 1, "Image");
    cell(results_, 0, 2,
         QStringLiteral("%1 × %2")
             .arg(result.preview.width())
             .arg(result.preview.height()));
    results_->setHorizontalHeaderLabels({QStringLiteral("名称"),
                                         QStringLiteral("类型"),
                                         QStringLiteral("尺寸 / px")});
  }
  if (perTarget) {
    results_->setHorizontalHeaderItem(
        3, new QTableWidgetItem(QStringLiteral("目标")));
    for (const auto& target : result.instances) {
      if (target.state == NodeState::Success) {
        continue;
      }
      const int row = results_->rowCount();
      results_->insertRow(row);
      cell(results_, row, 0, QStringLiteral("—"));
      results_->item(row, 0)->setData(Qt::UserRole, -1);
      cell(results_, row, 1,
           target.state == NodeState::Blocked ? QStringLiteral("受阻")
                                              : QStringLiteral("报错"));
      cell(results_, row, 2, target.error);
      cell(results_, row, 3, QString::number(target.index));
      results_->item(row, 3)->setData(Qt::DisplayRole, target.index);
      results_->item(row, 2)->setToolTip(target.error);
    }
  }
  results_->setSortingEnabled(true);
  results_->clearSelection();
  const bool present = !result.pipeline.isEmpty();
  setResultState(!result.error.isEmpty() ? QStringLiteral("执行失败")
                 : present               ? QStringLiteral("执行完成")
                                         : QStringLiteral("尚未运行"),
                 !result.error.isEmpty() ? "error"
                 : present               ? "ready"
                                         : "empty");
  const bool statisticsOnly = !result.objects.isEmpty() &&
      std::all_of(result.objects.begin(), result.objects.end(), [](const ResultObject& o) { return o.type == "Value"; });
  const bool decodeStatistics = std::any_of(result.objects.begin(), result.objects.end(),
      [](const ResultObject& o) { return o.label == QStringLiteral("有效比例"); });
  summary_->setText(!result.error.isEmpty() ? result.error
                    : present               ? QStringLiteral("%1 · %2")
                                    .arg(result.pipeline)
                                    .arg(!imageOnly
                                             ? (statisticsOnly ? QStringLiteral("%1 项统计") : QStringLiteral("%1 个对象"))
                                                   .arg(result.objects.size())
                                             : QStringLiteral("1 张图像"))
                              : QString());
  if (calibration) {
    const auto& score = result.objects.first();
    QStringList metrics{QStringLiteral("重投影 RMS %1 px").arg(score.measure, 0, 'g', 6),
                        QStringLiteral("%1 个姿态").arg(int(score.values.value("views")))};
    if (result.objects.size() > 1 && result.objects[1].unit == "mm")
      metrics << QStringLiteral("%1 %2 mm").arg(result.objects[1].label).arg(result.objects[1].measure, 0, 'g', 6);
    summary_->setText(metrics.join(QStringLiteral("  ·  ")));
  }
  if (result.error.isEmpty() && result.objects.size() == 1 && result.objects.first().type == "Plane3D") {
    const auto& plane = result.objects.first().values;
    summary_->setText(QStringLiteral("单位法向 (%1, %2, %3) · D %4 mm · RMS %5 mm")
        .arg(plane.value("normalX"), 0, 'g', 5).arg(plane.value("normalY"), 0, 'g', 5)
        .arg(plane.value("normalZ"), 0, 'g', 5).arg(plane.value("planeOffset"), 0, 'g', 6)
        .arg(plane.value("planeRms"), 0, 'g', 5));
  }
  summary_->setToolTip(result.error);
  if (decodeStatistics) {
    const auto qualityHelp = QStringLiteral("执行完成仅表示算法已结束。有效比例以整幅相机图像像素数为分母；解码拒绝原因可重叠，不可直接相加；未配置质量判定时不自动判为 OK/NG。");
    summary_->setToolTip(qualityHelp);
    for (int row = 0; row < results_->rowCount(); ++row)
      for (int col = 0; col < results_->columnCount(); ++col)
        if (auto* item = results_->item(row, col)) item->setToolTip(qualityHelp);
  }
  objectInfo_->setText(QStringLiteral("选中对象后查看坐标详情"));
  taskStatus_->setText(!result.error.isEmpty() ? QStringLiteral("执行失败")
                       : present ? QStringLiteral("执行完成")
                                 : QStringLiteral("就绪"));
  if (decodeStatistics)
    objectInfo_->setText(QStringLiteral("有效比例以整幅图像为分母；拒绝原因可重叠。执行完成不代表质量合格。"));
  variables_->setRowCount(document_.hasImage() ? 1 : 0);
  if (document_.hasImage()) {
    cell(variables_, 0, 0, QStringLiteral("原图"));
    cell(variables_, 0, 1, "Image");
    cell(variables_, 0, 2, "1");
  }
  if (present) {
    int row = variables_->rowCount();
    variables_->insertRow(row);
    cell(variables_, row, 0, QStringLiteral("结果"));
    cell(variables_, row, 1,
         !imageOnly
             ? (result.objects.isEmpty() ? "Object"
                                         : result.objects.first().type)
             : "Image");
    cell(variables_, row, 2,
         !imageOnly ? QString::number(result.objects.size())
                                 : "1");
  }
  int ng = 0, ok = 0;
  for (int i = 0; i < result.objects.size(); ++i) {
    const auto& object = result.objects[i];
    if (object.verdict == 0)
      ++ng;
    else if (object.verdict == 1)
      ++ok;
    for (auto it = object.values.begin(); it != object.values.end(); ++it) {
      const int row = variables_->rowCount();
      variables_->insertRow(row);
      cell(variables_, row, 0, QString("对象 %1.%2").arg(i + 1).arg(it.key()));
      cell(variables_, row, 1, "Number");
      cell(variables_, row, 2, QString::number(it.value(), 'g', 12));
    }
  }
  if (ng || ok)
    summary_->setText(summary_->text() +
                      QStringLiteral(" · 检测 %1 · OK %2 / NG %3")
                          .arg(ng ? "NG" : "OK")
                          .arg(ok)
                          .arg(ng));
  if (activeImageView()->qualityVisible() && !viewPinnedElsewhere())
    for (int i = 0; i < result.objects.size(); ++i)
      if (!result.objects[i].samples.isEmpty()) {
        for (int row = 0; row < results_->rowCount(); ++row)
          if (results_->item(row, 0) &&
              results_->item(row, 0)->data(Qt::UserRole).toInt() == i) {
            results_->setCurrentCell(row, 0);
            results_->selectRow(row);
            break;
          }
        activeImageView()->selectResult(i);
        const auto& values = result.objects[i].values;
        objectInfo_->setText(
            QStringLiteral("采样 %1 · RMS %2 px · 最大 %3 px · 容差外 %4")
                .arg(values.value("sampleCount"), 0, 'f', 0)
                .arg(values.value("rms"), 0, 'f', 3)
                .arg(values.value("maxResidual"), 0, 'f', 3)
                .arg(values.value("outsideTolerance"), 0, 'f', 0));
        break;
      }
  if (perTarget) {
    int succeeded = 0;
    for (const auto& target : result.instances)
      succeeded += target.state == NodeState::Success;
    summary_->setText(summary_->text() + QStringLiteral(" · 目标执行 %1 / %2")
                                             .arg(succeeded)
                                             .arg(result.instances.size()));
  }
  refreshExecutionTiming();
}
void MainWindow::refreshExecutionTiming(bool clear) {
  timingText_.clear();
  workbenchTiming_->clear();
  workbenchTiming_->hide();
  if (!clear && !execution_.busy() && !autoPreview_->isActive() &&
      activeImage_ >= 0 && activeImage_ < images_.size()) {
    const auto& image = images_[activeImage_];
    if (workflowMode_) {
      const auto& result = image.workflowResult;
      if (image.workflowResultVersion == image.version &&
          !result.pipeline.isEmpty())
        timingText_ =
            QStringLiteral("总耗时 %1 ms").arg(result.totalMs, 0, 'f', 2);
    } else {
      bool valid = image.resultVersion == image.baseVersion;
      if (nodeContext()) {
        const auto& outputs = image.workflowResult.nodeOutputs;
        valid = image.workflowResultVersion == image.version &&
                selectedStep_ >= 0 && selectedStep_ < outputs.size() &&
                (outputs[selectedStep_].state == NodeState::Success ||
                 outputs[selectedStep_].state == NodeState::Error);
      }
      if (valid && !inspectedResult_.pipeline.isEmpty()) {
        workbenchTiming_->setText(
            QStringLiteral("算子耗时 %1 ms")
                .arg(inspectedResult_.algorithmMs, 0, 'f', 2));
        workbenchTiming_->show();
      }
    }
  }
  const auto text =
      operatorStatusText_ + (workflowMode_ && !timingText_.isEmpty()
                                 ? QStringLiteral(" · ") + timingText_
                                 : QString());
  operatorStatus_->setText(text);
  operatorStatus_->setToolTip(
      workflowMode_ && !timingText_.isEmpty()
          ? text + QStringLiteral(
                       "\n总耗时包含当前图像本次处理的数据准备与算子执行")
          : text);
}
void MainWindow::setResultState(const QString& text, const char* state) {
  resultState_->setText(text);
  resultState_->setProperty("state", state);
  resultState_->style()->unpolish(resultState_);
  resultState_->style()->polish(resultState_);
  resultState_->update();
}
void MainWindow::rememberTool(Algorithm tool, bool multiView) {
  if (tool == Algorithm::StereoImage || tool == Algorithm::StructuredImage) return;
  const auto preset = qMakePair(tool, tool == Algorithm::ImageInput && multiView);
  recentTools_.removeAll(preset);
  recentTools_.prepend(preset);
  while (recentTools_.size() > 4) recentTools_.removeLast();
  qDeleteAll(recentGroup_->takeChildren());
  for (const auto& recent : recentTools_) {
    const auto& op = operatorDefinition(recent.first);
    auto* item = new QTreeWidgetItem(recentGroup_, {recent.second ? QStringLiteral("多视输入") : op.name});
    item->setData(0, Qt::UserRole, int(recent.first));
    item->setData(0, inputModeRole, recent.second);
    item->setToolTip(0, recent.second ? QStringLiteral("创建一组具名相机视图的输入") : op.description);
    item->setIcon(0, studioIcon(operatorIconName(recent.second ? Algorithm::StereoImage : op.id)));
  }
  recentGroup_->setHidden(false);
  recentGroup_->setExpanded(true);
  refreshContext();
}
void MainWindow::refreshNodeOutput() {
  if (!nodeContext()) return;
  auto* target = activeImageView();
  const int displayStep = viewNodeIndex();
  refreshViewIdentity();
  operatorControls_->setVisible(selectedStep_ >= 0 ||
                                workflowMode_ && selectedStep_ == -3);
  const auto& flow =
      activeImage_ >= 0 ? images_[activeImage_].workflowResult : result_;
  const bool stale =
      activeImage_ >= 0 && images_[activeImage_].workflowResultVersion !=
                               images_[activeImage_].version;
  workflowView_->setOutputs(stale ? QVector<NodeOutput>{} : flow.nodeOutputs);
  refreshDiagnostics();
  if (trainingView() && !viewPinnedElsewhere()) {
    syncNodeSettings();
    updateRangeView();
    return;
  }
  refreshInputChoices();
  nodeInputImage_ = {};
  nodeInputDomain_ = {};
  nodeProcessedImage_ = {};
  const QString resource =
      activeImage_ >= 0 ? images_[activeImage_].id : QString();
  const QString nodeIdentity = displayStep >= 0
                                   ? workflowIds_.value(displayStep)
                                   : QString::number(displayStep);
  const bool viewedNodeChanged =
      target->property("outputNodeId").toString() != nodeIdentity;
  const bool preserve =
      target->hasImage() &&
      target->property("resourceId").toString() == resource &&
      !viewedNodeChanged;
  const bool hadRaster = target->hasResultImage();
  QImage base = document_.preview, raster;
  QPainterPath domain;
  QVector<ResultObject> objects;
  if (document_.hasImage()) {
    nodeInputImage_ = document_.image;
    nodeInputDomain_ = imageDomainPath(document_.image);
  }
  target->setProperty("resourceId", resource);
  target->setProperty("outputNode", displayStep);
  target->setProperty("outputNodeId", nodeIdentity);
  nodeOutputType_->clear();
  nodeOutputType_->setStyleSheet({});
  Result inspected;
  if(displayStep>=0 && displayStep<workflow_.size() && sourceTool(workflow_[displayStep].algorithm)) {
    const auto cell=resolveSourceData(document_,workflow_[displayStep].options.value("binding").toString());
    const int frame=inputPreviewFrame(cell,workflow_[displayStep].options["binding"].toString());
    base=cell.frames.isEmpty()?QImage():makePreview(cell.frames[frame]).image;
    nodeInputImage_=cell.frames.isEmpty()?mv::MvImage():cell.frames[frame];nodeInputDomain_=imageDomainPath(nodeInputImage_);
    inspected.outputImage=nodeInputImage_;inspected.preview=base;inspected.pipeline=nodeNames().value(displayStep);
    inspected.spatial=sourceViewData(cell,resource+"/"+workflowIds_.value(displayStep));
    if (xyzInput(workflow_[displayStep]) && !stale && displayStep < flow.nodeOutputs.size() &&
        flow.nodeOutputs[displayStep].state == NodeState::Success) {
      inspected.spatial = flow.nodeOutputs[displayStep].spatial;
      inspected.outputImage = flow.nodeOutputs[displayStep].outputImage;
      inspected.preview = flow.nodeOutputs[displayStep].preview;
    }
    nodeOutputType_->setText(outputSummary(workflow_[displayStep]));
    if(cell.frames.isEmpty())target->setEmptyText(QStringLiteral("此输入暂无数据"));
  } else if (displayStep == -1 && document_.hasImage()) {
    base = document_.preview;
    nodeInputImage_ = document_.image;
    nodeInputDomain_ = imageDomainPath(nodeInputImage_);
    inspected.outputImage = document_.image;
    inspected.preview = document_.preview;
    inspected.pipeline = QStringLiteral("图像输入");
    nodeOutputType_->setText("Image");
  } else {
    const int index = displayStep == -2 ? workflowOutput_ : displayStep;
    const bool valid = !stale && index >= 0 && index < workflow_.size() &&
                       index < flow.nodeOutputs.size() &&
                       flow.nodeOutputs[index].parameters.algorithm ==
                           workflow_[index].algorithm;
    if (valid && document_.hasImage()) {
      const auto& output = flow.nodeOutputs[index];
      inspected.error = output.error;
      if (output.state == NodeState::Success || !output.objects.isEmpty() ||
          !output.instances.isEmpty()) {
        nodeInputImage_ = output.inputImage;
        nodeInputDomain_ = imageDomainPath(nodeInputImage_);
        nodeProcessedImage_ = output.outputImage;
        base = displayComposite(document_.preview, output.inputPreview,
                                imageDomainPath(output.inputImage));
        raster = output.preview;
        domain = imageDomainPath(output.outputImage);
        objects = output.objects;
        inspected.spatial = output.spatial;
        inspected.calibrationResult = output.calibrationResult;
        inspected.outputImage = output.outputImage;
        inspected.preview = output.preview;
        inspected.objects = output.objects;
        inspected.instances = output.instances;
        inspected.measureName = output.measureName;
        inspected.pipeline = nodeNames()[index];
        inspected.algorithmMs = output.algorithmMs;
        inspected.totalMs = output.totalMs;
        inspected.steps = flow.steps;
        nodeOutputType_->setText(outputSummary(output.parameters));
      } else if (output.state == NodeState::Pending) {
        inspected.pipeline = nodeNames()[index];
        nodeOutputType_->setText(QStringLiteral("未执行"));
      } else {
        inspected.pipeline = nodeNames()[index];
        nodeOutputType_->setStyleSheet("color:#c46c64;");
        nodeOutputType_->setText(output.state == NodeState::Blocked
                                     ? QStringLiteral("上游失败")
                                     : QStringLiteral("执行失败"));
      }
    } else
      target->setEmptyText(
          displayStep == -3
              ? QStringLiteral("选择流程节点查看输出")
              : QStringLiteral("节点尚未运行\n运行后显示真实输出"));
  }
  if(displayStep>=0&&displayStep<workflow_.size()&&workflow_[displayStep].algorithm==Algorithm::StereoCalibration&&!inspected.spatial) {
    const int source=rangeForNode(displayStep).dataA;
    if(source>=0&&source<workflow_.size()&&sourceTool(workflow_[source].algorithm))
      inspected.spatial=sourceViewData(resolveSourceData(document_,workflow_[source].options["binding"].toString()),resource+"/"+workflowIds_.value(displayStep));
  }
  if (!workflowMode_ && displayStep < 0) {
    base = {};
    nodeInputImage_ = {};
    nodeInputDomain_ = {};
  }
  if (workflowMode_ && target->compareEnabled() && !compareNodeInput_)
    base = document_.preview;
  target->setComparisonLabel(workflowMode_ && compareNodeInput_
                                ? QStringLiteral("节点输入") : QStringLiteral("原始图像"));
  target->setImage(base, preserve);
  target->setResultImage(raster, domain);
  target->setResults(objects);
  const auto range = viewPinnedElsewhere() ? rangeForNode(displayStep) : effectiveRange();
  target->setRoi(viewPinnedElsewhere()
                     ? range.scope == ScopeMode::Manual ? range.roi : Roi{}
                     : currentNodeRoi());
  target->setRangeOverlay((range.scope == ScopeMode::Region ||
                           range.poseSource != -1 ||
                           !range.exclusions.isEmpty()) &&
                                  nodeInputImage_.CountObj() && !stale
                              ? imageDomainPath(nodeInputImage_)
                              : QPainterPath{});
  if (!preserve || !hadRaster) {
    target->setLayerVisible(ImageView::Layer::Original, true);
    target->setLayerVisible(ImageView::Layer::Processed, !raster.isNull());
  }
  QString displayedPath=document_.imagePath;
  if (displayStep>=0 && displayStep<workflow_.size() && sourceTool(workflow_[displayStep].algorithm)) {
    const auto cell=resolveSourceData(document_,workflow_[displayStep].options.value("binding").toString());
    if (!cell.paths.isEmpty()) displayedPath=cell.paths[inputPreviewFrame(cell,workflow_[displayStep].options["binding"].toString())];
  }
  target->setImageName(target->hasImage() ? QFileInfo(displayedPath).fileName() : QString());
  inspectResult(viewPinnedElsewhere()
                    ? resultForNode(selectedStep_ == -2 ? workflowOutput_ : selectedStep_)
                    : inspected);
  syncNodeSettings();
  if (stale && displayStep != -1)
    setResultState(execution_.busy() ? QStringLiteral("正在计算")
                                     : QStringLiteral("等待运行"),
                   "running");
  refreshProperties();
  updateActions();
  syncViewTools();
  refreshViewIdentity();
  refreshRangeContext();
  if (workflowMode_) {
    const bool hadPoints = spatialView_ && !spatialView_->points.isEmpty();
    const bool hasPoints = inspected.spatial && !inspected.spatial->points.isEmpty();
    // Follow node navigation and newly available results. Ordinary refreshes
    // of the same result leave the user's 2D/3D buttons as set.
    if (viewedNodeChanged || hadPoints != hasPoints) {
      view3d_->setChecked(hasPoints);
      if (!hasPoints) view2d_->setChecked(true);
    }
  }
  refreshSpatialView(inspected.spatial);
}
void MainWindow::syncViewTools() {
  if (!view_) return;
  for (auto* target : {view_, nodeView_}) {
    if (!target) continue;
    const bool loaded = target->hasImage();
    for (const auto* name : {"viewTools", "layerTools"})
      if (auto* rail =
              target->findChild<QWidget*>(name, Qt::FindDirectChildrenOnly))
        rail->setVisible(loaded);
    const bool dark =
        qApp->palette().color(QPalette::Window).lightness() < 128;
    const QStringList icons{"image", "result", "rect", target->elementIcon()};
    const QStringList colors{
        dark ? "#cbd7e6" : "#748197", dark ? "#b3a2ff" : "#8062c7",
        dark ? "#ffc465" : "#b57a22", target->elementColor().name()};
    for (int i = 0; i < 4; ++i)
      if (auto* b = target->findChild<QToolButton*>("layerButton" +
                                                    QString::number(i))) {
        const QSignalBlocker block(b);
        b->setChecked(target->layerVisible(static_cast<ImageView::Layer>(i)));
        b->setEnabled(loaded && (i != 1 || target->hasResultImage()) &&
                      (i != 3 || target->hasElements()));
        b->setIcon(studioIcon(icons[i], QColor(colors[i])));
        b->setStyleSheet(
            QStringLiteral("QToolButton:checked{border-left:2px solid %1;}")
                .arg(colors[i]));
        if (i == 3) b->setToolTip(target->elementDescription());
      }
    if (auto* b = target->findChild<QToolButton*>("compareButton")) {
      const QSignalBlocker block(b);
      b->setEnabled(loaded && target->hasResultImage());
      b->setChecked(target->compareEnabled());
    }
    if (auto* b = target->findChild<QToolButton*>("actualSizeButton"))
      b->setEnabled(loaded);
    for (auto* button : target->findChildren<QToolButton*>())
      if (button->property("imageTool").isValid()) {
        const QSignalBlocker guard(button);
        const int tool = button->property("imageTool").toInt();
        button->setChecked(int(target->tool()) == tool ||
                           target->tool() ==
                                   ImageView::Tool::RotatedRectangle &&
                               tool == int(ImageView::Tool::Rectangle) ||
                           target->tool() == ImageView::Tool::Annulus &&
                               tool == int(ImageView::Tool::Circle));
        button->setEnabled(loaded && (tool == int(ImageView::Tool::Pan) || tool == int(ImageView::Tool::Select) ||
                                      target->roiEditingEnabled()));
      }
  }
  const auto* active = workflowMode_ ? nodeView_ : view_;
  const bool loaded = active->hasImage();
  if(zoomStatus_) {
    zoomStatus_->setText(QStringLiteral("图像 %1%").arg(active->zoom()*100,0,'f',0));
    zoomStatus_->setVisible(loaded && view2d_ && view2d_->isChecked());
  }
  for (const auto& pair : QVector<QPair<QAction*, ImageView::Tool>>{
           {selectAction_, ImageView::Tool::Select},
           {panAction_, ImageView::Tool::Pan},
           {rectangleAction_, ImageView::Tool::Rectangle},
           {circleAction_, ImageView::Tool::Circle}})
    if (pair.first) {
      const QSignalBlocker block(pair.first);
      pair.first->setEnabled(loaded && (pair.second == ImageView::Tool::Pan || pair.second == ImageView::Tool::Select ||
                                        active->roiEditingEnabled()));
      pair.first->setChecked(active->tool()==pair.second ||
          active->tool()==ImageView::Tool::RotatedRectangle && pair.second==ImageView::Tool::Rectangle ||
          active->tool()==ImageView::Tool::Annulus && pair.second==ImageView::Tool::Circle);
    }
  if (overlayAction_) {
    const QSignalBlocker block(overlayAction_);
    overlayAction_->setChecked(
        active->layerVisible(ImageView::Layer::Elements));
  }
  if (clearRoiAction_)
    clearRoiAction_->setEnabled(loaded && active->roiEditingEnabled());
}
void MainWindow::setWorkflowMode(bool enabled) {
  const bool modeChanged = enabled != workflowMode_;
  if (modeChanged && modeLayoutReady_) rememberModeLayout();
  if (enabled && !workflowMode_ && persistSettings_) {
    QSettings settings;
    settings.setValue("window/workbenchLibrarySplit",
                      librarySplit_->saveState());
  }
  if (enabled != workflowMode_) {
    stopExecution();
    storeActive();
    if (!nodeContext()) workbenchParameters_ = document_.parameters;
  }
  workflowMode_ = enabled;
  if (!enabled && !debugNodeId_.isEmpty())
    selectedStep_ = workflowIds_.indexOf(debugNodeId_);
  if (!enabled && selectedStep_ < 0 && !debugNodeId_.isEmpty())
    selectedStep_ = -3;
  if (!nodeContext())
    document_.parameters = workbenchParameters_;
  else if (selectedStep_ >= 0 && selectedStep_ < workflow_.size())
    document_.parameters = workflow_[selectedStep_];
  syncParameters();
  nodeOutputDock_->setVisible(enabled);
  canvasHeader_->setVisible(enabled);
  operatorControls_->show();
  centerTab_->setTabText(
      0, enabled ? QStringLiteral("工作流") : QStringLiteral("2D视图"));
  centerTab_->setTabIcon(0, studioIcon(enabled ? "flow" : "view-2d"));
  nodeInput_->setVisible(true);
  library_->setDragEnabled(enabled);
  workbenchMode_->setChecked(!enabled);
  workflowModeButton_->setChecked(enabled);
  if (auto* a = findChild<QAction*>("workbenchModeAction"))
    a->setChecked(!enabled);
  if (auto* a = findChild<QAction*>("workflowModeAction"))
    a->setChecked(enabled);
  centerPages_->setCurrentIndex(enabled ? 1 : 0);
  updateViews();
  if (enabled && selectedStep_ >= 0 && selectedStep_ < workflow_.size()) {
    document_.parameters = workflow_[selectedStep_];
    syncParameters();
  }
  if (!enabled) {
    view_->setImageName(document_.hasImage()
                            ? QFileInfo(document_.imagePath).fileName()
                            : QString());
    inspectResult(result_);
  }
  if (!nodeContext()) {
    result_ = activeImage_ >= 0 && images_[activeImage_].resultVersion ==
                                       images_[activeImage_].baseVersion
                  ? images_[activeImage_].result
                  : Result{};
    view_->setImage(document_.preview);
    view_->setRoi(currentNodeRoi());
    showResult(result_);
  }
  refreshWorkflow();
  refreshContext();
  if (modeChanged || !modeLayoutReady_) restoreModeLayout();
  modeLayoutReady_ = true;
  refreshFilm();
  updateActions();
}
void MainWindow::refreshWorkflow() {
  if (nodeSearch_) {
    const QSignalBlocker block(nodeSearch_);
    nodeSearch_->clear();
    const auto names = nodeNames();
    for (int i = 0; i < names.size(); ++i) nodeSearch_->addItem(names[i], workflowIds_[i]);
    nodeSearch_->setCurrentIndex(-1);
  }
  {
    QSignalBlocker block(workflowOutputChoice_);
    workflowOutputChoice_->clear();
    workflowOutputChoice_->addItem(QStringLiteral("输出 · 未指定"), -2);
    const auto names = nodeNames();
    for (int i = 0; i < names.size(); ++i)
      workflowOutputChoice_->addItem(QStringLiteral("输出 · ") + names[i], i);
    workflowOutputChoice_->setCurrentIndex(
        workflowOutputChoice_->findData(workflowOutput_));
    workflowOutputChoice_->setToolTip(workflowOutputChoice_->currentText() + QStringLiteral("\n选择流程运行及导出的结果节点"));
    workflowOutputChoice_->updateGeometry();
  }
  QVector<int> regions, poses, dataA, dataB;
  QVector<NodeSettings> bindings;
  for (int i = 0; i < workflow_.size(); ++i) {
    const auto r = rangeForNode(i);
    regions.append(r.scope == ScopeMode::Region ? r.regionSource : -1);
    poses.append(r.poseSource);
    dataA.append(r.dataA);
    dataB.append(r.dataB);
    bindings.append(r);
  }
  QVector<Parameters> portWorkflow;
  for (int i=0; i<workflow_.size(); ++i) portWorkflow.append(portParameters(i));
  workflowView_->setSteps(portWorkflow, selectedStep_, workflowInputs_,
                          workflowPositions_, workflowLinks_, regions,
                          workflowOutput_, poses, nodeNames(), dataA, dataB, bindings);
  syncInputTrack();
  refreshInputChoices();
  refreshContext();
  syncGeometryUi();
  if (nodeContext()) {
    imageTitle_->setText(
        QStringLiteral("工作流 · %1 个节点 · 拖入算子，拖动端口连线")
            .arg(workflow_.size()));
    refreshNodeOutput();
  }
}
void MainWindow::addWorkflowNode(Algorithm tool, QPointF position,
                                  int insertTarget, int insertSource, bool multiView) {
  const bool insert = insertTarget >= 0 && insertTarget < workflow_.size() &&
      workflowLinks_[insertTarget].contains(insertSource) &&
      workflowInputs_[insertTarget] == insertSource &&
      operatorDefinition(tool).output == "Image" && !typedInputTool(tool) &&
      (insertSource == -1 || insertSource >= 0 && insertSource < workflow_.size() &&
       outputType(workflow_[insertSource]) == "Image");
  EditGuard edit(*this, insert ? QStringLiteral("插入节点") : QStringLiteral("添加节点"));
  if (runQueue_.active() || workflow_.size() >= 32) return;
  if (workflowPositions_.size() != workflow_.size() + 1) {
    workflowPositions_.clear();
    for (int i = 0; i <= workflow_.size(); ++i)
      workflowPositions_.append(QPointF(45 + i % 3 * 240, 65 + i / 3 * 140));
  }
  Parameters p;
  p.algorithm = tool;
  if (tool == Algorithm::ImageInput && multiView) p.options["inputMode"] = 1;
  if (sourceTool(tool)) {
    QString key = tool == Algorithm::SequenceInput ? "sequence" : "image";
    bool exists = false;
    for (const auto& node : workflow_) exists |= node.options.value("binding").toString() == key;
    for(const auto& image:images_)exists|=image.document.sourceInputs.contains(key);
    if (exists) {
      key = tool == Algorithm::ImageInput ? "right" : "sequence-1";
      for (const auto& node : workflow_)
        if (node.options.value("binding").toString() == key) { key = "input-" + imageId(); break; }
      for(const auto& image:images_)if(image.document.sourceInputs.contains(key))key="input-"+imageId();
    }
    p.options["binding"] = key;
  }
  const int source =
      insert ? insertSource : workflowView_->nearestSource(position, tool);
  if (insert) {
    position = QPointF(std::max(position.x() - WorkflowView::nodeSize().width() / 2,
                                workflowPositions_[source + 1].x() +
                                    WorkflowView::nodeSize().width() + 40),
                        position.y() - WorkflowView::nodeSize().height() / 2);
    const auto shift = std::max(0., position.x() + WorkflowView::nodeSize().width() + 40 -
                                      workflowPositions_[insertTarget + 1].x());
    for (int i = 0; i < workflow_.size(); ++i)
      if (i == insertTarget || workflowAncestors(workflowLinks_, i).contains(insertTarget))
        workflowPositions_[i + 1].rx() += shift;
  }
  const bool region =
      source >= 0 &&
      outputType(workflow_[source]) == "Region";
  const int imageSource = typedInputTool(tool) ? -1
                          : region       ? workflowInputs_.value(source, -1)
                                         : source;
  if(tool==Algorithm::StereoRectify && source>=0)p.options["multiView"]=multiViewInput(workflow_[source])?1:0;
  workflow_.append(p);
  workflowIds_.append(imageId());
  workflowAliases_.append(QString());
  workflowAliases_ = nodeNames();
  sharedRanges_.append({});
  if (typedInputTool(tool) && source >= 0 &&
      acceptsData(tool, outputType(workflow_[source])))
    sharedRanges_.last().dataA = source;
  if (!typedInputTool(tool) && region) {
    sharedRanges_.last().scope = ScopeMode::Region;
    sharedRanges_.last().regionSource = source;
  }
  storeActive();
  for (auto& image : images_) {
    image.document.nodeSettings.resize(workflow_.size());
    image.document.rangeOverrides.resize(workflow_.size());
  }
  document_.nodeSettings.resize(workflow_.size());
  document_.rangeOverrides.resize(workflow_.size());
  workflowInputs_.append(imageSource);
  workflowLinks_.append(sourceTool(tool) || source < -1 || typedInputTool(tool) && sharedRanges_.last().dataA < 0 && !(captureTool(tool) && source == -1)
                            ? QVector<int>{}
                            : QVector<int>{source});
  workflowPositions_.append(position);
  if (insert) {
    workflowLinks_[insertTarget].removeAll(insertSource);
    workflowLinks_[insertTarget].append(workflow_.size() - 1);
    workflowInputs_[insertTarget] = workflow_.size() - 1;
  }
  selectedStep_ = workflow_.size() - 1;
  rememberTool(tool, multiViewInput(p));
  workflowChanged();
}
void MainWindow::removeWorkflowNode(int index) {
  EditGuard edit(*this, QStringLiteral("删除节点"));
  if (runQueue_.active() || index < 0 || index >= workflow_.size()) return;
  workflow_.remove(index);
  workflowIds_.removeAt(index);
  workflowAliases_.removeAt(index);
  sharedRanges_.remove(index);
  storeActive();
  auto removeSettings = [index](Document& doc) {
    if (index < doc.nodeSettings.size()) doc.nodeSettings.remove(index);
    if (index < doc.rangeOverrides.size()) doc.rangeOverrides.remove(index);
    for (auto& s : doc.nodeSettings) {
      remapInputBindings(s, [index](int n) { return n == index ? -2 : n > index ? n-1 : n; });
      for (int *v : {&s.dataA, &s.dataB, &s.calibrationSource}) {
        if (*v == index)
          *v = -2;
        else if (*v > index)
          --*v;
      }
      if (s.poseSource == index)
        s.poseSource = -2;
      else if (s.poseSource > index)
        --s.poseSource;
      if (s.regionSource == index)
        s.regionSource = -2;
      else if (s.regionSource > index)
        --s.regionSource;
    }
  };
  for (auto& s : sharedRanges_) {
    remapInputBindings(s, [index](int n) { return n == index ? -2 : n > index ? n-1 : n; });
    for (int *v : {&s.dataA, &s.dataB, &s.calibrationSource}) {
      if (*v == index)
        *v = -2;
      else if (*v > index)
        --*v;
    }
    if (s.poseSource == index)
      s.poseSource = -2;
    else if (s.poseSource > index)
      --s.poseSource;
    if (s.regionSource == index)
      s.regionSource = -2;
    else if (s.regionSource > index)
      --s.regionSource;
  }
  for (auto& image : images_) removeSettings(image.document);
  removeSettings(document_);
  for (auto& image : images_) {
    image.workflowResult = {};
    image.result.nodeOutputs.clear();
  }
  result_.nodeOutputs.clear();
  if (workflowOutput_ == index)
    workflowOutput_ = -2;
  else if (workflowOutput_ > index)
    --workflowOutput_;
  workflowInputs_.remove(index);
  workflowLinks_.remove(index);
  for (auto& sources : workflowLinks_) {
    sources.removeAll(index);
    for (int& source : sources)
      if (source > index) --source;
  }
  if (workflowPositions_.size() > index + 1)
    workflowPositions_.remove(index + 1);
  for (int& source : workflowInputs_) {
    if (source == index)
      source = -2;
    else if (source > index)
      --source;
  }
  selectedStep_ = !workflowMode_ && !debugNodeId_.isEmpty()
                      ? workflowIds_.indexOf(debugNodeId_)
                      : std::min(index, workflow_.size() - 1);
  if (!workflowMode_ && !debugNodeId_.isEmpty() && selectedStep_ < 0)
    selectedStep_ = -3;
  workflowChanged();
}
void MainWindow::connectWorkflowNode(int target, int source) {
  EditGuard edit(*this, QStringLiteral("连接节点"));
  if (runQueue_.active() || target < 0 || target >= workflow_.size()) return;
  if (!workflowView_->canConnect(source, target)) {
    setOperatorStatus(QStringLiteral("连线无效"), "error",
                      QStringLiteral("连线不能形成循环或连接节点自身"));
    return;
  }
  if (!(source>=0 && calibrationTool(workflow_[source].algorithm)) && typedInputTool(workflow_[target].algorithm) && !(captureTool(workflow_[target].algorithm) && source == -1)) {
    bindGeometryInput(target, source, false);
    return;
  }
  if (workflowLinks_[target].contains(source)) return;
  if(!confirmInputMerge(source,target))return;
  workflowLinks_[target].append(source);
  const auto type =
      source < 0 ? QString("Image")
                 : outputType(workflow_[source]);
  if (type == "Image") workflowInputs_[target] = source;
  if (type == "Region") {
    sharedRanges_[target].scope = ScopeMode::Region;
    sharedRanges_[target].regionSource = source;
    if (document_.rangeOverrides.value(target)) {
      document_.nodeSettings[target].scope = ScopeMode::Region;
      document_.nodeSettings[target].regionSource = source;
    }
  }
  if (type == "Match") {
    sharedRanges_[target].poseSource = source;
    sharedRanges_[target].poseReferenceSet = false;
  }
  selectedStep_ = target;
  workflowChanged();
}
void MainWindow::disconnectWorkflowNode(int target, int source) {
  EditGuard edit(*this, QStringLiteral("删除连线"));
  if (runQueue_.active() || target < 0 || target >= workflow_.size()) return;
  workflowLinks_[target].removeAll(source);
  auto clearData = [&](NodeSettings& v) {
    if (v.dataA == source) v.dataA = -2;
    if (v.dataB == source) v.dataB = -2;
  };
  clearData(sharedRanges_[target]);
  for (auto& image : images_)
    if (target < image.document.nodeSettings.size())
      clearData(image.document.nodeSettings[target]);
  if (target < document_.nodeSettings.size())
    clearData(document_.nodeSettings[target]);
  selectedStep_ = target;
  workflowChanged();
}
void MainWindow::rewireWorkflowConnection(int oldTarget, int oldSource,
                                          int target, int source,
                                          const QString& role) {
  EditGuard edit(*this, QStringLiteral("改接连线"));
  if (runQueue_.active() || oldTarget < 0 || oldTarget >= workflow_.size() ||
      target < 0 || target >= workflow_.size() || source < -1 ||
      source >= workflow_.size() ||
      !workflowLinks_[oldTarget].contains(oldSource))
    return;
  if (oldTarget == target && oldSource == source) return;
  if (source >= 0 && role != "Dependency" && rangeForNode(oldTarget).inputs.contains(role)) {
    const auto choices = compatibleOutputs(portParameters(source), workflow_[target], role);
    const auto prior = inputBinding(rangeForNode(oldTarget), role);
    QString key;
    for (const auto& port : choices) if (port.key == prior.output) key = port.key;
    if (key.isEmpty() && choices.size() == 1) key = choices.first().key;
    if (key.isEmpty() && !choices.isEmpty()) {
      QMenu menu(this);
      for (const auto& port : choices) menu.addAction(port.label)->setData(port.key);
      if (auto* chosen = menu.exec(QCursor::pos())) key = chosen->data().toString();
    }
    if (key.isEmpty()) return;
    auto links = workflowLinks_;
    bool otherBinding = false;
    const auto old = rangeForNode(oldTarget);
    for (const auto& port : inputPorts(workflow_[oldTarget]))
      if (port.key != role && inputBinding(old, port.key).node == oldSource) otherBinding = true;
    if (!otherBinding) links[oldTarget].removeAll(oldSource);
    if (!workflowAncestors(links, target).contains(source)) links[target].append(source);
    Document graph; graph.workflow = workflow_; graph.workflowInputs = workflowInputs_; graph.workflowLinks = links;
    try { workflowOrder(graph, true); } catch (...) { return; }
    if (!confirmInputMerge(source, target)) return;
    auto apply = [&](QVector<NodeSettings>& settings) {
      if (settings.size() != workflow_.size()) return;
      setInputBinding(settings[oldTarget], role, {-2, prior.output});
      setInputBinding(settings[target], role, {source, key});
      if (role == "Region") settings[target].scope = ScopeMode::Region;
      if (role == "Match") settings[target].poseReferenceSet = false;
    };
    apply(sharedRanges_); apply(document_.nodeSettings);
    for (auto& item : images_) apply(item.document.nodeSettings);
    if (role == "Image") { workflowInputs_[oldTarget] = -2; workflowInputs_[target] = source; }
    workflowLinks_ = links; selectedStep_ = target; workflowChanged(); return;
  }
  if(!confirmInputMerge(source,target))return;
  const auto type =
      source < 0 ? QString("Image")
                 : outputType(workflow_[source]);
  if (role == "ObjectA" || role == "ObjectB") {
    if (!typedInputTool(workflow_[target].algorithm) ||
        !acceptsData(workflow_[target].algorithm, type, role == "ObjectB"))
      return;
    auto links = workflowLinks_;
    const auto oldRange = rangeForNode(oldTarget);
    if ((role == "ObjectA" ? oldRange.dataB : oldRange.dataA) != oldSource)
      links[oldTarget].removeAll(oldSource);
    if (!links[target].contains(source)) links[target].append(source);
    Document test;
    test.workflow = workflow_;
    test.workflowInputs = workflowInputs_;
    test.workflowLinks = links;
    try {
      workflowOrder(test, true);
    } catch (...) {
      return;
    }
    auto clear = [&](NodeSettings& x) {
      auto& v = role == "ObjectA" ? x.dataA : x.dataB;
      if (v == oldSource) v = -2;
    };
    clear(sharedRanges_[oldTarget]);
    for (auto& image : images_)
      if (oldTarget < image.document.nodeSettings.size())
        clear(image.document.nodeSettings[oldTarget]);
    if (oldTarget < document_.nodeSettings.size())
      clear(document_.nodeSettings[oldTarget]);
    workflowLinks_ = links;
    bindGeometryInput(target, source, role == "ObjectB");
    return;
  }
  if(role=="Calibration" && (source<0 || !calibrationLink(workflow_[source].algorithm,workflow_[target].algorithm)))return;
  if ((role != "Dependency" && role != type) ||
      (role != "Dependency" && type == "XLD")) {
    setOperatorStatus(QStringLiteral("改接失败"), "error",
                      QStringLiteral("端口类型不匹配，已保留原连接"));
    return;
  }
  Document candidate;
  candidate.workflow = workflow_;
  candidate.workflowInputs = workflowInputs_;
  candidate.workflowLinks = workflowLinks_;
  candidate.workflowLinks[oldTarget].removeAll(oldSource);
  if (!candidate.workflowLinks[target].contains(source))
    candidate.workflowLinks[target].append(source);
  if (role == "Image") {
    if (candidate.workflowInputs[oldTarget] == oldSource)
      candidate.workflowInputs[oldTarget] = -2;
    candidate.workflowInputs[target] = source;
  }
  try {
    workflowOrder(candidate, true);
  } catch (const std::exception& error) {
    setOperatorStatus(
        QStringLiteral("改接失败"), "error",
        QString::fromUtf8(error.what()) + QStringLiteral("；已保留原连接"));
    return;
  }
  auto matches = [&](const NodeSettings& range) {
    return role == "Region" ? range.scope == ScopeMode::Region &&
                                  range.regionSource == oldSource
           : role == "Match" ? range.poseSource == oldSource
                             : false;
  };
  auto clear = [&](NodeSettings& range) {
    if (!matches(range)) return false;
    if (role == "Region")
      range.regionSource = -2;
    else {
      range.poseSource = -2;
      range.poseReferenceSet = false;
    }
    return true;
  };
  auto bind = [&](NodeSettings& range) {
    if (role == "Region") {
      range.scope = ScopeMode::Region;
      range.regionSource = source;
    } else if (role == "Match") {
      range.poseSource = source;
      range.poseReferenceSet = false;
    }
  };
  storeActive();
  const bool common = clear(sharedRanges_[oldTarget]);
  if (common || oldTarget != target) bind(sharedRanges_[target]);
  auto update = [&](Document& document) {
    if (document.nodeSettings.size() != workflow_.size()) return;
    const bool bound = clear(document.nodeSettings[oldTarget]);
    if (oldTarget == target && bound)
      bind(document.nodeSettings[target]);
    else if (oldTarget != target && document.rangeOverrides.value(target) &&
             &document == &document_)
      bind(document.nodeSettings[target]);
  };
  for (auto& image : images_) update(image.document);
  update(document_);
  workflowLinks_ = candidate.workflowLinks;
  workflowInputs_ = candidate.workflowInputs;
  selectedStep_ = target;
  workflowChanged();
}
void MainWindow::selectWorkflowInput(int target, int source) {
  EditGuard edit(*this, QStringLiteral("选择图像来源"));
  if (runQueue_.active() || target < 0 || target >= workflow_.size()) return;
  if (!workflowInputSources(workflowLinks_, target).contains(source) ||
      (source >= 0 &&
       outputType(workflow_[source]) != "Image"))
    return;
  if (workflowInputs_[target] == source) return;
  workflowInputs_[target] = source;
  sharedRanges_[target].inputs.remove("Image");
  for (auto& item : images_) if (target < item.document.nodeSettings.size()) item.document.nodeSettings[target].inputs.remove("Image");
  if (target < document_.nodeSettings.size()) document_.nodeSettings[target].inputs.remove("Image");
  selectedStep_ = target;
  workflowChanged();
}
void MainWindow::workflowChanged() {
  if (!workflowMode_ && !debugNodeId_.isEmpty()) {
    selectedStep_ = workflowIds_.indexOf(debugNodeId_);
    if (selectedStep_ < 0) selectedStep_ = -3;
  }
  stopExecution();
  storeActive();
  for (int i = 0; i < workflow_.size(); ++i) {
    Document graph;
    graph.workflow = workflow_;
    graph.workflowLinks = workflowLinks_;
    graph.workflowInputs = workflowInputs_;
    const auto upstream = workflowImageSources(graph, i);
    if (!upstream.contains(workflowInputs_[i]))
      workflowInputs_[i] = upstream.size() == 1 ? upstream.first() : -2;
    auto checkData = [&](NodeSettings &setting) {
      if (!setting.inputs.isEmpty()) {
        for (const auto& slot : setting.inputs.keys())
          setInputBinding(setting, slot, setting.inputs.value(slot));
        return;
      }
      if (workflow_[i].algorithm == Algorithm::StereoRectify) {
        for (bool second : {false, true}) {
          auto& source = second ? setting.dataB : setting.dataA;
          const auto expected = !second && workflow_[i].options.value("multiView").toInt()
              ? QString("MultiView") : QString("Image");
          if (source >= 0 && source < workflow_.size() && outputType(workflow_[source]) != expected)
            source = -2;
        }
      }
      if (setting.calibrationSource >= 0 &&
          (!workflowLinks_[i].contains(setting.calibrationSource) ||
           !calibrationLink(workflow_[setting.calibrationSource].algorithm,
                            workflow_[i].algorithm)))
        setting.calibrationSource = -2;
      for (bool second : {false, true}) {
        auto& index = second ? setting.dataB : setting.dataA;
        if (index >= 0 &&
            (!workflowAncestors(workflowLinks_, i).contains(index) ||
             !acceptsData(workflow_[i].algorithm,
                          outputType(workflow_[index]),
                          second)))
          index = -2;
      }
    };
    checkData(sharedRanges_[i]);
    for (auto& image : images_)
      if (i < image.document.nodeSettings.size())
        checkData(image.document.nodeSettings[i]);
    if (i < document_.nodeSettings.size()) checkData(document_.nodeSettings[i]);
    const auto regions = workflowObjectSources(graph, i, "Region");
    auto checkRegion = [&](Document& doc) {
      if (i < doc.nodeSettings.size() &&
          doc.nodeSettings[i].scope == ScopeMode::Region &&
          !regions.contains(doc.nodeSettings[i].regionSource))
        doc.nodeSettings[i].regionSource = -2;
    };
    auto& shared = sharedRanges_[i];
    if (shared.scope == ScopeMode::Region &&
        !regions.contains(shared.regionSource))
      shared.regionSource = -2;
    const auto poses = workflowObjectSources(graph, i, "Match");
    if (shared.poseSource != -1 && !poses.contains(shared.poseSource))
      shared.poseSource = -2;
    checkRegion(document_);
    for (auto& image : images_) checkRegion(image.document);
  }
  for (auto& image : images_) {
    ++image.version;
  }
  if (nodeContext() && selectedStep_ >= 0 &&
      selectedStep_ < workflow_.size()) {
    document_.parameters = workflow_[selectedStep_];
    syncParameters();
  }
  refreshWorkflow();
  setDirty(true);
  refreshFilm();
  schedulePreview();
  updateActions();
}
void MainWindow::refreshInputChoices() {
  const QSignalBlocker block(nodeInput_);
  nodeInput_->clear();
  nodeInput_->setVisible(true);
  if (!nodeContext()) {
    nodeInput_->addItem(QStringLiteral("原始图像"), -1);
    nodeInput_->setEnabled(false);
    operatorTitle_->setToolTip(
        QStringLiteral("工作台 · 独立试验；不修改工作流中的同类型节点"));
    syncNodeSettings();
    return;
  }
  nodeInput_->setEnabled(!runQueue_.active());
  if (selectedStep_ < 0 || selectedStep_ >= workflow_.size()) return;
  const auto names = nodeNames();
  if (sourceTool(workflow_[selectedStep_].algorithm) || captureTool(workflow_[selectedStep_].algorithm)) {
    nodeInput_->hide(); operatorTitle_->setText(names[selectedStep_]); syncNodeSettings(); return;
  }
  const auto upstream = workflowInputSources(workflowLinks_, selectedStep_);
  if (upstream.isEmpty())
    nodeInput_->addItem(QStringLiteral("输入 · 请先连线上游"), -2);
  for (int source : upstream) {
    if (source >= 0 &&
        outputType(workflow_[source]) != "Image")
      continue;
    nodeInput_->addItem(
        QStringLiteral("输入 · ") +
            (source < 0 ? QStringLiteral("原始图像") : names[source] + QStringLiteral(" → 图像")),
        source);
  }
  int current = workflowInputs_.value(selectedStep_, -2);
  if (current == -2 && nodeInput_->count() == 1 &&
      nodeInput_->itemData(0).toInt() >= -1)
    current = nodeInput_->itemData(0).toInt();
  if (nodeInput_->findData(current) < 0) {
    nodeInput_->addItem(QStringLiteral("请选择图像来源"), -2);
    current = -2;
  }
  showSourceSelection(nodeInput_, current, -2);
  fillNamedInput(nodeInput_, selectedStep_, "Image");
  operatorTitle_->setText(names[selectedStep_]);
  operatorTitle_->setToolTip(
      QStringLiteral("工作流节点 · %1").arg(names[selectedStep_]));
  syncNodeSettings();
}
void MainWindow::refreshProperties() {
  if (!document_.hasImage()) {
    imageInfo_->setText(QStringLiteral("未加载图像"));
    parameterInput_->setText(QStringLiteral("输入：未加载图像"));
    roiInfo_->clear();
    return;
  }
  const auto* model = trainingView() ? currentModel() : nullptr;
  const bool reference = model && model->referenceImage.CountObj();
  const bool outputVisible =
      activeImageView()->layerVisible(ImageView::Layer::Processed);
  const auto& image =
      reference ? model->referenceImage
      : nodeContext() && outputVisible && nodeProcessedImage_.CountObj()
          ? nodeProcessedImage_
      : nodeContext() && nodeInputImage_.CountObj() ? nodeInputImage_
      : !nodeContext() && outputVisible && result_.outputImage.CountObj()
          ? result_.outputImage
          : document_.image;
  const QSize size(int(image.Width()), int(image.Height()));
  const auto mapping = !reference
                           ? (image.Type() == mv::MvPixelType::UInt8
                                  ? QStringLiteral("原始 byte 显示")
                                  : QStringLiteral("%1 显示映射")
                                        .arg(pixelTypeText(image.Type())))
                       : image.Type() == mv::MvPixelType::UInt8
                           ? QStringLiteral("原始 byte 显示 · 模板参考")
                           : QStringLiteral("%1 显示映射 · 模板参考")
                                 .arg(pixelTypeText(image.Type()));
  imageInfo_->setText(QStringLiteral("%1 × %2 · %3 通道 · %4")
                          .arg(size.width())
                          .arg(size.height())
                          .arg(image.Channels())
                          .arg(mapping));
  syncNodeSettings();
  const int source = nodeContext() && selectedStep_ >= 0
                         ? workflowInputs_.value(selectedStep_, -2)
                         : -1;
  const QString input =
      source == -2 ? QStringLiteral("未连接")
      : source < 0
          ? QFileInfo(document_.imagePath).fileName()
          : QStringLiteral("%1 %2 · 图像输出")
                .arg(source + 1)
                .arg(operatorDefinition(workflow_[source].algorithm).name);
  parameterInput_->setText(QStringLiteral("输入：%1\n范围：%2")
                               .arg(input, roiText(document_.roi)));
}
void MainWindow::setDirty(bool dirty) {
  dirty_ = dirty;
  if (!dirty && editHistory_ && !restoringEdit_) editHistory_->setClean();
  const auto title = projectPath_.isEmpty()
                         ? QStringLiteral("未命名工程")
                         : QFileInfo(projectPath_).fileName();
  setWindowTitle(title + (dirty ? " *" : "") + " — MV Studio");
  projectCaption_->setText(
      title.toHtmlEscaped() +
      QStringLiteral(" &nbsp;<span style='color:%1'>•</span>&nbsp; %2")
          .arg(dirty ? "#b48236" : "#4b9b89",
               dirty ? QStringLiteral("未保存") : isBundledSamplePath(projectPath_) ? QStringLiteral("示例") : projectPath_.isEmpty() ? QStringLiteral("新建未保存") : QStringLiteral("已保存")));
}
void MainWindow::log(const QString& text) {
  logs_->appendPlainText(QTime::currentTime().toString("HH:mm:ss.zzz") +
                         "  " + text);
}
void MainWindow::setOperatorStatus(const QString& state, const char* kind,
                                   const QString& detail) {
  const auto name =
      pendingOperator_.isEmpty()
          ? operatorDefinition(document_.parameters.algorithm).name
          : pendingOperator_;
  const auto text = state==QStringLiteral("就绪") ? QStringLiteral("算子状态 · 就绪") :
      QStringLiteral("最近运行 · %1 · %2%3")
          .arg(name, state,
               detail.isEmpty() ? QString() : QStringLiteral("：") + detail);
  operatorStatusText_ = text;
  const auto displayed = text + (workflowMode_ && !timingText_.isEmpty()
                                     ? QStringLiteral(" · ") + timingText_
                                     : QString());
  operatorStatus_->setText(displayed);
  operatorStatus_->setToolTip(displayed);
  operatorStatus_->setProperty("state", QString::fromLatin1(kind));
  operatorStatus_->style()->unpolish(operatorStatus_);
  operatorStatus_->style()->polish(operatorStatus_);
  refreshDiagnostics();
}
bool MainWindow::guard(const QString& context,
                       const std::function<void()>& action, bool silent) {
  lastError_.clear();
  statusBar()->clearMessage();
  try {
    action();
    return true;
  } catch (const LoadingCancelled&) {
    taskStatus_->setText(QStringLiteral("加载已取消"));
    setOperatorStatus(QStringLiteral("已取消加载"),"idle");return false;
  } catch (const std::exception& e) {
    lastError_ =
        context + QStringLiteral("失败：") + QString::fromUtf8(e.what());
  } catch (...) {
    lastError_ = context + QStringLiteral("失败：未知错误");
  }
  log(lastError_);
  if (!silent) statusBar()->showMessage(lastError_, 15000);
  if (silent) {
    setOperatorStatus(QStringLiteral("报错"), "error", lastError_);
    setResultState(QStringLiteral("执行失败"), "error");
    taskStatus_->setText(QStringLiteral("执行失败"));
    progress_->hide();
    updateActions();
  } else if (isVisible() && persistSettings_)
    QMessageBox::warning(this, QStringLiteral("操作未完成"), lastError_);
  return false;
}
bool MainWindow::confirmDiscard() {
  if (!dirty_) return true;
  QMessageBox dialog(QMessageBox::Question,QStringLiteral("保存工程"),
      QStringLiteral("工程有未保存的更改，是否保存？"),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,this);
  dialog.setObjectName("unsavedProjectDialog");
  dialog.button(QMessageBox::Save)->setText(QStringLiteral("保存"));
  dialog.button(QMessageBox::Discard)->setText(QStringLiteral("不保存"));
  dialog.button(QMessageBox::Cancel)->setText(QStringLiteral("取消"));
  dialog.setDefaultButton(QMessageBox::Save);
  const auto choice=static_cast<QMessageBox::StandardButton>(dialog.exec());
  if (choice == QMessageBox::Cancel) return false;
  if (choice == QMessageBox::Save) {
    chooseSave();
    return !dirty_;
  }
  return true;
}
void MainWindow::chooseImage() {
  const int input=activeInputNode();if(input<0 || busy())return;
  if(multiViewInput(workflow_[input])) {showStereoImport();return;}
  if(workflow_[input].algorithm==Algorithm::SequenceInput) {showStructuredImport();return;}
  auto paths = QFileDialog::getOpenFileNames(
      this, QStringLiteral("添加图像"), {},
      QStringLiteral(
          "图像 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff);;所有文件 (*)"));
  if (!paths.isEmpty()) {
    guard(QStringLiteral("添加输入图像"),[&]{QVector<SourceData> cells;QStringList names;
      loadResources(QStringLiteral("读取输入图像…"),[&](const std::atomic_bool& cancelled){for(const auto& path:paths){if(cancelled.load())return;cells.append(loadSourceData({path},false,&cancelled));names.append(QFileInfo(path).fileName());}});
      importSourceSamples(cells,names);
    },true);
  }
}
void MainWindow::chooseProject() {
  if (busy()) return;
  auto path = QFileDialog::getOpenFileName(
      this, QStringLiteral("打开工程"), {},
      QStringLiteral("MVKit 工程 (*.mvproj *.mvkw.json);;JSON (*.json)"));
  if (!path.isEmpty() && confirmDiscard()) {
    openPath(path, true);
  }
}
void MainWindow::chooseSave(bool saveAs) {
  auto path = projectPath_;
  if(isBundledSamplePath(path)) {
    path=QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(QFileInfo(path).fileName());
    saveAs=true;
  }
  bool copyOriginalData = copyOriginalDataOnSave_;
  if (saveAs || path.isEmpty()) {
    QFileDialog dialog(this, saveAs ? QStringLiteral("另存工程") : QStringLiteral("保存工程"));
    dialog.setObjectName("saveProjectDialog");
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setOption(QFileDialog::DontConfirmOverwrite);
    dialog.setLabelText(QFileDialog::Accept, QStringLiteral("保存"));
    dialog.setLabelText(QFileDialog::Reject, QStringLiteral("取消"));
    dialog.setLabelText(QFileDialog::FileName, QStringLiteral("文件名"));
    dialog.setLabelText(QFileDialog::FileType, QStringLiteral("文件类型"));
    dialog.setLabelText(QFileDialog::LookIn, QStringLiteral("保存到"));
    dialog.setNameFilter(QStringLiteral("MV Studio 工程 (*.mvproj)"));
    dialog.setDefaultSuffix("mvproj");
    dialog.selectFile(path.isEmpty() ? "untitled.mvproj" : path);
    auto* copy = new QCheckBox(QStringLiteral("复制原始数据"), &dialog);
    copy->setObjectName("copyOriginalData");
    copy->setToolTip(QStringLiteral("将全部数据、序列及外部资源复制到工程旁的 .data 目录；迁移时请一起复制。默认仅保存数据引用。"));
    auto* grid = qobject_cast<QGridLayout*>(dialog.layout());
    grid->addWidget(copy, grid->rowCount(), 0, 1, grid->columnCount());
    if (dialog.exec() != QDialog::Accepted) return;
    path = dialog.selectedFiles().value(0);
    copyOriginalData = copy->isChecked();
    if (QFileInfo::exists(path)) {
      QMessageBox overwrite(QMessageBox::Question,QStringLiteral("覆盖工程"),
          QStringLiteral("工程已存在，是否覆盖？"),QMessageBox::Save|QMessageBox::Cancel,this);
      overwrite.setObjectName("overwriteProjectDialog");
      overwrite.button(QMessageBox::Save)->setText(QStringLiteral("覆盖"));
      overwrite.button(QMessageBox::Cancel)->setText(QStringLiteral("取消"));
      overwrite.setDefaultButton(QMessageBox::Cancel);
      if (overwrite.exec()!=QMessageBox::Save) return;
    }
  }
  if (!path.isEmpty()) saveTo(path, copyOriginalData);
}
void MainWindow::updateActions() {
  if (!runAction_) return;
  if (undoAction_) undoAction_->setEnabled(!runQueue_.active() && !nodeMoveEdit_ && editHistory_->canUndo());
  if (redoAction_) redoAction_->setEnabled(!runQueue_.active() && !nodeMoveEdit_ && editHistory_->canRedo());
  QString validation;
  try {
    if (nodeContext() && !workflowMode_ && selectedStep_ < 0)
      throw std::runtime_error("节点已删除，请重新选择流程节点");
    if (!nodeContext()) document_.parameters.validate();
    if (nodeContext()) {
      Document graph;
      graph.workflow = workflow_;
      graph.workflowInputs = workflowInputs_;
      graph.workflowLinks = workflowLinks_;
      workflowOrder(graph, true);
    }
  } catch (const std::exception& e) {
    validation = QString::fromUtf8(e.what());
  }
  validation_->setText(validation);
  validation_->setVisible(!validation.isEmpty());
  const bool explicitInputs=std::any_of(workflow_.begin(),workflow_.end(),[](const Parameters& p){return sourceTool(p.algorithm);});
  bool image = document_.hasImage() && (!explicitInputs || activeInputNode()>=0 && trackSamples(true).contains(activeImage_));
  int selected = 0;
  for (int row:trackSamples(true)) selected += images_[row].checked;
  currentRunAction_->setEnabled(image && !loading_ && !execution_.busy() && !runQueue_.active() && validation.isEmpty() && (!nodeContext() || !workflow_.isEmpty()));
  autoPreviewAction_->setEnabled(!runQueue_.active());
  runAction_->setEnabled(image && selected && !loading_ && !execution_.busy() &&
                         !runQueue_.active() && validation.isEmpty() &&
                         (!nodeContext() || !workflow_.isEmpty()));
  singleRunAction_->setEnabled(runAction_->isEnabled());
  saveAction_->setEnabled(!busy());
  saveAsAction_->setEnabled(saveAction_->isEnabled());
  rectangleAction_->setEnabled(image && !runQueue_.active());
  selectAction_->setEnabled(image && !runQueue_.active());
  panAction_->setEnabled(image);
  circleAction_->setEnabled(image && !runQueue_.active());
  clearRoiAction_->setEnabled(image && !runQueue_.active());
  cancelAction_->setEnabled(busy());
  operatorControls_->setEnabled(!runQueue_.active() &&
                                (!nodeContext() || selectedStep_ >= 0));
  library_->setEnabled(!runQueue_.active());
  if (nodeLibrary_) nodeLibrary_->setEnabled(!runQueue_.active());
  workflowView_->setEnabled(!runQueue_.active());
  nodeSearch_->setEnabled(!runQueue_.active());
  workbenchMode_->setEnabled(!runQueue_.active());
  workflowModeButton_->setEnabled(!runQueue_.active());
  for (const char* name : {"workbenchModeAction", "workflowModeAction"})
    if (auto* action = findChild<QAction*>(name))
      action->setEnabled(!runQueue_.active());
  view_->setRoiEditingEnabled(!runQueue_.active());
  const int input=activeInputNode();
  if(auto* calibration=findChild<QAction*>("cameraCalibrationAction")){calibration->setEnabled(!busy() && input>=0 && workflow_[input].algorithm==Algorithm::ImageInput && !multiViewInput(workflow_[input]) && !trackSamples().isEmpty());calibration->setToolTip(QStringLiteral("使用当前单图输入轨的已选数据进行单目标定"));}
  for(const auto& name:{"addImagesAction","importStereoGroups","importStructuredSequence"})if(auto* action=findChild<QAction*>(name))
    action->setEnabled(!busy() && input>=0 && (QString(name)=="addImagesAction" || QString(name)=="importStereoGroups" && multiViewInput(workflow_[input]) || QString(name)=="importStructuredSequence" && workflow_[input].algorithm==Algorithm::SequenceInput));
  syncNodeSettings();
  nodeScope_->setEnabled(!runQueue_.active());
  roiPurpose_->setEnabled(!runQueue_.active());
  workflowOutputChoice_->setEnabled(!runQueue_.active());
  runAction_->setToolTip(
      QStringLiteral("运行全部勾选的数据一次（Shift+F5），保留各项 "
                     "ROI，使用当前算子 / 流程参数"));
  syncViewTools();
  refreshDiagnostics();
}
void MainWindow::restoreDefaultLayout() {
  for (int mode = 0; mode < 2; ++mode) {
    modeLayouts_[mode].clear();
    modeViewSplits_[mode].clear();
    modeSpatialSplits_[mode].clear();
  }
  restoreState(defaultLayout_, 4);
  outputDock_->raise();
  nodeOutputDock_->setVisible(workflowMode_);
  if (workflowMode_)
    resizeDocks({nodeOutputDock_, outputDock_}, {290, 485}, Qt::Vertical);
  views_->setSizes({1, 1});
  spatialViews_->setSizes({1, 1});
  view_->fitImage();
}
void MainWindow::newProject() {
  if (persistSettings_ && !confirmDiscard()) return;
  stopExecution();
  removeImages(true);
  projectPath_.clear();
  copyOriginalDataOnSave_ = false;
  workflow_.clear();workflowIds_.clear();workflowAliases_.clear();debugNodeId_.clear();
  workflowInputs_.clear();workflowLinks_.clear();workflowOutput_=-2;sharedRanges_.clear();
  workbenchRanges_ = QVector<NodeSettings>(toolSlotCount);
  models_.clear();
  workflowPositions_.clear();
  workflowView_->setSteps({}, -1);
  selectedStep_ = -1;
  document_.parameters = {};
  workbenchParameters_ = {};
  result_ = {};
  setWorkflowMode(true);
  syncParameters();
  resetEditHistory();
  setDirty(false);
  pendingOperator_.clear();
  refreshExecutionTiming(true);
  setOperatorStatus(QStringLiteral("就绪"),"idle");
  setResultState(QStringLiteral("尚未运行"),"idle");
  taskStatus_->setText(QStringLiteral("就绪"));
  updateActions();
}
void MainWindow::stopExecution() {
  const bool wasBusy = busy();
  if(loadingCancel_)loadingCancel_->store(true);
  const bool running = execution_.busy();
  autoPreview_->stop();
  const bool wasBatch = runQueue_.active();
  runQueue_.cancel();
  for (auto& image : images_) {
    if (running && image.id == pendingImage_) {
      if (pendingWorkflow_)
        ++image.version;
      else
        ++image.baseVersion;
    }
    if (image.batchState == 1 || image.batchState == 2) image.batchState = 0;
  }
  execution_.cancel();
  batchCalibrationDirectory_.reset();
  if (wasBatch) {
    refreshInputChoices();
    syncGeometryUi();
  }
  workflowView_->setRunning(-3);
  pendingImage_.clear();
  if (wasBusy) setOperatorStatus(QStringLiteral("已停止"), "idle");
  progress_->hide();
  if (wasBusy) {
    taskStatus_->setText(QStringLiteral("已停止"));
    setResultState(QStringLiteral("已停止"), "stale");
    log(QStringLiteral("已停止当前任务及剩余队列，保留已完成结果。"));
  }
  view_->setRoiEditingEnabled(true);
  refreshFilm();
  updateActions();
}
void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  enableStudioNativeResize(this);
}
void MainWindow::changeEvent(QEvent* event) {
  QMainWindow::changeEvent(event);
  if (event->type() == QEvent::WindowStateChange && maximizeButton_) {
    maximizeButton_->setIcon(
        studioIcon(isMaximized() ? "restore" : "maximize"));
    maximizeButton_->setToolTip(isMaximized() ? QStringLiteral("还原窗口")
                                              : QStringLiteral("最大化"));
  }
}
bool MainWindow::nativeEvent(const QByteArray& type, void* message,
                             long* result) {
  if (handleStudioNativeEvent(this, message, result)) return true;
  return QMainWindow::nativeEvent(type, message, result);
}
void MainWindow::closeEvent(QCloseEvent* event) {
  if (persistSettings_ && !confirmDiscard()) {
    event->ignore();
    return;
  }
  stopExecution();
  if (persistSettings_) {
    rememberModeLayout();
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/layout-v6", saveState(4));
    for (int mode = 0; mode < 2; ++mode) {
      const auto key = QString("window/mode-%1/").arg(mode);
      s.setValue(key + "docks-v6", modeLayouts_[mode]);
      s.setValue(key + "views", modeViewSplits_[mode]);
      s.setValue(key + "spatial", modeSpatialSplits_[mode]);
    }
    if (!workflowMode_)
      s.setValue("window/workbenchLibrarySplit", librarySplit_->saveState());
  }
  event->accept();
}
}  // namespace workbench
