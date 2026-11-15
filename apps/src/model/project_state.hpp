// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "model/document.hpp"
namespace workbench {
struct SampleState {QString id;Document document;bool selected=true;};
// Presentation/navigation does not participate in computation revisions.
struct ProjectViewState {
  QString mode="workflow",debugNodeId;
  int active=-1,selectedNode=0;
  QVector<QPointF> positions;
  QString inputKey;
  QJsonObject preferredInputs;
};
// Project configuration contains frozen inputs and resources, never run results.
struct ProjectState {
  QVector<SampleState> samples;
  QVector<Parameters> workflow;
  QStringList nodeIds,nodeNames;
  QVector<int> inputs;
  QVector<QVector<int>> links;
  int output=-2;
  QVector<NodeSettings> sharedRanges;
  QVector<NodeSettings> basicRanges=QVector<NodeSettings>(toolSlotCount);
  QVector<ModelResource> models;
  Parameters basicParameters;
  ProjectViewState view;
};
}
