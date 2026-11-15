// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabBar>
#include <QUuid>
#include <QVBoxLayout>
#include <cmath>

#include "ui/shell/main_window.hpp"
#include "ui/widgets/parameter_spin_box.hpp"
#include "ui/widgets/parameter_grid.hpp"
#include "operators/range_model.hpp"
#include "ui/widgets/source_choice.hpp"
#include "ui/views/workflow_view.hpp"
namespace workbench {
namespace {
bool matching(Algorithm a) {
  return a == Algorithm::ShapeMatch || a == Algorithm::NccMatch;
}
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}  // namespace
NodeSettings MainWindow::effectiveRange() const {
  const int i =
      nodeContext() ? selectedStep_ : int(document_.parameters.algorithm);
  if (i < 0) return {};
  if (nodeContext())
    return rangeForNode(i);
  auto settings = document_.workbenchOverrides.value(i)
                      ? document_.workbenchRanges.value(i)
                      : workbenchRanges_.value(i);
  if (settings.modelId.isEmpty() && matching(document_.parameters.algorithm)) {
    QStringList candidates;
    for (const auto &model : models_)
      if (model.independent &&
          model.algorithm == document_.parameters.algorithm)
        candidates.append(model.id);
    if (candidates.size() == 1)
      settings.modelId = candidates.first();
  }
  return settings;
}
NodeSettings& MainWindow::editableRange() {
  const int i =
      nodeContext() ? selectedStep_ : int(document_.parameters.algorithm);
  if (nodeContext()) {
    document_.nodeSettings.resize(workflow_.size());
    document_.rangeOverrides.resize(workflow_.size());
    return document_.rangeOverrides.value(i) ? document_.nodeSettings[i]
                                             : sharedRanges_[i];
  }
  document_.workbenchRanges.resize(toolSlotCount);
  document_.workbenchOverrides.resize(toolSlotCount);
  return document_.workbenchOverrides.value(i) ? document_.workbenchRanges[i]
                                               : workbenchRanges_[i];
}
ModelResource* MainWindow::currentModel() {
  const auto key = effectiveRange().modelId;
  for (auto& m : models_)
    if (m.id == key && m.independent == !nodeContext()) return &m;
  return nullptr;
}
const ModelResource* MainWindow::currentModel() const {
  const auto* model = findModel(models_, effectiveRange().modelId);
  return model && model->independent == !nodeContext() ? model : nullptr;
}
bool MainWindow::trainingView() const {
  return matchingTabs_ && matching(document_.parameters.algorithm) &&
         matchingTabs_->currentIndex() == 1 &&
         (!nodeContext() || selectedStep_ >= 0);
}
void MainWindow::prepareSnapshot(Document& d) const {
  if (nodeContext() && !workflowMode_ &&
      (selectedStep_ < 0 || selectedStep_ >= workflow_.size()))
    throw std::runtime_error("节点已删除，请重新选择流程节点");
  d.workflowLabels = nodeNames();
  d.sharedNodeSettings = sharedRanges_;
  if (d.runWorkflow) d.roi = {};
  d.modelsRequired = true;
  d.models = models_;
  d.calibrationInputs={};
  if(d.runWorkflow)for(int node=0;node<d.workflow.size();++node)if(calibrationTool(d.workflow[node].algorithm)&&d.workflow[node].algorithm!=Algorithm::MonoCalibration) {
    const int source=workflowNodeSettings(d,node).dataA;
    if(source<0 || source>=d.workflow.size() || !sourceTool(d.workflow[source].algorithm))continue;
    const auto binding=d.workflow[source].options["binding"].toString();
    if(d.calibrationInputs.contains(binding))continue;
    QJsonArray samples;
    for(int row=0;row<images_.size();++row)if(images_[row].checked) {
      const auto cell=resolveSourceData(row==activeImage_?document_:images_[row].document,binding);
      if(cell.frames.isEmpty())continue;
      samples.append(QJsonObject{{"sampleId",images_[row].id},{"sequence",cell.sequence},
          {"views",QJsonArray::fromStringList(cell.views)},{"files",QJsonArray::fromStringList(cell.paths)},
          {"keys",QJsonArray::fromStringList(cell.keys)}});
    }
    d.calibrationInputs[binding]=samples;
  }
  d.coordinateFrame = d.imagePath;
  const int a = int(d.parameters.algorithm);
  d.activeRange = d.workbenchOverrides.value(a) ? d.workbenchRanges.value(a)
                                                : workbenchRanges_.value(a);
  if (!d.runWorkflow && !nodeContext())
    d.activeRange = effectiveRange();
  d.applyActiveRange = !d.runWorkflow;
  QStringList ids;
  if (d.runWorkflow) {
    for (int i = 0; i < workflow_.size(); ++i) {
      const auto id = workflowNodeSettings(d, i).modelId;
      ids << id;
      // Keep the candidate set intact: pruning an ambiguous model library to
      // another node's selected model must not silently resolve this node.
      if (id.isEmpty() && matching(workflow_[i].algorithm))
        for (const auto& model : models_)
          if (!model.independent && model.algorithm == workflow_[i].algorithm) ids << model.id;
    }
  } else
    ids << d.activeRange.modelId;
  d.models.clear();
  for (const auto& m : models_)
    if (ids.contains(m.id)) d.models.append(m);
}
void MainWindow::applyTrainedModels(const Result& result) {
  for (const auto& trained : result.trainedModels)
    for (auto& m : models_)
      if (m.id == trained.id) m = trained;
  runQueue_.applyTrainedModels(result.trainedModels);
}
void MainWindow::rangeChanged(bool modelChange) {
  if (syncing_) return;
  const int i =
      nodeContext() ? selectedStep_ : int(document_.parameters.algorithm);
  const bool overridden = nodeContext()
                              ? document_.rangeOverrides.value(i)
                              : document_.workbenchOverrides.value(i);
  if (modelChange) {
    const auto changed = effectiveRange().modelId;
    auto invalidatePose = [&](NodeSettings& range, const Document* image) {
      if (range.poseSource < 0) return;
      const auto source =
          image && image->rangeOverrides.value(range.poseSource)
              ? image->nodeSettings.value(range.poseSource)
              : sharedRanges_.value(range.poseSource);
      if (source.modelId == changed) {
        const auto* m = findModel(models_, changed);
        if (!m || range.referenceModelKey.isEmpty() ||
            range.referenceModelKey != modelBuildKey(*m))
          range.poseReferenceSet = false;
      }
    };
    for (auto& range : sharedRanges_) invalidatePose(range, nullptr);
    for (auto& image : images_)
      for (auto& range : image.document.nodeSettings)
        invalidatePose(range, &image.document);
    for (auto& range : document_.nodeSettings)
      invalidatePose(range, &document_);
  }
  if (modelChange || !overridden)
    for (auto& image : images_) {
      if (nodeContext())
        ++image.version;
      else
        ++image.baseVersion;
    }
  if (!nodeContext())
    document_.roi = effectiveRange().scope == ScopeMode::Manual
                        ? effectiveRange().roi
                        : Roi{};
  invalidate();
  setDirty(true);
  syncNodeSettings();
  syncGeometryUi();
  updateRangeView();
  refreshProperties();
  if (nodeContext()) refreshWorkflow();
}
void MainWindow::setReferenceImage(const QString& path, int resource) {
  EditGuard edit(*this, QStringLiteral("设置模板参考"));
  if (runQueue_.active() || !matching(document_.parameters.algorithm)) return;
  guard(
      QStringLiteral("设置参考图像"),
      [&] {
        auto reference = resource >= 0    ? images_[resource].document
                         : path.isEmpty() ? document_
                                          : loadImage(path);
        if (!reference.hasImage())
          throw std::runtime_error("请先载入参考图像");
        auto* m = currentModel();
        if (!m) {
          ModelResource model;
          model.id = id();
          model.name =
              QStringLiteral("%1模板 %2")
                  .arg(document_.parameters.algorithm == Algorithm::ShapeMatch
                           ? QStringLiteral("轮廓")
                           : QStringLiteral("灰度"))
                  .arg(models_.size() + 1);
          model.algorithm = document_.parameters.algorithm;
          model.independent = !nodeContext();
          models_.append(model);
          editableRange().modelId = model.id;
          m = &models_.last();
        }
        m->referenceImage = reference.image;
        m->artifactOnly = false;
        m->preview = reference.preview;
        m->referencePath = reference.imagePath;
        m->trainingRoi =
            path.isEmpty()
                ? Roi{}
                : Roi{RoiKind::Rectangle, QRectF(0, 0, m->preview.width() - 1,
                                                 m->preview.height() - 1)};
        m->exclusions.clear();
        m->artifact.clear();
        m->artifactKey.clear();
        m->outline = {};
        matchingTabs_->setCurrentIndex(1);
        roiPurpose_->setCurrentIndex(0);
        rangeChanged(true);
      },
      true);
}
bool MainWindow::importTemplate(const QString& path) {
  EditGuard edit(*this, QStringLiteral("加载模板"));
  if (runQueue_.active() || !matching(document_.parameters.algorithm) ||
      (nodeContext() && selectedStep_ < 0))
    return false;
  return guard(
      QStringLiteral("加载模板"),
      [&] {
        auto model = loadTemplateFile(path, document_.parameters.algorithm);
        model.independent = !nodeContext();
        document_.parameters.angleStart = model.angleStart;
        document_.parameters.angleExtent = model.angleExtent;
        if (nodeContext()) workflow_[selectedStep_] = document_.parameters;
        editableRange().modelId = model.id;
        models_.append(std::move(model));
        matchingTabs_->setCurrentIndex(0);
        syncParameters();
        rangeChanged(true);
        log(QStringLiteral("已加载模板：") + path);
      },
      true);
}
bool MainWindow::exportTemplate(const QString& path, bool nativeModel) {
  return guard(
      QStringLiteral("保存模板"),
      [&] {
        const auto* model = currentModel();
        if (!model) throw std::runtime_error("请先选择有效模板");
        saveTemplateFile(*model, path, nativeModel);
        log(QStringLiteral("已保存模板：") + path);
      },
      true);
}
void MainWindow::buildRangeUi(QVBoxLayout* layout, QWidget* parent) {
  matchingTabs_ = new QTabBar(parent);
  matchingTabs_->setObjectName("matchingTabs");
  matchingTabs_->addTab(QStringLiteral("查找"));
  matchingTabs_->addTab(QStringLiteral("模板"));
  layout->addWidget(matchingTabs_);
  connect(matchingTabs_, &QTabBar::currentChanged, this, [this] {
    if (syncing_) return;
    roiPurpose_->setCurrentIndex(0);
    syncNodeSettings();
    updateRangeView();
  });
  modelChoice_ = new QComboBox(parent);
  modelChoice_->setObjectName("modelChoice");
  templateFileActions_ = new QWidget(parent);
  auto* files = new QHBoxLayout(templateFileActions_);
  files->setContentsMargins(0, 0, 0, 0);
  loadTemplate_ =
      new QPushButton(QStringLiteral("加载"), templateFileActions_);
  saveTemplate_ =
      new QPushButton(QStringLiteral("保存"), templateFileActions_);
  loadTemplate_->setToolTip(QStringLiteral("加载模板文件，直接用于查找"));
  saveTemplate_->setToolTip(QStringLiteral("保存当前训练产物为独立模板文件"));
  loadTemplate_->setObjectName("loadTemplateFile");
  saveTemplate_->setObjectName("saveTemplateFile");
  files->addWidget(modelChoice_, 1);
  files->addWidget(loadTemplate_);
  files->addWidget(saveTemplate_);
  layout->addWidget(templateFileActions_);
  connect(loadTemplate_, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getOpenFileName(
        this, QStringLiteral("加载匹配模板"), {},
        QStringLiteral("匹配模板 (*.mvtemplate *.mvmodel);;所有文件 (*)"));
    if (!path.isEmpty()) importTemplate(path);
  });
  connect(saveTemplate_, &QPushButton::clicked, this, [this] {
    QString filter = QStringLiteral("Studio 模板 (*.mvtemplate)");
    auto path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存匹配模板"), {},
        filter + QStringLiteral(";;SDK 模型 (*.mvmodel)"), &filter);
    if (path.isEmpty()) return;
    const auto suffix = QFileInfo(path).suffix().toLower();
    const bool native = suffix == "mvmodel" ||
                        (suffix != "mvtemplate" && filter.startsWith("SDK"));
    if (QFileInfo(path).suffix().isEmpty())
      path += native ? ".mvmodel" : ".mvtemplate";
    exportTemplate(path, native);
  });
  connect(modelChoice_, QOverload<int>::of(&QComboBox::activated), this,
          [this](int row) {
            EditGuard edit(*this, QStringLiteral("选择模板"));
            editableRange().modelId = modelChoice_->itemData(row).toString();
            rangeChanged(true);
          });
  rangePanel_ = new QWidget(parent);
  rangePanel_->setObjectName("rangeBindingPanel");
  auto* form = new RangeForm(rangePanel_);
  form->setContentsMargins(0, 0, 0, 0);
  form->setVerticalSpacing(8);
  form->setSizeConstraint(QLayout::SetMinimumSize);
  nodeInput_ = new SourceChoice(rangePanel_);
  nodeInput_->setObjectName("nodeInputSource");
  form->addRow(QStringLiteral("图像"), nodeInput_);
  connect(nodeInput_, QOverload<int>::of(&QComboBox::activated), this,
          [this](int row) {
            if (nodeInput_->itemData(row).toInt() < 0) selectWorkflowInput(selectedStep_, nodeInput_->itemData(row).toInt());
            else bindNamedInput(selectedStep_, "Image", nodeInput_->itemData(row).toInt(), nodeInput_->itemData(row, Qt::UserRole+1).toString());
          });
  nodeScope_ = new QComboBox(rangePanel_);
  nodeScope_->setObjectName("nodeScope");
  form->addRow(QStringLiteral("作用范围"), nodeScope_);
  nodeScope_->setToolTip(QStringLiteral(
      "沿用输入定义域，或使用手绘范围/上游区域；不会隐式恢复整图"));
  connect(nodeScope_, QOverload<int>::of(&QComboBox::activated), this,
          &MainWindow::changeNodeScope);
  regionInput_ = new SourceChoice(rangePanel_);
  regionInput_->setObjectName("regionInputSource");
  form->addRow(QStringLiteral("区域来源"), regionInput_);
  connect(regionInput_, QOverload<int>::of(&QComboBox::activated), this,
          [this](int row) {
            EditGuard edit(*this, QStringLiteral("选择区域来源"));
            if (nodeContext() && selectedStep_ >= 0 && regionInput_->itemData(row).toInt() >= 0) {
              bindNamedInput(selectedStep_, "Region", regionInput_->itemData(row).toInt(), regionInput_->itemData(row, Qt::UserRole+1).toString()); return;
            }
            editableRange().regionSource =
                regionInput_->itemData(row).toInt();
            rangeChanged();
          });
  positionMode_ = new QComboBox(rangePanel_);
  positionMode_->setObjectName("rangePositionMode");
  positionMode_->addItems(
      {QStringLiteral("固定位置"), QStringLiteral("跟随定位")});
  form->addRow(QStringLiteral("位置"), positionMode_);
  connect(positionMode_, QOverload<int>::of(&QComboBox::activated), this,
          [this](int row) {
            EditGuard edit(*this, QStringLiteral("选择定位来源"));
            auto& s = editableRange();
            s.poseSource = row ? -2 : -1;
            s.inputs.remove("Match");
            s.poseReferenceSet = false;
            rangeChanged();
          });
  poseInput_ = new SourceChoice(rangePanel_);
  poseInput_->setObjectName("rangePoseSource");
  form->addRow(QStringLiteral("定位来源"), poseInput_);
  connect(poseInput_, QOverload<int>::of(&QComboBox::activated), this,
          [this](int row) {
            EditGuard edit(*this, QStringLiteral("选择定位来源"));
            if (nodeContext() && selectedStep_ >= 0 && poseInput_->itemData(row).toInt() >= 0) {
              bindNamedInput(selectedStep_, "Match", poseInput_->itemData(row).toInt(), poseInput_->itemData(row, Qt::UserRole+1).toString()); return;
            }
            auto& s = editableRange();
            s.poseSource = poseInput_->itemData(row).toInt();
            s.poseReferenceSet = false;
            rangeChanged();
          });
  poseIteration_ = new QComboBox(rangePanel_);
  poseIteration_->setObjectName("poseIterationMode");
  poseIteration_->addItems(
      {QStringLiteral("指定实例"), QStringLiteral("逐个匹配目标")});
  form->addRow(QStringLiteral("处理方式"), poseIteration_);
  connect(poseIteration_, QOverload<int>::of(&QComboBox::activated), this,
          [this](int index) {
            if (syncing_ || runQueue_.active()) return;
            EditGuard edit(*this, QStringLiteral("修改定位迭代方式"));
            editableRange().iteratePose = index == 1;
            rangeChanged();
          });
  poseInstance_ = new ParameterSpinBox(rangePanel_);
  poseInstance_->setObjectName("poseInstance");
  poseInstance_->setRange(1, 100);
  form->addRow(QStringLiteral("定位实例"), poseInstance_);
  connect(poseInstance_, QOverload<int>::of(&QSpinBox::valueChanged), this,
          [this](int value) {
            if (!syncing_) {
              EditGuard edit(*this, QStringLiteral("修改定位实例"));
              auto& s = editableRange();
              s.poseInstance = value - 1;
              s.poseReferenceSet = false;
              rangeChanged();
            }
          });
  referencePose_ =
      new QPushButton(QStringLiteral("使用当前定位作为参考"), rangePanel_);
  referencePose_->setObjectName("setReferencePose");
  form->addRow(referencePose_);
  connect(referencePose_, &QPushButton::clicked, this, [this] {
    EditGuard edit(*this, QStringLiteral("设置定位参考"));
    const auto s = effectiveRange();
    const auto& flow = images_[activeImage_].workflowResult;
    if (images_[activeImage_].workflowResultVersion !=
        images_[activeImage_].version)
      return;
    if (s.poseSource < 0 || s.poseSource >= flow.nodeOutputs.size() ||
        s.poseInstance >= flow.nodeOutputs[s.poseSource].objects.size()) {
      setOperatorStatus(QStringLiteral("缺少定位结果"), "error",
                        QStringLiteral("先连接并运行定位节点，再设置参考"));
      return;
    }
    const auto pose = flow.nodeOutputs[s.poseSource].objects[s.poseInstance];
    auto& setting = editableRange();
    setting.referencePosition = pose.center;
    setting.referenceAngle = pose.angle;
    setting.poseReferenceSet = true;
    setting.referenceSize = document_.preview.size();
    if (!s.referencePixelKey.isEmpty() &&
        s.referencePixelKey != document_.pixelKey) {
      setting.roi = {};
      setting.exclusions.clear();
      setting.scope = ScopeMode::Manual;
    }
    setting.referencePixelKey = document_.pixelKey;
    setting.referenceModelKey = flow.nodeOutputs[s.poseSource].modelKey;
    rangeChanged();
  });
  layout->insertWidget(1, rangePanel_);
  rangeOverride_ = new QCheckBox(QStringLiteral("此图独立设置"), parent);
  rangeOverride_->setObjectName("rangeOverride");
  restoreRange_ = new QPushButton(QStringLiteral("恢复节点共用范围"), parent);
  restoreRange_->setObjectName("restoreSharedRange");
  auto* ownership = new QHBoxLayout;
  ownership->addWidget(rangeOverride_);
  ownership->addWidget(restoreRange_, 1);
  layout->addLayout(ownership);
  connect(rangeOverride_, &QCheckBox::toggled, this, [this](bool enabled) {
    if (syncing_) return;
    EditGuard edit(*this, QStringLiteral("切换范围归属"));
    const auto current = effectiveRange();
    const int i =
        nodeContext() ? selectedStep_ : int(document_.parameters.algorithm);
    if (nodeContext()) {
      document_.nodeSettings.resize(workflow_.size());
      document_.rangeOverrides.resize(workflow_.size());
      document_.rangeOverrides[i] = enabled;
      if (enabled) document_.nodeSettings[i] = current;
    } else {
      document_.workbenchRanges.resize(toolSlotCount);
      document_.workbenchOverrides.resize(toolSlotCount);
      document_.workbenchOverrides[i] = enabled;
      if (enabled) document_.workbenchRanges[i] = current;
    }
    rangeChanged();
  });
  connect(restoreRange_, &QPushButton::clicked, this,
          [this] { rangeOverride_->setChecked(false); });
  templatePanel_ = new QWidget(parent);
  templatePanel_->setObjectName("modelTrainingPanel");
  auto* training = new QVBoxLayout(templatePanel_);
  training->setContentsMargins(0, 0, 0, 0);
  training->setSpacing(8);
  training->setAlignment(Qt::AlignTop);
  training->setSizeConstraint(QLayout::SetMinimumSize);
  referenceInfo_ = new QLabel(templatePanel_);
  referenceInfo_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  training->addWidget(referenceInfo_);
  referenceChoice_ = new QComboBox(templatePanel_);
  referenceChoice_->setObjectName("referenceImageChoice");
  training->addWidget(referenceChoice_);
  auto* useCurrent =
      new QPushButton(QStringLiteral("使用所选图像作为参考"), templatePanel_);
  useCurrent->setObjectName("useCurrentReference");
  training->addWidget(useCurrent);
  connect(useCurrent, &QPushButton::clicked, this, [this] {
    int index = referenceChoice_->currentData().toInt();
    if (index >= 0 && index < images_.size()) setReferenceImage({}, index);
  });
  auto* import =
      new QPushButton(QStringLiteral("导入参考图像…"), templatePanel_);
  import->setObjectName("importReference");
  training->addWidget(import);
  connect(import, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入参考图像"), {},
        QStringLiteral("图像 (*.png *.jpg *.bmp *.tif *.tiff)"));
    if (!path.isEmpty()) setReferenceImage(path);
  });
  auto* anglePanel = new ParameterGrid(templatePanel_);
  buildStart_ = new ParameterDoubleSpinBox(anglePanel);
  buildExtent_ = new ParameterDoubleSpinBox(anglePanel);
  buildStart_->setObjectName("modelBuildStart");
  buildExtent_->setObjectName("modelBuildExtent");
  buildStart_->setRange(-180, 180);
  buildExtent_->setRange(0, 360);
  for (auto* box : {buildStart_, buildExtent_}) {
    box->setDecimals(1);
    box->setKeyboardTracking(false);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
  }
  anglePanel->addField(QStringLiteral("训练起始角度 / °"), buildStart_);
  anglePanel->addField(QStringLiteral("训练角度范围 / °"), buildExtent_);
  training->addWidget(anglePanel);
  auto changeBuild = [this] {
    if (syncing_) return;
    EditGuard edit(*this, QStringLiteral("修改模板"));
    if (auto* m = currentModel()) {
      m->angleStart = buildStart_->value();
      m->angleExtent = buildExtent_->value();
      m->artifact.clear();
      m->artifactKey.clear();
      rangeChanged(true);
    }
  };
  connect(buildStart_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, changeBuild);
  connect(buildExtent_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, changeBuild);
  auto* rebuild = new QPushButton(QStringLiteral("重建模型"), templatePanel_);
  rebuild->setObjectName("rebuildModel");
  training->addWidget(rebuild);
  connect(rebuild, &QPushButton::clicked, this, [this] {
    EditGuard edit(*this, QStringLiteral("修改模板"));
    if (auto* m = currentModel()) {
      m->artifact.clear();
      m->artifactKey.clear();
      rangeChanged(true);
    }
  });
  modelStatus_ = new QLabel(templatePanel_);
  modelStatus_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  training->addWidget(modelStatus_);
  layout->addWidget(templatePanel_);
  rangeEditPanel_ = new QWidget(parent);
  auto* editing = new QVBoxLayout(rangeEditPanel_);
  editing->setContentsMargins(0, 0, 0, 0);
  editing->setAlignment(Qt::AlignTop);
  editing->setSizeConstraint(QLayout::SetMinimumSize);
  roiPurpose_ = new QComboBox(rangeEditPanel_);
  roiPurpose_->setObjectName("roiPurpose");
  connect(roiPurpose_, QOverload<int>::of(&QComboBox::activated), this,
          [this] {
            syncNodeSettings();
            updateRangeView();
          });
  auto* buttons = new QHBoxLayout;
  buttons->addWidget(roiPurpose_, 1);
  editRange_ = new QPushButton(QStringLiteral("编辑范围"), rangeEditPanel_);
  clearRange_ = new QPushButton(QStringLiteral("清除"), rangeEditPanel_);
  buttons->addWidget(editRange_);
  buttons->addWidget(clearRange_);
  editing->addLayout(buttons);
  connect(editRange_, &QPushButton::clicked, this, [this] {
    (activeImageView())->setTool(ImageView::Tool::Rectangle);
  });
  connect(clearRange_, &QPushButton::clicked, this,
          [this] { applyDocumentRoi({}); });
  addExclusion_ =
      new QPushButton(QStringLiteral("添加排除范围"), rangeEditPanel_);
  addExclusion_->setObjectName("addExclusion");
  removeExclusion_ =
      new QPushButton(QStringLiteral("移除当前排除范围"), rangeEditPanel_);
  auto* exclusions = new QHBoxLayout;
  exclusions->addWidget(addExclusion_);
  exclusions->addWidget(removeExclusion_);
  editing->addLayout(exclusions);
  connect(addExclusion_, &QPushButton::clicked, this, [this] {
    EditGuard edit(*this, QStringLiteral("修改排除范围"));
    auto* m = trainingView() ? currentModel() : nullptr;
    if (trainingView() && !m) return;
    auto& list = m ? m->exclusions : editableRange().exclusions;
    if (list.size() >= 16) return;
    list.append({});
    if (m) {
      m->artifact.clear();
      m->artifactKey.clear();
    }
    syncNodeSettings();
    roiPurpose_->setCurrentIndex(list.size());
    rangeChanged(m != nullptr);
  });
  connect(removeExclusion_, &QPushButton::clicked, this, [this] {
    int i = roiPurpose_->currentIndex() - 1;
    if (i < 0) return;
    EditGuard edit(*this, QStringLiteral("修改排除范围"));
    auto* m = trainingView() ? currentModel() : nullptr;
    auto& list = m ? m->exclusions : editableRange().exclusions;
    if (i >= list.size()) return;
    list.remove(i);
    if (m) {
      m->artifact.clear();
      m->artifactKey.clear();
    }
    roiPurpose_->setCurrentIndex(0);
    rangeChanged(m != nullptr);
  });
  roiInfo_ = new QLabel(rangeEditPanel_);
  roiInfo_->setObjectName("roiInfo");
  roiInfo_->setWordWrap(true);
  editing->addWidget(roiInfo_);
  auto* geometry = new ParameterGrid(rangeEditPanel_);
  roiAngle_ = new ParameterDoubleSpinBox(geometry);
  roiAngle_->setObjectName("roiAngleDegrees");
  roiAngle_->setRange(-180, 180);
  roiAngle_->setDecimals(2);
  roiAngle_->setKeyboardTracking(false);
  geometry->addField(QStringLiteral("ROI 角度 / °"), roiAngle_);
  roiInner_ = new ParameterDoubleSpinBox(geometry);
  roiInner_->setObjectName("roiInnerRadius");
  roiInner_->setRange(.01, 1e6);
  roiInner_->setDecimals(2);
  roiInner_->setKeyboardTracking(false);
  geometry->addField(QStringLiteral("环形内半径 / px"), roiInner_);
  editing->addWidget(geometry);
  for (auto* field : {roiAngle_, roiInner_})
    connect(field, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, field](double value) {
              if (syncing_ || runQueue_.active() ||
                  !activeImageView()->roiEditingEnabled())
                return;
              auto roi = currentNodeRoi();
              if (field == roiAngle_)
                roi.angle = value;
              else
                roi.innerRatio =
                    std::clamp(value / (roi.bounds.width() / 2), .01, .99);
              applyDocumentRoi(roi);
            });
  layout->addWidget(rangeEditPanel_);
  for (auto* button : parent->findChildren<QPushButton*>())
    button->setMinimumHeight(30);
  for (auto* field : parent->findChildren<QComboBox*>())
    field->setMinimumHeight(30);
  for (auto* field : templatePanel_->findChildren<QDoubleSpinBox*>())
    field->setMinimumHeight(30);
}
Roi MainWindow::currentNodeRoi() const {
  const int role = roiPurpose_ ? roiPurpose_->currentIndex() : 0;
  if (trainingView()) {
    const auto* m = currentModel();
    if (!m) return {};
    return role > 0 ? m->exclusions.value(role - 1) : m->trainingRoi;
  }
  const auto s = effectiveRange();
  return role > 0                       ? s.exclusions.value(role - 1)
         : s.scope == ScopeMode::Manual ? s.roi
                                        : Roi{};
}
void MainWindow::applyDocumentRoi(const Roi& roi) {
  EditGuard edit(*this, QStringLiteral("修改 ROI"));
  if (runQueue_.active() || viewPinnedElsewhere() || (nodeContext() && selectedStep_ < 0)) return;
  const int role = roiPurpose_->currentIndex();
  if (trainingView()) {
    auto* m = currentModel();
    if (!m) return;
    if (role > 0 && role <= m->exclusions.size())
      m->exclusions[role - 1] = roi;
    else if (role == 0)
      m->trainingRoi = roi;
    m->artifact.clear();
    m->artifactKey.clear();
    rangeChanged(true);
  } else {
    auto& s = editableRange();
    if (role > 0 && role <= s.exclusions.size())
      s.exclusions[role - 1] = roi;
    else if (role == 0) {
      s.roi = roi;
      s.scope =
          roi.kind == RoiKind::None ? ScopeMode::Full : ScopeMode::Manual;
      s.regionSource = -1;
    }
    s.referenceSize = document_.preview.size();
    rangeChanged();
  }
}
void MainWindow::changeNodeScope(int row) {
  if (syncing_ || runQueue_.active()) return;
  EditGuard edit(*this, QStringLiteral("修改作用范围"));
  auto& s = editableRange();
  const int value = nodeScope_->itemData(row).toInt();
  s.scope = static_cast<ScopeMode>(value);
  if (s.scope != ScopeMode::Region) s.regionSource = -1;
  if (s.scope == ScopeMode::Manual)
    s.referenceSize = document_.preview.size();
  rangeChanged();
}
void MainWindow::syncNodeSettings() {
  if (!roiPurpose_) return;
  document_.sharedNodeSettings = sharedRanges_;
  document_.models = models_;
  const bool prior = syncing_;
  syncing_ = true;
  const bool valid = document_.hasImage() &&
                     (!nodeContext() ||
                      selectedStep_ >= 0 && selectedStep_ < workflow_.size());
  const bool geometry =
      valid &&
      (sourceTool(document_.parameters.algorithm) || dataTool(document_.parameters.algorithm) ||
       document_.parameters.algorithm == Algorithm::StereoRectify ||
       document_.parameters.algorithm == Algorithm::StructuredDecode) &&
      document_.parameters.algorithm != Algorithm::CloudSelect && nodeContext();
  const bool match = valid && matching(document_.parameters.algorithm);
  const bool train = match && trainingView();
  matchingTabs_->setVisible(match);
  modelChoice_->setVisible(match);
  rangePanel_->setVisible(valid && !train && !geometry);
  templatePanel_->setVisible(train);
  templateFileActions_->setVisible(valid && match);
  loadTemplate_->setEnabled(!runQueue_.active());
  rangeEditPanel_->setVisible(valid && !geometry);
  rangeOverride_->setVisible(valid && !train && !geometry);
  restoreRange_->setVisible(valid && !train && !geometry);
  if (parameterPages_) parameterPages_->setVisible(!train);
  QSignalBlocker a(nodeScope_), b(regionInput_), c(positionMode_),
      d(poseInput_), e(poseInstance_), f(rangeOverride_), g(modelChoice_),
      h(buildStart_), j(buildExtent_), k(roiPurpose_);
  if (!valid) {
    auto* view = activeImageView();
    view->setRoiEditingEnabled(false);
    if (!viewPinnedElsewhere()) {
      view->setRoi({});
      view->setRangeOverlay({});
    }
    syncing_ = prior;
    return;
  }
  const auto s = effectiveRange();
  modelChoice_->clear();
  modelChoice_->addItem(QStringLiteral("模型 · 未选择"), QString());
  for (const auto& m : models_)
    if (m.algorithm == document_.parameters.algorithm &&
        m.independent == !nodeContext())
      modelChoice_->addItem(QStringLiteral("模型 · ") + m.name, m.id);
  showSourceSelection(modelChoice_, s.modelId, QString());
  const auto* model = currentModel();
  saveTemplate_->setEnabled(model && !model->artifact.isEmpty() &&
                            model->artifactKey == modelBuildKey(*model) &&
                            !runQueue_.active());
  {
    QSignalBlocker refBlock(referenceChoice_);
    const int referenceSelection = referenceChoice_->currentData().toInt();
    referenceChoice_->clear();
    for (int i = 0; i < images_.size(); ++i)
      referenceChoice_->addItem(
          QFileInfo(images_[i].document.imagePath).fileName(), i);
    referenceChoice_->setCurrentIndex(
        referenceSelection >= 0 && referenceSelection < images_.size()
            ? referenceSelection
            : activeImage_);
  }
  referenceInfo_->setText(
      model && model->referenceImage.CountObj()
          ? QStringLiteral("参考图像 · %1\n%2 × %3 · 固定工程资源")
                .arg(QFileInfo(model->referencePath).fileName())
                .arg(model->preview.width())
                .arg(model->preview.height())
      : model && model->artifactOnly
          ? QStringLiteral(
                "已加载 SDK 模型 · 可直接查找\n重新训练需先设置参考图像")
          : QStringLiteral("请设置固定的模板参考图像"));
  referenceInfo_->setToolTip(referenceInfo_->text());
  if (model) {
    buildStart_->setValue(model->angleStart);
    buildExtent_->setValue(model->angleExtent);
    modelStatus_->setText(
        model->artifactOnly ? QStringLiteral("文件模板已就绪 · 无需重新训练")
        : model->artifact.isEmpty()
            ? QStringLiteral("模型待训练\n绘制训练范围后自动更新")
            : QStringLiteral("已训练 · %1 ms\n原点 Row · %2\n原点 Col · "
                             "%3\n查找直接复用模型")
                  .arg(model->buildMs, 0, 'f', 2)
                  .arg(model->origin.y(), 0, 'f', 2)
                  .arg(model->origin.x(), 0, 'f', 2));
  } else
    modelStatus_->setText(QStringLiteral("未设置模型"));
  const bool trainable = !runQueue_.active() && model && !model->artifactOnly;
  buildStart_->setEnabled(trainable);
  buildExtent_->setEnabled(trainable);
  templatePanel_->findChild<QPushButton*>("rebuildModel")
      ->setEnabled(trainable);
  if (train && model && model->artifactOnly) rangeEditPanel_->hide();
  const bool override = nodeContext()
                            ? document_.rangeOverrides.value(selectedStep_)
                            : document_.workbenchOverrides.value(
                                  int(document_.parameters.algorithm));
  rangeOverride_->setChecked(override);
  restoreRange_->setEnabled(override && !runQueue_.active());
  restoreRange_->setVisible(override && !train);
  nodeScope_->clear();
  nodeScope_->addItem(QStringLiteral("输入有效范围"), int(ScopeMode::Full));
  nodeScope_->addItem(QStringLiteral("手绘范围"), int(ScopeMode::Manual));
  if (nodeContext())
    nodeScope_->addItem(QStringLiteral("引用区域"), int(ScopeMode::Region));
  nodeScope_->setCurrentIndex(nodeScope_->findData(int(s.scope)));
  regionInput_->clear();
  poseInput_->clear();
  regionInput_->addItem(QStringLiteral("请选择已连通区域"), -2);
  poseInput_->addItem(QStringLiteral("请选择已连通定位"), -2);
  Document graph;
  graph.workflow = workflow_;
  graph.workflowLinks = workflowLinks_;
  graph.workflowInputs = workflowInputs_;
  const auto names = nodeNames();
  if (nodeContext()) {
    for (int source : workflowObjectSources(graph, selectedStep_, "Region"))
      regionInput_->addItem(names[source] + QStringLiteral(" → 区域"), source);
    for (int source : workflowObjectSources(graph, selectedStep_, "Match"))
      poseInput_->addItem(names[source] + QStringLiteral(" → 位姿"), source);
  }
  showSourceSelection(regionInput_, s.regionSource, -2);
  if (nodeContext() && selectedStep_ >= 0) fillNamedInput(regionInput_, selectedStep_, "Region");
  regionInput_->setVisible(s.scope == ScopeMode::Region && !train);
  if (auto* label = qobject_cast<QFormLayout*>(rangePanel_->layout())
                        ->labelForField(regionInput_))
    label->setVisible(regionInput_->isVisible());
  positionMode_->setCurrentIndex(s.poseSource == -1 ? 0 : 1);
  positionMode_->setEnabled(nodeContext() && !runQueue_.active());
  positionMode_->setVisible(nodeContext() && !train);
  if (auto* label = qobject_cast<QFormLayout*>(rangePanel_->layout())
                        ->labelForField(positionMode_))
    label->setVisible(nodeContext() && !train);
  showSourceSelection(poseInput_, s.poseSource, -2);
  if (nodeContext() && selectedStep_ >= 0) fillNamedInput(poseInput_, selectedStep_, "Match");
  const QSignalBlocker iterationBlock(poseIteration_);
  poseIteration_->setCurrentIndex(s.iteratePose ? 1 : 0);
  poseInstance_->setValue(s.poseInstance + 1);
  for (QWidget* field :
       QVector<QWidget*>{static_cast<QWidget*>(poseInput_), poseInstance_,
                         poseIteration_, referencePose_}) {
    field->setVisible(s.poseSource != -1 && !train);
    if (auto* label = qobject_cast<QFormLayout*>(rangePanel_->layout())
                          ->labelForField(field))
      label->setVisible(field->isVisible());
  }
  poseInstance_->setToolTip(
      s.iteratePose
          ? QStringLiteral(
                "建立 ROI 定位参考时使用的目标；执行时处理全部匹配目标")
          : QStringLiteral("当前指定定位实例"));
  if (auto* label = qobject_cast<QLabel*>(
          qobject_cast<QFormLayout*>(rangePanel_->layout())
              ->labelForField(poseInstance_)))
    label->setText(s.iteratePose ? QStringLiteral("参考目标")
                                 : QStringLiteral("定位实例"));
  poseInstance_->setVisible(s.poseSource != -1 && !train);
  if (auto* label = qobject_cast<QFormLayout*>(rangePanel_->layout())
                        ->labelForField(poseInstance_))
    label->setVisible(poseInstance_->isVisible());
  int role = roiPurpose_->currentIndex();
  roiPurpose_->clear();
  roiPurpose_->addItem(train   ? QStringLiteral("训练范围")
                       : match ? QStringLiteral("搜索范围")
                               : QStringLiteral("处理范围"));
  const auto exclusions = train && model ? model->exclusions : s.exclusions;
  for (int i = 0; i < exclusions.size(); ++i)
    roiPurpose_->addItem(QStringLiteral("排除范围 %1").arg(i + 1));
  roiPurpose_->setCurrentIndex(
      std::max(0, std::min(role, roiPurpose_->count() - 1)));
  roiPurpose_->setVisible(roiPurpose_->count() > 1);
  bool editable =
      !geometry && !runQueue_.active() &&
      (train ? model != nullptr
             : roiPurpose_->currentIndex() > 0 || s.scope != ScopeMode::Region);
  if (!train && s.poseSource != -1 && s.poseReferenceSet &&
      activeImage_ >= 0 &&
      (s.referencePixelKey.isEmpty() ||
       s.referencePixelKey != document_.pixelKey)) {
    const auto& outputs = images_[activeImage_].workflowResult.nodeOutputs;
    if (s.poseSource >= 0 && s.poseSource < outputs.size() &&
        s.poseInstance < outputs[s.poseSource].objects.size()) {
      const auto& pose = outputs[s.poseSource].objects[s.poseInstance];
      editable = editable &&
                 QLineF(pose.center, s.referencePosition).length() < .5 &&
                 std::abs(pose.angle - s.referenceAngle) < .1;
    } else
      editable = false;
  }
  bool freshPose = false;
  if (nodeContext() && activeImage_ >= 0) {
    const auto& image = images_[activeImage_];
    freshPose =
        image.workflowResultVersion == image.version && s.poseSource >= 0 &&
        s.poseSource < image.workflowResult.nodeOutputs.size() &&
        image.workflowResult.nodeOutputs[s.poseSource].state ==
            NodeState::Success &&
        s.poseInstance <
            image.workflowResult.nodeOutputs[s.poseSource].objects.size();
  }
  referencePose_->setEnabled(freshPose && !runQueue_.active());
  auto* view = activeImageView();
  if (!viewPinnedElsewhere())
    view->setRoi(!editable && !train && s.poseSource != -1 ? Roi{} : currentNodeRoi());
  editable = editable && !viewPinnedElsewhere();
  view->setRoiEditingEnabled(editable);
  editRange_->setEnabled(editable);
  clearRange_->setEnabled(editable);
  addExclusion_->setEnabled(!runQueue_.active() && (!train || model) &&
                            exclusions.size() < 16);
  removeExclusion_->setEnabled(editable && roiPurpose_->currentIndex() > 0);
  removeExclusion_->setVisible(roiPurpose_->currentIndex() > 0);
  for (QWidget* field :
       QVector<QWidget*>{static_cast<QWidget*>(rangePanel_), templatePanel_,
                         modelChoice_, rangeOverride_, roiPurpose_})
    field->setEnabled(!runQueue_.active());
  QString detail = currentNodeRoi().kind == RoiKind::None
                       ? QStringLiteral("未绘制")
                       : roiText(currentNodeRoi());
  if (!train && roiPurpose_->currentIndex() == 0) {
    if (s.scope == ScopeMode::Full)
      detail = QStringLiteral("输入有效范围");
    else if (s.scope == ScopeMode::Region)
      detail = QStringLiteral("引用区域 · 只读");
  }
  if (roiAngle_) {
    const auto roi = currentNodeRoi();
    const QSignalBlocker aa(roiAngle_), bb(roiInner_);
    roiAngle_->parentWidget()->setVisible(roi.kind ==
                                          RoiKind::RotatedRectangle);
    roiInner_->parentWidget()->setVisible(roi.kind == RoiKind::Annulus);
    roiAngle_->setValue(roi.angle);
    roiInner_->setValue(roi.bounds.width() / 2 * roi.innerRatio);
    roiAngle_->setEnabled(editable);
    roiInner_->setEnabled(editable);
  }
  roiInfo_->setText(
      (override && !train ? QStringLiteral("此图覆盖 · ") : QString()) +
      roiPurpose_->currentText() + QStringLiteral(" · ") + detail +
      (viewPinnedElsewhere() ? QStringLiteral("\n视图已固定其他节点；解除固定或选回该节点后编辑 ROI")
       : !editable ? QStringLiteral("\n请在参考位置编辑；引用区域不可编辑")
                 : QString()));
  if (auto* label = qobject_cast<QLabel*>(
          qobject_cast<QFormLayout*>(rangePanel_->layout())
              ->labelForField(nodeInput_)))
    label->setText(document_.parameters.algorithm == Algorithm::CloudSelect
                       ? QStringLiteral("像素参考")
                       : QStringLiteral("图像"));
  if (document_.parameters.algorithm == Algorithm::CloudSelect) {
    QSignalBlocker block(nodeInput_);
    nodeInput_->clear();
    nodeInput_->addItem(document_.structuredFrames.isEmpty()
        ? QStringLiteral("由 A 输入提供 · 校正左图像素")
        : QStringLiteral("由 A 输入提供 · 结构光相机像素"), -1);
    nodeInput_->setEnabled(false);
    positionMode_->setEnabled(false);
    int region = nodeScope_->findData(int(ScopeMode::Region));
    if (region >= 0) nodeScope_->removeItem(region);
  }
  static_cast<RangeForm*>(rangePanel_->layout())->refreshRows();
  syncing_ = prior;
  refreshRangeContext();
}
void MainWindow::updateRangeView() {
  if (viewPinnedElsewhere()) {
    refreshNodeOutput();
    return;
  }
  auto* view = activeImageView();
  if (trainingView()) {
    const auto* m = currentModel();
    const QImage image = m ? m->preview : QImage{};
    const bool preserve =
        view->property("trainingModel").toString() == (m ? m->id : QString());
    view->setProperty("trainingModel", m ? m->id : QString());
    view->setImage(image, preserve);
    view->setRangeOverlay({});
    view->setResults({});
    view->setResultImage({});
    view->setRoi(currentNodeRoi());
    if (m && m->trainingRoi.kind != RoiKind::None) {
      try {
        view->setRangeOverlay(imageDomainPath(
            m->referenceImage.ReduceDomain(trainingDomain(*m))));
      } catch (...) {
      }
    }
    view->setImageName(m ? QFileInfo(m->referencePath).fileName() +
                               QStringLiteral(" · 参考")
                         : QString());
    if (nodeContext()) {
      nodeInputImage_ = m ? m->referenceImage : mv::MvImage{};
      nodeProcessedImage_ = {};
      nodeInputDomain_ = m && m->referenceImage.CountObj()
                             ? imageDomainPath(m->referenceImage)
                             : QPainterPath{};
    }
  } else if (nodeContext())
    refreshNodeOutput();
  else {
    view->setRangeOverlay({});
    view->setImage(document_.preview, true);
    view->setImageName(QFileInfo(document_.imagePath).fileName());
    view->setRoi(currentNodeRoi());
    view->setResults(result_.objects);
    view->setResultImage(result_.preview,
                         imageDomainPath(result_.outputImage));
  }
  syncViewTools();
}
}  // namespace workbench
