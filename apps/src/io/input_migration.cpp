// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "io/input_migration.hpp"
#include "model/document.hpp"
#include "model/workflow_ports.hpp"
#include "model/spatial_data.hpp"
#include "operators/operator_registry.hpp"
#include <QJsonArray>
#include <QUuid>
#include <QSet>
#include <stdexcept>
namespace workbench {
QJsonObject namedWorkflowInputs(const QJsonObject& original, const QString& path) {
  if (original["format"] != "mvkit-studio" || original["version"].toInt() < 17 ||
      original["version"].toInt() >= studioProjectVersion) return original;
  const auto flow = original["workflow"].toArray();
  QVector<Parameters> parameters;
  for (const auto& node : flow) {
    const auto p = parametersFromJson(node.toObject()["parameters"].toObject(), path, true);
    parameters.append(p);
  }
  if (flow.size() > 32) throw std::runtime_error("流程节点过多");
  QVector<QVector<int>> links;
  for (const auto& value : original["workflowLinks"].toArray()) {
    QVector<int> row; for (const auto& v : value.toArray()) row.append(v.toInt(-2)); links.append(row);
  }
  if (links.size() != flow.size()) return original; // Existing loader reports malformed graphs.
  QVector<NodeSettings> common;
  for (const auto& value : original["sharedRanges"].toArray()) common.append(nodeSettingsFromJson(value.toObject()));
  if (common.size() != flow.size()) return original;
  QMap<int, OutputBinding> extracted;
  QMap<int, int> extractionParents;
  QVector<int> indices(flow.size(), -2);
  int count = 0;
  for (int i = 0; i < flow.size(); ++i) {
    common[i] = resolveNodeInputs(parameters, links, i, common[i]);
    const auto a = parameters[i].algorithm;
    if (a == Algorithm::StereoImage || a == Algorithm::StructuredImage) {
      int source = common[i].dataA;
      if (source < 0 || source >= flow.size() || !workflowAncestors(links, i).contains(source))
        throw std::runtime_error("旧提取节点输入失效，不能迁移具名输出");
      extractionParents[i] = source;
      const QString key = a == Algorithm::StereoImage ? (parameters[i].options["component"].toInt() ? "right" : "left") : structuredImageKey(parameters[i]);
      QSet<int> visited;
      bool declares = false;
      while (source >= 0 && source < parameters.size() && !visited.contains(source)) {
        visited.insert(source);
        for (const auto& port : outputPorts(parameters[source])) declares |= port.key == key;
        if (declares) break;
        source = resolveNodeInputs(parameters, links, source, common[source]).dataA;
      }
      if (!declares) throw std::runtime_error("旧提取分量没有可迁移的原始输出");
      extracted[i] = {source, key};
    } else indices[i] = count++;
  }
  auto remap = [&](int node) {
    if (extracted.contains(node)) node = extracted[node].node;
    return node < 0 ? node : node < indices.size() ? indices[node] : -2;
  };
  auto settings = [&](NodeSettings s, int i) {
    s = resolveNodeInputs(parameters, links, i, s);
    const int source = original["workflowInputs"].toArray()[i].toInt(-2);
    if (extracted.contains(source) && !typedInputTool(parameters[i].algorithm)) setInputBinding(s, "Image", extracted[source]);
    for (const auto& port : inputPorts(parameters[i])) {
      auto ref = inputBinding(s, port.key);
      if (extracted.contains(ref.node)) setInputBinding(s, port.key, extracted[ref.node]);
    }
    s.dataA = remap(s.dataA); s.dataB = remap(s.dataB); s.calibrationSource = remap(s.calibrationSource);
    s.regionSource = remap(s.regionSource); s.poseSource = remap(s.poseSource);
    remapInputBindings(s, remap);
    return nodeSettingsJson(s);
  };
  auto result = original;
  QJsonArray newFlow, newInputs, newLinks, ranges;
  const auto sources = original["workflowInputs"].toArray();
  for (int i = 0; i < flow.size(); ++i) if (!extracted.contains(i)) {
    newFlow.append(flow[i]); newInputs.append(remap(sources[i].toInt(-2)));
    QJsonArray row; for (int node : links[i]) {
      const int mapped = remap(extractionParents.value(node, node));
      if (!row.contains(mapped)) row.append(mapped);
    }
    newLinks.append(row); ranges.append(settings(common[i], i));
  }
  result["workflow"] = newFlow; result["workflowInputs"] = newInputs; result["workflowLinks"] = newLinks;
  result["sharedRanges"] = ranges;
  for (const auto& key : {"nodeIds", "nodeAliases", "workflowPositions"}) {
    const auto source = original[key].toArray(); QJsonArray kept;
    const int offset = QString(key) == "workflowPositions" ? 1 : 0;
    if (offset && !source.isEmpty()) kept.append(source[0]);
    for (int i = 0; i < flow.size(); ++i) if (!extracted.contains(i) && i+offset < source.size()) kept.append(source[i+offset]);
    result[key] = kept;
  }
  for (const auto& key : {"selectedNode", "workflowOutput"}) result[key] = remap(original[key].toInt(-1));
  const auto ids = original["nodeIds"].toArray();
  for (auto it = extracted.begin(); it != extracted.end(); ++it)
    if (original["debugNodeId"] == ids[it.key()]) result["debugNodeId"] = ids[it->node];
  auto images = original["images"].toArray();
  for (int row = 0; row < images.size(); ++row) {
    auto image = images[row].toObject();
    const auto oldSettings = image["nodeSettings"].toArray(), oldOverrides = image["rangeOverrides"].toArray();
    QJsonArray newSettings, overrides;
    for (int i = 0; i < flow.size(); ++i) if (!extracted.contains(i)) {
      newSettings.append(settings(i < oldSettings.size() ? nodeSettingsFromJson(oldSettings[i].toObject()) : common[i], i));
      overrides.append(oldOverrides[i].toBool());
    }
    image["nodeSettings"] = newSettings; image["rangeOverrides"] = overrides; images[row] = image;
  }
  result["images"] = images; result["version"] = studioProjectVersion;
  return result;
}
namespace {
QJsonObject materializeInputs(QJsonObject project,const QString& path) {
  auto samples=project["images"].toArray();
  for(int row=0;row<samples.size();++row) {
    auto sample=samples[row].toObject();auto d=documentFromJson(sample,path);const auto original=d;
    for(const auto& node:project["workflow"].toArray()) {
      const auto p=node.toObject()["parameters"].toObject();const auto kind=p["algorithm"].toString();if(kind!="image_input"&&kind!="sequence_input")continue;
      const auto key=p["options"].toObject()["binding"].toString();if(d.sourceInputs.contains(key))continue;
      if(kind=="image_input"&&p["options"].toObject()["inputMode"].toInt()==1) {
        SourceData cell;cell.views=QStringList{"left","right"};cell.paths=QStringList{original.imagePath,original.rightPath};cell.keys=QStringList{original.pixelKey,original.rightPixelKey};cell.frames={original.image,original.rightImage};d.sourceInputs[key]=cell;
      } else d.sourceInputs[key]=resolveSourceData(original,key);
    }
    sample["sourceInputs"]=sourceInputsJson(d,path);samples[row]=sample;
  }
  project["images"]=samples;return project;
}
}

QJsonObject explicitTutorialInputs(const QJsonObject& original,const QString& path) {
  if(original["format"]!="mvkit-studio")return original;
  auto modern = [&](QJsonObject project) {
    // Bundled v12/v13 tutorials have the complete range/identity structures,
    // but predate a separate basic-workbench parameter record.
    if (project["version"].toInt() >= 12 && project["version"].toInt() < 17) {
      project["version"] = 17;
      if (!project.contains("basicParameters")) project["basicParameters"] = parametersJson(Parameters{}, path);
    }
    return namedWorkflowInputs(project, path);
  };
  auto flow=original["workflow"].toArray();
  for(const auto& node:flow) {
    const auto name=node.toObject()["parameters"].toObject()["algorithm"].toString();
    if(name=="image_input"||name=="sequence_input")return modern(materializeInputs(original,path));
  }
  if(flow.isEmpty())return original;
  bool stereo=false,sequence=false;
  for(const auto& node:flow){const auto name=node.toObject()["parameters"].toObject()["algorithm"].toString();stereo|=name=="stereo_rectify";sequence|=name=="structured_decode";}
  const QStringList bindings=stereo?QStringList{"views"}:sequence?QStringList{"sequence"}:QStringList{"image"};
  const int count=flow.size();
  if(count+bindings.size()>32)throw std::runtime_error("教程显式输入迁移超过节点预算");
  auto result=original;auto inputs=result["workflowInputs"].toArray(),links=result["workflowLinks"].toArray();
  auto ids=result["nodeIds"].toArray(),names=result["nodeAliases"].toArray(),shared=result["sharedRanges"].toArray(),positions=result["workflowPositions"].toArray();
  if(inputs.size()!=count||links.size()!=count||shared.size()!=count||ids.size()!=count||names.size()!=count)throw std::runtime_error("教程节点结构不完整");
  for(int i=0;i<count;++i) {
    const auto algorithm=flow[i].toObject()["parameters"].toObject()["algorithm"].toString();
    auto row=links[i].toArray();
    for(int j=0;j<row.size();++j)if(row[j].toInt()==-1)row[j]=count;
    if(inputs[i].toInt()==-1)inputs[i]=count;
    if(algorithm=="stereo_rectify"||algorithm=="structured_decode") {
      row=QJsonArray{count};
      if(algorithm=="stereo_rectify") {auto node=flow[i].toObject();auto parameters=node["parameters"].toObject();auto options=parameters["options"].toObject();options["multiView"]=1;parameters["options"]=options;node["parameters"]=parameters;flow[i]=node;}
      auto settings=shared[i].toObject();settings["dataA"]=count;settings["dataB"]=-2;shared[i]=settings;
    }
    links[i]=row;
  }
  for(const auto& binding:bindings) {
    Parameters p;p.algorithm=binding=="sequence"?Algorithm::SequenceInput:Algorithm::ImageInput;p.options["binding"]=binding;if(binding=="views")p.options["inputMode"]=1;
    // Legacy workflow entries require image/ROI metadata even for a source.
    auto entry=flow.first().toObject();entry["parameters"]=parametersJson(p,path);flow.append(entry);
    inputs.append(-2);links.append(QJsonArray{});shared.append(nodeSettingsJson({}));
    ids.append(QUuid::createUuid().toString(QUuid::WithoutBraces));
    names.append(binding=="views"?QStringLiteral("多视输入"):operatorDefinition(p.algorithm).name+(binding=="image" ? QString() : "-"+binding));
    if(!positions.isEmpty()) {auto position=positions.first().toArray();if(bindings.size()>1&&binding=="right")position[1]=position[1].toDouble()+140;positions.append(position);}
  }
  auto samples=result["images"].toArray();
  for(int k=0;k<samples.size();++k) {
    auto sample=samples[k].toObject();auto settings=sample["nodeSettings"].toArray(),overrides=sample["rangeOverrides"].toArray();
    if(settings.size()!=count||overrides.size()!=count)throw std::runtime_error("教程样本范围结构不完整");
    for(int i=0;i<count;++i) {
      const auto algorithm=flow[i].toObject()["parameters"].toObject()["algorithm"].toString();
      if(algorithm=="stereo_rectify"||algorithm=="structured_decode") {auto value=settings[i].toObject();value["dataA"]=count;value["dataB"]=-2;settings[i]=value;}
    }
    for(const auto& binding:bindings){Q_UNUSED(binding);settings.append(nodeSettingsJson({}));overrides.append(false);}
    // Materialize the legacy immutable pixels once into the canonical map.
    auto document=documentFromJson(sample,path);for(const auto& binding:bindings)if(!document.sourceInputs.contains(binding)) {
      if(binding=="views") {SourceData cell;cell.views=QStringList{"left","right"};cell.paths=QStringList{document.imagePath,document.rightPath};cell.keys=QStringList{document.pixelKey,document.rightPixelKey};cell.frames={document.image,document.rightImage};document.sourceInputs[binding]=cell;}
      else document.sourceInputs[binding]=resolveSourceData(document,binding);
    }
    sample["sourceInputs"]=sourceInputsJson(document,path);sample["nodeSettings"]=settings;sample["rangeOverrides"]=overrides;samples[k]=sample;
  }
  result["images"]=samples;result["workflow"]=flow;result["workflowInputs"]=inputs;result["workflowLinks"]=links;
  result["nodeIds"]=ids;result["nodeAliases"]=names;result["sharedRanges"]=shared;result["workflowPositions"]=positions;
  return modern(result);
}
}
