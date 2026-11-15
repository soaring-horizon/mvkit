# MV Studio 运行包

启动 `mv_studio.exe`。`mv_studio_worker.exe` 是后台计算进程，须与主程序放在同一目录。
请完整保留 DLL、`platforms`、`studio-examples` 和 `third-party`，不要仅复制主 EXE。

通过“文件 → 示例工程”打开随包的 10 个工程，按 F5 运行当前数据。
机加工件重建示例使用的虚拟相机、投影仪和几何真值位于 `studio-examples/data/synthetic/optical-rig`。

运行环境为 Windows x64，需安装不低于编译工具集版本的
[Microsoft Visual C++ v14 x64 运行库](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/)。
Qt 为 5.15.2，采用独立 DLL；通知、原始许可及源码获取说明见 `third-party/THIRD_PARTY.md`。
项目许可证见 `LICENSE` 和 `NOTICE`。

`contents.txt` 列出本次打包文件。重新打包会清理并重建此目录，请将自己的工程另存到其他目录。
