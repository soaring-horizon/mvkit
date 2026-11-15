// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QJsonObject>
namespace workbench {
// Appends real sources without renumbering existing operators or resources.
QJsonObject explicitTutorialInputs(const QJsonObject& project,const QString& path);
QJsonObject namedWorkflowInputs(const QJsonObject& project, const QString& path);
}
