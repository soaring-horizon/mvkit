// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/shell/main_window.hpp"
#include "ui/widgets/parameter_spin_box.hpp"
#include "model/spatial_data.hpp"
#include <QCollator>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QAction>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <algorithm>

namespace workbench {
bool MainWindow::addStructuredSequence(const QStringList& paths, const QString& model) {
  if (busy()) return false;
  if(model.isEmpty() && activeInputNode()>=0 && workflow_[activeInputNode()].algorithm==Algorithm::SequenceInput)return guard(QStringLiteral("添加序列输入"),[&]{SourceData cell;loadResources(QStringLiteral("读取图像序列…"),[&](const std::atomic_bool& cancelled){cell=loadSourceData(paths,true,&cancelled);});if(!importSourceSamples({cell},{QFileInfo(paths.first()).absoluteDir().dirName()}))throw std::runtime_error("未添加输入数据");},true);
  return guard(QStringLiteral("导入结构光序列"), [&] {
    Document d;
    loadResources(QStringLiteral("读取有序曝光…"),[&d,paths,model](const std::atomic_bool& cancelled){d=loadStructuredSequence(paths,model,&cancelled);});
    d.parameters = workbenchParameters_;
    EditGuard edit(*this, QStringLiteral("导入结构光序列"));
    storeActive();
    images_.append({d.captureId, std::move(d), {}, 0, true});
    activateImage(images_.size() - 1);
    view2d_->setChecked(true);
    view3d_->setChecked(true);
    updateViews();
    setDirty(true);
  });
}
void MainWindow::showStructuredImport() {
  if (busy() || activeInputNode()<0 || workflow_[activeInputNode()].algorithm!=Algorithm::SequenceInput) return;
  auto* dialog = new QDialog(this);
  dialog->setObjectName("structuredSequenceImport");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setModal(true);
  dialog->setWindowTitle(QStringLiteral("添加序列输入"));
  dialog->resize(850, 580);
  auto* layout = new QVBoxLayout(dialog);
  auto* help = new QLabel(QStringLiteral("一组有序图像作为一个样本，首帧用作参考图。单目标定：同一相机的不同标定板姿态放在一条序列中。结构光：每个板姿态的完整曝光单独作为一条序列，按投影顺序排列；模型在后续节点配置。拖动列表可调整顺序。"), dialog);
  help->setWordWrap(true); layout->addWidget(help);
  auto* row = new QHBoxLayout;
  auto* chooseFrames = new QPushButton(QStringLiteral("选择序列图像…"), dialog);
  chooseFrames->setObjectName("chooseStructuredFrames");
  auto* folders=new QPushButton(QStringLiteral("批量文件夹…"),dialog);folders->setObjectName("importSequenceFolders");
  row->addWidget(chooseFrames);row->addWidget(folders);row->addStretch();
  layout->addLayout(row);
  auto* body = new QHBoxLayout;
  auto* list = new QListWidget(dialog);
  list->setObjectName("structuredExposureList");
  list->setDragDropMode(QAbstractItemView::InternalMove);
  list->setSelectionMode(QAbstractItemView::SingleSelection);
  auto* preview = new QLabel(dialog);
  preview->setMinimumSize(260, 200); preview->setAlignment(Qt::AlignCenter);
  body->addWidget(list, 3); body->addWidget(preview, 2);
  layout->addLayout(body, 1);
  auto* status = new QLabel(dialog); status->setWordWrap(true); layout->addWidget(status);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("导入整组"));
  buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
  layout->addWidget(buttons);
  auto refresh = [=] {
    buttons->button(QDialogButtonBox::Ok)->setEnabled(list->count() > 0 && list->count() <= 64 && !busy());
    status->setText(QStringLiteral("已选 %1 帧 · 请核对帧顺序和图像规格；结构光模型在后续解码节点配置").arg(list->count()));
    for (int i = 0; i < list->count(); ++i)
      list->item(i)->setText(QString("%1   %2").arg(i, 3, 10, QChar('0')).arg(QFileInfo(list->item(i)->data(Qt::UserRole).toString()).fileName()));
  };
  connect(chooseFrames, &QPushButton::clicked, dialog, [=] {
    auto files = QFileDialog::getOpenFileNames(dialog, QStringLiteral("选择一组有序图像"), {}, QStringLiteral("图像 (*.png *.bmp *.tif *.tiff)"));
    if (files.isEmpty()) return;
    if (files.size() > 64) { status->setText(QStringLiteral("每套最多 64 帧")); return; }
    QCollator order; order.setNumericMode(true);
    std::sort(files.begin(), files.end(), [&](const QString& a, const QString& b) { return order.compare(a, b) < 0; });
    list->clear();
    for (const auto& path : files) {
      auto* item = new QListWidgetItem(QFileInfo(path).fileName(), list);
      item->setData(Qt::UserRole, path); item->setToolTip(path);
    }
    refresh(); list->setCurrentRow(0);
  });
  connect(list->model(), &QAbstractItemModel::rowsMoved, dialog, refresh);
  connect(list, &QListWidget::currentRowChanged, dialog, [=](int row) {
    if (row < 0) return;
    preview->setPixmap(QPixmap(list->item(row)->data(Qt::UserRole).toString()).scaled(320, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  });
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
    QStringList files;
    for (int i = 0; i < list->count(); ++i) files.append(list->item(i)->data(Qt::UserRole).toString());
    bool installed=false;
    guard(QStringLiteral("添加序列输入"),[&]{SourceData cell;loadResources(QStringLiteral("读取图像序列…"),[&](const std::atomic_bool& cancelled){cell=loadSourceData(files,true,&cancelled);});installed=importSourceSamples({cell},{QFileInfo(files.first()).absoluteDir().dirName()});},true);
    if(installed)dialog->accept();
    else status->setText(lastError());
  });
  connect(folders,&QPushButton::clicked,dialog,[this,dialog,status]{
    const auto parent=QFileDialog::getExistingDirectory(dialog,QStringLiteral("选择包含序列子文件夹的目录"));if(parent.isEmpty())return;
    QStringList names;QVector<QStringList> groups;
    QCollator order;order.setNumericMode(true);
    auto directories=QDir(parent).entryList(QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks);
    std::sort(directories.begin(),directories.end(),[&](const QString& a,const QString& b){return order.compare(a,b)<0;});
    for(const auto& name:directories){QDir dir(QDir(parent).filePath(name));auto files=dir.entryList({"*.png","*.bmp","*.tif","*.tiff","*.jpg","*.jpeg"},QDir::Files|QDir::NoSymLinks);if(files.isEmpty())continue;
      std::sort(files.begin(),files.end(),[&](const QString& a,const QString& b){return order.compare(a,b)<0;});QStringList paths;for(const auto& file:files)paths.append(dir.absoluteFilePath(file));names.append(name);groups.append(paths);}
    if(groups.isEmpty()){status->setText(QStringLiteral("子文件夹中没有图像。每个子文件夹应是一组完整序列。"));return;}
    QDialog plan(dialog);plan.setObjectName("sequenceFolderPlan");plan.setWindowTitle(QStringLiteral("确认批量序列"));plan.resize(700,420);QVBoxLayout layout(&plan);
    QLabel guide(QStringLiteral("每个子文件夹作为一个样本，内部按文件名自然排序。请核对帧数和顺序；可在资源管理中继续调整。"));guide.setWordWrap(true);layout.addWidget(&guide);
    QTableWidget table(groups.size(),3);table.setHorizontalHeaderLabels({QStringLiteral("导入"),QStringLiteral("序列文件夹"),QStringLiteral("帧数")});table.setEditTriggers(QAbstractItemView::NoEditTriggers);table.horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);table.setColumnWidth(0,56);table.setColumnWidth(2,80);layout.addWidget(&table);
    for(int row=0;row<groups.size();++row){auto* check=new QTableWidgetItem;check->setFlags(Qt::ItemIsUserCheckable|Qt::ItemIsEnabled);check->setCheckState(groups[row].size()<=64?Qt::Checked:Qt::Unchecked);table.setItem(row,0,check);auto* name=new QTableWidgetItem(names[row]);name->setToolTip(groups[row].join('\n'));table.setItem(row,1,name);table.setItem(row,2,new QTableWidgetItem(QString::number(groups[row].size())));}
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons.button(QDialogButtonBox::Ok)->setText(QStringLiteral("添加序列"));buttons.button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));layout.addWidget(&buttons);connect(&buttons,&QDialogButtonBox::accepted,&plan,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&plan,&QDialog::reject);
    if(plan.exec()!=QDialog::Accepted)return;QVector<QStringList> selected;QStringList selectedNames;for(int row=0;row<groups.size();++row)if(table.item(row,0)->checkState()==Qt::Checked){selected.append(groups[row]);selectedNames.append(names[row]);}
    if(selected.isEmpty()){status->setText(QStringLiteral("未选择序列，数据保持不变。"));return;}
    bool installed=false;guard(QStringLiteral("批量添加序列"),[&]{QVector<SourceData> cells;
      loadResources(QStringLiteral("读取批量序列…"),[&](const std::atomic_bool& cancelled){for(int i=0;i<selected.size();++i){if(cancelled.load())return;try{cells.append(loadSourceData(selected[i],true,&cancelled));}catch(const std::exception& e){throw std::runtime_error((selectedNames[i]+QStringLiteral("：")+QString::fromUtf8(e.what())).toUtf8().constData());}}});
      installed=importSourceSamples(cells,selectedNames);
    },true);
    if(installed)dialog->accept();else status->setText(lastError());
  });
  refresh(); dialog->show();
}
void MainWindow::showStructuredGenerator() {
  if (busy()) return;
  auto* dialog = new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setModal(true);
  dialog->setObjectName("structuredPatternGenerator");
  dialog->setWindowTitle(QStringLiteral("生成结构光投影图案")); dialog->resize(520, 390);
  auto* layout = new QVBoxLayout(dialog);
  auto* help = new QLabel(QStringLiteral("生成投影图案、顺序清单及 .mvslm 模型。图案用于投影，算法输入应为同步采集的相机曝光。可选加载相机/投影仪配置 JSON，将已有标定写入模型。"), dialog);
  help->setWordWrap(true); layout->addWidget(help);
  auto* form = new QFormLayout;
  auto field = [&](const QString& title, int lo, int hi, int value) {
    auto* spin = new ParameterSpinBox(dialog); spin->setRange(lo, hi); spin->setValue(value); form->addRow(title, spin); return spin;
  };
  auto* width = field(QStringLiteral("投影宽度 / px"), 8, 4096, 1024);
  auto* height = field(QStringLiteral("投影高度 / px"), 8, 4096, 768);
  auto* stripe = new QComboBox(dialog); stripe->addItems({"4", "8", "16", "32"}); stripe->setCurrentIndex(1);
  form->addRow(QStringLiteral("最小条纹宽 / px"), stripe);
  auto* type = new QComboBox(dialog); type->addItems({QStringLiteral("格雷码 + 四步相移"), QStringLiteral("格雷码")});
  form->addRow(QStringLiteral("编码方式"), type);
  auto* orientation = new QComboBox(dialog); orientation->addItems({QStringLiteral("双向"), QStringLiteral("纵条纹 · 投影列坐标"), QStringLiteral("横条纹 · 投影行坐标")});
  form->addRow(QStringLiteral("投影方向"), orientation);
  layout->addLayout(form);
  auto profile = std::make_shared<QJsonObject>();
  auto* calibration = new QPushButton(QStringLiteral("加载相机/投影仪配置（可选）…"), dialog); layout->addWidget(calibration);
  auto* status = new QLabel(dialog); status->setWordWrap(true); status->setText(QStringLiteral("未加载标定：生成的模型可用于解码。")); layout->addWidget(status);
  connect(calibration, &QPushButton::clicked, dialog, [=] {
    const auto path = QFileDialog::getOpenFileName(dialog, QStringLiteral("已有相机/投影仪标定"), {}, "MVKit (*.mvslconfig *.json)");
    if (path.isEmpty()) return;
    QFile f(path);
    QJsonParseError error;
    if (!f.open(QIODevice::ReadOnly) || f.size() > 65536) { status->setText(QStringLiteral("无法读取配置")); return; }
    const auto config = QJsonDocument::fromJson(f.readAll(), &error).object();
    if (error.error != QJsonParseError::NoError || config["format"] != "mvkit-camera-projector" || config["version"] != 1 || !config.contains("camera")) {
      status->setText(QStringLiteral("请选择 mvkit-camera-projector v1 配置；格式见结构光入门文档")); return;
    }
    *profile = config;
    const auto projector = config["projector"].toArray();
    if (projector.size() >= 9) {
      width->setValue(projector[projector.size() - 2].toInt());
      height->setValue(projector[projector.size() - 1].toInt());
    }
    status->setText(QStringLiteral("已加载：") + QFileInfo(path).fileName());
  });
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dialog);
  buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("生成并导出…")); buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("关闭")); layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
    const auto parent = QFileDialog::getExistingDirectory(dialog, QStringLiteral("选择输出父目录"));
    if (parent.isEmpty()) return;
    guard(QStringLiteral("生成结构光图案"), [&] {
      auto config = *profile;
      config["width"] = width->value(); config["height"] = height->value(); config["stripe"] = stripe->currentText().toInt();
      config["type"] = type->currentIndex() == 0 ? "gray_code_and_phase_shift" : "gray_code";
      config["orientation"] = QStringList{"both", "vertical", "horizontal"}[orientation->currentIndex()];
      status->setText(QStringLiteral("已保存：") + generateStructuredPatterns(config, parent));
    });
  });
  dialog->show();
}
}  // namespace workbench
