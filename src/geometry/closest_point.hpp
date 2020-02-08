#pragma once

#include <Eigen/Core>

namespace ccd {

//-----------------------------------------------------------------------------
// Unsigned Distances
//-----------------------------------------------------------------------------

template <typename T>
T point_segment_closest_point(
    const Eigen::Matrix<T, Eigen::Dynamic, 1>& point,
    const Eigen::Matrix<T, Eigen::Dynamic, 1>& segment_vertex0,
    const Eigen::Matrix<T, Eigen::Dynamic, 1>& segment_vertex1);

template <typename T>
T segment_segment_closest_points(
    const Eigen::Matrix<T, Eigen::Dynamic, 1>& segment0_vertex0,
    const Eigen::Matrix<T, Eigen::Dynamic, 1>& segment0_vertex1,
    const Eigen::Matrix<T, Eigen::Dynamic, 1>& segment1_vertex0,
    const Eigen::Matrix<T, Eigen::Dynamic, 1>& segment1_vertex1,
    Eigen::Matrix<T, Eigen::Dynamic, 1>& segment0_point,
    Eigen::Matrix<T, Eigen::Dynamic, 1>& segment1_point);

template <typename T>
T point_triangle_closest_point(
    const Eigen::Matrix<T, Eigen::Dynamic, 1>& point,
    const Eigen::Matrix<T, Eigen::Dynamic, 1>& triangle_vertex0,
    const Eigen::Matrix<T, Eigen::Dynamic, 1>& triangle_vertex1,
    const Eigen::Matrix<T, Eigen::Dynamic, 1>& triangle_vertex2);

} // namespace ccd

#include "distance.tpp"