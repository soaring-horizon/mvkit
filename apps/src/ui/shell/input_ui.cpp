// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/shell/main_window.hpp"
#include "ui/style/appearance.hpp"
#include "ui/style/operator_icons.hpp"
#include <QSet>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHeaderView>
#include <algorithm>
#include <QUuid>
#include "ui/views/image_view.hpp"
#include "model/sample_inputs.hpp"
#include "model/sample_edit.hpp"
#include <stdexcept>
namespace workbench {
int MainWindow::inputPreviewFrame(const SourceData& cell,const QString& binding) const {
  return std::max(0,cell.views.indexOf(inputPreviewViews_.value(binding,"left")));
}
int MainWindow::activeInputNode() const {
  if(selectedStep_>=0 && selectedStep_<workflow_.size() && sourceTool(workflow_[selectedStep_].algorithm))return selectedStep_;
  QVector<int> group=inputGroupNodes();
  for(int i:group)if(sourceTool(workflow_[i].algorithm) && workflow_[i].options.value("binding").toString()==activeInputKey_)return i;
  const auto preferred=preferredInputs_.value(workflowIds_.value(selectedStep_));
  for(int i:group)if(sourceTool(workflow_[i].algorithm) && workflow_[i].options.value("binding").toString()==preferred)return i;
  for(int i:group)if(sourceTool(workflow_[i].algorithm))return i;
  return -1;
}
QVector<int> MainWindow::inputGroupNodes() const {
  int seed=selectedStep_;
  if(seed<0 || seed>=workflow_.size()) {
    seed=-1;for(int i=0;i<workflow_.size();++i)if(sourceTool(workflow_[i].algorithm) && workflow_[i].options.value("binding").toString()==activeInputKey_)seed=i;
    if(seed<0)seed=workflowOutput_;
  }
  if(seed<0 || seed>=workflow_.size())return {};
  QSet<int> seen{seed};QVector<int> queue{seed};
  for(int q=0;q<queue.size();++q)for(int i=0;i<workflow_.size();++i) {
    if(!seen.contains(i) && (workflowLinks_.value(queue[q]).contains(i)||workflowLinks_.value(i).contains(queue[q]))) {seen.insert(i);queue.append(i);}
  }
  std::sort(queue.begin(),queue.end());return queue;
}
QVector<int> MainWindow::trackSamples(bool wholeGroup) const {
  const int input=activeInputNode();QVector<int> result;
  if(input<0) {bool explicitSources=false;for(const auto& node:workflow_)explicitSources|=sourceTool(node.algorithm);if(!explicitSources)for(int i=0;i<images_.size();++i)result.append(i);return result;}
  QVector<int> sources;
  if(wholeGroup) {for(int i:inputGroupNodes())if(sourceTool(workflow_[i].algorithm))sources.append(i);}
  else sources.append(input);
  for(int row=0;row<images_.size();++row) {
    const auto& d=row==activeImage_?document_:images_[row].document;
    bool found=false;
    for(int node:sources) {
      const auto key=workflow_[node].options.value("binding").toString();
      if(d.sourceInputs.contains(key)) {found|=wholeGroup || !d.sourceInputs[key].frames.isEmpty();continue;}
      // Development fixtures with implicit raw inputs are materialized by the
      // tutorial loader. This fallback never crosses explicitly owned tracks.
      if(d.sourceInputs.isEmpty()) {
        const auto cell=resolveSourceData(d,key);
        found|=!cell.frames.isEmpty() && cell.sequence==(workflow_[node].algorithm==Algorithm::SequenceInput) && cell.views.isEmpty()!=multiViewInput(workflow_[node]);
      }
    }
    if(found)result.append(row);
  }
  return result;
}
int MainWindow::inputGroupTarget() const {
  const auto group=inputGroupNodes();if(activeInputNode()<0)return -1;
  if(group.contains(workflowOutput_))return workflowOutput_;
  int target=activeInputNode();
  for(int node:group) {
    bool used=false;for(int other:group)used|=workflowLinks_.value(other).contains(node);
    if(!used)target=node;
  }
  return target;
}
bool MainWindow::confirmInputMerge(int source,int target) {
  try {
  if(source<0 || target<0)return true;
  auto component=[&](int seed){QSet<int> found{seed};QVector<int> todo{seed};for(int q=0;q<todo.size();++q)for(int n=0;n<workflow_.size();++n)if(!found.contains(n)&&(workflowLinks_.value(todo[q]).contains(n)||workflowLinks_.value(n).contains(todo[q]))){found.insert(n);todo.append(n);}return found;};
  const auto from=component(source),to=component(target);if(from.contains(target))return true;
  QStringList sourceKeys,targetKeys;for(int node:from)if(sourceTool(workflow_[node].algorithm))sourceKeys<<workflow_[node].options.value("binding").toString();for(int node:to)if(sourceTool(workflow_[node].algorithm))targetKeys<<workflow_[node].options.value("binding").toString();
  if(sourceKeys.isEmpty()||targetKeys.isEmpty())return true;
  QVector<int> sourceRows,targetRows;
  for(int row=0;row<images_.size();++row) {const auto& d=row==activeImage_?document_:images_[row].document;for(const auto& key:sourceKeys)if(d.sourceInputs.contains(key)){sourceRows.append(row);break;}for(const auto& key:targetKeys)if(d.sourceInputs.contains(key)){targetRows.append(row);break;}}
  if(sourceRows.isEmpty()||targetRows.isEmpty())return true;
  QVector<int> unpaired;for(int row:sourceRows)if(!targetRows.contains(row))unpaired.append(row);if(unpaired.isEmpty())return true;
  QDialog dialog(this);dialog.setObjectName("inputMergePlan");dialog.setWindowTitle(QStringLiteral("确认流程与数据关联"));dialog.resize(720,440);QVBoxLayout layout(&dialog);
  QLabel note(QStringLiteral("连线将联合两个输入组。请选择属于同一工件的数据；不关联的行保留，运行时明确报告缺项，不会按序号自动拼接。"));note.setWordWrap(true);layout.addWidget(&note);
  QTableWidget table(unpaired.size(),2);table.setObjectName("inputMergeMapping");table.setHorizontalHeaderLabels({QStringLiteral("来源数据"),QStringLiteral("关联到目标数据")});table.horizontalHeader()->setStretchLastSection(true);table.setColumnWidth(0,300);table.setEditTriggers(QAbstractItemView::NoEditTriggers);layout.addWidget(&table);
  QVector<QComboBox*> choices;
  for(int r=0;r<unpaired.size();++r) {const auto& d=unpaired[r]==activeImage_?document_:images_[unpaired[r]].document;table.setItem(r,0,new QTableWidgetItem(d.captureName.isEmpty()?QFileInfo(d.imagePath).fileName():d.captureName));auto* choice=new QComboBox(&table);choice->addItem(QStringLiteral("不关联，保留待配对数据"),-1);for(int row:targetRows){const auto& dest=row==activeImage_?document_:images_[row].document;choice->addItem(dest.captureName.isEmpty()?QFileInfo(dest.imagePath).fileName():dest.captureName,row);}table.setCellWidget(r,1,choice);choices.append(choice);}
  QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons.button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认连接"));buttons.button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));layout.addWidget(&buttons);connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
  if(dialog.exec()!=QDialog::Accepted)return false;
  storeActive();QVector<QPair<int,Document>> staged;QSet<int> used;QVector<int> consumed;
  for(int r=0;r<choices.size();++r) {const int destination=choices[r]->currentData().toInt();if(destination<0)continue;if(used.contains(destination))throw std::runtime_error("同一个目标数据不能关联多条来源，请重新选择");used.insert(destination);
    auto dest=images_[destination].document,origin=images_[unpaired[r]].document;
    for(const auto& key:sourceKeys)if(origin.sourceInputs.contains(key)){dest=withSampleInput(dest,workflow_,key,origin.sourceInputs[key]);origin.sourceInputs.remove(key);}
    for(int node:from)if(node<origin.nodeSettings.size()&&node<dest.nodeSettings.size()){dest.nodeSettings[node]=origin.nodeSettings[node];dest.rangeOverrides[node]=origin.rangeOverrides.value(node);}
    staged.append({destination,dest});staged.append({unpaired[r],origin});if(origin.sourceInputs.isEmpty())consumed.append(unpaired[r]);
  }
  EditGuard edit(*this,QStringLiteral("关联输入数据"));
  for(auto& item:staged){images_[item.first].document=std::move(item.second);++images_[item.first].version;++images_[item.first].baseVersion;}
  std::sort(consumed.begin(),consumed.end(),std::greater<int>());for(int row:consumed)images_.remove(row);
  if(!staged.isEmpty()) {activeImage_=-1;if(!images_.isEmpty())activateImage(0);setDirty(true);}
  return true;
  } catch(const std::exception& e) {setOperatorStatus(QStringLiteral("关联未完成"),"error",QString::fromUtf8(e.what()));return false;}
}
void MainWindow::syncInputTrack() {
  if(syncingInputTrack_)return;
  syncingInputTrack_=true;
  const int input=activeInputNode();
  if(input>=0){activeInputKey_=workflow_[input].options.value("binding").toString();for(int node:inputGroupNodes())preferredInputs_[workflowIds_.value(node)]=activeInputKey_;}else activeInputKey_.clear();
  if(inputTrackChoice_) {
    QSignalBlocker block(inputTrackChoice_);inputTrackChoice_->clear();
    const auto names=nodeNames();for(int i=0;i<workflow_.size();++i)if(sourceTool(workflow_[i].algorithm)) {
      const auto& p=workflow_[i];const auto binding=p.options["binding"].toString();int samples=0;
      for(int row=0;row<images_.size();++row)if(!resolveSourceData(row==activeImage_?document_:images_[row].document,binding).frames.isEmpty())++samples;
      const auto type=xyzInput(p)?QStringLiteral("XYZ"):multiViewInput(p)?QStringLiteral("多视"):p.algorithm==Algorithm::SequenceInput?QStringLiteral("序列"):QStringLiteral("单图");
      inputTrackChoice_->addItem(studioIcon(operatorIconName(multiViewInput(p)?Algorithm::StereoImage:p.algorithm)),
          QStringLiteral("%1 · %2 · %3 项").arg(names[i],type).arg(samples),i);
      inputTrackChoice_->setItemData(inputTrackChoice_->count()-1,names[i]+"\n"+binding,Qt::ToolTipRole);
    }
    inputTrackChoice_->setCurrentIndex(inputTrackChoice_->findData(input));
    inputTrackChoice_->setVisible(inputTrackChoice_->count()>0);inputTrackChoice_->setToolTip(input<0?QStringLiteral("选择输入轨"):QStringLiteral("数据资源 · ")+names.value(input));
    if(auto* label=findChild<QLabel*>("dataResourceTitle"))label->show();
  }
  const auto visible=trackSamples();
  if(input>=0 && !visible.isEmpty() && !trackSamples(true).contains(activeImage_) && !runQueue_.active() && !execution_.busy())activateImage(visible.first());
  refreshFilm();updateActions();syncingInputTrack_=false;
}
bool MainWindow::importSourceSamples(const QVector<SourceData>& cells,const QStringList& names) {
  const int input=activeInputNode();if(input<0 || cells.isEmpty() || cells.size()!=names.size())return false;
  const auto p=workflow_[input];const auto key=p.options.value("binding").toString();
  for(const auto& cell:cells)if(cell.frames.isEmpty() || cell.sequence!=(p.algorithm==Algorithm::SequenceInput) || !cell.views.isEmpty()!=multiViewInput(p))throw std::runtime_error("添加的数据类型与当前输入轨不匹配");
  QVector<int> targets(cells.size(),-1);QVector<int> groupSources;
  for(int node:inputGroupNodes())if(sourceTool(workflow_[node].algorithm))groupSources.append(node);
  const auto existing=trackSamples(true);
  if(groupSources.size()>1 && !existing.isEmpty()) {
    QDialog plan(this);plan.setObjectName("inputImportPlan");plan.setWindowTitle(QStringLiteral("确认输入与数据配对"));plan.resize(720,440);
    QVBoxLayout layout(&plan);QLabel note(QStringLiteral("添加到：%1。按数据身份明确配对；其他输入保持，不自动拼接不同流程。").arg(nodeNames().value(input)));note.setWordWrap(true);layout.addWidget(&note);
    QComboBox mode;mode.setObjectName("inputImportMode");mode.addItems({QStringLiteral("新增数据"),QStringLiteral("补充已有数据")});layout.addWidget(&mode);
    QTableWidget table(cells.size(),2);table.setObjectName("inputImportMapping");table.setHorizontalHeaderLabels({QStringLiteral("输入数据"),QStringLiteral("目标数据")});table.horizontalHeader()->setStretchLastSection(true);table.setColumnWidth(0,310);table.setEditTriggers(QAbstractItemView::NoEditTriggers);layout.addWidget(&table);
    QVector<QComboBox*> choices;
    for(int r=0;r<cells.size();++r) {
      auto* item=new QTableWidgetItem(names[r]);item->setToolTip(cells[r].paths.join('\n'));table.setItem(r,0,item);
      auto* choice=new QComboBox(&table);choice->addItem(QStringLiteral("新数据"),-1);
      for(int index:existing) {const auto& d=index==activeImage_?document_:images_[index].document;choice->addItem(d.captureName.isEmpty()?QFileInfo(d.imagePath).fileName():d.captureName,index);}
      table.setCellWidget(r,1,choice);choices.append(choice);
    }
    connect(&mode,QOverload<int>::of(&QComboBox::currentIndexChanged),&plan,[&](int m){
      QVector<int> missing;for(int index:existing) {const auto& d=index==activeImage_?document_:images_[index].document;if(!d.sourceInputs.contains(key)||d.sourceInputs[key].frames.isEmpty())missing.append(index);}
      for(int r=0;r<choices.size();++r)choices[r]->setCurrentIndex(m && r<missing.size()?choices[r]->findData(missing[r]):0);
    });
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons.button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认添加"));buttons.button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));layout.addWidget(&buttons);
    connect(&buttons,&QDialogButtonBox::accepted,&plan,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&plan,&QDialog::reject);
    if(plan.exec()!=QDialog::Accepted)return false;
    QSet<int> used;for(int r=0;r<choices.size();++r){targets[r]=choices[r]->currentData().toInt();if(targets[r]>=0){if(used.contains(targets[r]))throw std::runtime_error("不能将多条输入覆盖到同一个数据，请重新配对");used.insert(targets[r]);}}
  }
  storeActive();QVector<Document> staged;
  for(int r=0;r<cells.size();++r) {
    Document d=targets[r]>=0?images_[targets[r]].document:Document();
    if(targets[r]<0) {
      d.parameters=workbenchParameters_;d.captureId=QUuid::createUuid().toString(QUuid::WithoutBraces);d.captureName=names[r];
      d.image=cells[r].frames.first();d.imagePath=cells[r].paths.first();d.pixelKey=cells[r].keys.first();d.preview=makePreview(d.image).image;d.coordinateFrame=d.imagePath;
      d.nodeSettings.resize(workflow_.size());d.rangeOverrides.fill(false,workflow_.size());d.sourceInputs.insert(key,cells[r]);
    }
    staged.append(withSampleInput(d,workflow_,key,cells[r]));
  }
  EditGuard edit(*this,QStringLiteral("添加输入数据"));int first=-1;
  for(int r=0;r<staged.size();++r) {
    if(targets[r]>=0) {auto& item=images_[targets[r]];item.document=std::move(staged[r]);++item.version;++item.baseVersion;}
    else {const auto id=staged[r].captureId;images_.append({id,std::move(staged[r]),{},0,true});targets[r]=images_.size()-1;}
    if(first<0)first=targets[r];
  }
  activeImage_=-1;activateImage(first);setDirty(true);refreshFilm();updateActions();return true;
}
void MainWindow::deleteManagedSamples(const QVector<int>& rows) {
  if(busy()||rows.isEmpty())return;
  EditGuard edit(*this,QStringLiteral("删除完整数据"));storeActive();
  auto ordered=rows;std::sort(ordered.begin(),ordered.end(),std::greater<int>());
  for(int row:ordered)if(row>=0&&row<images_.size())images_.remove(row);
  activeImage_=-1;
  if(!images_.isEmpty())activateImage(0);else {const auto parameters=document_.parameters;document_={};document_.parameters=parameters;view_->setImage({});nodeView_->setImage({});result_={};}
  setDirty(true);syncInputTrack();updateActions();
}
bool MainWindow::setSampleInput(int sample, const QString& binding,
                                const QStringList& paths, bool sequence) {
  if (busy() || sample < 0 || sample >= images_.size()) return false;
  bool applied = false;
  const bool loaded = guard(QStringLiteral("配置数据输入"), [&] {
    SourceData cell;
    cell.sequence = sequence;
    if (!paths.isEmpty())loadResources(QStringLiteral("读取数据输入及校验帧…"),[&cell,paths,sequence](const std::atomic_bool& cancelled){cell=loadSourceData(paths,sequence,&cancelled);});
    applied = setSampleInputs({sample}, binding, cell);
  }, true);
  return loaded && applied;
}
bool MainWindow::setSampleInputs(const QVector<int>& samples,
                                 const QString& binding,
                                 const SourceData& sourceCell) {
  if (busy() || samples.isEmpty()) return false;
  return guard(QStringLiteral("配置数据输入"), [&] {
    QVector<QPair<int,Document>> staged;
    for(int sample:QSet<int>(samples.begin(),samples.end())) {
      if(sample<0||sample>=images_.size())throw std::runtime_error("请选择有效数据");
      auto original=sample==activeImage_?document_:images_[sample].document;
      if(sample==activeImage_&&nodeContext())original.parameters=workbenchParameters_;
      staged.append({sample,withSampleInput(original,workflow_,binding,sourceCell)});
    }
    EditGuard edit(*this,samples.size()==1?QStringLiteral("配置数据输入"):QStringLiteral("批量配置数据输入"));
    storeActive();
    for(auto& replacement:staged) {
      auto& resource=images_[replacement.first];resource.document=std::move(replacement.second);
      if(resource.document.captureId.isEmpty())resource.document.captureId=resource.id;
      ++resource.version;++resource.baseVersion;
    }
    const bool active = samples.contains(activeImage_);
    if (active) {
      const auto parameters=document_.parameters;
      document_=images_[activeImage_].document; document_.parameters=parameters;
      view_->setImage(document_.preview,true);
      nodeView_->setImage(document_.preview,true);
      invalidate();
    }
    setDirty(true);
    syncInputTrack();
    refreshProperties();
    if (active) schedulePreview();
  }, true);
}
void MainWindow::showSampleInputs() {
  showResourceManager(sourceTool(document_.parameters.algorithm)
                          ? document_.parameters.options.value("binding").toString()
                          : QString());
}
}  // namespace workbench
