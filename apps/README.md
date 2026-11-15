# MV Studio

MV Studio 是 MVKit 的桌面视觉工作台。用流程图连接图像、区域、轮廓、模板匹配结果、标定模型和点云。节点参数、数据管理、2D 与 3D 结果在同一个工程中。

1. 在 Windows 运行包中启动 `mv_studio.exe`，保持 `mv_studio_worker.exe` 及随包目录位于原位置。
2. 从“文件 → 示例工程”打开一个示例。按 F5 运行当前数据。
3. 选中节点查看结果，切换命名输入和输出。选中带有效点云的节点会自动打开 3D 视图。
4. 调整参数或 ROI 后，使用“另存工程”保留自己的副本。勾选复制原始数据可将输入素材一起带走。

随包的 10 个示例涵盖二维检测、定位与尺寸测量、双目与结构光重建。工程数据说明见 [Studio 示例目录](../examples/studio/README.md)。第三方依赖见 [许可与来源](THIRD_PARTY.md)。

在 Windows x64 从源码构建时，使用 Qt 5.15.2 的 `msvc2019_64` SDK。VS2026 需要 CMake 4.2 或更高版本。

```powershell
cmake --preset windows-studio "-DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64"
cmake --build --preset windows-studio-package --parallel 4
```

运行包输出到 `build/studio/package/Release/MVStudio`。VS2022 和 VS2019 可分别使用 `windows-2022-studio`、`windows-2019-studio` 及对应的 `-package` 构建预设。
