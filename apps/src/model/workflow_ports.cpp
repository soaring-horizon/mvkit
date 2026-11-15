// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "model/workflow_ports.hpp"
#include "model/spatial_data.hpp"
#include "runtime/execution.hpp"
#include "operators/operator_registry.hpp"
#include <stdexcept>
#include <QJsonArray>

namespace workbench {
QString outputSummary(const Parameters& p) {
  const auto ports = outputPorts(p);
  return ports.size() == 1 ? ports.first().type : QStringLiteral("%1 项输出").arg(ports.size());
}
QVector<OutputPort> outputPorts(const Parameters& p) {
  QVector<OutputPort> ports;
  auto add = [&](const char* key, const QString& label, const char* type = "Image") {
    ports.append({QString::fromLatin1(key), label, QString::fromLatin1(type)});
  };
  const auto a = p.algorithm;
  if (a == Algorithm::StereoRectify) {
    add("left", QStringLiteral("校正左图"));
    add("right", QStringLiteral("校正右图"));
    add("geometry", QStringLiteral("校正几何模型"), "StereoGeometry");
  } else if (a == Algorithm::StereoDisparity || a == Algorithm::DisparityFilter) {
    add("disparity", QStringLiteral("视差图"));
    add("score", QStringLiteral("匹配代价"));
    add("domain", QStringLiteral("有效域"), "Region");
    if (a == Algorithm::DisparityFilter) {
      add("disparityValid", QStringLiteral("有效视差图"));
      add("rejection", QStringLiteral("过滤原因图"));
      add("result", QStringLiteral("过滤统计"), "Value");
    }
  } else if (a == Algorithm::StructuredDecode) {
    add("result", QStringLiteral("投影对应关系"), "Correspondence");
    add("reference", QStringLiteral("参考图像"));
    add("projectorColumn", QStringLiteral("投影仪列坐标"));
    add("projectorRow", QStringLiteral("投影仪行坐标"));
    add("contrast", QStringLiteral("调制度"));
    add("confidence", QStringLiteral("归一化调制度"));
    add("decodeValid", QStringLiteral("解码有效图"));
    add("decodeReason", QStringLiteral("解码原因图"));
    add("domain", QStringLiteral("有效域"), "Region");
  } else if (multiViewInput(p)) {
    // Multi-view acquisition remains a resource container for calibration;
    // ordinary algorithms can bind its individual images directly.
    add("result", QStringLiteral("多视数据"), "MultiView");
    auto views = p.options["viewRoles"].toArray();
    if (views.isEmpty()) views = {"left", "right"};
    for (const auto& view : views) {
      const auto key = view.toString();
      ports.append({key == "left" || key == "right" ? key : "view:" + key,
                    key == "left" ? QStringLiteral("左目图像") : key == "right" ? QStringLiteral("右目图像") : key, "Image"});
    }
  } else {
    const auto type = outputType(p);
    ports.append({type == "Image" ? "image" : "result", operatorDefinition(a).name + QStringLiteral("结果"), type});
    if (type == "PointCloud" || a == Algorithm::SphereFit || a == Algorithm::PlaneFit) {
      add("x", QStringLiteral("X 坐标图"));
      add("y", QStringLiteral("Y 坐标图"));
      add("depth", QStringLiteral("深度 Z 图"));
      add("domain", QStringLiteral("有效域"), "Region");
    }
    if (a == Algorithm::SphereFit || a == Algorithm::PlaneFit)
      add("fitResidual", QStringLiteral("拟合残差图"));
    if (a == Algorithm::StereoReconstruct || a == Algorithm::StructuredReconstruct)
      add("valid", QStringLiteral("有效三维点图"));
  }
  // Numerical fields are outputs, not preview components. The schema follows
  // the current algorithm options (e.g. plane/sphere metrics).
  const auto numeric = numericalOutputSchema(p);
  for (auto it = numeric.values.cbegin(); it != numeric.values.cend(); ++it)
    ports.append({"value:" + it.key(), fieldLabel(it.key()), "Value"});
  if (a == Algorithm::ShapeMatch || a == Algorithm::NccMatch) {
    add("value:score", QStringLiteral("匹配得分"), "Value");
    add("value:angle", QStringLiteral("角度"), "Value");
  }
  return ports;
}

QVector<InputPort> inputPorts(const Parameters& p) {
  QVector<InputPort> ports;
  const auto a = p.algorithm;
  if (sourceTool(a)) return ports;
  if (a == Algorithm::StereoRectify || a == Algorithm::StereoDisparity) {
    ports = {{"ObjectA", QStringLiteral("左图像"), {"Image"}},
             {"ObjectB", QStringLiteral("右图像"), {"Image"}}};
  } else if (a == Algorithm::DisparityFilter) {
    ports = {{"ObjectA", QStringLiteral("视差图"), {"Image"}},
             {"ObjectB", QStringLiteral("匹配代价"), {"Image"}}};
  } else if (a == Algorithm::StereoReconstruct) {
    ports = {{"ObjectA", QStringLiteral("视差图"), {"Image"}},
             {"ObjectB", QStringLiteral("校正几何模型"), {"StereoGeometry"}}};
  } else if (typedInputTool(a)) {
    const QStringList types{"Image", "MultiView", "ImageSequence", "Region", "XLD", "Point", "Circle", "Ellipse", "Line", "Match", "Value", "Plane3D", "PointCloud", "Correspondence", "Calibration", "StereoPair", "Disparity"};
    for (bool second : {false, true}) {
      if (second && !needsSecondInput(p) && a != Algorithm::CloudSelect) continue;
      QStringList accepted;
      for (const auto& type : types) if (acceptsData(a, type, second)) accepted << type;
      ports.append({second ? "ObjectB" : "ObjectA", second ? QStringLiteral("输入 B") : QStringLiteral("输入"), accepted, !second || needsSecondInput(p)});
    }
  } else ports.append({"Image", QStringLiteral("图像"), {"Image"}});
  if (calibrationConsumer(a)) ports.append({"Calibration", QStringLiteral("标定模型"), {"Calibration"}, false});
  if (!captureTool(a) && !calibrationTool(a)) {
    ports.append({"Region", QStringLiteral("处理区域"), {"Region"}, false});
    ports.append({"Match", QStringLiteral("匹配定位"), {"Match"}, false});
  }
  return ports;
}

QVector<OutputPort> compatibleOutputs(const Parameters& source, const Parameters& target, const QString& slot) {
  QVector<OutputPort> result;
  for (const auto& in : inputPorts(target)) if (in.key == slot)
    for (const auto& out : outputPorts(source))
      if (in.types.contains(out.type) && (slot != "Calibration" || calibrationLink(source.algorithm, target.algorithm)))
        result.append(out);
  return result;
}
OutputBinding inputBinding(const NodeSettings& s, const QString& slot) {
  if (s.inputs.contains(slot)) return s.inputs[slot];
  return {slot == "ObjectA" ? s.dataA : slot == "ObjectB" ? s.dataB : slot == "Region" ? s.regionSource : slot == "Match" ? s.poseSource : slot == "Calibration" ? s.calibrationSource : -2, {}};
}
void setInputBinding(NodeSettings& s, const QString& slot, OutputBinding ref) {
  s.inputs[slot] = ref;
  if (slot == "ObjectA") s.dataA = ref.node;
  if (slot == "ObjectB") s.dataB = ref.node;
  if (slot == "Region") s.regionSource = ref.node;
  if (slot == "Match") s.poseSource = ref.node;
  if (slot == "Calibration") s.calibrationSource = ref.node;
}
void remapInputBindings(NodeSettings& s, const std::function<int(int)>& remap) {
  for (auto& ref : s.inputs) if (ref.node >= 0) ref.node = remap(ref.node);
}

NodeOutput selectOutput(const NodeOutput& source, const QString& key) {
  auto out = source;
  if (!out.outputImage.CountObj()) { out.outputImage = source.inputImage; out.preview = source.inputPreview; }
  if (key.isEmpty() || key == "result" || key == "image") {
    return out;
  }
  const auto viewKey = key.startsWith("view:") ? key.mid(5) : key;
  if (source.sourceData && source.sourceData->views.contains(viewKey)) {
    const auto& data = *source.sourceData;
    const int view = data.views.indexOf(viewKey);
    out.inputImage = out.outputImage = data.frames[view];
    out.inputPreview = out.preview = makePreview(out.outputImage).image;
    out.coordinateFrame = data.paths[view]; out.effectiveDomain = out.outputImage.GetDomain();
    return out;
  }
  if (key.startsWith("value:")) {
    const auto field = key.mid(6);
    for (auto& object : out.objects) {
      if (field == "score" && object.type == "Match") object.values["score"] = object.measure;
      if (!object.values.contains(field)) throw std::runtime_error("所选数值输出不存在，请重新选择输入");
      object.measure = object.values[field]; object.unit = fieldUnit(object, field);
      object.type = "Value"; object.label = fieldLabel(field);
      object.values = {{"value", object.measure}};
    }
    return out;
  }
  if (!source.spatial) throw std::runtime_error("所选输出不存在，请检查上游模式和输入绑定");
  if (key == "geometry") {
    if (source.spatial->calibration.isEmpty()) throw std::runtime_error("校正几何模型不存在");
    return out;
  }
  auto rasterKey = key == "domain" ? source.spatial->primary : key;
  if (!source.spatial->rasters.contains(rasterKey)) throw std::runtime_error("所选图像输出不存在，请重新选择输入");
  const auto& raster = source.spatial->rasters[rasterKey];
  out.outputImage = raster.image;
  out.inputImage = raster.image;
  out.preview = out.inputPreview = makePreview(raster.image).image;
  out.coordinateFrame = raster.frame;
  out.effectiveDomain = raster.image.GetDomain();
  out.objects.clear(); out.contours = {}; out.regions = {};
  if (key == "domain") out.regions = out.effectiveDomain;
  return out;
}
}
