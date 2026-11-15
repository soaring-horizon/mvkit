// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "model/sample_inputs.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <algorithm>
#include <stdexcept>
namespace workbench {
QMap<QString, SourceData> sampleInputs(const Document& d) {
  if(!d.sourceInputs.isEmpty())return d.sourceInputs;
  QMap<QString,SourceData> inputs;
  if(!d.structuredFrames.isEmpty()) inputs["sequence"]=resolveSourceData(d,"sequence");
  else if(d.rightImage.CountObj()) {
    inputs["left"]=resolveSourceData(d,"left");
    inputs["right"]=resolveSourceData(d,"right");
  } else if(d.image.CountObj()) inputs["image"]=resolveSourceData(d,"image");
  for(auto it=d.sourceInputs.begin();it!=d.sourceInputs.end();++it)inputs[it.key()]=it.value();
  return inputs;
}
QString sampleKind(const Document& d) {
  if(!d.structuredFrames.isEmpty())return "structured-light";
  if(d.rightImage.CountObj())return "stereo";
  const auto inputs=sampleInputs(d);
  if(inputs.size()>1)return "multi-input";
  if(!inputs.isEmpty()&&!inputs.first().views.isEmpty())return "multi-view";
  if(!inputs.isEmpty() && inputs.first().sequence)return "sequence";
  return "image";
}
QJsonObject sampleProvenance(const Document& d,const QString& identity) {
  auto sha=[](const QByteArray& b){return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex());};
  QJsonArray columns;
  const auto inputs=sampleInputs(d);
  for(auto it=inputs.begin();it!=inputs.end();++it) {
    QJsonArray frames;
    for(int i=0;i<it->paths.size();++i)frames.append(QJsonObject{{"path",it->paths[i]},{"pixelSha256",it->keys.value(i)}});
    columns.append(QJsonObject{{"binding",it.key()},{"sequence",it->sequence},{"views",QJsonArray::fromStringList(it->views)},{"missing",it->frames.isEmpty()},{"frames",frames}});
  }
  QJsonObject result{{"id",d.captureId.isEmpty()?identity:d.captureId},{"name",d.captureName},
    {"kind",sampleKind(d)},{"inputs",columns},{"referencePixelSha",d.pixelKey}};
  if(!d.calibrationInputs.isEmpty())result["calibrationInputs"]=d.calibrationInputs;
  if(d.rightImage.CountObj()) {
    result["left"]=d.imagePath;result["right"]=d.rightPath;
    result["leftPixelSha"]=d.pixelKey;result["rightPixelSha"]=d.rightPixelKey;
  }
  if(!d.stereoArtifact.isEmpty()) {result["calibrationName"]=d.stereoName;result["calibrationSha"]=sha(d.stereoArtifact);}
  if(!d.structuredModel.isEmpty()) {result["modelName"]=d.stereoName;result["modelSha"]=sha(d.structuredModel);}
  return result;
}
void applyLegacyInputOverrides(Document& d,bool requireComplete) {
  if(d.sourceInputs.isEmpty())return;
  bool imageChanged=false;
  if(std::any_of(d.workflow.begin(),d.workflow.end(),[](const Parameters& p){return sourceTool(p.algorithm);}))return;
  for(const auto& binding:QStringList{"image","left","right","sequence"}) {
    if(!d.sourceInputs.contains(binding))continue;
    const auto cell=d.sourceInputs[binding];
    if(cell.frames.isEmpty()) {
      if(requireComplete)throw std::runtime_error((QStringLiteral("当前样本缺少输入：")+binding+QStringLiteral("，请在资源管理中配置")).toUtf8().constData());
      continue;
    }
    if(cell.frames.size()!=cell.paths.size()||cell.keys.size()!=cell.paths.size()||cell.sequence!=(binding=="sequence"))
      throw std::runtime_error("样本输入类型或帧信息不完整");
    if(binding=="right") {
      d.rightImage=cell.frames.first();d.rightPath=cell.paths.first();d.rightPixelKey=cell.keys.first();d.rightPreview=makePreview(d.rightImage).image;
    } else {
      imageChanged|=d.imagePath!=cell.paths.first() || d.pixelKey!=cell.keys.first();
      d.image=cell.frames.first();d.imagePath=cell.paths.first();d.pixelKey=cell.keys.first();d.preview=makePreview(d.image).image;
      if(binding=="sequence") {d.structuredFrames=cell.frames;d.structuredPaths=cell.paths;d.structuredKeys=cell.keys;}
    }
  }
  if(imageChanged)d.coordinateFrame=d.imagePath;
}
}
