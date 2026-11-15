// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/views/workflow_view.hpp"
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsPathItem>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
namespace workbench {
namespace {
class Overview final : public QWidget {
 public:
  explicit Overview(WorkflowView* view) : QWidget(view->viewport()), view_(view) {
    setObjectName("workflowOverview");
    setToolTip(QStringLiteral("流程总览：点击或拖动定位画布"));
    setAccessibleName(QStringLiteral("流程总览"));
    setCursor(Qt::OpenHandCursor);
  }
 protected:
  QTransform mapping() const {
    const auto bounds = property("worldBounds").toRectF();
    const double scale = std::min((width() - 16.) / std::max(1., bounds.width()),
                                  (height() - 16.) / std::max(1., bounds.height()));
    QTransform map;
    map.translate(width() / 2., height() / 2.);
    map.scale(scale, scale);
    map.translate(-bounds.center().x(), -bounds.center().y());
    return map;
  }
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(dark ? QColor("#222b39") : QColor("#ffffff"));
    p.setPen(dark ? QColor("#47536a") : QColor("#ced7e5"));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);
    p.setClipRect(rect().adjusted(4, 4, -4, -4));
    const auto map = mapping();
    for (auto* item : view_->scene()->items()) {
      if (item->data(1) == "workflow-edge") {
        p.setPen(QPen(QColor("#9aaac5"), 1));
        p.drawPath(map.map(static_cast<QGraphicsPathItem*>(item)->path()));
      }
      if (item->data(1) == "workflow-node") {
        const int state = item->data(5).toInt();
        p.setBrush(state == int(NodeState::Error) ? QColor("#cb6666")
                   : state == int(NodeState::Success) ? QColor("#399780")
                   : state == int(NodeState::Blocked) ? QColor("#b99461") : QColor("#a6b1c8"));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(map.mapRect(QRectF(item->pos(), WorkflowView::nodeSize())), 2, 2);
      }
    }
    p.setBrush(QColor(101, 117, 221, 20));
    p.setPen(QPen(QColor("#6575dd"), 1.5));
    p.drawRect(map.mapRect(view_->mapToScene(view_->viewport()->rect()).boundingRect()));
  }
  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton) return;
    dragging_ = true;
    setCursor(Qt::ClosedHandCursor);
    view_->panTo(mapping().inverted().map(event->localPos()));
    event->accept();
  }
  void mouseMoveEvent(QMouseEvent* event) override {
    if (!event->buttons().testFlag(Qt::LeftButton)) dragging_ = false;
    if (dragging_) view_->panTo(mapping().inverted().map(event->localPos()));
  }
  void mouseReleaseEvent(QMouseEvent*) override { dragging_ = false; setCursor(Qt::OpenHandCursor); }
 private:
  WorkflowView* view_;
  bool dragging_ = false;
};
}  // namespace
void WorkflowView::initializeNavigation() {
  overview_ = new Overview(this);
  overview_->hide();
  setDragMode(RubberBandDrag);
}
void WorkflowView::setOverviewEnabled(bool enabled) { overviewEnabled_ = enabled; updateOverview(); }
void WorkflowView::updateOverview() {
  if (!overview_) return;
  QRectF bounds;
  for (auto* node : nodes_) bounds = bounds.united(QRectF(node->pos(), nodeSize()));
  bounds.adjust(-20, -20, 20, 20);
  overview_->setProperty("worldBounds", bounds);
  overview_->setGeometry(std::max(0, viewport()->width() - 188),
                          std::max(0, viewport()->height() - 120), 176, 108);
  const bool overflow = !mapToScene(viewport()->rect()).boundingRect().contains(bounds);
  overview_->setVisible(overviewEnabled_ && overflow && !nodes_.isEmpty() && viewport()->width() > 220);
  overview_->raise();
  overview_->update();
}
void WorkflowView::panTo(QPointF center) {
  fit_ = false;
  ensureCanvas(center);
  centerOn(center);
  updateOverview();
}
QVector<int> WorkflowView::selectedNodes() const {
  QVector<int> result;
  for (auto* node : nodes_)
    if (node->isSelected()) result.append(node->data(0).toInt());
  return result;
}
bool WorkflowView::focusNode(int index) {
  if (!isEnabled() || index < -1 || index + 1 >= nodes_.size()) return false;
  graph_->clearSelection();
  nodes_[index + 1]->setSelected(true);
  emit stepSelected(index);
  fitSelection();
  return true;
}
void WorkflowView::fitSelection() {
  auto indices = selectedNodes();
  if (indices.isEmpty() && selectedNode_ >= -1 && selectedNode_ + 1 < nodes_.size())
    indices.append(selectedNode_);
  if (indices.isEmpty()) return;
  QRectF bounds;
  for (int index : indices) bounds = bounds.united(QRectF(nodes_[index + 1]->pos(), nodeSize()));
  bounds.adjust(-65, -65, 65, 65);
  const double scale = std::clamp(std::min(viewport()->width() / bounds.width(),
                                          viewport()->height() / bounds.height()), .01, 1.5);
  setTransform(QTransform::fromScale(scale, scale));
  panTo(bounds.center());
}
void WorkflowView::arrangeSelection(Arrangement arrangement) {
  if (!isEnabled()) return;
  auto indices = selectedNodes();
  const bool horizontal = arrangement == Arrangement::Horizontal;
  const bool distribution = horizontal || arrangement == Arrangement::Vertical;
  if (indices.size() < (distribution ? 3 : 2)) return;
  auto coordinate = [&](int index) {
    return horizontal || arrangement == Arrangement::Left ? nodes_[index + 1]->pos().x()
                                                           : nodes_[index + 1]->pos().y();
  };
  std::stable_sort(indices.begin(), indices.end(), [&](int a, int b) { return coordinate(a) < coordinate(b); });
  const double first = coordinate(indices.first());
  const double spacing = distribution
      ? std::max(horizontal ? nodeSize().width() + 40 : nodeSize().height() + 28,
                  (coordinate(indices.last()) - first) / (indices.size() - 1)) : 0;
  emit nodeMoveStarted();
  for (int i = 0; i < indices.size(); ++i) {
    auto* node = nodes_[indices[i] + 1];
    auto position = node->pos();
    if (horizontal || arrangement == Arrangement::Left) position.setX(first + i * spacing);
    else position.setY(first + i * spacing);
    node->setPos(position);
  }
  emit nodeMoveFinished();
  updateOverview();
}
void WorkflowView::scrollContentsBy(int dx, int dy) {
  QGraphicsView::scrollContentsBy(dx, dy);
  updateOverview();
}
}  // namespace workbench
