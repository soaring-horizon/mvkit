// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/dialogs/resource_manager.hpp"
#include "ui/shell/main_window.hpp"
#include "ui/style/appearance.hpp"
#include "ui/style/operator_icons.hpp"
#include "ui/widgets/responsive_widgets.hpp"
#include "ui/widgets/run_selection_delegate.hpp"
#include <QCloseEvent>
#include <QCollator>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include "ui/widgets/frame_spin_box.hpp"
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace workbench {
namespace {
QString sampleName(const Document& d) {
  return d.captureName.isEmpty() ? QFileInfo(d.imagePath).fileName() : d.captureName;
}
QLabel* textLabel(QWidget* parent, const char* name) {
  auto* label = new QLabel(parent);
  label->setObjectName(name);
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return label;
}
// Reordering frozen frames does not read possibly changed files from disk.
QVector<int> orderedFrames(QWidget* parent, const QStringList& paths) {
  QDialog dialog(parent);
  dialog.setObjectName("inputSequenceOrder");
  dialog.setWindowTitle(QStringLiteral("核对序列顺序"));
  dialog.resize(600,480);
  QVBoxLayout layout(&dialog);
    QLabel help(QStringLiteral("拖动调整图像顺序；结构光请使用实际投影顺序。确认后只更新当前数据的此输入。"));
  help.setWordWrap(true); layout.addWidget(&help);
  QListWidget list;
  list.setObjectName("inputSequenceFrames");
  list.setDragDropMode(QAbstractItemView::InternalMove);
  for (int i=0; i<paths.size(); ++i) {
    auto* item = new QListWidgetItem(QFileInfo(paths[i]).fileName(), &list);
    item->setData(Qt::UserRole,i); item->setToolTip(paths[i]);
  }
  layout.addWidget(&list);
  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  buttons.button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认顺序"));
  buttons.button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
  layout.addWidget(&buttons);
  QObject::connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
  QObject::connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
  QVector<int> result;
  if (dialog.exec() == QDialog::Accepted)
    for (int i=0; i<list.count(); ++i) result.append(list.item(i)->data(Qt::UserRole).toInt());
  return result;
}
}

void MainWindow::showResourceManager(const QString& binding, int sample) {
  if (busy()) return;
  auto* existing = findChild<QDialog*>("captureManager");
  auto* manager = existing ? static_cast<ResourceManager*>(existing) : new ResourceManager(this);
  const int input=activeInputNode();
  manager->focusInput(binding.isEmpty()&&input>=0?workflow_[input].options["binding"].toString():binding,
                      sample >= 0 ? sample : activeImage_);
  manager->show(); manager->raise(); manager->activateWindow();
}
ResourceManager::ResourceManager(MainWindow* owner) : QDialog(owner), owner_(owner) {
  setObjectName("captureManager");
  setAttribute(Qt::WA_DeleteOnClose);
  connect(this,&QDialog::finished,this,[this]{setObjectName("closedResourceManager");});
  setWindowTitle(QStringLiteral("资源管理"));
  setModal(true); resize(1180,720); setMinimumSize(800,520);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(14,12,14,12); layout->setSpacing(10);
  auto* heading = new QHBoxLayout;
  auto* title = new QLabel(QStringLiteral("资源管理"),this);
  title->setObjectName("sectionTitle"); heading->addWidget(title);
  count_ = new QLabel(this); count_->setObjectName("imageCount");
  heading->addWidget(count_); heading->addStretch();
  heading->addWidget(new QLabel(QStringLiteral("数据输入源"),this));
  source_=new QComboBox(this);source_->setObjectName("resourceInputSource");
  source_->setMinimumWidth(180);source_->setMaximumWidth(320);
  source_->setToolTip(QStringLiteral("每次仅显示一个输入源的数据；切换来源只改变浏览，不切换主数据或运行算法。"));
  heading->addWidget(source_);
  auto* removeSample=new QPushButton(QStringLiteral("移除数据"),this);removeSample->setObjectName("deleteManagedSamples");removeSample->setToolTip(QStringLiteral("移除高亮行在所有输入源中的数据，运行勾选不影响此操作。"));
  connect(removeSample,&QPushButton::clicked,this,[this]{owner_->deleteManagedSamples(selectedSamples());refresh();});
  layout->addLayout(heading);
  auto* note = textLabel(this,"resourceManagerGuide");
  note->setText(QStringLiteral("当前页面只显示所选输入源的数据，可在上方切换来源。多视输入按相机分列，点击文件名预览对应相机；“运行”复选框与底部数据资源同步。"));
  note->setObjectName("secondaryText"); layout->addWidget(note);
  auto* split = new ViewSplitter(Qt::Horizontal,this);
  split->setObjectName("resourceManagerSplit");
  table_ = new QTableWidget(split); table_->setObjectName("captureGroupTable");
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->setSelectionBehavior(QAbstractItemView::SelectItems);
  table_->setItemDelegateForColumn(0,new RunSelectionDelegate(table_));
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setAlternatingRowColors(true); table_->setWordWrap(false);
  table_->verticalHeader()->hide();
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  auto* input = new QWidget; auto* body = new QVBoxLayout(input);
  body->setContentsMargins(10,10,10,10);
  inputTitle_ = textLabel(input,"resourceInputTitle"); inputTitle_->setObjectName("sectionTitle");
  auto* inputHeading = new QHBoxLayout;
  inputHeading->addWidget(inputTitle_,1);
  auto* fit = new QToolButton(input); fit->setIcon(studioIcon("fit"));
  fit->setObjectName("resourceInputFit");
  fit->setStyleSheet("QToolButton{padding:0;min-width:0;min-height:0;}");
  fit->setFixedSize(28,28); fit->setIconSize(QSize(18,18));
  fit->setToolTip(QStringLiteral("适应预览"));
  inputHeading->addWidget(fit,0,Qt::AlignTop);
  body->addLayout(inputHeading);
  frame_ = new FrameSpinBox(input); frame_->setObjectName("resourceInputFrame");
  frame_->setKeyboardTracking(false); body->addWidget(frame_);
  viewChoice_=new QComboBox(input);viewChoice_->setObjectName("resourceInputView");body->addWidget(viewChoice_);
  viewButtons_=new PairViewButtons(viewChoice_,input);body->addWidget(viewButtons_,0,Qt::AlignLeft);
  viewNames_=textLabel(input,"resourceViewNames");body->addWidget(viewNames_);
  connect(viewChoice_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{showFrame();});
  view_ = new ImageView(input); view_->setObjectName("resourceInputPreview");
  view_->setMinimumSize(160,120); view_->setRoiEditingEnabled(false);
  view_->setTool(ImageView::Tool::Pan); view_->setEmptyText(QStringLiteral("选择输入单元格查看图像"));
  body->addWidget(view_,1);
  path_ = textLabel(input,"resourceInputPath"); body->addWidget(path_);
  inputInfo_ = textLabel(input,"resourceInputInfo"); body->addWidget(inputInfo_);
  auto* edits = new QHBoxLayout;
  replaceInput_ = new QPushButton(QStringLiteral("替换…"),input);
  replaceInput_->setObjectName("replaceSampleInput"); edits->addWidget(replaceInput_);
  clearInput_ = new QPushButton(QStringLiteral("清空此输入"),input);
  clearInput_->setObjectName("clearSampleInput"); edits->addWidget(clearInput_);
  body->addLayout(edits);
  order_ = new QPushButton(QStringLiteral("调整序列顺序…"),input);
  order_->setObjectName("reorderSampleInput"); body->addWidget(order_);
  browse_ = new QPushButton(QStringLiteral("浏览序列与直方图…"),input);
  browse_->setObjectName("browseCaptureSequence"); body->addWidget(browse_);
  shareInput_ = new QPushButton(QStringLiteral("此输入应用到全部数据"),input);
  shareInput_->setToolTip(QStringLiteral("本页所有行都会使用这项输入，包括未勾选运行的行。"));
  shareInput_->setObjectName("shareSampleInput"); body->addWidget(shareInput_);
  auto* inputScroll = new QScrollArea(split);
  inputScroll->setMinimumWidth(270);
  inputScroll->setObjectName("resourceInputScroll");
  inputScroll->setWidgetResizable(true); inputScroll->setWidget(input);
  split->setSizes({760,360}); layout->addWidget(split,1);
  status_ = textLabel(this,"resourceManagerStatus"); layout->addWidget(status_);
  auto* close = new QDialogButtonBox(QDialogButtonBox::Close,this);
  close->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭")); close->addButton(removeSample, QDialogButtonBox::ActionRole); layout->addWidget(close);
  connect(close,&QDialogButtonBox::rejected,this,&QDialog::reject);
  connect(table_,&QTableWidget::currentCellChanged,this,[this]{if(!refreshing_)inspect();});
  connect(table_,&QTableWidget::cellClicked,this,[this](int,int){if(!refreshing_)inspect();});
  connect(source_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{binding_=source_->currentData().toString();refresh();});
  connect(table_,&QTableWidget::itemSelectionChanged,this,[this]{if(!refreshing_)updateActions();});
  connect(table_,&QTableWidget::itemChanged,this,[this](QTableWidgetItem* item){
    if(refreshing_ || item->column()!=0 || owner_->busy())return;
    const int index=item->data(Qt::UserRole).toInt();
    if(index<0 || index>=owner_->images_.size())return;
    owner_->images_[index].checked=item->checkState()==Qt::Checked;
    owner_->lastSingleImage_.clear();owner_->refreshFilm();owner_->updateActions();updateActions();
  });
  connect(table_,&QTableWidget::cellDoubleClicked,this,[this](int,int col){
    if(col>=2 && col<2+columns_.size()) selectFiles();
  });
  connect(frame_,QOverload<int>::of(&QSpinBox::valueChanged),this,[this]{showFrame();});
  connect(fit,&QToolButton::clicked,view_,&ImageView::fitImage);
  connect(replaceInput_,&QPushButton::clicked,this,&ResourceManager::selectFiles);
  connect(clearInput_,&QPushButton::clicked,this,&ResourceManager::clearInput);
  connect(shareInput_,&QPushButton::clicked,this,&ResourceManager::shareInput);
  connect(order_,&QPushButton::clicked,this,&ResourceManager::editOrder);
  connect(browse_,&QPushButton::clicked,this,[this]{
    if(preview_.frames.isEmpty() || !preview_.sequence) return;
    auto snapshot = sampleDocument(sampleIndex());
    snapshot.structuredFrames=preview_.frames; snapshot.structuredPaths=preview_.paths;
    snapshot.structuredKeys=preview_.keys; snapshot.image=preview_.frames.first();
    snapshot.captureName=sampleName(snapshot)+" · "+inputTitle_->text();
    owner_->showSequenceSnapshot(snapshot,this);
  });
  auto* poll = new QTimer(this);
  connect(poll,&QTimer::timeout,this,&ResourceManager::updateActions); poll->start(200);
  refresh();
}
void ResourceManager::closeEvent(QCloseEvent* event) {
  setObjectName("closedResourceManager"); QDialog::closeEvent(event);
}
const Document& ResourceManager::sampleDocument(int index) const {
  return index==owner_->activeImage_ ? owner_->document_ : owner_->images_[index].document;
}
int ResourceManager::sampleIndex() const {
  const auto* item = table_->item(table_->currentRow(),1);
  return item ? item->data(Qt::UserRole).toInt() : -1;
}
int ResourceManager::inputIndex() const {
  const int col=table_->currentColumn()-2;
  return col>=0 && col<columns_.size() ? columns_[col].input : -1;
}
QVector<int> ResourceManager::selectedSamples() const {
  QSet<int> unique;
  for(const auto& index:table_->selectionModel()->selectedIndexes())
    if(auto* item=table_->item(index.row(),1)) unique.insert(item->data(Qt::UserRole).toInt());
  return {unique.begin(),unique.end()};
}
void ResourceManager::focusInput(const QString& binding,int sample) {
  if(!binding.isEmpty())binding_=binding;
  else if(sample>=0&&sample<owner_->images_.size()&&sampleDocument(sample).sourceInputs.isEmpty()) {
    const auto& d=sampleDocument(sample);
    binding_=!d.structuredFrames.isEmpty()?"sequence":d.rightImage.CountObj()?"left":"image";
  }
  refresh();
  int col=1;
  for(int i=0;i<columns_.size();++i) if(inputs_[columns_[i].input].key==binding) {col=i+2;break;}
  if(binding.isEmpty() && !inputs_.isEmpty()) col=2;
  for(int row=0;row<table_->rowCount();++row)
    if(table_->item(row,1)->data(Qt::UserRole).toInt()==sample) {
      table_->setCurrentCell(row,col); table_->scrollToItem(table_->item(row,col)); inspect(); return;
    }
  if(table_->rowCount()) {table_->setCurrentCell(0,col);inspect();}
}
void ResourceManager::refresh() {
  const int current=sampleIndex();
  const QString key=binding_;
  const QString role=table_->currentColumn()>=2&&table_->currentColumn()-2<columns_.size()?columns_[table_->currentColumn()-2].role:QString();
  const int scroll=table_->horizontalScrollBar()->value();
  refreshing_=true; const QSignalBlocker block(table_);
  inputs_.clear();
  const auto names=owner_->nodeNames();
  auto add=[this](const QString& key,const QString& title,bool sequence,bool editable,bool multi=false){
    for(auto& input:inputs_) if(input.key==key) {
      input.conflict|=input.sequence!=sequence || input.multiView!=multi; input.nodes.append(title); return;
    }
    InputColumn input; input.key=key; input.title=title; input.nodes.append(title);
    input.sequence=sequence;input.multiView=multi; input.editable=editable; inputs_.append(input);
  };
  for(int i=0;i<owner_->workflow_.size();++i) {
    const auto& p=owner_->workflow_[i]; if(!sourceTool(p.algorithm)) continue;
    const auto binding=p.options.value("binding").toString();
    if(!binding.isEmpty()) add(binding,names[i],p.algorithm==Algorithm::SequenceInput,true,multiViewInput(p));
  }
  if(inputs_.isEmpty()) {
    bool image=false,stereo=false,sequence=false;
    for(int i=0;i<owner_->images_.size();++i) {
      const auto& d=sampleDocument(i);
      stereo|=d.rightImage.CountObj()!=0; sequence|=!d.structuredFrames.isEmpty();
      image|=!d.rightImage.CountObj() && d.structuredFrames.isEmpty();
    }
    if(stereo) {add("left",QStringLiteral("左图"),false,true); add("right",QStringLiteral("右图"),false,true);}
    if(sequence) add("sequence",QStringLiteral("图像序列"),true,true);
    if(image || (!stereo && !sequence)) add("image",QStringLiteral("图像"),false,true);
  }
  int selectedInput=0;
  {const QSignalBlocker block(source_);source_->clear();
    for(const auto& input:inputs_) {
      const auto type=input.multiView?QStringLiteral("多视"):input.sequence?QStringLiteral("序列"):QStringLiteral("单图");
      source_->addItem(studioIcon(operatorIconName(input.multiView?Algorithm::StereoImage:input.sequence?Algorithm::SequenceInput:Algorithm::ImageInput)),
          input.title+QStringLiteral(" · ")+type,input.key);
      source_->setItemData(source_->count()-1,input.nodes.join('\n')+"\n"+input.key,Qt::ToolTipRole);
    }
    selectedInput=qMax(0,source_->findData(binding_));source_->setCurrentIndex(selectedInput);
    binding_=source_->currentData().toString();
  }
  QVector<int> samples;
  for(int row=0;row<owner_->images_.size();++row) {
    const auto& d=sampleDocument(row);
    const bool legacyType=binding_=="sequence"?!d.structuredFrames.isEmpty()
        :(binding_=="left"||binding_=="right")?d.rightImage.CountObj()&&d.structuredFrames.isEmpty()
        :d.structuredFrames.isEmpty()&&!d.rightImage.CountObj();
    if(d.sourceInputs.contains(binding_)||d.sourceInputs.isEmpty()&&legacyType&&!resolveSourceData(d,binding_).frames.isEmpty())samples.append(row);
  }
  QStringList headers{QStringLiteral("运行"),QStringLiteral("序号")};columns_.clear();
  for(int index=0;index<inputs_.size();++index) {
    if(index!=selectedInput)continue;
    const auto& input=inputs_[index];
    const auto title=input.title+(input.nodes.size()>1?QString(" +%1").arg(input.nodes.size()-1):QString());
    QStringList roles;
    if(input.multiView) {
      for(int row:samples)for(const auto& view:resolveSourceData(sampleDocument(row),input.key).views)
        if(!roles.contains(view))roles.append(view);
      if(roles.isEmpty())roles=QStringList{"left","right"};
      if(roles.removeOne("right"))roles.prepend("right");
      if(roles.removeOne("left"))roles.prepend("left");
    } else roles.append(QString());
    for(const auto& view:roles) {
      columns_.append({index,view});
      headers.append(title+(view.isEmpty()?QString():QStringLiteral(" · ")+(view=="left"?QStringLiteral("左目"):view=="right"?QStringLiteral("右目"):view)));
    }
  }
  table_->clear(); table_->setColumnCount(headers.size());
  table_->setHorizontalHeaderLabels(headers);
  table_->setRowCount(samples.size());
  auto* header=table_->horizontalHeader();
  header->setSectionResizeMode(QHeaderView::Interactive);
  header->setSectionResizeMode(0,QHeaderView::Fixed); table_->setColumnWidth(0,54);
  table_->setColumnWidth(1,64);
  for(int i=0;i<columns_.size();++i) {
    const auto& input=inputs_[columns_[i].input];
    table_->setColumnWidth(i+2,190);
    auto* item=table_->horizontalHeaderItem(i+2);
    item->setToolTip(input.nodes.join('\n')+"\n"+input.key);
    item->setData(Qt::UserRole,input.key);item->setData(Qt::UserRole+1,columns_[i].role);
    item->setIcon(studioIcon(operatorIconName(input.multiView?Algorithm::StereoImage:input.sequence?Algorithm::SequenceInput:Algorithm::ImageInput)));
  }
  for(int row=0;row<samples.size();++row) {
    const int sample=samples[row];const auto& resource=owner_->images_[sample]; const auto& d=sampleDocument(sample);
    auto put=[this,row,sample](int col,const QString& text,const QString& tip={}) {
      auto* item=new QTableWidgetItem(text); item->setToolTip(tip);
      item->setData(Qt::UserRole,sample); table_->setItem(row,col,item); return item;
    };
    auto* run=put(0,{},QStringLiteral("点击此格或按空格切换参与运行；不改变当前图像"));
    run->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled|Qt::ItemIsUserCheckable);
    run->setCheckState(resource.checked ? Qt::Checked : Qt::Unchecked);
    run->setData(Qt::AccessibleTextRole,QStringLiteral("参与运行：")+sampleName(d));
    run->setTextAlignment(Qt::AlignCenter);
    if(resource.checked) run->setForeground(QColor("#299c85"));
    auto* ordinal=put(1,QString::number(row+1));ordinal->setTextAlignment(Qt::AlignCenter);
    for(int col=0;col<columns_.size();++col) {
      const auto& input=inputs_[columns_[col].input]; const auto sourceCell=resolveSourceData(d,input.key);
      const bool valid=!sourceCell.frames.isEmpty() && sourceCell.sequence==input.sequence && !sourceCell.views.isEmpty()==input.multiView && !input.conflict;
      const int view=input.multiView?sourceCell.views.indexOf(columns_[col].role):0;
      const auto text=input.conflict ? QStringLiteral("⚠ 输入类型冲突")
                      : !valid||view<0 ? QStringLiteral("⚠ 未配置")
                      : input.multiView ? QFileInfo(sourceCell.paths.value(view)).fileName()
                      : input.sequence ? QStringLiteral("%1 帧 · %2").arg(sourceCell.frames.size()).arg(QFileInfo(sourceCell.paths.value(0)).fileName())
                                       : QFileInfo(sourceCell.paths.value(0)).fileName();
      auto* item=put(col+2,text,valid ? (input.multiView?sourceCell.paths.value(view):sourceCell.paths.join('\n')) : QStringLiteral("此数据缺少有效输入，不会沿用其他数据的数据"));
      if(!valid||view<0) item->setForeground(QColor("#b47833"));
    }

  }
  refreshing_=false;
  if(table_->rowCount()) {
    int col=2; for(int i=0;i<columns_.size();++i) if(inputs_[columns_[i].input].key==key&&columns_[i].role==role) col=i+2;
    int selected=0;for(int row=0;row<table_->rowCount();++row)if(table_->item(row,1)->data(Qt::UserRole).toInt()==current)selected=row;
    table_->setCurrentCell(selected,col);
    table_->horizontalScrollBar()->setValue(scroll);
  }
  inspect();
}
void ResourceManager::inspect() {
  const int sample=sampleIndex(), index=inputIndex();
  const auto selectedRole=table_->currentColumn()>=2&&table_->currentColumn()-2<columns_.size()?columns_[table_->currentColumn()-2].role:QString();
  const auto previousView=selectedRole.isEmpty()?viewChoice_->currentData().toString():selectedRole;
  preview_={};
  if(sample<0) {
    inputTitle_->setText(QStringLiteral("尚无数据"));
    inputInfo_->setText(QStringLiteral("通过数据资源栏的“添加数据”导入图像、双目图组或图像序列。"));
  } else {
    const auto& d=sampleDocument(sample);
    if(index>=0) {
      const auto& input=inputs_[index]; preview_=resolveSourceData(d,input.key);
      inputTitle_->setText(input.title);
      inputInfo_->setText(input.conflict ? QStringLiteral("相同输入列被单图和序列节点同时引用，请先修改节点绑定。")
          : QStringLiteral("引用节点：%1\n只编辑当前数据的此输入；清空后将报告缺失。").arg(input.nodes.join(QStringLiteral("、"))));
    } else {
      inputTitle_->setText(sampleName(d));
      inputInfo_->setText(QStringLiteral("选择图像或序列单元格配置输入；此处预览不切换当前运行图像。"));
    }

  }
  {
    const QSignalBlocker block(frame_);
    frame_->setRange(1,qMax(1,preview_.frames.size())); frame_->setValue(1);
    frame_->setSuffix(QStringLiteral(" / %1 帧").arg(preview_.frames.size()));
  }
  frame_->setVisible(preview_.sequence && !preview_.frames.isEmpty());
  {const QSignalBlocker block(viewChoice_);viewChoice_->clear();
    for(const auto& role:preview_.views)viewChoice_->addItem(role=="left"?QStringLiteral("左目"):role=="right"?QStringLiteral("右目"):role,role);
    viewChoice_->setCurrentIndex(qMax(0,viewChoice_->findData(previousView)));
  }
  viewChoice_->setVisible(!preview_.views.isEmpty());
  viewButtons_->refresh();
  QStringList names;for(int i=0;i<preview_.views.size();++i)
    names<<QString("%1：%2").arg(preview_.views[i]=="left"?QStringLiteral("左目"):preview_.views[i]=="right"?QStringLiteral("右目"):preview_.views[i],QFileInfo(preview_.paths.value(i)).fileName());
  viewNames_->setText(names.join('\n'));viewNames_->setVisible(!names.isEmpty());
  showFrame();
  updateActions();
}
void ResourceManager::showFrame() {
  if(preview_.frames.isEmpty()) {view_->setImage({});view_->setImageName({});view_->setPixelText({});path_->clear();return;}
  const int i=std::clamp(preview_.views.isEmpty()?frame_->value()-1:viewChoice_->currentIndex(),0,preview_.frames.size()-1);
  const auto& image=preview_.frames[i];
  view_->setImage(makePreview(image).image);
  view_->setImageName(QFileInfo(preview_.paths.value(i)).fileName());
  view_->setPixelText({});
  path_->setText(QStringLiteral("%1 × %2 · %3 通道 · %4\n%5")
      .arg(image.Width()).arg(image.Height()).arg(image.Channels())
      .arg(pixelTypeText(image.Type()),QFileInfo(preview_.paths.value(i)).fileName()));
  path_->setToolTip(preview_.paths.value(i)+"\nSHA-256 "+preview_.keys.value(i));
}
void ResourceManager::updateActions() {
  const bool idle=!owner_->busy(); const int row=sampleIndex(),col=inputIndex();
  const bool editable=row>=0 && col>=0 && inputs_[col].editable && !inputs_[col].conflict;
  replaceInput_->setEnabled(idle&&editable); clearInput_->setEnabled(idle&&editable);
  shareInput_->setEnabled(idle&&editable&&!preview_.frames.isEmpty()&&table_->rowCount()>1);
  if (col >= 0) {
    clearInput_->setText(QStringLiteral("清空本行的%1").arg(inputs_[col].title));
    shareInput_->setText(QStringLiteral("将本行的%1应用到本页全部 %2 行").arg(inputs_[col].title).arg(table_->rowCount()));
  }
  auto* remove = findChild<QPushButton*>("deleteManagedSamples");
  const int selectedRows = selectedSamples().size();
  remove->setEnabled(idle && selectedRows > 0);
  remove->setText(QStringLiteral("移除整行数据（%1 行，所有输入）").arg(selectedRows));
  order_->setVisible(col>=0&&inputs_[col].sequence);
  order_->setEnabled(idle&&editable&&preview_.sequence&&preview_.frames.size()>1);
  browse_->setVisible(preview_.sequence&&!preview_.frames.isEmpty());
  int count=0;for(int row=0;row<table_->rowCount();++row)count+=owner_->images_[table_->item(row,1)->data(Qt::UserRole).toInt()].checked;
  count_->setText(QStringLiteral("%1 项数据 · 运行选择 %2").arg(table_->rowCount()).arg(count));
}
void ResourceManager::selectFiles() {
  const int row=sampleIndex(),col=inputIndex();
  if(owner_->busy()||row<0||col<0||!inputs_[col].editable||inputs_[col].conflict)return;
  const auto input=inputs_[col];
  if(input.multiView) {
    QDialog dialog(this);dialog.setObjectName("replaceMultiViewInput");dialog.setWindowTitle(QStringLiteral("替换多视图组"));QVBoxLayout layout(&dialog);
    QStringList files{preview_.paths.value(0),preview_.paths.value(1)};QVector<QLineEdit*> paths;
    for(int i=0;i<2;++i) {auto* line=new QLineEdit(files[i],&dialog);line->setReadOnly(true);paths.append(line);auto* button=new QPushButton(i?QStringLiteral("选择右图…"):QStringLiteral("选择左图…"),&dialog);layout.addWidget(button);layout.addWidget(line);
      connect(button,&QPushButton::clicked,&dialog,[&,i]{const auto path=QFileDialog::getOpenFileName(&dialog,i?QStringLiteral("右相机"):QStringLiteral("左相机"),{},QStringLiteral("图像 (*.png *.bmp *.tif *.tiff *.jpg *.jpeg)"));if(!path.isEmpty())paths[i]->setText(path);});}
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons.button(QDialogButtonBox::Ok)->setText(QStringLiteral("替换"));buttons.button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));layout.addWidget(&buttons);connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    owner_->guard(QStringLiteral("替换多视图组"),[&]{SourceData cell;owner_->loadResources(QStringLiteral("读取多视图组…"),[&](const std::atomic_bool& cancelled){cell=loadMultiViewData({paths[0]->text(),paths[1]->text()},{"left","right"},&cancelled);});owner_->setSampleInputs({row},input.key,cell);},true);refresh();return;
  }
  auto files=QFileDialog::getOpenFileNames(this,input.sequence?QStringLiteral("选择完整图像序列"):QStringLiteral("选择数据输入图像"),{},QStringLiteral("图像 (*.png *.bmp *.tif *.tiff *.jpg *.jpeg)"));
  if(files.isEmpty())return;
  if(files.size()>(input.sequence?64:1)){status_->setText(QStringLiteral("单图输入只能选择一张，序列最多64帧。"));return;}
  if(input.sequence){
    QCollator order;order.setNumericMode(true);
    std::sort(files.begin(),files.end(),[&](const QString&a,const QString&b){return order.compare(a,b)<0;});
    const auto indices=orderedFrames(this,files);if(indices.isEmpty())return;
    QStringList sorted;for(int i:indices)sorted.append(files[i]);files=sorted;
  }
  status_->setText(owner_->setSampleInput(row,input.key,files,input.sequence)?QStringLiteral("已更新当前数据的此输入。运行图像与运行选择保持。" ):owner_->lastError());refresh();
}
void ResourceManager::editOrder() {
  const int row=sampleIndex(),col=inputIndex();
  if(owner_->busy()||row<0||col<0||!inputs_[col].editable||inputs_[col].conflict||!preview_.sequence||preview_.frames.size()<2)return;
  const auto input=inputs_[col];const auto original=preview_;
  const auto indices=orderedFrames(this,original.paths);if(indices.isEmpty())return;
  SourceData sourceCell;sourceCell.sequence=true;
  for(int i:indices){sourceCell.paths.append(original.paths[i]);sourceCell.keys.append(original.keys[i]);sourceCell.frames.append(original.frames[i]);}
  status_->setText(owner_->setSampleInputs({row},input.key,sourceCell)?QStringLiteral("已更新序列顺序；沿用冻结像素，不重新读取文件。") :owner_->lastError());refresh();
}
void ResourceManager::clearInput() {
  const int row=sampleIndex(),col=inputIndex();
  if(owner_->busy()||row<0||col<0||!inputs_[col].editable||inputs_[col].conflict)return;
  status_->setText(owner_->setSampleInput(row,inputs_[col].key,{},inputs_[col].sequence)?QStringLiteral("已清空此输入；运行时报告缺失，不回退到旧数据。") :owner_->lastError());refresh();
}
void ResourceManager::shareInput() {
  const int col=inputIndex();if(owner_->busy()||col<0||!inputs_[col].editable||inputs_[col].conflict||preview_.frames.isEmpty())return;
  QVector<int> rows;for(int row=0;row<table_->rowCount();++row)rows.append(table_->item(row,1)->data(Qt::UserRole).toInt());
  status_->setText(owner_->setSampleInputs(rows,inputs_[col].key,preview_)?QStringLiteral("已应用到全部数据的同一输入列，保留各数据其他输入。") :owner_->lastError());refresh();
}
} // namespace workbench
