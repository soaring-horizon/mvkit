// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QComboBox>
#include <QSet>
#include <QTabWidget>
#include <QTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "io/delivery_report.hpp"
#include "model/sample_inputs.hpp"
#include "ui/shell/main_window.hpp"
#include "ui/views/workflow_view.hpp"
namespace workbench {
QVector<DeliveryRecord> MainWindow::deliveryRecords(bool selectedOnly) const {
  QVector<DeliveryRecord> records;
  for (const auto& image : images_) {
    if(selectedOnly && !image.checked)continue;
    DeliveryRecord r;
    r.imageId = image.id;
    r.imagePath = image.document.imagePath;
    r.capture = image.deliveryProvenance.isEmpty()?sampleProvenance(image.document,image.id):image.deliveryProvenance;
    r.image = image.document.preview;
    r.result = image.deliveryResult;
    r.utc = image.deliveryUtc;
    r.mode = image.deliveryWorkflow ? QStringLiteral("工作流")
                                    : QStringLiteral("基础算子");
    r.current =
        !image.deliveryUtc.isEmpty() &&
        image.deliveryVersion ==
            (image.deliveryWorkflow ? image.version : image.baseVersion);
    r.nodeIds = workflowIds_;
    r.nodeNames = nodeNames();
    records.append(std::move(r));
  }
  return records;
}
QString MainWindow::exportResults(const QString& directory, bool annotated, bool selectedOnly) {
  if (busy()) return {};
  QString path;
  guard(
      QStringLiteral("导出结果"),
      [&] {
        path = exportDelivery(deliveryRecords(selectedOnly), directory, annotated);
        log(QStringLiteral("结果已导出：") + path);
        setOperatorStatus(QStringLiteral("导出完成"), "ok", path);
      },
      true);
  return path;
}
void MainWindow::showBatchResults() {
  if (busy()) return;
  auto* dialog = new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setModal(true);
  dialog->setWindowTitle(QStringLiteral("批量结果"));
  dialog->setObjectName("batchResultsDialog");
  dialog->resize(960, 540);
  auto* layout = new QVBoxLayout(dialog);
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(12);
  const auto records = deliveryRecords();
  QSet<QString> selected;
  for(const auto& image:images_)if(image.checked)selected.insert(image.id);
  auto* scope=new QComboBox(dialog);scope->setObjectName("batchExportScope");
  scope->setAccessibleName(QStringLiteral("查看与导出范围"));
  scope->addItem(QStringLiteral("全部样本 · %1").arg(records.size()),false);
  scope->addItem(QStringLiteral("运行选择 · %1").arg(selected.size()),true);
  layout->addWidget(scope);
  int ok = 0, ng = 0, failed = 0, stale = 0, unrun = 0, unjudged = 0;
  for (const auto& r : records) {
    const auto state = deliveryState(r);
    ok += state == "OK";
    ng += state == "NG";
    failed += state == QStringLiteral("执行失败");
    stale += state == QStringLiteral("结果已过期");
    unrun += r.utc.isEmpty();
    unjudged += state == QStringLiteral("未判定");
  }
  auto* title = new QLabel(
      QStringLiteral(
          "%1 张 · OK %2 · NG %3 · 失败 %4 · 过期 %5 · 未运行 %6 · 未判定 %7")
          .arg(records.size())
          .arg(ok)
          .arg(ng)
          .arg(failed)
          .arg(stale)
          .arg(unrun)
          .arg(unjudged),
      dialog);
  layout->addWidget(title);
  auto* table = new QTableWidget(records.size(), 5, dialog);
  table->setObjectName("batchResultsTable");
  table->setHorizontalHeaderLabels(
      {QStringLiteral("图像"), QStringLiteral("检测"),
       QStringLiteral("算子 / ms"), QStringLiteral("总耗时 / ms"),
       QStringLiteral("错误 / 信息")});
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->verticalHeader()->hide();
  table->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
  for (int i = 0; i < records.size(); ++i) {
    const auto& r = records[i];
    QStringList cells{
        QFileInfo(r.imagePath).fileName(), deliveryState(r),
        r.current ? QString::number(r.result.algorithmMs, 'f', 2) : QString(),
        r.current ? QString::number(r.result.totalMs, 'f', 2) : QString(),
        deliveryExplanation(r)};
    for (int j = 0; j < cells.size(); ++j) {
      auto* cell = new QTableWidgetItem(cells[j]);
      cell->setToolTip(cells[j]);
      table->setItem(i, j, cell);
    }
  }
  connect(table,&QTableWidget::cellDoubleClicked,dialog,[this,dialog,records](int row,int){
    if(row<0||row>=records.size())return;
    const auto record=records[row];int sample=-1;
    for(int i=0;i<images_.size();++i)if(images_[i].id==record.imageId){sample=i;break;}
    if(sample<0)return;
    dialog->close();autoPreview_->stop();
    setWorkflowMode(record.mode==QStringLiteral("工作流"));
    activateImage(sample);
    if(record.current) {
      const int issue=deliveryIssueNode(record);
      const int node=workflowIds_.indexOf(record.nodeIds.value(issue));
      if(node>=0) { pinnedViewNode_.clear(); workflowView_->focusNode(node); refreshProperties(); }
    }
    if(workflowMode_)refreshNodeOutput();
    refreshContext();
  });
  layout->addWidget(table, 1);
  connect(scope,QOverload<int>::of(&QComboBox::currentIndexChanged),dialog,[scope,table,title,records,selected]{
    QMap<QString,int> counts;int total=0;
    for(int i=0;i<records.size();++i) {
      const bool show=!scope->currentData().toBool()||selected.contains(records[i].imageId);
      table->setRowHidden(i,!show);
      if(show){++total;++counts[deliveryState(records[i])];}
    }
    title->setText(QStringLiteral("%1 个样本 · OK %2 · NG %3 · 失败 %4 · 过期 %5 · 未运行 %6 · 未判定 %7")
      .arg(total).arg(counts["OK"]).arg(counts["NG"]).arg(counts[QStringLiteral("执行失败")])
      .arg(counts[QStringLiteral("结果已过期")]).arg(counts[QStringLiteral("未运行")]).arg(counts[QStringLiteral("未判定")]));
  });
  auto* note = new QLabel(
      QStringLiteral(
          "双击结果行回到样本及问题节点，不重新运行。查看与导出均使用上方所选范围，表格临时选择不影响范围。统计每个样本最后一次执行；失败优先于 "
          "NG。配置变更后的旧结果标为过期，不导出旧数值。CSV "
          "包含全部节点、目标、命名数值和错误；标注图保持原图尺寸。"),
      dialog);
  note->setWordWrap(true);
  layout->addWidget(note);
  auto* buttons = new QHBoxLayout;
  auto* status = new QLabel(dialog);
  status->setWordWrap(true);
  buttons->addWidget(status, 1);
  for (bool annotated : {false, true}) {
    auto* button =
        new QPushButton(annotated ? QStringLiteral("导出 CSV + 标注图")
                                  : QStringLiteral("导出 CSV"),
                        dialog);
    buttons->addWidget(button);
    connect(button, &QPushButton::clicked, dialog,
            [this, dialog, status, annotated, scope] {
              const auto directory = QFileDialog::getExistingDirectory(
                  dialog, QStringLiteral("选择结果目录"));
              if (directory.isEmpty()) return;
              const auto path = exportResults(directory, annotated, scope->currentData().toBool());
              status->setText(path.isEmpty()
                                  ? lastError()
                                  : QStringLiteral("已导出：") + path);
            });
  }
  layout->addLayout(buttons);
  dialog->show();
}
}  // namespace workbench
