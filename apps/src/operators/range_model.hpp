// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QJsonArray>

#include "runtime/execution.hpp"
namespace workbench {
QString referenceImageKey(const mv::MvImage& image);
mv::MvRegion trainingDomain(const ModelResource& model);
void buildModel(ModelResource& model);
Result findWithModel(const Document& input, ModelResource& model);
mv::MvRegion resolveRange(const Document& root, int node,
                          const NodeSettings& settings,
                          const mv::MvImage& image,
                          const QVector<NodeOutput>& outputs);
QJsonArray saveModels(const QVector<ModelResource>& models,
                      const QString& projectPath);
QVector<ModelResource> loadModels(const QJsonArray& array,
                                  const QString& projectPath);
ModelResource loadTemplateFile(const QString& path, Algorithm expected);
void saveTemplateFile(const ModelResource& model, const QString& path,
                      bool nativeModel = false);
}  // namespace workbench
