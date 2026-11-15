// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/shell/main_window.hpp"

#include <QAction>
#include <QComboBox>
#include <QDataStream>
#include <QDateTime>
#include <QJsonDocument>
#include <QSignalBlocker>
#include <QTabBar>
#include <QUndoCommand>
#include <QUndoStack>
#include <exception>
#include <algorithm>

#include "ui/views/workflow_view.hpp"
namespace workbench {
MainWindow::~MainWindow() {
  // QUndoStack::clear emits index/clean changes. Run it while the document
  // members still exist, before QObject starts destroying child widgets.
  restoringEdit_ = true;
  nodeMoveEdit_.reset();
  if (editHistory_) {
    editHistory_->disconnect(this);
    editHistory_->clear();
  }
}
namespace {
void parameters(QDataStream& out, const Parameters& p) {
  out << int(p.algorithm) << p.minimum << p.maximum << p.opening
      << p.minimumArea << p.sigma << p.edgeLow << p.edgeHigh << p.filter
      << p.mask << p.gray << p.templatePath << p.score << p.matches
      << p.angleStart << p.angleExtent << p.options << p.calibrationName << p.calibrationConfig << p.calibrationArtifact;
}
void ranges(QDataStream& out, const QVector<NodeSettings>& values) {
  out << values.size();
  for (const auto& value : values)
    out << QJsonDocument(nodeSettingsJson(value)).toJson(QJsonDocument::Compact);
}
void configuration(QDataStream& out, const Document& d) {
  out << d.imagePath << d.pixelKey << d.preview.cacheKey() << d.captureId
      << d.rightPath << d.rightPixelKey << d.stereoArtifact
      << d.captureName << d.stereoName << d.structuredPaths << d.structuredKeys << d.structuredModel;
  for (auto it = d.sourceInputs.begin(); it != d.sourceInputs.end(); ++it)
    out << it.key() << it->sequence << it->views << it->paths << it->keys;
  parameters(out, d.parameters);
  ranges(out, d.nodeSettings);
  ranges(out, d.workbenchRanges);
  out << d.rangeOverrides << d.workbenchOverrides;
}
}  // namespace
class StudioEditCommand final : public QUndoCommand {
 public:
  StudioEditCommand(MainWindow& owner,
                    std::shared_ptr<MainWindow::EditState> before,
                    std::shared_ptr<MainWindow::EditState> after,
                    const QString& text, const QString& merge)
      : QUndoCommand(text), owner_(owner), before_(std::move(before)),
        after_(std::move(after)), merge_(merge),
        time_(QDateTime::currentMSecsSinceEpoch()) {}
  int id() const override { return merge_.isEmpty() ? -1 : 1; }
  bool mergeWith(const QUndoCommand* command) override {
    const auto* next = dynamic_cast<const StudioEditCommand*>(command);
    if (!next || merge_ != next->merge_ || next->time_ - time_ > 800 ||
        after_->key != next->before_->key)
      return false;
    after_ = next->after_;
    time_ = next->time_;
    setObsolete(before_->key == after_->key);
    return true;
  }
  void undo() override { owner_.restoreEditState(*before_); }
  void redo() override {
    if (firstRedo_) firstRedo_ = false;  // The edit is already applied.
    else owner_.restoreEditState(*after_);
  }
 private:
  MainWindow& owner_;
  std::shared_ptr<MainWindow::EditState> before_, after_;
  QString merge_;
  qint64 time_;
  bool firstRedo_ = true;
};
MainWindow::EditGuard::EditGuard(MainWindow& owner, const QString& text,
                                const QString& merge)
    : owner_(owner), text_(text), merge_(merge) {
  if (owner_.editDepth_++ == 0 && owner_.editHistory_ && owner_.roiPurpose_ &&
      owner_.matchingTabs_ &&
      !owner_.restoringEdit_)
    before_ = owner_.captureEditState();
}
MainWindow::EditGuard::~EditGuard() {
  --owner_.editDepth_;
  if (!before_ || std::uncaught_exceptions()) return;
  auto after = owner_.captureEditState();
  if (before_->key != after->key)
    owner_.editHistory_->push(new StudioEditCommand(
        owner_, std::move(before_), std::move(after), text_, merge_));
}
QString MainWindow::editMergeKey(const QString& field) const {
  return QString::number(editNavigation_) + ':' + field + ':' +
         images_.value(activeImage_).id + ':' +
         (nodeContext() ? workflowIds_.value(selectedStep_)
                        : QStringLiteral("basic-%1").arg(int(document_.parameters.algorithm)));
}
std::shared_ptr<MainWindow::EditState> MainWindow::captureEditState() const {
  auto s = std::make_shared<EditState>();
  s->document = document_;s->inputKey=activeInputKey_;s->preferredInputs=preferredInputs_;
  s->images = images_;
  s->basicParameters = nodeContext() ? workbenchParameters_ : document_.parameters;
  if (activeImage_ >= 0 && activeImage_ < s->images.size()) {
    s->images[activeImage_].document = document_;
    s->images[activeImage_].document.parameters = s->basicParameters;
  }
  // Keep immutable image/model resources, never execution results or timings.
  for (auto& image : s->images) {
    image.result = {};
    image.workflowResult = {};
    image.batchState = 0;
    image.resultVersion = image.workflowResultVersion = 0;
  }
  s->workflow = workflow_;
  s->ids = workflowIds_;
  s->aliases = workflowAliases_;
  s->sharedRanges = sharedRanges_;
  s->basicRanges = workbenchRanges_;
  s->models = models_;
  s->inputs = workflowInputs_;
  s->links = workflowLinks_;
  s->positions = workflowPositions_;
  if (s->positions.size() != workflow_.size() + 1)
    s->positions = workflowView_->nodePositions();
  s->active = activeImage_;
  s->selected = selectedStep_;
  s->output = workflowOutput_;
  s->debugId = debugNodeId_;
  s->workflowMode = workflowMode_;
  s->role = roiPurpose_->currentIndex();
  s->matchingTab = matchingTabs_->currentIndex();
  QDataStream out(&s->key, QIODevice::WriteOnly);
  out.setVersion(QDataStream::Qt_5_15);
  out << s->ids << s->aliases << s->inputs << s->links << s->positions
      << s->output << s->workflow.size();
  for (const auto& p : s->workflow) parameters(out, p);
  ranges(out, s->sharedRanges);
  ranges(out, s->basicRanges);
  out << s->images.size();
  for (const auto& image : s->images) {
    out << image.id;
    configuration(out, image.document);
  }
  if (s->images.isEmpty()) parameters(out, s->basicParameters);
  out << s->models.size();
  for (const auto& m : s->models)
    out << QJsonDocument(modelSettingsJson(m)).toJson(QJsonDocument::Compact)
        << m.preview.cacheKey();
  return s;
}
void MainWindow::restoreEditState(const EditState& s) {
  const auto currentState = captureEditState();
  // Compare execution configuration, not cached model training metadata or
  // canvas positions. Independent debugging must not invalidate the graph.
  const auto contextKey = [](const EditState& state, const ImageResource& image,
                              bool workflow) {
    QByteArray key;
    QDataStream out(&key, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_5_15);
    const auto& doc = image.document;
    out << doc.imagePath << doc.pixelKey << doc.preview.cacheKey();
    if (workflow) {
      out << state.ids << state.aliases << state.inputs << state.links << state.output;
      for (const auto& p : state.workflow) parameters(out, p);
      ranges(out, state.sharedRanges);
      ranges(out, doc.nodeSettings);
      out << doc.rangeOverrides;
    } else {
      parameters(out, doc.parameters);
      ranges(out, state.basicRanges);
      ranges(out, doc.workbenchRanges);
      out << doc.workbenchOverrides;
    }
    for (const auto& model : state.models) {
      if (model.independent == workflow) continue;
      auto settings = modelSettingsJson(model);
      for (const char* derived : {"artifactKey", "originX", "originY", "buildMs", "generation"})
        settings.remove(derived);
      out << QJsonDocument(settings).toJson(QJsonDocument::Compact) << model.preview.cacheKey();
    }
    return key;
  };
  restoringEdit_ = true;
  stopExecution();
  pendingImage_.clear();
  ++revision_;
  ++editNavigation_;
  if (workflowMode_ != s.workflowMode) setWorkflowMode(s.workflowMode);
  auto restoredImages = s.images;
  for (auto& image : restoredImages) {
    image.version = image.baseVersion = revision_;
    for (int i = 0; i < images_.size(); ++i) {
      const auto& current = images_[i];
      if (current.id != image.id) continue;
      image.checked = current.checked;
      if (contextKey(s, image, true) == contextKey(*currentState, currentState->images[i], true)) {
        image.workflowResult = current.workflowResult;
        image.version = current.version;
        image.workflowResultVersion = current.workflowResultVersion;
      }
      if (contextKey(s, image, false) == contextKey(*currentState, currentState->images[i], false)) {
        image.result = current.result;
        image.baseVersion = current.baseVersion;
        image.resultVersion = current.resultVersion;
      }
    }
  }
  images_ = std::move(restoredImages);
  activeImage_ = s.active;
  workflow_ = s.workflow;
  workflowIds_ = s.ids;
  workflowAliases_ = s.aliases;
  workflowInputs_ = s.inputs;
  workflowLinks_ = s.links;
  workflowPositions_ = s.positions;
  workflowOutput_ = s.output;
  sharedRanges_ = s.sharedRanges;
  workbenchRanges_ = s.basicRanges;
  models_ = s.models;
  workbenchParameters_ = s.basicParameters;
  debugNodeId_ = s.debugId;activeInputKey_=s.inputKey;preferredInputs_=s.preferredInputs;
  selectedStep_ = s.selected;
  document_ = s.document;
  result_ = {};
  if (activeImage_ >= 0 && activeImage_ < images_.size()) {
    const auto& image = images_[activeImage_];
    if (nodeContext() && image.workflowResultVersion == image.version)
      result_ = image.workflowResult;
    else if (!nodeContext() && image.resultVersion == image.baseVersion)
      result_ = image.result;
  }
  lastSingleImage_.clear();
  {
    const QSignalBlocker block(matchingTabs_);
    matchingTabs_->setCurrentIndex(s.matchingTab);
  }
  view_->setImage(document_.preview, true);
  syncParameters();
  syncNodeSettings();
  roiPurpose_->setCurrentIndex(std::min(s.role, roiPurpose_->count() - 1));
  showResult(result_);
  workflowView_->resetExecution();
  refreshWorkflow();
  refreshContext();
  updateRangeView();
  refreshFilm();
  refreshExecutionTiming(true);
  setOperatorStatus(QStringLiteral("待运行"), "empty");
  restoringEdit_ = false;
  updateActions();
  schedulePreview();
}
void MainWindow::resetEditHistory() {
  pinnedViewNode_.clear();
  if (pinView_ && nodeContext()) refreshNodeOutput();
  else if (pinView_) refreshViewIdentity();
  if (editHistory_) editHistory_->clear();
  ++editNavigation_;
}
}  // namespace workbench
