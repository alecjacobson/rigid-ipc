// Time-of-impact computation for rigid bodies with angular trajectories.
#include "rigid_body_time_of_impact.hpp"

#include <ccd/interval_root_finder.hpp>
#include <geometry/distance.hpp>
#include <logger.hpp>
#include <utils/eigen_ext.hpp>
#include <utils/not_implemented_error.hpp>

namespace ccd {

/// Find time-of-impact between two rigid bodies
bool compute_edge_vertex_time_of_impact(
    const physics::RigidBody& bodyA,
    const physics::Pose<double>& poseA,         // Pose of bodyA
    const physics::Pose<double>& displacementA, // Displacement of bodyA
    const size_t& vertex_id,                    // In bodyA
    const physics::RigidBody& bodyB,
    const physics::Pose<double>& poseB,         // Pose of bodyB
    const physics::Pose<double>& displacementB, // Displacement of bodyB
    const size_t& edge_id,                      // In bodyB
    double& toi)
{
    int dim = bodyA.dim();
    assert(bodyB.dim() == dim);
    assert(dim == 2); // TODO: 3D

    const physics::Pose<Interval> poseA_interval = poseA.cast<Interval>();
    const physics::Pose<Interval> poseB_interval = poseB.cast<Interval>();

    const physics::Pose<Interval> displacementA_interval =
        displacementA.cast<Interval>();
    const physics::Pose<Interval> displacementB_interval =
        displacementB.cast<Interval>();

    const auto distance = [&](Interval t) {
        // Compute the poses at time t
        physics::Pose<Interval> bodyA_pose_interval =
            poseA_interval + displacementA_interval * t;
        physics::Pose<Interval> bodyB_pose_interval =
            poseB_interval + displacementB_interval * t;

        // Get the world vertex of the point at time t
        Eigen::VectorX3<Interval> v0 =
            bodyA.world_vertex<Interval>(bodyA_pose_interval, vertex_id);
        // Get the world vertex of the edge at time t
        Eigen::VectorX3<Interval> v1 = bodyB.world_vertex<Interval>(
            bodyB_pose_interval, bodyB.edges(edge_id, 0));
        Eigen::VectorX3<Interval> v2 = bodyB.world_vertex<Interval>(
            bodyB_pose_interval, bodyB.edges(edge_id, 1));

        return geometry::point_line_signed_distance<Interval>(v0, v1, v2);
    };

    const auto is_point_along_edge = [&](Interval t) {
        // Compute the poses at time t
        physics::Pose<Interval> bodyA_pose_interval =
            poseA_interval + displacementA_interval * t;
        physics::Pose<Interval> bodyB_pose_interval =
            poseB_interval + displacementB_interval * t;

        // Get the world vertex of the point at time t
        Eigen::VectorX3<Interval> v0 =
            bodyA.world_vertex<Interval>(bodyA_pose_interval, vertex_id);
        // Get the world vertex of the edge at time t
        Eigen::VectorX3<Interval> v1 = bodyB.world_vertex<Interval>(
            bodyB_pose_interval, bodyB.edges(edge_id, 0));
        Eigen::VectorX3<Interval> v2 = bodyB.world_vertex<Interval>(
            bodyB_pose_interval, bodyB.edges(edge_id, 1));

        // Project the point onto the edge by computing its scalar projection
        Eigen::VectorX3<Interval> edge_vec = v2 - v1;
        Interval alpha = (v0 - v1).dot(edge_vec) / edge_vec.squaredNorm();
        // spdlog::debug("α ∈ {}", logger::fmt_interval(alpha));
        return overlap(alpha, Interval(0, 1));
    };

    Interval toi_interval;
    // TODO: Set tolerance dynamically
    bool is_impacting = interval_root_finder(
        distance, is_point_along_edge, Interval(0, 1), toi_interval);
    // Return a conservative time-of-impact
    toi = toi_interval.lower();
    return is_impacting;
}

// Find time-of-impact between two rigid bodies
bool compute_edge_edge_time_of_impact(
    const physics::RigidBody& bodyA,
    const physics::Pose<double>& poseA,         // Pose of bodyA
    const physics::Pose<double>& displacementA, // Displacement of bodyA
    const size_t& edgeA_id,                     // In bodyA
    const physics::RigidBody& bodyB,
    const physics::Pose<double>& poseB,         // Pose of bodyB
    const physics::Pose<double>& displacementB, // Displacement of bodyB
    const size_t& edgeB_id,                     // In bodyB
    double& toi)
{
    throw NotImplementedError(
        "Edge-edge time-of-impact not implemented for rigid bodies!");
}

template <typename T>
inline Eigen::VectorX3<T> triangle_normal(
    const Eigen::VectorX3<T>& face_vertex0,
    const Eigen::VectorX3<T>& face_vertex1,
    const Eigen::VectorX3<T>& face_vertex2)
{
    return ((face_vertex1 - face_vertex0).template head<3>())
        .cross((face_vertex2 - face_vertex0).template head<3>());
}

// Find time-of-impact between two rigid bodies
bool compute_face_vertex_time_of_impact(
    const physics::RigidBody& bodyA,
    const physics::Pose<double>& poseA,         // Pose of bodyA
    const physics::Pose<double>& displacementA, // Displacement of bodyA
    const size_t& vertex_id,                    // In bodyA
    const physics::RigidBody& bodyB,
    const physics::Pose<double>& poseB,         // Pose of bodyB
    const physics::Pose<double>& displacementB, // Displacement of bodyB
    const size_t& face_id,                      // In bodyB
    double& toi)
{
    int dim = bodyA.dim();
    assert(bodyB.dim() == dim);
    assert(dim == 3);

    const physics::Pose<Interval> poseA_interval = poseA.cast<Interval>();
    const physics::Pose<Interval> poseB_interval = poseB.cast<Interval>();

    const physics::Pose<Interval> displacementA_interval =
        displacementA.cast<Interval>();
    const physics::Pose<Interval> displacementB_interval =
        displacementB.cast<Interval>();

    const auto distance = [&](Interval t) {
        // Compute the poses at time t
        physics::Pose<Interval> bodyA_pose_interval =
            poseA_interval + displacementA_interval * t;
        physics::Pose<Interval> bodyB_pose_interval =
            poseB_interval + displacementB_interval * t;

        // Get the world vertex of the point at time t
        Eigen::VectorX3<Interval> vertex =
            bodyA.world_vertex<Interval>(bodyA_pose_interval, vertex_id);
        // Get the world vertex of the edge at time t
        Eigen::VectorX3<Interval> face_vertex0 = bodyB.world_vertex<Interval>(
            bodyB_pose_interval, bodyB.faces(face_id, 0));
        Eigen::VectorX3<Interval> face_vertex1 = bodyB.world_vertex<Interval>(
            bodyB_pose_interval, bodyB.faces(face_id, 1));
        Eigen::VectorX3<Interval> face_vertex2 = bodyB.world_vertex<Interval>(
            bodyB_pose_interval, bodyB.faces(face_id, 2));

        Eigen::VectorX3<Interval> normal =
            triangle_normal(face_vertex0, face_vertex1, face_vertex2);

        return geometry::point_plane_signed_distance<Interval>(
            vertex, face_vertex0, normal);
    };

    const auto is_point_inside_triangle = [&](Interval t) {
        // Compute the poses at time t
        physics::Pose<Interval> bodyA_pose_interval =
            poseA_interval + displacementA_interval * t;
        physics::Pose<Interval> bodyB_pose_interval =
            poseB_interval + displacementB_interval * t;

        // Get the world vertex of the point at time t
        Eigen::VectorX3<Interval> vertex =
            bodyA.world_vertex<Interval>(bodyA_pose_interval, vertex_id);
        // Get the world vertex of the edge at time t
        Eigen::VectorX3<Interval> face_vertex0 = bodyB.world_vertex<Interval>(
            bodyB_pose_interval, bodyB.faces(face_id, 0));
        Eigen::VectorX3<Interval> face_vertex1 = bodyB.world_vertex<Interval>(
            bodyB_pose_interval, bodyB.faces(face_id, 1));
        Eigen::VectorX3<Interval> face_vertex2 = bodyB.world_vertex<Interval>(
            bodyB_pose_interval, bodyB.faces(face_id, 2));

        Eigen::VectorX3<Interval> normal0 =
            triangle_normal(face_vertex0, face_vertex1, vertex).normalized();
        Eigen::VectorX3<Interval> normal1 =
            triangle_normal(face_vertex0, vertex, face_vertex2).normalized();
        Eigen::VectorX3<Interval> normal2 =
            triangle_normal(face_vertex1, face_vertex2, vertex).normalized();
        for (int i = 0; i < normal0.size(); i++) {
            if (!overlap(intersect(normal0(i), normal1(i)), normal2(i))) {
                return false;
            }
        }
        return true;
    };

    Interval toi_interval;
    // TODO: Set tolerance dynamically
    bool is_impacting = interval_root_finder(
        distance, is_point_inside_triangle, Interval(0, 1), toi_interval);
    // Return a conservative time-of-impact
    toi = toi_interval.lower();
    return is_impacting;
}

} // namespace ccd