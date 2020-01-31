﻿#include "rigid_body.hpp"

#include <Eigen/Eigenvalues>
#include <Eigen/Geometry>

#include <autodiff/autodiff_types.hpp>
#include <finitediff.hpp>
#include <logger.hpp>
#include <utils/flatten.hpp>
#include <utils/not_implemented_error.hpp>

namespace ccd {

namespace physics {

    RigidBody RigidBody::from_points(const Eigen::MatrixXd& vertices,
        const Eigen::MatrixXi& faces,
        const Eigen::MatrixXi& edges,
        const Pose<double>& pose,
        const Pose<double>& velocity,
        const double density,
        const Eigen::VectorXb& is_dof_fixed,
        const bool oriented)
    {
        int dim = vertices.cols();
        assert(dim == pose.dim());
        assert(dim == velocity.dim());

        // move vertices so their center of mass is at (0, 0)
        Eigen::MatrixXd vertices_ = vertices;
        vertices_.rowwise() += pose.position.transpose();

        Eigen::RowVectorXd center_of_mass
            = compute_center_of_mass(vertices_, dim == 2 ? edges : faces);
        Eigen::MatrixXd centered_vertices
            = vertices_.rowwise() - center_of_mass;

        // set position so current vertices match input
        Pose<double> adjusted_pose(center_of_mass, pose.rotation);

        assert(is_dof_fixed.size() == pose.ndof());
        return RigidBody(centered_vertices, faces, edges, adjusted_pose,
            velocity, density, is_dof_fixed, oriented);
    }

    RigidBody::RigidBody(const Eigen::MatrixXd& vertices,
        const Eigen::MatrixXi& faces,
        const Eigen::MatrixXi& edges,
        const Pose<double>& pose,
        const Pose<double>& velocity,
        const double density,
        const Eigen::VectorXb& is_dof_fixed,
        const bool oriented)
        : vertices(vertices)
        , faces(faces)
        , edges(edges)
        , is_dof_fixed(is_dof_fixed)
        , is_oriented(oriented)
        , pose(pose)
        , pose_prev(pose)
        , velocity(velocity)
        , velocity_prev(velocity)
    {
        Eigen::VectorXd center_of_mass;
        compute_mass_properties(vertices, dim() == 2 ? edges : faces, mass,
            center_of_mass, moment_of_inertia);
        assert(center_of_mass.squaredNorm() < 1e-8);

        // TODO: Not sure why this is times based on Chrono
        // (https://bit.ly/2TVjJVm). Might be because mass above is actually
        // volume.
        mass *= density;
        Eigen::VectorXd principal_I;
        if (dim() == 3) {
            // Got this from Chrono: https://bit.ly/2RpbTl1
            Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es;
            es.compute(moment_of_inertia);
            principal_I = es.eigenvalues();
        } else {
            principal_I = moment_of_inertia.diagonal();
        }

        mass_matrix = Eigen::MatrixXd(ndof(), ndof());
        mass_matrix.diagonal().head(pos_ndof()).setConstant(mass);
        mass_matrix.diagonal().tail(rot_ndof()) = principal_I;
        inv_mass_matrix = mass_matrix.cwiseInverse();

        r_max = vertices.rowwise().squaredNorm().maxCoeff();
    }

    Eigen::MatrixXd RigidBody::world_velocities() const
    {
        if (dim() != 2) {
            throw NotImplementedError(
                "RigidBody::world_velocities() not implemented for 3D yet!");
        }
        // compute X[i] = dR(theta)/d\theta * r_i * d\theta/dt + dX/dt
        Eigen::MatrixXd dR = pose.construct_rotation_matrix_gradient()[0];
        Eigen::MatrixXd dR_r_i = vertices * dR.transpose();
        // TODO: This should be
        // dR_r_i.rowwise.array() * velocity.rotation.array()
        return (dR_r_i * velocity.rotation(0)).rowwise()
            + velocity.position.transpose();
    }

    Eigen::MatrixXd RigidBody::world_vertices_gradient(
        const Pose<double>& _pose) const
    {
        typedef AutodiffType<Eigen::Dynamic> Diff;
        Diff::activate(_pose.ndof());

        Pose<Diff::DDouble1> dpose(Diff::d1vars(0, _pose.position),
            Diff::d1vars(_pose.pos_ndof(), _pose.rotation));
        dpose.rotation /= Diff::DDouble1(r_max);

        Diff::D1MatrixXd dx = world_vertices<Diff::DDouble1>(dpose);

        flatten<Diff::DDouble1>(dx);
        Eigen::MatrixXd gradient = Diff::get_gradient(dx);
#ifdef WITH_DERIVATIVE_CHECK
        Eigen::MatrixXd exact_gradient = world_vertices_gradient_exact(_pose);
        bool is_grad_correct = fd::compare_jacobian(gradient, exact_gradient);
        assert(is_grad_correct);
#endif
        return gradient;
    }

    Eigen::MatrixXd RigidBody::world_vertices_gradient_exact(
        const Pose<double>& _pose) const
    {
        /// The gradient has shape vertices.size() by ndof.
        /// The order of rows is x-positions, y-positions(, z-positions).
        Eigen::MatrixXd gradient(vertices.size(), _pose.ndof());

        for (int i = 0; i < _pose.pos_ndof(); i++) {
            // gradient of r wrt position(i)
            Eigen::MatrixXd grad_U
                = Eigen::MatrixXd::Zero(vertices.rows(), vertices.cols());
            grad_U.col(i).setOnes();
            gradient.col(i) = flat<double>(grad_U);
        }

        // Tensor of rotation matrix gradients
        std::vector<Eigen::MatrixXd> grad_R
            = _pose.construct_rotation_matrix_gradient();
        for (int i = 0; i < _pose.rot_ndof(); i++) {
            // gradient of r wrt rotation(i)
            gradient.col(i + _pose.pos_ndof())
                = flat<double>(vertices * grad_R[i].transpose());
        }

        return gradient;
    }

    std::vector<Eigen::MatrixXd> RigidBody::world_vertices_hessian_exact(
        const Pose<double>& _pose) const
    {
        /// Each hessian has shape ndof by ndof, we return a list of
        /// vertice.size(). The order of rows is x-positions, y-positions(,
        /// z-positions).
        int ndof = _pose.ndof();
        int pos_ndof = _pose.pos_ndof();
        int rot_ndof = _pose.rot_ndof();
        std::vector<Eigen::MatrixXd> hessian(
            vertices.size(), Eigen::MatrixXd::Zero(ndof, ndof));

        std::vector<std::vector<Eigen::MatrixXd>> hess_R
            = _pose.construct_rotation_matrix_hessian();
        std::vector<std::vector<Eigen::VectorXd>> grad_U;
        for (int i = 0; i < rot_ndof; i++) {
            grad_U.push_back(std::vector<Eigen::VectorXd>());
            for (int j = 0; j < rot_ndof; j++) {
                grad_U[i].push_back(
                    flat<double>(vertices * hess_R[i][j].transpose()));
            }
        }

        for (long i = 0; i < vertices.size(); i++) {
            for (int j = pos_ndof; j < ndof; j++) {
                for (int k = pos_ndof; k < ndof; k++) {
                    hessian[size_t(i)](j, k) = grad_U[j][k](i);
                }
            }
        }
        return hessian;
    }

} // namespace physics
} // namespace ccd
