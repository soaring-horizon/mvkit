// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QGraphicsView>

#include "runtime/execution.hpp"
#include "model/image_preview.hpp"
class QGraphicsPixmapItem;
class QGraphicsPathItem;
class QLabel;
namespace workbench {
class ElidedLabel;


class ImageView final : public QGraphicsView {
  Q_OBJECT
 public:
  enum class Tool { Select, Pan, Rectangle, Circle, RotatedRectangle, Annulus };
  enum class Layer { Original, Processed, Roi, Elements };
  enum class PixelSource { None, Original, Processed, Input };
  explicit ImageView(QWidget* parent = nullptr);
  void setImage(const QImage& image, bool preserveView = false);
  void setQualityVisible(bool visible);
  void setLinkedPixel(int row, int column) {
    linkedPixel_ = {double(column), double(row)};
    setProperty("linkedRow", row);
    setProperty("linkedColumn", column);
    viewport()->update();
  }
  void clearLinkedPixel() {
    linkedPixel_ = {-1, -1};
    setProperty("linkedRow", -1);
    setProperty("linkedColumn", -1);
    viewport()->update();
  }
  bool qualityVisible() const { return qualityVisible_; }
  void setImageName(const QString& name);
  void setPixelText(const QString& text);
  void setPixelSample(int row, int column, const QString& values,
                      PixelSource source,
                      mv::MvPixelType type = mv::MvPixelType::UInt8);
  void setRoi(const Roi& roi);
  void setRangeOverlay(const QPainterPath& path);
  void setRangeContext(const QString& text, bool visibleAtRest,
                        const QPointF& origin = {}, double angle = 0,
                        bool frameVisible = false);
  void setTool(Tool tool);
  void setRoiEditingEnabled(bool enabled);
  void setResults(const QVector<ResultObject>& objects);
  void setOverlayVisible(bool visible);
  void setResultImage(const QImage& image);
  void setResultImage(const QImage& image, const QPainterPath& domain);
  void setLayerVisible(Layer layer, bool visible);
  bool layerVisible(Layer layer) const;
  void setCompare(bool enabled);
  void setComparisonLabel(const QString& label) { comparisonLabel_ = label; viewport()->update(); }
  bool compareEnabled() const { return compare_; }
  void setComparePosition(double position);
  double comparePosition() const { return comparePosition_; }
  double compareViewportX() const;
  PixelSource pixelSourceAt(int row, int column) const;
  bool hasImage() const { return !imageSize_.isEmpty(); }
  bool hasResultImage() const;
  bool hasElements() const { return !overlays_.isEmpty(); }
  QColor elementColor() const;
  QString elementIcon() const;
  QString elementDescription() const;
  void setEmptyText(const QString& text) {
    emptyText_ = text;
    viewport()->update();
  }
  void selectResult(int index);
  bool focusResult(int index);
  int selectedResult() const { return selectedResult_; }
  void fitImage();
  void actualSize();
  void zoomBy(double factor);
  double zoom() const { return transform().m11(); }
  Roi roi() const { return roi_; }
  bool roiSelected() const { return roiSelected_; }
  bool roiEditingEnabled() const { return editable_; }
  Tool tool() const { return tool_; }
 signals:
  void roiChanged(const workbench::Roi& roi);
  void cursorPixel(int row, int column);
  void pixelPicked(int row, int column);
  void zoomChanged(double scale);
  void displayStateChanged();
  void toolChanged();
  void resultPicked(int index);
  void resultActivated(int index);

 protected:
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void scrollContentsBy(int dx, int dy) override;
  void drawForeground(QPainter* painter, const QRectF& rect) override;
  void changeEvent(QEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;

 private:
  enum class Edit { None, Draw, Move, Resize };
  QPointF imagePoint(const QPoint& point) const;
  QPointF linkedPixel_{-1, -1};
  QVector<QPointF> roiHandles() const;
  int handleAt(const QPoint& point) const;
  bool roiHit(const QPoint& point) const;
  bool curtainHit(const QPoint& point) const;
  int resultAt(const QPoint& point) const;
  void updatePointer(const QPoint& point);
  void drawRoi();
  void updateTheme();
  void updateComparisonClip();
  void positionTools();
  void moveRoi(const QPointF& delta);
  void ensureCanvas(const QPointF& center);
  void zoomAt(double factor, const QPoint& anchor);
  QGraphicsScene* scene_ = nullptr;
  QGraphicsPixmapItem *image_ = nullptr, *resultImage_ = nullptr;
  QGraphicsPathItem* roiItem_ = nullptr;
  QGraphicsPathItem* rangeItem_ = nullptr;
  QLabel* caption_ = nullptr;
  ElidedLabel* pixelLabel_ = nullptr;
  QLabel* rangeContext_ = nullptr;
  QString rangeContextText_;
  QPointF referenceOrigin_;
  double referenceAngle_ = 0;
  bool contextAtRest_ = false, referenceVisible_ = false;
  int pendingResult_ = -1;
  QPoint resultPress_;
  QVector<QGraphicsPathItem*> overlays_;
  QVector<QString> resultTypes_;
  QVector<ResultObject> qualityResults_;
  bool qualityVisible_ = false;
  QPainterPath resultDomain_;
  QSize imageSize_;
  QString emptyText_, pixelTemplate_, pixelFullText_;
  QString comparisonLabel_ = QStringLiteral("原始图像");
  Roi roi_, previousRoi_;
  Tool tool_ = Tool::Select;
  Edit edit_ = Edit::None;
  QPointF anchor_;
  QPointF panAnchor_;
  int resizeHandle_ = -1, selectedResult_ = -1;
  bool panning_ = false, fit_ = true, compare_ = false,
       curtainDragging_ = false, roiSelected_ = false, editable_ = true,
       spacePan_ = false, dark_ = false;
  double comparePosition_ = .5;
  bool layers_[4] = {true, false, true, true};
};
}  // namespace workbench
