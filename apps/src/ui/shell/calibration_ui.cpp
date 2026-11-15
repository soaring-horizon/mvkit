// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include <QCryptographicHash>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include "operators/calibration_tools.hpp"
#include "ui/shell/main_window.hpp"
#include "ui/widgets/parameter_spin_box.hpp"
#include "ui/widgets/parameter_grid.hpp"
namespace workbench {
bool MainWindow::setCalibrationBoard(const QString& path) {
  if (busy() || (document_.parameters.algorithm != Algorithm::CalibrationDetect && !calibrationTool(document_.parameters.algorithm)))
    return false;
  return guard(
      QStringLiteral("标定板"),
      [&] {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
          throw std::runtime_error("标定板文件无法读取");
        const auto bytes=file.readAll();
        const auto hash =
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
                .toHex();
        EditGuard edit(*this, QStringLiteral("选择标定板"));
        document_.parameters.calibrationConfig = {
            {"boardPath", QFileInfo(path).absoluteFilePath()},
            {"boardSha", QString::fromLatin1(hash)},
            {"boardData",QString::fromLatin1(bytes.toBase64())}};
        parametersChanged();
      },
      true);
}
bool MainWindow::loadCalibrationArtifact(const QString& path) {
  if (busy() || (document_.parameters.algorithm != Algorithm::ImageRectify &&
                 document_.parameters.algorithm != Algorithm::PlaneMeasure))
    return false;
  return guard(
      QStringLiteral("加载标定"),
      [&] {
        const auto bytes = loadCalibrationFile(path);
        EditGuard edit(*this, QStringLiteral("加载标定文件"));
        if (nodeContext())
          editableRange().calibrationSource = -1;
        document_.parameters.calibrationName = QFileInfo(path).fileName();
        document_.parameters.calibrationArtifact = bytes;
        parametersChanged();
      },
      true);
}
void MainWindow::showCalibrationWizard() {
  if (busy() || images_.isEmpty()) return;
  const int input=activeInputNode();
  if(input>=0 && (workflow_[input].algorithm!=Algorithm::ImageInput || multiViewInput(workflow_[input])))return;
  QVector<Document> calibrationInputs;
  for(int row:trackSamples())if(images_[row].checked) {
    auto d=row==activeImage_?document_:images_[row].document;
    if(input>=0){const auto cell=resolveSourceData(d,workflow_[input].options.value("binding").toString());if(cell.frames.isEmpty())continue;d.image=cell.frames.first();d.imagePath=cell.paths.first();d.pixelKey=cell.keys.first();d.preview=makePreview(d.image).image;}
    calibrationInputs.append(d);
  }
  auto* dialog = new QDialog(this);
  dialog->setWindowTitle(QStringLiteral("相机标定"));
  dialog->setObjectName("cameraCalibrationDialog");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setModal(true);
  dialog->resize(1120, 760);
  auto* outer = new QVBoxLayout(dialog);
  outer->setContentsMargins(16, 16, 16, 16);
  outer->setSpacing(12);
  QJsonArray files;
  for (const auto& resource : calibrationInputs)
      files.append(QJsonObject{{"path", resource.imagePath},{"pixelKey", resource.pixelKey}});
  auto* heading = new QLabel(
      QStringLiteral(
          "单相机 · 已选 %1 张 · 至少 3 "
          "张不同倾角/"
          "位置，同一分辨率。成功检测的观测用于求解；失败帧会单独列出。")
          .arg(files.size()),
      dialog);
  heading->setWordWrap(true);
  outer->addWidget(heading);
  auto* split = new QSplitter(Qt::Horizontal, dialog);
  auto* preview = new ImageView(split);
  preview->setObjectName("calibrationPreview");
  preview->setRoiEditingEnabled(false);
  preview->setEmptyText(QStringLiteral("请在图像资源中加载对应的标定图"));
  for (const auto& resource : calibrationInputs) {
      preview->setImage(resource.preview);preview->setImageName(QFileInfo(resource.imagePath).fileName());break;
    }
  auto* settings = new QWidget(split);
  auto* options = new QVBoxLayout(settings);
  options->setContentsMargins(12, 0, 0, 0);
  options->setSpacing(10);
  auto* board =
      new QPushButton(QStringLiteral("选择标定板描述文件…"), settings);
  board->setObjectName("calibrationBoardFile");
  options->addWidget(board);
  auto* boardLabel = new QLabel(
      QStringLiteral(".cpd / .descr · 使用与图像对应的物理尺寸和布局"),
      settings);
  boardLabel->setWordWrap(true);
  options->addWidget(boardLabel);
  auto descriptor =
      std::make_shared<QJsonObject>(document_.parameters.calibrationConfig);
  if (!descriptor->value("boardPath").toString().isEmpty())
    boardLabel->setText(
        QFileInfo(descriptor->value("boardPath").toString()).fileName());
  connect(
      board, &QPushButton::clicked, dialog, [dialog, boardLabel, descriptor] {
        const auto path = QFileDialog::getOpenFileName(
            dialog, QStringLiteral("标定板描述"), {},
            QStringLiteral("标定板 (*.cpd *.descr)"));
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
          boardLabel->setText(QStringLiteral("文件无法读取"));
          return;
        }
        *descriptor = {
            {"boardPath", path},
            {"boardSha",
             QString::fromLatin1(QCryptographicHash::hash(
                                     file.readAll(), QCryptographicHash::Sha256)
                                     .toHex())}};
        boardLabel->setText(QFileInfo(path).fileName());
      });
  auto* grid = new ParameterGrid(settings);
  auto numeric = [&](const char* name, const QString& label, double low,
                     double high, double value, int decimals) {
    auto* box = new ParameterDoubleSpinBox(grid);
    box->setObjectName(name);
    box->setRange(low, high);
    box->setDecimals(decimals);
    box->setValue(value);
    box->setKeyboardTracking(false);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    grid->addField(label, box);
    return box;
  };
  auto* focus =
      numeric("calibrationFocus", QStringLiteral("初始焦距 / mm"), .1, 500,
              document_.parameters.options.value("focusMM").toDouble(12), 3);
  auto* pitch =
      numeric("calibrationPitch", QStringLiteral("像元尺寸 / µm"), .1, 100,
              document_.parameters.options.value("pitchUM").toDouble(3.45), 3);
  options->addWidget(grid);
  auto* note =
      new QLabel(QStringLiteral("当前支持 area_scan_division，固定给定的 X/Y "
                                "像元尺寸。相机参数使用米制；世界平面为标定板 "
                                "Z="
                                "0。点击成功观测可选择保存的测量平面，只有与工"
                                "件共面的位姿才适用于平面测量。"),
                 settings);
  note->setWordWrap(true);
  options->addWidget(note);
  auto* solve = new QPushButton(QStringLiteral("检测并标定"), settings);
  solve->setObjectName("solveCalibration");
  solve->setProperty("primary", true);
  options->addWidget(solve);
  auto* status = new QLabel(QStringLiteral("准备就绪"), settings);
  status->setObjectName("calibrationStatus");
  status->setWordWrap(true);
  options->addWidget(status);
  options->addStretch();
  split->addWidget(preview);
  split->addWidget(settings);
  split->setSizes({640, 380});
  outer->addWidget(split, 1);
  auto* table = new QTableWidget(0, 4, dialog);
  table->setObjectName("calibrationViews");
  table->setHorizontalHeaderLabels(
      {QStringLiteral("图像"), QStringLiteral("检测点数"),
       QStringLiteral("重投影 RMS / px"), QStringLiteral("状态 / 原因")});
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->verticalHeader()->hide();
  table->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  table->setMaximumHeight(230);
  outer->addWidget(table);
  auto* actions = new QHBoxLayout;
  auto* load = new QPushButton(QStringLiteral("加载已有标定…"), dialog);
  auto* save = new QPushButton(QStringLiteral("保存标定文件…"), dialog);
  save->setObjectName("saveCameraCalibration");
  save->setEnabled(false);
  auto* cancel = new QPushButton(QStringLiteral("关闭"), dialog);
  actions->addWidget(load);
  actions->addStretch();
  actions->addWidget(save);
  actions->addWidget(cancel);
  outer->addLayout(actions);
  auto model = std::make_shared<QJsonObject>();
  auto* service = new ExecutionService(dialog);
  auto showView = [calibrationInputs, model, table, preview, status](int row) {
    const auto views = model->value("views").toArray();
    if (row < 0 || row >= views.size()) return;
    const auto view = views[row].toObject();
    bool found = false;
    for (const auto& resource : calibrationInputs)
      if (resource.imagePath == view["path"].toString() &&
          resource.pixelKey == view["pixelKey"].toString()) {
        found = true;
        preview->setImage(resource.preview);
        preview->setImageName(
            QFileInfo(resource.imagePath).fileName());
        break;
      }
    if (!found) preview->setImage({});
    QVector<ResultObject> points;
    const auto rows = view["rows"].toArray(), cols = view["columns"].toArray();
    for (int i = 0; found && i < rows.size(); ++i) {
      ResultObject p;
      p.type = "Point";
      p.center = {cols[i].toDouble(), rows[i].toDouble()};
      p.overlay.moveTo(p.center + QPointF(-3, 0));
      p.overlay.lineTo(p.center + QPointF(3, 0));
      p.overlay.moveTo(p.center + QPointF(0, -3));
      p.overlay.lineTo(p.center + QPointF(0, 3));
      points.append(p);
    }
    preview->setResults(points);
    if (view["status"] == "success") {
      (*model)["worldPose"] = view["pose"];
      (*model)["planeView"] = view["index"];
      status->setText(QStringLiteral("总 RMS %1 px · 世界平面：%2")
                          .arg(model->value("rms").toDouble(), 0, 'f', 4)
                          .arg(QFileInfo(view["path"].toString()).fileName()));
    }
  };
  connect(table, &QTableWidget::cellClicked, dialog,
          [showView](int row, int) { showView(row); });
  auto install = [model, table, status, save, showView](const QByteArray& bytes,
                                                        const QString& error) {
    *model = QJsonDocument::fromJson(bytes).object();
    const auto views = model->value("views").toArray();
    table->setRowCount(views.size());
    int first = -1;
    const int savedPlane = model->value("planeView").toInt(-1);
    for (int i = 0; i < views.size(); ++i) {
      const auto v = views[i].toObject();
      if (v["status"] == "success" &&
          (first < 0 || v["index"].toInt(-1) == savedPlane))
        first = i;
      QStringList text{QFileInfo(v["path"].toString()).fileName(),
                       QString::number(v["rows"].toArray().size()),
                       v.contains("rms")
                           ? QString::number(v["rms"].toDouble(), 'f', 4)
                           : QString(),
                       v["status"] == "success" ? QStringLiteral("成功")
                                                : v["error"].toString()};
      for (int j = 0; j < 4; ++j) {
        auto* cell = new QTableWidgetItem(text[j]);
        cell->setToolTip(text[j]);
        table->setItem(i, j, cell);
      }
    }
    save->setEnabled(error.isEmpty() && model->value("valid").toBool());
    if (!error.isEmpty())
      status->setText(error);
    else if (first >= 0) {
      table->selectRow(first);
      showView(first);
    } else
      status->setText(QStringLiteral("已加载相机参数"));
  };
  connect(service, &ExecutionService::completed, dialog,
          [=](quint64, const Result& result) {
            solve->setEnabled(true);
            board->setEnabled(true);
            grid->setEnabled(true);
            load->setEnabled(true);
            install(result.calibrationResult, result.error);
          });
  connect(solve, &QPushButton::clicked, dialog, [=] {
    if (files.size() < 3 || files.size() > 64 || descriptor->isEmpty()) {
      status->setText(
          QStringLiteral("请选择描述文件，并在图像资源中选中 3–64 张标定图"));
      return;
    }
    Document job;
    job.calibrationJob = *descriptor;
    job.calibrationJob["images"] = files;
    job.calibrationJob["focusMM"] = focus->value();
    job.calibrationJob["pitchUM"] = pitch->value();
    if (service->start(job, 1)) {
      *model = QJsonObject{};
      table->setRowCount(0);
      preview->setResults({});
      solve->setEnabled(false);
      board->setEnabled(false);
      grid->setEnabled(false);
      save->setEnabled(false);
      load->setEnabled(false);
      status->setText(QStringLiteral("正在检测与求解…关闭可取消任务"));
    }
  });
  connect(save, &QPushButton::clicked, dialog, [=] {
    const auto path =
        QFileDialog::getSaveFileName(dialog, QStringLiteral("保存标定"), {},
                                     QStringLiteral("Studio 标定 (*.mvcalib)"));
    if (path.isEmpty()) return;
    try {
      saveCalibrationFile(QJsonDocument(*model).toJson(QJsonDocument::Compact),
                          path);
      status->setText(QStringLiteral("已保存：") + path);
    } catch (const std::exception& e) {
      status->setText(QString::fromUtf8(e.what()));
    }
  });
  connect(load, &QPushButton::clicked, dialog, [=] {
    const auto path =
        QFileDialog::getOpenFileName(dialog, QStringLiteral("加载标定"), {},
                                     QStringLiteral("Studio 标定 (*.mvcalib)"));
    if (path.isEmpty()) return;
    try {
      install(loadCalibrationFile(path), {});
    } catch (const std::exception& e) {
      status->setText(QString::fromUtf8(e.what()));
    }
  });
  connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
  connect(dialog, &QDialog::finished, service, &ExecutionService::cancel);
  if (!document_.parameters.calibrationArtifact.isEmpty()) {
    try {
      const auto& bytes = document_.parameters.calibrationArtifact;
      readCalibrationArtifact(bytes);
      install(bytes, {});
    } catch (const std::exception& e) {
      status->setText(QString::fromUtf8(e.what()));
    }
  }
  dialog->show();
}
}  // namespace workbench
