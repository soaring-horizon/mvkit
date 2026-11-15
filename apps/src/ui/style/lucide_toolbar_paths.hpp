// Generated from the pinned Lucide Icons subset; do not edit.
// Copyright (c) 2026 Lucide Icons and Contributors. SPDX-License-Identifier: ISC
// Original SVGs, commit, hashes and LICENSE: apps/third-party/lucide-icons
#pragma once
#include <QHash>
#include <QPainterPath>
#include <QString>
#include <QVector>
namespace workbench { namespace detail {
inline const QVector<QPainterPath>& lucideToolbarPaths(const QString& name) {
  static const QHash<QString,QVector<QPainterPath>> assets = [] {
    QHash<QString,QVector<QPainterPath>> result;
    {
      QVector<QPainterPath> paths;
      {
        QPainterPath path;
        path.addRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
        paths.append(path);
      }
      {
        QPainterPath path;
        path.addEllipse(QPointF(9, 9), 2, 2);
        paths.append(path);
      }
      {
        QPainterPath path;
        path.moveTo(21, 15);
        path.lineTo(17.914, 11.914);
        path.cubicTo(17.1330003, 11.1332361, 15.8669997, 11.1332361, 15.086, 11.914);
        path.lineTo(6, 21);
        paths.append(path);
      }
      result.insert(QStringLiteral("image"),paths);
    }
    {
      QVector<QPainterPath> paths;
      {
        QPainterPath path;
        path.moveTo(9, 14);
        path.lineTo(4, 9);
        path.lineTo(9, 4);
        paths.append(path);
      }
      {
        QPainterPath path;
        path.moveTo(4, 9);
        path.lineTo(14.5, 9);
        path.cubicTo(17.5375661, 9, 20, 11.4624339, 20, 14.5);
        path.cubicTo(20, 17.5375661, 17.5375661, 20, 14.5, 20);
        path.lineTo(11, 20);
        paths.append(path);
      }
      result.insert(QStringLiteral("undo-2"),paths);
    }
    {
      QVector<QPainterPath> paths;
      {
        QPainterPath path;
        path.moveTo(15, 14);
        path.lineTo(20, 9);
        path.lineTo(15, 4);
        paths.append(path);
      }
      {
        QPainterPath path;
        path.moveTo(20, 9);
        path.lineTo(9.5, 9);
        path.cubicTo(6.46243388, 9, 4, 11.4624339, 4, 14.5);
        path.cubicTo(4, 17.5375661, 6.46243388, 20, 9.5, 20);
        path.lineTo(13, 20);
        paths.append(path);
      }
      result.insert(QStringLiteral("redo-2"),paths);
    }
    {
      QVector<QPainterPath> paths;
      {
        QPainterPath path;
        path.addRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
        paths.append(path);
      }
      {
        QPainterPath path;
        path.moveTo(3, 9);
        path.lineTo(21, 9);
        paths.append(path);
      }
      {
        QPainterPath path;
        path.moveTo(9, 21);
        path.lineTo(9, 9);
        paths.append(path);
      }
      result.insert(QStringLiteral("panels-top-left"),paths);
    }
    return result;
  }();
  static const QHash<QString,QString> aliases{
    {QStringLiteral("layout"),QStringLiteral("panels-top-left")},
    {QStringLiteral("redo"),QStringLiteral("redo-2")},
    {QStringLiteral("undo"),QStringLiteral("undo-2")},
    {QStringLiteral("view-2d"),QStringLiteral("image")},
  };
  static const QVector<QPainterPath> empty;
  const auto found=assets.constFind(aliases.value(name));
  return found==assets.constEnd() ? empty : found.value();
}
} }
