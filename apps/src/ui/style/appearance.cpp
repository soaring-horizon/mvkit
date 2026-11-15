// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/style/appearance.hpp"
#include "ui/style/fluent_paths.hpp"
#include "ui/style/lucide_toolbar_paths.hpp"

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyleFactory>
#include <QResource>

static void ensureStudioUiResources() {
  static const bool initialized = [] {
    Q_INIT_RESOURCE(studio_ui);
    return true;
  }();
  Q_UNUSED(initialized);
}
namespace workbench {
namespace {
bool drawSelectedToolbarIcon(QPainter& painter,const QString& name,const QColor& color) {
  const auto& paths=detail::lucideToolbarPaths(name);
  if(paths.isEmpty())return false;
  painter.save();
  // Match the selected 20px preview: 24px source grid, 1.5 source-unit stroke.
  painter.scale(20.0/24.0,20.0/24.0);
  painter.setPen(QPen(color,1.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
  painter.setBrush(Qt::NoBrush);
  for(const auto& path:paths)painter.drawPath(path);
  painter.restore();return true;
}
bool drawFluentIcon(QPainter& painter,const QString& name,const QColor& color) {
  const auto& paths=detail::fluentPaths(name);
  if(paths.isEmpty())return false;
  painter.save();
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  for(const auto& path:paths)painter.drawPath(path);
  if(name=="run-current") {
    painter.setPen(QPen(color,1.0,Qt::SolidLine,Qt::RoundCap));
    painter.drawLine(QPointF(5,18),QPointF(17,18));
  }
  if(name=="run-selected") {
    painter.translate(4.8,5.4);painter.scale(.45,.45);
    for(const auto& path:detail::fluentPaths(QStringLiteral("play")))painter.drawPath(path);
  }
  painter.restore();return true;
}
} // namespace
QIcon studioIcon(const QString& name, const QColor& color) {
  QPixmap pixmap(60, 60);
  pixmap.fill(Qt::transparent);
  QPainter p(&pixmap);
  p.setRenderHint(QPainter::Antialiasing);
  p.scale(3, 3);
  p.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(Qt::NoBrush);
  if (drawSelectedToolbarIcon(p,name,color)) {
    // Only the four user-selected toolbar identities override Fluent.
  } else if (drawFluentIcon(p,name,color)) {
    // Official Fluent 20px filled outlines; preserve data-resource action glyphs.
  } else if (name == "more") {
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    for (int x : {5, 10, 15}) p.drawEllipse(QPointF(x, 10), 1.2, 1.2);
  } else if (name == "studio") {
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#5965df"));
    p.drawRoundedRect(QRectF(1, 1, 18, 18), 5, 5);
    p.setPen(Qt::white);
    p.setFont(QFont("Segoe UI", 6, QFont::DemiBold));
    p.drawText(QRectF(1, 1, 18, 18), Qt::AlignCenter, "MV");
  } else if (name == "chevronDown" || name == "chevronRight") {
    if(name=="chevronDown") {p.drawLine(6,8,10,12);p.drawLine(10,12,14,8);}
    else {p.drawLine(8,6,12,10);p.drawLine(12,10,8,14);}
  } else if (name == "corner-menu") {
    p.setPen(Qt::NoPen);p.setBrush(color);
    // Right angle at (17,17), without a native menu arrow over this glyph.
    QPainterPath corner;
    corner.moveTo(3,17);corner.lineTo(17,3);corner.lineTo(17,17);corner.closeSubpath();
    p.drawPath(corner);
  } else if (name == "minimize") {
    p.drawLine(5, 11, 15, 11);
  } else if (name == "pin") {
    p.drawLine(8, 3, 14, 3);
    p.drawLine(9, 3, 9, 8);
    p.drawLine(13, 3, 13, 8);
    p.drawLine(9, 8, 5, 12);
    p.drawLine(5, 12, 16, 12);
    p.drawLine(16, 12, 13, 8);
    p.drawLine(10, 12, 10, 18);
  } else if (name == "maximize") {
    p.drawRoundedRect(QRectF(4, 4, 12, 12), 1, 1);
  } else if (name == "restore") {
    p.drawRect(QRectF(6, 3, 10, 10));
    p.setBrush(QApplication::palette().color(QPalette::Window));
    p.drawRect(QRectF(3, 6, 10, 10));
  } else if (name == "close") {
    p.drawLine(6, 6, 14, 14);
    p.drawLine(14, 6, 6, 14);
  } else if (name == "split") {
    p.drawRoundedRect(QRectF(2, 3, 16, 14), 1, 1);
    p.drawLine(8, 3, 8, 17);
  } else if (name == "check-all") {
    p.drawRoundedRect(QRectF(3,3,12,12),1,1);p.drawPolyline(QPolygonF(QVector<QPointF>{{6,9},{9,12},{14,6}}));
    p.drawPolyline(QPolygonF(QVector<QPointF>{{6,18},{18,18},{18,6}}));
  } else if (name == "selection-clear") {
    p.drawRoundedRect(QRectF(3,3,14,14),1,1);p.drawLine(6,10,14,10);
  } else if (name == "plus" || name == "minus") {
    p.drawLine(4, 10, 16, 10);
    if (name == "plus") p.drawLine(10, 4, 10, 16);
  } else if (name == "actual") {
    for (const double x : {4., 15.}) {
      p.drawLine(QPointF(x - 2, 6), QPointF(x, 4));
      p.drawLine(QPointF(x, 4), QPointF(x, 16));
      p.drawLine(QPointF(x - 2, 16), QPointF(x + 2, 16));
    }
    p.drawPoint(QPointF(9.5, 7));
    p.drawPoint(QPointF(9.5, 13));
  } else if (name == "compare") {
    p.drawRoundedRect(QRectF(2, 3, 16, 14), 1, 1);
    p.drawLine(10, 1, 10, 19);
    p.drawLine(4, 10, 8, 10);
    p.drawLine(12, 10, 16, 10);
  } else if (name == "point") {
    p.drawLine(3, 10, 17, 10);
    p.drawLine(10, 3, 10, 17);
    p.drawEllipse(QRectF(7, 7, 6, 6));
  } else if (name == "text") {
    p.setFont(QFont("Segoe UI", 9, QFont::DemiBold));
    p.drawText(QRectF(0, 0, 20, 20), Qt::AlignCenter, "T");
  } else if (name == "circle") {
    p.drawEllipse(QRectF(3, 3, 14, 14));
  } else if (name == "check") {
    p.drawLine(4, 10, 8, 14);
    p.drawLine(8, 14, 16, 5);
  } else if (name == "edges") {
    QPainterPath a;
    a.moveTo(3, 15);
    a.cubicTo(5, 2, 8, 19, 11, 7);
    a.cubicTo(13, 1, 15, 9, 17, 3);
    p.drawPath(a);
  } else if (name == "filter") {
    for (int i = 0; i < 3; ++i) {
      p.drawLine(3, 5 + i * 5, 17, 5 + i * 5);
      p.drawEllipse(QRectF(6 + i * 3, 3 + i * 5, 4, 4));
    }
  } else if (name == "flow") {
    p.drawRoundedRect(QRectF(2, 2, 6, 5), 1, 1);
    p.drawRoundedRect(QRectF(12, 13, 6, 5), 1, 1);
    p.drawLine(5, 7, 5, 15);
    p.drawLine(5, 15, 12, 15);
  } else if (name == "layers") {
    QPolygonF a;
    a << QPointF(2, 7) << QPointF(10, 2) << QPointF(18, 7) << QPointF(10, 12);
    p.drawPolygon(a);
    p.drawPolyline(QPolygonF(QVector<QPointF>{{2, 11}, {10, 16}, {18, 11}}));
  } else if (name == "trash") {
    p.drawRect(QRectF(5, 6, 10, 11));
    p.drawLine(3, 5, 17, 5);
    p.drawLine(8, 2, 12, 2);
    p.drawLine(8, 9, 8, 14);
    p.drawLine(12, 9, 12, 14);
  } else if (name == "image" || name == "result") {
    p.drawRoundedRect(QRectF(2, 3, 16, 14), 2, 2);
    p.drawEllipse(QRectF(5, 6, 3, 3));
    QPolygonF a;
    a << QPointF(3, 15) << QPointF(9, 10) << QPointF(13, 13) << QPointF(16, 10);
    p.drawPolyline(a);
    if (name == "result") p.drawLine(13, 2, 18, 7);
  } else if (name == "sun") {
    p.drawEllipse(QRectF(6, 6, 8, 8));
    for (int i = 0; i < 8; ++i) {
      p.save();
      p.translate(10, 10);
      p.rotate(i * 45);
      p.drawLine(0, -7, 0, -9);
      p.restore();
    }
  } else {
    p.drawRoundedRect(QRectF(3, 3, 14, 14), name == "match" ? 0 : 2,
                      name == "match" ? 0 : 2);
    if (name == "match") {
      p.drawLine(7, 10, 13, 10);
      p.drawLine(10, 7, 10, 13);
    }
  }
  p.end();
  pixmap.setDevicePixelRatio(3);
  return QIcon(pixmap);
}
void applyAppearance(QApplication& app, bool dark) {
  ensureStudioUiResources();
  app.setStyle(QStyleFactory::create("Fusion"));
  app.setFont(QFont("Microsoft YaHei UI", 9));
  const QString panel = dark ? "#202632" : "#ffffff",
                bg = dark ? "#171c26" : "#f3f5f9",
                soft = dark ? "#262e3c" : "#f7f8fb",
                line = dark ? "#343e4e" : "#e2e7ef",
                text = dark ? "#e3eaf4" : "#263349",
                muted = dark ? "#9eacc1" : "#5f6c80",
                active = dark ? "#333d66" : "#eef0ff";
  QPalette palette;
  palette.setColor(QPalette::Window, QColor(panel));
  palette.setColor(QPalette::Base, QColor(panel));
  palette.setColor(QPalette::AlternateBase, QColor(soft));
  palette.setColor(QPalette::WindowText, QColor(text));
  palette.setColor(QPalette::Text, QColor(text));
  palette.setColor(QPalette::Button, QColor(panel));
  palette.setColor(QPalette::ButtonText, QColor(text));
  palette.setColor(QPalette::Highlight, QColor("#5366df"));
  palette.setColor(QPalette::HighlightedText, Qt::white);
  app.setPalette(palette);
  app.setStyleSheet(QStringLiteral(R"(
QWidget{color:%1;} QMainWindow{background:%2;} QMenuBar,QToolBar,QDockWidget{background:%3;}
QMenu#workflowNavigationMenu{padding:3px;}
QMenu#workflowNavigationMenu::item{padding:5px 12px 5px 22px;}
QMenu#workflowNavigationMenu::item:non-exclusive{padding-left:12px;}
QMenu#workflowNavigationMenu::indicator{width:12px;height:12px;}
QMenuBar{padding:0;border:0;} QMenuBar::item{padding:3px 8px;} QMenuBar::item:selected{background:%7;}
QToolBar{spacing:6px;padding:4px 10px;border:0;border-bottom:1px solid %4;}
QToolBar#mainToolbar{spacing:6px;padding:7px 12px;}
QToolBar#mainToolbar::separator{background:%4;width:1px;margin:7px 8px;}
 QToolBar#brandToolbar{padding:3px 15px;}
QWidget#appHeader,QWidget#titleBar{background:%3;} QWidget#titleBar{border-bottom:1px solid %4;}
QLabel#brandLogo{font-family:'Segoe UI';font-size:12px;font-weight:600;color:white;background:#5965df;border-radius:7px;}
QLabel#brandTitle{font-size:12px;font-weight:600;color:%1;padding:0 7px 0 2px;} QLabel#projectCaption{font-size:11px;color:%6;padding:0;} QFrame#brandDivider{background:%4;}
QWidget#modeSwitcher{background:%5;border:1px solid %4;border-radius:6px;} QWidget#modeSwitcher QToolButton{padding:0 8px;min-height:0;min-width:0;border:1px solid transparent;}
QWidget#modeSwitcher QToolButton:checked{background:%3;color:#5366df;}
QWidget#modeSwitcher QToolButton:focus{border-color:transparent;}
QWidget#titleBar QToolButton#themeButton{padding:2px;min-width:28px;margin:0 8px;}
QWidget#titleBar QToolButton#themeButton:focus{border:1px solid transparent;}
QToolButton#minimizeWindow,QToolButton#maximizeWindow,QToolButton#closeWindow{border:0;padding:0;border-radius:0;}
QToolButton#closeWindow:hover{background:#e45b64;}
QToolButton,QPushButton{border:1px solid transparent;border-radius:5px;background:transparent;padding:5px 9px;min-height:20px;}
QToolButton:focus,QPushButton:focus{border:1px solid #6575df;}
QToolButton:hover,QPushButton:hover{background:%5;border-color:%4;} QToolButton:checked,QPushButton:checked{color:#5366df;background:%7;}
QPushButton#primaryButton,QPushButton[primary="true"],QToolButton#primaryTool{background:#5366df;color:white;border:0;padding:5px 12px;}
QPushButton#primaryButton:disabled,QPushButton[primary="true"]:disabled,QToolButton#primaryTool:disabled,QPushButton:disabled,QToolButton:disabled{color:#9ca8b8;background:%5;}
QToolBar#mainToolbar QToolButton{padding:0;min-width:0;min-height:0;border:1px solid transparent;border-radius:5px;}
/* Style repolish replaces QWidget minimum sizes: constrain both axes in QSS.
   30px content + two 1px borders yields a square 32px painted button. */
QToolBar#mainToolbar QToolButton[viewToggle="true"]{min-width:30px;max-width:30px;min-height:30px;max-height:30px;padding:0;margin:0;}
QToolBar#mainToolbar QToolButton:focus{border-color:#8793e5;}
QToolBar#mainToolbar QToolButton:disabled{background:transparent;border-color:transparent;}
/* 32px content plus two 1px borders keeps 34px after theme repolish. */
QToolBar#mainToolbar QToolButton#primaryTool{min-height:32px;max-height:32px;}
QToolBar#mainToolbar QToolButton#primaryTool:disabled{background:%5;}
QToolButton#layoutButton::menu-indicator{image:none;width:0;}
QWidget#filmstrip QPushButton{padding:4px;min-width:24px;min-height:24px;}
QToolButton#addDataTypesButton{padding:0;min-width:0;min-height:0;border:0;border-radius:2px;background:transparent;}
QToolButton#addDataTypesButton:hover{background:%5;}
QToolButton#addDataTypesButton::menu-indicator{image:none;width:0;height:0;}
QPushButton#workflowFitButton{padding:0;min-width:0;min-height:0;}
QLabel[filmVerdict="true"]{background:%3;border:1px solid %4;border-radius:3px;padding:0px 2px;font-size:11px;font-weight:600;}
QLabel[filmVerdict="true"][state="OK"]{color:#17845f;}
QLabel[filmVerdict="true"][state="NG"]{color:#c53f47;}
QMainWindow::separator{background:%4;width:7px;height:7px;} QMainWindow::separator:hover{background:#7f8ad1;}
QDockWidget{font-weight:500;} QDockWidget::title{padding:8px 12px;background:%3;border-bottom:1px solid %4;color:%6;}
QTreeWidget{background:%3;border:0;padding:5px;outline:0;} QTreeWidget::item{padding:7px 5px;border-radius:4px;}
QTreeWidget::item:selected{background:%7;color:#5366df;} QTreeWidget::item:hover{background:%5;}
QListWidget#exampleProjectsList{border:1px solid %4;border-radius:5px;background:%3;outline:0;}
QListWidget#exampleProjectsList::item{padding:12px 10px;border-radius:4px;}
QListWidget#exampleProjectsList::item:selected{background:%7;color:#5366df;}
QTextBrowser#exampleProjectGuide{border:1px solid %4;border-radius:5px;background:%3;padding:8px;}
QTabWidget::pane{border:0;border-top:1px solid %4;background:%3;} QTabBar::tab{background:%3;color:%6;padding:9px 12px;border-bottom:2px solid transparent;}
QTabBar::tab:selected{color:#5366df;border-bottom:2px solid #5366df;} QTabBar::base{border:0;}
QWidget#canvasHeader{background:%3;border-bottom:1px solid %4;} QLabel#viewTitle{font-weight:500;padding:8px 14px;}
QLabel#sectionTitle{font-weight:600;} QLabel#secondaryText,QLabel#imageCount{color:%6;font-size:12px;} QLabel#validationMessage{color:#c55259;}
QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox{background:%3;border:1px solid %4;border-radius:5px;padding:5px 7px;min-height:22px;}
QLineEdit:focus,QDoubleSpinBox:focus,QSpinBox:focus,QComboBox:focus{border-color:#8895ec;}
QComboBox{padding-right:27px;}
QComboBox::drop-down{subcontrol-origin:padding;subcontrol-position:top right;width:22px;border:0;}
QComboBox::down-arrow{image:url(:/studio/choice-arrow.png);width:12px;height:12px;}
QWidget#dockGrip{background:%3;border-top:1px solid %4;} QWidget#dockGrip:hover{background:%7;}
QTabBar#centerTabs{background:%3;border-bottom:1px solid %4;} QTabBar#centerTabs::tab{padding:10px 18px;}
QWidget#displayModes{background:%5;border:1px solid %4;border-radius:5px;}
QWidget#displayModes QToolButton{min-height:18px;padding:2px 9px;} QWidget#displayModes QToolButton:checked{background:%3;color:#5366df;}
QLabel#nodeOutputInfo{font-size:11px;color:%6;} QLabel#outputType{font-size:12px;font-weight:500;color:#4b9b89;background:%5;border-radius:3px;padding:3px 7px;}
QLabel#resultState{color:#4b9b89;background:%5;border-radius:3px;padding:4px 7px;font-size:11px;}
QToolButton#advancedParameters,QToolButton#rangeDetailsToggle{text-align:left;padding-left:0;}
QLabel#parameterGroupHeading{font-weight:600;color:%6;padding-top:4px;}
QDoubleSpinBox[invalidParameter="true"],QSpinBox[invalidParameter="true"]{border-color:#ca6d6d;}
QLabel#resultState[state="empty"]{color:%6;} QLabel#resultState[state="running"]{color:#6575dd;}
QLabel#resultState[state="stale"]{color:#b48236;} QLabel#resultState[state="error"]{color:#c46c64;}
QLabel#roiInfo{color:%6;font-size:11px;padding:7px 8px;background:%5;border:1px solid %4;border-radius:5px;}
QWidget#modelToolbar,QWidget#spatialToolbar{background:%3;border-bottom:1px solid %4;}
QWidget#spatialToolbar QComboBox{min-height:18px;padding:3px 7px;}
QFrame#filmFrame{background:transparent;border:0;}
QPushButton[filmPreview="true"]{padding:2px;border:1px solid %4;border-radius:5px;background:%3;}
QPushButton[filmPreview="true"][current="true"]{border:2px solid #6575df;background:%7;}
QPushButton[filmPreview="true"][running="true"]{border:2px solid #cf9a3f;}
QToolButton[batchSelection="true"]{border:1px solid %4;border-radius:4px;background:%3;font-size:11px;padding:3px 4px;}
QToolButton[batchSelection="true"]:checked{border:2px solid #299c85;background:%5;color:%1;}
QWidget#filmstrip{background:%3;border-top:1px solid %4;}
QWidget#viewHeader{background:%3;border-bottom:1px solid %4;}
QLabel#operatorsHeading,QLabel#flowNodesHeading{padding:5px 6px;color:%1;}
QLabel#workbenchTiming{color:%1;padding-left:8px;border-left:1px solid %4;}
QLabel#operatorStatus[state="error"]{color:#c55259;}
QLabel#operatorStatus[state="ok"]{color:#279a83;}
QLabel#operatorStatus[state="running"]{color:#6575df;}
QTableWidget{border:0;background:%3;gridline-color:%4;alternate-background-color:%5;selection-background-color:%7;selection-color:%1;outline:0;}
QHeaderView::section{background:%5;color:%6;border:0;padding:7px 5px;} QTableWidget::item{padding:6px 4px;}
QPlainTextEdit{border:0;background:%3;font-family:'Microsoft YaHei UI';font-size:11px;}
QStatusBar{background:%5;border-top:1px solid %4;font-size:11px;} QStatusBar::item{border:0;}
QSplitter::handle{background:%4;} QSplitter::handle:hover{background:#7f8ad1;}
QScrollArea{border:0;}
QScrollBar:vertical{background:transparent;width:6px;margin:0;}
QScrollBar:horizontal{background:transparent;height:6px;margin:0;}
QScrollBar::handle:vertical{background:rgba(136,151,174,80);border-radius:3px;min-height:24px;}
QScrollBar::handle:horizontal{background:rgba(136,151,174,80);border-radius:3px;min-width:24px;}
QScrollBar::handle:hover{background:rgba(136,151,174,155);}
QScrollBar::add-line,QScrollBar::sub-line{width:0;height:0;border:0;background:transparent;}
QScrollBar::up-arrow,QScrollBar::down-arrow,QScrollBar::left-arrow,QScrollBar::right-arrow{width:0;height:0;image:none;}
QScrollBar::add-page,QScrollBar::sub-page{background:transparent;}
QFrame#viewTools,QFrame#layerTools{background:%3;border:1px solid %4;border-radius:6px;} QFrame#viewTools QToolButton,QFrame#layerTools QToolButton{padding:3px;min-width:24px;min-height:24px;color:%6;}
QFrame#viewTools QToolButton:checked,QFrame#layerTools QToolButton:checked{background:%7;}
QFrame#viewTools QToolButton:disabled,QFrame#layerTools QToolButton:disabled{background:transparent;border-color:transparent;color:%6;}
QFrame#viewTools QToolButton::menu-button{background:transparent;border:0;border-radius:0;width:9px;}
QFrame#viewTools QToolButton::menu-arrow{width:5px;height:5px;}
QLabel#canvasCaption,QLabel#pixelStatus,QLabel#rangeContext{background:%3;color:%6;border:1px solid %4;border-radius:4px;padding:4px 8px;font-size:11px;}
QLabel#pixelStatus{font-family:Consolas;}
)")
                        .arg(text, bg, panel, line, soft, muted, active)
                        .arg(dark ? "#566277" : "#c5cdd9"));
  if (dark)
    app.setStyleSheet(app.styleSheet() + QStringLiteral(R"(
QToolButton:checked,QPushButton:checked,QTreeWidget::item:selected,
QWidget#modeSwitcher QToolButton:checked,QWidget#displayModes QToolButton:checked,
QListWidget#exampleProjectsList::item:selected{color:#c3cdff;}
QTabBar::tab:selected{color:#c3cdff;border-bottom-color:#9dacf9;}
QLabel[filmVerdict="true"][state="OK"]{color:#55c69f;}
QLabel[filmVerdict="true"][state="NG"]{color:#f08087;}
QWidget#viewHeader,QWidget#modelToolbar,QWidget#spatialToolbar{background:#202632;}
)") );
}
}  // namespace workbench
