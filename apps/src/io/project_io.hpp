// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QJsonObject>
#include <QVector>
#include <QStringList>
#include <atomic>
namespace workbench {
struct ProjectState;
QJsonObject projectStateJson(const ProjectState& state,const QString& path);
struct ProjectResource {QString path, fingerprint;bool pixels=false;};
QJsonObject readProjectConfiguration(const QString& path);
// Preserve resource identities when moving a configuration between directories.
QJsonObject rebaseProjectResources(const QJsonObject& object,const QString& source,const QString& target);
void writeProjectConfiguration(const QJsonObject& object,const QString& path);
// Copies referenced files byte-for-byte, then atomically commits an ordinary project.
void writeProjectWithOriginalData(const QJsonObject& object,const QString& path);
QVector<ProjectResource> projectResources(const QJsonObject& object,const QString& project);
QStringList missingProjectResources(const QJsonObject& object,const QString& project);
QJsonObject relocateProjectResources(const QJsonObject& object,const QString& project,const QString& searchRoot,const std::atomic_bool* cancelled=nullptr);
}
