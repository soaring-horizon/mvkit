// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/shell/main_window.hpp"
#include <algorithm>
#include <iterator>
#include "io/project_io.hpp"
#include "io/input_migration.hpp"
#include "model/project_state.hpp"
#include "ui/dialogs/sample_projects.hpp"
#include "model/metadata_limits.hpp"
#include "operators/operator_registry.hpp"
#include "operators/range_model.hpp"
#include "ui/views/workflow_view.hpp"
#include <QAction>
#include <QComboBox>
#include <QFileInfo>
#include <QJsonArray>
#include <QLabel>
#include <QSet>
#include <QTabBar>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace workbench {
namespace {
QString imageId(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
void requireAvailable(const QJsonObject& object,const QString& path) {
  const auto missing=missingProjectResources(object,path);
  if(!missing.isEmpty())throw std::runtime_error((QStringLiteral("缺失资源，请恢复原文件路径后重试：\n")+missing.join('\n')).toUtf8().constData());
}
}
bool MainWindow::openPath(const QString& path,bool project,bool promoteLegacyInputs) {
  if(loading_||execution_.busy()||runQueue_.active())return false;
  autoPreview_->stop();
  return guard(QStringLiteral("打开"),[&]{
    if(!project) {
      Document document;loadResources(QStringLiteral("读取图像及校验像素…"),[&document,path](const std::atomic_bool&){document=loadImage(path);});
      installDocument(std::move(document),{});
    } else {
      QJsonObject object;
      loadResources(QStringLiteral("读取工程配置及检查资源…"),[&object,path,promoteLegacyInputs](const std::atomic_bool&){object=readProjectConfiguration(path);requireAvailable(object,path);if(promoteLegacyInputs)object=explicitTutorialInputs(object,path);});
      installProjectConfiguration(object,path);
    }
    resetEditHistory();log(QStringLiteral("已打开：")+path);
  });
}
void MainWindow::installProjectConfiguration(const QJsonObject& object,const QString& path) {
  if (object["format"] == "mvkit-workbench")
    installDocument(documentFromJson(object, path),
                    QFileInfo(path).absoluteFilePath());
  else {
    const auto version = object.value("version");
    if (object["format"] != "mvkit-studio" || !version.isDouble() ||
        version.toDouble() != version.toInt() || version.toInt() < 1 || version.toInt() > studioProjectVersion)
      throw std::runtime_error("不支持的工程格式或版本");
    auto entries = object["images"].toArray();
    if (entries.isEmpty() && object["version"].toInt() < 15)
      throw std::runtime_error("工程图像数量无效");
    QVector<ImageResource> loaded;
    loadResources(QStringLiteral("读取工程样本及校验来源…"),[&loaded,entries,path,draft=object["version"].toInt()>=15](const std::atomic_bool& cancelled){
      for(const auto& value:entries) {
        if(cancelled.load())return;
        if(!value.isObject())throw std::runtime_error("图像资源无效");
        const auto entry=value.toObject();auto document=documentFromJson(entry,path,draft);
        const auto expected=entry["imagePixelSha256"].toString();
        if(!expected.isEmpty()&&expected!=document.pixelKey)throw std::runtime_error("参考图像像素已改变，请重新绑定样本");
        const auto id=entry["resourceId"].toString();
        if(id.size()>128)throw std::runtime_error("样本身份无效");
        for(const auto& existing:loaded)if(!id.isEmpty()&&existing.id==id)throw std::runtime_error("样本身份重复");
        if(entry.contains("runSelected")&&!entry["runSelected"].isBool())throw std::runtime_error("运行选择标识无效");
        loaded.append({id.isEmpty()?imageId():id,std::move(document),{},0,entry["runSelected"].toBool(true)});
      }
    });
    const auto basicParameters=object["version"].toInt()>=15
      ? parametersFromJson(object["basicParameters"].toObject(),path,true) : Parameters{};
    QVector<Parameters> flow;
    for (const auto& value : object["workflow"].toArray()) {
      flow.append(object["version"].toInt() >= 15
                      ? parametersFromJson(value.toObject()["parameters"].toObject(), path, true)
                      : documentFromJson(value.toObject(), path).parameters);
      if (flow.size() > 32) throw std::runtime_error("流程节点过多");
    }
    QStringList ids, aliases;
    const bool modernIdentity = object["version"].toInt() >= 4;
    const auto storedIds = object["nodeIds"].toArray();
    const auto storedAliases = object["nodeAliases"].toArray();
    if (modernIdentity && (storedIds.size() != flow.size() ||
                           storedAliases.size() != flow.size()))
      throw std::runtime_error("节点身份数量无效");
    for (int i = 0; i < flow.size(); ++i) {
      const auto id =
          modernIdentity ? storedIds[i].toString() : imageId();
      auto alias = modernIdentity
                       ? storedAliases[i].toString().trimmed()
                       : QString();
      // Normalize only the old generated primary-image suffix. Preserve
      // custom names, other bindings, duplicate-name checks and identities.
      const auto imageName=operatorDefinition(Algorithm::ImageInput).name;
      if(flow[i].algorithm==Algorithm::ImageInput &&
         flow[i].options.value("binding").toString()=="image" &&
         alias.compare(imageName+"-image",Qt::CaseInsensitive)==0 &&
         !object["nodeAliases"].toArray().contains(imageName))alias=imageName;
      if (flow[i].algorithm == Algorithm::SequenceInput) {
        const auto previousPrefix = QStringLiteral("图像序列输入");
        if (alias == previousPrefix || alias.startsWith(previousPrefix + "-"))
          alias.replace(0, previousPrefix.size(), operatorDefinition(flow[i].algorithm).name);
      }
      for (const auto& old : operatorAliases(flow[i].algorithm)) {
        const auto base = operatorDefinition(flow[i].algorithm).name;
        if (alias == old || alias.startsWith(old + "-")) alias = base + alias.mid(old.size());
      }
      // Legacy v4 names can already occupy 60 characters. Adding the
      // canonical operator prefix must not make our own v5 save
      // unreadable.
      const int nameLimit = object["version"].toInt() >= 5 ? 80 : 60;
      if (id.isEmpty() || id.size() > 128 || ids.contains(id) ||
          alias.size() > nameLimit || alias.contains('\n') ||
          alias.contains('\r') || alias.contains('\t') ||
          (object["version"].toInt() >= 5 &&
           alias == operatorDefinition(flow[i].algorithm).name + "-"))
        throw std::runtime_error("节点身份或名称无效");
      ids.append(id);
      aliases.append(alias);
    }
    if (object["version"].toInt() >= 5 &&
        workflowNodeNames(flow, aliases) != aliases)
      throw std::runtime_error("节点名称必须保留算子前缀且不可重复");
    Document graph;
    graph.workflow = flow;
    if (object.contains("workflowInputs")) {
      const auto array = object["workflowInputs"].toArray();
      if (array.size() != flow.size())
        throw std::runtime_error("节点输入数量无效");
      for (const auto& value : array) {
        if (!value.isDouble() || value.toDouble() != value.toInt(-99))
          throw std::runtime_error("节点输入无效");
        graph.workflowInputs.append(value.toInt());
      }
    }
    auto sources = workflowSources(graph);
    graph.workflowInputs = sources;
    if (object.contains("workflowLinks")) {
      const auto array = object["workflowLinks"].toArray();
      if (array.size() != flow.size())
        throw std::runtime_error("节点连线数量无效");
      for (const auto& value : array) {
        if (!value.isArray() || value.toArray().size() > 33)
          throw std::runtime_error("节点连线无效");
        QVector<int> dependencies;
        for (const auto& source : value.toArray()) {
          if (!source.isDouble() ||
              source.toDouble() != source.toInt(-99) ||
              dependencies.contains(source.toInt()))
            throw std::runtime_error("节点连线无效");
          dependencies.append(source.toInt());
        }
        graph.workflowLinks.append(dependencies);
      }
    }
    const auto links = workflowConnections(graph);
    graph.workflowLinks = links;
    workflowOrder(graph, true);
    const int outputNode = object["version"] == 1
                               ? (flow.isEmpty() ? -2 : flow.size() - 1)
                               : object["workflowOutput"].toInt(-99);
    if (outputNode < -2 || outputNode == -1 || outputNode >= flow.size())
      throw std::runtime_error("流程输出节点无效");
    for (auto& image : loaded) {
      auto& doc = image.document;
      if (doc.nodeSettings.isEmpty()) {
        doc.workflow = flow;
        QVector<NodeSettings> migrated;
        for (int i = 0; i < flow.size(); ++i)
          migrated.append(workflowNodeSettings(doc, i));
        doc.nodeSettings = migrated;
        doc.workflow.clear();
      }
      if (doc.nodeSettings.size() != flow.size())
        throw std::runtime_error("图像的节点范围数量无效");
      for (int i = 0; i < flow.size(); ++i) {
        const auto& s = doc.nodeSettings[i];
        validateRoi(s.roi, doc.preview.width(), doc.preview.height());
        validateRoi(s.templateRoi, doc.preview.width(),
                    doc.preview.height());
        if (s.scope == ScopeMode::Region && s.regionSource >= 0 &&
            !workflowObjectSources(graph, i, "Region")
                 .contains(s.regionSource))
          throw std::runtime_error("区域输入必须是连线可达的上游区域");
      }
    }
    QVector<QPointF> positions;
    for (const auto& value : object["workflowPositions"].toArray()) {
      const auto a = value.toArray();
      if (a.size() != 2 || !a[0].isDouble() || !a[1].isDouble() ||
          !std::isfinite(a[0].toDouble()) ||
          !std::isfinite(a[1].toDouble()) ||
          std::abs(a[0].toDouble()) > 1e6 ||
          std::abs(a[1].toDouble()) > 1e6)
        throw std::runtime_error("节点位置无效");
      positions.append(QPointF(a[0].toDouble(), a[1].toDouble()));
    }
    if (!positions.isEmpty() && positions.size() != flow.size() + 1)
      throw std::runtime_error("节点位置数量无效");
    const int index = object["active"].toInt(0);
    if (loaded.isEmpty() ? index != -1 : (index < 0 || index >= loaded.size()))
      throw std::runtime_error("工程活动图像无效");
    if (object["version"].toInt() >= 3) {
      const auto angleConvention = object["angleConvention"].toString();
      if (!angleConvention.isEmpty() &&
          angleConvention != "image-clockwise")
        throw std::runtime_error("不支持的位姿角度约定");
      const bool migrateAngles = angleConvention.isEmpty();
      QVector<NodeSettings> common, workbench;
      for (const auto& v : object["sharedRanges"].toArray())
        common.append(nodeSettingsFromJson(v.toObject()));
      for (const auto& v : object["workbenchRanges"].toArray())
        workbench.append(nodeSettingsFromJson(v.toObject()));
      const auto supportedWorkbenchSize = [](int size) {
        constexpr int counts[]{6, 11, 13, 19, 26, 31, 32, 33, 36, 38, 41, 44, toolSlotCount};
        return std::find(std::begin(counts), std::end(counts), size) != std::end(counts);
      };
      if (common.size() != flow.size() || !supportedWorkbenchSize(workbench.size()))
        throw std::runtime_error("公共范围数量无效");
      QVector<ModelResource> models;
      loadResources(QStringLiteral("读取冻结模型及标定资源…"),[&models,object,path](const std::atomic_bool&){models=loadModels(object["models"].toArray(),path);});
      for (const auto& resource : loaded)
        if (resource.document.rangeOverrides.size() != flow.size() ||
            !supportedWorkbenchSize(resource.document.workbenchOverrides.size()) ||
            !supportedWorkbenchSize(resource.document.workbenchRanges.size()))
          throw std::runtime_error("逐图覆盖数量无效");
      if (migrateAngles) {
        auto migrate = [](NodeSettings& setting) {
          if (setting.poseReferenceSet)
            setting.referenceAngle = -setting.referenceAngle;
        };
        for (auto& setting : common) migrate(setting);
        for (auto& setting : workbench) migrate(setting);
        for (auto& image : loaded) {
          for (auto& setting : image.document.nodeSettings)
            migrate(setting);
          for (auto& setting : image.document.workbenchRanges)
            migrate(setting);
        }
      }
      sharedRanges_ = common;
      workbench.resize(toolSlotCount);
      workbenchRanges_ = workbench;
      models_ = models;
    } else
      migrateRanges(loaded, flow, index, path);
    images_ = std::move(loaded);
    activeImage_ = -1;
    document_ = {};
    if (object["version"].toInt() >= 15)workbenchParameters_=basicParameters;
    document_.parameters = workbenchParameters_;
    result_ = {};
    projectPath_ = QFileInfo(path).absoluteFilePath();
    copyOriginalDataOnSave_ = false;
    workflow_ = flow;
    workflowIds_ = ids;
    workflowAliases_ = workflowNodeNames(flow, aliases);
    preferredInputs_.clear();const auto tracks=object["inputTracks"].toObject();
    if(tracks.size()>32)throw std::runtime_error("输入轨视图记录过多");
    for(auto it=tracks.begin();it!=tracks.end();++it){if(!it.value().isString()||it.value().toString().size()>128)throw std::runtime_error("输入轨视图记录无效");if(ids.contains(it.key()))preferredInputs_[it.key()]=it.value().toString();}
    activeInputKey_=object["activeInput"].toString();
    if(activeInputKey_.size()>128)throw std::runtime_error("输入轨标识无效");
    debugNodeId_ =
        modernIdentity ? object["debugNodeId"].toString() : QString();
    if (debugNodeId_.size() > 128) debugNodeId_.clear();
    workflowInputs_ = sources;
    workflowLinks_ = links;
    workflowPositions_ = positions;
    workflowOutput_ = outputNode;
    normalizeModelOwnership();
    workflowView_->setSteps({}, -1);
    selectedStep_ = object["version"].toInt()>=3 ? std::clamp(object["selectedNode"].toInt(0),0,std::max(0,workflow_.size()-1)) : std::min(selectedStep_,workflow_.size()-1);
    if (index >= 0) activateImage(index);
    else {
      view_->setImage({});
      nodeView_->setImage({});
      showResult({});
      refreshFilm();
    }
    if (object["version"].toInt() >= 3) {
      selectedStep_ = std::clamp(object["selectedNode"].toInt(0), 0,
                                 std::max(0, workflow_.size() - 1));
      if (object["mode"] == "workbench" && !debugNodeId_.isEmpty()) {
        selectedStep_ = workflowIds_.indexOf(debugNodeId_);
        if (selectedStep_ < 0) selectedStep_ = -3;
      }
      matchingTabs_->setCurrentIndex(0);
      if (object["mode"] == "workflow" && !workflowMode_)
        setWorkflowMode(true);
      else if (object["mode"] == "workbench" && workflowMode_)
        setWorkflowMode(false);
      if (nodeContext() && selectedStep_ >= 0 &&
          selectedStep_ < workflow_.size()) {
        document_.parameters = workflow_[selectedStep_];
        syncParameters();
      }
    }
    syncParameters();
    refreshWorkflow();
    refreshProperties();
    updateActions();
    setDirty(false);
    rememberProject(projectPath_);
  }
  pendingOperator_.clear();
  refreshExecutionTiming(true);
  setOperatorStatus(QStringLiteral("就绪"),"idle");
  setResultState(QStringLiteral("尚未运行"),"idle");
  taskStatus_->setText(QStringLiteral("就绪"));
}
QJsonObject MainWindow::projectConfiguration(const QString& path) {
  storeActive();ProjectState state;
  for(const auto& resource:images_)state.samples.append({resource.id,resource.document,resource.checked});
  state.workflow=workflow_;state.nodeIds=workflowIds_;state.nodeNames=workflowAliases_;
  state.inputs=workflowInputs_;state.links=workflowLinks_;state.output=workflowOutput_;
  state.sharedRanges=sharedRanges_;state.basicRanges=workbenchRanges_;state.models=models_;
  state.basicParameters=nodeContext()?workbenchParameters_:document_.parameters;
  state.view={workflowMode_?QString("workflow"):QString("workbench"),debugNodeId_,activeImage_,selectedStep_,workflowPositions_};
  state.view.inputKey=activeInputKey_;
  for(const auto& id:workflowIds_)if(preferredInputs_.contains(id))state.view.preferredInputs[id]=preferredInputs_[id];
  return projectStateJson(state,path);
}
bool MainWindow::saveTo(const QString& path, bool copyOriginalData) {
  return guard(QStringLiteral("保存工程"), [&] {
    if(isBundledSamplePath(path))throw std::runtime_error("内置示例保留原样，请将工程另存到其他位置");
    const auto object = projectConfiguration(path);
    const bool copy = copyOriginalData;
    if (copy) writeProjectWithOriginalData(object, path);
    else writeProjectConfiguration(object, path);
    projectPath_ = QFileInfo(path).absoluteFilePath();
    copyOriginalDataOnSave_ = copy;
    setDirty(false);
    rememberProject(projectPath_);
    log(QStringLiteral("工程已保存：") + path);
  });
}
bool MainWindow::openRelocatedProject(const QString& path,const QString& directory) {
  if(busy())return false;
  return guard(QStringLiteral("恢复工程资源"),[&]{
    QJsonObject recovered;
    loadResources(QStringLiteral("按名称及内容指纹查找缺失资源…"),[&recovered,path,directory](const std::atomic_bool& cancelled){
      recovered=relocateProjectResources(readProjectConfiguration(path),path,directory,&cancelled);
      if(!cancelled.load())requireAvailable(recovered,path);
    });
    installProjectConfiguration(recovered,path);resetEditHistory();setDirty(true);
    log(QStringLiteral("资源引用已在内存中恢复；保存工程后才写入新路径。"));
  });
}
} // namespace workbench
