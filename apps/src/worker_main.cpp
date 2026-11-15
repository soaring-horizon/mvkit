// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include <QCoreApplication>
#include <QDir>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <cstdio>
#endif

#include "runtime/worker_protocol.hpp"
namespace {
void reportNode(int node) {
  const auto record = QByteArray("node:") + QByteArray::number(node) + '\n';
#ifdef Q_OS_WIN
  const auto pipe = GetStdHandle(STD_OUTPUT_HANDLE);
  if (pipe && pipe != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(pipe, record.constData(), DWORD(record.size()), &written,
              nullptr);
  }
#else
  std::fwrite(record.constData(), 1, std::size_t(record.size()), stdout);
  std::fflush(stdout);
#endif
}
}  // namespace
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().size() != 2 && app.arguments().size() != 3) return 2;
  const QDir directory(app.arguments()[1]);
  try {
    auto document =
        workbench::readWorkerRequest(directory.filePath("request.bin"));
    if (app.arguments().size() == 3) document.batchCalibrationDirectory = app.arguments().at(2);
    workbench::writeWorkerResult(directory.filePath("result.bin"),
                                 workbench::execute(document, reportNode));
    return 0;
  } catch (const std::exception& error) {
    try {
      workbench::Result result;
      result.error = QString::fromUtf8(error.what());
      workbench::writeWorkerResult(directory.filePath("result.bin"), result);
    } catch (...) {
    }
    return 1;
  }
}
