// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "model/document.hpp"
namespace workbench {
struct RunItem {QString id;Document snapshot;};
QStringList planRunSamples(const QStringList& selected,const QString& last,bool nextOnly);
class RunController {
 public:
  quint64 begin(QVector<RunItem> items);
  bool advance(quint64 generation,bool failed);
  void applyTrainedModels(const QVector<ModelResource>& models);
  void complete();
  void cancel();
  bool active() const{return active_;}
  int index() const{return index_;}
  int errors() const{return errors_;}
  quint64 generation() const{return generation_;}
  const QVector<RunItem>& items() const{return items_;}
 private:
  QVector<RunItem> items_;
  int index_=0,errors_=0;
  bool active_=false;
  quint64 generation_=0;
};
}
