// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include <QComboBox>
#include <QFileInfo>
#include <QDockWidget>
#include <QHash>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTreeWidget>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <stdexcept>

#include "ui/style/appearance.hpp"
#include "ui/style/operator_icons.hpp"
#include "ui/shell/main_window.hpp"
#include "operators/operator_registry.hpp"
#include "model/spatial_data.hpp"
#include "ui/widgets/responsive_widgets.hpp"
#include "ui/views/workflow_view.hpp"
namespace workbench {
QStringList MainWindow::nodeNames() const {
  return workflowNodeNames(workflow_, workflowAliases_);
}
void MainWindow::buildNodeLibrary(QWidget* page, QDockWidget* resources) {
  operatorsTitle_ = page->findChild<QLabel*>("operatorsHeading");
  librarySplit_ = new ViewSplitter(Qt::Vertical, resources);
  librarySplit_->setObjectName("workbenchLibrarySplit");
  librarySplit_->setHandleWidth(5);
  librarySplit_->setChildrenCollapsible(false);
  librarySplit_->addWidget(page);
  nodeLibraryPanel_ = new QWidget(librarySplit_);
  auto* layout = new QVBoxLayout(nodeLibraryPanel_);
  layout->setContentsMargins(6, 6, 6, 6);
  auto* title = new QLabel(QStringLiteral("流程节点"), nodeLibraryPanel_);
  title->setObjectName("flowNodesHeading");
  layout->addWidget(title);
  auto* search = new QLineEdit(nodeLibraryPanel_);
  search->setObjectName("nodeSearch");
  search->setPlaceholderText(QStringLiteral("搜索流程节点…"));
  layout->addWidget(search);
  nodeLibrary_ = new QTreeWidget(nodeLibraryPanel_);
  nodeLibrary_->setObjectName("workflowNodeLibrary");
  nodeLibrary_->setHeaderHidden(true);
  nodeLibrary_->setIconSize(QSize(16,16));
  nodeLibrary_->setRootIsDecorated(false);
  nodeLibrary_->setDragEnabled(false);
  nodeLibrary_->setContextMenuPolicy(Qt::CustomContextMenu);
  layout->addWidget(nodeLibrary_, 1);
  librarySplit_->addWidget(nodeLibraryPanel_);
  librarySplit_->setSizes({430, 300});
  if (persistSettings_) {
    QSettings settings;
    librarySplit_->restoreState(
        settings.value("window/workbenchLibrarySplit").toByteArray());
  }
  resources->setWidget(librarySplit_);
  connect(search, &QLineEdit::textChanged, this, [this](const QString& text) {
    for (int i = 0; i < nodeLibrary_->topLevelItemCount(); ++i) {
      auto* item = nodeLibrary_->topLevelItem(i);
      item->setHidden(!item->text(0).contains(text, Qt::CaseInsensitive));
    }
  });
  connect(nodeLibrary_, &QTreeWidget::itemClicked, this,
          [this](QTreeWidgetItem* item) {
            debugNode(item->data(0, Qt::UserRole).toString());
          });
  connect(nodeLibrary_, &QTreeWidget::customContextMenuRequested, this,
          [this](QPoint position) {
            auto* item = nodeLibrary_->itemAt(position);
            if (!item || runQueue_.active()) return;
            const auto id = item->data(0, Qt::UserRole).toString();
            QMenu menu;
            auto* debug = menu.addAction(QStringLiteral("调试此节点"));
            auto* rename = menu.addAction(QStringLiteral("重命名…"));
            auto* action =
                menu.exec(nodeLibrary_->viewport()->mapToGlobal(position));
            if (action == debug)
              debugNode(id);
            else if (action == rename)
              requestNodeRename(workflowIds_.indexOf(id));
          });
  refreshNodeLibrary();
}
void MainWindow::refreshNodeLibrary() {
  if (!nodeLibrary_) return;
  const QSignalBlocker block(nodeLibrary_);
  const int scroll = nodeLibrary_->verticalScrollBar()->value();
  Document graph;
  graph.workflow = workflow_;
  graph.workflowLinks = workflowLinks_;
  graph.workflowInputs = workflowInputs_;
  QVector<int> order;
  try {
    order = workflowOrder(graph, true);
  } catch (...) {
    for (int i = 0; i < workflow_.size(); ++i) order.append(i);
  }
  const auto names = nodeNames();
  const auto query =
      nodeLibraryPanel_->findChild<QLineEdit*>("nodeSearch")->text();
  bool rebuild = nodeLibrary_->topLevelItemCount() != order.size();
  for (int row = 0; !rebuild && row < order.size(); ++row) {
    auto* item = nodeLibrary_->topLevelItem(row);
    rebuild =
        item->data(0, Qt::UserRole).toString() != workflowIds_[order[row]] ||
        item->text(0) != names[order[row]];
  }
  if (rebuild)
    nodeLibrary_->clear();
  else
    nodeLibrary_->clearSelection();
  int row = 0;
  for (int i : order) {
    auto* item = rebuild ? new QTreeWidgetItem(nodeLibrary_, {names[i]})
                         : nodeLibrary_->topLevelItem(row);
    ++row;
    item->setData(0, Qt::UserRole, workflowIds_.value(i));
    const auto& op = operatorDefinition(workflow_[i].algorithm);
    item->setIcon(0, studioIcon(operatorIconName(multiViewInput(workflow_[i]) ? Algorithm::StereoImage : op.id)));
    const int source = workflowInputs_.value(i, -2);
    item->setToolTip(0, QStringLiteral("流程节点 · %1\n类型：%2\n输入：%3")
                            .arg(names[i], multiViewInput(workflow_[i]) ? QStringLiteral("多视输入") : op.name,
                                 source == -1 ? QStringLiteral("原始图像")
                                 : source < 0 ? QStringLiteral("未连接")
                                              : names.value(source)));
    item->setHidden(!names[i].contains(query, Qt::CaseInsensitive));
    if (!workflowMode_ && workflowIds_.value(i) == debugNodeId_)
      nodeLibrary_->setCurrentItem(item);
  }
  nodeLibrary_->verticalScrollBar()->setValue(scroll);
  nodeLibraryPanel_->setVisible(!workflowMode_);
  operatorsTitle_->setText(workflowMode_ ? QStringLiteral("算子")
                                         : QStringLiteral("基础算子"));
  if (!workflowMode_ && !debugNodeId_.isEmpty())
    library_->clearSelection();
  else if (!workflowMode_) {
    const QSignalBlocker libraryBlock(library_);
    const int basicScroll = library_->verticalScrollBar()->value();
    QTreeWidgetItem* selected = nullptr;
    for (int group = 0; group < library_->topLevelItemCount(); ++group) {
      auto* category = library_->topLevelItem(group);
      if (category == recentGroup_) continue;
      for (int child = 0; child < category->childCount(); ++child)
        if (category->child(child)->data(0, Qt::UserRole).toInt() ==
            int(document_.parameters.algorithm))
          selected = category->child(child);
    }
    auto* current = library_->currentItem();
    if (current && current->data(0, Qt::UserRole).isValid() &&
        current->data(0, Qt::UserRole).toInt() ==
            int(document_.parameters.algorithm))
      current->setSelected(true);
    else
      library_->setCurrentItem(selected);
    library_->verticalScrollBar()->setValue(basicScroll);
  }
}
bool MainWindow::debugNode(const QString& id) {
  const int index = workflowIds_.indexOf(id);
  if (runQueue_.active() || index < 0) return false;
  ++editNavigation_;
  stopExecution();
  storeActive();
  if (!nodeContext()) workbenchParameters_ = document_.parameters;
  debugNodeId_ = id;
  selectedStep_ = index;
  if (workflowMode_) setWorkflowMode(false);
  document_.parameters = workflow_[index];
  roiPurpose_->setCurrentIndex(0);
  syncParameters();
  refreshWorkflow();
  refreshContext();
  refreshNodeOutput();
  refreshFilm();
  schedulePreview();
  return true;
}
bool MainWindow::selectBasicOperator(Algorithm tool) {
  if (runQueue_.active() || workflowMode_) return false;
  ++editNavigation_;
  stopExecution();
  storeActive();
  if (!nodeContext()) workbenchParameters_ = document_.parameters;
  const bool changed = workbenchParameters_.algorithm != tool;
  debugNodeId_.clear();
  document_.parameters = workbenchParameters_;
  document_.parameters.algorithm = tool;
  workbenchParameters_ = document_.parameters;
  if (changed && document_.hasImage()) setDirty(true);
  if (changed && activeImage_ >= 0) ++images_[activeImage_].baseVersion;
  document_.roi = effectiveRange().scope == ScopeMode::Manual
                      ? effectiveRange().roi
                      : Roi{};
  syncParameters();
  result_ = activeImage_ >= 0 && images_[activeImage_].resultVersion ==
                                     images_[activeImage_].baseVersion
                ? images_[activeImage_].result
                : Result{};
  view_->setImage(document_.preview, true);
  showResult(result_);
  updateRangeView();
  refreshContext();
  refreshFilm();
  updateActions();
  schedulePreview();
  return true;
}
void MainWindow::refreshOperatorSearch() {
  if(library_) {
    const auto* search=findChild<QLineEdit*>("operatorSearch");
    const auto query=search ? search->text() : QString();
    for(int i=0;i<library_->topLevelItemCount();++i) {
      auto* group=library_->topLevelItem(i);bool any=false;
      for(int j=0;j<group->childCount();++j) {
        auto* item=group->child(j);
        const auto& op = operatorDefinition(static_cast<Algorithm>(item->data(0,Qt::UserRole).toInt()));
        const auto terms = QStringList{item->text(0), op.category, op.description, op.key} + op.bindings + operatorAliases(op.id);
        const bool show=(workflowMode_ || !sourceTool(static_cast<Algorithm>(item->data(0,Qt::UserRole).toInt()))) && terms.join(' ').contains(query.trimmed(),Qt::CaseInsensitive);
        item->setHidden(!show);any|=show;
      }
      group->setHidden(!any);
    }
  }
}
void MainWindow::refreshContext() {
  refreshOperatorSearch();
  if (auto* back = findChild<QToolButton*>("returnToWorkflow"))
    back->setVisible(!workflowMode_ && nodeContext());
  refreshNodeLibrary();
}
bool MainWindow::renameWorkflowNode(const QString& id, const QString& name) {
  const int index = workflowIds_.indexOf(id);
  const auto trimmed = name.trimmed();
  const auto base = index >= 0
                        ? multiViewInput(workflow_[index]) ? QStringLiteral("多视输入") : operatorDefinition(workflow_[index].algorithm).name
                        : QString();
  if (runQueue_.active() || index < 0 || trimmed.size() > 60 ||
      trimmed.contains('\n') || trimmed.contains('\r') ||
      trimmed.contains('\t') ||
      (!trimmed.isEmpty() && trimmed != base &&
       !trimmed.startsWith(base + "-")) ||
      trimmed == base + "-")
    return false;
  const auto canonical = trimmed.isEmpty() ? base : trimmed;
  const auto existing = nodeNames();
  for (int i = 0; i < existing.size(); ++i)
    if (i != index && existing[i] == canonical) {
      setOperatorStatus(QStringLiteral("名称已存在"), "error",
                        QStringLiteral("请使用不同的算子后缀"));
      return false;
    }
  EditGuard edit(*this, QStringLiteral("重命名节点"));
  workflowAliases_[index] = canonical;
  const auto names = nodeNames();
  for (auto& image : images_) {
    auto& output = image.workflowResult;
    output.steps.clear();
    QStringList pipeline;
    Document graph;
    graph.workflow = workflow_;
    graph.workflowInputs = workflowInputs_;
    graph.workflowLinks = workflowLinks_;
    for (int i : workflowOrder(graph, true))
      if (i < output.nodeOutputs.size() &&
          output.nodeOutputs[i].state != NodeState::Pending) {
        output.steps.append({names[i], output.nodeOutputs[i].algorithmMs});
        pipeline.append(names[i]);
      }
    output.pipeline = pipeline.join(QStringLiteral(" → "));
  }
  refreshWorkflow();
  refreshContext();
  setDirty(true);
  return true;
}
void MainWindow::requestNodeRename(int index) {
  if (index < 0 || index >= workflow_.size() || runQueue_.active()) return;
  bool ok = false;
  const auto base = multiViewInput(workflow_[index])?QStringLiteral("多视输入"):operatorDefinition(workflow_[index].algorithm).name;
  const auto current = nodeNames()[index];
  const auto name = QInputDialog::getText(
      this, QStringLiteral("重命名流程节点"),
      QStringLiteral("固定算子名称：%1\n后缀（显示为 %1-后缀，留空恢复原名）")
          .arg(base),
      QLineEdit::Normal,
      current == base ? QString() : current.mid(base.size() + 1), &ok);
  if (ok)
    renameWorkflowNode(
        workflowIds_[index],
        name.trimmed().isEmpty() ? base : base + "-" + name.trimmed());
}
void MainWindow::normalizeModelOwnership() {
  QSet<QString> nodeModels, basicModels;
  for (const auto& range : sharedRanges_) nodeModels.insert(range.modelId);
  for (const auto& range : workbenchRanges_)
    basicModels.insert(range.modelId);
  for (const auto& image : images_) {
    for (const auto& range : image.document.nodeSettings)
      nodeModels.insert(range.modelId);
    for (const auto& range : image.document.workbenchRanges)
      basicModels.insert(range.modelId);
  }
  QHash<QString, QString> copies;
  QVector<ModelResource> separated = models_;
  for (auto& model : separated) {
    if (nodeModels.contains(model.id))
      model.independent = false;
    else if (basicModels.contains(model.id))
      model.independent = true;
  }
  for (const auto& model : models_) {
    if (!basicModels.contains(model.id) || !nodeModels.contains(model.id))
      continue;
    if (separated.size() >= 1024)
      throw std::runtime_error("独立模板资源过多");
    auto copy = model;
    copy.independent = true;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copies.insert(model.id, copy.id);
    separated.append(copy);
  }
  models_ = separated;
  auto rebind = [&](QVector<NodeSettings>& ranges) {
    for (auto& range : ranges)
      if (copies.contains(range.modelId))
        range.modelId = copies[range.modelId];
  };
  rebind(workbenchRanges_);
  for (auto& image : images_) rebind(image.document.workbenchRanges);
  rebind(document_.workbenchRanges);
}
void MainWindow::samplePixel(ImageView* target, int row, int col) {
  if (spatialView_) {
    if (target->pixelSourceAt(row, col) == ImageView::PixelSource::None) {
      target->setPixelSample(row, col, {}, ImageView::PixelSource::None);
      if (auto* legend = workflowMode_ ? spatialLegend_ : spatialWorkbenchLegend_)
        if ((workflowMode_ ? spatialChoice_ : spatialWorkbenchChoice_)->currentData() == "decodeReason") legend->setText(QStringLiteral("图像外部"));
      return;
    }
    auto* choice = workflowMode_ ? spatialChoice_ : spatialWorkbenchChoice_;
    auto it = spatialView_->rasters.constFind(choice->currentData().toString());
    if (it == spatialView_->rasters.end()) return;
    const bool base =
        it->semantic != "intensity" &&
        target->pixelSourceAt(row, col) == ImageView::PixelSource::Original;
    const auto sampled = base ? spatialView_->rasters.constFind(spatialReferenceKey(*spatialView_)) : it;
    if (sampled == spatialView_->rasters.cend() || !sampled->image.CountObj()) {
      target->setPixelSample(row, col, {}, ImageView::PixelSource::None);
      return;
    }
    const auto& image = sampled->image;
    if (row < 0 || col < 0 || row >= image.Height() || col >= image.Width() ||
        !image.GetDomain().TestRegionPoint(row, col)) {
      target->setPixelSample(row, col, {}, ImageView::PixelSource::None);
      return;
    }
    const auto value = pixelText(image, row, col);
    if (value.contains("nan", Qt::CaseInsensitive) ||
        value.contains("inf", Qt::CaseInsensitive))
      target->setPixelSample(row, col, {}, ImageView::PixelSource::None);
    else {
      const auto displayValue = sampled->unit == "m"
                                    ? QString::number(value.toDouble() * 1000, 'g', 8) + " mm"
                                    : sampled->semantic == "residual" ? value + " mm" : value;
      target->setPixelSample(row, col, displayValue,
                             base ? ImageView::PixelSource::Original
                                  : ImageView::PixelSource::Processed,
                             image.Type());
      if (!base && it.key() == "decodeReason") {
        const auto detail = structuredReasonText(value.toInt());
        for (auto* legend : {spatialLegend_, spatialWorkbenchLegend_}) {
          legend->setText(detail); legend->setToolTip(QStringLiteral("原因位掩码 %1：%2").arg(value, detail));
        }
      }
    }
    return;
  }

  if (trainingView() && !viewPinnedElsewhere()) {
    const auto* model = currentModel();
    if (model && model->referenceImage.CountObj())
      target->setPixelSample(
          row, col, pixelText(model->referenceImage, row, col),
          target->pixelSourceAt(row, col), model->referenceImage.Type());
    return;
  }
  const auto source = target->pixelSourceAt(row, col);
  const bool rawCompare = workflowMode_ && target == nodeView_ &&
                          target->compareEnabled() && !compareNodeInput_;
  const auto& raw =
      nodeContext()
          ? source == ImageView::PixelSource::Processed ? nodeProcessedImage_
            : !rawCompare && nodeInputDomain_.contains(QPointF(col, row)) ? nodeInputImage_
                                                           : document_.image
      : source == ImageView::PixelSource::Processed ? result_.outputImage
                                                    : document_.image;
  if (source == ImageView::PixelSource::None || !raw.CountObj())
    target->setPixelSample(row, col, {}, ImageView::PixelSource::None);
  else
    target->setPixelSample(row, col, pixelText(raw, row, col),
        workflowMode_ && target->compareEnabled() && compareNodeInput_ &&
                source == ImageView::PixelSource::Original
            ? ImageView::PixelSource::Input : source, raw.Type());
}
}  // namespace workbench
