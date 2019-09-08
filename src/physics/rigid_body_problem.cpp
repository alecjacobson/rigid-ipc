#include "rigid_body_problem.hpp"

#include <iostream>

#include <autodiff/finitediff.hpp>
#include <utils/flatten.hpp>
#include <utils/invalid_param_error.hpp>
#include <utils/tensor.hpp>

#include <io/read_rb_scene.hpp>
#include <io/serialize_json.hpp>

#include <logger.hpp>
#include <profiler.hpp>

namespace ccd {

namespace physics {

    RigidBodyProblem::RigidBodyProblem()
        : RigidBodyProblem("rigid_body_problem")
    {
    }

    RigidBodyProblem::RigidBodyProblem(const std::string& name)
        : coefficient_restitution(0)
        , gravity_(Eigen::Vector3d::Zero())
        , collision_eps(2)
        , name_(name)

    {
    }

    void RigidBodyProblem::settings(const nlohmann::json& params)
    {
        collision_eps = params["collision_eps"].get<double>();
        coefficient_restitution
            = params["coefficient_restitution"].get<double>();

        io::from_json(params["gravity"], gravity_);
        assert(gravity_.rows() == 3);

        std::vector<physics::RigidBody> rbs;
        io::read_rb_scene(params, rbs);

        init(rbs);
        m_assembler.init(rbs);
    }

    nlohmann::json RigidBodyProblem::settings() const
    {
        nlohmann::json json;

        json["collision_eps"] = collision_eps;
        json["coefficient_restitution"] = coefficient_restitution;
        json["gravity"] = io::to_json(gravity_);
        return json;
    }

    void RigidBodyProblem::init(const std::vector<RigidBody> rbs)
    {
        m_assembler.init(rbs);

        vertices_t0.resize(m_assembler.num_vertices(), 2);
        vertices_t0.setZero();

        vertices_q1.resize(m_assembler.num_vertices(), 2);
        vertices_q1.setZero();

        Fcollision.resize(m_assembler.num_vertices(), 2);
        Fcollision.setZero();
        update_constraint();

        for (size_t i = 0; i < m_assembler.m_rbs.size(); ++i) {
            auto& rb = m_assembler.m_rbs[i];
            spdlog::info(
                "rb={} mass={} innertia={}", i, rb.mass, rb.moment_of_inertia);
        }
    }

    nlohmann::json RigidBodyProblem::state() const
    {
        nlohmann::json json;
        std::vector<nlohmann::json> rbs;
        Eigen::Vector2d p = Eigen::Vector2d::Zero();
        double L = 0.0;
        double T = 0.0;
        double G = 0.0;

        for (auto& rb : m_assembler.m_rbs) {
            nlohmann::json jrb;
            jrb["position"] = io::to_json(Eigen::VectorXd(rb.position));
            jrb["velocity"] = io::to_json(Eigen::VectorXd(rb.velocity));
            rbs.push_back(jrb);

            p += rb.mass * rb.velocity.head(2);

            L += rb.moment_of_inertia * rb.velocity(2);

            T += 1.0 / 2.0 * rb.mass * rb.velocity.head(2).transpose()
                * rb.velocity.head(2);
            T += 1.0 / 2.0 * rb.moment_of_inertia * rb.velocity(2)
                * rb.velocity(2);

            if (rb.is_dof_fixed[0] && rb.velocity[0] != 0.0) {
                spdlog::error(
                    "fixed body has nonzero vel x {}", rb.velocity[0]);
            }
            if (rb.is_dof_fixed[1] && rb.velocity[1] != 0.0) {
                spdlog::error(
                    "fixed body has nonzero vel y {}", rb.velocity[1]);
            }
            if (rb.is_dof_fixed[2] && rb.velocity[2] != 0.0) {
                spdlog::error(
                    "fixed body has nonzero angular vel {}", rb.velocity[2]);
            }
            if (!rb.is_dof_fixed[0] && !rb.is_dof_fixed[1]) {
                G += -rb.mass * gravity_.transpose() * rb.position;
            }
        }
        // Another way of compting total energy
        //        Eigen::MatrixXd vel = m_assembler.world_velocities();
        //        ccd::flatten(vel);
        //        Eigen::VectorXd vel_ = vel;
        //        double kinetic = 1.0 / 2.0  * (vel_.transpose() *
        //        m_assembler.m_mass_matrix * vel_)[0];

        json["rigid_bodies"] = rbs;
        json["linear_momentum"] = io::to_json(Eigen::VectorXd(p));
        json["angular_momentum"] = L;
        json["kinetic_energy"] = T;
        json["potential_energy"] = G;
        return json;
    }

    void RigidBodyProblem::state(const nlohmann::json& args)
    {
        nlohmann::json json;
        auto& rbs = args["rigid_bodies"];
        assert(rbs.size() == m_assembler.m_rbs.size());
        size_t i = 0;
        for (auto& jrb : args["rigid_bodies"]) {
            Eigen::VectorXd velocity;
            io::from_json(jrb["velocity"], velocity);
            Eigen::VectorXd position;
            io::from_json(jrb["position"], position);

            m_assembler.m_rbs[i].position = position;
            m_assembler.m_rbs[i].velocity = velocity;
            i += 1;
        }
    }

    bool RigidBodyProblem::simulation_step(const double time_step)
    {

        for (auto& rb : m_assembler.m_rbs) {
            rb.position_prev = rb.position;
            rb.velocity_prev = rb.velocity;
            rb.position = rb_position_next(rb, time_step);
            rb.velocity = (rb.position - rb.position_prev) / time_step;
        }

        Fcollision.setZero();
        vertices_q1.setZero();

        vertices_t0 = m_assembler.world_vertices_t0();
        vertices_q1 = m_assembler.world_vertices_t1();

        return detect_collisions(
            vertices_t0, vertices_q1, CollisionCheck::CONSERVATIVE);
    }

    void RigidBodyProblem::update_constraint()
    {
        vertices_t0 = m_assembler.world_vertices_t0();
        vertices_q1 = m_assembler.world_vertices_t1();

        const Eigen::SparseMatrix<double>& S = m_assembler.m_position_to_dof;
        sigma_t1 = S * m_assembler.rb_positions_t1();

        // base problem initial solution
        // start from collision free state
        x0 = S * m_assembler.rb_positions_t0();
        num_vars_ = int(x0.size());

        original_ev_impacts
            = constraint().initialize(vertices_t0, m_assembler.m_edges,
                m_assembler.m_vertex_to_body_map, vertices_q1 - vertices_t0);

        std::sort(original_ev_impacts.begin(), original_ev_impacts.end(),
            ccd::compare_impacts_by_time<ccd::EdgeVertexImpact>);
    }

    opt::OptimizationResults RigidBodyProblem::solve_constraints()
    {
        return solver().solve();
    }

    void RigidBodyProblem::init_solve() { return solver().init_solve(); }
    opt::OptimizationResults RigidBodyProblem::step_solve()
    {
        return solver().step_solve();
    }

    bool RigidBodyProblem::take_step(
        const Eigen::VectorXd& sigma, const double time_step)
    {

        // This need to be done BEFORE updating positions
        // -------------------------------------
        if (coefficient_restitution > -1) {
            solve_velocities();
        }

        // update final position
        // -------------------------------------
        Eigen::VectorXd rb_positions = m_assembler.m_dof_to_position * sigma;
        m_assembler.set_rb_positions(rb_positions);
        Eigen::MatrixXd q1 = m_assembler.world_vertices_t1();

        // This need to be done AFTER updating positions
        if (coefficient_restitution < 0) {
            for (auto& rb : m_assembler.m_rbs) {
                rb.velocity = (rb.position - rb.position_prev) / time_step;
            }
        }

        return detect_collisions(vertices_t0, q1, CollisionCheck::EXACT);
    }

    void RigidBodyProblem::solve_velocities()
    {

        // precompute normal directions (since velocities will change i can't do
        // it after
        Eigen::MatrixXd normals(original_ev_impacts.size(), 2);

        for (int i = 0; i < normals.rows(); ++i) {
            const auto& ev_candidate = original_ev_impacts[size_t(i)];

            double toi = ev_candidate.time;

            const long edge_id = ev_candidate.edge_index;
            const long a_id = ev_candidate.vertex_index;
            const int b0_id = m_assembler.m_edges.coeff(edge_id, 0);
            const int b1_id = m_assembler.m_edges.coeff(edge_id, 1);

            const size_t body_B_id
                = size_t(m_assembler.m_vertex_to_body_map(b0_id));
            bool is_oriented = m_assembler.m_rbs[body_B_id].is_oriented;

            Eigen::Vector2d n_toi;
            auto v_toi = vertices_t0 + (vertices_q1 - vertices_t0) * toi;
            Eigen::VectorXd e_toi
                = v_toi.row(b1_id) - v_toi.row(b0_id); // edge at toi
            n_toi << -e_toi(1), e_toi(0);              // 90deg ccw rotation
            n_toi.normalize();

            if (is_oriented) {
                n_toi = -n_toi;
            } else {
                // check normal points towards A
                Eigen::Vector2d va = vertices_t0.row(a_id);
                Eigen::Vector2d vb = vertices_t0.row(b0_id);
                if ((va - vb).transpose() * n_toi <= 0.0) {
                    n_toi *= -1;
                }
            }
            normals.row(i) = n_toi.transpose();
        }

#ifndef NDEBUG
        double prev_toi = -1;
#endif
        for (int i = 0; i < normals.rows(); ++i) {
            const auto& ev_candidate = original_ev_impacts[size_t(i)];

            double toi = ev_candidate.time;
#ifndef NDEBUG
            assert(prev_toi <= toi);
            prev_toi = toi;
#endif
            double alpha = ev_candidate.alpha;

            // global ids of the vertices
            const long edge_id = ev_candidate.edge_index;
            const long a_id = ev_candidate.vertex_index;
            const int b0_id = m_assembler.m_edges.coeff(edge_id, 0);
            const int b1_id = m_assembler.m_edges.coeff(edge_id, 1);

            const size_t body_A_id
                = size_t(m_assembler.m_vertex_to_body_map(a_id));
            const size_t body_B_id
                = size_t(m_assembler.m_vertex_to_body_map(b0_id));

            // local (rigid body) ids of the vertices
            const long r_A_id = a_id - m_assembler.m_body_vertex_id[body_A_id];
            const long r_B0_id
                = b0_id - m_assembler.m_body_vertex_id[body_B_id];
            const long r_B1_id
                = b1_id - m_assembler.m_body_vertex_id[body_B_id];

            auto& body_A = m_assembler.m_rbs[body_A_id];
            auto& body_B = m_assembler.m_rbs[body_B_id];

            // The velocities of the center of mass
            // at the time of collision!!
            const Eigen::Vector3d vel_A_prev = body_A.velocity_prev
                + toi * (body_A.velocity - body_A.velocity_prev);
            const Eigen::Vector3d vel_B_prev = body_B.velocity_prev
                + toi * (body_B.velocity - body_B.velocity_prev);

            const Eigen::Vector2d& V_Aprev = vel_A_prev.head(2);
            const Eigen::Vector2d& V_Bprev = vel_B_prev.head(2);
            // The angular velocities
            const double& w_Aprev = vel_A_prev(2);
            const double& w_Bprev = vel_B_prev(2);
            // The masss
            const double inv_m_A
                = (body_A.is_dof_fixed[0] || body_A.is_dof_fixed[1])
                ? 0.0
                : 1.0 / body_A.mass;
            const double inv_m_B
                = (body_B.is_dof_fixed[0] || body_B.is_dof_fixed[1])
                ? 0.0
                : 1.0 / body_B.mass;
            // The moment of inertia
            const double inv_I_A
                = body_A.is_dof_fixed[2] ? 0 : 1.0 / body_A.moment_of_inertia;
            const double inv_I_B
                = body_B.is_dof_fixed[2] ? 0 : 1.0 / body_B.moment_of_inertia;

            // Vectors from the center of mass to the collision point
            // (90deg rotation counter clockwise)
            //
            //      (1) first get vertices position wrt rigid bodies
            const Eigen::Vector2d r0_A = body_A.vertices.row(r_A_id);
            const Eigen::Vector2d r0_B0
                = body_B.vertices.row(r_B0_id); // edge vertex 0
            const Eigen::Vector2d r0_B1
                = body_B.vertices.row(r_B1_id); // edge vertex 1
            const Eigen::Vector2d r0_B = r0_B0 + alpha * (r0_B1 - r0_B0);

            //      (2) and the angular displacement at time of collision
            const double theta_Atoi = body_A.position_prev(2)
                + toi * (body_A.position - body_A.position_prev)(2);
            const double theta_Btoi = body_B.position_prev(2)
                + toi * (body_B.position - body_B.position_prev)(2);
            //      (3) then the vectors are given by r = R(\theta_{t})*r_0
            const Eigen::Vector2d r_Aperp_toi
                = body_A.grad_theta(theta_Atoi) * r0_A;
            const Eigen::Vector2d r_Bperp_toi
                = body_B.grad_theta(theta_Btoi) * r0_B;

            // The collision point velocities BEFORE collision
            const Eigen::Vector2d v_Aprev = V_Aprev + w_Aprev * r_Aperp_toi;
            const Eigen::Vector2d v_Bprev = V_Bprev + w_Bprev * r_Bperp_toi;

            // The relative veolicity magnitud BEFORE collision
            const Eigen::Vector2d& n_toi = normals.row(i);

            const double vrel_prev_toi
                = (v_Aprev - v_Bprev).transpose() * n_toi;
            if (vrel_prev_toi >= 0.0) {
                continue;
            }
            // solve for the impulses
            const double nr_A_toi = n_toi.transpose() * r_Aperp_toi;
            const double nr_B_toi = n_toi.transpose() * r_Bperp_toi;
            const double K = (inv_m_A + inv_m_B //
                + inv_I_A * nr_A_toi * nr_A_toi
                + inv_I_B * nr_B_toi * nr_B_toi);

            const double j
                = -1.0 / K * (1.0 + coefficient_restitution) * vrel_prev_toi;

            // update
            Eigen::Vector2d V_A_delta = inv_m_A * j * n_toi;
            Eigen::Vector2d V_B_delta = -inv_m_B * j * n_toi;
            double w_A_delta = inv_I_A * j * nr_A_toi;
            double w_B_delta = -inv_I_B * j * nr_B_toi;

            if (!(body_A.is_dof_fixed[0] || body_A.is_dof_fixed[1]))
                body_A.velocity.head(2) = V_Aprev + V_A_delta;

            if (!(body_B.is_dof_fixed[0] || body_B.is_dof_fixed[1]))
                body_B.velocity.head(2) = V_Bprev + V_B_delta;

            if (!body_A.is_dof_fixed[2])
                body_A.velocity(2) = w_Aprev + w_A_delta;

            if (!body_B.is_dof_fixed[2])
                body_B.velocity(2) = w_Bprev + w_B_delta;
        }
    }

    Eigen::Vector3d RigidBodyProblem::rb_position_next(
        const RigidBody& rb, const double time_step) const
    {
        Eigen::Vector3d x = rb.position;
        x += time_step * rb.velocity;                 // momentum
        x += time_step * time_step * gravity_;        // body-forces
        x = (rb.is_dof_fixed).select(rb.position, x); // reset fixed nodes
        return x;
    }

    bool RigidBodyProblem::detect_collisions(const Eigen::MatrixXd& q0,
        const Eigen::MatrixXd& q1,
        const CollisionCheck check_type)
    {
        assert(q0.cols() == 2);
        assert(q1.cols() == 2);

        EdgeVertexImpacts ev_impacts;
        double scale
            = check_type == CollisionCheck::EXACT ? 1.0 : (1.0 + collision_eps);
        ccd::detect_edge_vertex_collisions(q0, (q1 - q0) * scale,
            m_assembler.m_edges, m_assembler.m_vertex_to_body_map, ev_impacts,
            constraint().detection_method);

        return ev_impacts.size() > 0;
    }

    ////////////////////////////////////////////////////////////////////////////
    /// Functional
    ////////////////////////////////////////////////////////////////////////////

    double RigidBodyProblem::eval_f(const Eigen::VectorXd& sigma)
    {
        Eigen::VectorXd diff = (sigma - sigma_t1);
        const Eigen::SparseMatrix<double>& invS = m_assembler.m_dof_to_position;
        const Eigen::SparseMatrix<double>& M = m_assembler.m_rb_mass_matrix;
        return 0.5 * diff.transpose() * invS.transpose() * M * invS * diff;
    }

    Eigen::VectorXd RigidBodyProblem::eval_grad_f(const Eigen::VectorXd& sigma)
    {
        Eigen::VectorXd grad_f;
        Eigen::VectorXd diff = (sigma - sigma_t1);

        const Eigen::SparseMatrix<double>& invS = m_assembler.m_dof_to_position;
        const Eigen::SparseMatrix<double>& M = m_assembler.m_rb_mass_matrix;

        grad_f = invS.transpose() * M * invS * diff;

#ifdef WITH_DERIVATIVE_CHECK
        Eigen::VectorXd grad_f_approx = eval_grad_f_approx(*this, sigma);
        if(!compare_gradient(grad_f, grad_f_approx)){
            spdlog::trace("finite gradient check failed for f");
        }
#endif
        return grad_f;
    }

    Eigen::SparseMatrix<double> RigidBodyProblem::eval_hessian_f(
        const Eigen::VectorXd& sigma)
    {
        const Eigen::SparseMatrix<double>& invS = m_assembler.m_dof_to_position;
        const Eigen::SparseMatrix<double>& M = m_assembler.m_rb_mass_matrix;

        Eigen::SparseMatrix<double> hessian_f;
        hessian_f = invS.transpose() * M * invS.transpose();

#ifdef WITH_DERIVATIVE_CHECK
        Eigen::MatrixXd hessian_f_approx = eval_hess_f_approx(*this, sigma);
        if(!compare_jacobian(hessian_f, hessian_f_approx)){
            spdlog::trace("finite hessian check failed for f");
        }
#endif
        return hessian_f;
    }

} // namespace physics
} // namespace ccd
