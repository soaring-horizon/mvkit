// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "operators/operator_registry.hpp"

#include <stdexcept>
namespace workbench {
const QVector<OperatorDefinition>& operatorRegistry() {
  static const QVector<OperatorDefinition> tools{
      {Algorithm::Regions,
       "regions",
       QStringLiteral("区域分割"),
       QStringLiteral("分割与区域"),
       QStringLiteral(
           "固定、全局自动或局部动态阈值分割，可配置开运算和面积筛选；输出独立连通区域。"),
       "Region",
       {"Threshold", "BinaryThreshold", "DynThreshold", "OpeningCircle", "Connection", "SelectShape"}},
      {Algorithm::Edges,
       "edges",
       QStringLiteral("边缘提取"),
       QStringLiteral("边缘与轮廓"),
       QStringLiteral(
           "Canny 亚像素轮廓；ROI 约束处理范围，长度和坐标保留原图单位。"),
       "XLD",
       {"EdgesSubPix"}},
      {Algorithm::Filter,
       "filter",
       QStringLiteral("图像滤波"),
       QStringLiteral("图像处理"),
       QStringLiteral(
           "按配置选用均值、高斯或中值滤波；原图保持不变，输出结果图层。"),
       "Image",
       {"MeanImage", "GaussFilter", "MedianImage"}},
      {Algorithm::Gray,
       "gray",
       QStringLiteral("灰度处理"),
       QStringLiteral("图像处理"),
       QStringLiteral("灰度转换或灰度反相，按处理意图选择配置。"),
       "Image",
       {"Rgb1ToGray", "InvertImage"}},
      {Algorithm::ShapeMatch,
       "shape",
       QStringLiteral("形状匹配"),
       QStringLiteral("模板定位"),
       QStringLiteral("独立定位工具。选择模板图像，或以当前 ROI "
                      "建模；返回位置、角度和得分。"),
       "Match",
       {"CreateShapeModel", "FindShapeModel"}},
      {Algorithm::NccMatch,
       "ncc",
       QStringLiteral("灰度匹配"),
       QStringLiteral("模板定位"),
       QStringLiteral("独立 NCC 定位工具。选择模板图像，或以当前 ROI "
                      "建模；不与轮廓匹配混为一个算子。"),
       "Match",
       {"CreateNccModel", "FindNccModel"}},
      {Algorithm::ContourFilter,
       "contour_filter",
       QStringLiteral("轮廓筛选"),
       QStringLiteral("边缘与轮廓"),
       QStringLiteral("按长度、闭合性筛选并排序；保留原始亚像素坐标。"),
       "XLD",
       {"LengthXld", "SelectObj"}},
      {Algorithm::LineFit,
       "line_fit",
       QStringLiteral("直线拟合"),
       QStringLiteral("边缘与轮廓"),
       QStringLiteral("逐轮廓拟合有限端点及无限直线，输出长度和残差。"),
       "Line",
       {"FitLineContourXld"}},
      {Algorithm::CircleFit,
       "circle_fit",
       QStringLiteral("圆拟合"),
       QStringLiteral("边缘与轮廓"),
       QStringLiteral("逐轮廓拟合圆，输出圆心、半径、直径及残差。"),
       "Circle",
       {"FitCircleContourXld"}},
      {Algorithm::Measure,
       "measure",
       QStringLiteral("几何测量"),
       QStringLiteral("测量与判定"),
       QStringLiteral(
           "按命名输入计算直径、中心距、圆心到线距离、圆边间隙及夹角。"),
       "Value",
       {"DistancePp", "DistancePl", "AngleLl"}},
      {Algorithm::Judge,
       "judge",
       QStringLiteral("公差判定"),
       QStringLiteral("测量与判定"),
       QStringLiteral(
           "对选定数值执行包含端点的上下限判定；NG 与算法执行失败分开。"),
       "Verdict",
       {}},
      {Algorithm::ContourProcess,
       "contour_process",
       QStringLiteral("轮廓处理"),
       QStringLiteral("边缘与轮廓"),
       QStringLiteral("平滑、分段、相邻/共线/共圆合并，按操作配置。"),
       "XLD",
       {"SmoothContoursXld", "SegmentContoursXld", "UnionAdjacentContoursXld",
        "UnionCollinearContoursXld", "UnionCocircularContoursXld"}},
      {Algorithm::EllipseFit,
       "ellipse_fit",
       QStringLiteral("椭圆拟合"),
       QStringLiteral("边缘与轮廓"),
       QStringLiteral("输出椭圆中心、长短轴、方向和采样残差。"),
       "Ellipse",
       {"FitEllipseContourXld"}},
      {Algorithm::RegionMorph,
       "region_morph",
       QStringLiteral("区域形态学"),
       QStringLiteral("分割与区域"),
       QStringLiteral("圆/矩形结构元素的开闭、膨胀腐蚀及填孔、连通。"),
       "Region",
       {"OpeningCircle", "ClosingCircle", "DilationCircle", "ErosionCircle",
        "FillUp", "Connection"}},
      {Algorithm::RegionFilter,
       "region_filter",
       QStringLiteral("区域筛选"),
       QStringLiteral("分割与区域"),
       QStringLiteral("按面积、包围框、圆度筛选并排序。"),
       "Region",
       {"AreaCenter", "Circularity", "SelectObj"}},
      {Algorithm::RegionFeatures,
       "region_features",
       QStringLiteral("区域特征"),
       QStringLiteral("分割与区域"),
       QStringLiteral("计数或逐区域面积、宽高、圆度，空集合数量为零。"),
       "Value",
       {"AreaCenter", "SmallestRectangle1", "SmallestRectangle2", "Circularity"}},
      {Algorithm::RegionBoolean,
       "region_boolean",
       QStringLiteral("区域集合运算"),
       QStringLiteral("分割与区域"),
       QStringLiteral("合并 A、A/B 并交差及对称差，保留真实有效域。"),
       "Region",
       {"Union1", "Union2", "Intersection", "Difference", "SymmDifference"}},
      {Algorithm::ScaleCalibration,
       "scale_calibration",
       QStringLiteral("像素比例换算"),
       QStringLiteral("测量与判定"),
       QStringLiteral("等比例平面条件下，以已知长度将像素长度换算为毫米。"),
       "Value",
       {}},
      {Algorithm::ImageMorph,
       "image_morph",
       QStringLiteral("灰度形态学"),
       QStringLiteral("图像处理"),
       QStringLiteral("矩形灰度腐蚀、膨胀、开闭、白/黑顶帽，保留实际像素。"),
       "Image",
       {"GrayErosionRect", "GrayDilationRect", "GrayOpeningRect",
        "GrayClosingRect"}},
      {Algorithm::ImageAffine,
       "image_affine",
       QStringLiteral("仿射变换"),
       QStringLiteral("图像处理"),
       QStringLiteral("正向或逆向变换，支持平移、旋转、缩放、切变；固定画布。"),
       "Image",
       {"AffineTransImage", "HomMat2dInvert", "AffineTransRegion"}},
      {Algorithm::ImageGradient,
       "image_gradient",
       QStringLiteral("梯度提取"),
       QStringLiteral("图像处理"),
       QStringLiteral(
           "Sobel 幅值、高斯一阶导数和 LoG，真实有符号结果不转显示值。"),
       "Image",
       {"SobelAmp", "DerivateGauss", "LaplaceOfGauss"}},
      {Algorithm::ImageEnhance,
       "image_enhance",
       QStringLiteral("图像增强"),
       QStringLiteral("图像处理"),
       QStringLiteral("线性灰度变换或局部锐化，方法作为配置。"),
       "Image",
       {"ScaleImage", "Emphasize"}},
      {Algorithm::CalibrationDetect,
       "calibration_detect",
       QStringLiteral("标定板检测"),
       QStringLiteral("标定与校正"),
       QStringLiteral("按描述文件检测标记点，输出实际检测中心和索引。"),
       "Point",
       {"FindCalibObject", "GetCalibDataObservPoints"}},
      {Algorithm::ImageRectify,
       "image_rectify",
       QStringLiteral("单目图像校正"),
       QStringLiteral("标定与校正"),
       QStringLiteral("加载冻结标定文件，去畸变或世界平面重采样。"),
       "Image",
       {"ChangeRadialDistortionImage", "ImageToWorldPlane"}},
      {Algorithm::PlaneMeasure,
       "plane_measure",
       QStringLiteral("平面测量"),
       QStringLiteral("测量与判定"),
       QStringLiteral("原始像素中心通过相机与平面位姿计算毫米中心距。"),
       "Value",
       {"ImagePointsToWorldPlane"}},
      {Algorithm::StereoRectify,
       "stereo_rectify",
       QStringLiteral("双目校正"),
       QStringLiteral("双目视觉"),
       QStringLiteral("输入左图、右图及双目标定，分别输出校正左图、校正右图和校正几何模型。"),
       "StereoPair",
       {"GenBinocularRectificationMap", "MapImage"}},
      {Algorithm::StereoDisparity,
       "stereo_disparity",
       QStringLiteral("视差计算"),
       QStringLiteral("双目视觉"),
       QStringLiteral("两个单通道图像输入，分别输出 SGM 视差图和匹配代价。"),
       "Disparity",
       {"BinocularDisparityMs"}},
      {Algorithm::StereoReconstruct,
       "stereo_reconstruct",
       QStringLiteral("双目重建"),
       QStringLiteral("双目视觉"),
       QStringLiteral("XYZ、深度及保留像素对应的点云。"),
       "PointCloud",
       {"DisparityImageToXyz", "XyzToObjectModel3d"}},
      {Algorithm::CloudSelect,
       "cloud_select",
       QStringLiteral("点云选取"),
       QStringLiteral("点云处理"),
       QStringLiteral("校正左图二维 ROI 与毫米深度区间选点。"),
       "PointCloud",
       {}},
      {Algorithm::CloudMeasure,
       "cloud_measure",
       QStringLiteral("三维测量"),
       QStringLiteral("点云处理"),
       QStringLiteral("深度范围或三维两点距离，单位 mm。"),
       "Value",
       {}},
      {Algorithm::StereoImage,
       "stereo_image",
       QStringLiteral("双目图像提取"),
       QStringLiteral("双目视觉"),
       QStringLiteral("显式提取校正左图或右图供二维算子处理，保留像素坐标系。"),
       "Image",
       {}},
      {Algorithm::DisparityFilter,
       "disparity_filter",
       QStringLiteral("视差过滤"),
       QStringLiteral("双目视觉"),
       QStringLiteral("按 SGM 匹配代价上限和有效连通面积筛选视差；代价越小越优，不是概率。"),
       "Disparity",
       {"Threshold", "Connection", "SelectShape"}},
      {Algorithm::StructuredDecode, "structured_decode", QStringLiteral("结构光解码"),
       QStringLiteral("结构光"),
       QStringLiteral("完整有序曝光序列与冻结模型解码，输出投影坐标、对比度、有效域和质量。"),
       "Correspondence", {"DecodeStructuredLightPattern", "GetStructuredLightObject"}},
      {Algorithm::StructuredReconstruct, "structured_reconstruct", QStringLiteral("结构光重建"),
       QStringLiteral("结构光"),
       QStringLiteral("使用模型内完整相机/投影仪标定三角化，保留相机像素映射；点云为 mm。"),
       "PointCloud", {"ReconstructSurfaceStructuredLight", "ObjectModel3dToXyz"}},
      {Algorithm::StructuredImage, "structured_image", QStringLiteral("结构光分量提取"),
       QStringLiteral("结构光"),
       QStringLiteral("提取相机参考图、投影坐标或质量图供二维处理；保留相机坐标系。"),
       "Image", {}},
      {Algorithm::ImageInput, "image_input", QStringLiteral("图像输入"),
       QStringLiteral("数据输入"), QStringLiteral("引用当前样本的一列图像；多个输入在同一次样本运行中同步绑定。"), "Image", {}},
      {Algorithm::SequenceInput, "sequence_input", QStringLiteral("序列输入"),
         QStringLiteral("数据输入"), QStringLiteral("引用当前样本的一组有序图像，整组作为一次输入；可用于单目标定或结构光曝光。"), "ImageSequence", {}},
      {Algorithm::MonoCalibration,"mono_calibration",QStringLiteral("单目标定"),QStringLiteral("标定与校正"),
         QStringLiteral("使用当前序列中不同板姿态的图像，求解单相机内参和世界平面；一条序列就是一组标定图。"),"Calibration",
       {"CreateCalibData","FindCalibObject","CalibrateCameras","GetCalibData"}},
      {Algorithm::StereoCalibration,"stereo_calibration",QStringLiteral("双目标定"),QStringLiteral("标定与校正"),
       QStringLiteral("使用完整左右图样本联合标定两台相机；输出内参、相对位姿及重投影误差。"),"Calibration",
       {"CreateCalibData","FindCalibObject","CalibrateCameras","GetCalibData"}},
      {Algorithm::StructuredCalibration,"structured_calibration",QStringLiteral("结构光标定"),QStringLiteral("标定与校正"),
       QStringLiteral("双方向曝光解码后，联合标定相机与投影仪；每套曝光对应一个标定板姿态。"),"Calibration",
       {"DecodeStructuredLightPattern","GetStructuredLightObject","SetCalibDataObservPoints","CalibrateCameras","GetCalibData","SetStructuredLightModelParam"}},
      {Algorithm::GrayStats, "gray_stats", QStringLiteral("灰度统计"), QStringLiteral("测量与判定"),
       QStringLiteral("在图像有效范围内测量亮度均值、标准差、极值或极差，输出数值用于判定。"), "Value", {"Intensity", "MinMaxGray"}},
      {Algorithm::Polar, "polar", QStringLiteral("极坐标变换"), QStringLiteral("图像处理"),
       QStringLiteral("按圆心、角度及半径展开环形图像，横轴为角度、纵轴为半径。"), "Image", {"PolarTransImageExt"}},
      {Algorithm::SphereFit, "sphere_fit", QStringLiteral("球体拟合"), QStringLiteral("点云处理"),
       QStringLiteral("对选取的三维点拟合球体，测量球心、半径、直径和径向残差。"), "Value", {"FitPrimitivesObjectModel3d","GetObjectModel3dParams"}},
      {Algorithm::PlaneFit, "plane_fit", QStringLiteral("平面拟合"), QStringLiteral("点云处理"),
       QStringLiteral("以正交最小二乘拟合点云的基准平面，输出单位法向、平面偏移与残差，可直接连接三维测量或公差判定。"),
       "Plane3D", {"FitPrimitivesObjectModel3d","GetObjectModel3dParams"}}};
  return tools;
}
const OperatorDefinition& operatorDefinition(Algorithm id) {
  for (const auto& tool : operatorRegistry())
    if (tool.id == id) return tool;
  throw std::runtime_error("未知软件算子");
}
QStringList operatorAliases(Algorithm id) {
  switch (id) {
    case Algorithm::ShapeMatch: return {QStringLiteral("轮廓匹配")};
    case Algorithm::ImageMorph: return {QStringLiteral("图像形态学")};
    case Algorithm::ScaleCalibration: return {QStringLiteral("比例标定")};
    case Algorithm::StereoImage: return {QStringLiteral("图像分量")};
    case Algorithm::StructuredImage: return {QStringLiteral("结构光图像")};
    case Algorithm::RegionBoolean: return {QStringLiteral("区域集合")};
    case Algorithm::Edges: return {QStringLiteral("亚像素边缘提取")};
    case Algorithm::ImageRectify: return {QStringLiteral("图像校正")};
    default: return {};
  }
}
QStringList workflowNodeNames(const QVector<Parameters>& steps,
                              const QStringList& aliases) {
  QStringList names;
  for (int i = 0; i < steps.size(); ++i) {
    const auto base = xyzInput(steps[i]) ? QStringLiteral("XYZ 输入") : multiViewInput(steps[i]) ? QStringLiteral("多视输入") : operatorDefinition(steps[i].algorithm).name;
    auto candidate = aliases.value(i).trimmed();
    for (const auto& old : operatorAliases(steps[i].algorithm))
      if (candidate == old || candidate.startsWith(old + "-")) candidate = base + candidate.mid(old.size());
    if(steps[i].algorithm==Algorithm::ImageInput) {
      const auto old=multiViewInput(steps[i])?QStringLiteral("图像输入"):QStringLiteral("多视输入");
      if(candidate==old || candidate.startsWith(old+"-"))candidate=base+candidate.mid(old.size());
    }
    if (!candidate.isEmpty() && candidate != base &&
        !candidate.startsWith(base + "-"))
      candidate = base + "-" + candidate;
    if (candidate.isEmpty()) {
      bool previous = false;
      for (int j = 0; j < i; ++j)
        previous |= steps[j].algorithm == steps[i].algorithm;
      candidate = previous ? base + "-1" : base;
    }
    auto unique = candidate;
    int suffix = 1;
    while (names.contains(unique))
      unique = base + QStringLiteral("-%1").arg(suffix++);
    names.append(unique);
  }
  return names;
}
Algorithm algorithmFromKey(const QString& key) {
  if (key == "appearance")
    throw std::runtime_error("工程包含已移除的外观比较算子，请移除该节点后使用");
  for (const auto& tool : operatorRegistry())
    if (tool.key == key) return tool.id;
  throw std::runtime_error("工程包含未知软件算子");
}
}  // namespace workbench
