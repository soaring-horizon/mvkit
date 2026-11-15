#include <calibration/calibration/homography.hpp>
#include <calibration/targets/find_calib_object.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <core/exception.hpp>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

namespace mv::calibration {
namespace {
struct Blob {
  double row{}, col{};
  std::size_t area{};
};

// A projective lattice remains a convex quadrilateral. Recover its four outer
// corners, then associate marks in the predicted lattice, rather than sorting
// image rows (which interleave as soon as a plate is rotated or tilted).
// Forward geometry: convex hull, maximal inscribed quadrilateral, normalized
// DLT.
std::vector<core::Point2d> order_grid(const std::vector<Blob> &blobs,
                                      std::int64_t rows, std::int64_t columns) {
  auto points = blobs;
  std::sort(points.begin(), points.end(), [](const Blob &a, const Blob &b) {
    return a.col == b.col ? a.row < b.row : a.col < b.col;
  });
  auto cross = [](const Blob &a, const Blob &b, const Blob &c) {
    return (b.col - a.col) * (c.row - a.row) -
           (b.row - a.row) * (c.col - a.col);
  };
  std::vector<Blob> hull;
  for (const auto &p : points) {
    while (hull.size() >= 2 &&
           cross(hull[hull.size() - 2], hull.back(), p) <= 0)
      hull.pop_back();
    hull.push_back(p);
  }
  const auto lower = hull.size();
  for (auto it = points.rbegin() + 1; it != points.rend(); ++it) {
    while (hull.size() > lower &&
           cross(hull[hull.size() - 2], hull.back(), *it) <= 0)
      hull.pop_back();
    hull.push_back(*it);
  }
  if (!hull.empty())
    hull.pop_back();
  if (hull.size() < 4)
    throw core::Exception(core::Status::NotFound, "detect_circle_grid_points",
                          "degenerate circle lattice");
  std::array<Blob, 4> corners{};
  double area = -1;
  for (std::size_t a = 0; a + 3 < hull.size(); ++a)
    for (std::size_t b = a + 1; b + 2 < hull.size(); ++b)
      for (std::size_t c = b + 1; c + 1 < hull.size(); ++c)
        for (std::size_t d = c + 1; d < hull.size(); ++d) {
          const auto candidate = cross(hull[a], hull[b], hull[c]) +
                                 cross(hull[a], hull[c], hull[d]);
          if (candidate > area) {
            area = candidate;
            corners = {hull[a], hull[b], hull[c], hull[d]};
          }
        }
  const auto start = std::min_element(corners.begin(), corners.end(),
                                      [](const Blob &a, const Blob &b) {
                                        return a.row + a.col < b.row + b.col;
                                      }) -
                     corners.begin();
  std::rotate(corners.begin(), corners.begin() + start, corners.end());
  std::vector<core::Point2d> best;
  double best_score = std::numeric_limits<double>::infinity();
  // Both axis assignments are tested for rectangular grids. For a square,
  // the positive image-column direction provides a deterministic convention.
  for (int swap = 0; swap < 2; ++swap) {
    std::vector<double> x{0, double(columns - 1), double(columns - 1), 0},
        y{0, 0, double(rows - 1), double(rows - 1)}, u, v;
    for (int k : swap ? std::array<int, 4>{0, 3, 2, 1}
                      : std::array<int, 4>{0, 1, 2, 3}) {
      u.push_back(corners[k].col);
      v.push_back(corners[k].row);
    }
    const auto h = fit_planar_homography_2d(x, y, u, v);
    std::vector<core::Point2d> ordered;
    std::vector<bool> used(blobs.size());
    double score = 0;
    bool valid = true;
    const double spacing =
        std::sqrt(area / (2. * double((rows - 1) * (columns - 1))));
    for (std::int64_t r = 0; r < rows && valid; ++r)
      for (std::int64_t c = 0; c < columns; ++c) {
        const double w = h[6] * c + h[7] * r + h[8],
                     pc = (h[0] * c + h[1] * r + h[2]) / w,
                     pr = (h[3] * c + h[4] * r + h[5]) / w;
        double distance = std::numeric_limits<double>::infinity();
        std::size_t index = blobs.size();
        for (std::size_t i = 0; i < blobs.size(); ++i)
          if (!used[i]) {
            const double d = std::hypot(blobs[i].row - pr, blobs[i].col - pc);
            if (d < distance) {
              distance = d;
              index = i;
            }
          }
        if (index == blobs.size() || distance > .45 * spacing) {
          valid = false;
          break;
        }
        used[index] = true;
        ordered.push_back({blobs[index].row, blobs[index].col});
        score += distance * distance;
      }
    if (valid && (best.empty() || (rows != columns && score < best_score))) {
      best = std::move(ordered);
      best_score = score;
    }
  }
  if (best.empty())
    throw core::Exception(core::Status::NotFound, "detect_circle_grid_points",
                          "circle marks do not form a projective grid");
  return best;
}
} // namespace
std::vector<core::Point2d> detect_circle_grid_points(const core::Image &image,
                                                     std::int64_t rows,
                                                     std::int64_t columns) {
  if (image.empty() || image.pixel_type() != core::PixelType::UInt8)
    throw core::Exception(core::Status::InvalidArgument,
                          "detect_circle_grid_points", "Gray8 image required");
  if (rows < 2 || columns < 2)
    throw core::Exception(core::Status::InvalidArgument,
                          "detect_circle_grid_points", "invalid grid size");
  const auto *p = static_cast<const std::uint8_t *>(image.data());
  std::uint8_t lo = 255, hi = 0;
  for (std::size_t r = 0; r < image.height(); ++r)
    for (std::size_t c = 0; c < image.width(); ++c) {
      auto v = p[r * image.stride() + c];
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
  if (static_cast<int>(hi) - static_cast<int>(lo) < 40)
    throw core::Exception(core::Status::NotFound, "detect_circle_grid_points",
                          "insufficient image contrast");
  const int threshold = (static_cast<int>(lo) + static_cast<int>(hi)) / 2;
  const std::size_t w = image.width(), h = image.height();
  std::vector<std::uint8_t> seen(w * h, 0);
  std::vector<Blob> blobs;
  constexpr int drs[4] = {-1, 1, 0, 0}, dcs[4] = {0, 0, -1, 1};
  for (std::size_t r = 0; r < h; ++r)
    for (std::size_t c = 0; c < w; ++c) {
      const std::size_t idx = r * w + c;
      if (seen[idx] || p[r * image.stride() + c] >= threshold)
        continue;
      std::deque<std::pair<int, int>> q;
      q.emplace_back(static_cast<int>(r), static_cast<int>(c));
      seen[idx] = 1;
      double sr = 0, sc = 0;
      std::size_t area = 0;
      int rmin = int(r), rmax = int(r), cmin = int(c), cmax = int(c);
      while (!q.empty()) {
        auto [rr, cc] = q.front();
        q.pop_front();
        sr += rr;
        sc += cc;
        ++area;
        rmin = std::min(rmin, rr);
        rmax = std::max(rmax, rr);
        cmin = std::min(cmin, cc);
        cmax = std::max(cmax, cc);
        for (int k = 0; k < 4; ++k) {
          int nr = rr + drs[k], nc = cc + dcs[k];
          if (nr < 0 || nc < 0 || nr >= static_cast<int>(h) ||
              nc >= static_cast<int>(w))
            continue;
          std::size_t ni =
              static_cast<std::size_t>(nr) * w + static_cast<std::size_t>(nc);
          if (!seen[ni] && p[static_cast<std::size_t>(nr) * image.stride() +
                             static_cast<std::size_t>(nc)] < threshold) {
            seen[ni] = 1;
            q.emplace_back(nr, nc);
          }
        }
      }
      // Small perspective-foreshortened marks remain valid. Reject the
      // enclosing plate frame by its low bounding-box occupancy instead of its
      // area alone.
      const double occupancy =
          double(area) / (double(rmax - rmin + 1) * double(cmax - cmin + 1));
      if (area >= 5 && area <= 2000 && occupancy >= .35)
        blobs.push_back({sr / area, sc / area, area});
    }
  const std::size_t need = static_cast<std::size_t>(rows * columns);
  if (blobs.size() < need)
    throw core::Exception(core::Status::NotFound, "detect_circle_grid_points",
                          "not enough circle marks");
  std::sort(blobs.begin(), blobs.end(),
            [](const Blob &a, const Blob &b) { return a.area > b.area; });
  if (blobs.size() > need)
    blobs.resize(need);
  return order_grid(blobs, rows, columns);
}
} // namespace mv::calibration
