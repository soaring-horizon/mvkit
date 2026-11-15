// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "model/sample_edit.hpp"
#include "model/sample_inputs.hpp"
#include <stdexcept>
namespace workbench {
Document withSampleInput(const Document& original,const QVector<Parameters>& workflow,const QString& binding,const SourceData& input) {
  if(binding.isEmpty()||binding.size()>128||input.frames.size()!=input.paths.size()||input.keys.size()!=input.paths.size()||input.frames.size()>(input.sequence?64:input.views.isEmpty()?1:8) || (!input.views.isEmpty() && (input.sequence || input.views.size()!=input.frames.size())))
    throw std::runtime_error("样本输入列或数据无效");
  if(!original.sourceInputs.contains(binding)&&original.sourceInputs.size()>=32)throw std::runtime_error("样本最多支持32个显式输入列");
  auto staged=original;
  if(staged.sourceInputs.isEmpty()) {
    staged.sourceInputs=sampleInputs(original);
    for(const auto& node:workflow)if(sourceTool(node.algorithm)) {
      const auto key=node.options.value("binding").toString();if(staged.sourceInputs.contains(key))continue;
      const auto cell=resolveSourceData(original,key);if(!cell.frames.isEmpty())staged.sourceInputs[key]=cell;
    }
  }
  staged.sourceInputs[binding]=input;
  qint64 pixels=0;
  for(const auto& cell:sampleInputs(staged))for(const auto& image:cell.frames) {
    if(!image.CountObj())throw std::runtime_error("样本输入像素无效");
    pixels+=qint64(image.Width())*qint64(image.Height());
    if(pixels>128000000)throw std::runtime_error("样本输入像素总量超过工程读写预算");
  }
  const auto previousWorkflow=staged.workflow;staged.workflow=workflow;
  applyLegacyInputOverrides(staged,false);staged.workflow=previousWorkflow;
  return staged;
}
}
