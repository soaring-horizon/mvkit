// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QStringList>
#include <QVector>

#include "model/document.hpp"
namespace workbench {
struct OperatorDefinition {
  Algorithm id;
  QString key, name, category, description, output;
  QStringList bindings;
};
const QVector<OperatorDefinition>& operatorRegistry();
const OperatorDefinition& operatorDefinition(Algorithm id);
QStringList operatorAliases(Algorithm id);
QStringList workflowNodeNames(const QVector<Parameters>& steps,
                              const QStringList& aliases = {});
Algorithm algorithmFromKey(const QString& key);
}  // namespace workbench
