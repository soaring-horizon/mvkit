// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/dialogs/sequence_view.hpp"
#include "ui/shell/main_window.hpp"
#include "ui/style/appearance.hpp"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include "ui/widgets/frame_spin_box.hpp"
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>

namespace workbench {
ExposureStatistics exposureStatistics(const mv::MvImage& image) {
  if (image.CountObj() != 1 || image.Channels() != 1 || image.Type() != mv::MvPixelType::UInt8)
    throw std::runtime_error("曝光统计需要单通道 byte 图像");
  ExposureStatistics stats;
  const auto* pixels = static_cast<const unsigned char*>(image.Data());
  const auto stride = image.Stride();
  const int width = int(image.Width()), height = int(image.Height());
  quint64 sum = 0;
  for (int row = 0; row < height; ++row)
    for (int col = 0; col < width; ++col) {
      const auto value = pixels[row * stride + col]; ++stats.histogram[value]; sum += value;
    }
  stats.pixels = quint64(width) * height;
  stats.mean = stats.pixels ? double(sum) / stats.pixels : 0;
  for (int value = 0; value < 256; ++value) if (stats.histogram[value]) { stats.minimum = value; break; }
  for (int value = 255; value >= 0; --value) if (stats.histogram[value]) { stats.maximum = value; break; }
  return stats;
}
namespace {
class Histogram final : public QWidget {
 public:
  explicit Histogram(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(110); setMaximumHeight(155); setAccessibleName(QStringLiteral("图像灰度直方图"));
  }
  void setStatistics(const ExposureStatistics& stats) { stats_ = stats; update(); }
 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const QRectF plot = rect().adjusted(12, 8, -12, -24);
    p.fillRect(plot, palette().color(QPalette::Base));
    const auto peak = *std::max_element(stats_.histogram.begin(), stats_.histogram.end());
    QPainterPath path;
    path.moveTo(plot.left(), plot.bottom());
    for (int i = 0; i < 256; ++i) {
      const double x = plot.left() + i * plot.width() / 255;
      const double y = peak ? plot.bottom() - double(stats_.histogram[i]) / peak * plot.height() : plot.bottom();
      path.lineTo(x, y);
    }
    path.lineTo(plot.right(), plot.bottom()); path.closeSubpath();
    auto fill = palette().color(QPalette::Highlight); fill.setAlpha(90);
    p.fillPath(path, fill); p.setPen(palette().color(QPalette::Highlight)); p.drawPath(path);
    p.setPen(palette().color(QPalette::Text));
    p.drawText(QRectF(plot.left(), plot.bottom() + 3, plot.width() / 2, 20), Qt::AlignLeft, "0");
    p.drawText(QRectF(plot.center().x(), plot.bottom() + 3, plot.width() / 2, 20), Qt::AlignRight, "255");
  }
 private:
  ExposureStatistics stats_;
};
}  // namespace
void MainWindow::showStructuredSequence(int index, QWidget* browserParent) {
  if (index < 0) index = activeImage_;
  if (index < 0 || index >= images_.size()) return;
  auto snapshot = index==activeImage_ ? document_ : images_[index].document;
  QString binding="sequence";
  const bool selectedSequence=nodeContext() && selectedStep_>=0 && selectedStep_<workflow_.size() && workflow_[selectedStep_].algorithm==Algorithm::SequenceInput;
  if(selectedSequence)binding=workflow_[selectedStep_].options.value("binding").toString();
  auto cell=resolveSourceData(snapshot,binding);
  if(cell.frames.isEmpty() && !selectedSequence && !snapshot.sourceInputs.contains(binding))
    for(auto it=snapshot.sourceInputs.begin();it!=snapshot.sourceInputs.end();++it)
      if(it->sequence&&!it->frames.isEmpty()){cell=it.value();break;}
  if(!cell.sequence || cell.frames.isEmpty()) return;
  snapshot.structuredFrames=cell.frames;snapshot.structuredPaths=cell.paths;snapshot.structuredKeys=cell.keys;
  snapshot.image=cell.frames.first();snapshot.imagePath=cell.paths.first();
  if(selectedSequence)snapshot.captureName=nodeNames().value(selectedStep_);
  showSequenceSnapshot(snapshot,browserParent);
}
void MainWindow::showSequenceSnapshot(const Document& snapshot,QWidget* browserParent) {
  // Own read-only frame vectors in the dialog callbacks, including custom input columns.
  auto* dialog = new QDialog(browserParent ? browserParent : this);
  dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setObjectName("structuredSequenceBrowser");
  dialog->setWindowTitle(QStringLiteral("图像序列 · %1").arg(snapshot.captureName));
  dialog->resize(1050, 720);
  auto* layout = new QVBoxLayout(dialog);
  auto* title = new QLabel(QStringLiteral("%1 · %2 帧 · %3×%4 · 原始 byte %5")
      .arg(snapshot.captureName).arg(snapshot.structuredFrames.size()).arg(snapshot.image.Width()).arg(snapshot.image.Height()).arg(snapshot.structuredFrames.first().Channels()==3?QStringLiteral("RGB"):QStringLiteral("灰度")), dialog);
  title->setTextFormat(Qt::PlainText);
  title->setObjectName("sectionTitle"); layout->addWidget(title);
  auto* note = new QLabel(QStringLiteral("只读浏览工程内冻结图像；上下键或左右键切换，支持缩放和平移。不会切换当前处理图像，也不会更改序列顺序或运行算法。"), dialog);
  note->setWordWrap(true); layout->addWidget(note);
  auto* split = new QSplitter(Qt::Horizontal, dialog);
  auto* list = new QListWidget(split); list->setObjectName("sequenceFrameList");
  for (int i = 0; i < snapshot.structuredFrames.size(); ++i) {
    auto* item = new QListWidgetItem(QString("%1   %2").arg(i, 3, 10, QChar('0')).arg(QFileInfo(snapshot.structuredPaths[i]).fileName()), list);
    item->setToolTip(snapshot.structuredPaths[i] + "\nSHA-256 " + snapshot.structuredKeys[i]);
  }
  auto* pane = new QWidget(split); auto* box = new QVBoxLayout(pane); box->setContentsMargins(0, 0, 0, 0);
  auto* navigation = new QHBoxLayout;
  auto* previous = new QPushButton(QStringLiteral("上一帧"), pane); previous->setObjectName("sequencePrevious");
  auto* next = new QPushButton(QStringLiteral("下一帧"), pane); next->setObjectName("sequenceNext");
  auto* ordinal = new FrameSpinBox(pane); ordinal->setObjectName("sequenceFrameNumber"); ordinal->setRange(1, snapshot.structuredFrames.size());
  ordinal->setSuffix(QStringLiteral(" / %1 帧").arg(snapshot.structuredFrames.size())); ordinal->setKeyboardTracking(false);
  navigation->addWidget(previous); navigation->addWidget(ordinal); navigation->addWidget(next); navigation->addStretch(); box->addLayout(navigation);
  auto* view = new ImageView(pane); view->setObjectName("sequenceFrameView"); view->setRoiEditingEnabled(false);
  view->setTool(ImageView::Tool::Pan);
  for (const auto& tool : QVector<QPair<QString, QString>>{{"pan", QStringLiteral("平移画布")}, {"fit", QStringLiteral("适应画布")}, {"actual", QStringLiteral("原始大小 1:1")}}) {
    auto* button = new QToolButton(pane); button->setIcon(studioIcon(tool.first));
    button->setToolTip(tool.second); button->setAccessibleName(tool.second); navigation->addWidget(button);
    if (tool.first == "pan") {
      button->setCheckable(true); button->setChecked(true);
      connect(button, &QToolButton::toggled, view, [=](bool checked) { view->setTool(checked ? ImageView::Tool::Pan : ImageView::Tool::Select); });
    } else if (tool.first == "fit") connect(button, &QToolButton::clicked, view, &ImageView::fitImage);
    else connect(button, &QToolButton::clicked, view, &ImageView::actualSize);
  }
  box->addWidget(view, 1);
  auto* summary = new QLabel(pane); summary->setObjectName("sequenceFrameStatistics"); summary->setWordWrap(true); box->addWidget(summary);
  auto* histogram = new Histogram(pane); histogram->setObjectName("sequenceHistogram"); box->addWidget(histogram);
  split->setSizes({260, 790}); layout->addWidget(split, 1);
  auto* close = new QDialogButtonBox(QDialogButtonBox::Close, dialog); close->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
  connect(close, &QDialogButtonBox::rejected, dialog, &QDialog::reject); layout->addWidget(close);
  auto cache = std::make_shared<QMap<int, ExposureStatistics>>();
  connect(list, &QListWidget::currentRowChanged, dialog, [=](int row) {
    if (row < 0 || row >= snapshot.structuredFrames.size()) return;
    const QSignalBlocker block(ordinal); ordinal->setValue(row + 1);
    const auto& image = snapshot.structuredFrames[row];
    view->setImage(makePreview(image).image, view->property("sequenceRow").isValid());
    view->setImageName(QStringLiteral("%1 · 帧序号 %2").arg(QFileInfo(snapshot.structuredPaths[row]).fileName()).arg(row, 3, 10, QChar('0')));
    view->setPixelText(QStringLiteral("Row — · Col — · 原始图像"));
    view->setProperty("sequenceRow", row);
    previous->setEnabled(row > 0); next->setEnabled(row + 1 < snapshot.structuredFrames.size());
    if (!cache->contains(row)) (*cache)[row] = exposureStatistics(image.Channels()==3?image.Rgb1ToGray():image);
    const auto stats = cache->value(row); histogram->setStatistics(stats);
    summary->setText(QStringLiteral("均值 %1 · 灰度 %2–%3 · 0 值 %4% · 255 值 %5% · %6 个像素")
        .arg(stats.mean, 0, 'f', 2).arg(stats.minimum).arg(stats.maximum)
        .arg(100. * stats.histogram[0] / stats.pixels, 0, 'f', 2)
        .arg(100. * stats.histogram[255] / stats.pixels, 0, 'f', 2).arg(stats.pixels));
    summary->setToolTip((image.Channels()==3?QStringLiteral("RGB 图像的直方图与统计基于转灰度结果，原图保持彩色。\n"):QString())+QStringLiteral("0/255 比例仅描述像素分布；投影黑白归一化帧可正常包含大量极值，不能据此断言过曝或欠曝。"));
  });
  connect(ordinal, QOverload<int>::of(&QSpinBox::valueChanged), dialog, [=](int value) { list->setCurrentRow(value - 1); });
  connect(previous, &QPushButton::clicked, dialog, [=] { list->setCurrentRow(std::max(0, list->currentRow() - 1)); });
  connect(next, &QPushButton::clicked, dialog, [=] { list->setCurrentRow(std::min(list->count() - 1, list->currentRow() + 1)); });
  for (bool forward : {false, true}) {
    auto* key = new QShortcut(QKeySequence(forward ? Qt::Key_Right : Qt::Key_Left), dialog);
    connect(key, &QShortcut::activated, dialog, [=] { list->setCurrentRow(std::clamp(list->currentRow() + (forward ? 1 : -1), 0, list->count() - 1)); });
  }
  connect(view, &ImageView::cursorPixel, dialog, [=](int row, int col) {
    const int selected = list->currentRow(); if (selected < 0) return;
    const auto& image = snapshot.structuredFrames[selected];
    const bool inside = row >= 0 && col >= 0 && row < int(image.Height()) && col < int(image.Width());
    view->setPixelSample(row, col, inside ? pixelText(image, row, col) : QString(),
                         inside ? ImageView::PixelSource::Original : ImageView::PixelSource::None);
  });
  list->setCurrentRow(0); dialog->show();
}
}  // namespace workbench
