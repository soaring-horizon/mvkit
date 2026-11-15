// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/views/workflow_view.hpp"
#include "model/workflow_ports.hpp"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDragLeaveEvent>
#include <QGraphicsSimpleTextItem>
#include <QLineEdit>
#include <QWidgetAction>
#include <QFontMetrics>
#include <QGraphicsObject>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
namespace workbench {
namespace {
constexpr double cardWidth = 144, cardHeight = 52, portY = cardHeight / 2;
const char* mime = "application/x-mvkit-operator";
class Node final : public QGraphicsObject {
 public:
  QString name, output;
  QStringList inputKeys;
  QMap<QString, QString> inputLabels;
  bool geometryNode = false, dual = false, sourceNode = false;
  Algorithm tool = Algorithm::Regions;
  QStringList ports() const {
    if (sourceNode) return {};
    return inputKeys;
  }
  int index = -1;
  bool selected = false, complete = false, connected = true;
  bool finalOutput = false, running = false;
  NodeState state = NodeState::Pending;
  double milliseconds = 0;
  QString inputHint;
  bool outputHint = false;
  std::function<void()> moved;
  Node() {
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
  }
  QPointF inputPort(const QString& type = "Image") const {
    if (geometryNode && inputKeys.size() <= 3) return pos() + QPointF(0, type == "ObjectB" ? 41 : type == "Calibration" ? 11 : portY);
    if (geometryNode && inputKeys.contains(type))
      return pos() + QPointF(0, inputKeys.size() == 1 ? portY : 10. + 32. * inputKeys.indexOf(type) / (inputKeys.size()-1));
    return pos() + QPointF(0, type == "Match"                         ? 11
                              : type == "Region" || type == "ObjectB" ? 41
                                                                      : portY);
  }
  QPointF outputPort() const { return pos() + QPointF(cardWidth, portY); }
  QRectF boundingRect() const override {
    return {-7, -5, cardWidth + 14, cardHeight + 10};
  }
  void paint(QPainter* p, const QStyleOptionGraphicsItem*,
             QWidget*) override {
    const bool dark =
        QApplication::palette().color(QPalette::Window).lightness() < 128;
    const QColor text = dark ? QColor("#e3eaf4") : QColor("#263349"),
                 muted = dark ? QColor("#9eacc1") : QColor("#748197");
    p->setRenderHint(QPainter::Antialiasing);
    const QColor border = running                       ? QColor("#6575dd")
                          : state == NodeState::Success ? QColor("#399780")
                          : state == NodeState::Error   ? QColor("#cb6666")
                          : state == NodeState::Blocked ? QColor("#b99461")
                          : dark                        ? QColor("#465166")
                                                        : QColor("#d5dce8");
    if (selected || isSelected()) {
      p->setBrush(Qt::NoBrush);
      p->setPen(QPen(dark ? QColor("#a1abef") : QColor("#8994e0"), 1));
      p->drawRoundedRect(QRectF(-3, -3, cardWidth + 6, cardHeight + 6), 8, 8);
    }
    p->setBrush(dark ? QColor("#202632") : Qt::white);
    p->setPen(
        QPen(border, state == NodeState::Pending && !running ? 1.2 : 1.6));
    p->drawRoundedRect(QRectF(0, 0, cardWidth, cardHeight), 7, 7);
    QFont font = QApplication::font();
    p->setFont(font);
    font.setBold(true);
    p->setFont(font);
    p->setPen(selected ? QColor("#6575dd") : text);
    p->drawText(QRectF(11, index < 0 ? 17 : 6, cardWidth - 22, 19),
                Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(font).elidedText(name, Qt::ElideRight,
                                              int(cardWidth - 22)));
    font.setBold(false);
    font.setPointSizeF(font.pointSizeF() - .5);
    p->setFont(font);
    p->setPen(muted);
    if (index >= 0)
      p->drawText(QRectF(11, 29, cardWidth - 22, 16),
                  (state == NodeState::Success ||
                   (state == NodeState::Error && milliseconds > 0)) &&
                          !running
                      ? QStringLiteral("%1 ms").arg(milliseconds, 0, 'f', 2)
                      : QStringLiteral("— ms"));
    if (finalOutput) {
      p->setPen(QPen(muted, 1.1));
      p->drawLine(QPointF(cardWidth - 15, 37), QPointF(cardWidth - 8, 37));
      p->drawLine(QPointF(cardWidth - 11, 34), QPointF(cardWidth - 8, 37));
      p->drawLine(QPointF(cardWidth - 11, 40), QPointF(cardWidth - 8, 37));
    }
    auto port = [&](double x, QColor color) {
      p->setBrush(dark ? QColor("#202632") : Qt::white);
      p->setPen(QPen(color, 1.7));
      p->drawEllipse(QPointF(x, portY), 4.5, 4.5);
    };
    if (index >= 0 && !sourceNode && geometryNode) {
      for (const auto& key : inputKeys) {
        p->setBrush(dark ? QColor("#202632") : Qt::white);
        p->setPen(QPen(QColor("#8c9bd8"), 1.5));
        p->drawEllipse(inputPort(key)-pos(), 4, 4);
      }
    } else if (index >= 0 && !sourceNode) {
      port(0, connected ? QColor("#8c9bd8") : QColor("#c09355"));
      if (!geometryNode || dual) {
        p->setBrush(dark ? QColor("#202632") : Qt::white);
        p->setPen(QPen(QColor("#41a893"), 1.3));
        p->drawRect(QRectF(-3, 38, 6, 6));
      }
      if (!geometryNode) {
        p->setPen(QPen(QColor("#927bd7"), 1.3));
        p->drawPolygon(
            QPolygonF(QVector<QPointF>{{0, 7}, {4, 11}, {0, 15}, {-4, 11}}));
      }
    }
    port(cardWidth,
         output == "Image" ? QColor("#8c9bd8") : QColor("#41a893"));
    if (!inputHint.isEmpty() || outputHint) {
      const auto point = outputHint ? QPointF(cardWidth, portY)
                                   : inputPort(inputHint) - pos();
      p->setBrush(QColor(54, 164, 139, 35));
      QPen hint(QColor("#36a48b"), 2);
      hint.setCosmetic(true);
      p->setPen(hint);
      p->drawEllipse(point, 8, 8);
    }
  }
  QVariant itemChange(GraphicsItemChange c, const QVariant& v) override {
    if (c == ItemPositionHasChanged && moved) moved();
    return QGraphicsObject::itemChange(c, v);
  }
  void hoverMoveEvent(QGraphicsSceneHoverEvent* e) override {
    if (index >= 0 && std::abs(e->pos().x()) < 8) {
      QString nearest; double distance = 1e9;
      for (const auto& key : inputKeys) {
        const double d = std::abs((inputPort(key)-pos()).y() - e->pos().y());
        if (d < distance) { distance = d; nearest = key; }
      }
      setToolTip(inputLabels.value(nearest));
    }
    else if (e->pos().x() > cardWidth - 8)
      setToolTip(QStringLiteral("输出 · ") + data(10).toString());
    else
      setToolTip(data(8).toString());
    QGraphicsObject::hoverMoveEvent(e);
  }
};
class Cable final : public QGraphicsPathItem {
 public:
  bool dependencyOnly = false;
  QPainterPath shape() const override {
    QPainterPathStroker stroke;
    stroke.setWidth(12);
    return stroke.createStroke(path());
  }
  QRectF boundingRect() const override {
    return path().boundingRect().adjusted(-7, -7, 7, 7);
  }
  void paint(QPainter* p, const QStyleOptionGraphicsItem*,
             QWidget*) override {
    const QColor color(isSelected() ? "#6979e6" : "#9aaac5");
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(QPen(color, isSelected() ? 2.6 : 1.6,
                   dependencyOnly ? Qt::DashLine : Qt::SolidLine));
    p->drawPath(path());
    if (!path().isEmpty()) {
      const auto b = path().pointAtPercent(1);
      p->setPen(Qt::NoPen);
      p->setBrush(color);
      p->drawPolygon(QPolygonF(
          QVector<QPointF>{b, b + QPointF(-8, -4), b + QPointF(-8, 4)}));
      if (isSelected()) {
        p->setBrush(QApplication::palette().color(QPalette::Base));
        p->setPen(QPen(QColor("#6979e6"), 1.7));
        p->drawEllipse(path().pointAtPercent(0), 6, 6);
        p->drawEllipse(b, 6, 6);
      }
    }
  }
  QVariant itemChange(GraphicsItemChange change,
                      const QVariant& value) override {
    if (change == ItemSelectedHasChanged) setZValue(value.toBool() ? 2 : -1);
    return QGraphicsPathItem::itemChange(change, value);
  }
};
QPainterPath wire(QPointF a, QPointF b) {
  const double d = std::max(45., std::abs(b.x() - a.x()) * .45);
  QPainterPath p(a);
  p.cubicTo(a + QPointF(d, 0), b - QPointF(d, 0), b);
  return p;
}
}  // namespace
WorkflowView::WorkflowView(QWidget* parent)
    : QGraphicsView(parent), graph_(new QGraphicsScene(this)) {
  setObjectName("workflowView");
  setScene(graph_);
  setFrameShape(QFrame::NoFrame);
  setRenderHint(QPainter::Antialiasing);
  setDragMode(NoDrag);
  setTransformationAnchor(NoAnchor);
  setResizeAnchor(NoAnchor);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  viewport()->setCursor(Qt::ArrowCursor);
  setAcceptDrops(true);
  setMouseTracking(true);
  setToolTip(QStringLiteral(
      "中键或空格平移，滚轮以鼠标为中心缩放；拖入算子，拖动端口连线；"
      "选中节点或连线按 Delete 删除"));
  initializeNavigation();
}
void WorkflowView::setSteps(
    const QVector<Parameters>& steps, int selected, const QVector<int>& inputs,
    const QVector<QPointF>& positions, const QVector<QVector<int>>& links,
    const QVector<int>& regions, int output, const QVector<int>& poses,
    const QStringList& labels, const QVector<int>& dataA,
    const QVector<int>& dataB, const QVector<NodeSettings>& bindings) {
  selectedNode_ = selected;
  parameters_ = steps;
  bindings_ = bindings;
  regions_ = regions;
  poses_ = poses;
  QVector<Algorithm> keys;
  for (const auto& p : steps) keys.append(p.algorithm);
  dataA_ = dataA;
  dataB_ = dataB;
  QVector<QString> newTypes;QVector<bool> newModes;
  for(const auto& p:steps) {newTypes.append(outputType(p));newModes.append(multiViewInput(p)||p.algorithm==Algorithm::StereoRectify && p.options.value("multiView").toInt());}
  const bool portLayoutChanged=newTypes!=outputTypes_ || newModes!=multiViewModes_;
  outputTypes_=newTypes;multiViewModes_=newModes;
  inputs_ = inputs;
  if (inputs_.isEmpty())
    for (int i = 0; i < steps.size(); ++i) inputs_.append(i - 1);
  links_ = links;
  if (links_.isEmpty())
    for (int source : inputs_)
      links_.append(source >= -1 ? QVector<int>{source} : QVector<int>{});
  const auto names =
      labels.size() == steps.size() ? labels : workflowNodeNames(steps);
  if (nodes_.isEmpty() || keys != tools_ || portLayoutChanged) {
    clearDropPreview();
    cancelCable();
    rebuilding_ = true;
    nodes_.clear();
    edges_.clear();
    graph_->clear();
    cable_ = nullptr;
    cableSource_ = -3;
    cableTarget_ = -1;
    tools_ = keys;
    for (int i = -1; i < steps.size(); ++i) {
      auto* n = new Node;
      n->index = i;
      n->name = i < 0 ? QStringLiteral("图像输入") : names[i];
      n->setData(3, n->name);
      n->sourceNode = i >= 0 && sourceTool(steps[i].algorithm);
      if (i >= 0) n->tool = steps[i].algorithm;
      n->geometryNode = i >= 0 && typedInputTool(steps[i].algorithm);
      if (i >= 0) for (const auto& port : inputPorts(steps[i]))
        if (port.required || port.key == "Calibration" || port.key == "ObjectB" || !n->geometryNode) n->inputKeys << port.key;
      n->dual = i >= 0 && (needsSecondInput(steps[i]) || steps[i].algorithm == Algorithm::Measure ||
                           steps[i].algorithm == Algorithm::RegionBoolean ||
                           steps[i].algorithm == Algorithm::PlaneMeasure ||
                           steps[i].algorithm == Algorithm::CloudSelect);
      n->output =
          i < 0 ? "Image" : outputType(steps[i]);
      QStringList descriptions;
      if (i >= 0) for (const auto& port : outputPorts(steps[i])) descriptions << port.label + " (" + port.type + ")";
      n->setData(10, descriptions.join('\n'));
      n->setData(0, i);
      n->setData(1, "workflow-node");
      graph_->addItem(n);
      const int ordinal = i + 1;
      n->setPos(
          positions.size() == steps.size() + 1
              ? positions[ordinal]
              : QPointF(45 + ordinal % 3 * 240, 65 + ordinal / 3 * 140));
      n->moved = [this, n] {
        if (!rebuilding_) {
          fit_ = false;
          updateEdges();
          updateOverview();
          emit nodeMoved(n->index, n->pos());
        }
      };
      nodes_.append(n);
    }
    rebuilding_ = false;
    graph_->setSceneRect(
        graph_->itemsBoundingRect().adjusted(-65, -65, 85, 85));
  }
  for (auto* item : nodes_) {
    auto* n = static_cast<Node*>(item);
    if (positions.size() == nodes_.size() && !movingNode_) {
      rebuilding_ = true;
      n->setPos(positions[n->index + 1]);
      rebuilding_ = false;
    }
    if (n->index >= 0) {
      n->name = names[n->index];
      n->setData(3, n->name);
      n->inputKeys.clear();
      n->inputLabels.clear();
      for (const auto& port : inputPorts(steps[n->index])) {
        if (port.required || port.key == "Calibration" || port.key == "ObjectB" || !n->geometryNode) n->inputKeys << port.key;
        n->inputLabels[port.key] = port.label + " · " + port.types.join(" / ");
      }
      QStringList descriptions;
      for (const auto& port : outputPorts(steps[n->index])) descriptions << port.label + " (" + port.type + ")";
      n->setData(10, descriptions.join('\n'));
    }
    if (n->index < 0) {
      bool explicitInputs = false;
      for (const auto& p : steps) explicitInputs |= sourceTool(p.algorithm);
      bool legacyUsed = false;
      for (const auto& row : links_) legacyUsed |= row.contains(-1);
      n->setVisible(!steps.isEmpty() && (!explicitInputs || legacyUsed));
      n->name = explicitInputs ? QStringLiteral("数据参考图 · 兼容") : QStringLiteral("图像输入");
      n->setData(3,n->name);
    }
    n->selected = n->index == selected;
    n->connected = n->index < 0 || n->sourceNode ||
        (n->geometryNode ? dataA_.value(n->index,-2)>=0 && (!needsSecondInput(steps[n->index]) || dataB_.value(n->index,-2)>=0) ||
                           captureTool(steps[n->index].algorithm) && inputs_.value(n->index,-2)==-1
                         : inputs_.value(n->index,-2)>=-1);
    n->finalOutput = n->index >= 0 && n->index == output;
    n->setData(6, n->finalOutput);
    n->update();
  }
  updateEdges();
  if (fit_)
    fitGraph();
  else
    ensureCanvas(mapToScene(viewport()->rect().center()));
  updateOverview();
}
void WorkflowView::setOutputs(const QVector<NodeOutput>& outputs) {
  for (auto* item : nodes_) {
    auto* n = static_cast<Node*>(item);
    n->complete = n->index < 0 ? !outputs.isEmpty()
                               : n->index < outputs.size() &&
                                     outputs[n->index].parameters.algorithm ==
                                         tools_[n->index];
    n->running = false;
    n->state = n->complete && n->index >= 0 ? outputs[n->index].state
                                            : NodeState::Pending;
    n->complete =
        n->complete && (n->index < 0 || n->state == NodeState::Success);
    n->setData(5, int(n->state));
    n->setToolTip(n->index >= 0 && n->index < outputs.size()
                      ? n->name + "\n" + outputs[n->index].error
                      : n->name);
    n->milliseconds = n->index >= 0 && n->index < outputs.size()
                          ? outputs[n->index].algorithmMs
                          : 0;
    if (n->index >= 0) {
      const QString state =
          n->state == NodeState::Success ? QStringLiteral("执行成功")
          : n->state == NodeState::Error ? QStringLiteral("执行失败")
          : n->state == NodeState::Blocked
              ? QStringLiteral("上游失败，未执行")
          : n->connected ? QStringLiteral("等待运行")
                         : QStringLiteral("未连接输入");
      n->setToolTip(
          n->name +
          (n->finalOutput ? QStringLiteral(" · 流程输出") : QString()) +
          "\n" + state +
          (n->index < outputs.size() && !outputs[n->index].error.isEmpty()
               ? "\n" + outputs[n->index].error
               : QString()));
    }
    n->setData(8, n->toolTip());
    n->update();
  }
  updateOverview();
}
void WorkflowView::resetExecution() { setOutputs({}); }
void WorkflowView::setRunning(int node) {
  for (auto* item : nodes_) {
    auto* n = static_cast<Node*>(item);
    n->running = n->index == node;
    if (n->running) n->setToolTip(n->name + QStringLiteral("\n正在运行"));
    n->update();
  }
}
void WorkflowView::updateEdges() {
  auto rolesFor = [&](int target, int source) {
    QStringList roles;
    if (target < bindings_.size())
      for (auto it = bindings_[target].inputs.cbegin(); it != bindings_[target].inputs.cend(); ++it)
        if (it->node == source) roles << it.key();
    if (roles.isEmpty()) {
      if (dataA_.value(target,-2) == source) roles << "ObjectA";
      if (dataB_.value(target,-2) == source) roles << "ObjectB";
    }
    if (roles.isEmpty()) roles << "";
    return roles;
  };
  int count = 0;
  for (int i = 0; i < links_.size(); ++i)
    for (int source : links_[i])
      count += rolesFor(i, source).size();
  while (edges_.size() > count) delete edges_.takeLast();
  while (edges_.size() < count) {
    auto* e = new Cable;
    graph_->addItem(e);
    e->setZValue(-1);
    e->setFlag(QGraphicsItem::ItemIsSelectable);
    e->setToolTip(QStringLiteral(
        "单击选中；拖动两端手柄改接；Delete 断开，Esc 取消改接"));
    edges_.append(e);
  }
  int ordinal = 0;
  for (int i = 0; i < links_.size(); ++i)
    for (int source : links_[i]) {
      const QStringList roles = rolesFor(i, source);
      for (const auto& role : roles) {
        auto* e = edges_[ordinal++];
        auto* cable = static_cast<Cable*>(e);
        const bool region = source >= 0 && regions_.value(i, -1) == source;
        const bool pose = source >= 0 && poses_.value(i, -1) == source;
        const bool calibration=source>=0 && calibrationTool(tools_[source]);
        cable->dependencyOnly =
            !calibration && !region && !pose && dataA_.value(i, -2) != source &&
            dataB_.value(i, -2) != source && inputs_.value(i, -2) != source;
        e->setToolTip(
            calibration ? QStringLiteral("标定模型：使用本次成功求解的内参和位姿；上游失败时停止本分支")
            : dataA_.value(i, -2) == source
                ? QStringLiteral("类型化输入 A：消费真实轮廓、几何或命名数值")
            : dataB_.value(i, -2) == source
                ? (tools_[i] == Algorithm::CloudSelect
                       ? QStringLiteral("区域 B：校正左图分割区域，提取对应三维点")
                       : QStringLiteral("类型化输入 B：第二个测量对象"))
            : pose
                ? QStringLiteral(
                      "定位数据 · Match：变换下游作用范围。选中按 Delete 断开")
            : region
                ? QStringLiteral(
                      "Region 区域数据：限定下游处理范围。选中按 Delete 断开")
            : cable->dependencyOnly
                ? QStringLiteral(
                      "处理依赖（虚线）：先执行上游对象算子；图像从上"
                      "游图像候选中选择。选中按 Delete 断开")
                : QStringLiteral(
                      "处理依赖：图像可在已连通的上游候选中选择。选中"
                      "按 Delete 断开"));
        if (!role.isEmpty() && source >= 0 && i < bindings_.size()) {
          QString from = bindings_[i].inputs.value(role).output, to = role;
          for (const auto& port : outputPorts(parameters_[source])) if (port.key == from) from = port.label;
          for (const auto& port : inputPorts(parameters_[i])) if (port.key == role) to = port.label;
          e->setToolTip(nodes_[source+1]->data(3).toString() + " → " + from + "\n" + nodes_[i+1]->data(3).toString() + " → " + to);
        }
        e->setToolTip(e->toolTip() +
            QStringLiteral(
                "\n单击选中后，拖动两端圆形手柄改接；Esc 或空白释放保留原线"));
        e->setData(1, "workflow-edge");
        e->setData(0, i);
        e->setData(2, source);
        e->setData(4, calibration?QString("Calibration"):role);
        e->setVisible(source >= -1 && source + 1 < nodes_.size());
        if (e->isVisible())
          e->setPath(wire(static_cast<Node*>(nodes_[source + 1])->outputPort(),
                          static_cast<Node*>(nodes_[i + 1])
                              ->inputPort(!role.isEmpty() ? role
                                          : region        ? "Region"
                                          : pose          ? "Match"
                                                          : "Image")));
      }
    }
}
int WorkflowView::nearestSource(QPointF position, Algorithm tool) const {
  if (sourceTool(tool)) return -2;
  int source = -2;
  double distance = std::numeric_limits<double>::max();
  for (auto* item : nodes_) {
    const auto* node = static_cast<Node*>(item);
    if (!node->isVisible()) continue;
    if (captureTool(tool) && node->index>=0 && !sourceTool(tools_[node->index])) continue;
    if (captureTool(tool) && node->index < 0) { source = -1; continue; }
    if (typedInputTool(tool)) {
      if (!acceptsData(tool, node->output)) continue;
      if(calibrationTool(tool) && (node->index<0 || !sourceTool(tools_[node->index])))continue;
    } else if (node->output != "Image" && node->output != "Region")
      continue;
    if (node->index >= 0 && !sourceTool(tools_[node->index]) && !typedInputTool(tools_[node->index]) && inputs_.value(node->index, -2) < -1) continue;
    const auto input =
        position + QPointF(0, node->output == "Region" ? 41 : portY);
    const auto d = QLineF(input, node->outputPort()).length();
    if (d < distance) {
      distance = d;
      source = node->index;
    }
  }
  return source;
}
QVector<QPointF> WorkflowView::nodePositions() const {
  QVector<QPointF> positions;
  for (auto* item : nodes_) positions.append(item->pos());
  return positions;
}
QPair<int, int> WorkflowView::insertionConnection(QPointF position) const {
  const double margin = 20 / transform().m11();
  for (auto* edge : edges_) {
    const int target = edge->data(0).toInt(), source = edge->data(2).toInt();
    if (!edge->isVisible() || target < 0 || target >= inputs_.size() ||
        inputs_[target] != source || source < -1 ||
        source >= 0 && outputTypes_.value(source) != "Image")
      continue;
    const auto path = edge->path();
    if (path.isEmpty() || QLineF(position, path.pointAtPercent(0)).length() < margin ||
        QLineF(position, path.pointAtPercent(1)).length() < margin)
      continue;
    QPainterPathStroker hit;
    hit.setWidth(margin);
    if (hit.createStroke(path).contains(position)) return {target, source};
  }
  return {-1, -3};
}
void WorkflowView::clearDropPreview() {
  delete dropPreview_;
  delete dropGhost_;
  dropPreview_ = nullptr;
  dropGhost_ = nullptr;
}
void WorkflowView::showDropPreview(Algorithm tool, QPointF position) {
  clearDropPreview();
  const auto connection = operatorDefinition(tool).output == "Image" && !dataTool(tool)
                              ? insertionConnection(position) : QPair<int, int>{-1, -3};
  const int source =
      connection.first >= 0 ? connection.second : nearestSource(position, tool);
  if (source < -1) {
    auto* ghost = graph_->addRect(QRectF(position, nodeSize()), QPen(QColor("#6575dd"), 1.5), QColor(105,121,230,28));
    dropGhost_ = ghost; ghost->setAcceptedMouseButtons(Qt::NoButton); return;
  }
  const auto* upstream = static_cast<Node*>(nodes_[source + 1]);
  if (connection.first >= 0)
    position = QPointF(std::max(position.x() - cardWidth / 2,
                                upstream->outputPort().x() + 40),
                        position.y() - cardHeight / 2);
  const auto input = position + QPointF(0, upstream->output == "Region" ? 41 : portY);
  auto path = wire(upstream->outputPort(), input);
  if (connection.first >= 0) {
    auto target = static_cast<Node*>(nodes_[connection.first + 1])->inputPort();
    target.setX(std::max(target.x(), position.x() + cardWidth + 40));
    path.addPath(wire(position + QPointF(cardWidth, portY),
                      target));
  }
  QPen pen(QColor("#36a48b"), 2, Qt::DashLine);
  pen.setCosmetic(true);
  dropPreview_ = graph_->addPath(path, pen);
  dropPreview_->setZValue(4);
  auto* ghost = graph_->addRect(QRectF(position, nodeSize()), pen,
                                QColor(105, 121, 230, 28));
  dropGhost_ = ghost;
  ghost->setZValue(5);
  ghost->setData(1, "workflow-drop-preview");
  ghost->setData(0, connection.first);
  ghost->setAcceptedMouseButtons(Qt::NoButton);
  auto* label = new QGraphicsSimpleTextItem(operatorDefinition(tool).name +
      (connection.first >= 0 ? QStringLiteral(" · 插入") : QStringLiteral(" · 添加")), ghost);
  label->setPos(position + QPointF(10, 14));
  label->setBrush(QColor("#6575dd"));
}
bool WorkflowView::validCable(int source, int target, QPoint point,
                              bool reverse) const {
  if (source < -1 || source >= tools_.size() || target < 0 ||
      target >= tools_.size() || source == target)
    return false;
  auto links = links_;
  if (rewireTarget_ >= 0) links[rewireTarget_].removeAll(rewireSource_);
  if (source >= 0 && workflowAncestors(links, source).contains(target))
    return false;
  if (source >= 0) {
    const auto slot = rewireTarget_ >= 0 ? rewireRole_ : reverse ? cableInputType_ : inputTypeAt(target, point);
    if (slot == "Dependency") return true;
    if (!compatibleOutputs(parameters_[source], parameters_[target], slot).isEmpty()) return true;
    // Dropping on the body uses the primary input, rather than the nearest
    // optional region/pose pin. Exact pin drops remain type constrained.
    if (!reverse && portAt(point, false) != target) {
      for (const auto& port : inputPorts(parameters_[target]))
        if (!compatibleOutputs(parameters_[source], parameters_[target], port.key).isEmpty()) return true;
    }
    return false;
  }
  const auto type = source < 0 ? QString("Image")
                               : outputTypes_.value(source);
  if(type=="Calibration")return calibrationLink(tools_[source],tools_[target]);
  if (rewireTarget_ >= 0 && rewireRole_ == "Dependency") return true;
  const auto required = rewireTarget_ >= 0 ? rewireRole_
                        : reverse          ? cableInputType_
                                           : type;
  if (sourceTool(tools_[target])) return false;
  if ((captureTool(tools_[target])||calibrationTool(tools_[target])) && source>=0 && !sourceTool(tools_[source])) return false;
  if (captureTool(tools_[target]) && source == -1) return true; // Legacy root.
  if (typedInputTool(tools_[target])) {
    const auto role = rewireTarget_ >= 0 ? rewireRole_
                      : reverse          ? cableInputType_
                                         : inputTypeAt(target, point);
    return acceptsData(tools_[target], type, role == "ObjectB") &&
           (role == "ObjectA" || role == "ObjectB");
  }
  if (type == "XLD" ||
      (type != "Image" && type != "Region" && type != "Match") ||
      type != required)
    return false;
  if (!reverse && portAt(point, false) == target &&
      inputTypeAt(target, point) != required)
    return false;
  return true;
}
bool WorkflowView::beginRewire(QPoint point) {
  for (auto* edge : edges_) {
    if (!edge->isSelected() || edge->path().isEmpty()) continue;
    const auto from = viewportTransform().map(edge->path().pointAtPercent(0));
    const auto to = viewportTransform().map(edge->path().pointAtPercent(1));
    const bool moveSource = QLineF(QPointF(point), from).length() <= 11;
    if (!moveSource && QLineF(QPointF(point), to).length() > 11) continue;
    cancelCable();
    fit_ = false;
    setFocus(Qt::MouseFocusReason);
    rewireTarget_ = edge->data(0).toInt();
    rewireSource_ = edge->data(2).toInt();
    rewireRole_ =
        !edge->data(4).toString().isEmpty() ? edge->data(4).toString()
        : rewireSource_ >= 0 &&
                regions_.value(rewireTarget_, -1) == rewireSource_
            ? "Region"
        : rewireSource_ >= 0 && poses_.value(rewireTarget_, -1) == rewireSource_
            ? "Match"
        : inputs_.value(rewireTarget_, -2) == rewireSource_ ? "Image"
                                                            : "Dependency";
    cablePress_ = point;
    cableTarget_ = moveSource ? rewireTarget_ : -1;
    cableSource_ = moveSource ? -3 : rewireSource_;
    cableInputType_ =
        rewireRole_ == "Dependency" ? QString("Image") : rewireRole_;
    edge->setOpacity(.25);
    cable_ = graph_->addPath(edge->path(),
                             QPen(QColor("#7685e6"), 2, Qt::DashLine));
    cable_->setZValue(3);
    return true;
  }
  return false;
}
bool WorkflowView::canConnect(int source, int target) const {
  if (target < 0 || target >= tools_.size() || source < -1 ||
      source >= tools_.size() || source == target)
    return false;
  if (source >= 0) {
    if (workflowAncestors(links_, source).contains(target)) return false;
    for (const auto& port : inputPorts(parameters_[target]))
      if (!compatibleOutputs(parameters_[source], parameters_[target], port.key).isEmpty()) return true;
    return false;
  }
  if(source>=0 && calibrationTool(tools_[source]))
    return calibrationLink(tools_[source],tools_[target]) && !workflowAncestors(links_,source).contains(target);
  if (sourceTool(tools_[target])) return false;
  if ((captureTool(tools_[target])||calibrationTool(tools_[target])) && source>=0 && !sourceTool(tools_[source])) return false;
  if (captureTool(tools_[target]) && source == -1) return true;
  const auto type =
      source < 0 ? QString("Image") : outputTypes_.value(source);
  if (typedInputTool(tools_[target])) {
    if (!acceptsData(tools_[target], type)) return false;
  } else if (type != "Image" && type != "Region" && type != "Match")
    return false;
  return source < 0 || !workflowAncestors(links_, source).contains(target);
}
int WorkflowView::portAt(QPoint point, bool output) const {
  int hit = -3;
  double nearest = 22.;
  for (auto* item : nodes_) {
    auto* node = static_cast<Node*>(item);
    if (!node->isVisible() || (!output && (node->index < 0 || node->sourceNode))) continue;
    if (output) {
      const auto port = viewportTransform().map(node->outputPort());
      const auto d = QLineF(QPointF(point), port).length();
      if (d < nearest) { nearest = d; hit = node->index; }
      continue;
    }
    double distance = 1e9;
    for (const QString& type : node->ports()) {
      const auto port = viewportTransform().map(
          output ? node->outputPort() : node->inputPort(type));
      distance = std::min(distance, QLineF(QPointF(point), port).length());
    }
    if (distance < nearest) {
      nearest = distance;
      hit = node->index;
    }
  }
  return hit;
}
QString WorkflowView::inputTypeAt(int target, QPoint point) const {
  if (target < 0 || target + 1 >= nodes_.size()) return "Image";
  const auto* n = static_cast<Node*>(nodes_[target + 1]);
  QString found = "Image";
  double distance = 1e9;
  for (const QString& type : n->ports()) {
    const auto d =
        QLineF(QPointF(point), viewportTransform().map(n->inputPort(type)))
            .length();
    if (d < distance) {
      distance = d;
      found = type;
    }
  }
  return found;
}
int WorkflowView::nodeAt(QPoint point) const {
  const auto scenePoint = mapToScene(point);
  for (auto* item : nodes_)
    if (item->isVisible() && item->contains(item->mapFromScene(scenePoint)))
      return static_cast<Node*>(item)->index;
  return -3;
}
void WorkflowView::cancelCable() {
  for (auto* item : nodes_) {
    auto* node = static_cast<Node*>(item);
    node->inputHint.clear();
    node->outputHint = false;
    node->setData(9, false);
    node->update();
  }
  for (auto* edge : edges_) edge->setOpacity(1);
  delete cable_;
  cable_ = nullptr;
  cableSource_ = -3;
  cableTarget_ = -1;
  cableInputType_.clear();
  rewireTarget_ = -1;
  rewireSource_ = -3;
  rewireRole_.clear();
  viewport()->setCursor(Qt::ArrowCursor);
}
void WorkflowView::updateCable(QPoint point) {
  const bool reverse = cableTarget_ >= 0;
  int source = reverse ? portAt(point, true) : cableSource_;
  int target = reverse ? cableTarget_ : portAt(point, false);
  if (reverse && source == -3) source = nodeAt(point);
  if (!reverse && target < 0) target = nodeAt(point);
  const QString type = source >= 0 && source < tools_.size()
                           ? outputTypes_.value(source)
                           : QString("Image");
  const bool valid = validCable(source, target, point, reverse);
  for (auto* item : nodes_) {
    auto* node = static_cast<Node*>(item);
    const auto required = rewireTarget_ >= 0   ? rewireRole_
                          : reverse            ? cableInputType_
                          : node->geometryNode ? inputTypeAt(node->index, point)
                                               : type;
    const auto inputPoint = mapFromScene(node->inputPort(
        node->geometryNode ? (rewireTarget_ >= 0 ? rewireRole_
                              : reverse          ? cableInputType_
                                        : inputTypeAt(node->index, point))
                           : required));
    const bool candidate = reverse
        ? validCable(node->index, cableTarget_, point, true)
        : validCable(cableSource_, node->index, inputPoint, false);
    node->inputHint = candidate && !reverse ? required : QString();
    node->outputHint = candidate && reverse;
    node->setData(9, candidate);
    node->update();
  }
  const auto free = mapToScene(point);
  const auto from =
      reverse && !valid
          ? free
          : static_cast<Node*>(nodes_[source + 1])->outputPort();
  const auto to =
      !reverse && !valid
          ? free
          : static_cast<Node*>(nodes_[target + 1])
                ->inputPort(rewireTarget_ >= 0 ? (rewireRole_ == "Dependency"
                                                      ? QString("Image")
                                                      : rewireRole_)
                            : reverse          ? cableInputType_
                            : target >= 0 && typedInputTool(tools_[target])
                                ? inputTypeAt(target, point)
                                : type);
  cable_->setPath(wire(from, to));
  cable_->setPen(
      QPen(QColor(valid ? "#36a48b" : "#7685e6"), 2, Qt::DashLine));
  viewport()->setCursor(valid ? Qt::CrossCursor : Qt::PointingHandCursor);
}
void WorkflowView::chooseInput(int target, QPoint screen,
                               const QString& type) {
  if (target < 0 || target >= tools_.size()) return;
  QMenu menu;
  auto* search = new QLineEdit(&menu);
  search->setPlaceholderText(QStringLiteral("搜索已连通的上游"));
  auto* searchAction = new QWidgetAction(&menu);
  searchAction->setDefaultWidget(search);
  menu.addAction(searchAction);
  int choices = 0;
  const auto upstream = workflowInputSources(links_, target);
  for (int source : upstream) {
    if (source >= 0) {
      for (const auto& port : compatibleOutputs(parameters_[source], parameters_[target], type)) {
        auto* action = menu.addAction(nodes_[source+1]->data(3).toString() + " → " + port.label);
        ++choices;
        connect(action, &QAction::triggered, this, [=] { emit namedInputSelected(target, type, source, port.key); });
      }
      continue;
    }
    if ((source < 0 && type != "Image") ||
        (source >= 0 &&
         (type == "ObjectA" || type == "ObjectB"
              ? !acceptsData(tools_[target],
                             outputTypes_.value(source),
                             type == "ObjectB")
              : outputTypes_.value(source) != type)))
      continue;
    auto* action =
        menu.addAction(source < 0 ? QStringLiteral("原始图像")
                                  : nodes_[source + 1]->data(3).toString() +
                                      QStringLiteral(" → ") + type);
    ++choices;
    action->setCheckable(true);
    action->setChecked((type == "Image"     ? inputs_.value(target)
                        : type == "Region"  ? regions_.value(target)
                        : type == "ObjectA" ? dataA_.value(target)
                        : type == "ObjectB" ? dataB_.value(target)
                                            : poses_.value(target)) == source);
    connect(action, &QAction::triggered, this, [this, target, source, type] {
      if (type == "Image")
        emit inputSelected(target, source);
      else
        emit typedInputSelected(target, type, source);
    });
  }
  if (!choices)
    menu.addAction(QStringLiteral("请先连接上游节点"))->setEnabled(false);
  connect(search, &QLineEdit::textChanged, &menu, [&menu, searchAction](const QString& text) {
    for (auto* action : menu.actions())
      if (action != searchAction) action->setVisible(action->text().contains(text, Qt::CaseInsensitive));
  });
  search->setFocus();
  menu.exec(screen);
}
void WorkflowView::mousePressEvent(QMouseEvent* e) {
  if (e->button() == Qt::MiddleButton ||
      (spacePan_ && e->button() == Qt::LeftButton)) {
    panning_ = true;
    fit_ = false;
    panAnchor_ = viewportTransform().inverted().map(e->localPos());
    viewport()->setCursor(Qt::ClosedHandCursor);
    e->accept();
    return;
  }
  if (e->button() == Qt::LeftButton && isEnabled()) {
    if (beginRewire(e->pos())) {
      e->accept();
      return;
    }
    const int out = portAt(e->pos(), true), in = portAt(e->pos(), false);
    if (out != -3 || in >= 0) {
      fit_ = false;
      setFocus(Qt::MouseFocusReason);
      cancelCable();
      cablePress_ = e->pos();
      cableSource_ = out;
      cableTarget_ = out == -3 ? in : -1;
      if (cableTarget_ >= 0)
        cableInputType_ = inputTypeAt(cableTarget_, e->pos());
      cable_ = graph_->addPath({}, QPen(QColor("#7685e6"), 2, Qt::DashLine));
      cable_->setZValue(3);
      e->accept();
      return;
    }
    if (auto* n = dynamic_cast<Node*>(itemAt(e->pos()))) {
      for (auto* item : nodes_) {
        auto* other = static_cast<Node*>(item);
        other->selected = other == n;
        other->update();
      }
      emit stepSelected(n->index);
      movingNode_ = true;
      emit nodeMoveStarted();
    }
  }
  QGraphicsView::mousePressEvent(e);
  updateEdges();
}
void WorkflowView::mouseMoveEvent(QMouseEvent* e) {
  if (panning_) {
    const double zoom = transform().m11();
    const auto center = panAnchor_ + (QPointF(viewport()->width() * .5,
                                              viewport()->height() * .5) -
                                      e->localPos()) /
                                         zoom;
    ensureCanvas(center);
    centerOn(center);
    const auto residual = viewportTransform().map(panAnchor_) - e->localPos();
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() +
                                    qRound(residual.x()));
    verticalScrollBar()->setValue(verticalScrollBar()->value() +
                                  qRound(residual.y()));
    e->accept();
    return;
  }
  if (cable_) {
    updateCable(e->pos());
    e->accept();
    return;
  }
  if (!spacePan_)
    viewport()->setCursor(portAt(e->pos(), true) != -3 ||
                                  portAt(e->pos(), false) >= 0
                              ? Qt::PointingHandCursor
                              : Qt::ArrowCursor);
  QGraphicsView::mouseMoveEvent(e);
}
void WorkflowView::mouseReleaseEvent(QMouseEvent* e) {
  if (panning_) {
    mouseMoveEvent(e);
    stopPan();
    e->accept();
    return;
  }
  if (cable_ && e->button() == Qt::LeftButton) {
    const bool reverse = cableTarget_ >= 0;
    int target = reverse ? cableTarget_ : portAt(e->pos(), false);
    int source = reverse ? portAt(e->pos(), true) : cableSource_;
    if (reverse && source == -3) source = nodeAt(e->pos());
    if (!reverse && target < 0) target = nodeAt(e->pos());
    const bool clicked =
        QLineF(QPointF(cablePress_), QPointF(e->pos())).length() <
        QApplication::startDragDistance();
    const int oldTarget = rewireTarget_, oldSource = rewireSource_;
    const auto role = rewireRole_;
    const bool valid = validCable(source, target, e->pos(), reverse);
    const int menuTarget = cableTarget_;
    const QString inputType = cableInputType_;
    auto dataRole =
        reverse ? cableInputType_ : inputTypeAt(target, e->pos());
    if (!reverse && target >= 0 && portAt(e->pos(), false) != target) {
      dataRole.clear();
    }
    cancelCable();
    // Resolve release coordinates even when Windows coalesces every move
    // event.
    if (oldTarget >= 0) {
      if (valid && !clicked) {
        emit connectionRewired(oldTarget, oldSource, target, source, role);
        const bool applied =
            role == "ObjectA"   ? dataA_.value(target, -2) == source
            : role == "ObjectB" ? dataB_.value(target, -2) == source
                                : oldTarget < links_.size() &&
                                      !links_[oldTarget].contains(oldSource);
        for (auto* edge : edges_)
          edge->setSelected(
              edge->data(0).toInt() == (applied ? target : oldTarget) &&
              edge->data(2).toInt() == (applied ? source : oldSource));
      } else if (!clicked)
        emit editRejected(QStringLiteral(
            "改接未完成：保留原连接，请连接同类型端口并避免循环"));
    } else if (clicked && reverse)
      chooseInput(menuTarget, e->globalPos(), inputType);
    else if (target >= 0 && source >= -1) {
      const QString type =
          source < 0 ? "Image" : outputTypes_.value(source);
      if (valid && source >= 0)
        emit namedInputRequested(target, dataRole, source);
      else if (valid)
        if (type!="Calibration" && target >= 0 && typedInputTool(tools_[target]))
          emit typedInputSelected(target, dataRole, source);
        else
          emit inputChanged(target, source);
      else
        emit editRejected(QStringLiteral(
            "无法连接：输入类型不匹配、存在循环或不支持该类型"));
    } else if (!clicked)
      emit editRejected(QStringLiteral(
          "连线未接入：请拖到目标节点或端口（支持正向、反向连接）"));
    e->accept();
    return;
  }
  QGraphicsView::mouseReleaseEvent(e);
  if (movingNode_) {
    movingNode_ = false;
    emit nodeMoveFinished();
  }
  ensureCanvas(mapToScene(viewport()->rect().center()));
  updateOverview();
}
void WorkflowView::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
    spacePan_ = true;
    viewport()->setCursor(Qt::OpenHandCursor);
    e->accept();
    return;
  }
  if (e->key() == Qt::Key_F && e->modifiers() == Qt::NoModifier) {
    fitGraph();
    e->accept();
    return;
  }
  if (e->key() == Qt::Key_Escape && cable_) {
    cancelCable();
    e->accept();
    return;
  }
  if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
    if (cable_) cancelCable();
    const auto selected = graph_->selectedItems();
    for (auto* item : selected)
      if (item->data(1) == "workflow-node" && item->data(0).toInt() >= 0) {
        emit nodeRemoved(item->data(0).toInt());
        return;
      }
    for (auto* item : selected)
      if (item->data(1) == "workflow-edge") {
        if (!item->data(4).toString().isEmpty())
          emit typedConnectionRemoved(item->data(0).toInt(),
                                      item->data(4).toString(),
                                      item->data(2).toInt());
        else
          emit connectionRemoved(item->data(0).toInt(), item->data(2).toInt());
        return;
      }
  }
  QGraphicsView::keyPressEvent(e);
}
void WorkflowView::contextMenuEvent(QContextMenuEvent* e) {
  if (auto* n = dynamic_cast<Node*>(itemAt(e->pos()))) {
    const int i = n->index;
    if (i < 0) return;
    QMenu menu;
    auto* debug = menu.addAction(QStringLiteral("在工作台调试"));
    auto* rename = menu.addAction(QStringLiteral("重命名…"));
    auto* input =
        menu.addAction(n->geometryNode ? QStringLiteral("选择类型化输入 A…")
                                       : QStringLiteral("选择输入图像…"));
    auto* dependencies = menu.addMenu(QStringLiteral("高级：添加执行依赖"));
    for (int source = -1; source < tools_.size(); ++source) {
      if (source == i || links_[i].contains(source) ||
          (source >= 0 && workflowAncestors(links_, source).contains(i)))
        continue;
      auto* action =
          dependencies->addAction(nodes_[source + 1]->data(3).toString());
      connect(action, &QAction::triggered, this,
              [this, i, source] { emit dependencyAdded(i, source); });
    }
    auto* remove = menu.addAction(QStringLiteral("删除节点"));
    const auto* choice = menu.exec(e->globalPos());
    if (choice == debug)
      emit nodeDebugRequested(i);
    else if (choice == rename)
      emit nodeRenamed(i);
    else if (choice == input)
      chooseInput(i, e->globalPos(), n->geometryNode ? "ObjectA" : "Image");
    else if (choice == remove)
      emit nodeRemoved(i);
  } else if (auto* item = itemAt(e->pos());
             item && item->data(1) == "workflow-edge") {
    const int target = item->data(0).toInt(), source = item->data(2).toInt();
    QMenu menu;
    auto* a = menu.addAction(QStringLiteral("断开连线"));
    const auto role = item->data(4).toString();
    if (menu.exec(e->globalPos()) == a) {
      if (!role.isEmpty())
        emit typedConnectionRemoved(target, role, source);
      else
        emit connectionRemoved(target, source);
    }
  }
}
void WorkflowView::dragEnterEvent(QDragEnterEvent* e) {
  if (e->mimeData()->hasFormat(mime)) {
    bool ok = false;
    const int id = e->mimeData()->data(mime).toInt(&ok);
    if (ok && id >= 0 && id != 17 && id < toolSlotCount) {
      showDropPreview(static_cast<Algorithm>(id), mapToScene(e->pos()));
      e->acceptProposedAction();
    }
  }
}
void WorkflowView::dragMoveEvent(QDragMoveEvent* e) {
  if (e->mimeData()->hasFormat(mime)) {
    bool ok = false;
    const int id = e->mimeData()->data(mime).toInt(&ok);
    if (ok && id >= 0 && id != 17 && id < toolSlotCount) {
      showDropPreview(static_cast<Algorithm>(id), mapToScene(e->pos()));
      e->acceptProposedAction();
    }
  }
}
void WorkflowView::dragLeaveEvent(QDragLeaveEvent* e) {
  clearDropPreview();
  e->accept();
}
void WorkflowView::dropEvent(QDropEvent* e) {
  bool ok = false;
  const int id = e->mimeData()->data(mime).toInt(&ok);
  if (ok && id >= 0 && id != 17 && id < toolSlotCount) {
    const auto position = mapToScene(e->pos());
    const auto insertion = operatorDefinition(static_cast<Algorithm>(id)).output == "Image" &&
                                   !dataTool(static_cast<Algorithm>(id))
                               ? insertionConnection(position) : QPair<int, int>{-1, -3};
    clearDropPreview();
    if (insertion.first >= 0)
      emit nodeInserted(id, position, insertion.first, insertion.second);
    else emit nodeAdded(id, position);
    e->acceptProposedAction();
  }
}
void WorkflowView::drawBackground(QPainter* p, const QRectF& r) {
  const bool dark =
      QApplication::palette().color(QPalette::Window).lightness() < 128;
  p->fillRect(r, dark ? QColor("#171c26") : QColor("#f5f7fb"));
  p->setPen(dark ? QColor("#30394b") : QColor("#e0e6f0"));
  for (double x = std::floor(r.left() / 20) * 20; x < r.right(); x += 20)
    for (double y = std::floor(r.top() / 20) * 20; y < r.bottom(); y += 20)
      p->drawPoint(QPointF(x, y));
}
void WorkflowView::ensureCanvas(QPointF center) {
  const double w = viewport()->width() / transform().m11(),
               h = viewport()->height() / transform().m11();
  const auto required =
      QRectF(center.x() - 3 * w, center.y() - 3 * h, 6 * w, 6 * h)
          .united(graph_->itemsBoundingRect());
  if (!sceneRect().contains(required))
    setSceneRect(sceneRect().united(required));
}
void WorkflowView::stopPan() {
  panning_ = false;
  viewport()->setCursor(spacePan_ ? Qt::OpenHandCursor : Qt::ArrowCursor);
}
void WorkflowView::keyReleaseEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
    spacePan_ = false;
    if (!panning_) viewport()->setCursor(Qt::ArrowCursor);
    e->accept();
    return;
  }
  QGraphicsView::keyReleaseEvent(e);
}
void WorkflowView::focusOutEvent(QFocusEvent* e) {
  if (movingNode_) {
    movingNode_ = false;
    emit nodeMoveFinished();
  }
  if (cable_) cancelCable();
  spacePan_ = false;
  stopPan();
  QGraphicsView::focusOutEvent(e);
}
void WorkflowView::leaveEvent(QEvent* e) {
  if (!panning_ && !cable_) viewport()->setCursor(Qt::ArrowCursor);
  QGraphicsView::leaveEvent(e);
}
void WorkflowView::wheelEvent(QWheelEvent* e) {
  const auto anchor = e->position();
  const auto point = viewportTransform().inverted().map(anchor);
  fit_ = false;
  const double z = std::clamp(
      transform().m11() * std::pow(1.0015, e->angleDelta().y()), .01, 64.);
  setTransform(QTransform::fromScale(z, z));
  const auto center =
      point + (QPointF(viewport()->width() * .5, viewport()->height() * .5) -
               anchor) /
                  z;
  ensureCanvas(center);
  centerOn(center);
  const auto residual = viewportTransform().map(point) - anchor;
  horizontalScrollBar()->setValue(horizontalScrollBar()->value() +
                                  qRound(residual.x()));
  verticalScrollBar()->setValue(verticalScrollBar()->value() +
                                qRound(residual.y()));
  e->accept();
}
void WorkflowView::fitGraph() {
  fit_ = true;
  const auto bounds = graph_->itemsBoundingRect().adjusted(-50, -50, 50, 50);
  const double z =
      std::clamp(std::min(viewport()->width() / bounds.width(),
                          viewport()->height() / bounds.height()),
                 .01, 1.25);
  setTransform(QTransform::fromScale(z, z));
  ensureCanvas(bounds.center());
  centerOn(bounds.center());
  updateOverview();
}
void WorkflowView::resizeEvent(QResizeEvent* e) {
  const auto previous = e->oldSize().isValid() ? e->oldSize() : size();
  const auto center = mapToScene(QPoint((previous.width() - 1) / 2,
                                       (previous.height() - 1) / 2));
  QGraphicsView::resizeEvent(e);
  if (fit_)
    fitGraph();
  else {
    ensureCanvas(center);
    centerOn(center);
    const auto residual = viewportTransform().map(center) - viewport()->rect().center();
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + qRound(residual.x()));
    verticalScrollBar()->setValue(verticalScrollBar()->value() + qRound(residual.y()));
  }
  updateOverview();
}
}  // namespace workbench
