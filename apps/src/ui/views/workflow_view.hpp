// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QGraphicsView>

#include "runtime/execution.hpp"
#include "operators/operator_registry.hpp"
namespace workbench {
class WorkflowView final : public QGraphicsView {
  Q_OBJECT
 public:
  explicit WorkflowView(QWidget* parent = nullptr);
  static QSizeF nodeSize() { return {144, 52}; }
  void setSteps(const QVector<Parameters>& steps, int selected,
                const QVector<int>& inputs = {},
                const QVector<QPointF>& positions = {},
                const QVector<QVector<int>>& links = {},
                const QVector<int>& regions = {}, int output = -1,
                const QVector<int>& poses = {}, const QStringList& names = {},
                const QVector<int>& dataA = {}, const QVector<int>& dataB = {},
                const QVector<NodeSettings>& bindings = {});
  void setOutputs(const QVector<NodeOutput>& outputs);
  void resetExecution();
  void setRunning(int node);
  void fitGraph();
  bool canConnect(int source, int target) const;
  int nearestSource(QPointF position,
                    Algorithm tool = Algorithm::Regions) const;
  QVector<QPointF> nodePositions() const;
  QPair<int, int> insertionConnection(QPointF position) const;
  enum class Arrangement { Left, Top, Horizontal, Vertical };
  bool focusNode(int index);
  void fitSelection();
  void arrangeSelection(Arrangement arrangement);
  QVector<int> selectedNodes() const;
  void setOverviewEnabled(bool enabled);
  void updateOverview();
  void panTo(QPointF center);
 signals:
  void stepSelected(int index);
  void nodeAdded(int algorithm, QPointF position);
  void nodeInserted(int algorithm, QPointF position, int target, int source);
  void nodeRemoved(int index);
  void nodeRenamed(int index);
  void nodeDebugRequested(int index);
  void inputChanged(int target, int source);
  void inputSelected(int target, int source);
  void typedInputSelected(int target, QString type, int source);
  void namedInputRequested(int target, QString slot, int source);
  void namedInputSelected(int target, QString slot, int source, QString output);
  void dependencyAdded(int target, int source);
  void connectionRemoved(int target, int source);
  void typedConnectionRemoved(int target, QString role, int source);
  void connectionRewired(int oldTarget, int oldSource, int target, int source,
                         QString role);
  void nodeMoved(int index, QPointF position);
  void nodeMoveStarted();
  void nodeMoveFinished();
  void editRejected(QString reason);

 protected:
  void drawBackground(QPainter*, const QRectF&) override;
  void wheelEvent(QWheelEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void leaveEvent(QEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  void keyReleaseEvent(QKeyEvent*) override;
  void focusOutEvent(QFocusEvent*) override;
  void contextMenuEvent(QContextMenuEvent*) override;
  void dragEnterEvent(QDragEnterEvent*) override;
  void dragMoveEvent(QDragMoveEvent*) override;
  void dragLeaveEvent(QDragLeaveEvent*) override;
  void dropEvent(QDropEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void scrollContentsBy(int dx, int dy) override;

 private:
  QGraphicsScene* graph_;
  QVector<QGraphicsItem*> nodes_;
  QVector<QGraphicsPathItem*> edges_;
  QVector<Algorithm> tools_;
  QVector<Parameters> parameters_;
  QVector<NodeSettings> bindings_;
  QVector<QString> outputTypes_;
  QVector<bool> multiViewModes_;
  QVector<int> inputs_;
  QVector<QVector<int>> links_;
  QVector<int> dataA_, dataB_;
  QVector<int> regions_;
  QVector<int> poses_;
  QString cableInputType_;
  QString rewireRole_;
  int rewireTarget_ = -1, rewireSource_ = -3;
  bool validCable(int source, int target, QPoint point, bool reverse) const;
  bool beginRewire(QPoint point);
  bool panning_ = false, spacePan_ = false;
  QPointF panAnchor_;
  void ensureCanvas(QPointF center);
  void stopPan();
  QGraphicsPathItem* cable_ = nullptr;
  int cableSource_ = -3, cableTarget_ = -1;
  bool fit_ = true, rebuilding_ = false;
  bool movingNode_ = false;
  QWidget* overview_ = nullptr;
  bool overviewEnabled_ = true;
  int selectedNode_ = -3;
  void initializeNavigation();
  QGraphicsPathItem* dropPreview_ = nullptr;
  QGraphicsItem* dropGhost_ = nullptr;
  void showDropPreview(Algorithm tool, QPointF position);
  void clearDropPreview();
  void updateEdges();
  int portAt(QPoint point, bool output) const;
  void chooseInput(int target, QPoint screen, const QString& type = "Image");
  QString inputTypeAt(int target, QPoint point) const;
  int nodeAt(QPoint point) const;
  void cancelCable();
  void updateCable(QPoint point);
  QPoint cablePress_;
};
}  // namespace workbench
