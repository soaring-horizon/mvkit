// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "runtime/execution.hpp"
namespace workbench {
struct DeliveryRecord {
  QString imageId, imagePath, mode, utc;
  QStringList nodeIds, nodeNames;
  QImage image;
  Result result;
  QJsonObject capture;
  bool current = false;
};
QString deliveryExplanation(const DeliveryRecord& record);
int deliveryIssueNode(const DeliveryRecord& record);
QString deliveryState(const DeliveryRecord& record);
QString exportDelivery(const QVector<DeliveryRecord>& records,
                       const QString& parent, bool annotated);
}  // namespace workbench
