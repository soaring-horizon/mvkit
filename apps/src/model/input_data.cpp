// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "model/document.hpp"
#include "model/workflow_ports.hpp"
#include "runtime/execution.hpp"
#include "model/spatial_data.hpp"
#include "operators/range_model.hpp"
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <stdexcept>
#include <cmath>
#include <algorithm>
namespace workbench {
mv::MvImage readInputImage(const QString& path) {
  mv::MvImage image;
  image.ReadImage(path.toUtf8().constData());
  if (image.CountObj() == 1) return image;
  // XYZ(I) TIFF pages are one organized spatial input. Intensity is optional;
  // coordinate planes remain native Float32, never a display-normalized image.
  if (image.CountObj() != 3 && image.CountObj() != 4)
    throw std::runtime_error("多页输入须为 X、Y、Z（及可选亮度）TIFF");
  const auto x = image.SelectObj(1), y = image.SelectObj(2), z = image.SelectObj(3);
  for (const auto& plane : {x, y, z})
    if (plane.Channels() != 1 || plane.Type() != mv::MvPixelType::Float32 ||
        plane.Width() != x.Width() || plane.Height() != x.Height())
      throw std::runtime_error("XYZ 坐标页须为同尺寸单通道 real 图像");
  return x.Compose3(y, z);
}
NodeSettings resolveNodeInputs(const QVector<Parameters> &workflow,
                               const QVector<QVector<int>> &links, int target,
                               NodeSettings settings,
                               const QVector<ModelResource> &models) {
  if (target < 0 || target >= workflow.size())
    return settings;
  const auto &p = workflow[target];
  const auto ancestors = workflowAncestors(links, target);
  auto unique = [&](const auto &accepts) {
    int selected = -2;
    for (int source : ancestors)
      if (source >= 0 && source < workflow.size() && accepts(source)) {
        if (selected >= 0)
          return -2;
        selected = source;
      }
    return selected;
  };
  if (typedInputTool(p.algorithm)) {
    for (bool second : {false, true}) {
      if (second && !needsSecondInput(p))
        continue;
      auto &selected = second ? settings.dataB : settings.dataA;
      auto accepts = [&](int source) {
        const auto type = outputType(workflow[source]);
        if (!acceptsData(p.algorithm, type, second))
          return false;
        if (calibrationTool(p.algorithm) || captureTool(p.algorithm)) {
          if (!sourceTool(workflow[source].algorithm))
            return false;
          if (p.algorithm == Algorithm::StereoRectify) {
            const auto expected = !second && p.options["multiView"].toInt()
                                      ? QString("MultiView")
                                      : QString("Image");
            return type == expected &&
                   source != (second ? settings.dataA : settings.dataB);
          }
        }
        return true;
      };
      if (selected < 0)
        selected = unique(accepts);
    }
  }
  // Optional inputs only resolve after their corresponding mode is requested.
  if (settings.scope == ScopeMode::Region && settings.regionSource < 0)
    settings.regionSource = unique(
        [&](int source) { return outputType(workflow[source]) == "Region"; });
  if (settings.poseSource == -2)
    settings.poseSource = unique(
        [&](int source) { return outputType(workflow[source]) == "Match"; });
  if (calibrationConsumer(p.algorithm) && settings.calibrationSource == -2) {
    QVector<int> candidates;
    for (int source : ancestors)
      if (source >= 0 && source < workflow.size() &&
          calibrationLink(workflow[source].algorithm, p.algorithm))
        candidates.append(source);
    if (candidates.size() == 1)
      settings.calibrationSource = candidates.first();
    else if (candidates.isEmpty() && !p.calibrationArtifact.isEmpty())
      settings.calibrationSource = -1;
  }
  if (settings.modelId.isEmpty() && (p.algorithm == Algorithm::ShapeMatch ||
                                     p.algorithm == Algorithm::NccMatch)) {
    QStringList candidates;
    for (const auto &model : models)
      if (model.algorithm == p.algorithm && !model.independent)
        candidates.append(model.id);
    if (candidates.size() == 1)
      settings.modelId = candidates.first();
  }
  // Explicit named bindings are authoritative, including invalid/missing
  // selections. Never silently redirect them after node deletion/mode changes.
  for (const auto& port : inputPorts(p)) {
    if (settings.inputs.contains(port.key)) {
      setInputBinding(settings, port.key, settings.inputs[port.key]);
      continue;
    }
    if (!port.required && !(port.key == "Region" && settings.scope == ScopeMode::Region) &&
        !(port.key == "Match" && settings.poseSource != -1)) continue;
    auto ref = inputBinding(settings, port.key);
    if (ref.node < 0 && port.key == "ObjectB" &&
        (p.algorithm == Algorithm::StereoDisparity || p.algorithm == Algorithm::DisparityFilter ||
         p.algorithm == Algorithm::StereoRectify && p.options["multiView"].toInt()))
      ref.node = settings.dataA;
    QVector<OutputBinding> candidates;
    const QString preferred = p.algorithm == Algorithm::StereoRectify || p.algorithm == Algorithm::StereoDisparity
        ? (port.key == "ObjectA" ? "left" : "right")
        : p.algorithm == Algorithm::DisparityFilter ? (port.key == "ObjectA" ? "disparity" : "score")
        : p.algorithm == Algorithm::StereoReconstruct ? (port.key == "ObjectA" ? "disparity" : "geometry") : QString();
    for (int node : ancestors) {
      if (node < 0 || (ref.node >= 0 && node != ref.node)) continue;
      auto outputs = compatibleOutputs(workflow[node], p, port.key);
      // Retain the primary typed result in legacy bindings. New selections
      // explicitly name a scalar/raster and therefore bypass this migration.
      if (ref.node >= 0) {
        for (const auto& out : outputs) if (out.key == "result") { outputs = {out}; break; }
      }
      if (!preferred.isEmpty()) {
        for (const auto& out : outputs) if (out.key == preferred) { outputs = {out}; break; }
      }
      for (const auto& out : outputs) {
        const auto left = inputBinding(settings, "ObjectA");
        if (port.key == "ObjectB" && (p.algorithm == Algorithm::StereoRectify || p.algorithm == Algorithm::StereoDisparity) &&
            left.node == node && left.output == out.key) continue;
        candidates.append({node, out.key});
      }
    }
    if (candidates.size() == 1) setInputBinding(settings, port.key, candidates.first());
  }
  return settings;
}
std::shared_ptr<const SpatialData> sourceViewData(const SourceData& data,const QString& group) {
  if(data.views.isEmpty()||data.views.size()!=data.frames.size())return {};
  auto result=std::make_shared<SpatialData>();result->group=group;result->frame=data.paths.value(0);
  result->primary=data.views.contains("left")?"left":data.views.first();
  for(int i=0;i<data.views.size();++i){const auto role=data.views[i];
    result->rasters[role]={data.frames[i],role=="left"?QStringLiteral("左目"):role=="right"?QStringLiteral("右目"):role,
        "intensity",{},data.paths.value(i)};}
  return result;
}
namespace {
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
}
SourceData loadSourceData(const QStringList& paths, bool sequence, const std::atomic_bool* cancelled) {
  require(!paths.isEmpty() && paths.size() <= 64 && (sequence || paths.size() == 1),
          "图像输入须为一张图，序列输入须为 1–64 帧");
  SourceData data;
  data.sequence = sequence;
  QSet<QString> files;
  qint64 pixels = 0;
  for (const auto& path : paths) {
    if(cancelled && cancelled->load())return {};
    const auto canonical = QFileInfo(path).canonicalFilePath();
    require(!canonical.isEmpty() && !files.contains(canonical), "输入文件缺失或序列包含重复文件");
    files.insert(canonical);
    // SourceData owns raw frames and fingerprints, never display previews.
    // Creating an RGB preview for every exposure wastes work on large batches.
    const auto imagePath = QFileInfo(path).absoluteFilePath();
    auto image = readInputImage(imagePath);
    pixels += qint64(image.Width()) * qint64(image.Height());
    require(pixels <= 64000000, "一个输入最多 6400 万像素");
    if (sequence) {
      require((image.Channels() == 1 || image.Channels() == 3) && image.Type() == mv::MvPixelType::UInt8,
              "图像序列须为 byte 灰度或 RGB 图像");
      require(data.frames.isEmpty() || (data.frames.first().Width() == image.Width() &&
              data.frames.first().Height() == image.Height() &&
              data.frames.first().Channels() == image.Channels()), "同一图像序列的图像规格须一致");
    }
    data.paths.append(imagePath); data.keys.append(referenceImageKey(image)); data.frames.append(image);
  }
  return data;
}
SourceData loadMultiViewData(const QStringList& paths,const QStringList& views,const std::atomic_bool* cancelled) {
  require(paths.size()>=2 && paths.size()<=8 && paths.size()==views.size(),"多视输入须为2–8个具名视图");
  QSet<QString> unique,files; qint64 pixels=0;SourceData data; data.views=views;
  for(int i=0;i<paths.size();++i) {
    require(!views[i].isEmpty() && views[i].size()<=64 && !unique.contains(views[i]),"视图名称须唯一"); unique.insert(views[i]);
    const auto file=QFileInfo(paths[i]).canonicalFilePath();require(!file.isEmpty()&&!files.contains(file),"不同视图不能引用同一个文件");files.insert(file);
    const auto image=loadSourceData({paths[i]},false,cancelled);
    if(cancelled && cancelled->load())return {};
    pixels+=qint64(image.frames.first().Width())*qint64(image.frames.first().Height());require(pixels<=64000000,"多视输入像素总量过大");
    data.paths+=image.paths;data.keys+=image.keys;data.frames+=image.frames;
  }
  return data;
}
SourceData resolveSourceData(const Document& d, const QString& binding) {
  // An explicitly empty cell means missing, never use a previous/legacy input.
  if (d.sourceInputs.contains(binding)) return d.sourceInputs.value(binding);
  if (!d.sourceInputs.isEmpty()) return {}; // An owned sample never borrows another input's reference image.
  if (binding == "sequence" && !d.structuredFrames.isEmpty())
    return {true, d.structuredPaths, d.structuredKeys, d.structuredFrames};
  if (binding == "right" && d.rightImage.CountObj())
    return {false, {d.rightPath}, {d.rightPixelKey}, {d.rightImage}};
  if ((binding == "image" || binding == "left") && d.image.CountObj())
    return {false, {d.imagePath}, {d.pixelKey}, {d.image}};
  return {};
}
QJsonObject sourceInputsJson(const Document& d, const QString& project) {
  QJsonObject root;
  const QDir parent = QFileInfo(project).absoluteDir();
  for (auto it = d.sourceInputs.begin(); it != d.sourceInputs.end(); ++it) {
    QJsonArray files, keys;
    for (const auto& path : it->paths) files.append(parent.relativeFilePath(path));
    for (const auto& key : it->keys) keys.append(key);
    root[it.key()] = QJsonObject{{"sequence", it->sequence}, {"views", QJsonArray::fromStringList(it->views)}, {"files", files}, {"keys", keys}};
  }
  return root;
}
void readSourceInputs(Document& d, const QJsonObject& root, const QString& project) {
  require(root.size() <= 32, "样本输入列过多");
  qint64 pixels = 0;
  for (auto it = root.begin(); it != root.end(); ++it) {
    require(!it.key().isEmpty() && it.key().size() <= 128 && it.value().isObject(), "样本输入列无效");
    const auto cell = it.value().toObject();
    const auto files = cell["files"].toArray(), keys = cell["keys"].toArray();
    require(cell["sequence"].isBool() && files.size() <= 64 && keys.size() == files.size(), "样本输入格式无效");
    QStringList paths;
    for (const auto& file : files) {
      require(file.isString() && !file.toString().isEmpty(), "输入文件路径无效");
      paths.append(QFileInfo(project).absoluteDir().absoluteFilePath(file.toString()));
    }
    SourceData data;
    data.sequence = cell["sequence"].toBool();
    for(const auto& view:cell["views"].toArray()) { require(view.isString(),"视图角色无效");data.views.append(view.toString()); }
    require(!data.sequence || data.views.isEmpty(),"多视图组不能同时是曝光序列");
    if (!paths.isEmpty()) data = data.views.isEmpty() ? loadSourceData(paths, data.sequence) : loadMultiViewData(paths,data.views);
    for (int i = 0; i < data.keys.size(); ++i) {
      require(data.keys[i] == keys[i].toString(), "输入图像内容已改变，请重新绑定样本");
      pixels += data.frames[i].Width() * data.frames[i].Height();
    }
    require(pixels <= 128000000, "样本输入像素总量过大");
    d.sourceInputs.insert(it.key(), data);
  }
}
void applyCaptureContext(Document& stage, const Document& capture) {
  stage.captureId = capture.captureId;
  stage.imagePath = capture.imagePath;
  stage.rightImage = capture.rightImage; stage.rightPath = capture.rightPath;
  stage.rightPixelKey = capture.rightPixelKey; stage.rightPreview = capture.rightPreview;
  stage.stereoArtifact = capture.stereoArtifact;
  stage.structuredModel = capture.structuredModel;
  stage.structuredFrames = capture.structuredFrames;
  stage.structuredPaths = capture.structuredPaths; stage.structuredKeys = capture.structuredKeys;
}
bool executeInputNode(const Document& snapshot, int index,
                      const QVector<NodeOutput>& outputs, NodeOutput& n) {
  const auto& p = snapshot.workflow[index];
  QElapsedTimer timer; timer.start();
  if (sourceTool(p.algorithm)) {
    p.validate();
    require(workflowConnections(snapshot)[index].isEmpty(), "数据源节点不能连接上游");
    const auto key = p.options.value("binding").toString();
    const auto data = resolveSourceData(snapshot, key);
    require(!data.frames.isEmpty(), "当前样本缺少此输入，请在数据资源中配置样本");
    require(data.sequence == (p.algorithm == Algorithm::SequenceInput) && !data.views.isEmpty()==multiViewInput(p), "样本输入类型不匹配");
    require(data.frames.size() == data.paths.size() && data.keys.size() == data.paths.size(), "样本输入不完整");
    n.sourceData = std::make_shared<SourceData>(data);
    n.inputImage = n.outputImage = data.frames.first();
    n.inputPreview = n.preview = makePreview(n.outputImage).image;
    n.coordinateFrame = data.paths.first();
    n.effectiveDomain = n.outputImage.GetDomain();
    if (xyzInput(p)) {
      const auto& image = data.frames.first();
      require(image.Channels() == 3, "XYZ 输入须为三通道坐标图或多页 XYZ TIFF");
      for (int c = 0; c < 3; ++c)
        require(image.Type(c) == mv::MvPixelType::Float32, "XYZ 坐标须为 real 浮点图像");
      require(image.Width()*image.Height() <= 1000000, "XYZ 输入最多 100 万点");
      const double scale = p.options.value("xyzUnit").toInt() == 0 ? 1000. : 1.;
      auto spatial = std::make_shared<SpatialData>();
      spatial->group = snapshot.captureId;
      spatial->frame = n.coordinateFrame;
      spatial->primary = "depth";
      spatial->calibration = QJsonDocument(QJsonObject{{"format","mvkit-xyz-result"},
          {"version",1},{"lengthUnit","m"}}).toJson(QJsonDocument::Compact);
      mv::MvTuple rows, columns;
      for (int row = 0; row < int(image.Height()); ++row) {
        const float* planes[3];
        for (int c = 0; c < 3; ++c)
          planes[c] = reinterpret_cast<const float*>(static_cast<const unsigned char*>(image.Data(c)) + row*image.Stride(c));
        for (int col = 0; col < int(image.Width()); ++col) {
          const double x=planes[0][col], y=planes[1][col], z=planes[2][col];
          if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || z <= 0) continue;
          spatial->points.append({x*scale,y*scale,z*scale,row,col});
          rows.Append(row); columns.Append(col);
        }
      }
      require(!spatial->points.isEmpty(), "XYZ 输入没有有限且 Z > 0 的有效点");
      mv::MvRegion domain; domain.GenRegionRuns(rows, columns, columns);
      const QStringList keys{"x","y","depth"}, labels{"X / m","Y / m","Z / m"};
      for (int c=0;c<3;++c)
        spatial->rasters[keys[c]] = {image.AccessChannel(c+1).ScaleImage(scale/1000.,0).ReduceDomain(domain),
                                    labels[c],c==2?"depth":"coordinate","m",spatial->frame};
      n.spatial = spatial;
      n.outputImage = spatial->rasters["depth"].image;
      n.preview = spatialPreview(*spatial,"depth",true).image;
      n.effectiveDomain = domain;
    }
    n.measureName = !data.views.isEmpty()?QStringLiteral("多视图组"):data.sequence ? QStringLiteral("图像序列") : QStringLiteral("原始图像");
    n.state = NodeState::Success;
    n.totalMs = timer.nsecsElapsed() / 1e6;
    return true;
  }
  if (!captureTool(p.algorithm)) return false;
  p.validate();
  bool explicitSources = false;
  for (const auto& node : snapshot.workflow) explicitSources |= sourceTool(node.algorithm);
  const auto links=workflowConnections(snapshot);
  const auto settings = resolveNodeInputs(
      snapshot.workflow, links, index, workflowNodeSettings(snapshot, index));
  const bool legacy=!explicitSources && settings.dataA<0 && settings.dataB<0;
  if (legacy && p.calibrationArtifact.isEmpty()) return false; // Existing sample-bound project.
  auto source = [&](int upstream, bool sequence) -> const SourceData& {
    require(upstream >= 0 && upstream < outputs.size() &&
                workflowAncestors(links,index).contains(upstream), "请选择已连通上游的原始数据输入；存在多个来源时须明确选择");
    const auto& output = outputs[upstream];
    require(output.state == NodeState::Success && output.sourceData &&
                output.sourceData->sequence == sequence, "校正和解码须连接原始图像／序列输入节点");
    return *output.sourceData;
  };
  auto stage = snapshot;
  stage.runWorkflow = false; stage.parameters = p; stage.roi = {};
  stage.activeRange = settings;
  if (stage.captureId.isEmpty()) stage.captureId = stage.imagePath; // One immutable sample.
  if (!legacy && p.algorithm == Algorithm::StereoRectify) {
    auto image = [&](const QString& slot) {
      const auto ref = inputBinding(settings, slot);
      require(ref.node >= 0 && ref.node < outputs.size() && workflowAncestors(links,index).contains(ref.node),
              "请选择已连通上游的左、右图像输出");
      const auto choices = compatibleOutputs(snapshot.workflow[ref.node], p, slot);
      require(std::any_of(choices.begin(), choices.end(), [&](const OutputPort& port) { return port.key == ref.output; }),
              "左右图像输出已失效，请重新选择");
      auto value = selectOutput(outputs[ref.node], ref.output);
      require(value.state == NodeState::Success && value.outputImage.CountObj() == 1, "左右输入须为单张图像");
      return value;
    };
    const auto a = image("ObjectA"), b = image("ObjectB");
    const auto left = inputBinding(settings, "ObjectA"), right = inputBinding(settings, "ObjectB");
    require(left.node != right.node || left.output != right.output, "左右图不能绑定同一个输出");
    stage.image = a.outputImage; stage.rightImage = b.outputImage;
    stage.imagePath = stage.coordinateFrame = a.coordinateFrame;
    stage.rightPath = b.coordinateFrame;
    stage.preview = makePreview(stage.image).image; stage.rightPreview = makePreview(stage.rightImage).image;
  } else if (!legacy) {
    const auto& a=source(settings.dataA,p.algorithm==Algorithm::StructuredDecode);
    stage.image=a.frames.first();stage.imagePath=a.paths.first();stage.pixelKey=a.keys.first();
    stage.preview=makePreview(stage.image).image;stage.coordinateFrame=stage.imagePath;
    if (p.algorithm==Algorithm::StereoRectify) {
      if(p.options.value("multiView").toInt()) {
        const int left=a.views.indexOf("left"),right=a.views.indexOf("right");
        require(left>=0 && right>=0 && left!=right,"多视输入缺少左、右相机角色");
        stage.image=a.frames[left];stage.imagePath=a.paths[left];stage.pixelKey=a.keys[left];stage.preview=makePreview(stage.image).image;stage.coordinateFrame=stage.imagePath;
        stage.rightImage=a.frames[right];stage.rightPath=a.paths[right];stage.rightPixelKey=a.keys[right];
      } else {
        const auto& b=source(settings.dataB,false);
        require(a.views.isEmpty() && b.views.isEmpty() && settings.dataA!=settings.dataB,"双目左右图须连接不同的单图输入");
        stage.rightImage=b.frames.first();stage.rightPath=b.paths.first();stage.rightPixelKey=b.keys.first();
      }
      stage.rightPreview=makePreview(stage.rightImage).image;
    } else {
      stage.structuredFrames=a.frames;stage.structuredPaths=a.paths;stage.structuredKeys=a.keys;
    }
  }
  if (p.algorithm==Algorithm::StereoRectify) {
    stage.structuredFrames.clear();stage.structuredPaths.clear();stage.structuredKeys.clear();stage.structuredModel.clear();
    if (!p.calibrationArtifact.isEmpty())stage.stereoArtifact=p.calibrationArtifact;
  } else if (!p.calibrationArtifact.isEmpty())stage.structuredModel=p.calibrationArtifact;
  const auto r = executeSpatial(stage);
  n.inputImage = stage.image; n.inputPreview = stage.preview; n.effectiveDomain = stage.image.GetDomain();
  n.outputImage = r.outputImage; n.preview = r.preview; n.spatial = r.spatial; n.objects = r.objects;
  n.measureName = r.measureName; n.algorithmMs = r.algorithmMs; n.totalMs = r.totalMs;
  n.error = r.error; n.state = r.error.isEmpty() ? NodeState::Success : NodeState::Error;
  n.coordinateFrame = r.spatial ? r.spatial->frame : stage.coordinateFrame;
  n.captureContext = std::make_shared<Document>(stage);
  return true;
}
} // namespace workbench
