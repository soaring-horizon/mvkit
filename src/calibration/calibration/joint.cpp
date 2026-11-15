// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
// Forward implementation of two-device bundle adjustment. The objective is
// sum ||project(K_i, T_reference_to_i * T_board_to_reference * X)-x_i||^2.
// Camera 0 fixes the gauge; camera 1 and all shared board poses are optimized.
// Public basis: MVTec calibrate_cameras / get_calib_data operator descriptions
// and the standard reprojection bundle-adjustment least-squares formulation.
#include <algorithm>
#include <calibration/calibration/quality.hpp>
#include <calibration/camera_model.hpp>
#include <calibration/geometry3d.hpp>
#include <calibration/model.hpp>
#include <calibration/solver/manifold.hpp>
#include <cmath>
#include <map>
#include <memory>

namespace mv::calibration {
namespace {
using Id = solver::ParameterBlockId;
using Board = std::pair<std::int64_t, std::int64_t>;
Pose3d pose(std::span<const double> v) {
  return {{v[0], v[1], v[2]}, normalize_quaternion({v[3], v[4], v[5], v[6]})};
}
solver::ParameterBlock pose_block(Id id, const Pose3d &p) {
  const auto q = normalize_quaternion(p.rotation);
  return {
      id,
      {p.translation.x, p.translation.y, p.translation.z, q.w, q.x, q.y, q.z},
      6,
      solver::ManifoldKind::PoseSE3,
      false,
      {},
      {}};
}
class JointResidual final : public solver::ResidualFunction {
public:
  JointResidual(CameraParameters camera, std::vector<IntrinsicBinding> bindings,
                Id board, Id device, std::vector<Point3d> points,
                std::vector<double> rows, std::vector<double> columns)
      : camera_(std::move(camera)), bindings_(std::move(bindings)),
        points_(std::move(points)), rows_(std::move(rows)),
        columns_(std::move(columns)), device_(device != 0) {
    for (const auto &b : bindings_)
      ids_.push_back(b.id);
    ids_.push_back(board);
    if (device_)
      ids_.push_back(device);
  }
  std::size_t residual_size() const noexcept override {
    return points_.size() * 2;
  }
  std::span<const Id> parameter_blocks() const noexcept override {
    return ids_;
  }
  core::Status evaluate(std::span<const solver::ParameterBlockView> p,
                        std::span<double> r,
                        std::span<solver::JacobianView> j) const override {
    try {
      if (p.size() != ids_.size() || r.size() != residual_size())
        return core::Status::InvalidArgument;
      values(p, r);
      if (j.empty())
        return core::Status::Ok;
      std::vector<solver::ParameterBlockView> perturbed(p.begin(), p.end());
      std::vector<double> plus(r.size()), minus(r.size());
      for (std::size_t k = 0; k < p.size(); ++k) {
        solver::ParameterBlock block{p[k].id,
                                     {p[k].values.begin(), p[k].values.end()},
                                     p[k].tangent_size,
                                     p[k].manifold,
                                     false,
                                     {},
                                     {}};
        for (std::size_t c = 0; c < p[k].tangent_size; ++c) {
          const double h = p[k].manifold == solver::ManifoldKind::Euclidean
                               ? 1e-7 * std::max(1., std::abs(p[k].values[c]))
                               : 1e-7;
          std::vector<double> delta(p[k].tangent_size, 0.);
          delta[c] = h;
          block.values.assign(p[k].values.begin(), p[k].values.end());
          if (solver::apply_local_delta(block, delta) != core::Status::Ok)
            return core::Status::InvalidState;
          perturbed[k].values = block.values;
          values(perturbed, plus);
          block.values.assign(p[k].values.begin(), p[k].values.end());
          delta[c] = -h;
          if (solver::apply_local_delta(block, delta) != core::Status::Ok)
            return core::Status::InvalidState;
          perturbed[k].values = block.values;
          values(perturbed, minus);
          for (std::size_t row = 0; row < r.size(); ++row)
            j[k](row, c) = (plus[row] - minus[row]) / (2 * h);
        }
        perturbed[k] = p[k];
      }
      return core::Status::Ok;
    } catch (const core::Exception &e) {
      return e.status();
    }
  }

private:
  void values(std::span<const solver::ParameterBlockView> p,
              std::span<double> r) const {
    auto camera = camera_;
    for (std::size_t i = 0; i < bindings_.size(); ++i)
      set_camera_parameter_value(camera, bindings_[i].name,
                                 p[i].values[0] * bindings_[i].scale);
    auto transform = pose(p[bindings_.size()].values);
    if (device_)
      transform = compose_pose(pose(p.back().values), transform);
    for (std::size_t i = 0; i < points_.size(); ++i) {
      const auto projected =
          project_camera_point(camera, transform_point(transform, points_[i]));
      r[2 * i] = projected.row - rows_[i];
      r[2 * i + 1] = projected.col - columns_[i];
    }
  }
  CameraParameters camera_;
  std::vector<IntrinsicBinding> bindings_;
  std::vector<Id> ids_;
  std::vector<Point3d> points_;
  std::vector<double> rows_, columns_;
  bool device_;
};
} // namespace

void calibrate_two_cameras(CalibrationDataModel &data, double *rmse) {
  // Independent fits initialize the joint problem only. The returned solution
  // is a shared optimization, never an average of independent final poses.
  std::vector<CalibrationDataModel> initial;
  for (int device = 0; device < 2; ++device) {
    const auto &configured = data.camera(device);
    if (!configured.parameters || !is_perspective(*configured.parameters))
      throw core::Exception(
          core::Status::Unsupported, "calibrate_cameras",
          "two-device calibration requires perspective area-scan cameras");
    for (const auto *s :
         {"transx", "transy", "transz", "alpha", "beta", "gamma"})
      if (device == 1 && configured.excluded_settings.count(s))
        throw core::Exception(
            core::Status::Unsupported, "calibrate_cameras",
            "fixed camera-1 pose components are not supported");
    auto single = CalibrationDataModel::create(
        "calibration_object", 1,
        static_cast<std::int64_t>(data.num_calibration_objects()));
    single.options() = data.options();
    single.camera(0) = configured;
    for (std::size_t i = 0; i < data.num_calibration_objects(); ++i)
      single.target(i) = data.target(i);
    for (const auto &[key, observation] : data.observations())
      if (key.camera_index == device)
        single.replace_observation({0, key.calib_object_index, key.pose_index},
                                   observation);
    double error;
    try {
      calibrate_cameras(single, &error);
    } catch (const core::Exception &e) {
      throw core::Exception(e.status(), "calibrate_cameras",
                            "camera " + std::to_string(device) + " initialization: " + std::string(e.message()));
    }
    initial.push_back(std::move(single));
  }
  std::vector<Pose3d> relative;
  for (const auto &[key, left] : initial[0].calibration_result()->poses) {
    const auto it = initial[1].calibration_result()->poses.find(key);
    if (it != initial[1].calibration_result()->poses.end())
      relative.push_back(compose_pose(
          it->second, inverse_pose(left))); // reference -> device 1
  }
  if (relative.size() < 3)
    throw core::Exception(core::Status::InvalidState, "calibrate_cameras",
                          "at least three shared board poses are required");
  Pose3d extrinsic = relative.front();
  Point3d t{};
  Quaterniond q{0, 0, 0, 0};
  for (const auto &p : relative) {
    t.x += p.translation.x;
    t.y += p.translation.y;
    t.z += p.translation.z;
    const auto a = p.rotation;
    const auto b = relative.front().rotation;
    const double sign =
        a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z < 0 ? -1. : 1.;
    q.w += sign * a.w;
    q.x += sign * a.x;
    q.y += sign * a.y;
    q.z += sign * a.z;
  }
  const double n = double(relative.size());
  extrinsic = {{t.x / n, t.y / n, t.z / n}, normalize_quaternion(q)};
  solver::SolverProblem problem;
  std::array<std::vector<IntrinsicBinding>, 2> bindings;
  Id next = 1;
  for (int device = 0; device < 2; ++device) {
    const auto &camera = *initial[device].camera(0).parameters;
    for (const auto &name : optimizable_parameter_names(camera)) {
      const double scale = name == "k1"   ? 1e4
                           : name == "k2" ? 1e8
                           : name == "k3" ? 1e12
                                          : 1.;
      bindings[device].push_back({name, next, scale});
      solver::ParameterBlock b{
          next++,
          {camera_parameter_value(camera, name) / scale},
          1,
          solver::ManifoldKind::Euclidean,
          data.camera(device).excluded_settings.count(name) != 0,
          {},
          {}};
      if (name == "focus" || name == "sx" || name == "sy")
        b.lower_bounds = {1e-12};
      problem.parameter_blocks.push_back(std::move(b));
    }
  }
  const Id device_id = next++;
  problem.parameter_blocks.push_back(pose_block(device_id, extrinsic));
  std::map<Board, Id> boards;
  for (int device = 0; device < 2; ++device)
    for (const auto &[key, p] : initial[device].calibration_result()->poses) {
      const Board key2{key.calib_object_index, key.pose_index};
      if (boards.count(key2))
        continue;
      const auto reference =
          device == 0 ? p : compose_pose(inverse_pose(extrinsic), p);
      boards.emplace(key2, next);
      problem.parameter_blocks.push_back(pose_block(next++, reference));
    }
  solver::ResidualBlockId residual_id = 1;
  for (const auto &[key, o] : data.observations()) {
    std::vector<Point3d> points;
    points.reserve(o.indices.size());
    for (const auto index : o.indices)
      points.push_back(data.target(key.calib_object_index).points.at(index));
    problem.residual_blocks.push_back(
        {residual_id++, std::make_unique<JointResidual>(
                            *initial[key.camera_index].camera(0).parameters,
                            bindings[key.camera_index],
                            boards.at({key.calib_object_index, key.pose_index}),
                            key.camera_index == 0 ? 0 : device_id,
                            std::move(points), o.rows, o.columns)});
  }
  auto backend =
      solver::make_optimizer_backend(solver::OptimizerBackendKind::BuiltinLm);
  solver::SolverSummary summary;
  const auto status = backend->solve(problem, data.options(), &summary);
  if (status != core::Status::Ok)
    throw core::Exception(status, "calibrate_cameras", "joint fit: " + summary.message +
        " (cost " + std::to_string(summary.initial_cost) + " -> " + std::to_string(summary.final_cost) + ")");
  std::map<Id, const solver::ParameterBlock *> blocks;
  for (const auto &b : problem.parameter_blocks)
    blocks.emplace(b.id, &b);
  std::array<CameraParameters, 2> final{*initial[0].camera(0).parameters,
                                        *initial[1].camera(0).parameters};
  for (int device = 0; device < 2; ++device)
    for (const auto &b : bindings[device])
      set_camera_parameter_value(final[device], b.name,
                                 blocks.at(b.id)->values[0] * b.scale);
  extrinsic = pose(blocks.at(device_id)->values);
  CalibrationResult result;
  result.solver_summary = summary;
  result.camera_poses = {Pose3d{}, inverse_pose(extrinsic)};
  for (const auto &[key, id] : boards)
    result.poses.emplace(ObservationKey{0, key.first, key.second},
                         pose(blocks.at(id)->values));
  double squared = 0, sum = 0, maximum = 0;
  for (const auto &[key, o] : data.observations()) {
    auto p = result.poses.at({0, key.calib_object_index, key.pose_index});
    if (key.camera_index == 1)
      p = compose_pose(extrinsic, p);
    double view = 0;
    for (std::size_t i = 0; i < o.indices.size(); ++i) {
      const auto ip = project_camera_point(
          final[key.camera_index],
          transform_point(
              p, data.target(key.calib_object_index).points.at(o.indices[i])));
      const double error =
          std::hypot(ip.row - o.rows[i], ip.col - o.columns[i]);
      view += error * error;
      sum += error;
      maximum = std::max(maximum, error);
      ++result.num_points;
    }
    squared += view;
    result.per_view_rmse.emplace(key,
                                 std::sqrt(view / double(o.indices.size())));
  }
  result.rmse = std::sqrt(squared / double(result.num_points));
  result.mean_error = sum / double(result.num_points);
  result.max_error = maximum;
  for (int device = 0; device < 2; ++device) {
    CalibrationResult quality;
    compute_calibration_uncertainty(problem, bindings[device], &quality,
                                    data.options().linear_backend);
    result.device_stddev.push_back(quality.camera_stddev);
    result.device_covariance.push_back(quality.camera_covariance);
    result.device_covariance_available.push_back(quality.covariance_available);
    if (device == 0) {
      result.camera_stddev = quality.camera_stddev;
      result.camera_covariance = quality.camera_covariance;
      result.covariance_available = quality.covariance_available;
      result.quality_message = quality.quality_message;
    }
  }
  // Public C adapter runs on a candidate model; commit all results together.
  for (int device = 0; device < 2; ++device)
    data.camera(device).parameters = std::move(final[device]);
  *rmse = result.rmse;
  data.set_calibration_result(std::move(result));
}
} // namespace mv::calibration
