// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "runtime/worker_protocol.hpp"
#include "model/workflow_ports.hpp"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

#include "operators/operator_registry.hpp"
#include "model/spatial_data.hpp"
namespace workbench {
namespace {
constexpr quint32 magic = 0x4d564b46;
constexpr qint64 byteLimit = 1024LL * 1024 * 1024;
void ensure(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
// QDataStream's QImage operator encodes PNG. These local task snapshots
// need lossless pixels, not compression; row transport also avoids padding
// bytes and keeps RGB layout explicit on both sides of the private protocol.
void preview(QDataStream& out, const QImage& value) {
  out << !value.isNull();
  if (value.isNull()) return;
  const auto pixels = value.convertToFormat(QImage::Format_RGB888);
  ensure(!pixels.isNull(), "无法分配预览传输缓冲");
  const qint64 rowBytes = qint64(pixels.width()) * 3;
  ensure(rowBytes <= INT_MAX && rowBytes <= byteLimit / pixels.height(),
         "预览数据过大");
  out << qint32(pixels.width()) << qint32(pixels.height())
      << pixels.devicePixelRatio();
  for (int row = 0; row < pixels.height(); ++row)
    ensure(out.writeRawData(
               reinterpret_cast<const char*>(pixels.constScanLine(row)),
               int(rowBytes)) == rowBytes,
           "预览数据写入失败");
}
QImage preview(QDataStream& in) {
  bool present = false;
  in >> present;
  if (!present) return {};
  qint32 width = 0, height = 0;
  double scale = 0;
  in >> width >> height >> scale;
  const qint64 rowBytes = qint64(width) * 3;
  ensure(width > 0 && height > 0 && rowBytes <= INT_MAX &&
             rowBytes <= byteLimit / height && std::isfinite(scale) &&
             scale > 0,
         "预览数据规格无效");
  QImage pixels(width, height, QImage::Format_RGB888);
  ensure(!pixels.isNull(), "无法分配预览缓冲");
  pixels.setDevicePixelRatio(scale);
  for (int row = 0; row < height; ++row)
    ensure(in.readRawData(reinterpret_cast<char*>(pixels.scanLine(row)),
                          int(rowBytes)) == rowBytes,
           "预览像素数据不完整");
  return pixels;
}
void parameters(QDataStream& out, const Parameters& p) {
  out << qint32(p.algorithm) << p.minimum << p.maximum << p.opening
      << p.minimumArea << p.sigma << qint32(p.edgeLow) << qint32(p.edgeHigh)
      << qint32(p.filter) << qint32(p.mask) << qint32(p.gray) << p.templatePath
      << p.score << qint32(p.matches) << p.angleStart << p.angleExtent
      << p.options << p.calibrationName
      << p.calibrationConfig << p.calibrationArtifact;
}
Parameters parameters(QDataStream& in) {
  Parameters p;
  qint32 algorithm, low, high, filter, mask, gray, matches;
  in >> algorithm >> p.minimum >> p.maximum >> p.opening >> p.minimumArea >>
      p.sigma >> low >> high >> filter >> mask >> gray >> p.templatePath >>
      p.score >> matches >> p.angleStart >> p.angleExtent >> p.options >>
      p.calibrationName >> p.calibrationConfig >>
      p.calibrationArtifact;
  ensure(in.status() == QDataStream::Ok, "工作进程参数数据不完整");
  ensure(algorithm >= 0 && algorithm < toolSlotCount,
         "工作进程算法类型无效");
  p.algorithm = static_cast<Algorithm>(algorithm);
  operatorDefinition(p.algorithm);
  p.edgeLow = low;
  p.edgeHigh = high;
  p.filter = filter;
  p.mask = mask;
  p.gray = gray;
  p.matches = matches;
  return p;
}
void roi(QDataStream& out, const Roi& r) {
  out << qint32(r.kind) << r.bounds << r.angle << r.innerRatio;
}
Roi roi(QDataStream& in) {
  qint32 kind;
  QRectF bounds;
  double angle, inner;
  in >> kind >> bounds >> angle >> inner;
  ensure(kind >= 0 && kind <= 4, "工作进程 ROI 类型无效");
  return {static_cast<RoiKind>(kind), bounds, angle, inner};
}
quint32 count(QDataStream& in, quint32 maximum) {
  quint32 value = 0;
  in >> value;
  ensure(in.status() == QDataStream::Ok && value <= maximum,
         "工作进程对象数量无效");
  return value;
}
void image(QDataStream& out, const mv::MvImage& image) {
  const bool present = image.CountObj() != 0;
  out << present;
  if (!present) return;
  ensure(image.CountObj() == 1, "工作进程仅支持单图像对象");
  const qint64 width = static_cast<qint64>(image.Width()),
               height = static_cast<qint64>(image.Height());
  const int channels = static_cast<int>(image.Channels());
  ensure(width > 0 && height > 0 && width <= INT_MAX && height <= INT_MAX &&
             (channels == 1 || channels == 3),
         "工作进程图像尺寸无效");
  out << qint32(width) << qint32(height) << qint32(channels);
  for (int channel = 0; channel < channels; ++channel) {
    const auto type = image.Type(channel);
    const int bytes =
        type == mv::MvPixelType::UInt8 ? 1
        : (type == mv::MvPixelType::UInt16 || type == mv::MvPixelType::Int16)
            ? 2
            : 4;
    const qint64 rowBytes = width * bytes;
    ensure(rowBytes * height <= byteLimit, "工作进程单平面图像超过 1 GiB");
    out << qint32(type) << qint64(rowBytes * height);
    const auto* data = static_cast<const char*>(image.Data(channel));
    for (qint64 row = 0; row < height; ++row)
      ensure(out.writeRawData(data + row * image.Stride(channel),
                              int(rowBytes)) == rowBytes,
             "写入图像快照失败");
  }
  mv::MvTuple rows, starts, ends;
  image.GetDomain().GetRegionRuns(&rows, &starts, &ends);
  out << quint32(rows.Length());
  for (std::size_t i = 0; i < rows.Length(); ++i)
    out << qint64(rows.I(i)) << qint64(starts.I(i)) << qint64(ends.I(i));
}
mv::MvImage image(QDataStream& in) {
  bool present = false;
  in >> present;
  if (!present) return {};
  qint32 width = 0, height = 0, channels = 0;
  in >> width >> height >> channels;
  ensure(width > 0 && height > 0 && (channels == 1 || channels == 3),
         "工作进程图像尺寸无效");
  std::array<mv::MvImage, 3> planes;
  for (int channel = 0; channel < channels; ++channel) {
    qint32 type = -1;
    qint64 size = -1;
    in >> type >> size;
    ensure(type >= 0 && type <= 4, "工作进程像素类型无效");
    const int bytes = type == 0 ? 1 : (type == 1 || type == 2) ? 2 : 4;
    ensure(
        size == qint64(width) * height * bytes && size > 0 && size <= byteLimit,
        "工作进程像素缓冲无效");
    QByteArray pixels(int(size), Qt::Uninitialized);
    ensure(in.readRawData(pixels.data(), int(size)) == size,
           "工作进程像素数据不完整");
    const auto name =
        pixelTypeText(static_cast<mv::MvPixelType>(type)).toLatin1();
    planes[channel].GenImage1(name.constData(), width, height,
                              pixels.constData());
  }
  auto result =
      channels == 1 ? planes[0] : planes[0].Compose3(planes[1], planes[2]);
  const auto runs = count(in, 16000000);
  mv::MvTuple rows, starts, ends;
  for (quint32 i = 0; i < runs; ++i) {
    qint64 row, first, last;
    in >> row >> first >> last;
    ensure(
        row >= 0 && row < height && first >= 0 && last >= first && last < width,
        "工作进程定义域无效");
    rows.Append(std::int64_t(row));
    starts.Append(std::int64_t(first));
    ends.Append(std::int64_t(last));
  }
  mv::MvRegion domain;
  domain.GenRegionRuns(rows, starts, ends);
  return result.ReduceDomain(domain);
}
void objects(QDataStream& out, const QVector<ResultObject>& objects) {
  out << quint32(objects.size());
  for (const auto& o : objects)
    out << o.type << o.measure << o.center << o.overlay << o.angle << o.first
        << o.second << o.radius << o.values << o.label << o.unit
        << qint32(o.verdict) << o.radius2 << qint32(o.instance) << o.samples
        << o.residuals;
}
QVector<ResultObject> objects(QDataStream& in) {
  QVector<ResultObject> result;
  const auto size = count(in, 10000);
  result.reserve(int(size));
  for (quint32 i = 0; i < size; ++i) {
    ResultObject o;
    qint32 verdict;
    in >> o.type >> o.measure >> o.center >> o.overlay >> o.angle >> o.first >>
        o.second >> o.radius >> o.values >> o.label >> o.unit >> verdict >>
        o.radius2 >> o.instance >> o.samples >> o.residuals;
    ensure(o.values.size() <= 32 && o.label.size() <= 256 &&
               std::isfinite(o.radius) && verdict >= -1 && verdict <= 1,
           "几何结果无效");
    ensure(o.samples.size() <= 2000000 &&
               o.samples.size() == o.residuals.size() && o.instance >= 0 &&
               o.instance <= 100 && std::isfinite(o.radius2),
           "拟合证据或目标编号无效");
    o.verdict = verdict;
    for (double v : o.values) ensure(std::isfinite(v), "几何数值无效");
    result.append(std::move(o));
  }
  return result;
}
void nativeObjects(QDataStream& out, const mv::MvRegion& regions,
                   const mv::MvXLDCont& contours) {
  out << quint32(regions.CountObj());
  for (std::int64_t i = 1; i <= regions.CountObj(); ++i) {
    mv::MvTuple rows, starts, ends;
    regions.SelectObj(i).GetRegionRuns(&rows, &starts, &ends);
    out << quint32(rows.Length());
    for (std::size_t j = 0; j < rows.Length(); ++j)
      out << qint64(rows.I(j)) << qint64(starts.I(j)) << qint64(ends.I(j));
  }
  out << quint32(contours.CountObj());
  for (std::int64_t i = 1; i <= contours.CountObj(); ++i) {
    mv::MvTuple rows, columns;
    contours.SelectObj(i).GetContourXld(&rows, &columns);
    out << quint32(rows.Length());
    for (std::size_t j = 0; j < rows.Length(); ++j)
      out << rows.D(j) << columns.D(j);
  }
}
void nativeObjects(QDataStream& in, mv::MvRegion& regions,
                   mv::MvXLDCont& contours) {
  const auto regionCount = count(in, 10000);
  quint32 budget = 0;
  for (quint32 i = 0; i < regionCount; ++i) {
    const auto size = count(in, 2000000);
    ensure((budget += size) <= 2000000, "区域数据超过预算");
    mv::MvTuple rows, starts, ends;
    for (quint32 j = 0; j < size; ++j) {
      qint64 r, a, b;
      in >> r >> a >> b;
      ensure(r >= INT_MIN && r <= INT_MAX && a >= INT_MIN && b <= INT_MAX &&
                 b >= a,
             "区域游程无效");
      rows.Append(std::int64_t(r));
      starts.Append(std::int64_t(a));
      ends.Append(std::int64_t(b));
    }
    mv::MvRegion region;
    region.GenRegionRuns(rows, starts, ends);
    regions = i == 0 ? region : regions.ConcatObj(region);
  }
  const auto contourCount = count(in, 10000);
  budget = 0;
  for (quint32 i = 0; i < contourCount; ++i) {
    const auto size = count(in, 2000000);
    ensure((budget += size) <= 2000000, "轮廓数据超过预算");
    mv::MvTuple rows, columns;
    for (quint32 j = 0; j < size; ++j) {
      double r, c;
      in >> r >> c;
      ensure(std::isfinite(r) && std::isfinite(c), "轮廓坐标无效");
      rows.Append(r);
      columns.Append(c);
    }
    mv::MvXLDCont contour;
    contour.GenContourPolygonXld(rows, columns);
    contours = i == 0 ? contour : contours.ConcatObj(contour);
  }
}
void open(QFile& file) {
  ensure(file.open(QIODevice::ReadOnly), "无法读取工作进程数据");
  ensure(file.size() <= byteLimit, "工作进程数据超过 1 GiB");
}
void header(QDataStream& in) {
  in.setVersion(QDataStream::Qt_5_15);
  quint32 value = 0;
  in >> value;
  ensure(value == magic, "工作进程协议版本不匹配");
}
void commit(QSaveFile& file, QDataStream& out) {
  ensure(out.status() == QDataStream::Ok && file.commit(),
         "无法保存工作进程数据");
}
}  // namespace
QByteArray encodeImage(const mv::MvImage& value) {
  QByteArray bytes;
  QDataStream out(&bytes, QIODevice::WriteOnly);
  out.setVersion(QDataStream::Qt_5_15);
  image(out, value);
  ensure(out.status() == QDataStream::Ok, "图像资源编码失败");
  return bytes;
}
mv::MvImage decodeImage(const QByteArray& bytes) {
  ensure(bytes.size() <= 128 * 1024 * 1024, "图像资源过大");
  QDataStream in(bytes);
  in.setVersion(QDataStream::Qt_5_15);
  auto result = image(in);
  ensure(in.status() == QDataStream::Ok && in.atEnd(), "图像资源损坏");
  return result;
}
void writeWorkerRequest(const QString& path, const Document& d) {
  QSaveFile file(path);
  ensure(file.open(QIODevice::WriteOnly), "无法创建任务快照");
  QDataStream out(&file);
  out.setVersion(QDataStream::Qt_5_15);
  out << magic << d.imagePath << d.displayMapping << d.pixelKey;
  image(out, d.image);
  preview(out, d.preview);
  roi(out, d.roi);
  parameters(out, d.parameters);
  out << d.runWorkflow << quint32(d.workflow.size());
  for (const auto& p : d.workflow) parameters(out, p);
  out << d.workflowInputs << d.workflowLinks;
  out << qint32(d.workflowOutput) << qint32(d.workflowTarget) << d.workflowScope
      << d.workflowLabels << quint32(d.nodeSettings.size());
  for (const auto& s : d.nodeSettings) {
    out << QJsonDocument(nodeSettingsJson(s)).toJson(QJsonDocument::Compact);
  }
  out << quint32(d.sharedNodeSettings.size());
  for (const auto& s : d.sharedNodeSettings)
    out << QJsonDocument(nodeSettingsJson(s)).toJson(QJsonDocument::Compact);
  out << d.rangeOverrides << d.applyActiveRange << d.modelsRequired
      << d.modelBinding << d.coordinateFrame
      << QJsonDocument(nodeSettingsJson(d.activeRange))
             .toJson(QJsonDocument::Compact)
      << quint32(d.models.size());
  for (const auto& m : d.models) {
    out << QJsonDocument(modelSettingsJson(m)).toJson(QJsonDocument::Compact);
    image(out, m.referenceImage);
    out << m.artifact << m.outline;
  }
  out << d.calibrationJob << d.calibrationInputs << d.captureId << d.rightPath << d.rightPixelKey
      << d.stereoArtifact;
  image(out, d.rightImage);
  preview(out, d.rightPreview);
  out << d.structuredModel << d.structuredPaths << d.structuredKeys << quint32(d.structuredFrames.size());
  for (const auto& frame : d.structuredFrames) image(out, frame);
  out << quint32(d.sourceInputs.size());
  for (auto it = d.sourceInputs.begin(); it != d.sourceInputs.end(); ++it) {
    out << it.key() << it->sequence << it->views << it->paths << it->keys << quint32(it->frames.size());
    for (const auto& frame : it->frames) image(out, frame);
  }
  commit(file, out);
}
Document readWorkerRequest(const QString& path) {
  QFile file(path);
  open(file);
  QDataStream in(&file);
  header(in);
  Document d;
  in >> d.imagePath >> d.displayMapping >> d.pixelKey;
  d.image = image(in);
  d.preview = preview(in);
  d.roi = roi(in);
  d.parameters = parameters(in);
  in >> d.runWorkflow;
  const auto size = count(in, 32);
  for (quint32 i = 0; i < size; ++i) d.workflow.append(parameters(in));
  in >> d.workflowInputs >> d.workflowLinks;
  qint32 output;
  in >> output;
  d.workflowOutput = output;
  qint32 target;
  in >> target >> d.workflowScope >> d.workflowLabels;
  ensure(d.workflowScope.size()<=int(size),"流程运行范围过大");
  QSet<int> scoped;for(int node:d.workflowScope){ensure(node>=0 && node<int(size) && !scoped.contains(node),"流程运行范围无效");scoped.insert(node);}
  d.workflowTarget = target;
  ensure(
      target >= -1 && target < int(size) &&
          (d.workflowLabels.isEmpty() || d.workflowLabels.size() == int(size)),
      "节点调试目标或名称无效");
  const auto settingsCount = count(in, 32);
  ensure(!settingsCount || settingsCount == size, "节点范围数量不匹配");
  for (quint32 i = 0; i < settingsCount; ++i) {
    QByteArray bytes;
    in >> bytes;
    d.nodeSettings.append(
        nodeSettingsFromJson(QJsonDocument::fromJson(bytes).object()));
  }
  const auto shared = count(in, 32);
  ensure(!shared || shared == size, "公共范围数量无效");
  for (quint32 i = 0; i < shared; ++i) {
    QByteArray bytes;
    in >> bytes;
    d.sharedNodeSettings.append(
        nodeSettingsFromJson(QJsonDocument::fromJson(bytes).object()));
  }
  QByteArray active;
  in >> d.rangeOverrides >> d.applyActiveRange >> d.modelsRequired >>
      d.modelBinding >> d.coordinateFrame >> active;
  d.activeRange =
      nodeSettingsFromJson(QJsonDocument::fromJson(active).object());
  const auto models = count(in, 1024);
  for (quint32 i = 0; i < models; ++i) {
    QByteArray bytes;
    in >> bytes;
    auto m = modelSettingsFromJson(QJsonDocument::fromJson(bytes).object());
    m.referenceImage = image(in);
    if (m.referenceImage.CountObj())
      m.preview = makePreview(m.referenceImage).image;
    in >> m.artifact >> m.outline;
    d.models.append(m);
  }
  in >> d.calibrationJob >> d.calibrationInputs >> d.captureId >> d.rightPath >> d.rightPixelKey >>
      d.stereoArtifact;
  d.rightImage = image(in);
  d.rightPreview = preview(in);
  in >> d.structuredModel >> d.structuredPaths >> d.structuredKeys;
  const auto frames = count(in, 64);
  for (quint32 i = 0; i < frames; ++i) d.structuredFrames.append(image(in));
  const auto columns = count(in, 32);
  qint64 inputPixels = 0;
  for (quint32 i = 0; i < columns; ++i) {
    QString key; SourceData cell;
    in >> key >> cell.sequence >> cell.views >> cell.paths >> cell.keys;
    ensure(!key.isEmpty() && key.size() <= 128 && !d.sourceInputs.contains(key), "样本输入列重复或无效");
    const auto cellSize = count(in, 64);
    ensure(cellSize == quint32(cell.paths.size()) && cellSize == quint32(cell.keys.size()) && (cell.sequence && cell.views.isEmpty() || cell.views.isEmpty() && cellSize <= 1 || !cell.sequence && cellSize<=8 && cellSize==quint32(cell.views.size()) && QSet<QString>(cell.views.begin(),cell.views.end()).size()==cell.views.size()), "输入帧数不匹配");
    for (quint32 f = 0; f < cellSize; ++f) {
      const auto frame = image(in);
      ensure(frame.CountObj() == 1, "输入帧为空");
      inputPixels += frame.Width() * frame.Height();
      ensure(inputPixels <= 128000000, "样本输入过大");
      cell.frames.append(frame);
    }
    d.sourceInputs.insert(key, cell);
  }
  if (!d.structuredFrames.isEmpty()) validateStructuredCapture(d);
  else if (d.rightImage.CountObj()) {
    // Raw pairs may be loaded before a processing node has a calibration.
    validateStereoCapture(d,d.stereoArtifact);
  }
  if (d.runWorkflow) workflowOrder(d, true);
  ensure(in.status() == QDataStream::Ok && in.atEnd(), "任务快照损坏");
  return d;
}
void writeWorkerResult(const QString& path, const Result& r) {
  QSaveFile file(path);
  ensure(file.open(QIODevice::WriteOnly), "无法创建任务结果");
  QDataStream out(&file);
  out.setVersion(QDataStream::Qt_5_15);
  out << magic << r.calibrationResult << r.error << r.pipeline << r.measureName
      << r.algorithmMs << r.totalMs << r.steps << qint32(r.outputNode)
      << quint32(r.nodeOutputs.size());
  if (r.nodeOutputs.isEmpty()) {
    out << encodeSpatial(r.spatial);
    image(out, r.outputImage);
    preview(out, r.preview);
    objects(out, r.objects);
    nativeObjects(out, r.regions, r.contours);
  }
  for (const auto& n : r.nodeOutputs) {
    out << n.calibrationResult << encodeSpatial(n.spatial);
    parameters(out, n.parameters);
    roi(out, n.roi);
    image(out, n.outputImage);
    preview(out, n.preview);
    objects(out, n.objects);
    out << n.measureName << n.algorithmMs << n.totalMs;
    out << qint32(n.state) << n.error;
    nativeObjects(out, n.regions, n.contours);
    nativeObjects(out, n.effectiveDomain, {});
    out << n.coordinateFrame << n.modelKey << quint32(n.instances.size());
    for (const auto& instance : n.instances)
      out << qint32(instance.index) << qint32(instance.state) << instance.error
          << instance.algorithmMs << qint32(instance.objectCount);
  }
  out << quint32(r.trainedModels.size());
  for (const auto& m : r.trainedModels)
    out << QJsonDocument(modelSettingsJson(m)).toJson(QJsonDocument::Compact)
        << m.artifact << m.outline;
  commit(file, out);
}
Result readWorkerResult(const QString& path, const Document& input) {
  QFile file(path);
  open(file);
  QDataStream in(&file);
  header(in);
  Result r;
  in >> r.calibrationResult >> r.error >> r.pipeline >> r.measureName >>
      r.algorithmMs >> r.totalMs >> r.steps;
  qint32 output;
  in >> output;
  r.outputNode = output;
  const auto size = count(in, 32);
  ensure(!size || (input.runWorkflow && size == quint32(input.workflow.size())),
         "结果节点数量与任务不匹配");
  ensure(output >= -2 && output < qint32(size), "结果输出节点无效");
  if (!size) {
    QByteArray spatial;
    in >> spatial;
    r.spatial = decodeSpatial(spatial);
    r.outputImage = image(in);
    r.preview = preview(in);
    r.objects = objects(in);
    nativeObjects(in, r.regions, r.contours);
  }

  for (quint32 i = 0; i < size; ++i) {
    NodeOutput n;
    QByteArray spatial;
    in >> n.calibrationResult >> spatial;
    n.spatial = decodeSpatial(spatial);
    n.parameters = parameters(in);
    n.roi = roi(in);

    n.outputImage = image(in);
    n.preview = preview(in);
    n.objects = objects(in);
    in >> n.measureName >> n.algorithmMs >> n.totalMs;
    qint32 state;
    in >> state >> n.error;
    ensure(state >= 0 && state <= 3, "节点状态无效");
    n.state = static_cast<NodeState>(state);
    nativeObjects(in, n.regions, n.contours);
    mv::MvXLDCont unused;
    nativeObjects(in, n.effectiveDomain, unused);
    in >> n.coordinateFrame >> n.modelKey;
    const auto instanceCount = count(in, 100);
    for (quint32 j = 0; j < instanceCount; ++j) {
      InstanceStatus s;
      qint32 instanceState;
      in >> s.index >> instanceState >> s.error >> s.algorithmMs >>
          s.objectCount;
      ensure(s.index >= 1 && s.index <= 100 && instanceState >= 0 &&
                 instanceState <= 3 && s.objectCount >= 0,
             "目标结果无效");
      s.state = NodeState(instanceState);
      n.instances.append(s);
    }
    r.nodeOutputs.append(std::move(n));
  }
  const auto trained = count(in, 32);
  for (quint32 i = 0; i < trained; ++i) {
    QByteArray bytes;
    in >> bytes;
    auto m = modelSettingsFromJson(QJsonDocument::fromJson(bytes).object());
    const auto* reference = findModel(input.models, m.id);
    ensure(reference != nullptr, "模型结果来源无效");
    m.referenceImage = reference->referenceImage;
    m.preview = reference->preview;
    in >> m.artifact >> m.outline;
    r.trainedModels.append(m);
  }
  if (size) {
    const auto sources = workflowSources(input);
    std::function<NodeOutput(int,int)> raster = [&](int node, int depth) -> NodeOutput {
      if (node < 0) { NodeOutput root; root.outputImage=input.image; root.preview=input.preview; return root; }
      ensure(depth <= int(size), "结果栅格来源不能循环");
      ensure(node < int(size), "结果栅格来源无效");
      if (r.nodeOutputs[node].outputImage.CountObj()) return r.nodeOutputs[node];
      const auto settings = workflowNodeSettings(input, node);
      const auto binding = inputBinding(settings, typedInputTool(input.workflow[node].algorithm) ? "ObjectA" : "Image");
      if (binding.node >= 0 && !binding.output.isEmpty()) {
        ensure(binding.node < int(size), "结果命名输入来源无效");
        auto selected = selectOutput(r.nodeOutputs[binding.node], binding.output);
        if (selected.outputImage.CountObj()) return selected;
        return raster(binding.node, depth+1);
      }
      const int parent = typedInputTool(input.workflow[node].algorithm)
                             ? workflowNodeSettings(input, node).dataA
                             : sources.value(node, -1);
      return raster(parent, depth+1);
    };
    for (int i = 0; i < r.nodeOutputs.size(); ++i)
      if (sourceTool(input.workflow[i].algorithm))
        r.nodeOutputs[i].sourceData = std::make_shared<SourceData>(resolveSourceData(input, input.workflow[i].options["binding"].toString()));
    for (int i = 0; i < r.nodeOutputs.size(); ++i) {
      auto& n = r.nodeOutputs[i];
      int source = input.runWorkflow
                             ? (typedInputTool(input.workflow[i].algorithm)
                                          ? workflowNodeSettings(input, i).dataA
                                          : sources.value(i, -1))
                             : -1;
      if (source == -2) source = sources.value(i, -1);
      if (n.state != NodeState::Success && n.instances.isEmpty() &&
          n.objects.isEmpty())
        continue;
      if (input.workflow[i].algorithm == Algorithm::StereoImage || input.workflow[i].algorithm == Algorithm::StructuredImage) {
        n.inputImage = n.outputImage;
        n.inputPreview = n.preview;
        continue;
      }
      if (sourceTool(input.workflow[i].algorithm)) {
        const auto cell = resolveSourceData(input, input.workflow[i].options.value("binding").toString());
        n.sourceData = std::make_shared<SourceData>(cell);
        n.inputImage = n.outputImage; n.inputPreview = n.preview; continue;
      }
      const auto setting = workflowNodeSettings(input, i);
      const auto binding = inputBinding(setting, typedInputTool(input.workflow[i].algorithm) ? "ObjectA" : "Image");
      if (binding.node >= 0 && binding.node < r.nodeOutputs.size() && !binding.output.isEmpty()) {
        auto selected = selectOutput(r.nodeOutputs[binding.node], binding.output);
        if (!selected.outputImage.CountObj()) selected = raster(binding.node, 0);
        n.inputImage = selected.outputImage;
        n.inputPreview = selected.preview;
        if (n.effectiveDomain.CountObj()) n.inputImage = n.inputImage.ReduceDomain(n.effectiveDomain);
        continue;
      }
      ensure(source >= -1 && source < int(size), "结果输入连线无效");
      const auto reference = raster(source, 0);
      n.inputImage = reference.outputImage;
      n.inputPreview = reference.preview;
      if (n.effectiveDomain.CountObj()) {
        n.inputImage = n.inputImage.ReduceDomain(n.effectiveDomain);
        // Definition-domain changes leave the source preview pixels intact.
      }
    }
    if (r.outputNode >= 0 && r.outputNode < int(size) &&
        (r.nodeOutputs[r.outputNode].state == NodeState::Success ||
         !r.nodeOutputs[r.outputNode].instances.isEmpty() ||
         !r.nodeOutputs[r.outputNode].objects.isEmpty())) {
      const auto& selected = r.nodeOutputs[r.outputNode];
      r.spatial = selected.spatial;
      r.outputImage = selected.outputImage;
      r.preview = selected.preview;
      r.objects = selected.objects;
      r.instances = selected.instances;
      r.regions = selected.regions;
      r.contours = selected.contours;
    }
  }
  ensure(in.status() == QDataStream::Ok && in.atEnd(), "任务结果损坏");
  return r;
}
}  // namespace workbench
