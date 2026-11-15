// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/shell/main_window.hpp"
#include "ui/dialogs/sample_projects.hpp"
#include "ui/widgets/studio_controls.hpp"
#include "ui/views/model_view.hpp"
#include "ui/views/workflow_view.hpp"
#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>
namespace workbench {
void MainWindow::rememberProject(const QString& path) {
  if(!persistSettings_ || path.isEmpty() || isBundledSamplePath(path))return;
  QSettings settings;auto paths=settings.value("projects/recent").toStringList();
  paths.removeAll(path);paths.prepend(path);while(paths.size()>8)paths.removeLast();
  settings.setValue("projects/recent",paths);
}
void MainWindow::refreshRecentProjects() {
  if(!recentProjectsMenu_)return;
  recentProjectsMenu_->clear();
  const auto paths=persistSettings_ ? QSettings().value("projects/recent").toStringList() : QStringList{};
  for(const auto& path:paths) {
    auto* action=recentProjectsMenu_->addAction(QFileInfo(path).fileName());
    action->setToolTip(path);action->setEnabled(QFileInfo::exists(path)&&!busy());
    connect(action,&QAction::triggered,this,[this,path]{if(!busy()&&confirmDiscard())openPath(path,true);});
  }
  if(paths.isEmpty())recentProjectsMenu_->addAction(QStringLiteral("暂无最近工程"))->setEnabled(false);
}
void MainWindow::buildMenus() {
  auto* file = mainMenu_->addMenu(QStringLiteral("文件"));
  file->setObjectName("fileMenu");
  auto* newAction =
      file->addAction(studioIcon("new"), QStringLiteral("新建工程"));
  newAction->setObjectName("newProjectAction");
  newAction->setShortcut(QKeySequence::New);
  connect(newAction, &QAction::triggered, this, &MainWindow::newProject);
  auto* dataMenu = mainMenu_->addMenu(QStringLiteral("数据"));
  auto* open =
      dataMenu->addAction(studioIcon("image"), QStringLiteral("添加图像…"));
  open->setObjectName("addImagesAction");
  open->setShortcut(QKeySequence("Ctrl+I"));
  connect(open, &QAction::triggered, this, &MainWindow::chooseImage);
  connect(findChild<QPushButton*>("addImagesButton"),&QPushButton::clicked,open,&QAction::trigger);
  auto* project = file->addAction(QStringLiteral("打开工程"));
  project->setObjectName("openProjectAction");
  project->setIcon(studioIcon("folder"));
  project->setShortcuts({QKeySequence::Open, QKeySequence("Ctrl+Shift+O")});
  connect(project, &QAction::triggered, this, &MainWindow::chooseProject);
  auto* stereoImport = dataMenu->addAction(QStringLiteral("导入双目图组…"));
  stereoImport->setObjectName("importStereoGroups");
  connect(stereoImport, &QAction::triggered, this,
          &MainWindow::showStereoImport);
  auto* structuredImport = dataMenu->addAction(QStringLiteral("导入结构光序列…"));
  structuredImport->setObjectName("importStructuredSequence");
  connect(structuredImport, &QAction::triggered, this, &MainWindow::showStructuredImport);
  if(auto* menu=findChild<QMenu*>("addDataMenu")) {
    menu->addAction(open);menu->addAction(stereoImport);menu->addAction(structuredImport);
  }
  auto* patterns = dataMenu->addAction(QStringLiteral("生成结构光投影图案…"));
  patterns->setObjectName("generateStructuredPatterns");
  connect(patterns, &QAction::triggered, this, &MainWindow::showStructuredGenerator);
  auto* captures = dataMenu->addAction(QStringLiteral("资源管理…"));
  captures->setObjectName("manageCaptureGroups");
  connect(captures, &QAction::triggered, this, &MainWindow::showCaptureManager);
  auto* sequence = dataMenu->addAction(QStringLiteral("浏览当前曝光序列…"));
  sequence->setObjectName("browseStructuredSequence");
  connect(dataMenu, &QMenu::aboutToShow, this, [this, sequence] {
    sequence->setEnabled(!document_.structuredFrames.isEmpty());
  });
  connect(sequence, &QAction::triggered, this, [this] { showStructuredSequence(); });
  auto* exportCloud = dataMenu->addAction(QStringLiteral("导出当前点云…"));
  exportCloud->setObjectName("exportObservedCloud");
  connect(dataMenu, &QMenu::aboutToShow, this, [this, exportCloud] {
    exportCloud->setEnabled(!busy() && spatialView_ && !spatialView_->points.isEmpty());
  });
  connect(exportCloud, &QAction::triggered, this, [this] {
    if (busy()) return;
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出完整有效点云 / mm"),
        "point-cloud.ply", QStringLiteral("PLY 点云 (*.ply)"));
    if (!path.isEmpty()) exportObservedPointCloud(path);
  });
  saveAction_ = file->addAction(studioIcon("save"), QStringLiteral("保存工程"));
  saveAction_->setShortcut(QKeySequence::Save);
  connect(saveAction_, &QAction::triggered, this, [this] { chooseSave(); });
  saveAsAction_ = file->addAction(QStringLiteral("另存工程"));
  connect(saveAsAction_, &QAction::triggered, this,
          [this] { chooseSave(true); });
  file->addSeparator();
  auto* examples = file->addAction(studioIcon("examples"), QStringLiteral("示例工程"));
  examples->setObjectName("exampleProjectsAction");
  connect(examples, &QAction::triggered, this, &MainWindow::showExampleProjects);
  recentProjectsMenu_ = file->addMenu(QStringLiteral("最近工程"));
  recentProjectsMenu_->setObjectName("recentProjectsMenu");
  connect(recentProjectsMenu_, &QMenu::aboutToShow, this, &MainWindow::refreshRecentProjects);
  refreshRecentProjects();
  file->addSeparator();
  auto* quit = file->addAction(QStringLiteral("退出"));
  connect(quit, &QAction::triggered, this, &QWidget::close);
  auto* edit = mainMenu_->addMenu(QStringLiteral("编辑"));
  undoAction_ = editHistory_->createUndoAction(this, QStringLiteral("撤销"));
  redoAction_ = editHistory_->createRedoAction(this, QStringLiteral("重做"));
  undoAction_->setIcon(studioIcon("undo"));
  redoAction_->setIcon(studioIcon("redo"));
  undoAction_->setObjectName("undoAction");
  redoAction_->setObjectName("redoAction");
  undoAction_->setShortcut(QKeySequence::Undo);
  redoAction_->setShortcuts({QKeySequence::Redo, QKeySequence("Ctrl+Shift+Z")});
  edit->addAction(undoAction_);
  edit->addAction(redoAction_);
  edit->addSeparator();
  auto* selectImages = edit->addAction(QStringLiteral("选择全部图像"));
  connect(selectImages, &QAction::triggered, this,
          [this] { this->selectImages(true); });
  auto* removeImages = edit->addAction(QStringLiteral("移除选中图像"));
  connect(removeImages, &QAction::triggered, this,
          [this] { this->removeImages(); });
  auto* view = mainMenu_->addMenu(QStringLiteral("视图"));
  view->setObjectName("viewMenu");
  auto* modes=view->addMenu(QStringLiteral("工作模式"));
  modes->setObjectName("workModeMenu");
  view->addSeparator();
  auto* fit = view->addAction(QStringLiteral("适应活动视图"));
  fit->setObjectName("fitActiveViewAction");
  fit->setShortcut(QKeySequence("F"));
  connect(fit, &QAction::triggered, this, [this] {
    auto* model=workflowMode_ ? nodeModelView_ : modelView_;
    auto* focus=QApplication::focusWidget();
    if(model && model->isVisible() && (model==focus || model->isAncestorOf(focus))) {
      model->resetCamera();return;
    }
    if (workflowMode_ && !nodeView_->hasFocus())
      workflowView_->fitGraph();
    else
      (workflowMode_ ? nodeView_ : view_)->fitImage();
  });
  auto* actual = view->addAction(QStringLiteral("图像原始大小 1:1"));
  actual->setObjectName("actualSizeAction");
  actual->setShortcut(QKeySequence("Ctrl+1"));
  connect(actual, &QAction::triggered, this,
          [this] { (workflowMode_ ? nodeView_ : view_)->actualSize(); });
  overlayAction_ = view->addAction(QStringLiteral("显示结果元素"));
  overlayAction_->setCheckable(true);
  overlayAction_->setChecked(true);
  connect(overlayAction_, &QAction::toggled, this, [this](bool visible) {
    (workflowMode_ ? nodeView_ : view_)->setOverlayVisible(visible);
  });
  for (auto* dock : findChildren<QDockWidget*>())
    view->addAction(dock->toggleViewAction());
  auto* redock=view->addAction(QStringLiteral("停靠视图"));
  redock->setObjectName("dockNodeViewAction");
  redock->setEnabled(nodeOutputDock_->isFloating());
  connect(redock,&QAction::triggered,this,[this] {
    if(!workflowMode_)setWorkflowMode(true);
    setNodeViewFloating(false);
  });
  auto* reset = view->addAction(QStringLiteral("恢复默认布局"));
  reset->setObjectName("restoreLayoutAction");
  connect(reset, &QAction::triggered, this,
          &MainWindow::restoreDefaultLayout);
  auto* workbench = modes->addAction(QStringLiteral("工作台"));
  auto* workflow = modes->addAction(QStringLiteral("工作流"));
  modes->insertAction(workbench, workflow);
  workbench->setObjectName("workbenchModeAction");
  workflow->setObjectName("workflowModeAction");
  workbench->setCheckable(true);
  workflow->setCheckable(true);
  workbench->setChecked(true);
  auto* modeGroup = new QActionGroup(this);
  modeGroup->addAction(workbench);
  modeGroup->addAction(workflow);
  connect(workbench, &QAction::triggered, this,
          [this] { setWorkflowMode(false); });
  connect(workflow, &QAction::triggered, this,
          [this] { setWorkflowMode(true); });
  auto* execution = mainMenu_->addMenu(QStringLiteral("执行"));
  auto* toolsMenu = mainMenu_->addMenu(QStringLiteral("工具"));
  dataMenu->removeAction(patterns);
  toolsMenu->addAction(patterns);
  auto* report = execution->addAction(QStringLiteral("批量结果与导出…"));
  report->setObjectName("batchResultsAction");
  connect(report, &QAction::triggered, this, &MainWindow::showBatchResults);
  auto* calibrationAction = toolsMenu->addAction(QStringLiteral("相机标定…"));
  calibrationAction->setObjectName("cameraCalibrationAction");
  connect(calibrationAction, &QAction::triggered, this,
          &MainWindow::showCalibrationWizard);
  toolsMenu->addSeparator();
  auto* roiMenu = toolsMenu->addMenu(QStringLiteral("ROI 绘制"));
  auto* group = new QActionGroup(this);
  group->setExclusive(true);
  auto makeTool = [this, group, roiMenu](const QString& title,
                                         const QString& icon, const char* key,
                                         ImageView::Tool tool) {
    auto* a = roiMenu->addAction(studioIcon(icon), title);
    a->setCheckable(true);
    a->setShortcut(QKeySequence(key));
    group->addAction(a);
    connect(a, &QAction::triggered, this, [this, tool] {
      (workflowMode_ ? nodeView_ : view_)->setTool(tool);
    });
    return a;
  };
  panAction_ =
      makeTool(QStringLiteral("平移"), "pan", "V", ImageView::Tool::Pan);
  selectAction_ = makeTool(QStringLiteral("选择 / 调整 ROI"), "select", "S",
                           ImageView::Tool::Select);
  selectAction_->setChecked(true);
  rectangleAction_ = makeTool(QStringLiteral("矩形 ROI"), "rect", "R",
                              ImageView::Tool::Rectangle);
  circleAction_ = makeTool(QStringLiteral("圆 ROI"), "circle", "C",
                           ImageView::Tool::Circle);
  clearRoiAction_ = roiMenu->addAction(QStringLiteral("清除 ROI"));
  clearRoiAction_->setToolTip(
      QStringLiteral("箭头模式选中 ROI 后可按 Delete 删除"));
  connect(clearRoiAction_, &QAction::triggered, this,
          [this] { applyDocumentRoi({}); });
  buildImageNavigation(view_);
  runAction_ =
      execution->addAction(studioIcon("play"), QStringLiteral("连续运行"));
  runAction_->setObjectName("runAction");
  runAction_->setShortcut(QKeySequence("Shift+F5"));
  connect(runAction_, &QAction::triggered, this, [this] { runSelected(); });
  singleRunAction_ =
      execution->addAction(studioIcon("step"), QStringLiteral("单次运行"));
  singleRunAction_->setObjectName("singleRunAction");
  singleRunAction_->setShortcut(QKeySequence("Ctrl+F5"));
  singleRunAction_->setToolTip(
      QStringLiteral("处理下一项勾选数据，末尾回到第一项（Ctrl+F5）"));
  connect(singleRunAction_, &QAction::triggered, this, [this] { runNext(); });
  currentRunAction_ = execution->addAction(studioIcon("run-current"), QStringLiteral("运行当前"));
  currentRunAction_->setObjectName("runCurrentAction");
  currentRunAction_->setShortcut(QKeySequence("F5"));
  currentRunAction_->setToolTip(QStringLiteral("处理当前显示的数据（F5）"));
  execution->insertAction(singleRunAction_, currentRunAction_);
  connect(currentRunAction_, &QAction::triggered, this, [this]{ run(); });
  autoPreviewAction_ = execution->addAction(QStringLiteral("自动预览当前数据"));
  autoPreviewAction_->setObjectName("autoPreviewAction");
  autoPreviewAction_->setCheckable(true);
  autoPreviewAction_->setChecked(true);
  connect(autoPreviewAction_, &QAction::toggled, this, [this](bool enabled){
    if(!enabled) { autoPreview_->stop(); updateActions(); }
  });
  cancelAction_ =
      execution->addAction(studioIcon("stop"), QStringLiteral("停止"));
  cancelAction_->setObjectName("stopAction");
  cancelAction_->setShortcut(QKeySequence("F6"));
  connect(cancelAction_, &QAction::triggered, this,
          &MainWindow::stopExecution);
  auto* help = mainMenu_->addMenu(QStringLiteral("帮助"));
  auto* usage = help->addAction(QStringLiteral("操作说明"));
  connect(usage, &QAction::triggered, this, [this] {
    QMessageBox::information(
        this, QStringLiteral("操作说明"),
        QStringLiteral(
            "可在执行菜单关闭自动预览，参数或 ROI 变动后按 F5 运行当前。连续运行（Shift+"
            "F5）完成一轮所选数据，单次运行（Ctrl+F5）处理下一项勾选数据，使"
            "用当前算子/流程参数和各图自己的 "
            "ROI。停止（F6）取消当前任务及剩余队列。\n\n"
            "点击胶片上半部缩略图切换显示；点击下半部名称独立多选。运行期间不"
            "能"
            "切图或改变选择。上半部蓝框表示当前显示，金框表示处理中，下半部绿"
            "色"
            "框表示选中。\n\n"
            "S 选择 ROI，V 平移，R/C 绘制矩形/圆；方向键微调，Shift "
            "加速，Esc "
            "取消，Delete 删除。中键或空格临时平移，滚轮缩放，F 适应，Ctrl+1 "
            "原始大小。\n\n"
            "工作流：从左侧拖入算子，拖动端口连线或改接；点击输入端口或右侧输"
            "入"
            "框选择图像源。选中节点或连线按 Delete "
            "删除，也可右键操作。右侧分隔条可调预览高度。\n\n"
            "2D、3D 可分别或同时显示。3D 导入 PLY/MVKit "
            "点云，左键旋转、中键/右键平移、滚轮缩放；只显示实际模型点，不将 "
            "2D 图像转换为伪 3D。"));
  });
  auto* about = help->addAction(QStringLiteral("关于 MV Studio"));
  connect(about, &QAction::triggered, this, [this] {
    QMessageBox::information(
        this, "MV Studio",
        QStringLiteral(
            "Qt %1 · MVKit %2\n自有代码 Apache-2.0；Qt 许可见 third-party "
            "目录。\n工作流支持拖拽节点、自由连线及图像分支拓扑执行。")
            .arg(qVersion(), QCoreApplication::applicationVersion()));
  });
  auto* toolbar = mainToolbar_;
  toolbar->addAction(newAction);
  toolbar->addAction(project);
  toolbar->addAction(saveAction_);
  toolbar->addAction(examples);
  toolbar->addSeparator();
  toolbar->addAction(undoAction_);
  toolbar->addAction(redoAction_);
  toolbar->addSeparator();
  view2d_ = toolbar->addAction(studioIcon("view-2d"), QStringLiteral("2D"));
  view3d_ = toolbar->addAction(studioIcon("cube"), QStringLiteral("3D"));
  view2d_->setObjectName("view2DAction");
  view3d_->setObjectName("view3DAction");
  for (auto* action : {view2d_, view3d_}) {
    action->setCheckable(true);
    connect(action, &QAction::triggered, this, &MainWindow::updateViews);
  }
  view2d_->setChecked(true);
  view2d_->setToolTip(
      QStringLiteral("显示 2D 图像 / 工作流；可与 3D 同时开启"));
  view3d_->setToolTip(QStringLiteral("显示 3D 点云；可与 2D 同时开启"));
  auto* layoutMenu = new QMenu(this);
  layoutMenu->setObjectName("layoutMenu");
  auto* resetLayout = layoutMenu->addAction(QStringLiteral("恢复默认布局"));
  resetLayout->setObjectName("resetLayoutAction");
  connect(resetLayout, &QAction::triggered, this,
          &MainWindow::restoreDefaultLayout);
  auto* saveLayout = layoutMenu->addAction(QStringLiteral("保存当前布局"));
  saveLayout->setObjectName("saveLayoutAction");
  connect(saveLayout, &QAction::triggered, this, [this] {
    QSettings s;
    s.setValue("window/customLayout-v6", saveState(4));
    s.setValue("window/customViews", views_->saveState());
    s.setValue("window/customSpatialViews", spatialViews_->saveState());
  });
  auto* loadLayout = layoutMenu->addAction(QStringLiteral("加载已保存布局"));
  loadLayout->setObjectName("loadLayoutAction");
  connect(loadLayout, &QAction::triggered, this, [this] {
    QSettings s;
    const auto state = s.contains("window/customLayout-v6")
        ? s.value("window/customLayout-v6").toByteArray()
        : migrateLegacyLayout(s.value("window/customLayout-v5",s.value("window/customLayout-v4")).toByteArray());
    if (!state.isEmpty()) restoreState(state, 4);
    nodeOutputDock_->setVisible(workflowMode_);
    views_->restoreState(s.value("window/customViews").toByteArray());
    spatialViews_->restoreState(s.value("window/customSpatialViews").toByteArray());
    rememberModeLayout();
  });
  auto* layoutButton = new QToolButton(toolbar);
  layoutButton->setObjectName("layoutButton");
  layoutButton->setIcon(studioIcon("layout"));
  layoutButton->setText(QStringLiteral("布局"));
  layoutButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  layoutButton->setToolTip(QStringLiteral("布局"));layoutButton->setAccessibleName(QStringLiteral("布局"));
  layoutButton->setPopupMode(QToolButton::InstantPopup);
  layoutButton->setMenu(layoutMenu);
  toolbar->addWidget(layoutButton);
  auto* spring = new QWidget(toolbar);
  spring->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  toolbar->addWidget(spring);
  toolbar->addAction(cancelAction_);
  toolbar->addAction(singleRunAction_);
  toolbar->addAction(runAction_);
  if (auto* button=qobject_cast<QToolButton*>(toolbar->widgetForAction(runAction_))) {
    button->setObjectName("primaryTool");
    button->setIconSize(QSize(20,20));
    button->setIcon(studioIcon("play", runAction_->isEnabled() ? QColor(Qt::white) : QColor("#9ca8b8")));
    button->setText(runAction_->text());
    connect(runAction_, &QAction::changed, button, [this, button] {
      button->setIcon(studioIcon("play", runAction_->isEnabled() ? QColor(Qt::white) : QColor("#9ca8b8")));
      button->setText(runAction_->text());
    });
  }
  for(auto* action:toolbar->actions())if(auto* button=qobject_cast<QToolButton*>(toolbar->widgetForAction(action))) {
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);button->setIconSize(QSize(20,20));button->setFixedSize(36,32);
  }
  for(auto* action:{view2d_,view3d_})
    if(auto* button=qobject_cast<QToolButton*>(toolbar->widgetForAction(action))) {
      button->setProperty("viewToggle",true);
      button->setFixedSize(32,32);
    }
  for(auto* action:{cancelAction_,singleRunAction_,runAction_}) {
    auto* button=qobject_cast<QToolButton*>(toolbar->widgetForAction(action));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setText(action->text());
    button->setFixedSize(action==cancelAction_ ? 66 : action==runAction_ ? 128 : 100,action==runAction_ ? 34 : 32);
    if(action==singleRunAction_) button->setObjectName("singleRunTool");
    if(action==cancelAction_)button->setObjectName("stopTool");
  }
  mainMenu_->ensurePolished();
  // Fusion adds a horizontal menu margin in addition to the item padding.
  // Align visible project text with the actual first menu item, not just
  // the two containers' edges.
  findChild<QWidget*>("projectInfo")
      ->layout()
      ->setContentsMargins(
          mainMenu_->actionGeometry(mainMenu_->actions().first()).left() + 8,
          0, 0, 0);
}
}
