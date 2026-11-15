// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/style/operator_icons.hpp"
#include "model/document.hpp"
namespace workbench {
QString operatorIconName(Algorithm algorithm) {
  switch(algorithm) {
    case Algorithm::Regions: return QStringLiteral("operator:regions");
    case Algorithm::Edges: return QStringLiteral("operator:edges");
    case Algorithm::Filter: return QStringLiteral("operator:filter");
    case Algorithm::Gray: return QStringLiteral("operator:gray");
    case Algorithm::ShapeMatch: return QStringLiteral("operator:shapematch");
    case Algorithm::NccMatch: return QStringLiteral("operator:nccmatch");
    case Algorithm::ContourFilter: return QStringLiteral("operator:contourfilter");
    case Algorithm::LineFit: return QStringLiteral("operator:linefit");
    case Algorithm::CircleFit: return QStringLiteral("operator:circlefit");
    case Algorithm::Measure: return QStringLiteral("operator:measure");
    case Algorithm::Judge: return QStringLiteral("operator:judge");
    case Algorithm::ContourProcess: return QStringLiteral("operator:contourprocess");
    case Algorithm::EllipseFit: return QStringLiteral("operator:ellipsefit");
    case Algorithm::RegionMorph: return QStringLiteral("operator:regionmorph");
    case Algorithm::RegionFilter: return QStringLiteral("operator:regionfilter");
    case Algorithm::GrayStats:
    case Algorithm::RegionFeatures: return QStringLiteral("operator:regionfeatures");
    case Algorithm::RegionBoolean: return QStringLiteral("operator:regionboolean");
    case Algorithm::ScaleCalibration: return QStringLiteral("operator:scalecalibration");
    case Algorithm::ImageMorph: return QStringLiteral("operator:imagemorph");
    case Algorithm::Polar:
    case Algorithm::ImageAffine: return QStringLiteral("operator:imageaffine");
    case Algorithm::ImageGradient: return QStringLiteral("operator:imagegradient");
    case Algorithm::ImageEnhance: return QStringLiteral("operator:imageenhance");
    case Algorithm::CalibrationDetect: return QStringLiteral("operator:calibrationdetect");
    case Algorithm::MonoCalibration:
    case Algorithm::StereoCalibration:
    case Algorithm::StructuredCalibration: return QStringLiteral("operator:calibrationdetect");
    case Algorithm::ImageRectify: return QStringLiteral("operator:imagerectify");
    case Algorithm::PlaneMeasure: return QStringLiteral("operator:planemeasure");
    case Algorithm::StereoRectify: return QStringLiteral("operator:stereorectify");
    case Algorithm::StereoDisparity: return QStringLiteral("operator:stereodisparity");
    case Algorithm::StereoReconstruct: return QStringLiteral("operator:stereoreconstruct");
    case Algorithm::CloudSelect: return QStringLiteral("operator:cloudselect");
    case Algorithm::PlaneFit: return QStringLiteral("operator:planemeasure");
    case Algorithm::SphereFit:
    case Algorithm::CloudMeasure: return QStringLiteral("operator:cloudmeasure");
    case Algorithm::StereoImage: return QStringLiteral("operator:stereoimage");
    case Algorithm::DisparityFilter: return QStringLiteral("operator:disparityfilter");
    case Algorithm::StructuredDecode: return QStringLiteral("operator:structureddecode");
    case Algorithm::StructuredReconstruct: return QStringLiteral("operator:structuredreconstruct");
    case Algorithm::StructuredImage: return QStringLiteral("operator:structuredimage");
    case Algorithm::ImageInput: return QStringLiteral("operator:imageinput");
    case Algorithm::SequenceInput: return QStringLiteral("operator:sequenceinput");
  }
  return QStringLiteral("layers");
}
} // namespace workbench
