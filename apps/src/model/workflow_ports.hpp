// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "model/document.hpp"
#include <functional>

namespace workbench {
struct OutputPort {
  QString key, label, type;
};
struct InputPort {
  QString key, label;
  QStringList types;
  bool required = true;
};
// Schemas are available before execution. Preview selection never changes them.
QVector<OutputPort> outputPorts(const Parameters& parameters);
QString outputSummary(const Parameters& parameters);
QVector<InputPort> inputPorts(const Parameters& parameters);
QVector<OutputPort> compatibleOutputs(const Parameters& source,
                                     const Parameters& target, const QString& slot);
OutputBinding inputBinding(const NodeSettings& settings, const QString& slot);
void setInputBinding(NodeSettings& settings, const QString& slot, OutputBinding binding);
void remapInputBindings(NodeSettings& settings, const std::function<int(int)>& remap);
struct NodeOutput;
NodeOutput selectOutput(const NodeOutput& source, const QString& key);
}
