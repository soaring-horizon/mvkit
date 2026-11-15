// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/shell/main_window.hpp"
#include "ui/widgets/loading_task.hpp"
#include <QEventLoop>
#include <QLabel>
#include <QProgressDialog>
#include <QPushButton>
#include <QTimer>
#include <future>
namespace workbench {
void MainWindow::loadResources(const QString& title,const std::function<void(const std::atomic_bool&)>& operation) {
  if(loading_)throw LoadingCancelled();
  autoPreview_->stop();loading_=true;loadingCancel_=std::make_shared<std::atomic_bool>(false);updateActions();
  try {
    QProgressDialog dialog(title,QStringLiteral("取消"),0,0,this);
    dialog.setObjectName("resourceLoadingDialog");dialog.setWindowTitle(QStringLiteral("加载资源"));
    dialog.setWindowModality(Qt::WindowModal);dialog.setMinimumDuration(150);
    if(auto* button=dialog.findChild<QPushButton*>())button->setObjectName("resourceLoadingCancel");
    dialog.setAutoClose(false);dialog.setAutoReset(false);dialog.setValue(0);
    const auto cancelled=loadingCancel_;
    connect(&dialog,&QProgressDialog::canceled,this,[&dialog,cancelled]{
      cancelled->store(true);if(auto* button=dialog.findChild<QPushButton*>())button->setEnabled(false);dialog.setLabelText(QStringLiteral("正在取消，等当前读取结束后保留原工程…"));dialog.show();
    });
    auto future=std::async(std::launch::async,[operation,cancelled]{operation(*cancelled);});
    QEventLoop loop;QTimer poll;poll.setInterval(10);
    connect(&poll,&QTimer::timeout,&loop,[&]{if(future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready)loop.quit();});
    poll.start();loop.exec();
    if(cancelled->load())throw LoadingCancelled();
    future.get();
  } catch(...) {loading_=false;loadingCancel_.reset();updateActions();throw;}
  loading_=false;loadingCancel_.reset();updateActions();
}
}
