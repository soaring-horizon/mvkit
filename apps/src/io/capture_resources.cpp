// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "model/spatial_data.hpp"
#include <QFileInfo>
#include <QJsonArray>
#include <QThread>
#include <filesystem>
#include <stdexcept>
namespace workbench {
namespace {
void require(bool valid, const char* error) {
  if (!valid) throw std::runtime_error(error);
}
}
void publishCaptureDirectory(const QString& source, const QString& destination) {
  require(!QFileInfo::exists(destination), "目标采集目录已存在");
  std::error_code error;
  for (int attempt = 0; attempt < 5; ++attempt) {
    // Windows scanners can briefly hold a just-written PNG/JSON handle.
    // Retry only the atomic rename, never expose a partially copied package.
    std::filesystem::rename(std::filesystem::u8path(source.toUtf8().constData()),
                            std::filesystem::u8path(destination.toUtf8().constData()), error);
    if (!error) return;
    if (attempt < 4) QThread::msleep(10u << attempt);
  }
  throw std::runtime_error((QStringLiteral("采集目录发布失败：") +
      QString::fromLocal8Bit(error.message().c_str())).toUtf8().constData());
}
void validateStereoCapture(const Document& d, const QByteArray& calibration) {
  const auto j = calibration.isEmpty() ? QJsonObject() : stereoCalibration(calibration);
  require(d.image.CountObj() == 1 && d.rightImage.CountObj() == 1,
          "采集组缺少左右图像");
  require(d.image.Width() == d.rightImage.Width() &&
              d.image.Height() == d.rightImage.Height() &&
              d.image.Width() * d.image.Height() <= 1000000,
          "左右图须同尺寸，每组最多 100 万像素");
  int i = 0;
  for (const auto* image : {&d.image, &d.rightImage}) {
    require(image->Channels() == 1 || image->Channels() == 3, "采集图须为灰度或 RGB");
    for (size_t c = 0; c < image->Channels(); ++c)
      require(image->Type(c) == mv::MvPixelType::UInt8, "采集图须为原始 byte 数据");
    const auto camera = j[i++ == 0 ? "leftCamera" : "rightCamera"].toArray();
    require(calibration.isEmpty() || (image->Width() == camera[7].toInt() && image->Height() == camera[8].toInt()),
            "图像规格与双目标定不匹配，未修改采集组");
  }
}
} // namespace workbench
