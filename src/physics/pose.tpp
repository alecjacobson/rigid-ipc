#include "pose.hpp"

#include <Eigen/Geometry>

#include <utils/not_implemented_error.hpp>

namespace ccd {
namespace physics {

    template <typename T>
    Pose<T>::Pose()
        : position()
        , rotation()
    {
    }

    template <typename T>
    Pose<T>::Pose(int dim)
        : position(Eigen::VectorXd::Zero(Pose<T>::dim_to_pos_ndof(dim)))
        , rotation(Eigen::VectorXd::Zero(Pose<T>::dim_to_rot_ndof(dim)))
    {
    }

    template <typename T>
    Pose<T>::Pose(const typename Pose<T>::VectorXT& position,
        const typename Pose<T>::VectorXT& rotation)
        : position(position)
        , rotation(rotation)
    {
    }

    template <typename T> Pose<T>::Pose(const typename Pose<T>::VectorXT& dof)
    {
        if (dof.size() == dim_to_ndof(2)) {
            position = dof.head(dim_to_pos_ndof(2));
            rotation = dof.tail(dim_to_rot_ndof(2));
        } else if (dof.size() == dim_to_ndof(3)) {
            position = dof.head(dim_to_pos_ndof(3));
            rotation = dof.tail(dim_to_rot_ndof(3));
        } else {
            throw NotImplementedError("Unknown pose convertion for given ndof");
        }
    }

    template <typename T>
    std::vector<Pose<T>> Pose<T>::dofs_to_poses(
        const typename Pose<T>::VectorXT& dofs, int dim)
    {
        int ndof = dim_to_ndof(dim);
        int num_poses = dofs.size() / ndof;
        assert(dofs.size() % ndof == 0);
        std::vector<Pose<T>> poses;
        poses.reserve(num_poses);
        for (int i = 0; i < num_poses; i++) {
            poses.emplace_back(dofs.segment(i * ndof, ndof));
        }
        return poses;
    }

    template <typename T>
    typename Pose<T>::VectorXT Pose<T>::poses_to_dofs(
        const std::vector<Pose<T>>& poses)
    {
        int ndof = poses.size() ? poses[0].ndof() : 0;
        VectorXT dofs(poses.size() * ndof);
        for (int i = 0; i < poses.size(); i++) {
            assert(poses[i].ndof() == ndof);
            dofs.segment(i * ndof, ndof) = poses[i].dof();
        }
        return dofs;
    }

    template <typename T> typename Pose<T>::VectorXT Pose<T>::dof() const
    {
        VectorXT pose_dof(ndof());
        pose_dof.head(pos_ndof()) = position;
        pose_dof.tail(rot_ndof()) = rotation;
        return pose_dof;
    }

    template <typename T>
    typename Pose<T>::MatrixXT Pose<T>::construct_rotation_matrix() const
    {
        if (dim() == 2) {
            return Eigen::Rotation2D<T>(rotation(0)).toRotationMatrix();
        } else {
            typedef Eigen::Matrix<T, 3, 1> Vector3T;
            return (Eigen::AngleAxis<T>(rotation.z(), Vector3T::UnitZ())
                * Eigen::AngleAxis<T>(rotation.y(), Vector3T::UnitY())
                * Eigen::AngleAxis<T>(rotation.x(), Vector3T::UnitX()))
                .toRotationMatrix();
        }
    }

    template <typename T>
    std::vector<typename Pose<T>::MatrixXT>
    Pose<T>::construct_rotation_matrix_gradient() const
    {
        std::vector<MatrixXT> grad_R(rot_ndof(), MatrixXT(dim(), dim()));
        if (dim() == 2) {
            // clang-format off
            grad_R[0] <<
                -sin(rotation(0)), -cos(rotation(0)),
                 cos(rotation(0)), -sin(rotation(0));
            // clang-format on
        } else {
            // Construct 3D rotation matricies
            typedef Eigen::Matrix<T, 3, 1> Vector3T;
            Eigen::Matrix<T, 3, 3> Rx
                = Eigen::AngleAxis<T>(rotation.x(), Vector3T::UnitX())
                      .toRotationMatrix();
            Eigen::Matrix<T, 3, 3> Ry
                = Eigen::AngleAxis<T>(rotation.y(), Vector3T::UnitY())
                      .toRotationMatrix();
            Eigen::Matrix<T, 3, 3> Rz
                = Eigen::AngleAxis<T>(rotation.z(), Vector3T::UnitZ())
                      .toRotationMatrix();

            // Construct gradient of each rotation matrix wrt its angle
            Eigen::Matrix<T, 3, 3> grad_Rx;
            // clang-format off
            grad_Rx <<
                0,          0,                  0,
                0, -sin(rotation.x()), -cos(rotation.x()),
                0,  cos(rotation.x()), -sin(rotation.x());
            // clang-format on
            Eigen::Matrix<T, 3, 3> grad_Ry;
            // clang-format off
            grad_Rx <<
                -sin(rotation.y()), 0,  cos(rotation.y()),
                         0,         0,          0,
                -cos(rotation.y()), 0, -sin(rotation.y());
            // clang-format on
            Eigen::Matrix<T, 3, 3> grad_Rz;
            // clang-format off
            grad_Rx <<
                -sin(rotation.z()), -cos(rotation.z()), 0,
                 cos(rotation.z()), -sin(rotation.z()), 0,
                         0,                 0,          0;
            // clang-format on
            grad_R[0] = Rz * Ry * grad_Rx;
            grad_R[1] = Rz * grad_Ry * Rx;
            grad_R[2] = grad_Rz * Ry * Rx;
        }
        return grad_R;
    }

    template <typename T>
    std::vector<std::vector<typename Pose<T>::MatrixXT>>
    Pose<T>::construct_rotation_matrix_hessian() const
    {
        std::vector<std::vector<MatrixXT>> hess_R(rot_ndof(),
            std::vector<MatrixXT>(rot_ndof(), MatrixXT(dim(), dim())));

        if (dim() == 2) {
            hess_R[0][0]
                = -Eigen::Rotation2D<T>(rotation(0)).toRotationMatrix();
        } else {
            // Construct 3D rotation matricies
            typedef Eigen::Matrix<T, 3, 1> Vector3T;
            Eigen::Matrix<T, 3, 3> Rx
                = Eigen::AngleAxis<T>(rotation.x(), Vector3T::UnitX())
                      .toRotationMatrix();
            Eigen::Matrix<T, 3, 3> Ry
                = Eigen::AngleAxis<T>(rotation.y(), Vector3T::UnitY())
                      .toRotationMatrix();
            Eigen::Matrix<T, 3, 3> Rz
                = Eigen::AngleAxis<T>(rotation.z(), Vector3T::UnitZ())
                      .toRotationMatrix();

            // Construct gradient of each rotation matrix wrt its angle
            Eigen::Matrix<T, 3, 3> grad_Rx;
            // clang-format off
            grad_Rx <<
                0,          0,                  0,
                0, -sin(rotation.x()), -cos(rotation.x()),
                0,  cos(rotation.x()), -sin(rotation.x());
            // clang-format on
            Eigen::Matrix<T, 3, 3> grad_Ry;
            // clang-format off
            grad_Rx <<
                -sin(rotation.y()), 0,  cos(rotation.y()),
                         0,         0,          0,
                -cos(rotation.y()), 0, -sin(rotation.y());
            // clang-format on
            Eigen::Matrix<T, 3, 3> grad_Rz;
            // clang-format off
            grad_Rx <<
                -sin(rotation.z()), -cos(rotation.z()), 0,
                 cos(rotation.z()), -sin(rotation.z()), 0,
                         0,                 0,          0;
            // clang-format on
            hess_R[0][0] = Rz * Ry * -Rx;          // ∂R/∂x ∂R/∂x
            hess_R[0][1] = Rz * grad_Ry * grad_Rx; // ∂R/∂x ∂R/∂y
            hess_R[0][2] = grad_Rz * Ry * grad_Rx; // ∂R/∂x ∂R/∂z
            hess_R[1][0] = Rz * grad_Ry * grad_Rx; // ∂R/∂y ∂R/∂x
            hess_R[1][1] = Rz * -Ry * Rx;          // ∂R/∂y ∂R/∂y
            hess_R[1][2] = grad_Rz * grad_Ry * Rx; // ∂R/∂y ∂R/∂z
            hess_R[2][0] = grad_Rz * Ry * grad_Rx; // ∂R/∂z ∂R/∂x
            hess_R[2][1] = grad_Rz * grad_Ry * Rx; // ∂R/∂z ∂R/∂y
            hess_R[2][2] = -Rz * Ry * Rx;          // ∂R/∂z ∂R/∂z
        }
        return hess_R;
    }

    template <typename T> Pose<T> Pose<T>::operator+(Pose<T> other) const
    {
        return Pose<T>(
            this->position + other.position, this->rotation + other.rotation);
    }

    template <typename T> Pose<T>& Pose<T>::operator+=(Pose<T> other)
    {
        this->position += other.position;
        this->rotation += other.rotation;
        return *this;
    }

    template <typename T> Pose<T> Pose<T>::operator-(Pose<T> other) const
    {
        return Pose<T>(
            this->position - other.position, this->rotation - other.rotation);
    }

    template <typename T> Pose<T>& Pose<T>::operator-=(Pose<T> other)
    {
        this->position -= other.position;
        this->rotation -= other.rotation;
        return *this;
    }

    template <typename T> Pose<T> Pose<T>::operator/(double x) const
    {
        return Pose<T>(this->position / x, this->rotation / x);
    }

    template <typename T> Pose<T> Pose<T>::operator*(double x) const
    {
        return Pose<T>(this->position * x, this->rotation * x);
    }

    template <typename T>
    Pose<T> Pose<T>::lerp_poses(Pose<T> pose0, Pose<T> pose1, double t)
    {
        return (pose1 - pose0) * t + pose0;
    }

} // namespace physics
} // namespace ccd
