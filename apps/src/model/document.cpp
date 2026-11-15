// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "model/document.hpp"
#include "model/workflow_ports.hpp"
#include "model/metadata_limits.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStringList>
#include <QTransform>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <vector>

#include "operators/detection_tools.hpp"
#include "operators/image_processing.hpp"
#include "operators/operator_registry.hpp"
#include "operators/range_model.hpp"
#include "model/spatial_data.hpp"
#include "runtime/worker_protocol.hpp"

namespace workbench {
namespace {
[[noreturn]] void invalid(const QString& message) {
  throw std::runtime_error(message.toUtf8().constData());
}
struct Plane {
  const unsigned char* data;
  std::size_t stride;
  mv::MvPixelType type;
};
template <class T>
double sample(const Plane& plane, int row, int column) {
  T value;
  std::memcpy(&value,
              plane.data + static_cast<std::size_t>(row) * plane.stride +
                  static_cast<std::size_t>(column) * sizeof(T),
              sizeof(T));
  return static_cast<double>(value);
}
double sample(const Plane& p, int r, int c) {
  switch (p.type) {
    case mv::MvPixelType::UInt8:
      return sample<std::uint8_t>(p, r, c);
    case mv::MvPixelType::UInt16:
      return sample<std::uint16_t>(p, r, c);
    case mv::MvPixelType::Int16:
      return sample<std::int16_t>(p, r, c);
    case mv::MvPixelType::Int32:
      return sample<std::int32_t>(p, r, c);
    case mv::MvPixelType::Float32:
      return sample<float>(p, r, c);
  }
  invalid(QStringLiteral("不支持的像素类型"));
}
double number(const QJsonObject& object, const char* key) {
  const auto value = object.value(QLatin1String(key));
  if (!value.isDouble() || !std::isfinite(value.toDouble()))
    invalid(QStringLiteral("工程参数无效：") + key);
  return value.toDouble();
}
int integer(const QJsonObject& object, const char* key) {
  const double value = number(object, key);
  if (value != std::floor(value) || value < 0 || value > 65535)
    invalid(QStringLiteral("工程整数参数无效：") + key);
  return static_cast<int>(value);
}
}  // namespace
bool multiViewInput(const Parameters& p) { return p.algorithm==Algorithm::ImageInput && p.options.value("inputMode").toInt()==1; }
QString outputType(const Parameters& p) { return xyzInput(p) ? QString("PointCloud") : multiViewInput(p) ? QString("MultiView") : operatorDefinition(p.algorithm).output; }
bool needsSecondInput(const Parameters& p) {
  return p.algorithm == Algorithm::StereoRectify || p.algorithm == Algorithm::StereoDisparity ||
         p.algorithm == Algorithm::StereoReconstruct || p.algorithm == Algorithm::DisparityFilter || p.algorithm == Algorithm::Measure &&
             p.options.value("mode").toInt() != 0 ||
         (p.algorithm == Algorithm::CloudMeasure && p.options.value("mode").toInt() == 3) ||
         p.algorithm == Algorithm::PlaneMeasure ||
         p.algorithm == Algorithm::CloudSelect &&
             p.options.value("selection").toInt() == 1 ||
         p.algorithm == Algorithm::RegionBoolean &&
             p.options.value("operation").toInt() != 0;
}
void Parameters::validate() const {
  operatorDefinition(algorithm);
  if (xyzInput(*this) && options.value("xyzUnit").toDouble(0) != 0 && options.value("xyzUnit").toDouble(0) != 1)
    invalid(QStringLiteral("XYZ 单位须为 m 或 mm"));
  if((algorithm==Algorithm::ImageInput && options.contains("inputMode") && options["inputMode"].toDouble()!=0 && options["inputMode"].toDouble()!=1 && options["inputMode"].toDouble()!=2) ||
     (algorithm==Algorithm::StereoRectify && options.contains("multiView") && options["multiView"].toDouble()!=0 && options["multiView"].toDouble()!=1))invalid(QStringLiteral("输入模式无效"));
  if (optionTool(algorithm)) {
    if (algorithm == Algorithm::StructuredImage) {
      const double component = options.value("component").toDouble(0);
      if (component < 0 || component > 6 || component != std::floor(component))
        invalid(QStringLiteral("结构光图像分量无效"));
    }
    if (algorithm == Algorithm::StructuredDecode) {
      const auto contrast = options.value("minContrast").toDouble(10);
      const auto amplitude = options.value("minAmplitude").toDouble(2);
      if (contrast < 0 || contrast > 255 || contrast != std::floor(contrast) || amplitude < 0 || amplitude > 255)
        invalid(QStringLiteral("结构光质量阈值须在 0–255 内，对比度为整数"));
    }
    if (algorithm == Algorithm::StereoImage || algorithm == Algorithm::CloudSelect) {
      const auto key = algorithm == Algorithm::StereoImage ? "component" : "selection";
      const auto value = options.value(key).toDouble(0);
      if (value < 0 || value > 1 || value != std::floor(value))
        invalid(QStringLiteral("图像分量或点云选择方式无效"));
    }
    validateDetection(*this);
    validateProcessing(*this);
    if (options.size() > 24) invalid(QStringLiteral("工具参数过多"));
    for (auto it = options.begin(); it != options.end(); ++it) {
      if (it.key() == "field") {
        // Keys belong to the selected upstream output, not a global metric
        // whitelist. Execution checks existence on the actual selected object.
        const auto key = it.value().toString();
        const bool identifier = std::all_of(key.begin(), key.end(), [](QChar c) {
          return c >= QLatin1Char('a') && c <= QLatin1Char('z') ||
                 c >= QLatin1Char('A') && c <= QLatin1Char('Z') ||
                 c >= QLatin1Char('0') && c <= QLatin1Char('9') || c == QLatin1Char('_');
        });
        if (!it.value().isString() || key.isEmpty() || key.size() > 64 || !identifier)
          invalid(QStringLiteral("命名数值无效"));
      } else if (it.key() == "viewRoles" && multiViewInput(*this)) {
        const auto roles = it.value().toArray();
        if (!it.value().isArray() || roles.size() < 2 || roles.size() > 8)
          invalid(QStringLiteral("多视输出名称无效"));
        QStringList names;
        for (const auto& role : roles) {
          const auto name = role.toString();
          if (!role.isString() || name.isEmpty() || name.size() > 128 || names.contains(name))
            invalid(QStringLiteral("多视输出名称无效"));
          names.append(name);
        }
      } else if (it.key() == "binding" && sourceTool(algorithm)) {
        const auto key = it.value().toString();
        if (!it.value().isString() || key.isEmpty() || key.size() > 128 || key.contains('\n') || key.contains('\r'))
          invalid(QStringLiteral("样本输入列名无效"));
      } else if (!it.value().isDouble())
        invalid(QStringLiteral("工具参数类型无效"));
    }
    for (auto it = options.begin(); it != options.end(); ++it)
      if (it.value().isDouble() && !std::isfinite(it.value().toDouble()))
        invalid(QStringLiteral("参数必须为有限数值"));
    auto val = [&](const char* key, double def) {
      return options.value(key).toDouble(def);
    };
    if (algorithm == Algorithm::ContourFilter &&
        (val("minLength", 0) < 0 ||
         val("minLength", 0) > val("maxLength", 1e9)))
      invalid(QStringLiteral("轮廓长度下限须不大于上限"));
    if ((algorithm == Algorithm::LineFit || algorithm == Algorithm::CircleFit ||
         algorithm == Algorithm::EllipseFit) &&
        (val("maxRms", 3) < 0 || val("minArc", 60) < 0 ||
         val("minArc", 60) > 360))
      invalid(QStringLiteral("残差及圆弧覆盖角度无效"));
    if (val("qualityTolerance", 1) <= 0 || val("qualityTolerance", 1) > 1e6)
      invalid(QStringLiteral("质量显示容差无效"));
    if (algorithm == Algorithm::ContourProcess &&
        (val("relativeGap", .1) < 0 || val("relativeGap", .1) > 100 ||
         val("operation", 0) < 0 || val("operation", 0) > 4 ||
         val("operation", 0) != std::floor(val("operation", 0)) ||
         val("smooth", 5) < 3 || val("smooth", 5) > 101 ||
         int(val("smooth", 5)) % 2 == 0 || val("distance", 2) <= 0 ||
         val("distance", 2) > 1e6 || val("shift", 2) < 0 ||
         val("shift", 2) > 1e6 || val("angle", 10) < 0 ||
         val("angle", 10) > 180))
      invalid(QStringLiteral("轮廓处理参数无效"));
    if (algorithm == Algorithm::Judge && val("lower", 0) > val("upper", 100))
      invalid(QStringLiteral("公差下限须不大于上限"));
    for (auto key :
         {"mode", "method", "sort", "closed", "take", "smooth", "operation"})
      if (val(key, 0) != std::floor(val(key, 0)))
        invalid(QStringLiteral("选项和数量须为整数"));
    if (val("take", 0) < 0 || val("take", 0) > 10000 ||
        val("borderMargin", 0) < 0 || val("borderMargin", 0) > 20)
      invalid(QStringLiteral("轮廓数量或边缘宽度无效"));
    for (auto key : {"objectA", "objectB"})
      if (val(key, 1) < 1 || val(key, 1) > 10000 ||
          val(key, 1) != std::floor(val(key, 1)))
        invalid(QStringLiteral("对象编号须为 1–10000 的整数"));
    if (algorithm < Algorithm::ImageMorph &&
        (val("mode", 0) < 0 || val("mode", 0) > (algorithm == Algorithm::RegionFeatures ? 9 : 5) || val("method", 0) < 0 ||
         val("method", 0) > 2 || val("sort", 0) < 0 || val("sort", 0) > 3 ||
         val("closed", 0) < 0 || val("closed", 0) > 2))
      invalid(QStringLiteral("工具配置类型无效"));
  }
  if ((algorithm == Algorithm::Filter &&
       (filter < 0 || filter > 2 || mask < 3 || mask > 31 || mask % 2 == 0)) ||
      (algorithm == Algorithm::Gray && (gray < 0 || gray > 1)))
    invalid(QStringLiteral("滤波窗口须为 3–31 的奇数，且配置类型有效"));
  if ((algorithm == Algorithm::ShapeMatch ||
       algorithm == Algorithm::NccMatch) &&
      (!std::isfinite(score) || score < 0 || score > 1 || matches < 1 ||
       matches > 100 || !std::isfinite(angleStart) ||
       !std::isfinite(angleExtent) || angleStart < -180 || angleStart > 180 ||
       angleExtent < 0 || angleExtent > 360))
    invalid(QStringLiteral("匹配得分须为 0–1，数量 1–100，角度范围 0–360 度"));
  if (algorithm == Algorithm::Regions) {
    const double method = options.value("thresholdMethod").toDouble(0);
    const double window = options.value("localWindow").toDouble(31);
    const double offset = options.value("localOffset").toDouble(8);
    const double dark = options.value("dark").toDouble(0);
    if (method < 0 || method > 2 || method != std::floor(method) ||
        (method != 0 && dark != 0 && dark != 1) ||
        (method == 2 && (window < 3 || window > 255 || window != std::floor(window) || int(window)%2 == 0 || offset < 0 || offset > 65535)))
      invalid(QStringLiteral("阈值选项无效，局部窗口须为 3–255 的奇数"));
  }
  if (algorithm == Algorithm::Regions && options.value("thresholdMethod").toInt() == 0 &&
      (!std::isfinite(minimum) || !std::isfinite(maximum) ||
       minimum > maximum || minimum < -2147483648.0 || maximum > 2147483647.0))
    invalid(QStringLiteral("灰度上下限须在 int32 范围内，且下限 ≤ 上限"));
  if (algorithm == Algorithm::Regions &&
      (!std::isfinite(opening) || opening < 0 || opening > 100))
    invalid(QStringLiteral("开运算半径须在 0–100 之间（0 表示关闭）"));
  if (algorithm == Algorithm::Regions &&
      (!std::isfinite(minimumArea) || minimumArea < 0 || minimumArea > 1e12))
    invalid(QStringLiteral("最小面积须在 0–10¹² 之间"));
  if (algorithm == Algorithm::Edges &&
      (!std::isfinite(sigma) || sigma < 0.3 || sigma > 10 || edgeLow < 0 ||
       edgeLow > edgeHigh || edgeHigh > 65535))
    invalid(QStringLiteral(
        "Canny 参数须满足 0.3 ≤ Alpha ≤ 10，0 ≤ Low ≤ High ≤ 65535"));
}
QVector<int> workflowSources(const Document& d) {
  if (!d.workflowInputs.isEmpty()) {
    if (d.workflowInputs.size() != d.workflow.size())
      invalid(QStringLiteral("节点输入数量无效"));
    auto sources = d.workflowInputs;
    if (!d.workflowLinks.isEmpty()) {
      for (int i = 0; i < sources.size(); ++i) {
        if (sources[i] != -2)
          continue;
        QVector<int> candidates;
        for (int source : workflowAncestors(d.workflowLinks, i))
          if (source == -1 || source >= 0 && source < d.workflow.size() &&
                                  outputType(d.workflow[source]) == "Image")
            candidates.append(source);
        if (candidates.size() == 1)
          sources[i] = candidates.first();
      }
    }
    return sources;
  }
  QVector<int> sources;
  for (int i = 0; i < d.workflow.size(); ++i) sources.append(i - 1);
  return sources;
}
QVector<QVector<int>> workflowConnections(const Document& d) {
  if (!d.workflowLinks.isEmpty()) {
    if (d.workflowLinks.size() != d.workflow.size())
      invalid(QStringLiteral("节点连线数量无效"));
    return d.workflowLinks;
  }
  QVector<QVector<int>> links;
  for (int source : workflowSources(d))
    links.append(source >= -1 ? QVector<int>{source} : QVector<int>{});
  return links;
}
QVector<int> workflowAncestors(const QVector<QVector<int>>& links, int target) {
  QVector<int> found;
  QVector<bool> seen(links.size(), false);
  std::function<void(int)> visit = [&](int node) {
    if (node < 0 || node >= links.size() || seen[node]) return;
    seen[node] = true;
    for (int source : links[node]) {
      if (source < -1 || source >= links.size()) continue;
      if (!found.contains(source)) found.append(source);
      if (source >= 0) visit(source);
    }
  };
  visit(target);
  std::sort(found.begin(), found.end());
  return found;
}
QVector<int> workflowInputSources(const QVector<QVector<int>>& links,
                                  int target) {
  return workflowAncestors(links, target);
}
QVector<int> workflowImageSources(const Document& d, int target) {
  auto sources = workflowInputSources(workflowConnections(d), target);
  sources.erase(
      std::remove_if(
          sources.begin(), sources.end(),
          [&](int source) {
            if (source < 0) return false;
            const auto ports = outputPorts(d.workflow[source]);
            return std::none_of(ports.begin(), ports.end(), [](const OutputPort& p) { return p.type == "Image"; });
          }),
      sources.end());
  return sources;
}
QVector<int> workflowObjectSources(const Document& d, int target,
                                   const QString& type) {
  auto sources = workflowAncestors(workflowConnections(d), target);
  sources.erase(
      std::remove_if(
          sources.begin(), sources.end(),
          [&](int i) {
            if (i < 0) return true;
            const auto ports = outputPorts(d.workflow[i]);
            return std::none_of(ports.begin(), ports.end(), [&](const OutputPort& p) { return p.type == type; });
          }),
      sources.end());
  return sources;
}
NodeSettings workflowNodeSettings(const Document& d, int node) {
  auto resolved = [&](NodeSettings s) {
    if (!typedInputTool(d.workflow[node].algorithm) && !sourceTool(d.workflow[node].algorithm) &&
        !s.inputs.contains("Image") && node < d.workflowInputs.size()) {
      const int source = d.workflowInputs[node];
      if (source == -1 || source >= 0 && source < d.workflow.size() && outputType(d.workflow[source]) == "Image")
        setInputBinding(s, "Image", {source, source >= 0 ? QString("image") : QString()});
    }
    return resolveNodeInputs(d.workflow, workflowConnections(d), node, s, d.models);
  };
  if (!d.sharedNodeSettings.isEmpty()) {
    if (d.sharedNodeSettings.size() != d.workflow.size())
      invalid(QStringLiteral("公共范围数量无效"));
    if (!d.rangeOverrides.value(node))
      return resolved(d.sharedNodeSettings[node]);
  }
  if (!d.nodeSettings.isEmpty()) {
    if (d.nodeSettings.size() != d.workflow.size())
      invalid(QStringLiteral("节点范围数量无效"));
    return resolved(d.nodeSettings[node]);
  }
  NodeSettings settings;
  // Legacy shared ROI is copied to every node; matching used it as template
  // when no external template existed. New nodes start at Full explicitly.
  if (d.workflow[node].templatePath.isEmpty() &&
      (d.workflow[node].algorithm == Algorithm::ShapeMatch ||
       d.workflow[node].algorithm == Algorithm::NccMatch))
    settings.templateRoi = d.roi;
  else if (d.roi.kind != RoiKind::None) {
    settings.scope = ScopeMode::Manual;
    settings.roi = d.roi;
  }
  return resolved(settings);
}
int workflowOutputNode(const Document& d) {
  return d.workflowOutput == -1 ? d.workflow.size() - 1 : d.workflowOutput;
}
QJsonObject nodeSettingsJson(const NodeSettings& s) {
  auto encode = [](const Roi& r) {
    return QJsonObject{
        {"kind", int(r.kind)},         {"x", r.bounds.x()},
        {"y", r.bounds.y()},           {"width", r.bounds.width()},
        {"height", r.bounds.height()}, {"angle", r.angle},
        {"innerRatio", r.innerRatio}};
  };
  QJsonArray exclusions;
  for (const auto& r : s.exclusions) exclusions.append(encode(r));
  QJsonObject inputs;
  for (auto it = s.inputs.cbegin(); it != s.inputs.cend(); ++it)
    inputs[it.key()] = QJsonObject{{"node", it->node}, {"output", it->output}};
  return {{"scope", int(s.scope)},
          {"inputs", inputs},
          {"regionSource", s.regionSource},
          {"roi", encode(s.roi)},
          {"templateRoi", encode(s.templateRoi)},
          {"exclusions", exclusions},
          {"referenceSize",
           QJsonArray{s.referenceSize.width(), s.referenceSize.height()}},
          {"poseSource", s.poseSource},
          {"referenceX", s.referencePosition.x()},
          {"referenceY", s.referencePosition.y()},
          {"referenceAngle", s.referenceAngle},
          {"poseInstance", s.poseInstance},
          {"iteratePose", s.iteratePose},
          {"poseReferenceSet", s.poseReferenceSet},
          {"modelId", s.modelId},
          {"dataA", s.dataA},
          {"dataB", s.dataB},
          {"calibrationSource", s.calibrationSource},
          {"referencePixelKey", s.referencePixelKey},
          {"referenceModelKey", s.referenceModelKey}};
}
NodeSettings nodeSettingsFromJson(const QJsonObject& o) {
  auto decode = [](const QJsonObject& r) {
    const int kind = integer(r, "kind");
    if (kind > 4) invalid(QStringLiteral("节点 ROI 类型无效"));
    return Roi{static_cast<RoiKind>(kind),
               QRectF(number(r, "x"), number(r, "y"), number(r, "width"),
                      number(r, "height")),
               r.value("angle").toDouble(0),
               r.value("innerRatio").toDouble(.5)};
  };
  const int mode = integer(o, "scope");
  const double source = number(o, "regionSource");
  if (mode > 2 || source != std::floor(source) || source < -2 || source > 31)
    invalid(QStringLiteral("节点处理范围无效"));
  NodeSettings s{static_cast<ScopeMode>(mode), decode(o["roi"].toObject()),
                 decode(o["templateRoi"].toObject()), int(source)};
  const auto inputs = o["inputs"].toObject();
  if (inputs.size() > 32) invalid(QStringLiteral("节点输入过多"));
  for (auto it = inputs.begin(); it != inputs.end(); ++it) {
    const auto ref = it->toObject();
    const auto node = ref["node"].toDouble(-3);
    if (node != std::floor(node) || node < -2 || node > 31 || ref["output"].toString().size() > 128)
      invalid(QStringLiteral("具名输入绑定无效"));
    s.inputs[it.key()] = {int(node), ref["output"].toString()};
  }
  for (auto key : {"dataA", "dataB", "calibrationSource"})
    if (o.contains(key)) {
      const auto v = number(o, key);
      if (v != std::floor(v) || v < -2 || v > 31)
        invalid(QStringLiteral("类型化输入索引无效"));
      (QString(key) == "dataA"   ? s.dataA
       : QString(key) == "dataB" ? s.dataB
                                 : s.calibrationSource) = int(v);
    }
  if (o.contains("referenceSize")) {
    const auto size = o["referenceSize"].toArray();
    if (size.size() != 2 || size[0].toInt(-2) < -1 || size[1].toInt(-2) < -1)
      invalid(QStringLiteral("参考图像规格无效"));
    s.referenceSize = QSize(size[0].toInt(), size[1].toInt());
    const double pose = number(o, "poseSource");
    if (pose != std::floor(pose) || pose < -2 || pose > 31)
      invalid(QStringLiteral("定位输入无效"));
    s.poseSource = int(pose);
    s.referencePosition = {number(o, "referenceX"), number(o, "referenceY")};
    s.referenceAngle = number(o, "referenceAngle");
    s.iteratePose = o.value("iteratePose").toBool(false);
    s.poseInstance = integer(o, "poseInstance");
    if (s.poseInstance > 99) invalid(QStringLiteral("定位实例无效"));
    s.poseReferenceSet = o["poseReferenceSet"].toBool();
    s.modelId = o["modelId"].toString();
    s.referencePixelKey = o["referencePixelKey"].toString();
    s.referenceModelKey = o["referenceModelKey"].toString();
    if (s.modelId.size() > 128 || o["exclusions"].toArray().size() > 16)
      invalid(QStringLiteral("范围配置过多"));
    for (const auto& value : o["exclusions"].toArray())
      s.exclusions.append(decode(value.toObject()));
  }
  return s;
}
QVector<int> workflowOrder(const Document& d, bool allowDraft) {
  const auto sources = workflowSources(d);
  const auto links = workflowConnections(d);
  QVector<int> order, state(d.workflow.size(), 0);
  std::function<void(int)> visit = [&](int i) {
    if (state[i] == 2) return;
    if (state[i] == 1) invalid(QStringLiteral("连线不能形成循环"));
    state[i] = 1;
    for (int dependency : links[i]) {
      if (dependency < -1 || dependency >= d.workflow.size() || dependency == i)
        invalid(QStringLiteral("节点连线无效"));
      if (dependency >= 0) visit(dependency);
    }
    const int source = sources[i];
    if (!allowDraft && !sourceTool(d.workflow[i].algorithm) && !captureTool(d.workflow[i].algorithm) &&
        (source < -1 || !workflowImageSources(d, i).contains(source)))
      invalid(QStringLiteral("节点 %1 未连接有效图像输入").arg(i + 1));
    state[i] = 2;
    order.append(i);
  };
  for (int i = 0; i < d.workflow.size(); ++i) visit(i);
  return order;
}
QString pixelTypeText(mv::MvPixelType type) {
  switch (type) {
    case mv::MvPixelType::UInt8:
      return "byte";
    case mv::MvPixelType::UInt16:
      return "uint2";
    case mv::MvPixelType::Int16:
      return "int2";
    case mv::MvPixelType::Int32:
      return "int4";
    case mv::MvPixelType::Float32:
      return "real";
  }
  return "unknown";
}
Preview makePreview(const mv::MvImage& image) {
  if (image.CountObj() != 1 || (image.Channels() != 1 && image.Channels() != 3))
    invalid(QStringLiteral("工作台当前显示单张灰度或 RGB 图像"));
  if (image.Width() > INT_MAX || image.Height() > INT_MAX)
    invalid(QStringLiteral("图像尺寸超出显示范围"));
  const int width = static_cast<int>(image.Width()),
            height = static_cast<int>(image.Height());
  std::vector<Plane> planes;
  bool byteOnly = true;
  for (std::size_t i = 0; i < image.Channels(); ++i) {
    planes.push_back({static_cast<const unsigned char*>(image.Data(i)),
                      image.Stride(i), image.Type(i)});
    byteOnly &= image.Type(i) == mv::MvPixelType::UInt8;
  }
  double minimum = 0, maximum = 255;
  if (!byteOnly) {
    minimum = std::numeric_limits<double>::infinity();
    maximum = -minimum;
    for (const auto& plane : planes)
      for (int r = 0; r < height; ++r)
        for (int c = 0; c < width; ++c) {
          const double v = sample(plane, r, c);
          if (std::isfinite(v)) {
            minimum = std::min(minimum, v);
            maximum = std::max(maximum, v);
          }
        }
    if (!std::isfinite(minimum)) {
      minimum = 0;
      maximum = 0;
    }
  }
  QImage result(width, height, QImage::Format_RGB888);
  if (result.isNull()) invalid(QStringLiteral("无法分配图像显示缓冲区"));
  if (byteOnly) {
    // byte pixels already occupy the display range. Avoid three floating
    // point conversions, finite checks and rounding operations per pixel.
    if (planes.size() == 1) {
      const QImage gray(planes[0].data, width, height, int(planes[0].stride),
                        QImage::Format_Grayscale8);
      result = gray.convertToFormat(QImage::Format_RGB888);
    } else {
      for (int r = 0; r < height; ++r) {
        auto* out = result.scanLine(r);
        const auto* red = planes[0].data + r * planes[0].stride;
        const auto* green = planes[1].data + r * planes[1].stride;
        const auto* blue = planes[2].data + r * planes[2].stride;
        for (int c = 0; c < width; ++c) {
          out[c * 3] = red[c];
          out[c * 3 + 1] = green[c];
          out[c * 3 + 2] = blue[c];
        }
      }
    }
    if (result.isNull()) invalid(QStringLiteral("无法分配图像显示缓冲区"));
    return {result, QStringLiteral("原始 byte 显示")};
  }
  for (int r = 0; r < height; ++r) {
    auto* out = result.scanLine(r);
    for (int c = 0; c < width; ++c)
      for (int channel = 0; channel < 3; ++channel) {
        const double v = sample(planes[planes.size() == 1 ? 0 : channel], r, c);
        const double mapped = !std::isfinite(v) || maximum == minimum
                                  ? 0
                                  : (v - minimum) * 255.0 / (maximum - minimum);
        out[c * 3 + channel] = static_cast<unsigned char>(
            std::clamp(std::round(mapped), 0.0, 255.0));
      }
  }
  return {result,
          byteOnly ? QStringLiteral("原始 byte 显示")
                   : QStringLiteral(
                         "仅显示映射 [%1, %2] → [0, 255]；非有限值显示为黑色")
                         .arg(minimum, 0, 'g', 6)
                         .arg(maximum, 0, 'g', 6)};
}
QString pixelText(const mv::MvImage& image, int row, int column) {
  if (row < 0 || column < 0 || row >= static_cast<int>(image.Height()) ||
      column >= static_cast<int>(image.Width()))
    return {};
  QStringList values;
  for (std::size_t i = 0; i < image.Channels(); ++i) {
    const Plane plane{static_cast<const unsigned char*>(image.Data(i)),
                      image.Stride(i), image.Type(i)};
    values << QString::number(sample(plane, row, column), 'g', 9);
  }
  return values.join(", ");
}
void validateRoi(const Roi& roi, int width, int height) {
  if (roi.kind == RoiKind::None) return;
  const auto& b = roi.bounds;
  if ((int(roi.kind) < 1 || int(roi.kind) > 4) || !std::isfinite(b.x()) ||
      !std::isfinite(b.y()) || !std::isfinite(b.width()) ||
      !std::isfinite(b.height()) || !std::isfinite(roi.angle) ||
      std::abs(roi.angle) > 180 || !std::isfinite(roi.innerRatio) ||
      roi.innerRatio <= 0 || roi.innerRatio >= 1 || b.width() < 1 ||
      b.height() < 1 || b.left() < 0 || b.top() < 0 || b.right() > width - 1 ||
      b.bottom() > height - 1 ||
      ((roi.kind == RoiKind::Circle || roi.kind == RoiKind::Annulus) &&
       std::abs(b.width() - b.height()) > 1e-6))
    invalid(QStringLiteral(
        "ROI 必须位于图像内，宽高至少为 1；圆 ROI 的宽高须相等"));
}
mv::MvRegion makeRegion(const Roi& roi) {
  mv::MvRegion region;
  if (roi.kind == RoiKind::Rectangle)
    region.GenRectangle1(roi.bounds.top(), roi.bounds.left(),
                         roi.bounds.bottom(), roi.bounds.right());
  else if (roi.kind == RoiKind::RotatedRectangle)
    region.GenRectangle2(roi.bounds.center().y(), roi.bounds.center().x(),
                         -roi.angle * 3.14159265358979323846 / 180,
                         roi.bounds.width() / 2, roi.bounds.height() / 2);
  else if (roi.kind == RoiKind::Annulus) {
    region.GenCircle(roi.bounds.center().y(), roi.bounds.center().x(),
                     roi.bounds.width() / 2);
    mv::MvRegion inner;
    inner.GenCircle(roi.bounds.center().y(), roi.bounds.center().x(),
                    roi.bounds.width() / 2 * roi.innerRatio);
    region = region.Difference(inner);
  } else if (roi.kind == RoiKind::Circle)
    region.GenCircle(roi.bounds.center().y(), roi.bounds.center().x(),
                     roi.bounds.width() / 2);
  else
    invalid(QStringLiteral("没有有效 ROI"));
  return region;
}
mv::MvRegion regionMask(const mv::MvRegion& regions) {
  if (regions.CountObj()) return regions.Union1();
  mv::MvRegion empty;
  empty.GenRegionRuns(mv::MvTuple{}, mv::MvTuple{}, mv::MvTuple{});
  return empty;
}
Document loadImage(const QString& path) {
  Document document;
  document.imagePath = QFileInfo(path).absoluteFilePath();
  document.coordinateFrame = document.imagePath;
  document.image = readInputImage(document.imagePath);
  document.pixelKey = referenceImageKey(document.image);
  const auto preview = makePreview(document.image);
  document.preview = preview.image;
  document.displayMapping = preview.mapping;
  return document;
}
QJsonObject parametersJson(const Parameters& p, const QString& path) {
  QJsonObject calibrationAsset;
  if (!p.calibrationArtifact.isEmpty()) {
    const auto key =
        QString::fromLatin1(QCryptographicHash::hash(p.calibrationArtifact,
                                                     QCryptographicHash::Sha256)
                                .toHex());
    const auto folder = QFileInfo(path).absoluteDir().filePath(
        QFileInfo(path).fileName() + ".assets");
    if (!QDir().mkpath(folder)) invalid(QStringLiteral("无法创建标定资源目录"));
    const auto asset = folder + "/" + key + ".mvcalib";
    QSaveFile file(asset);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(p.calibrationArtifact) != p.calibrationArtifact.size() ||
        !file.commit())
      invalid(QStringLiteral("标定资源保存失败"));
    calibrationAsset = {
        {"file", QFileInfo(path).absoluteDir().relativeFilePath(asset)},
        {"sha256", key}};
  }
  return QJsonObject{
           {"algorithm", operatorDefinition(p.algorithm).key},
           {"options", p.options},
           {"calibrationName", p.calibrationName},
           {"calibrationConfig",
            calibrationConfigJson(p.calibrationConfig, path, true)},
           {"calibrationAsset", calibrationAsset},
           {"filter", p.filter},
           {"mask", p.mask},
           {"gray", p.gray},
           {"template", p.templatePath.isEmpty()
                            ? QString()
                            : QFileInfo(path).absoluteDir().relativeFilePath(
                                  p.templatePath)},
           {"score", p.score},
           {"matches", p.matches},
           {"angleStart", p.angleStart},
           {"angleExtent", p.angleExtent},
           {"minimum", p.minimum},
           {"maximum", p.maximum},
           {"opening", p.opening},
           {"minimumArea", p.minimumArea},
           {"sigma", p.sigma},
           {"edgeLow", p.edgeLow},
           {"edgeHigh", p.edgeHigh}};
}
Parameters parametersFromJson(const QJsonObject& values, const QString& path, bool draft) {
  Parameters parameters;
  const auto algorithm = values["algorithm"].toString();
  parameters.algorithm = algorithmFromKey(algorithm);
  if (values.contains("filter")) {
    parameters.filter = integer(values, "filter");
    parameters.mask = integer(values, "mask");
    parameters.gray = integer(values, "gray");
    parameters.score = number(values, "score");
    parameters.matches = integer(values, "matches");
    parameters.angleStart = number(values, "angleStart");
    parameters.angleExtent = number(values, "angleExtent");
    const auto model = values["template"].toString();
    if (!model.isEmpty())
      parameters.templatePath =
          QFileInfo(path).absoluteDir().absoluteFilePath(model);
  }
  parameters.minimum = number(values, "minimum");
  parameters.maximum = number(values, "maximum");
  parameters.opening = number(values, "opening");
  parameters.minimumArea = number(values, "minimumArea");
  parameters.sigma = number(values, "sigma");
  parameters.edgeLow = integer(values, "edgeLow");
  parameters.edgeHigh = integer(values, "edgeHigh");
  parameters.options = values["options"].toObject();
  // Older projects shared this label with a retired tool.
  parameters.calibrationName = values["calibrationName"].toString(
      values["comparisonName"].toString());
  parameters.calibrationConfig = calibrationConfigJson(
      values["calibrationConfig"].toObject(), path, false);
  const auto calibBytes = values["calibrationArtifact"].toString().toLatin1();
  if (calibBytes.size() > 12 * 1024 * 1024)
    invalid(QStringLiteral("标定资源过大"));
  parameters.calibrationArtifact = QByteArray::fromBase64(calibBytes);
  if (!values["calibrationAsset"].toObject().isEmpty()) {
    const auto asset = values["calibrationAsset"].toObject();
    QFile file(QFileInfo(path).absoluteDir().absoluteFilePath(
        asset["file"].toString()));
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 ||
        file.size() > 8 * 1024 * 1024)
      invalid(QStringLiteral("标定资源缺失或过大"));
    parameters.calibrationArtifact = file.readAll();
    if (QString::fromLatin1(
            QCryptographicHash::hash(parameters.calibrationArtifact,
                                     QCryptographicHash::Sha256)
                .toHex()) != asset["sha256"].toString())
      invalid(QStringLiteral("标定资源校验失败"));
  }
  if (!calibBytes.isEmpty() &&
      parameters.calibrationArtifact.toBase64() != calibBytes)
    invalid(QStringLiteral("标定资源编码无效"));
  if (!draft) parameters.validate();
  return parameters;
}
QJsonObject projectJson(const Document& document, const QString& path, bool draft) {
  if (!document.hasImage()) invalid(QStringLiteral("请先打开图像"));
  if (!draft) document.parameters.validate();
  validateRoi(document.roi, document.preview.width(),
              document.preview.height());
  const auto& r = document.roi;
  return {
      {"sourceInputs", sourceInputsJson(document, path)},
      {"capture", captureJson(document, path)},
      {"format", "mvkit-workbench"},
      {"version", 1},
      {"image",
       QFileInfo(path).absoluteDir().relativeFilePath(document.imagePath)},
      {"parameters", parametersJson(document.parameters, path)},
      {"roi", QJsonObject{{"kind", r.kind == RoiKind::None        ? "none"
                                   : r.kind == RoiKind::Rectangle ? "rectangle"
                                   : r.kind == RoiKind::RotatedRectangle
                                       ? "rotated_rectangle"
                                   : r.kind == RoiKind::Annulus ? "annulus"
                                                                : "circle"},
                          {"angle", r.angle},
                          {"innerRatio", r.innerRatio},
                          {"x", r.bounds.x()},
                          {"y", r.bounds.y()},
                          {"width", r.bounds.width()},
                          {"height", r.bounds.height()}}},
      {"rangeOverrides",
       [&] {
         QJsonArray a;
         for (bool v : document.rangeOverrides) a.append(v);
         return a;
       }()},
      {"workbenchRanges",
       [&] {
         QJsonArray a;
         for (const auto& s : document.workbenchRanges)
           a.append(nodeSettingsJson(s));
         return a;
       }()},
      {"workbenchOverrides",
       [&] {
         QJsonArray a;
         for (bool v : document.workbenchOverrides) a.append(v);
         return a;
       }()},
      {"nodeSettings", [&] {
         QJsonArray array;
         for (const auto& s : document.nodeSettings)
           array.append(nodeSettingsJson(s));
         return array;
       }()}};
}
void saveProject(const Document& document, const QString& path) {
  const auto bytes = QJsonDocument(projectJson(document, path)).toJson();
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
      !file.commit())
    invalid(file.errorString());
}
Document loadProject(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) invalid(file.errorString());
  if (file.size() > metadataByteLimit) invalid(QStringLiteral("工程文件过大"));
  QJsonParseError error;
  const auto json = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !json.isObject())
    invalid(QStringLiteral("工程 JSON 无效：") + error.errorString());
  return documentFromJson(json.object(), path);
}
Document documentFromJson(const QJsonObject& root, const QString& path, bool draft) {
  if (root["format"] != "mvkit-workbench" || root["version"] != 1)
    invalid(QStringLiteral("不支持的工程格式或版本"));
  const auto imagePath = root["image"].toString();
  if (imagePath.isEmpty()) invalid(QStringLiteral("工程缺少图像路径"));
  const auto parameters = parametersFromJson(root["parameters"].toObject(), path, draft);
  const auto roiValues = root["roi"].toObject();
  const auto kind = roiValues["kind"].toString();
  if (kind != "none" && kind != "rectangle" && kind != "circle" &&
      kind != "rotated_rectangle" && kind != "annulus")
    invalid(QStringLiteral("工程 ROI 类型无效"));
  Roi roi{kind == "none"                ? RoiKind::None
          : kind == "rectangle"         ? RoiKind::Rectangle
          : kind == "rotated_rectangle" ? RoiKind::RotatedRectangle
          : kind == "annulus"           ? RoiKind::Annulus
                                        : RoiKind::Circle,
          QRectF(number(roiValues, "x"), number(roiValues, "y"),
                 number(roiValues, "width"), number(roiValues, "height")),
          roiValues.value("angle").toDouble(0),
          roiValues.value("innerRatio").toDouble(.5)};
  auto document =
      loadImage(QFileInfo(path).absoluteDir().absoluteFilePath(imagePath));
  validateRoi(roi, document.preview.width(), document.preview.height());
  readSourceInputs(document, root["sourceInputs"].toObject(), path);
  document.parameters = parameters;
  document.roi = roi;
  for (const auto& v : root["rangeOverrides"].toArray()) {
    if (!v.isBool()) invalid(QStringLiteral("范围覆盖标识无效"));
    document.rangeOverrides.append(v.toBool());
  }
  for (const auto& v : root["workbenchOverrides"].toArray()) {
    if (!v.isBool()) invalid(QStringLiteral("范围覆盖标识无效"));
    document.workbenchOverrides.append(v.toBool());
  }
  for (const auto& v : root["workbenchRanges"].toArray())
    document.workbenchRanges.append(nodeSettingsFromJson(v.toObject()));
  if (document.rangeOverrides.size() > 32 ||
      document.workbenchOverrides.size() > toolSlotCount ||
      document.workbenchRanges.size() > toolSlotCount)
    invalid(QStringLiteral("范围覆盖数量无效"));
  if (root.contains("nodeSettings")) {
    if (!root["nodeSettings"].isArray() ||
        root["nodeSettings"].toArray().size() > 32)
      invalid(QStringLiteral("节点范围数量无效"));
    for (const auto& value : root["nodeSettings"].toArray()) {
      if (!value.isObject()) invalid(QStringLiteral("节点范围无效"));
      document.nodeSettings.append(nodeSettingsFromJson(value.toObject()));
    }
  }
  restoreCapture(document, root["capture"].toObject(), path);
  return document;
}
QPainterPath roiPath(const Roi& roi) {
  QPainterPath path;
  if (roi.kind == RoiKind::Rectangle || roi.kind == RoiKind::RotatedRectangle)
    path.addRect(roi.bounds);
  else if (roi.kind == RoiKind::Circle || roi.kind == RoiKind::Annulus)
    path.addEllipse(roi.bounds);
  if (roi.kind == RoiKind::Annulus) {
    const auto c = roi.bounds.center();
    double r = roi.bounds.width() / 2 * roi.innerRatio;
    path.addEllipse(c, r, r);
    path.setFillRule(Qt::OddEvenFill);
  }
  if (roi.kind == RoiKind::RotatedRectangle) {
    QTransform t;
    t.translate(roi.bounds.center().x(), roi.bounds.center().y());
    t.rotate(roi.angle);
    t.translate(-roi.bounds.center().x(), -roi.bounds.center().y());
    path = t.map(path);
  }
  return path;
}
QString roiText(const Roi& roi) {
  if (roi.kind == RoiKind::None) return QStringLiteral("全图");
  const auto& b = roi.bounds;
  if (roi.kind == RoiKind::RotatedRectangle)
    return QStringLiteral("旋转矩形 · 中心 (%1, %2) · %3 × %4 · %5°")
        .arg(b.center().y(), 0, 'f', 1)
        .arg(b.center().x(), 0, 'f', 1)
        .arg(b.width(), 0, 'f', 1)
        .arg(b.height(), 0, 'f', 1)
        .arg(roi.angle, 0, 'f', 1);
  if (roi.kind == RoiKind::Annulus)
    return QStringLiteral("环形 · 内半径 %1 / 外半径 %2 px")
        .arg(b.width() / 2 * roi.innerRatio, 0, 'f', 1)
        .arg(b.width() / 2, 0, 'f', 1);
  if (roi.kind == RoiKind::Circle)
    return QStringLiteral("圆 · Row %1 / Col %2 / R %3")
        .arg(b.center().y(), 0, 'f', 1)
        .arg(b.center().x(), 0, 'f', 1)
        .arg(b.width() / 2, 0, 'f', 1);
  return QStringLiteral("矩形 · (%1, %2) – (%3, %4)")
      .arg(b.top(), 0, 'f', 0)
      .arg(b.left(), 0, 'f', 0)
      .arg(b.bottom(), 0, 'f', 0)
      .arg(b.right(), 0, 'f', 0);
}
}  // namespace workbench
