// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QString>
#include <QVector>

namespace workbench {
struct SampleProject {
  QString id, title, bundle, project, flow, steps, expected;
};
QVector<SampleProject> sampleProjects();
QString bundledSampleProject(const QString& id);
bool isBundledSamplePath(const QString& path);
// Creates an independent directory with images and frozen model assets.
// The bundled source and existing user files are never overwritten.
QString createSampleProject(const QString& id, const QString& parentDirectory);
}  // namespace workbench
