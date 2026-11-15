// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/views/model_view.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "ui/style/appearance.hpp"
#include "ui/widgets/responsive_widgets.hpp"
namespace workbench {
ModelView::ModelView(QWidget* parent) : QWidget(parent) {
  setObjectName("modelView");
  setFocusPolicy(Qt::StrongFocus);
  setMinimumWidth(180);
  setMinimumHeight(120);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  auto* header = new QWidget(this);
  header_ = header;
  header->setObjectName("modelToolbar");
  header->setFixedHeight(40);
  auto* row = new QHBoxLayout(header);
  row->setContentsMargins(10, 2, 10, 2);
  auto* viewIcon = new QLabel(header);
  viewIcon->setObjectName("modelViewHeaderIcon");
  viewIcon->setFixedSize(16,16);
  viewIcon->setPixmap(studioIcon("cube").pixmap(16,16));
  row->addWidget(viewIcon);
  row->addWidget(new QLabel(QStringLiteral("3D 视图"), header));
  row->addStretch();
  auto* load =
      new QPushButton(studioIcon("folder"), QStringLiteral("导入模型"), header);
  load->setObjectName("loadModelButton");
  load->setToolTip(QStringLiteral("导入三维模型"));
  auto* reset =
      new QPushButton(studioIcon("fit"), QStringLiteral("复位"), header);
  reset->setObjectName("resetModelButton");
  reset->setToolTip(QStringLiteral("复位相机并持续适应视图"));
  row->addWidget(load);
  row->addWidget(reset);
  layout->addWidget(header);
  layout->addStretch();
  footer_ = new ElidedLabel(this);
  footer_->setObjectName("modelFooter");
  connect(reset, &QPushButton::clicked, this, &ModelView::resetCamera);
  connect(load, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入三维点云"), {},
        QStringLiteral("PLY / MVKit 模型 (*.ply *.mvkit);;所有文件 (*)"));
    if (!path.isEmpty()) emit loadRequested(path);
  });
}
void ModelView::load(const QString& path) {
  const QFileInfo file(path);
  if (!file.exists() || file.size() > 128 * 1024 * 1024)
    throw std::runtime_error("模型不存在或超过 128 MB 显示预算");
  mv::MvObjectModel3D model;
  model.ReadObjectModel3d(mv::MvString(path.toUtf8().constData()),
                          mv::MvTuple(1.), {}, {});
  const auto x = model.GetObjectModel3dParams("point_coord_x"),
             y = model.GetObjectModel3dParams("point_coord_y"),
             z = model.GetObjectModel3dParams("point_coord_z");
  if (!x.Length() || x.Length() > 2000000)
    throw std::runtime_error("模型无有效点或超过 200 万点显示预算");
  double lo[3] = {INFINITY, INFINITY, INFINITY},
         hi[3] = {-INFINITY, -INFINITY, -INFINITY};
  for (size_t i = 0; i < x.Length(); ++i) {
    const double v[3] = {x.D(i), y.D(i), z.D(i)};
    for (int c = 0; c < 3; ++c) {
      if (!std::isfinite(v[c])) throw std::runtime_error("模型包含非有限坐标");
      lo[c] = std::min(lo[c], v[c]);
      hi[c] = std::max(hi[c], v[c]);
    }
  }
  const double extent =
      std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], 1e-12});
  if (!std::isfinite(extent)) throw std::runtime_error("模型坐标范围过大");
  QVector<QVector3D> points;
  const size_t step = std::max(size_t(1), (x.Length() + 99999) / 100000);
  for (size_t i = 0; i < x.Length(); i += step)
    points.append(
        QVector3D(float((x.D(i) - (lo[0] / 2 + hi[0] / 2)) / extent),
                  float((y.D(i) - (lo[1] / 2 + hi[1] / 2)) / extent),
                  float((z.D(i) - (lo[2] / 2 + hi[2] / 2)) / extent)));
  spatial_.reset();
  projectedIds_.clear();
  residualColors_ = {};
  selected_ = -1;
  selectionText_.clear();
  model_ = std::move(model);
  points_ = std::move(points);
  pointCount_ = int(x.Length());
  name_ = file.fileName();
  resetCamera();
}
void ModelView::resetCamera() {
  yaw_ = -25;
  pitch_ = 25;
  scale_ = 1;
  fit_ = true;
  pan_ = {};
  updateFooter();
  update();
}
double ModelView::projectionSize() const {
  return fit_ ? std::max(1, std::min(width(), height() - contentTop_)) * .72
              : manualSize_;
}
void ModelView::updateFooter() {
  if (!footer_) return;
  const auto full = QStringLiteral("%1 · %2 点%3")
      .arg(name_).arg(pointCount_)
      .arg(points_.size() < pointCount_
               ? QStringLiteral(" · 显示采样 %1 点").arg(points_.size()) : QString());
  const auto text = width() < 430
                        ? QStringLiteral("%1 点%2").arg(pointCount_).arg(spatial_ ? " · mm" : "")
                        : full;
  footer_->setGeometry(12, height() - 40, std::max(0, width() - 24), 30);
  footer_->setText(text);
  footer_->setToolTip(full + (selectionText_.isEmpty() ? QString() : "\n" + selectionText_));
  footer_->setVisible(!points_.isEmpty() && height() - contentTop_ > 65);
  footer_->raise();
}
void ModelView::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  findChild<QPushButton*>("loadModelButton")->setText(width() >= 360 ? QStringLiteral("导入模型") : QString());
  findChild<QPushButton*>("resetModelButton")->setText(width() >= 280 ? QStringLiteral("复位") : QString());
  updateFooter();
}
void ModelView::paintEvent(QPaintEvent*) {
  projectedIds_.clear();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const bool dark = palette().color(QPalette::Window).lightness() < 128;
  p.fillRect(rect(), QColor(dark ? "#171d26" : "#e9edf4"));
  const QRectF area(0, contentTop_, width(),
                    std::max(1, height() - contentTop_));
  p.setClipRect(area);
  p.setPen(QColor(dark ? "#293344" : "#d6deea"));
  for (int x = 0; x < width(); x += 32) p.drawLine(x, contentTop_, x, height());
  for (int y = contentTop_; y < height(); y += 32) p.drawLine(0, y, width(), y);
  p.setPen(palette().color(QPalette::WindowText));
  if (points_.isEmpty()) {
    p.drawText(
        area, Qt::AlignCenter,
        spatial_ && spatial_->primary == "disparity"
            ? QStringLiteral("当前为视差输出\n选择双目重建或点云节点查看三维")
        : spatial_ && spatial_->primary == "left"
            ? QStringLiteral("当前为校正图像对\n选择双目重建或点云节点查看三维")
          : spatial_ && (spatial_->primary == "projectorColumn" || spatial_->primary == "projectorRow")
              ? QStringLiteral("当前为结构光解码输出\n选择结构光重建或点云节点查看三维")
          : (spatial_ || property("captureView").toBool())
            ? QStringLiteral(
                  "当前输出没有有效三维点\n运行流程或检查选取范围")
            : QStringLiteral("导入三维模型\n\nPLY / MVKit 点云\n左键旋转 · "
                             "中键平移 · 滚轮缩放"));
    return;
  }
  QMatrix4x4 rotation;
  rotation.rotate(pitch_, 1, 0, 0);
  rotation.rotate(yaw_, 0, 1, 0);
  const double size = projectionSize();
  const auto origin = area.center() + pan_;
  struct Vertex {
    QPointF p;
    float z;
    int id;
  };
  QVector<Vertex> projected;
  projected.reserve(points_.size());
  int ordinal = 0;
  for (const auto& point : points_) {
    const auto q = rotation * point;
    projected.append({origin + QPointF(q.x() * size, -q.y() * size), q.z(),
                      spatial_ ? pointIds_[ordinal++] : -1});
  }
  std::sort(projected.begin(), projected.end(),
            [](const Vertex& a, const Vertex& b) { return a.z < b.z; });
  for (const auto& point : projected) {
    const bool residual = spatial_ && !residualColors_.isNull();
    QColor residualColor;
    if (residual) {
      const auto& sample = spatial_->points[point.id];
      residualColor = residualColors_.pixelColor(sample.column, sample.row);
      if (residualColor.alpha() == 0) continue;
    }
    const double depth = std::clamp(double(point.z + .5), 0., 1.);
    p.setPen(
        QPen(residual ? residualColor : QColor::fromHsvF(.60 - depth * .10, dark ? .5 : .65,
                              dark ? .65 + depth * .35 : .55 + depth * .25),
             2));
    p.drawPoint(point.p);
    if (spatial_) projectedIds_.append({point.p, point.id});
  }
  if (spatial_ && selected_ >= 0 && selected_ < spatial_->points.size()) {
    const auto& v = spatial_->points[selected_];
    auto q = rotation * QVector3D(float((v.x - center_.x()) / extent_),
                                  float((v.y - center_.y()) / extent_),
                                  float((v.z - center_.z()) / extent_));
    auto at = origin + QPointF(q.x() * size, -q.y() * size);
    p.setPen(QPen(QColor("#e9ad3e"), 2));
    p.drawEllipse(at, 6, 6);
  }
  const QPointF axes(width() - 55, height() - 65);
  const QVector3D basis[]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  const QColor colors[]{QColor("#d36c67"), QColor("#41a78c"),
                        QColor("#667fd5")};
  for (int i = 0; i < 3; ++i) {
    const auto axis = rotation * basis[i];
    const auto end = axes + QPointF(axis.x() * 28, -axis.y() * 28);
    p.setPen(QPen(colors[i], 1.5));
    p.drawLine(axes, end);
    p.drawText(end + QPointF(3, 0), QString(QChar('X' + i)));
  }
  p.setPen(palette().color(QPalette::WindowText));
  if (spatial_ && selected_ >= 0 && selected_ < spatial_->points.size()) {
    const auto& q = spatial_->points[selected_];
    auto text = QStringLiteral("X %1 · Y %2 · Z %3 mm")
                    .arg(q.x, 0, 'g', 6)
                    .arg(q.y, 0, 'g', 6)
                    .arg(q.z, 0, 'g', 6);
    if (!residualColors_.isNull()) {
      const auto& image = spatial_->rasters["fitResidual"].image;
      const auto* row = reinterpret_cast<const float*>(static_cast<const char*>(image.Data()) + q.row * image.Stride());
      text += QStringLiteral(" · 残差 %1 mm%2").arg(row[q.column], 0, 'g', 6)
          .arg(std::abs(row[q.column]) > residualTolerance_ ? QStringLiteral(" · 超差") : QString());
    }
    p.drawText(QRectF(12, height() - 66, width() - 24, 24),
               Qt::AlignLeft | Qt::AlignVCenter,
               p.fontMetrics().elidedText(text, Qt::ElideRight, width() - 24));
  }
}
void ModelView::mousePressEvent(QMouseEvent* e) {
  setFocus(Qt::MouseFocusReason);
  press_ = e->pos();
  drag_ = e->button();
  last_ = e->pos();
}
void ModelView::mouseMoveEvent(QMouseEvent* e) {
  const auto delta = e->pos() - last_;
  last_ = e->pos();
  if (drag_ == Qt::LeftButton) {
    if (!delta.isNull() && fit_) { manualSize_ = projectionSize(); fit_ = false; }
    yaw_ += delta.x() * .5f;
    pitch_ = std::clamp(pitch_ + delta.y() * .5f, -89.f, 89.f);
  } else if (drag_ == Qt::MiddleButton || drag_ == Qt::RightButton) {
    if (fit_) { manualSize_ = projectionSize(); fit_ = false; }
    pan_ += delta;
    setCursor(Qt::ClosedHandCursor);
  }
  update();
}
void ModelView::mouseReleaseEvent(QMouseEvent* e) {
  if (spatial_ && drag_ == Qt::LeftButton &&
      (e->pos() - press_).manhattanLength() < 4) {
    int picked = -1;
    double best = 100;
    for (const auto& p : projectedIds_) {
      double d = QLineF(p.first, e->pos()).length();
      if (d <= 8 && d <= best) {
        best = d;
        picked = p.second;
      }
    }
    if (picked >= 0 && picked < spatial_->points.size()) {
      const auto& p = spatial_->points[picked];
      highlightPixel(p.row, p.column);
      emit pixelPicked(p.row, p.column);
    }
  }
  drag_ = Qt::NoButton;
  unsetCursor();
}
void ModelView::wheelEvent(QWheelEvent* e) {
  manualSize_ = std::clamp(projectionSize() * std::pow(1.0015, e->angleDelta().y()), 1., 1e6);
  fit_ = false;
  update();
  e->accept();
}
void ModelView::setCompact(bool compact) {
  contentTop_ = compact ? 0 : 40;
  header_->setVisible(!compact);
  updateFooter();
  update();
}
void ModelView::setSpatial(std::shared_ptr<const SpatialData> payload) {
  if (payload == spatial_ && (payload || !property("captureView").toBool())) return;
  projectedIds_.clear();
  residualColors_ = {};
  if (!payload) {
    if (!spatial_ && !property("captureView").toBool()) return;
    spatial_.reset();
    points_.clear();
    pointIds_.clear();
    pointCount_ = 0;
    selected_ = -1;
    setProperty("selectedOrdinal",0);
    setProperty("linkedRow",-1);
    setProperty("linkedColumn",-1);
    selectionText_.clear();
    updateFooter();
    update();
    return;
  }
  bool changed = !spatial_ || payload->group != spatial_->group ||
                 payload->frame != spatial_->frame;
  spatial_ = std::move(payload);
  points_.clear();
  pointIds_.clear();
  selected_ = -1;
  setProperty("selectedOrdinal",0);
  setProperty("linkedRow",-1);
  setProperty("linkedColumn",-1);
  selectionText_.clear();
  pointCount_ = spatial_->points.size();
  name_ = spatial_->rasters.contains("reference") ? QStringLiteral("结构光相机 · mm")
      : spatial_->rasters.contains("left") ? QStringLiteral("校正左相机 · mm")
                                          : QStringLiteral("导入 XYZ · mm");
  if (pointCount_) {
    double lo[3] = {INFINITY, INFINITY, INFINITY},
           hi[3] = {-INFINITY, -INFINITY, -INFINITY};
    for (auto p : spatial_->points) {
      double v[] = {p.x, p.y, p.z};
      for (int c = 0; c < 3; ++c) {
        lo[c] = std::min(lo[c], v[c]);
        hi[c] = std::max(hi[c], v[c]);
      }
    }
    center_ = {float((lo[0] + hi[0]) / 2), float((lo[1] + hi[1]) / 2),
               float((lo[2] + hi[2]) / 2)};
    extent_ = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], 1e-9});
    int step = std::max(1, (pointCount_ + 99999) / 100000);
    for (int i = 0; i < pointCount_; i += step) {
      auto p = spatial_->points[i];
      points_.append({float((p.x - center_.x()) / extent_),
                      float((p.y - center_.y()) / extent_),
                      float((p.z - center_.z()) / extent_)});
      pointIds_.append(i);
    }
  }
  if (changed)
    resetCamera();
  else
    update();
  updateFooter();
}
void ModelView::setResidualDisplay(const QImage& colors, double tolerance, bool onlyOutside) {
  projectedIds_.clear();
  residualColors_ = colors;
  residualTolerance_ = tolerance;
  setProperty("residualDisplay", !colors.isNull());
  setProperty("residualToleranceMM", tolerance);
  setProperty("residualOnlyOutside", onlyOutside);
  // Never leave a hidden point selected after changing the display filter.
  if (selected_ >= 0 && spatial_ && !colors.isNull()) {
    const auto& point = spatial_->points[selected_];
    if (colors.pixelColor(point.column, point.row).alpha() == 0) highlightPixel(-1, -1);
  }
  update();
}
void ModelView::highlightPixel(int row, int col) {
  selected_ = -1;
  if (spatial_)
    for (int i = 0; i < spatial_->points.size(); ++i) {
      const auto& p = spatial_->points[i];
      if (p.row == row && p.column == col) {
        if (!residualColors_.isNull() && residualColors_.pixelColor(col, row).alpha() == 0) break;
        selected_ = i;
        selectionText_ =
            QStringLiteral("Row %1 · Col %2 · X %3 · Y %4 · Z %5 mm")
                .arg(row)
                .arg(col)
                .arg(p.x, 0, 'f', 3)
                .arg(p.y, 0, 'f', 3)
                .arg(p.z, 0, 'f', 3);
        break;
      }
    }
  if (selected_ < 0) selectionText_ = QStringLiteral("此像素没有有效三维点");
  setProperty("selectedOrdinal",selected_+1);
  setToolTip(selected_>=0 ? QStringLiteral("点 %1 · ").arg(selected_+1)+selectionText_ : selectionText_);
  setProperty("linkedRow", selected_ >= 0 ? row : -1);
  setProperty("linkedColumn", selected_ >= 0 ? col : -1);
  updateFooter();
  update();
}

QPointF ModelView::pixelPosition(int row, int col) const {
  if (spatial_)
    for (const auto& at : projectedIds_) {
      const auto& p = spatial_->points[at.second];
      if (p.row == row && p.column == col) return at.first;
    }
  return {-1, -1};
}
}  // namespace workbench
