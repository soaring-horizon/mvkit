// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include <QElapsedTimer>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

#include "runtime/execution.hpp"
#include "operators/operator_registry.hpp"
namespace workbench {
namespace {
int pixelBytes(mv::MvPixelType type) {
  switch (type) {
    case mv::MvPixelType::UInt8:
      return 1;
    case mv::MvPixelType::UInt16:
    case mv::MvPixelType::Int16:
      return 2;
    default:
      return 4;
  }
}
const char* pixelName(mv::MvPixelType type) {
  switch (type) {
    case mv::MvPixelType::UInt8:
      return "byte";
    case mv::MvPixelType::UInt16:
      return "uint2";
    case mv::MvPixelType::Int16:
      return "int2";
    case mv::MvPixelType::Int32:
      return "int4";
    default:
      return "real";
  }
}
mv::MvImage compose(const mv::MvImage& old, const mv::MvImage& next) {
  if (!old.CountObj()) return next;
  if (old.Width() != next.Width() || old.Height() != next.Height() ||
      old.Channels() != next.Channels())
    throw std::runtime_error("逐目标图像规格不一致");
  auto domain = old.GetDomain(), addition = next.GetDomain();
  mv::MvTuple centerRow, centerColumn;
  const auto overlap =
      domain.Intersection(addition).AreaCenter(&centerRow, &centerColumn);
  if (overlap.Length() && overlap.D() > 0)
    throw std::runtime_error("逐目标图像定义域重叠，无法无歧义合成");
  mv::MvTuple rows, first, last;
  addition.GetRegionRuns(&rows, &first, &last);
  std::array<mv::MvImage, 3> planes;
  for (std::size_t channel = 0; channel < old.Channels(); ++channel) {
    if (old.Type(channel) != next.Type(channel))
      throw std::runtime_error("逐目标图像像素类型不一致");
    const auto rowBytes =
        std::size_t(old.Width()) * pixelBytes(old.Type(channel));
    if (rowBytes * old.Height() * old.Channels() > 512 * 1024 * 1024)
      throw std::runtime_error("逐目标图像超过合成预算");
    QByteArray pixels(int(rowBytes * old.Height()), Qt::Uninitialized);
    for (std::size_t row = 0; row < std::size_t(old.Height()); ++row)
      std::memcpy(pixels.data() + row * rowBytes,
                  static_cast<const char*>(old.Data(channel)) +
                      row * old.Stride(channel),
                  rowBytes);
    for (std::size_t i = 0; i < rows.Length(); ++i) {
      const auto row = rows.I(i), begin = first.I(i), end = last.I(i);
      std::memcpy(pixels.data() + row * rowBytes +
                      begin * pixelBytes(old.Type(channel)),
                  static_cast<const char*>(next.Data(channel)) +
                      row * next.Stride(channel) +
                      begin * pixelBytes(next.Type(channel)),
                  std::size_t(end - begin + 1) * pixelBytes(old.Type(channel)));
    }
    planes[channel].GenImage1(pixelName(old.Type(channel)), old.Width(),
                              old.Height(), pixels.constData());
  }
  auto combined = old.Channels() == 1
                      ? planes[0]
                      : planes[0].Compose3(planes[1], planes[2]);
  return combined.ReduceDomain(domain.Union2(addition));
}
}  // namespace
Result executeInstances(const Document& snapshot,
                        const std::function<void(int)>& started) {
  Result result;
  QElapsedTimer clock;
  clock.start();
  try {
    const auto links = workflowConnections(snapshot);
    QVector<int> roots;
    int poseSource = -1;
    for (int i = 0; i < snapshot.workflow.size(); ++i) {
      const auto s = workflowNodeSettings(snapshot, i);
      if (!s.iteratePose || !snapshot.workflowScope.isEmpty() && !snapshot.workflowScope.contains(i)) continue;
      if (snapshot.workflowTarget >= 0 && snapshot.workflowTarget != i &&
          !workflowAncestors(links, snapshot.workflowTarget).contains(i))
        continue;
      if (s.poseSource < 0 || s.poseSource >= snapshot.workflow.size() ||
          operatorDefinition(snapshot.workflow[s.poseSource].algorithm)
                  .output != "Match" ||
          !workflowAncestors(links, i).contains(s.poseSource))
        throw std::runtime_error("逐目标处理需要连接有效定位源");
      if (poseSource >= 0 && poseSource != s.poseSource)
        throw std::runtime_error(
            "当前逐目标流程仅支持同一定位源，不做不同定位源的隐式配对");
      poseSource = s.poseSource;
      roots.append(i);
    }
    QVector<bool> affected(snapshot.workflow.size(), false);
    for (int i = 0; i < affected.size(); ++i)
      for (int root : roots)
        if (i == root || workflowAncestors(links, i).contains(root))
          affected[i] = true;
    result.nodeOutputs.resize(snapshot.workflow.size());
    for (int i = 0; i < snapshot.workflow.size(); ++i)
      result.nodeOutputs[i].parameters = snapshot.workflow[i];
    result.outputNode = snapshot.workflowTarget >= 0
                            ? snapshot.workflowTarget
                            : workflowOutputNode(snapshot);
    for (int root : roots) {
      const auto s = workflowNodeSettings(snapshot, root);
      bool valid = s.scope == ScopeMode::Manual;
      if (s.scope == ScopeMode::Region && s.regionSource >= 0)
        for (int other : roots)
          if (other == s.regionSource ||
              workflowAncestors(links, s.regionSource).contains(other))
            valid = true;
      if (!valid) {
        auto& node = result.nodeOutputs[root];
        node.state = NodeState::Error;
        node.error = QStringLiteral(
            "逐目标处理需要手绘范围，或同一逐目标分支产生的区域范围；不能对整图"
            "重复处理");
        throw std::runtime_error(node.error.toUtf8().constData());
      }
    }
    QVector<NodeOutput> overrides(snapshot.workflow.size());
    if (snapshot.workflowTarget >= 0 && !affected[snapshot.workflowTarget])
      return execute(snapshot, started, &overrides);
    if (affected.value(poseSource))
      throw std::runtime_error("暂不支持嵌套定位迭代");
    auto discovery = snapshot;
    discovery.workflowTarget = poseSource;
    auto prefix = execute(discovery, started, &overrides);
    if (poseSource >= prefix.nodeOutputs.size()) return prefix;
    const auto& poses = prefix.nodeOutputs[poseSource];
    for (int i = 0; i < prefix.nodeOutputs.size(); ++i)
      if (!affected[i]) overrides[i] = prefix.nodeOutputs[i];
    const int count = poses.objects.size();
    result.nodeOutputs = prefix.nodeOutputs;
    result.trainedModels = prefix.trainedModels;
    result.outputNode = snapshot.workflowTarget >= 0
                            ? snapshot.workflowTarget
                            : workflowOutputNode(snapshot);
    if (poses.state != NodeState::Success || count == 0) {
      for (int i = 0; i < affected.size(); ++i)
        if (affected[i]) {
          auto& n = result.nodeOutputs[i];
          n.state = NodeState::Blocked;
          n.error = QStringLiteral("定位未找到目标，逐目标流程未执行");
        }
    } else
      for (int instance = 0; instance < count; ++instance) {
        if (count > 100) throw std::runtime_error("匹配目标超过 100 个");
        auto child = snapshot;
        child.models = snapshot.models;
        for (auto& model : child.models)
          for (const auto& trained : result.trainedModels)
            if (model.id == trained.id) model = trained;
        child.sharedNodeSettings.clear();
        child.nodeSettings.clear();
        child.rangeOverrides.clear();
        for (int i = 0; i < snapshot.workflow.size(); ++i) {
          auto s = workflowNodeSettings(snapshot, i);
          s.iteratePose = false;
          if (affected[i] && s.poseSource == poseSource)
            s.poseInstance = instance;
          child.nodeSettings.append(s);
          if (affected[i] && dataTool(child.workflow[i].algorithm)) {
            if (s.dataA == poseSource)
              child.workflow[i].options["objectA"] = instance + 1;
            if (s.dataB == poseSource)
              child.workflow[i].options["objectB"] = instance + 1;
          }
        }
        auto current = execute(child, started, &overrides);
        if (current.nodeOutputs.size() != snapshot.workflow.size())
          throw std::runtime_error("逐目标子流程未返回节点结果");
        for (const auto& model : current.trainedModels) {
          bool found = false;
          for (auto& old : result.trainedModels)
            if (old.id == model.id) {
              old = model;
              found = true;
            }
          if (!found) result.trainedModels.append(model);
        }
        for (int i = 0; i < current.nodeOutputs.size(); ++i) {
          auto leaf = current.nodeOutputs[i];
          if (!affected[i]) {
            result.nodeOutputs[i] = leaf;
            if (leaf.state != NodeState::Pending) overrides[i] = leaf;
            continue;
          }
          if (leaf.state == NodeState::Pending) continue;
          auto& aggregate = result.nodeOutputs[i];
          if (instance == 0) {
            aggregate = {};
            aggregate.parameters = snapshot.workflow[i];
            aggregate.coordinateFrame = leaf.coordinateFrame;
            aggregate.measureName = leaf.measureName;
            aggregate.inputImage = snapshot.image;
            aggregate.inputPreview = snapshot.preview;
          }
          std::size_t existingBudget = 0, newBudget = 0;
          for (const auto& object : aggregate.objects)
            existingBudget +=
                object.samples.size() + object.overlay.elementCount();
          for (const auto& object : leaf.objects)
            newBudget += object.samples.size() + object.overlay.elementCount();
          if (existingBudget + newBudget > 2000000 ||
              aggregate.objects.size() + leaf.objects.size() > 10000) {
            leaf.state = NodeState::Error;
            leaf.error =
                QStringLiteral("逐目标几何/采样超过聚合预算，本目标未加入显示");
            leaf.objects.clear();
          }
          if (leaf.state == NodeState::Success) {
            try {
              if (!leaf.preview.isNull()) {
                aggregate.outputImage =
                    compose(aggregate.outputImage, leaf.outputImage);
                aggregate.preview = makePreview(aggregate.outputImage).image;
              } else if (!aggregate.outputImage.CountObj())
                aggregate.outputImage = leaf.outputImage;
              for (auto object : leaf.objects) {
                object.instance = instance + 1;
                object.label = QStringLiteral("目标 %1 · ").arg(instance + 1) +
                               object.label;
                aggregate.objects.append(std::move(object));
              }

              if (leaf.contours.CountObj())
                aggregate.contours =
                    aggregate.contours.CountObj()
                        ? aggregate.contours.ConcatObj(leaf.contours)
                        : leaf.contours;
              if (leaf.regions.CountObj())
                aggregate.regions =
                    aggregate.regions.CountObj()
                        ? aggregate.regions.ConcatObj(leaf.regions)
                        : leaf.regions;
              if (leaf.effectiveDomain.CountObj())
                aggregate.effectiveDomain =
                    aggregate.effectiveDomain.CountObj()
                        ? aggregate.effectiveDomain.Union2(leaf.effectiveDomain)
                        : leaf.effectiveDomain;
            } catch (const std::exception& e) {
              leaf.state = NodeState::Error;
              leaf.error = QString::fromUtf8(e.what());
            }
          }
          if (leaf.state != NodeState::Success)
            for (auto object : leaf.objects)
              if (!object.samples.isEmpty()) {
                object.instance = instance + 1;
                object.label = QStringLiteral("目标 %1 · 拟合未通过 · ")
                                   .arg(instance + 1) +
                               object.label;
                aggregate.objects.append(std::move(object));
              }
          aggregate.algorithmMs += leaf.algorithmMs;
          aggregate.totalMs += leaf.totalMs;
          aggregate.instances.append({instance + 1, leaf.state, leaf.error,
                                      leaf.algorithmMs, leaf.objects.size()});
        }
      }
    QStringList errors;
    const auto names =
        workflowNodeNames(snapshot.workflow, snapshot.workflowLabels);
    for (int i = 0; i < result.nodeOutputs.size(); ++i) {
      auto& n = result.nodeOutputs[i];
      if (!n.instances.isEmpty()) {
        n.state = NodeState::Success;
        QStringList failures;
        for (const auto& target : n.instances)
          if (target.state != NodeState::Success) {
            n.state = NodeState::Error;
            failures << QStringLiteral("目标 %1：%2")
                            .arg(target.index)
                            .arg(target.error);
          }
        n.error = failures.join("；");
      }
      if (n.state == NodeState::Error || n.state == NodeState::Blocked)
        errors << names[i] + "：" + n.error;
      result.algorithmMs += n.algorithmMs;
      result.steps.append({names[i], n.algorithmMs});
    }
    if (result.outputNode >= 0 &&
        result.outputNode < result.nodeOutputs.size()) {
      const auto& n = result.nodeOutputs[result.outputNode];
      result.objects = n.objects;
      result.contours = n.contours;
      result.regions = n.regions;
      result.outputImage = n.outputImage;
      result.preview = n.preview;
      result.measureName = n.measureName;
      result.instances = n.instances;
    } else
      errors << QStringLiteral("请指定流程输出节点");
    result.error = errors.join("；");
    result.pipeline = QStringLiteral("逐目标流程 · %1 个目标").arg(count);
  } catch (const std::exception& e) {
    result.error = QString::fromUtf8(e.what());
  }
  result.totalMs = clock.nsecsElapsed() / 1e6;
  return result;
}
}  // namespace workbench
