// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "runtime/run_controller.hpp"
#include <QSet>
#include <stdexcept>
namespace workbench {
QStringList planRunSamples(const QStringList& selected,const QString& last,bool nextOnly) {
  if(selected.isEmpty())throw std::runtime_error("请先在图像资源中选择待处理样本");
  if(!nextOnly)return selected;
  const int previous=selected.indexOf(last);return {selected[(previous+1)%selected.size()]};
}
quint64 RunController::begin(QVector<RunItem> items) {
  if(active_||items.isEmpty())throw std::runtime_error("运行队列为空或仍在运行");
  QSet<QString> ids;for(const auto& item:items) {
    if(item.id.isEmpty()||ids.contains(item.id))throw std::runtime_error("运行队列样本身份无效或重复");ids.insert(item.id);
  }
  items_=std::move(items);index_=errors_=0;active_=true;return ++generation_;
}
bool RunController::advance(quint64 generation,bool failed) {
  if(!active_||generation!=generation_||index_>=items_.size())return false;
  errors_+=failed;++index_;return true;
}
void RunController::applyTrainedModels(const QVector<ModelResource>& models) {
  for(int i=index_+1;i<items_.size();++i)for(auto& model:items_[i].snapshot.models)
    for(const auto& trained:models)if(model.id==trained.id)model=trained;
}
void RunController::complete(){active_=false;items_.clear();index_=0;}
void RunController::cancel(){complete();errors_=0;++generation_;}
}
