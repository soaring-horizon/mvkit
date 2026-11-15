// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "runtime/run_snapshot.hpp"
#include "model/sample_inputs.hpp"
namespace workbench {
RunSnapshot freezeRunSnapshot(Document prepared,const QString& sampleId,quint64 revision) {
  RunSnapshot run;run.sampleId=sampleId;run.configurationRevision=revision;
  run.provenance=sampleProvenance(prepared,sampleId);run.provenance["configurationRevision"]=QString::number(revision);
  run.document=std::make_shared<const Document>(std::move(prepared));return run;
}
}
