// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/views/image_view.hpp"
#include "ui/widgets/responsive_widgets.hpp"

#include <QApplication>
#include <QFontMetrics>
#include <QGraphicsPixmapItem>
#include <QGraphicsSimpleTextItem>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPathStroker>
#include <QScrollBar>
#include <QWheelEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
namespace workbench {
namespace {
QColor colorFor(const QString& type, bool dark) {
  if (type == "Region") return QColor(dark ? "#83a2ed" : "#5e79d1");
  if (type == "Match") return QColor(dark ? "#91a8ff" : "#657ee8");
  if (type == "Text") return QColor(dark ? "#c7a3ff" : "#8c62c6");
  if (type == "Point" || type == "Cross")
    return QColor(dark ? "#ffb768" : "#cc783b");
  return QColor(dark ? "#5adac1" : "#159c87");
}
class ResultPixmap final : public QGraphicsPixmapItem {
 public:
  bool compare = false;
  double splitX = 0;
  QPainterPath domain;
  void paint(QPainter* p, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override {
    if (domain.isEmpty()) return;
    p->save();
    p->setClipPath(domain, Qt::IntersectClip);
    if (compare) {
      const auto bounds = boundingRect();
      const double left = std::max(bounds.left(), splitX);
      p->setClipRect(
          QRectF(left, bounds.top(), std::max(0., bounds.right() - left),
                 bounds.height()),
          Qt::IntersectClip);
    }
    QGraphicsPixmapItem::paint(p, option, widget);
    p->restore();
  }
};
}  // namespace
ImageView::ImageView(QWidget* parent)
    : QGraphicsView(parent), scene_(new QGraphicsScene(this)) {
  setObjectName("imageView");
  setScene(scene_);
  setFrameShape(QFrame::NoFrame);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setTransformationAnchor(NoAnchor);
  setResizeAnchor(NoAnchor);
  setRenderHint(QPainter::Antialiasing);
  image_ = scene_->addPixmap(QPixmap());
  image_->setOffset(-.5, -.5);
  resultImage_ = new ResultPixmap;
  scene_->addItem(resultImage_);
  resultImage_->setOffset(-.5, -.5);
  resultImage_->setZValue(1);
  resultImage_->hide();
  roiItem_ = scene_->addPath(QPainterPath());
  roiItem_->setZValue(20);
  QPen rangePen(QColor("#39a892"),1,Qt::DashLine);rangePen.setCosmetic(true);
  rangeItem_=scene_->addPath({},rangePen);rangeItem_->setZValue(19);
  caption_ = new QLabel(this);
  caption_->setObjectName("canvasCaption");
  caption_->setTextFormat(Qt::PlainText);
  caption_->setAttribute(Qt::WA_TransparentForMouseEvents);
  caption_->hide();
  pixelLabel_ = new ElidedLabel(this);
  pixelLabel_->setElideMode(Qt::ElideMiddle);
  pixelLabel_->setObjectName("pixelStatus");
  pixelLabel_->setTextFormat(Qt::PlainText);
  pixelLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
  pixelLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  pixelLabel_->hide();
  rangeContext_ = new QLabel(this);
  rangeContext_->setObjectName("rangeContext");
  rangeContext_->setTextFormat(Qt::PlainText);
  rangeContext_->hide();
  updateTheme();
  setTool(Tool::Select);
}
void ImageView::setImage(const QImage& image, bool preserveView) {
  if (preserveView && hasImage() && image.size() == imageSize_) {
    image_->setPixmap(QPixmap::fromImage(image));
    updateComparisonClip();
    positionTools();
    emit displayStateChanged();
    return;
  }
  edit_ = Edit::None;
  panning_ = curtainDragging_ = false;
  roiSelected_ = false;
  imageSize_ = image.size();
  rangeItem_->setPath({});
  image_->setPixmap(QPixmap::fromImage(image));
  layers_[0] = true;
  layers_[1] = false;
  compare_ = false;
  setResultImage({});
  scene_->setSceneRect(QRectF(-.5, -.5, image.width(), image.height()));
  setSceneRect(scene_->sceneRect());
  setResults({});
  setRoi({});
  fitImage();
  positionTools();
  emit displayStateChanged();
}
void ImageView::setImageName(const QString& name) {
  caption_->setText(name);
  caption_->setToolTip(name);
  positionTools();
}
void ImageView::setPixelText(const QString& text) {
  pixelTemplate_ = text;
  pixelFullText_ = text;
  pixelLabel_->setText(text);
  positionTools();
}
void ImageView::setPixelSample(int row, int column, const QString& values,
                               PixelSource source, mv::MvPixelType type) {
  const int normalWidth = std::max(
      4, QString::number(std::max(imageSize_.width(), imageSize_.height()))
             .size());
  const int coordinateWidth =
      source == PixelSource::None
          ? std::max({normalWidth, QString::number(row).size(),
                      QString::number(column).size()})
          : normalWidth;
  const auto field = [coordinateWidth](int value) {
    return QString::number(value).rightJustified(coordinateWidth);
  };
  const auto coordinate = QString(coordinateWidth, QLatin1Char('8'));
  const auto location =
      source == PixelSource::Original    ? QStringLiteral("原始图像")
      : source == PixelSource::Input     ? QStringLiteral("节点输入")
      : source == PixelSource::Processed ? QStringLiteral("结果图像")
                                         : QStringLiteral("图像外部");
  QString text =
      QStringLiteral("Row %1 · Col %2").arg(field(row), field(column));
  QString budget = QStringLiteral("Row %1 · Col %1").arg(coordinate);
  if (source != PixelSource::None) {
    const int width = type == mv::MvPixelType::UInt8    ? 3
                      : type == mv::MvPixelType::UInt16 ? 5
                      : type == mv::MvPixelType::Int16  ? 6
                                                        : 16;
    auto channels = values.split(", ");
    QStringList channelBudgets;
    for (auto& channel : channels) {
      channel = channel.rightJustified(width);
      channelBudgets.append(QString(width, QLatin1Char('8')));
    }
    text += QStringLiteral(" · [%1]").arg(channels.join(","));
    budget += QStringLiteral(" · [%1]").arg(channelBudgets.join(","));
  }
  text += QStringLiteral(" · ") + location;
  budget += QStringLiteral(" · 原始图像");
  const bool resized = pixelTemplate_ != budget;
  pixelTemplate_ = budget;
  pixelFullText_ = text;
  pixelLabel_->setToolTip(text);
  pixelLabel_->setText(text);
  if (resized) positionTools();
}
void ImageView::setRoi(const Roi& roi) {
  if ((edit_ == Edit::None && roi_ == roi) ||
      (edit_ != Edit::None && previousRoi_ == roi))
    return;
  edit_ = Edit::None;
  roi_ = roi;
  roiSelected_ = false;
  drawRoi();
}
void ImageView::setRangeOverlay(const QPainterPath& path) {
  rangeItem_->setPath(path);rangeItem_->setVisible(layers_[2]&&!path.isEmpty());viewport()->update();
}
void ImageView::setRangeContext(const QString& text, bool atRest,
                                const QPointF& origin, double angle, bool frame) {
  rangeContextText_ = text;
  rangeContext_->setText(text);
  rangeContext_->setToolTip(text);
  contextAtRest_ = atRest;
  referenceOrigin_ = origin;
  referenceAngle_ = angle;
  referenceVisible_ = frame;
  positionTools();
  viewport()->update();
}
void ImageView::setRoiEditingEnabled(bool enabled) {
  if (editable_ == enabled) return;
  editable_ = enabled;
  if (!enabled) {
    roiSelected_ = false;
    if (int(tool_) >= int(Tool::Rectangle)) setTool(Tool::Select);
  } else
    setTool(Tool::Select);
  viewport()->update();
}
void ImageView::setTool(Tool tool) {
  if (edit_ != Edit::None) {
    roi_ = previousRoi_;
    edit_ = Edit::None;
    drawRoi();
  }
  tool_ =
      !editable_ && (int(tool) >= int(Tool::Rectangle)) ? Tool::Select : tool;
  panning_ = false;
  if (tool_ != Tool::Select) roiSelected_ = false;
  viewport()->setCursor(tool_ == Tool::Pan      ? Qt::OpenHandCursor
                        : tool_ == Tool::Select ? Qt::ArrowCursor
                                                : Qt::CrossCursor);
  viewport()->update();
  positionTools();
  emit toolChanged();
}
void ImageView::drawRoi() {
  auto path = roiPath(roi_);
  roiItem_->setPath(path);
  roiItem_->setVisible(layers_[2]);
  rangeItem_->setVisible(layers_[2]&&!rangeItem_->path().isEmpty());
  positionTools();
  viewport()->update();
}
void ImageView::setResultImage(const QImage& image) {
  QPainterPath domain;
  if (!image.isNull())
    domain.addRect(QRectF(-.5, -.5, image.width(), image.height()));
  setResultImage(image, domain);
}
void ImageView::setResultImage(const QImage& image,
                               const QPainterPath& domain) {
  resultDomain_ = domain;
  auto* raster = static_cast<ResultPixmap*>(resultImage_);
  raster->domain = domain;
  raster->setPixmap(QPixmap::fromImage(image));
  if (image.isNull()) {
    compare_ = false;
    layers_[1] = false;
  }
  raster->setVisible(layers_[1] && !image.isNull());
  updateComparisonClip();
  emit displayStateChanged();
}
bool ImageView::hasResultImage() const {
  return !resultImage_->pixmap().isNull();
}
bool ImageView::layerVisible(Layer layer) const {
  return layers_[static_cast<int>(layer)];
}
void ImageView::setLayerVisible(Layer layer, bool visible) {
  layers_[static_cast<int>(layer)] = visible;
  if (compare_ && (!layers_[0] || !layers_[1])) compare_ = false;
  image_->setVisible(layers_[0]);
  resultImage_->setVisible(layers_[1] && hasResultImage());
  roiItem_->setVisible(layers_[2]);
  rangeItem_->setVisible(layers_[2]&&!rangeItem_->path().isEmpty());
  for (auto* item : overlays_) item->setVisible(layers_[3]);
  updateComparisonClip();
  positionTools();
  emit displayStateChanged();
}
void ImageView::setCompare(bool enabled) {
  compare_ = enabled && hasImage() && hasResultImage();
  if (compare_) {
    layers_[0] = layers_[1] = true;
    image_->show();
    resultImage_->show();
  }
  updateComparisonClip();
  emit displayStateChanged();
}
void ImageView::setComparePosition(double position) {
  if (!std::isfinite(position)) return;
  comparePosition_ = std::clamp(position, 0., 1.);
  updateComparisonClip();
}
double ImageView::compareViewportX() const {
  if (!hasImage()) return viewport()->width() * .5;
  const auto bounds = mapFromScene(image_->boundingRect()).boundingRect();
  const double left = std::max(0, bounds.left()),
               right = std::min(viewport()->width(), bounds.right());
  return left <= right
             ? std::clamp(viewport()->width() * comparePosition_, left, right)
             : viewport()->width() * comparePosition_;
}
void ImageView::updateComparisonClip() {
  if (!resultImage_) return;
  auto* raster = static_cast<ResultPixmap*>(resultImage_);
  raster->compare = compare_;
  raster->splitX = mapToScene(QPoint(qRound(compareViewportX()), 0)).x();
  raster->update();
  viewport()->update();
}
ImageView::PixelSource ImageView::pixelSourceAt(int row, int column) const {
  if (row < 0 || column < 0 || row >= imageSize_.height() ||
      column >= imageSize_.width())
    return PixelSource::None;
  const bool processed =
      layers_[1] && hasResultImage() &&
      resultDomain_.contains(QPointF(column, row)) &&
      (!compare_ || column >= static_cast<ResultPixmap*>(resultImage_)->splitX);
  if (processed) return PixelSource::Processed;
  return layers_[0] ? PixelSource::Original : PixelSource::None;
}
void ImageView::setQualityVisible(bool visible) {
  qualityVisible_ = visible;
  if (visible && selectedResult_ < 0)
    for (int i = 0; i < qualityResults_.size(); ++i)
      if (!qualityResults_[i].samples.isEmpty()) {
        selectResult(i);
        emit resultPicked(i);
        break;
      }
  viewport()->update();
}
void ImageView::setResults(const QVector<ResultObject>& objects) {
  pendingResult_ = -1;
  for (auto* item : overlays_) {
    scene_->removeItem(item);
    delete item;
  }
  overlays_.clear();
  resultTypes_.clear();
  qualityResults_ = objects;
  selectedResult_ = -1;
  for (const auto& object : objects) {
    auto* item = scene_->addPath(object.overlay);
    item->setZValue(10);
    item->setVisible(layers_[3]);
    // Scalars without a geometric anchor belong to the result table. Giving
    // every calibration RMS an image-origin label obscures the actual marks.
    if ((object.type != "Value" || !object.overlay.isEmpty()) && (!object.label.isEmpty() ||
        !object.values.isEmpty() && object.type != "XLD" &&
            object.type != "Match" && object.type != "Point")) {
      auto* label = new QGraphicsSimpleTextItem(item);
      label->setFlag(QGraphicsItem::ItemIgnoresTransformations);
      label->setText((object.label.isEmpty() ? object.type : object.label) +
                     " " + QString::number(object.measure, 'f', 3) + " " +
                     object.unit);
      label->setBrush(object.verdict == 0   ? QColor("#e86767")
                      : object.verdict == 1 ? QColor("#38ba92")
                                            : QColor("#6575dd"));
      label->setPos(object.center + QPointF(3, -5));
    }
    overlays_.append(item);
    resultTypes_.append(object.type);
  }
  selectResult(-1);
  if (qualityVisible_)
    for (int i = 0; i < qualityResults_.size(); ++i)
      if (!qualityResults_[i].samples.isEmpty()) {
        selectResult(i);
        break;
      }
  emit displayStateChanged();
}
void ImageView::setOverlayVisible(bool visible) {
  setLayerVisible(Layer::Elements, visible);
}
QColor ImageView::elementColor() const {
  if (resultTypes_.isEmpty()) return colorFor("Region", dark_);
  for (const auto& type : resultTypes_)
    if (type != resultTypes_.first())
      return QColor(dark_ ? "#b3a2ff" : "#8062c7");
  return colorFor(resultTypes_.first(), dark_);
}
QString ImageView::elementIcon() const {
  if (resultTypes_.isEmpty()) return "layers";
  for (const auto& type : resultTypes_)
    if (type != resultTypes_.first()) return "layers";
  const auto type = resultTypes_.first();
  return type == "Match"                      ? "match"
         : type == "XLD"                      ? "edges"
         : type == "Point" || type == "Cross" ? "point"
         : type == "Text"                     ? "text"
                                              : "layers";
}
QString ImageView::elementDescription() const {
  QStringList types;
  for (const auto& type : resultTypes_) {
    const auto label = type == "Region"  ? QStringLiteral("区域")
                       : type == "XLD"   ? QStringLiteral("轮廓")
                       : type == "Match" ? QStringLiteral("匹配框")
                       : type == "Text"  ? QStringLiteral("文字")
                       : type == "Point" || type == "Cross"
                           ? QStringLiteral("点 / 交点")
                           : type;
    if (!types.contains(label)) types.append(label);
  }
  return types.isEmpty() ? QStringLiteral("结果元素：尚无输出")
                         : QStringLiteral("结果元素：%1 · %2 个对象")
                               .arg(types.join(" / "))
                               .arg(overlays_.size());
}
void ImageView::selectResult(int index) {
  selectedResult_ = index;
  for (int i = 0; i < overlays_.size(); ++i) {
    const QColor color = i == index ? QColor(dark_ ? "#ffc465" : "#b57a22")
                                    : colorFor(resultTypes_[i], dark_);
    QPen pen(color, i == index ? 2.5 : 1.25);
    pen.setCosmetic(true);
    const bool fill = resultTypes_[i] == "Region" ||
                      resultTypes_[i] == "Text" || resultTypes_[i] == "Point";
    overlays_[i]->setPen(resultTypes_[i] == "Region" ? QPen(Qt::NoPen) : pen);
    QColor brush = color;
    brush.setAlpha(resultTypes_[i] == "Region" ? (i == index ? 140 : 65) : 255);
    overlays_[i]->setBrush(fill ? QBrush(brush) : QBrush(Qt::NoBrush));
  }
}
int ImageView::resultAt(const QPoint& point) const {
  if (!layers_[3]) return -1;
  const auto scenePoint = mapToScene(point);
  const double tolerance = 6 / std::max(.01, zoom());
  int hit = -1;
  double area = 1e300;
  for (int i = 0; i < overlays_.size(); ++i) {
    const auto& path = overlays_[i]->path();
    const auto bounds = path.boundingRect();
    if (!bounds.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(scenePoint)) continue;
    bool contains = (resultTypes_[i] == "Region" || resultTypes_[i] == "Match" || resultTypes_[i] == "Text") &&
                    path.contains(scenePoint);
    if (!contains) {
      QPainterPathStroker stroke;
      stroke.setWidth(tolerance * 2);
      contains = stroke.createStroke(path).contains(scenePoint);
    }
    const double candidate = std::max(1., bounds.width() * bounds.height());
    if (contains && candidate <= area) { hit = i; area = candidate; }
  }
  return hit;
}
bool ImageView::focusResult(int index) {
  if (index < 0 || index >= overlays_.size() || !layers_[3]) return false;
  auto bounds = overlays_[index]->path().boundingRect();
  if (bounds.isEmpty()) bounds = QRectF(bounds.center() - QPointF(4, 4), QSizeF(8, 8));
  const double margin = std::max(8., std::max(bounds.width(), bounds.height()) * .15);
  bounds.adjust(-margin, -margin, margin, margin);
  const double scale = std::clamp(std::min(viewport()->width() / bounds.width(),
                                          viewport()->height() / bounds.height()), .01, 64.);
  fit_ = false;
  setTransform(QTransform::fromScale(scale, scale));
  ensureCanvas(bounds.center());
  centerOn(bounds.center());
  // QGraphicsView rounds centerOn to integer scroll positions. Correct the
  // remaining device-pixel offset, as in manual resize preservation.
  const auto residual=viewportTransform().map(bounds.center())-viewport()->rect().center();
  horizontalScrollBar()->setValue(horizontalScrollBar()->value()+qRound(residual.x()));
  verticalScrollBar()->setValue(verticalScrollBar()->value()+qRound(residual.y()));
  selectResult(index);
  updateComparisonClip();
  emit zoomChanged(zoom());
  return true;
}
void ImageView::updateTheme() {
  dark_ = QApplication::palette().color(QPalette::Window).lightness() < 128;
  setBackgroundBrush(QColor(dark_ ? "#171d26" : "#e9edf4"));
  if (roiItem_) {
    QPen pen(QColor(dark_ ? "#ffc465" : "#b57a22"), 1.5, Qt::DashLine);
    pen.setCosmetic(true);
    roiItem_->setPen(pen);
  }
  selectResult(selectedResult_);
  viewport()->update();
  emit displayStateChanged();
}
void ImageView::changeEvent(QEvent* event) {
  QGraphicsView::changeEvent(event);
  if (roiItem_ && (event->type() == QEvent::PaletteChange ||
                   event->type() == QEvent::ApplicationPaletteChange))
    updateTheme();
}
void ImageView::fitImage() {
  fit_ = true;
  if (hasImage()) {
    const auto imageRect = image_->sceneBoundingRect();
    fitInView(imageRect.adjusted(-8, -8, 8, 8), Qt::KeepAspectRatio);
    ensureCanvas(imageRect.center());
    centerOn(imageRect.center());
  }
  updateComparisonClip();
  emit zoomChanged(zoom());
}
void ImageView::actualSize() {
  const auto center = mapToScene(viewport()->rect().center());
  fit_ = false;
  resetTransform();
  ensureCanvas(center);
  centerOn(center);
  updateComparisonClip();
  emit zoomChanged(zoom());
}
void ImageView::zoomBy(double factor) {
  zoomAt(factor, viewport()->rect().center());
}
void ImageView::ensureCanvas(const QPointF& center) {
  if (!hasImage()) return;
  const double w = viewport()->width() / std::max(.01, zoom()),
               h = viewport()->height() / std::max(.01, zoom());
  const auto required =
      QRectF(center.x() - 3 * w, center.y() - 3 * h, 6 * w, 6 * h)
          .united(image_->sceneBoundingRect());
  if (!sceneRect().contains(required))
    setSceneRect(sceneRect().united(required));
}
void ImageView::zoomAt(double factor, const QPoint& anchor) {
  if (!hasImage() || !std::isfinite(factor) || factor <= 0) return;
  const auto point = mapToScene(anchor);
  fit_ = false;
  const double target = std::clamp(zoom() * factor, .01, 64.);
  setTransform(QTransform::fromScale(target, target));
  const auto center =
      point + QPointF(viewport()->rect().center() - anchor) / target;
  ensureCanvas(center);
  centerOn(center);
  const auto residual = mapFromScene(point) - anchor;
  horizontalScrollBar()->setValue(horizontalScrollBar()->value() +
                                  residual.x());
  verticalScrollBar()->setValue(verticalScrollBar()->value() + residual.y());
  updateComparisonClip();
  emit zoomChanged(zoom());
}
void ImageView::wheelEvent(QWheelEvent* event) {
  zoomAt(std::pow(1.0015, event->angleDelta().y()),
         event->position().toPoint());
  event->accept();
}
QPointF ImageView::imagePoint(const QPoint& point) const {
  const auto p = mapToScene(point);
  return {std::clamp(std::round(p.x()), 0., double(imageSize_.width() - 1)),
          std::clamp(std::round(p.y()), 0., double(imageSize_.height() - 1))};
}
QVector<QPointF> ImageView::roiHandles() const {
  const auto r = roi_.bounds;
  QVector<QPointF> handles = {r.topLeft(),     {r.center().x(), r.top()},
                              r.topRight(),    {r.right(), r.center().y()},
                              r.bottomRight(), {r.center().x(), r.bottom()},
                              r.bottomLeft(),  {r.left(), r.center().y()}};
  if (roi_.kind == RoiKind::RotatedRectangle) {
    QTransform t;
    t.translate(r.center().x(), r.center().y());
    t.rotate(roi_.angle);
    t.translate(-r.center().x(), -r.center().y());
    handles.append(
        QPointF(r.center().x(), r.top() - 22 / std::max(.01, zoom())));
    for (auto& p : handles) p = t.map(p);
  }
  if (roi_.kind == RoiKind::Annulus)
    handles.append(r.center() + QPointF(r.width() / 2 * roi_.innerRatio, 0));
  return handles;
}
int ImageView::handleAt(const QPoint& point) const {
  if (!roiSelected_ || !layers_[2] || !editable_) return -1;
  const auto handles = roiHandles();
  int nearest = -1;
  double distance = 8;
  for (int i = 0; i < handles.size(); ++i) {
    if ((roi_.kind == RoiKind::Circle || roi_.kind == RoiKind::Annulus) &&
        i < 8 && i % 2 == 0)
      continue;
    const double current = QLineF(point, mapFromScene(handles[i])).length();
    if (current < distance) {
      nearest = i;
      distance = current;
    }
  }
  return nearest;
}
bool ImageView::roiHit(const QPoint& point) const {
  if (!layers_[2] || roi_.kind == RoiKind::None) return false;
  if (roiItem_->path().contains(mapToScene(point))) return true;
  QPainterPathStroker stroke;
  stroke.setWidth(10 / std::max(.01, zoom()));
  return stroke.createStroke(roiItem_->path()).contains(mapToScene(point));
}
bool ImageView::curtainHit(const QPoint& point) const {
  if (!compare_) return false;
  const auto bounds = mapFromScene(image_->boundingRect())
                          .boundingRect()
                          .intersected(viewport()->rect());
  return bounds.contains(point) &&
         std::abs(point.x() - compareViewportX()) <= 8;
}
void ImageView::updatePointer(const QPoint& point) {
  if (panning_)
    viewport()->setCursor(Qt::ClosedHandCursor);
  else if (curtainHit(point) || curtainDragging_)
    viewport()->setCursor(Qt::SplitHCursor);
  else if (spacePan_ || tool_ == Tool::Pan)
    viewport()->setCursor(Qt::OpenHandCursor);
  else if (tool_ == Tool::Select) {
    const int handle = handleAt(point);
    viewport()->setCursor(
        handle == 8  ? Qt::PointingHandCursor
        : handle < 0 ? (roiHit(point) ? Qt::SizeAllCursor : Qt::ArrowCursor)
        : handle % 2 ? (handle == 1 || handle == 5 ? Qt::SizeVerCursor
                                                   : Qt::SizeHorCursor)
        : handle == 0 || handle == 4 ? Qt::SizeFDiagCursor
                                     : Qt::SizeBDiagCursor);
  } else
    viewport()->setCursor(Qt::CrossCursor);
}
void ImageView::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && tool_ == Tool::Select &&
      hasImage()) {
    auto p = mapToScene(event->pos());
    emit pixelPicked(int(std::floor(p.y() + .5)), int(std::floor(p.x() + .5)));
  }

  if (!hasImage()) return;
  setFocus(Qt::MouseFocusReason);
  if (event->button() == Qt::MiddleButton ||
      (event->button() == Qt::LeftButton && spacePan_)) {
    panning_ = true;
    panAnchor_ = viewportTransform().inverted().map(event->localPos());
    updatePointer(event->pos());
    event->accept();
    return;
  }
  if (event->button() != Qt::LeftButton) {
    QGraphicsView::mousePressEvent(event);
    return;
  }
  if (curtainHit(event->pos())) {
    curtainDragging_ = true;
    updatePointer(event->pos());
    event->accept();
    return;
  }
  if (tool_ == Tool::Pan) {
    panning_ = true;
    panAnchor_ = viewportTransform().inverted().map(event->localPos());
    updatePointer(event->pos());
    event->accept();
    return;
  }
  previousRoi_ = roi_;
  anchor_ = mapToScene(event->pos());
  if (tool_ == Tool::Select) {
    pendingResult_ = resultAt(event->pos());
    if (editable_ && layers_[2] && roi_.kind != RoiKind::None) {
      QPainterPathStroker boundary;
      boundary.setWidth(8 / std::max(.01, zoom()));
      if (boundary.createStroke(roiItem_->path()).contains(mapToScene(event->pos())))
        pendingResult_ = -1;
    }
    resultPress_ = event->pos();
    resizeHandle_ = editable_ ? handleAt(event->pos()) : -1;
    if (resizeHandle_ >= 0)
      edit_ = Edit::Resize;
    else if (editable_ && roiHit(event->pos())) {
      roiSelected_ = true;
      edit_ = Edit::Move;
    } else
      roiSelected_ = false;
    viewport()->update();
    event->accept();
    return;
  }
  anchor_ = imagePoint(event->pos());
  edit_ = Edit::Draw;
  roiSelected_ = false;
  roi_ = {tool_ == Tool::Rectangle          ? RoiKind::Rectangle
          : tool_ == Tool::RotatedRectangle ? RoiKind::RotatedRectangle
          : tool_ == Tool::Annulus          ? RoiKind::Annulus
                                            : RoiKind::Circle,
          QRectF(anchor_, anchor_)};
  drawRoi();
  event->accept();
}
void ImageView::moveRoi(const QPointF& delta) {
  auto r = previousRoi_.bounds;
  r.moveLeft(std::clamp(r.left() + std::round(delta.x()), 0.,
                        std::max(0., imageSize_.width() - 1 - r.width())));
  r.moveTop(std::clamp(r.top() + std::round(delta.y()), 0.,
                       std::max(0., imageSize_.height() - 1 - r.height())));
  roi_.bounds = r;
}
void ImageView::mouseMoveEvent(QMouseEvent* event) {
  if (!hasImage()) return;
  const auto position = viewportTransform().inverted().map(event->localPos());
  if (panning_) {
    fit_ = false;
    const auto center = panAnchor_ + (QPointF(viewport()->width() * .5,
                                              viewport()->height() * .5) -
                                      event->localPos()) /
                                         zoom();
    ensureCanvas(center);
    centerOn(center);
    const auto residual =
        viewportTransform().map(panAnchor_) - event->localPos();
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() +
                                    qRound(residual.x()));
    verticalScrollBar()->setValue(verticalScrollBar()->value() +
                                  qRound(residual.y()));
  } else if (curtainDragging_)
    setComparePosition(double(event->pos().x()) /
                       std::max(1, viewport()->width()));
  else if (edit_ == Edit::Move) {
    moveRoi(position - anchor_);
    drawRoi();
  } else if (edit_ == Edit::Resize) {
    auto point = imagePoint(event->pos());
    const auto r = previousRoi_.bounds;
    if (roi_.kind == RoiKind::RotatedRectangle && resizeHandle_ == 8) {
      const auto delta = mapToScene(event->pos()) - r.center();
      roi_.angle = std::remainder(
          std::atan2(delta.y(), delta.x()) * 180 / 3.14159265358979323846 + 90.,
          360.);
      drawRoi();
      event->accept();
      return;
    }
    if (roi_.kind == RoiKind::Annulus && resizeHandle_ == 8) {
      roi_.innerRatio =
          std::clamp(QLineF(r.center(), mapToScene(event->pos())).length() /
                         (r.width() / 2),
                     .01, .99);
      drawRoi();
      event->accept();
      return;
    }
    if (roi_.kind == RoiKind::RotatedRectangle) {
      QTransform inverse;
      inverse.rotate(-roi_.angle);
      point = r.center() + inverse.map(point - r.center());
    }
    if (roi_.kind == RoiKind::Circle || roi_.kind == RoiKind::Annulus) {
      const auto c = r.center();
      const double radius =
          std::clamp(resizeHandle_ == 1 || resizeHandle_ == 5
                         ? std::abs(point.y() - c.y())
                         : std::abs(point.x() - c.x()),
                     .5,
                     std::min({c.x(), c.y(), imageSize_.width() - 1 - c.x(),
                               imageSize_.height() - 1 - c.y()}));
      roi_.bounds =
          QRectF(c.x() - radius, c.y() - radius, 2 * radius, 2 * radius);
    } else {
      double left = r.left(), right = r.right(), top = r.top(),
             bottom = r.bottom();
      if (resizeHandle_ == 0 || resizeHandle_ == 6 || resizeHandle_ == 7)
        left = std::min(point.x(), right - 1);
      if (resizeHandle_ == 2 || resizeHandle_ == 3 || resizeHandle_ == 4)
        right = std::max(point.x(), left + 1);
      if (resizeHandle_ == 0 || resizeHandle_ == 1 || resizeHandle_ == 2)
        top = std::min(point.y(), bottom - 1);
      if (resizeHandle_ == 4 || resizeHandle_ == 5 || resizeHandle_ == 6)
        bottom = std::max(point.y(), top + 1);
      roi_.bounds = QRectF(QPointF(left, top), QPointF(right, bottom));
      if (roi_.kind == RoiKind::RotatedRectangle) {
        QTransform rotation;
        rotation.rotate(roi_.angle);
        auto delta = rotation.map(roi_.bounds.center() - r.center());
        auto center = r.center() + delta;
        const double w = std::min(roi_.bounds.width(),
                                  double(imageSize_.width() - 1)),
                     h = std::min(roi_.bounds.height(),
                                  double(imageSize_.height() - 1));
        center.setX(
            std::clamp(center.x(), w / 2, imageSize_.width() - 1 - w / 2));
        center.setY(
            std::clamp(center.y(), h / 2, imageSize_.height() - 1 - h / 2));
        roi_.bounds = QRectF(center - QPointF(w / 2, h / 2), QSizeF(w, h));
      }
    }
    drawRoi();
  } else if (edit_ == Edit::Draw) {
    const auto point = imagePoint(event->pos());
    if (tool_ == Tool::Rectangle || tool_ == Tool::RotatedRectangle)
      roi_.bounds = QRectF(anchor_, point).normalized();
    else {
      const double radius =
          std::min({QLineF(anchor_, point).length(), anchor_.x(), anchor_.y(),
                    imageSize_.width() - 1 - anchor_.x(),
                    imageSize_.height() - 1 - anchor_.y()});
      roi_.bounds = QRectF(anchor_.x() - radius, anchor_.y() - radius,
                           2 * radius, 2 * radius);
    }
    drawRoi();
  }
  updatePointer(event->pos());
  const auto sample = viewportTransform().inverted().map(event->localPos());
  emit cursorPixel(int(std::floor(sample.y() + .5)),
                   int(std::floor(sample.x() + .5)));
  event->accept();
}
void ImageView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && tool_ == Tool::Select &&
      pendingResult_ >= 0 && resizeHandle_ < 0 &&
      (event->pos() - resultPress_).manhattanLength() < QApplication::startDragDistance()) {
    roi_ = previousRoi_;
    edit_ = Edit::None;
    roiSelected_ = false;
    const int picked = pendingResult_;
    pendingResult_ = -1;
    drawRoi();
    selectResult(picked);
    emit resultPicked(picked);
    event->accept();
    return;
  }
  pendingResult_ = -1;
  if (panning_ && (event->button() == Qt::LeftButton ||
                   event->button() == Qt::MiddleButton)) {
    mouseMoveEvent(event);
    panning_ = false;
    updatePointer(event->pos());
    event->accept();
    return;
  }
  if (curtainDragging_ && event->button() == Qt::LeftButton) {
    setComparePosition(double(event->pos().x()) /
                       std::max(1, viewport()->width()));
    curtainDragging_ = false;
    updatePointer(event->pos());
    event->accept();
    return;
  }
  if (edit_ != Edit::None && event->button() == Qt::LeftButton) {
    mouseMoveEvent(event);
    edit_ = Edit::None;
    if (roi_.bounds.width() < 1 || roi_.bounds.height() < 1)
      roi_ = previousRoi_;
    roiSelected_ = roi_.kind != RoiKind::None;
    setTool(Tool::Select);
    drawRoi();
    if (!(roi_ == previousRoi_)) emit roiChanged(roi_);
    event->accept();
    return;
  }
  QGraphicsView::mouseReleaseEvent(event);
}
void ImageView::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && tool_ == Tool::Select && !curtainHit(event->pos())) {
    const int picked = resultAt(event->pos());
    if (picked >= 0) {
      edit_ = Edit::None;
      roiSelected_ = false;
      pendingResult_ = -1;
      selectResult(picked);
      emit resultActivated(picked);
      event->accept();
      return;
    }
  }
  QGraphicsView::mouseDoubleClickEvent(event);
}
void ImageView::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Delete && editable_ && tool_ == Tool::Select &&
      roiSelected_) {
    setRoi({});
    emit roiChanged(roi_);
    event->accept();
    return;
  }

  if (event->key() == Qt::Key_Escape) {
    pendingResult_ = -1;
    if (edit_ != Edit::None) {
      roi_ = previousRoi_;
      edit_ = Edit::None;
      drawRoi();
    }
    roiSelected_ = false;
    curtainDragging_ = false;
    viewport()->update();
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Space && edit_ == Edit::None) {
    spacePan_ = true;
    viewport()->setCursor(Qt::OpenHandCursor);
    event->accept();
    return;
  }
  if (editable_ && tool_ == Tool::Select && roiSelected_ && layers_[2] &&
      edit_ == Edit::None) {
    QPointF delta;
    const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
    if (event->key() == Qt::Key_Left)
      delta.setX(-step);
    else if (event->key() == Qt::Key_Right)
      delta.setX(step);
    else if (event->key() == Qt::Key_Up)
      delta.setY(-step);
    else if (event->key() == Qt::Key_Down)
      delta.setY(step);
    if (!delta.isNull()) {
      previousRoi_ = roi_;
      moveRoi(delta);
      drawRoi();
      if (!(roi_ == previousRoi_)) emit roiChanged(roi_);
      event->accept();
      return;
    }
  }
  QGraphicsView::keyPressEvent(event);
}
void ImageView::keyReleaseEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Space && spacePan_) {
    spacePan_ = false;
    if (!panning_) setTool(tool_);
    event->accept();
    return;
  }
  QGraphicsView::keyReleaseEvent(event);
}
void ImageView::focusOutEvent(QFocusEvent* event) {
  pendingResult_ = -1;
  spacePan_ = false;
  panning_ = false;
  curtainDragging_ = false;
  if (edit_ != Edit::None) {
    roi_ = previousRoi_;
    edit_ = Edit::None;
    drawRoi();
  }
  QGraphicsView::focusOutEvent(event);
}
void ImageView::positionTools() {
  const auto bounds = viewport()->geometry();
  for (const auto& name : {"viewTools", "layerTools"})
    if (auto* w = findChild<QWidget*>(name, Qt::FindDirectChildrenOnly)) {
      w->setVisible(hasImage());
      auto* more = w->findChild<QToolButton*>("railMore", Qt::FindDirectChildrenOnly);
      auto buttons = w->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
      if (!more) {
        more = new QToolButton(w);
        more->setObjectName("railMore");
        more->setIcon(studioIcon("more"));
        more->setIconSize(QSize(18,18));
        more->setToolTip(QStringLiteral("更多工具"));
        more->setAccessibleName(QStringLiteral("更多工具"));
        more->setPopupMode(QToolButton::InstantPopup);
        auto* menu = new QMenu(more);
        more->setMenu(menu);
        qobject_cast<QVBoxLayout*>(w->layout())->addWidget(more);
        connect(menu, &QMenu::aboutToShow, w, [menu, w, more] {
          menu->clear();
          for (auto* b : w->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly)) if (b != more && b->isHidden()) {
            auto* a = menu->addAction(b->icon(), b->toolTip());
            a->setEnabled(b->isEnabled());
            a->setCheckable(b->isCheckable());
            a->setChecked(b->isChecked());
            connect(a, &QAction::triggered, b, &QToolButton::click);
            if (b->menu()) menu->addMenu(b->menu());
          }
        });
      }
      buttons.removeAll(more);
      const int rowHeight = buttons.isEmpty() ? 34 : qMax(32, buttons.first()->sizeHint().height()) + 3;
      const int capacity = qMax(1, (bounds.height() - 66) / rowHeight);
      const bool overflow = capacity < buttons.size();
      for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setVisible(!overflow || i < qMax(0, capacity - 1));
      more->setVisible(overflow);
      w->adjustSize();
      w->move(bounds.left() + (QString::fromLatin1(name) == "viewTools"
                                   ? 10
                                   : bounds.width() - w->width() - 10),
              bounds.top() + 12);
      w->raise();
    }
  if (pixelLabel_) {
    pixelLabel_->setVisible(hasImage() && !pixelLabel_->text().isEmpty());
    pixelLabel_->setMinimumSize(0, 0);
    pixelLabel_->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    pixelLabel_->setText(pixelTemplate_);
    const auto preferred = pixelLabel_->sizeHint() + QSize(4, 0);
    pixelLabel_->setFixedSize(
        std::min(std::max(0, bounds.width() - 24), preferred.width()),
        preferred.height());
    pixelLabel_->setText(pixelFullText_);
    pixelLabel_->setToolTip(pixelFullText_);
    pixelLabel_->move(bounds.right() - pixelLabel_->width() - 12,
                      bounds.bottom() - pixelLabel_->height() - 10);
    pixelLabel_->raise();
  }
  if (caption_) {
    const int available = bounds.width() - 36 -
                          (pixelLabel_->isVisible() ? pixelLabel_->width() : 0);
    caption_->setVisible(hasImage() && !caption_->text().isEmpty() &&
                         available > 20);
    caption_->setMaximumWidth(std::max(0, available));
    caption_->adjustSize();
    caption_->move(bounds.left() + 12,
                   bounds.bottom() - caption_->height() - 10);
    caption_->raise();
  }
  if (rangeContext_) {
    const auto* tools = findChild<QWidget*>("viewTools", Qt::FindDirectChildrenOnly);
    const int left = tools && tools->isVisible() ? tools->x() + tools->width() + 8 : bounds.left() + 12;
    const int available = bounds.right() - left - 70;
    rangeContext_->setText(QFontMetrics(rangeContext_->font()).elidedText(
        rangeContextText_, Qt::ElideRight, std::max(0, available - 18)));
    rangeContext_->setMaximumWidth(std::max(0, available));
    rangeContext_->adjustSize();
    rangeContext_->move(left, bounds.top() + 12);
    rangeContext_->setVisible(
        hasImage() && layers_[2] && !rangeContext_->text().isEmpty() &&
        available > 100 &&
        (contextAtRest_ || roiSelected_ || int(tool_) >= int(Tool::Rectangle)));
    rangeContext_->raise();
  }
}
void ImageView::scrollContentsBy(int dx, int dy) {
  QGraphicsView::scrollContentsBy(dx, dy);
  positionTools();
  updateComparisonClip();
}
void ImageView::resizeEvent(QResizeEvent* event) {
  // The viewport already has its new geometry when Qt delivers this event.
  // Read the old center in viewport coordinates before updating scroll ranges.
  const auto previous = event->oldSize().isValid() ? event->oldSize() : size();
  const auto center = mapToScene(QPoint((previous.width() - 1) / 2,
                                       (previous.height() - 1) / 2));
  QGraphicsView::resizeEvent(event);
  if (fit_)
    fitImage();
  else {
    ensureCanvas(center);
    centerOn(center);
    const auto residual = viewportTransform().map(center) - viewport()->rect().center();
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + qRound(residual.x()));
    verticalScrollBar()->setValue(verticalScrollBar()->value() + qRound(residual.y()));
  }
  positionTools();
  updateComparisonClip();
}
void ImageView::drawForeground(QPainter* p, const QRectF&) {
  p->save();
  if (linkedPixel_.x() >= 0 && linkedPixel_.y() >= 0) {
    p->setPen(QPen(QColor("#e9ad3e"), 0));
    const double r = 6 / std::max(.01, zoom());
    p->drawEllipse(linkedPixel_, r, r);
  }
  p->resetTransform();
  if (!hasImage()) {
    p->setPen(QColor(dark_ ? "#9eacc1" : "#748197"));
    p->drawText(viewport()->rect(), Qt::AlignCenter,
                emptyText_.isEmpty()
                    ? QStringLiteral("打开图像，开始视觉检测\n\nCtrl+I "
                                     "添加图像 · Ctrl+O 打开工程")
                    : emptyText_);
  } else {
    if (qualityVisible_ && layers_[3] && selectedResult_ >= 0 &&
        selectedResult_ < qualityResults_.size()) {
      const auto& fit = qualityResults_[selectedResult_];
      const double tolerance = fit.values.value("qualityTolerance", 1);
      for (int i = 0; i < fit.samples.size(); ++i) {
        auto point = viewportTransform().map(fit.samples[i]);
        if (!viewport()->rect().contains(point.toPoint())) continue;
        p->setPen(Qt::NoPen);
        p->setBrush(fit.residuals.value(i) > tolerance ? QColor("#eaa04a")
                                                       : QColor("#29b890"));
        p->drawEllipse(point, 2., 2.);
      }
    }
    if (referenceVisible_ && layers_[2]) {
      const auto origin = viewportTransform().map(referenceOrigin_);
      QTransform rotation;
      rotation.rotate(referenceAngle_);
      const auto x = origin + rotation.map(QPointF(30, 0));
      const auto y = origin + rotation.map(QPointF(0, 30));
      p->setPen(QPen(QColor("#d99642"), 1.6));
      p->drawLine(origin, x);
      p->drawLine(origin, y);
      p->drawText(x + QPointF(3, 3), "X");
      p->drawText(y + QPointF(3, 3), "Y");
      p->drawEllipse(origin, 3, 3);
    }
    if (compare_) {
      const auto bounds = mapFromScene(image_->boundingRect())
                              .boundingRect()
                              .intersected(viewport()->rect());
      const double x = compareViewportX();
      const double y = bounds.center().y();
      p->setPen(QPen(QColor(dark_ ? "#aab6ff" : "#6575dd"), 2));
      p->drawLine(QPointF(x, bounds.top()), QPointF(x, bounds.bottom()));
      p->setBrush(QColor(dark_ ? "#26334b" : "#ffffff"));
      p->drawRoundedRect(QRectF(x - 13, y - 21, 26, 42), 10, 10);
      p->drawLine(QPointF(x - 4, y - 8), QPointF(x - 4, y + 8));
      p->drawLine(QPointF(x + 4, y - 8), QPointF(x + 4, y + 8));
      p->setPen(QColor(dark_ ? "#e3eaf4" : "#526177"));
      if (x - bounds.left() > 55)
        p->drawText(QRectF(x - 80, bounds.top() + 8, 70, 20), Qt::AlignRight,
                    comparisonLabel_);
      if (bounds.right() - x > 55)
        p->drawText(QRectF(x + 10, bounds.top() + 8, 70, 20), Qt::AlignLeft,
                    QStringLiteral("结果图像"));
    }
    if (editable_ && tool_ == Tool::Select && roiSelected_ && layers_[2]) {
      p->setPen(QPen(QColor(dark_ ? "#ffc465" : "#b57a22"), 1.4));
      p->setBrush(QColor(dark_ ? "#202632" : "#ffffff"));
      const auto handles = roiHandles();
      for (int i = 0; i < handles.size(); ++i)
        if ((roi_.kind != RoiKind::Circle && roi_.kind != RoiKind::Annulus) ||
            i % 2 || i == 8) {
          const auto point = mapFromScene(handles[i]);
          p->drawRect(QRectF(point.x() - 3, point.y() - 3, 6, 6));
        }
    }
  }
  p->restore();
}
}  // namespace workbench
