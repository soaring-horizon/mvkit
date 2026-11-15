// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "model/document.hpp"
#include <memory>
namespace workbench {
struct RunSnapshot {
  std::shared_ptr<const Document> document;
  QString sampleId;
  quint64 configurationRevision=0;
  QJsonObject provenance;
};
RunSnapshot freezeRunSnapshot(Document prepared,const QString& sampleId,quint64 revision);
}
