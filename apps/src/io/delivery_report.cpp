// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "io/delivery_report.hpp"

#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>
#include <stdexcept>

#include "operators/operator_registry.hpp"
namespace workbench {
namespace {
QString csv(QString value) {
  // Spreadsheet applications treat leading =,+,-,@ as formulas. Numeric
  // values are generated separately; protect user-controlled text columns.
  if (!value.isEmpty() && QString("=+-@\t\r").contains(value.front()))
    value.prepend('\'');
  value.replace('"', "\"\"");
  return '"' + value + '"';
}
void write(const QString& file, const QByteArray& bytes) {
  QSaveFile out(file);
  if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() ||
      !out.commit())
    throw std::runtime_error("导出文件写入失败");
}
}  // namespace
QString deliveryState(const DeliveryRecord& r) {
  if (r.utc.isEmpty()) return QStringLiteral("未运行");
  if (!r.current) return QStringLiteral("结果已过期");
  if (!r.result.error.isEmpty()) return QStringLiteral("执行失败");
  bool verdict = false, ng = false;
  auto scan = [&](const QVector<ResultObject>& objects) {
    for (const auto& o : objects)
      if (o.verdict >= 0) {
        verdict = true;
        ng |= o.verdict == 0;
      }
  };
  if (r.result.nodeOutputs.isEmpty())
    scan(r.result.objects);
  else
    for (const auto& n : r.result.nodeOutputs) scan(n.objects);
  return ng        ? QStringLiteral("NG")
         : verdict ? QStringLiteral("OK")
                   : QStringLiteral("未判定");
}
int deliveryIssueNode(const DeliveryRecord& r) {
  if(!r.current || r.utc.isEmpty())return -1;
  for(int i=0;i<r.result.nodeOutputs.size();++i)
    if(r.result.nodeOutputs[i].state==NodeState::Error)return i;
  for(int i=0;i<r.result.nodeOutputs.size();++i)
    for(const auto& o:r.result.nodeOutputs[i].objects)if(o.verdict==0)return i;
  return -1;
}
QString deliveryExplanation(const DeliveryRecord& r) {
  if(r.utc.isEmpty())return QStringLiteral("尚未运行");
  if(!r.current)return QStringLiteral("配置已更改，需重新运行");
  if(!r.result.error.isEmpty())return r.result.error;
  QStringList reasons;
  auto scan=[&](const QVector<ResultObject>& objects,const QString& node){
    for(const auto& o:objects)if(o.verdict==0) {
      const auto value=o.values.value("value",o.measure);
      reasons<<QStringLiteral("%1：%2 %3，允许 [%4, %5] %3")
        .arg(node).arg(value,0,'g',8).arg(o.unit)
        .arg(o.values.value("lower"),0,'g',8).arg(o.values.value("upper"),0,'g',8);
    }
  };
  if(r.result.nodeOutputs.isEmpty())scan(r.result.objects,QStringLiteral("判定"));
  else for(int i=0;i<r.result.nodeOutputs.size();++i)scan(r.result.nodeOutputs[i].objects,r.nodeNames.value(i));
  return reasons.isEmpty() ? deliveryState(r) : reasons.join(QStringLiteral("；"));
}
QString exportDelivery(const QVector<DeliveryRecord>& records,
                       const QString& parent, bool annotated) {
  if (records.isEmpty()) throw std::runtime_error("没有可导出的图像资源");
  if (annotated)
    for (const auto& r : records)
      if (!r.capture.isEmpty() && r.capture["kind"] != "image" && r.capture["kind"] != "sample")
        throw std::runtime_error(
            "复合输入样本支持 CSV "
            "导出；二维标注图需单独指定投影坐标，暂不混合导出");
  QDir root(parent);
  if (!root.exists()) throw std::runtime_error("请选择已有的导出目录");
  const auto finalFolder = root.filePath(
      "MVKit-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") +
      '-' + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
  QTemporaryDir temporary(root.filePath(".mvkit-export-XXXXXX"));
  if (!temporary.isValid()) throw std::runtime_error("无法创建结果目录");
  const auto folder = temporary.path();
  QString text =
      QString::fromUtf8("\xEF\xBB\xBF") +
      "image_id,image,mode,run_utc,image_state,node_id,node,node_state,target,"
      "object,type,field,value,unit,verdict,error,algorithm_ms,total_ms\r\n";
  auto row = [&](const DeliveryRecord& r, int node, const QString& state,
                 int target, int object, const QString& type,
                 const QString& field, const QString& value,
                 const QString& unit, int verdict, const QString& error) {
    QStringList fields{r.imageId,
                       QFileInfo(r.imagePath).fileName(),
                       r.mode,
                       r.utc,
                       deliveryState(r),
                       r.nodeIds.value(node),
                       r.nodeNames.value(node),
                       state,
                       QString::number(target),
                       QString::number(object),
                       type,
                       field};
    for (auto& x : fields) x = csv(x);
    fields << value << csv(unit)
           << csv(verdict < 0 ? QString()
                  : verdict   ? "OK"
                              : "NG")
           << csv(error)
           << (r.current ? QString::number(r.result.algorithmMs, 'g', 17)
                         : QString())
           << (r.current ? QString::number(r.result.totalMs, 'g', 17)
                         : QString());
    text += fields.join(',') + "\r\n";
  };
  QJsonArray manifest;
  for (int index = 0; index < records.size(); ++index) {
    const auto& r = records[index];
    QJsonObject info{{"imageId", r.imageId},      {"source", r.imagePath},
                     {"captureGroup", r.capture}, {"mode", r.mode},
                     {"runUtc", r.utc},           {"state", deliveryState(r)},
                     {"current", r.current}};
    if (!r.current || r.utc.isEmpty()) {
      row(r, -1, deliveryState(r), 0, 0, {}, {}, {}, {}, -1, {});
      manifest.append(info);
      continue;
    }
    QJsonArray configuration;
    for (int n = 0; n < r.result.nodeOutputs.size(); ++n) {
      const auto& p = r.result.nodeOutputs[n].parameters;
      QJsonObject node{{"id", r.nodeIds.value(n)},
                      {"name", r.nodeNames.value(n)},
                      {"operator", operatorDefinition(p.algorithm).key},
                      {"options", p.options},
                      {"minimum", p.minimum},
                      {"maximum", p.maximum},
                      {"opening", p.opening},
                      {"minimumArea", p.minimumArea}};
      const auto& output=r.result.nodeOutputs[n];
      node["coordinateFrame"]=output.coordinateFrame;
      node["modelKey"]=output.modelKey;
      if(!p.calibrationArtifact.isEmpty())node["calibrationSha256"]=QString::fromLatin1(QCryptographicHash::hash(p.calibrationArtifact,QCryptographicHash::Sha256).toHex());
      if(output.sourceData) {
        QJsonArray sourceFrames;
        for(int f=0;f<output.sourceData->paths.size();++f)sourceFrames.append(QJsonObject{{"path",output.sourceData->paths[f]},{"pixelSha256",output.sourceData->keys.value(f)}});
        node["sourceFrames"]=sourceFrames;node["sourceBinding"]=p.options.value("binding");
      }
      configuration.append(node);
    }
    info["nodes"] = configuration;
    auto emitObjects = [&](int node, const QVector<ResultObject>& objects,
                           const QString& state, const QString& error) {
      if (objects.isEmpty())
        row(r, node, state, 0, 0, {}, {}, {}, {}, -1, error);
      for (int i = 0; i < objects.size(); ++i) {
        const auto& o = objects[i];
        row(r, node, state, o.instance, i + 1, o.type, "value",
            QString::number(o.measure, 'g', 17), o.unit, o.verdict, error);
        for (auto it = o.values.begin(); it != o.values.end(); ++it)
          if (it.key() != "value")
            row(r, node, state, o.instance, i + 1, o.type, it.key(),
                QString::number(it.value(), 'g', 17), fieldUnit(o, it.key()),
                o.verdict, error);
      }
    };
    if (r.result.nodeOutputs.isEmpty())
      emitObjects(-1, r.result.objects,
                  r.result.error.isEmpty() ? QStringLiteral("成功")
                                           : QStringLiteral("报错"),
                  r.result.error);
    else
      for (int n = 0; n < r.result.nodeOutputs.size(); ++n) {
        const auto& out = r.result.nodeOutputs[n];
        const auto state = QStringList{
            QStringLiteral("未执行"), QStringLiteral("成功"),
            QStringLiteral("报错"), QStringLiteral("受阻")}[int(out.state)];
        emitObjects(n, out.objects, state, out.error);
        for (const auto& target : out.instances)
          if (target.state != NodeState::Success)
            row(r, n, state, target.index, 0, {}, {}, {}, {}, -1, target.error);
      }
    if (annotated && !r.image.isNull()) {
      auto picture = r.image.convertToFormat(QImage::Format_RGB888);
      QPainter painter(&picture);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.setFont(QFont("Microsoft YaHei UI", 10));
      int drawn = 0;
      auto draw = [&](const QVector<ResultObject>& objects) {
        for (const auto& o : objects) {
          if (o.overlay.isEmpty()) continue;
          if (++drawn > 10000) throw std::runtime_error("标注对象超过预算");
          painter.setPen(QPen(
              o.verdict == 0 ? QColor("#df695f") : QColor("#28b59d"), 1.5));
          painter.drawPath(o.overlay);
          painter.drawText(o.center + QPointF(5, -5),
                           o.label + " " + QString::number(o.measure, 'g', 7) +
                               " " + o.unit);
        }
      };
      bool judged = false;
      for (const auto& n : r.result.nodeOutputs)
        for (const auto& o : n.objects) judged |= o.verdict >= 0;
      if (judged) {
        for (const auto& n : r.result.nodeOutputs)
          if (!n.objects.isEmpty() && n.objects.first().type == "Verdict")
            draw(n.objects);
      } else
        draw(r.result.objects);
      painter.fillRect(QRect(0, 0, picture.width(), 24),
                       QColor(20, 28, 40, 220));
      painter.setPen(Qt::white);
      painter.drawText(QPoint(6, 17), deliveryState(r) + " · " +
                                          QFileInfo(r.imagePath).fileName());
      painter.end();
      const auto name = QString("%1.png").arg(index + 1, 4, 10, QChar('0'));
      QSaveFile png(folder + '/' + name);
      if (!png.open(QIODevice::WriteOnly) || !picture.save(&png, "PNG") ||
          !png.commit())
        throw std::runtime_error("标注图保存失败");
      info["annotated"] = name;
    }
    manifest.append(info);
  }
  write(folder + "/results.csv", text.toUtf8());
  write(folder + "/manifest.json",
        QJsonDocument(
            QJsonObject{{"format", "mvkit-studio-report"},
                        {"version", 1},
                        {"createdUtc", QDateTime::currentDateTimeUtc().toString(
                                           Qt::ISODateWithMs)},
                        {"images", manifest}})
            .toJson());
  if (!QDir().rename(folder, finalFolder))
    throw std::runtime_error("无法完成结果目录保存");
  temporary.setAutoRemove(false);
  return finalFolder;
}
}  // namespace workbench
