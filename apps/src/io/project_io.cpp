// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "io/project_io.hpp"
#include "io/input_migration.hpp"
#include "model/document.hpp"
#include "model/project_state.hpp"
#include "operators/range_model.hpp"
#include "model/metadata_limits.hpp"
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QMap>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <functional>
#include <stdexcept>
namespace workbench {
namespace {
void require(bool ok,const QString& message){if(!ok)throw std::runtime_error(message.toUtf8().constData());}
QString absolute(const QString& project,const QString& path){return QDir::cleanPath(QFileInfo(project).absoluteDir().absoluteFilePath(path));}
QString pathKey(QString path){
#ifdef Q_OS_WIN
  return path.toCaseFolded();
#else
  return path;
#endif
}
using Rewrite=std::function<QString(const QString&,const QString&,bool)>;
QJsonValue references(QJsonValue value,const Rewrite& rewrite,const QString& location={}) {
  if(value.isArray()) {auto array=value.toArray();for(int i=0;i<array.size();++i)array[i]=references(array[i],rewrite,location+"/"+QString::number(i));return array;}
  if(!value.isObject())return value;
  auto object=value.toObject();
  auto apply=[&](const char* field,const QString& hash,bool pixels){
    const auto path=object.value(field).toString();if(!path.isEmpty())object[field]=rewrite(path,hash,pixels);
  };
  if(object.value("format")=="mvkit-workbench") {
    const auto capture=object.value("capture").toObject();
    auto hash=object.value("imagePixelSha256").toString();
    if(hash.isEmpty())hash=capture["leftSha"].toString(capture["referenceSha"].toString());
    if(hash.isEmpty()&&!capture["frames"].toArray().isEmpty())hash=capture["frames"].toArray().first().toObject()["sha256"].toString();
    if(hash.isEmpty())for(const auto& input:object.value("sourceInputs").toObject()) {
      const auto cell=input.toObject();const auto files=cell["files"].toArray(),keys=cell["keys"].toArray();
      for(int i=0;i<files.size()&&i<keys.size();++i)if(files[i]==object.value("image"))hash=keys[i].toString();
    }
    apply("image",hash,true);
  }
  if(object.value("algorithm").isString()) apply("template",{},false);
  if(object.value("boardData").toString().isEmpty())apply("boardPath",object.value("boardSha").toString(),false);
  if(object.value("kind")=="stereo")apply("right",object.value("rightSha").toString(),true);
  if(object.value("file").isString()&&object.value("sha256").isString())apply("file",object.value("sha256").toString(),location.contains("/frames/"));
  if(object.value("files").isArray()&&object.value("keys").isArray()) {
    auto files=object.value("files").toArray();const auto hashes=object.value("keys").toArray();
    for(int i=0;i<files.size()&&i<hashes.size();++i)if(files[i].isString()&&!files[i].toString().isEmpty())files[i]=rewrite(files[i].toString(),hashes[i].toString(),true);
    object["files"]=files;
  }
  for(const auto& key:object.keys())if(object[key].isObject()||object[key].isArray())object[key]=references(object[key],rewrite,location+"/"+key);
  return object;
}
QString digest(const QString& path,bool pixels) {
  if(pixels)return loadImage(path).pixelKey;
  QFile file(path);require(file.open(QIODevice::ReadOnly),QStringLiteral("无法读取资源：")+path);
  QCryptographicHash hash(QCryptographicHash::Sha256);require(hash.addData(&file),QStringLiteral("资源校验失败：")+path);
  return QString::fromLatin1(hash.result().toHex());
}
}
QJsonObject projectStateJson(const ProjectState& state,const QString& path) {
  require(state.workflow.size()<=32 && state.nodeIds.size()==state.workflow.size() && state.nodeNames.size()==state.workflow.size(),QStringLiteral("流程身份数量无效"));
  QJsonArray resources,flow,sources,links,positions,shared,basic;QSet<QString> identities;
  for(const auto& sample:state.samples) {
    require(!sample.id.isEmpty() && sample.id.size()<=128 && !identities.contains(sample.id),QStringLiteral("样本身份无效或重复"));identities.insert(sample.id);
    auto document=sample.document;
    document.nodeSettings.resize(state.workflow.size());document.rangeOverrides.resize(state.workflow.size());
    document.workbenchRanges.resize(toolSlotCount);document.workbenchOverrides.resize(toolSlotCount);
    auto entry=projectJson(document,path,true);entry["resourceId"]=sample.id;entry["imagePixelSha256"]=document.pixelKey;entry["runSelected"]=sample.selected;resources.append(entry);
  }
  for(const auto& p:state.workflow)flow.append(QJsonObject{{"parameters",parametersJson(p,path)}});
  for(int input:state.inputs)sources.append(input);
  for(const auto& row:state.links){QJsonArray array;for(int source:row)array.append(source);links.append(array);}
  for(const auto& point:state.view.positions)positions.append(QJsonArray{point.x(),point.y()});
  for(const auto& settings:state.sharedRanges)shared.append(nodeSettingsJson(settings));
  for(const auto& settings:state.basicRanges)basic.append(nodeSettingsJson(settings));
  return {{"format","mvkit-studio"},{"version",studioProjectVersion},{"angleConvention","image-clockwise"},
    {"images",resources},{"workflow",flow},{"workflowInputs",sources},{"workflowLinks",links},{"workflowOutput",state.output},
    {"nodeIds",QJsonArray::fromStringList(state.nodeIds)},{"nodeAliases",QJsonArray::fromStringList(state.nodeNames)},
    {"sharedRanges",shared},{"workbenchRanges",basic},{"basicParameters",parametersJson(state.basicParameters,path)},
    {"models",saveModels(state.models,path)},{"active",state.view.active},{"selectedNode",state.view.selectedNode},
    {"debugNodeId",state.view.debugNodeId},{"activeInput",state.view.inputKey},{"inputTracks",state.view.preferredInputs},{"mode",state.view.mode},{"workflowPositions",positions}};
}
QJsonObject readProjectConfiguration(const QString& path) {
  QFile file(path);require(file.open(QIODevice::ReadOnly)&&file.size()<=metadataByteLimit,QStringLiteral("无法读取工程，或工程配置超过128 MiB：")+path);
  QJsonParseError error;const auto json=QJsonDocument::fromJson(file.readAll(),&error);
  require(error.error==QJsonParseError::NoError&&json.isObject(),QStringLiteral("工程JSON无效：")+error.errorString());return namedWorkflowInputs(json.object(), path);
}
void writeProjectConfiguration(const QJsonObject& object,const QString& path) {
  const auto bytes=QJsonDocument(object).toJson();require(bytes.size()<=metadataByteLimit,QStringLiteral("工程配置超过128 MiB"));
  QSaveFile file(path);require(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit(),QStringLiteral("工程写入失败：")+file.errorString());
}
void writeProjectWithOriginalData(const QJsonObject& object,const QString& path) {
  const QDir parent = QFileInfo(path).absoluteDir();
  const auto directory = QFileInfo(path).absoluteFilePath() + ".data";
  const bool existed = QFileInfo::exists(directory);
  require(QDir().mkpath(directory), QStringLiteral("无法创建工程数据目录：") + directory);
  QStringList created, createdDirectories;
  QMap<QString,QString> copies;
  try {
    const auto copied = references(object,[&](const QString& file,const QString& fingerprint,bool pixels) {
      const auto source = absolute(path,file);
      if (!fingerprint.isEmpty())
        require(digest(source,pixels)==fingerprint, QStringLiteral("原始数据已改变，请重新加载后再保存：")+source);
      const auto sourceKey = pathKey(source);
      if (copies.contains(sourceKey)) return copies[sourceKey];
      const auto hash = digest(source,false);
      const auto bucket=QDir(directory).filePath(hash);
      const auto target=QDir(bucket).filePath(QFileInfo(source).fileName());
      if (!QFileInfo::exists(bucket)) {
        require(QDir().mkdir(bucket),QStringLiteral("无法创建工程资源目录：")+bucket);
        createdDirectories.append(bucket);
      }
      if (QFileInfo::exists(target)) {
        require(digest(target,false)==hash, QStringLiteral("已有工程数据损坏：")+target);
      } else {
        QFile input(source);
        require(input.open(QIODevice::ReadOnly), QStringLiteral("无法读取原始数据：")+source);
        QSaveFile output(target);
        require(output.open(QIODevice::WriteOnly), QStringLiteral("无法复制原始数据：")+target);
        while (!input.atEnd()) {
          const auto block=input.read(1024*1024);
          require(!block.isEmpty() && output.write(block)==block.size(), QStringLiteral("原始数据复制失败：")+source);
        }
        require(input.error()==QFile::NoError && output.commit(), QStringLiteral("原始数据复制失败：")+source);
        created.append(target);
        require(digest(target,false)==hash, QStringLiteral("复制后数据校验失败：")+source);
      }
      if (!fingerprint.isEmpty())
        require(digest(target,pixels)==fingerprint,QStringLiteral("复制的数据与当前工程不符：")+source);
      const auto relative=parent.relativeFilePath(target);
      copies.insert(sourceKey,relative);
      return relative;
    }).toObject();
    writeProjectConfiguration(copied,path);
  } catch (...) {
    for (const auto& file:created) QFile::remove(file);
    for (const auto& createdDirectory:createdDirectories) QDir().rmdir(createdDirectory);
    if (!existed) QDir().rmdir(directory);
    throw;
  }
}
QJsonObject rebaseProjectResources(const QJsonObject& object,const QString& source,const QString& target) {
  return references(object,[&](const QString& file,const QString&,bool) {
    return QFileInfo(target).absoluteDir().relativeFilePath(absolute(source,file));
  }).toObject();
}
QVector<ProjectResource> projectResources(const QJsonObject& object,const QString& project) {
  QVector<ProjectResource> result;QSet<QString> seen;
  references(object,[&](const QString& file,const QString& hash,bool pixels){
    const auto path=absolute(project,file),key=pathKey(path)+"|"+hash+(pixels?"|pixels":"|bytes");
    if(!seen.contains(key)){seen.insert(key);result.append({path,hash,pixels});}return file;
  });return result;
}
QStringList missingProjectResources(const QJsonObject& object,const QString& project) {
  QStringList result;for(const auto& resource:projectResources(object,project))if(!QFileInfo::exists(resource.path)&&!result.contains(resource.path))result<<resource.path;return result;
}
QJsonObject relocateProjectResources(const QJsonObject& object,const QString& project,const QString& searchRoot,const std::atomic_bool* cancelled) {
  const auto root=QFileInfo(searchRoot).canonicalFilePath();require(!root.isEmpty()&&QFileInfo(root).isDir(),QStringLiteral("请选择可读取的资源根目录"));
  const auto prefix=pathKey(root).endsWith('/')?pathKey(root):pathKey(root)+"/";
  QMap<QString,QStringList> index;QDirIterator files(root,QDir::Files|QDir::NoSymLinks,QDirIterator::Subdirectories);int count=0;
  while(files.hasNext()) {
    if(cancelled&&cancelled->load())return object;
    files.next();const auto canonical=files.fileInfo().canonicalFilePath();
    if(canonical.isEmpty()||!pathKey(canonical).startsWith(prefix))continue;
    require(++count<=100000,QStringLiteral("搜索目录超过100000个文件，请缩小资源目录"));
    index[files.fileInfo().fileName().toCaseFolded()]<<canonical;
  }
  QMap<QString,QString> replacements;
  for(const auto& resource:projectResources(object,project)) {
    if(cancelled&&cancelled->load())return object;
    if(QFileInfo::exists(resource.path))continue;
    require(resource.fingerprint.size()==64,QStringLiteral("缺失资源没有有效指纹，不能自动猜测同名文件；请恢复原路径或重新加载原始数据：")+resource.path);
    auto candidates=index.value(QFileInfo(resource.path).fileName().toCaseFolded());candidates.sort();QString match;
    for(const auto& candidate:candidates) {
      if(cancelled&&cancelled->load())return object;
      try {if(digest(candidate,resource.pixels)==resource.fingerprint){match=candidate;break;}}catch(const std::exception&){}
    }
    require(!match.isEmpty(),QStringLiteral("未找到名称及内容指纹一致的资源（同名异内容不会替换）：")+resource.path);
    const auto key=pathKey(resource.path);require(!replacements.contains(key)||replacements[key]==match,QStringLiteral("同一路径的资源指纹冲突：")+resource.path);replacements[key]=match;
  }
  return references(object,[&](const QString& file,const QString&,bool){
    const auto key=pathKey(absolute(project,file));return replacements.contains(key)?QFileInfo(project).absoluteDir().relativeFilePath(replacements[key]):file;
  }).toObject();
}
}
