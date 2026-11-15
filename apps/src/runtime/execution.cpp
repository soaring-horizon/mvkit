// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "runtime/execution.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QThread>
#include <QTransform>
#include <algorithm>
#include <memory>
#include <stdexcept>

#include "operators/calibration_tools.hpp"
#include "operators/image_processing.hpp"
#include "operators/operator_registry.hpp"
#include "operators/range_model.hpp"
#include "model/spatial_data.hpp"
#include "model/workflow_ports.hpp"
#include "model/sample_inputs.hpp"
#include "runtime/worker_protocol.hpp"

namespace workbench {
Result execute(const Document& inputSnapshot,
               const std::function<void(int)>& started,
               const QVector<NodeOutput>* overrides) {
  Result result;
  QElapsedTimer total, algorithm;
  total.start();
  try {
    auto normalized=inputSnapshot;
    applyLegacyInputOverrides(normalized);
    for (auto& node : normalized.workflow) if (multiViewInput(node)) {
      const auto data = resolveSourceData(normalized, node.options["binding"].toString());
      if (!data.views.isEmpty()) node.options["viewRoles"] = QJsonArray::fromStringList(data.views);
    }
    // Legacy overrides are now materialized. Recursive processing stages must
    // retain their upstream pixels/domains instead of reapplying the raw root.
    if (std::none_of(normalized.workflow.begin(), normalized.workflow.end(),
                     [](const Parameters& p){return sourceTool(p.algorithm);}))
      normalized.sourceInputs.clear();
    const auto& snapshot=normalized;
    if (!snapshot.calibrationJob.isEmpty())
      return executeCalibrationJob(snapshot);
    if (snapshot.runWorkflow && !overrides) {
      for (int i = 0; i < snapshot.workflow.size(); ++i)
        if ((snapshot.workflowScope.isEmpty() || snapshot.workflowScope.contains(i)) && workflowNodeSettings(snapshot, i).iteratePose &&
            (snapshot.workflowTarget < 0 || snapshot.workflowTarget == i ||
             workflowAncestors(workflowConnections(snapshot),
                               snapshot.workflowTarget)
                 .contains(i)))
          return executeInstances(snapshot, started);
    }
    if (!snapshot.runWorkflow && snapshot.applyActiveRange) {
      auto stage = snapshot;
      stage.applyActiveRange = false;
      stage.roi = {};
      stage.modelBinding = snapshot.activeRange.modelId;
      stage.image = stage.image.ReduceDomain(
          resolveRange(snapshot, -1, snapshot.activeRange, stage.image, {}));
      // ReduceDomain changes the valid domain, not the underlying pixels.
      // Preview composition applies that domain separately in ImageView.
      auto output = execute(stage, started);
      output.totalMs = total.nsecsElapsed() / 1e6;
      return output;
    }
    if (snapshot.runWorkflow && snapshot.workflow.isEmpty())
      throw std::runtime_error("流程没有处理节点");
    if (snapshot.runWorkflow && !snapshot.workflow.isEmpty()) {
      const auto sources = workflowSources(snapshot);
      const auto order = workflowOrder(snapshot, true);
      const auto links = workflowConnections(snapshot);
      const auto nodeNames =
          workflowNodeNames(snapshot.workflow, snapshot.workflowLabels);
      if (snapshot.workflowTarget < -1 ||
          snapshot.workflowTarget >= snapshot.workflow.size())
        throw std::runtime_error("节点调试目标已失效");
      auto required = snapshot.workflowTarget >= 0
                          ? workflowAncestors(links, snapshot.workflowTarget)
                          : snapshot.workflowScope;
      if (snapshot.workflowTarget >= 0)
        required.append(snapshot.workflowTarget);
      QStringList names;
      QVector<QPair<QString, double>> steps;
      QVector<NodeOutput> outputs(snapshot.workflow.size());
      for (int i = 0; i < outputs.size(); ++i)
        outputs[i].parameters = snapshot.workflow[i];
      auto models = snapshot.models;
      double elapsed = 0;
      for (int i : order) {
        if (!required.isEmpty() && !required.contains(i)) continue;
        if (overrides && i < overrides->size() &&
            (*overrides)[i].state != NodeState::Pending) {
          outputs[i] = (*overrides)[i];
          elapsed += outputs[i].algorithmMs;
          names << nodeNames[i];
          steps.append({nodeNames[i], outputs[i].algorithmMs});
          continue;
        }
        auto& n = outputs[i];
        n.parameters = snapshot.workflow[i];
        QStringList blocked;
        for (int upstream : links[i])
          if (upstream >= 0 && outputs[upstream].state != NodeState::Success)
            blocked << nodeNames[upstream];
        if (!blocked.isEmpty()) {
          n.state = NodeState::Blocked;
          n.error = QStringLiteral("上游失败：%1")
                        .arg(blocked.join(QStringLiteral("、")));
          continue;
        }
        if (started) started(i);
        auto stage = snapshot;
        stage.runWorkflow = false;
        stage.parameters = snapshot.workflow[i];
        try {
          const auto calibration=workflowCalibrationModel(snapshot,i,outputs);
          if(!calibration.isEmpty()) {
            stage.parameters.calibrationArtifact=calibration;
            stage.workflow[i].calibrationArtifact=calibration;
          }
          if(calibrationTool(stage.parameters.algorithm)) {
            const int source=workflowNodeSettings(snapshot,i).dataA;
            if(source<0 || source>=outputs.size() || !workflowAncestors(links, i).contains(source) || !outputs[source].sourceData)
              throw std::runtime_error("标定须选择已连通的原始图像、多视或序列输入节点");
            const auto& cell=*outputs[source].sourceData;
            if(!acceptsData(stage.parameters.algorithm,outputType(snapshot.workflow[source])))
              throw std::runtime_error("标定输入类型不匹配");
            const auto r=executeWorkflowCalibration(stage,cell,snapshot.workflow[source].options["binding"].toString());
            n.calibrationResult=r.calibrationResult;n.spatial=r.spatial;n.inputImage=n.outputImage=r.outputImage;
            n.inputPreview=n.preview=r.preview;n.objects=r.objects;n.measureName=r.measureName;
            n.coordinateFrame=outputs[source].coordinateFrame;n.effectiveDomain=n.inputImage.GetDomain();
            n.algorithmMs=r.algorithmMs;n.totalMs=r.totalMs;n.state=NodeState::Success;
            names<<nodeNames[i];steps.append({nodeNames[i],n.algorithmMs});elapsed+=n.algorithmMs;continue;
          }
          if (executeInputNode(stage, i, outputs, n)) {
            names << nodeNames[i]; steps.append({nodeNames[i], n.algorithmMs}); elapsed += n.algorithmMs;
            continue;
          }
          const auto settings = workflowNodeSettings(snapshot, i);
          const int selectedImage = settings.inputs.contains("Image") ? settings.inputs["Image"].node : sources[i];
          if (!dataTool(stage.parameters.algorithm) && (selectedImage < -1 ||
              !workflowImageSources(snapshot, i).contains(selectedImage)))
            throw std::runtime_error("未连接有效图像输入");
          if ((stage.parameters.algorithm == Algorithm::StereoRectify ||
               stage.parameters.algorithm == Algorithm::StructuredDecode) &&
              sources[i] != -1)
            throw std::runtime_error(
                "双目校正须直接连接采集输入，不能仅变更左图");
          auto resolved = outputs;
          // n references outputs[i]. Detach the adapter table before reading
          // outputs through QVector's non-const API, which would otherwise
          // move that reference into the shared copy.
          resolved.detach();
          auto resolve = [&](const QString& slot, int legacy) -> int {
            auto ref = inputBinding(settings, slot);
            if (!settings.inputs.contains(slot)) ref.node = legacy;
            if (ref.node < 0) return ref.node;
            if (ref.node >= outputs.size() || !workflowAncestors(links, i).contains(ref.node))
              throw std::runtime_error("所选输入已断开，请重新选择来源");
            if (!ref.output.isEmpty()) {
              const auto choices = compatibleOutputs(snapshot.workflow[ref.node], stage.parameters, slot);
              if (std::none_of(choices.begin(), choices.end(), [&](const OutputPort& p) { return p.key == ref.output; }))
                throw std::runtime_error("所选输出已失效或类型不兼容");
              resolved[ref.node] = selectOutput(outputs[ref.node], ref.output);
            }
            return ref.node;
          };
          const int imageSource = resolve("Image", sources[i]);
          if (imageSource >= 0) {
            stage.image = resolved[imageSource].outputImage;
            stage.preview = resolved[imageSource].preview;
          }
          const int rasterSource=dataTool(stage.parameters.algorithm) && settings.dataA>=0 && settings.dataA<outputs.size()
                                   ? resolve("ObjectA", settings.dataA) : imageSource;
          if (rasterSource>=0 && resolved[rasterSource].outputImage.CountObj()) {
            stage.image=resolved[rasterSource].outputImage;
            stage.preview=resolved[rasterSource].preview.isNull() ? resolved[rasterSource].inputPreview : resolved[rasterSource].preview;
          }
          const auto rasterFrame = rasterSource >= 0 ? resolved[rasterSource].coordinateFrame : snapshot.coordinateFrame;
          if (settings.scope == ScopeMode::Region) resolve("Region", settings.regionSource);
          if (settings.poseSource >= 0) resolve("Match", settings.poseSource);
          if (settings.poseSource >= 0 &&
              workflowObjectSources(snapshot, i, "Match")
                  .contains(settings.poseSource) &&
              settings.poseInstance >=
                  outputs[settings.poseSource].objects.size()) {
            n.state = NodeState::Blocked;
            n.error = QStringLiteral("定位未找到所选实例，当前节点未执行");
            continue;
          }
          stage.templateImage = stage.image;
          n.effectiveDomain = resolveRange(
              [&] {
                auto root = snapshot;
                root.coordinateFrame = rasterFrame;
                return root;
              }(),
              i, settings, stage.image, resolved);
          stage.image = stage.image.ReduceDomain(n.effectiveDomain);
          stage.roi = {};
          stage.modelBinding = settings.modelId;
          stage.activeRange = settings;
          stage.models = models;
          stage.modelsRequired = snapshot.modelsRequired;
          stage.templateRoi = settings.templateRoi;
          stage.separateTemplateRoi = true;
          n.roi = settings.scope == ScopeMode::Manual ? settings.roi : Roi{};
          n.coordinateFrame = rasterFrame;
          stage.coordinateFrame = n.coordinateFrame;
          n.inputImage = stage.image;
          n.inputPreview = stage.preview;
          Result output;
          if (dataTool(stage.parameters.algorithm)) {
            NodeOutput inputA, inputB;
            auto source = [&](int index, bool second) -> const NodeOutput* {
              const auto slot = second ? QString("ObjectB") : QString("ObjectA");
              const auto ref = inputBinding(settings, slot);
              if (index < 0 || index >= outputs.size() ||
                  !workflowAncestors(links, i).contains(index) ||
                  (ref.output.isEmpty() && !acceptsData(
                      stage.parameters.algorithm,
                      outputType(snapshot.workflow[index]),
                      second)))
                throw std::runtime_error("类型化输入未连接或类型不匹配");
              if (!ref.output.isEmpty()) {
                const auto choices = compatibleOutputs(snapshot.workflow[index], stage.parameters, slot);
                if (std::none_of(choices.begin(), choices.end(), [&](const OutputPort& p) { return p.key == ref.output; }))
                  throw std::runtime_error("所选输出已失效或类型不兼容");
              }
              auto& selected = second ? inputB : inputA;
              selected = selectOutput(outputs[index], ref.output);
              return &selected;
            };
            const auto* a = source(settings.dataA, false);
            const auto* b = needsSecondInput(stage.parameters)
                                ? source(settings.dataB, true)
                                : nullptr;
            if (a->captureContext) {
              applyCaptureContext(stage, *a->captureContext);
              n.captureContext = a->captureContext;
            }
            n.coordinateFrame = a->coordinateFrame;
            stage.coordinateFrame = n.coordinateFrame;
            if (a->outputImage.CountObj()) {
              stage.image = a->outputImage;
              n.inputImage = stage.image;
              n.inputPreview =
                  a->preview.isNull() ? a->inputPreview : a->preview;
              stage.preview = n.inputPreview;
            }
            if (b && b->coordinateFrame != n.coordinateFrame &&
                stage.parameters.algorithm != Algorithm::StereoDisparity)
              throw std::runtime_error("输入坐标系不同，不能直接测量");
            output = executeGeometry(stage, a, b);
            if (stage.parameters.algorithm == Algorithm::StereoImage || stage.parameters.algorithm == Algorithm::StructuredImage) {
              const auto key = stage.parameters.algorithm == Algorithm::StructuredImage
                                   ? structuredImageKey(stage.parameters)
                                   : stage.parameters.options.value("component").toInt() == 0
                                   ? QString("left") : QString("right");
              n.coordinateFrame = a->spatial->rasters[key].frame;
              n.inputImage = output.outputImage;
              n.inputPreview = output.preview;
              n.effectiveDomain = output.outputImage.GetDomain();
            } else if (!output.spatial) output.spatial = a->spatial;
          } else
            output = execute(stage);
          n.coordinateFrame =
              processingFrame(n.coordinateFrame, stage.parameters);
          n.spatial = output.spatial;
          if (n.spatial && spatialTool(stage.parameters.algorithm) &&
              stage.parameters.algorithm != Algorithm::StereoImage && stage.parameters.algorithm != Algorithm::StructuredImage)
            n.coordinateFrame = n.spatial->frame;
          n.outputImage = output.outputImage;
          n.preview = output.preview;
          n.objects = output.objects;
          n.regions = output.regions;
          n.contours = output.contours;
          n.measureName = output.measureName;
          n.algorithmMs = output.algorithmMs;
          n.totalMs = output.totalMs;
          n.error = output.error;
          n.state =
              output.error.isEmpty() ? NodeState::Success : NodeState::Error;
          for (const auto& trained : output.trainedModels) {
            if (trained.id == settings.modelId)
              n.modelKey = trained.artifactKey;
            for (auto& m : models)
              if (m.id == trained.id) m = trained;
            bool found = false;
            for (auto& m : result.trainedModels)
              if (m.id == trained.id) {
                m = trained;
                found = true;
              }
            if (!found) result.trainedModels.append(trained);
          }
        } catch (const std::exception& error) {
          n.error = QString::fromUtf8(error.what());
          n.state = NodeState::Error;
        }
        names << nodeNames[i];
        steps.append({names.last(), n.algorithmMs});
        elapsed += n.algorithmMs;
      }
      result.outputNode = snapshot.workflowTarget >= 0
                              ? snapshot.workflowTarget
                              : workflowOutputNode(snapshot);
      if (result.outputNode >= 0 && result.outputNode < outputs.size()) {
        const auto& out = outputs[result.outputNode];
        if (out.state == NodeState::Success || !out.objects.isEmpty()) {
          result.spatial = out.spatial;
          result.outputImage = out.outputImage;
          result.preview = out.preview;
          result.objects = out.objects;
          result.regions = out.regions;
          result.contours = out.contours;
          result.measureName = out.measureName;
          result.calibrationResult = out.calibrationResult;
        }
      } else
        result.error = QStringLiteral("请指定流程输出节点");
      for (int i = 0; i < outputs.size(); ++i)
        if (outputs[i].state == NodeState::Error ||
            outputs[i].state == NodeState::Blocked) {
          if (!result.error.isEmpty()) result.error += "；";
          result.error +=
              nodeNames[i] + QStringLiteral("：") + outputs[i].error;
        }
      result.steps = steps;
      result.nodeOutputs = std::move(outputs);
      result.pipeline = names.join(QStringLiteral(" → "));
      result.algorithmMs = elapsed;
      result.totalMs = total.nsecsElapsed() / 1e6;
      return result;
    }
    if (sourceTool(snapshot.parameters.algorithm)) throw std::runtime_error("数据输入节点请在工作流中配置和调试");
    if (!snapshot.hasImage()) throw std::runtime_error("请先打开图像");
    const auto& p = snapshot.parameters;
    p.validate();
    validateRoi(snapshot.roi, snapshot.preview.width(),
                snapshot.preview.height());
    algorithm.start();
    auto input = snapshot.image;
    if (p.algorithm == Algorithm::Filter || p.algorithm == Algorithm::Gray) {
      if (snapshot.roi.kind != RoiKind::None)
        input = input.ReduceDomain(makeRegion(snapshot.roi));
      if (p.algorithm == Algorithm::Filter) {
        result.outputImage =
            p.filter == 0 ? input.MeanImage(p.mask, p.mask)
            : p.filter == 1
                ? input.GaussFilter(p.mask)
                : input.MedianImage("square", p.mask / 2, "continued");
      } else
        result.outputImage = p.gray == 1             ? input.InvertImage()
                             : input.Channels() == 3 ? input.Rgb1ToGray()
                                                     : input;
      result.algorithmMs = algorithm.nsecsElapsed() / 1e6;
      result.preview = makePreview(result.outputImage).image;
      result.pipeline = operatorDefinition(p.algorithm).name;
      result.measureName = QStringLiteral("结果图像");
      result.totalMs = total.nsecsElapsed() / 1e6;
      result.steps.append({result.pipeline, result.algorithmMs});
      return result;
    }
    if (p.algorithm == Algorithm::StereoRectify || p.algorithm == Algorithm::StructuredDecode)
      return executeSpatial(snapshot);
    if (spatialTool(p.algorithm))
      throw std::runtime_error(
          "此算子需要连接双目或点云输入，请在工作流中使用或调试节点");
    if (imageProcessingTool(p.algorithm)) return executeProcessing(snapshot);
    if (dataTool(p.algorithm)) {
      if (p.algorithm == Algorithm::Measure ||
          p.algorithm == Algorithm::Judge ||
          p.algorithm == Algorithm::ScaleCalibration ||
          p.algorithm == Algorithm::RegionBoolean ||
          p.algorithm == Algorithm::PlaneMeasure)
        throw std::runtime_error(
            "数据算子需要类型化上游，请拖入工作流并连接，或从流程节点列表调试");
      if (snapshot.roi.kind != RoiKind::None)
        input = input.ReduceDomain(makeRegion(snapshot.roi));
      auto stage = snapshot;
      stage.image = input;
      return executeGeometry(stage, nullptr, nullptr);
    }
    // Convert the retained pixels before restricting their domain. The RGB
    // conversion only guarantees pixels inside its domain; zeroed exterior
    // pixels would otherwise create artificial Canny edges at the ROI boundary.
    if (input.Channels() == 3)
      input = input.FullDomain().Rgb1ToGray().ReduceDomain(input.GetDomain());
    if (p.algorithm == Algorithm::ShapeMatch ||
        p.algorithm == Algorithm::NccMatch) {
      if (snapshot.modelsRequired || !snapshot.modelBinding.isEmpty()) {
        const auto* resource =
            findModel(snapshot.models, snapshot.modelBinding);
        if (!resource)
          throw std::runtime_error("请在模板页设置参考图像和训练范围");
        auto model = *resource;
        try {
          result = findWithModel(snapshot, model);
        } catch (const std::exception& error) {
          result.error = QString::fromUtf8(error.what());
        }
        if (!model.artifact.isEmpty()) result.trainedModels.append(model);
        result.totalMs = total.nsecsElapsed() / 1e6;
        return result;
      }
      mv::MvImage pattern;
      if (!p.templatePath.isEmpty()) {
        pattern = loadImage(p.templatePath).image;
        if (pattern.Channels() == 3) pattern = pattern.Rgb1ToGray();
        if (snapshot.roi.kind != RoiKind::None)
          input = input.ReduceDomain(makeRegion(snapshot.roi));
      } else {
        const auto templateRange =
            snapshot.separateTemplateRoi ? snapshot.templateRoi : snapshot.roi;
        if (templateRange.kind == RoiKind::None)
          throw std::runtime_error("请选择模板图像，或先绘制模板 ROI");
        validateRoi(templateRange, snapshot.preview.width(),
                    snapshot.preview.height());
        auto templateInput =
            snapshot.templateImage.CountObj() ? snapshot.templateImage : input;
        if (templateInput.Channels() == 3)
          templateInput = templateInput.Rgb1ToGray();
        pattern =
            templateInput.ReduceDomain(makeRegion(templateRange)).CropDomain();
        if (snapshot.separateTemplateRoi && snapshot.roi.kind != RoiKind::None)
          input = input.ReduceDomain(makeRegion(snapshot.roi));
      }
      constexpr double radians = 3.14159265358979323846 / 180.;
      const double start = p.angleStart * radians,
                   extent = p.angleExtent * radians;
      mv::MvTuple rows, columns, angles, scores;
      if (p.algorithm == Algorithm::ShapeMatch) {
        const auto model = pattern.CreateShapeModel(
            1, start, extent, .05, "none", "use_polarity", 30, 10);
        input.FindShapeModel(model, start, extent, p.score, p.matches, .5,
                             "least_squares", 1, .9, &rows, &columns, &angles,
                             &scores);
      } else {
        const auto model =
            pattern.CreateNccModel(1, start, extent, .05, "use_polarity");
        input.FindNccModel(model, start, extent, p.score, p.matches, .5, "true",
                           1, &rows, &columns, &angles, &scores);
      }
      result.algorithmMs = algorithm.nsecsElapsed() / 1e6;
      for (std::size_t i = 0; i < rows.Length(); ++i) {
        ResultObject object;
        object.type = "Match";
        object.measure = scores.D(i);
        object.center = {columns.D(i), rows.D(i)};
        object.angle = -angles.D(i) / radians;  // SDK CCW -> Qt image clockwise
        object.values["sdkAngle"] = angles.D(i) / radians;
        object.values["angle"] = object.angle;
        const double width = static_cast<double>(pattern.Width()) - 1;
        const double height = static_cast<double>(pattern.Height()) - 1;
        QPainterPath rectangle;
        rectangle.addRect(-width / 2, -height / 2, width, height);
        QTransform transform;
        transform.translate(object.center.x(), object.center.y());
        transform.rotate(object.angle);
        object.overlay = transform.map(rectangle);
        result.objects.append(object);
      }
      result.pipeline = operatorDefinition(p.algorithm).name;
      result.measureName = QStringLiteral("匹配得分");
      result.steps.append({result.pipeline, result.algorithmMs});
      result.totalMs = total.nsecsElapsed() / 1e6;
      return result;
    }
    if (snapshot.roi.kind != RoiKind::None)
      input = input.ReduceDomain(makeRegion(snapshot.roi));
    mv::MvRegion regions;
    mv::MvXLDCont edges;
    std::int64_t count;
    if (p.algorithm == Algorithm::Regions) {
      const auto method = p.options.value("thresholdMethod").toInt();
      const auto polarity = p.options.value("dark").toInt() ? "dark" : "light";
      if (method == 1) {
        std::int64_t used;
        regions = input.BinaryThreshold("max_separability", polarity, &used);
        result.steps.append({QStringLiteral("全局自动阈值 %1").arg(used), 0});
      } else if (method == 2) {
        const int width = p.options.value("localWindow").toInt(31);
        regions = input.DynThreshold(input.MeanImage(width, width), p.options.value("localOffset").toDouble(8), polarity);
      } else {
        regions = input.Threshold(p.minimum, p.maximum);
      }
      if (p.opening > 0) regions = regions.OpeningCircle(p.opening);
      regions = regions.Connection().SelectShape(
          "area", "and", p.minimumArea,
          std::max(p.minimumArea, static_cast<double>(snapshot.image.Width()) *
                                      snapshot.image.Height()));
      count = regions.CountObj();
      result.regions = regions;
      result.measureName = QStringLiteral("面积 / px²");
      result.pipeline = QStringLiteral("区域分割 · 连通与面积筛选");
    } else {
      edges = input.EdgesSubPix("canny", p.sigma, p.edgeLow, p.edgeHigh);
      count = edges.CountObj();
      result.contours = edges;
      result.measureName = QStringLiteral("长度 / px");
      result.pipeline = QStringLiteral("边缘提取 · Canny");
    }
    result.algorithmMs = algorithm.nsecsElapsed() / 1e6;
    result.steps.append(
        {operatorDefinition(p.algorithm).name, result.algorithmMs});
    // Explicit display budgets avoid unbounded path allocation; never
    // silently truncate results.
    if (count > 10000)
      throw std::runtime_error("结果超过 10000 个对象，请缩小 ROI 或调整参数");
    std::size_t points = 0;
    for (std::int64_t i = 1; i <= count; ++i) {
      ResultObject object;
      if (p.algorithm == Algorithm::Regions) {
        const auto region = regions.SelectObj(i);
        mv::MvTuple row, column;
        const auto area = region.AreaCenter(&row, &column);
        object.type = "Region";
        object.measure = area.D();
        object.center = {column.D(), row.D()};
        mv::MvTuple rows, starts, ends;
        region.GetRegionRuns(&rows, &starts, &ends);
        points += rows.Length();
        if (points > 2000000)
          throw std::runtime_error("叠加几何超过 200 万项，请缩小 ROI");
        object.overlay.setFillRule(Qt::WindingFill);
        for (std::size_t j = 0; j < rows.Length(); ++j)
          object.overlay.addRect(starts.D(j) - .5, rows.D(j) - .5,
                                 ends.D(j) - starts.D(j) + 1, 1);
      } else {
        const auto contour = edges.SelectObj(i);
        object.type = "XLD";
        object.measure = contour.LengthXld().D();
        mv::MvTuple rows, columns;
        contour.GetContourXld(&rows, &columns);
        points += rows.Length();
        if (points > 2000000)
          throw std::runtime_error("叠加几何超过 200 万项，请缩小 ROI");
        for (std::size_t j = 0; j < rows.Length(); ++j) {
          const QPointF point(columns.D(j), rows.D(j));
          if (j == 0)
            object.overlay.moveTo(point);
          else
            object.overlay.lineTo(point);
        }
        object.center = object.overlay.boundingRect().center();
      }
      result.objects.push_back(std::move(object));
    }
  } catch (const std::exception& error) {
    result.objects.clear();
    result.error = QString::fromUtf8(error.what());
  } catch (...) {
    result.objects.clear();
    result.error = QStringLiteral("未识别的执行错误");
  }
  result.totalMs = total.nsecsElapsed() / 1e6;
  return result;
}
ExecutionService::ExecutionService(QObject* parent, const QString& program)
    : QObject(parent),
      program_(program.isEmpty() ? QCoreApplication::applicationDirPath() +
                                       "/mv_studio_worker"
#ifdef Q_OS_WIN
                                       ".exe"
#endif
                                 : program) {
}
ExecutionService::~ExecutionService() { cancel(); }
void ExecutionService::cancel() {
  ++generation_;
  auto* process = worker_;
  worker_ = nullptr;
  if (process) {
    disconnect(process, nullptr, this, nullptr);
    if (qEnvironmentVariableIsSet("MVKIT_STUDIO_TRACE_WORKER")) qInfo() << "[worker] cancel" << process->processId() << process->property("node");
    if(process->state()!=QProcess::NotRunning) {
      process->kill();
      process->waitForFinished(1500);
    }
    // A node-progress slot may cancel while QProcess is emitting readyRead.
    // Retire the worker immediately, then defer destruction until that
    // signal stack has unwound. Keep parent ownership for shutdown cleanup.
    process->setObjectName({});
    process->deleteLater();
  }
  directory_.reset();
}
bool ExecutionService::start(const Document& snapshot, quint64 revision) {
  if (busy()) return false;
  auto directory = std::make_shared<QTemporaryDir>();
  if (!directory->isValid()) throw std::runtime_error("无法创建计算任务目录");
  const bool trace = qEnvironmentVariableIsSet("MVKIT_STUDIO_TRACE_WORKER");
  if (trace) qInfo() << "[worker] serialize" << revision << directory->path();
  directory_ = directory;
  auto* process = new QProcess(this);
  process->setObjectName("algorithmWorker");
  worker_ = process;
  const auto generation = ++generation_;
  auto progress = std::make_shared<QByteArray>();
  process->setProperty("node", -1);
  auto consumeProgress = [this, process, generation, revision, progress,
                          nodes = snapshot.workflow.size()] {
    if (generation_ != generation || worker_ != process) return;
    progress->append(process->readAllStandardOutput());
    if (progress->size() > 4096) {
      progress->clear();
      return;
    }
    int end = -1;
    while ((end = progress->indexOf('\n')) >= 0) {
      const auto line = progress->left(end);
      progress->remove(0, end + 1);
      if (!line.startsWith("node:")) continue;
      bool ok = false;
      const int node = line.mid(5).toInt(&ok);
      if (ok && node >= 0 && node < nodes &&
          process->property("node").toInt() != node) {
        process->setProperty("node", node);
        if (qEnvironmentVariableIsSet("MVKIT_STUDIO_TRACE_WORKER")) qInfo() << "[worker] node" << revision << node;
        emit nodeStarted(revision, node);
        if (generation_ != generation || worker_ != process) return;
      }
    }
  };
  connect(process, &QProcess::readyReadStandardOutput, this, consumeProgress);
#ifdef Q_OS_WIN
  process->setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments* args) {
        args->flags |= 0x08000000;  // CREATE_NO_WINDOW: worker and test
                                    // helper are hidden.
      });
#endif
  auto finish = [this, process, snapshot, revision, consumeProgress,
                 generation](bool failedToStart) {
    if (generation_ != generation || worker_ != process) return;
    consumeProgress();
    if (generation_ != generation || worker_ != process) return;
    if(process->property("decoding").toBool())return;
    process->setProperty("decoding",true);
    auto result=std::make_shared<Result>();const auto directory=directory_;
    auto* decoding=QThread::create([result,directory,snapshot,failedToStart,revision]{
      QElapsedTimer timer;timer.start();
      try {
        if(failedToStart)throw std::runtime_error("无法启动计算进程，请保留程序旁的 mv_studio_worker 及依赖文件");
        *result=readWorkerResult(directory->filePath("result.bin"),snapshot);
      } catch(const std::exception& error){result->error=QString::fromUtf8(error.what());}
      catch(...){result->error=QStringLiteral("读取任务结果失败");}
      if(qEnvironmentVariableIsSet("MVKIT_STUDIO_TRACE_WORKER"))qInfo()<<"[worker] deserialize-finished"<<revision<<timer.elapsed()<<result->error;
    });
    connect(decoding,&QThread::finished,this,[this,process,generation,revision,result]{
      if(generation_!=generation || worker_!=process)return;
      worker_=nullptr;directory_.reset();
      if(qEnvironmentVariableIsSet("MVKIT_STUDIO_TRACE_WORKER"))qInfo()<<"[worker] completed"<<revision<<process->exitCode()<<process->exitStatus()<<result->error;
      process->deleteLater();emit completed(revision,*result);
    });
    connect(decoding,&QThread::finished,decoding,&QObject::deleteLater);
    decoding->start();
  };
  connect(process,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [finish](int, QProcess::ExitStatus) { finish(false); });
  connect(process, &QProcess::errorOccurred, this,
          [this, finish](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
              QTimer::singleShot(0, this, [finish] { finish(true); });
          });
  auto preparationError=std::make_shared<QString>();
  auto* preparation=QThread::create([directory,snapshot,preparationError,trace,revision]{
    QElapsedTimer timer;timer.start();
    try { writeWorkerRequest(directory->filePath("request.bin"),snapshot); }
    catch(const std::exception& error){*preparationError=QString::fromUtf8(error.what());}
    catch(...){*preparationError=QStringLiteral("任务快照准备失败");}
    if(trace)qInfo()<<"[worker] serialize-finished"<<revision<<timer.elapsed()<<*preparationError;
  });
  // The independent job owns the scratch directory until writing is finished.
  // Cancellation retires this generation without waiting on serialization.
  connect(preparation,&QThread::finished,this,[this,process,generation,revision,preparationError,directory,cache = snapshot.batchCalibrationDirectory]{
    if(generation_!=generation || worker_!=process)return;
    if(!preparationError->isEmpty()) {
      Result result;result.error=*preparationError;worker_=nullptr;directory_.reset();process->deleteLater();emit completed(revision,result);return;
    }
    if(qEnvironmentVariableIsSet("MVKIT_STUDIO_TRACE_WORKER"))qInfo()<<"[worker] launch"<<revision<<program_;
    QStringList args{directory->path()};
    if (!cache.isEmpty()) args << cache;
    process->start(program_, args);
  });
  connect(preparation,&QThread::finished,preparation,&QObject::deleteLater);
  preparation->start();
  return true;
}
}  // namespace workbench
