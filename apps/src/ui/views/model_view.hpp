// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QVector3D>
#include <QWidget>
#include <mvkit/cpp/mvkit.hpp>

#include "model/spatial_data.hpp"
namespace workbench {
class ElidedLabel;
class ModelView final : public QWidget {
  Q_OBJECT
 public:
  explicit ModelView(QWidget* parent = nullptr);
  void load(const QString& path);
  int pointCount() const { return pointCount_; }
  void resetCamera();
  void setSpatial(std::shared_ptr<const SpatialData> data);
  void setResidualDisplay(const QImage& colors, double tolerance, bool onlyOutside);
  void highlightPixel(int row, int column);
  void setCompact(bool compact);
  QPointF pixelPosition(int row, int column) const;
 signals:
  void loadRequested(const QString& path);
  void pixelPicked(int row, int column);

 protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void wheelEvent(QWheelEvent*) override;
  void resizeEvent(QResizeEvent*) override;

 private:
  mv::MvObjectModel3D model_;
  std::shared_ptr<const SpatialData> spatial_;
  QImage residualColors_;
  double residualTolerance_ = .1;
  QVector<int> pointIds_;
  QVector<QPair<QPointF, int>> projectedIds_;
  QVector3D center_;
  double extent_ = 1;
  int selected_ = -1, contentTop_ = 40;
  QWidget* header_ = nullptr;
  ElidedLabel* footer_ = nullptr;
  void updateFooter();
  double projectionSize() const;
  bool fit_ = true;
  double manualSize_ = 1;
  QPoint press_;
  QString selectionText_;
  QVector<QVector3D> points_;
  QString name_;
  int pointCount_ = 0;
  float yaw_ = -25, pitch_ = 25, scale_ = 1;
  QPoint last_;
  QPointF pan_;
  Qt::MouseButton drag_ = Qt::NoButton;
};
}  // namespace workbench
