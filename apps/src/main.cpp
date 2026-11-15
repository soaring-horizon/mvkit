// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QFont>
#include <QStyleFactory>

#include "ui/style/appearance.hpp"
#include "ui/shell/main_window.hpp"

int main(int argc, char** argv) {
  QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
      Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("MVKit");
  // Keep the existing QSettings identity so a product rename preserves preferences.
  QCoreApplication::setApplicationName("MVKit Studio");
  QGuiApplication::setApplicationDisplayName("MV Studio");
  QCoreApplication::setApplicationVersion(QStringLiteral(MVKIT_STUDIO_VERSION));
  workbench::applyAppearance(app);
  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("MV Studio 机器视觉工作台"));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument("file",
                               QStringLiteral("可选图像列表或 .mvproj 工程"));
  parser.process(app);
  workbench::MainWindow window;
  window.show();
  if (!parser.positionalArguments().isEmpty()) {
    const auto path = parser.positionalArguments().first();
    const bool project = path.endsWith(".json", Qt::CaseInsensitive) ||
                         path.endsWith(".mvproj", Qt::CaseInsensitive);
    if (project)
      window.openPath(path, true);
    else
      window.addImages(parser.positionalArguments());
  }
  return app.exec();
}
