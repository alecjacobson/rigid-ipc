// An interval object.
#pragma once

#include <string>

#include <boost/numeric/interval.hpp>

#define USE_FILIB_INTERVALS
#ifdef USE_FILIB_INTERVALS
#include <interval/filib_rounding.hpp>
#endif

#include <utils/eigen_ext.hpp>

namespace ipc::rigid {

namespace interval_options {
    typedef boost::numeric::interval_lib::checking_catch_nan<double>
        CheckingPolicy;
} // namespace interval_options

#ifdef USE_FILIB_INTERVALS

// Use filib rounding arithmetic
typedef boost::numeric::interval<
    double,
    boost::numeric::interval_lib::policies<
        boost::numeric::interval_lib::save_state<FILibRounding>,
        interval_options::CheckingPolicy>>
    Interval;

#elif defined(__APPLE__)

// clang-format off
#warning "Rounding modes seem to be broken with trigonometric functions on macOS, unable to compute exact interval arithmetic!"
// clang-format on
typedef boost::numeric::interval<
    double,
    boost::numeric::interval_lib::policies<
        boost::numeric::interval_lib::save_state<
            boost::numeric::interval_lib::rounded_transc_exact<double>>,
        interval_options::CheckingPolicy>>
    Interval;

#else

// Use proper rounding arithmetic
typedef boost::numeric::interval<
    double,
    boost::numeric::interval_lib::policies<
        boost::numeric::interval_lib::save_state<
            boost::numeric::interval_lib::rounded_transc_std<double>>,
        interval_options::CheckingPolicy>>
    Interval;

#endif // USE_FILIB_INTERVALS

template <typename Derived>
inline Eigen::VectorXd width(const Eigen::MatrixBase<Derived>& x)
{
    Eigen::VectorXd w(x.size());
    for (int i = 0; i < x.size(); i++) {
        w(i) = width(x(i));
    }
    return w;
}

template <typename Derived>
inline double diagonal_width(const Eigen::MatrixBase<Derived>& x)
{
    Eigen::VectorXd widths = width(x);
    double w = 0;
    for (int i = 0; i < widths.size(); i++) {
        w += widths(i) * widths(i);
    }
    return sqrt(w);
}

template <typename Derived>
inline bool zero_in(const Eigen::MatrixBase<Derived>& x)
{
    // Check if the origin is in the n-dimensional interval
    for (int i = 0; i < x.size(); i++) {
        if (!boost::numeric::zero_in(x(i))) {
            return false;
        }
    }
    return true;
}

typedef Vector2<Interval> Vector2I;
typedef Vector3<Interval> Vector3I;
typedef VectorX<Interval> VectorXI;
typedef VectorMax3<Interval> VectorMax3I;
typedef Matrix3<Interval> Matrix2I;
typedef Matrix3<Interval> Matrix3I;
typedef MatrixMax3<Interval> MatrixMax3I;
typedef MatrixX<Interval> MatrixXI;

/// @brief Format a string for an Interval
std::string fmt_interval(const Interval& i, const int precision = 16);
/// @brief Format an eigen VectorX<Interval>
std::string fmt_eigen_intervals(const VectorXI& x, const int precision = 16);
} // namespace ipc::rigid

namespace Eigen {

template <typename BinOp>
struct ScalarBinaryOpTraits<ipc::rigid::Interval, double, BinOp> {
    typedef ipc::rigid::Interval ReturnType;
};

template <typename BinOp>
struct ScalarBinaryOpTraits<double, ipc::rigid::Interval, BinOp> {
    typedef ipc::rigid::Interval ReturnType;
};

#if EIGEN_MAJOR_VERSION >= 3
namespace internal {
    template <typename X, typename S, typename P>
    struct is_convertible<X, boost::numeric::interval<S, P>> {
        enum { value = is_convertible<X, S>::value };
    };

    template <typename S, typename P1, typename P2>
    struct is_convertible<
        boost::numeric::interval<S, P1>,
        boost::numeric::interval<S, P2>> {
        enum { value = true };
    };
} // namespace internal
#endif
} // namespace Eigen
