# 工作台第三方依赖

工作台与 MVKit 自有代码继续采用根目录的 Apache-2.0。Qt 未进入 MVKit 核心库依赖。

## Qt 5.15.2

- 作者：The Qt Company Ltd. 及 Qt 源文件中列明的其他贡献者。
- SDK：官方 `qt.qt5.5152.win64_msvc2019_64` 预编译包，来自 <https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt5_5152/>。
- 本工具只动态链接 Qt Core、Gui、Widgets，并加载对应平台插件；开源构建按 LGPL v3 使用这些模块。原始许可在 `qtbase-5.15.2/LICENSE.LGPL3` 与 `LICENSE.GPL3`；保留的其他许可仅为上游归档原文，并不改变 MVKit 许可证。
- 对应源码：<https://download.qt.io/archive/qt/5.15/5.15.2/submodules/qtbase-everywhere-src-5.15.2.tar.xz>，官方 SHA256 `909fad2591ee367993a75d7e2ea50ad4db332f05e1c38dd7a5a274e156a4e0f8`。
- 本次未修改 Qt 库源码或运行时 DLL。aqtinstall 3.3.0 仅用于下载和修正 SDK 中的本机安装路径，不随应用运行，也不作为源码仓库依赖。
- `qtbase-5.15.2/src/3rdparty` 保留该版本 QtBase 的第三方 attribution 与引用的许可文件。它包含可选实现的通知，不表示应用启用了其中每一项。

程序使用独立 Qt DLL，允许用接口兼容的修改版本替换；请保持 Core、Gui、Widgets、平台插件的一致性。应用源码和构建步骤在本仓库内。修改 Qt 时另行保存对应源码与修改记录；对外分发 Qt 二进制时，连同原始许可、版权通知和对应源码一起提供或按上游许可履行源码提供要求。

没有引入 Charts、Virtual Keyboard、WebEngine、QML、商业插件或 HALCON 随附的私有 Qt 运行库。

## Windows 运行文件

`build/bin/Release` 中的 `mv_studio.exe`、`mv_studio_worker.exe`、`mvkit.dll`、`mvkitcpp.dll` 均由本工作树构建。CMake 从选定 SDK 复制 `Qt5Core.dll`、`Qt5Gui.dll`、`Qt5Widgets.dll` 和 `platforms/qwindows.dll`。本应用以 MVKit 公开图像读写接口解码图片，无需部署 Qt 图片格式插件。

运行环境需要匹配架构的 Microsoft Visual C++ 运行库。部署使用 Microsoft Visual C++ v14 x64 运行库。若随后改用 `windeployqt` 或调整 Qt 配置，需同步更新实际随包文件与通知。

MVKit 自身的 Eigen、libjpeg-turbo、libpng、zlib、libtiff 等许可仍见根 README 的第三方组件说明及相应源码目录；它们不由 Qt 的许可证替代。Qt 测试工具所需平台插件也保留相同 Qt 许可。

## Microsoft Fluent UI System Icons

当前多数工具栏、视图操作与 37 类算子使用官方 Microsoft Fluent UI System Icons 的 20px Regular 原件，MIT 许可，Copyright (c) Microsoft Corporation。固定提交为 `9eb184e96268d3293462f9d9d19225b4c7b07774`；完整许可、原 SVG、逐文件来源/散列及语义映射保留在 `fluentui-system-icons/`，随应用复制。

源 SVG 由 `apps/devtools/generate_fluent_icons.py` 使用开发工具 fontTools 4.55.3 离线转为 QPainterPath，保留官方填充/镂空规则，再以当前状态色绘制；“运行当前”和“运行所选”按用户要求组合底线/环形播放。没有新增 Qt 模块、图像插件或运行时 Python/字体依赖，MVKit 自有代码的 Apache-2.0 保持。

## Lucide Icons 选定工具栏子集

按用户选择，仅二维视图、撤销、重做及布局使用 `image`、`undo-2`、`redo-2`、`panels-top-left`。固定提交 `a630cec77eac6568b989e87e4947a8bfe45caacd`，来源、逐文件 SHA-256、完整上游 ISC 许可（含其保留的 Feather MIT 通知）位于 `lucide-icons/` 并随应用复制。其余工具栏、3D和37类算子继续使用原 Fluent 资源。

`apps/devtools/generate_lucide_toolbar_icons.py` 由官方 SVG 生成 `QPainterPath`，使用圆端/圆角、无填充、24px源网格1.5线宽后缩放至20px，与用户选定预览一致。只改变绘制外观，没有新增运行依赖或更改 MVKit Apache-2.0 许可。
