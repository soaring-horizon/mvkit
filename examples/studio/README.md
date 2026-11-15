# MV Studio 发布版示例

从 MV Studio 的“文件 → 示例工程”打开任意工程，按 F5 运行。

| 机加工件重建工程 | 数据与操作 |
|---|---|
| [机加工件双目重建](synthetic-stereo-part/synthetic-stereo-part.mvproj) | 校正左右图，计算视差、深度和点云 |
| [机加工件结构光重建](synthetic-structured-part/synthetic-structured-part.mvproj) | 解码同一工件的 25 帧有序曝光，重建点云 |

另有 8 个自建基础练习，覆盖计数、亮度、图像处理、定位与尺寸、椭圆、比例换算、双目和结构光。

- `data/synthetic/` 存放自建练习和工件渲染数据。虚拟相机参数、曝光信息和几何真值见 [数据说明](data/synthetic/optical-rig/README.md)。
- 工程通过相对路径读取数据。迁移时复制整套目录，也可以在 Studio 中另存并勾选复制原始数据。
