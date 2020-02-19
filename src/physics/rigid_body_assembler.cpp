#include <physics/rigid_body_assembler.hpp>

#include <Eigen/Geometry>

#include <finitediff.hpp>

#include <physics/mass.hpp>

#include <logger.hpp>
#include <utils/eigen_ext.hpp>
#include <utils/flatten.hpp>
#include <utils/not_implemented_error.hpp>

namespace ccd {

namespace physics {

    void RigidBodyAssembler::init(const std::vector<RigidBody>& rigid_bodies)
    {
        m_rbs = rigid_bodies;

        size_t num_bodies = rigid_bodies.size();
        m_body_vertex_id.resize(num_bodies + 1);
        m_body_face_id.resize(num_bodies + 1);
        m_body_edge_id.resize(num_bodies + 1);

        // Store the starting position of each RB vertex in the global vertices
        for (size_t i = 0; i < num_bodies; ++i) {
            auto& rb = rigid_bodies[i];
            m_body_vertex_id[i + 1] = m_body_vertex_id[i] + rb.vertices.rows();
            m_body_face_id[i + 1] = m_body_face_id[i] + rb.faces.rows();
            m_body_edge_id[i + 1] = m_body_edge_id[i] + rb.edges.rows();
        }

        // global edges and faces
        m_edges.resize(m_body_edge_id.back(), 2);
        m_faces.resize(m_body_face_id.back(), 3);
        for (size_t i = 0; i < num_bodies; ++i) {
            auto& rb = rigid_bodies[i];
            m_edges.block(m_body_edge_id[i], 0, rb.edges.rows(), 2) =
                (rb.edges.array() + int(m_body_vertex_id[i]));
            if (rb.faces.size() != 0) {
                m_faces.block(m_body_face_id[i], 0, rb.faces.rows(), 3) =
                    (rb.faces.array() + int(m_body_vertex_id[i]));
            }
        }
        // vertex to body map
        m_vertex_to_body_map.resize(m_body_vertex_id.back());
        for (size_t i = 0; i < num_bodies; ++i) {
            auto& rb = rigid_bodies[i];
            m_vertex_to_body_map
                .segment(m_body_vertex_id[i], rb.vertices.rows())
                .setConstant(int(i));
        }

        // rigid body mass-matrix
        int rb_ndof = num_bodies ? rigid_bodies[0].ndof() : 0;
        int rb_pos_ndof = num_bodies ? rigid_bodies[0].pos_ndof() : 0;
        int rb_rot_ndof = num_bodies ? rigid_bodies[0].rot_ndof() : 0;
        typedef Eigen::Triplet<double> Triplet;
        std::vector<Triplet> mass_matrix_triplets;
        mass_matrix_triplets.reserve(
            int(num_bodies) * (rb_pos_ndof + rb_rot_ndof * rb_rot_ndof));
        Eigen::VectorXd scaling_vector(int(num_bodies) * rb_ndof);
        for (int i = 0; i < int(num_bodies); ++i) {
            auto& rb = rigid_bodies[size_t(i)];
            for (int pos_dofi = 0; pos_dofi < rb.pos_ndof(); pos_dofi++) {
                mass_matrix_triplets.emplace_back(
                    rb_ndof * i + pos_dofi, rb_ndof * i + pos_dofi, rb.mass);
            }
            for (int Ii = 0; Ii < rb.moment_of_inertia.rows(); Ii++) {
                for (int Ij = 0; Ij < rb.moment_of_inertia.cols(); Ij++) {
                    mass_matrix_triplets.emplace_back(
                        rb_ndof * i + rb.pos_ndof() + Ii,
                        rb_ndof * i + rb.pos_ndof() + Ij,
                        rb.moment_of_inertia(Ii, Ij));
                }
            }

            // scale rigid body pose to dof
            // set postion scaling to 1
            scaling_vector.segment(rb_ndof * i, rb.pos_ndof()).setOnes();
            // set rotation scaling to r_max
            scaling_vector.segment(rb_ndof * i + rb.pos_ndof(), rb.rot_ndof())
                .setConstant(rb.r_max);
        }
        m_rb_mass_matrix.resize(num_bodies * rb_ndof, num_bodies * rb_ndof);
        m_rb_mass_matrix.setFromTriplets(
            mass_matrix_triplets.begin(), mass_matrix_triplets.end());
        m_pose_to_dof = Eigen::SparseDiagonal<double>(scaling_vector);
        m_dof_to_pose = m_pose_to_dof.cwiseInverse();

        // particles mass-matrix
        {
            Eigen::SparseMatrix<double> M;
            physics::construct_mass_matrix(
                world_vertices_t0(), dim() == 2 ? m_edges : m_faces, M);
            // Repeat the mass vector to make a mass matrix per dof
            m_mass_matrix =
                Eigen::SparseDiagonal<double>(M.diagonal().replicate(dim(), 1));
        }
        m_inv_mass_matrix = m_mass_matrix.cwiseInverse();

        // rigid_body dof_fixed flag
        is_rb_dof_fixed.resize(int(num_bodies) * rb_ndof);
        for (int i = 0; i < int(num_bodies); ++i) {
            auto& rb = rigid_bodies[size_t(i)];
            is_rb_dof_fixed.segment(rb_ndof * i, rb_ndof) = rb.is_dof_fixed;
        }

        // rigid_body vertex dof_fixed flag
        is_dof_fixed.resize(m_body_vertex_id.back(), rb_ndof);
        for (size_t i = 0; i < num_bodies; ++i) {
            auto& rb = rigid_bodies[i];
            is_dof_fixed.block(
                m_body_vertex_id[i], 0, rb.vertices.rows(), rb_ndof) =
                rb.is_dof_fixed.transpose().replicate(rb.vertices.rows(), 1);
        }

        average_edge_length = 0;
        for (const auto& body : rigid_bodies) {
            average_edge_length += body.edges.rows() * body.average_edge_length;
        }
        average_edge_length /= m_edges.rows();
    }

    void RigidBodyAssembler::global_to_local_vertex(
        const long global_vertex_id,
        long& rigid_body_id,
        long& local_vertex_id) const
    {
        rigid_body_id = vertex_id_to_body_id(global_vertex_id);
        local_vertex_id =
            global_vertex_id - m_body_vertex_id[size_t(rigid_body_id)];
    }

    void RigidBodyAssembler::global_to_local_edge(
        const long global_edge_id,
        long& rigid_body_id,
        long& local_edge_id) const
    {
        rigid_body_id = edge_id_to_body_id(global_edge_id);
        local_edge_id = global_edge_id - m_body_edge_id[size_t(rigid_body_id)];
    }

    void RigidBodyAssembler::global_to_local_face(
        const long global_face_id,
        long& rigid_body_id,
        long& local_face_id) const
    {
        rigid_body_id = face_id_to_body_id(global_face_id);
        local_face_id = global_face_id - m_body_face_id[size_t(rigid_body_id)];
    }

    std::vector<Pose<double>>
    RigidBodyAssembler::rb_poses(const bool previous) const
    {
        std::vector<Pose<double>> poses;
        poses.reserve(num_bodies());
        for (const auto& rb : m_rbs) {
            poses.push_back(previous ? rb.pose_prev : rb.pose);
        }
        return poses;
    }

    void
    RigidBodyAssembler::set_rb_poses(const std::vector<Pose<double>>& poses)
    {
        assert(num_bodies() == poses.size());
        for (size_t i = 0; i < num_bodies(); ++i) {
            m_rbs[i].pose = poses[i];
        }
    }

    Eigen::MatrixXd
    RigidBodyAssembler::world_vertices(const RigidBody::Step step) const
    {
        Eigen::MatrixXd V(num_vertices(), dim());
        for (size_t i = 0; i < num_bodies(); ++i) {
            auto& rb = m_rbs[i];
            V.block(m_body_vertex_id[i], 0, rb.vertices.rows(), dim()) =
                rb.world_vertices(step);
        }
        return V;
    }

    Eigen::MatrixXd RigidBodyAssembler::world_velocities() const
    {
        Eigen::MatrixXd V(num_vertices(), dim());
        for (size_t i = 0; i < num_bodies(); ++i) {
            auto& rb = m_rbs[i];
            V.block(m_body_vertex_id[i], 0, rb.vertices.rows(), dim()) =
                rb.world_velocities();
        }
        return V;
    }

    void RigidBodyAssembler::world_vertices_gradient(
        const std::vector<Pose<double>>& poses,
        Eigen::SparseMatrix<double>& grad_u) const
    {
        assert(num_bodies() == poses.size());

        typedef Eigen::Triplet<double> Triplet;
        std::vector<Triplet> triplets;
        triplets.reserve(size_t(num_vertices()) * dim());

        grad_u.resize(
            int(num_vertices() * dim()),
            int(num_bodies()) * Pose<double>::dim_to_ndof(dim()));
        for (size_t i = 0; i < num_bodies(); ++i) {
            auto& rb = m_rbs[i];
            auto& p_i = poses[i];
            Eigen::MatrixXd el_grad = rb.world_vertices_gradient(p_i);

            long d = el_grad.rows() / dim();
            // Loop over dimensions (e.g. x then y)
            for (int d_i = 0; d_i < dim(); ++d_i) {
                // Loop over vertices
                for (int j = 0; j < d; ++j) {
                    // Loop over dof
                    for (int k = 0; k < el_grad.cols(); ++k) {
                        triplets.emplace_back(
                            int(m_body_vertex_id[i] + d_i * num_vertices()) + j,
                            int(3 * i) + k, el_grad(d_i * d + j, k));
                    }
                }
            }
        }

        grad_u.setFromTriplets(triplets.begin(), triplets.end());
    }

} // namespace physics
} // namespace ccd
