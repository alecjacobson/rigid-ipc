#include <catch2/catch.hpp>

#include <geometry/distance.hpp>
#include <logger.hpp>
#include <utils/not_implemented_error.hpp>

using namespace ccd::geometry;

//-----------------------------------------------------------------------------
// Unsigned Distances
//-----------------------------------------------------------------------------

TEST_CASE("Point-point distance", "[distance]")
{
    int dim = GENERATE(2, 3);
    Eigen::VectorX3d p0 = Eigen::VectorX3d::Zero(dim);
    Eigen::VectorX3d p1 = Eigen::VectorX3d::Zero(dim);
    double expected_distance = GENERATE(-10, -1, -1e-12, 0, 1e-12, 1, 10);
    SECTION("Aligned with X-axis") { p1(0) = expected_distance; }
    SECTION("Diagonal vector")
    {
        p1.setOnes();
        p1.normalize();
        p1 *= expected_distance;
    }
    double distance = point_point_distance<double>(p0, p1);
    CHECK(distance == Approx(abs(expected_distance)));
}

TEST_CASE("Point-segment distance", "[distance]")
{
    int dim = GENERATE(2, 3);
    double expected_distance = GENERATE(-10, -1, -1e-12, 0, 1e-12, 1, 10);
    Eigen::VectorX3d p = Eigen::VectorX3d::Zero(dim);
    p.y() = expected_distance;
    Eigen::VectorX3d s0 = Eigen::VectorX3d::Zero(dim);
    s0.x() = -10;
    Eigen::VectorX3d s1 = Eigen::VectorX3d::Zero(dim);
    s1.x() = 10;

    double distance = point_segment_distance<double>(p, s0, s1);
    CHECK(distance == Approx(abs(expected_distance)));
}

TEST_CASE("Segment-segment distance", "[distance]")
{
    // TODO
}

TEST_CASE("Point-triangle distance", "[distance]")
{
    double py = GENERATE(-10, -1, -1e-12, 0, 1e-12, 1, 10);
    Eigen::Vector3d p(0, py, 0);
    Eigen::Vector3d t0(-1, 0, 1);
    Eigen::Vector3d t1(1, 0, 1);
    Eigen::Vector3d t2(0, 0, -1);

    Eigen::Vector3d closest_point;
    SECTION("closest to triangle")
    {
        double pz = GENERATE(0, -1 + 1e-12, -1, 1, 1 - 1e-12);
        p.z() = pz;
        closest_point = p;
        closest_point.y() = 0;
    }
    SECTION("closest to t0")
    {
        double px = GENERATE(-1, -1 - 1e-12, -11);
        p.x() = px;
        p.z() = t0.z();
        closest_point = t0;
    }
    SECTION("closest to t1")
    {
        double px = GENERATE(1, 1 + 1e-12, 11);
        p.x() = px;
        p.z() = t1.z();
        closest_point = t1;
    }
    SECTION("closest to t2")
    {
        double pz = GENERATE(-1, -1 - 1e-12, -11);
        p.z() = pz;
        closest_point = t2;
    }
    SECTION("closest to t0t1")
    {
        double alpha = GENERATE(0.0, 1e-4, 0.5, 1.0 - 1e-4, 1.0);
        closest_point = (t1 - t0) * alpha + t0;
        Eigen::Vector2d perp = segment_normal(
            Eigen::Vector2d(t0.x(), t0.z()), Eigen::Vector2d(t1.x(), t1.z()));
        double scale = GENERATE(0, 1e-12, 1e-4, 1, 2, 11, 1000);
        p.x() = closest_point.x() + scale * perp.x();
        p.z() = closest_point.z() + scale * perp.y();
    }
    SECTION("closest to t1t2")
    {
        double alpha = GENERATE(0.0, 1e-4, 0.5, 1.0 - 1e-4, 1.0);
        closest_point = (t2 - t1) * alpha + t1;
        Eigen::Vector2d perp = segment_normal(
            Eigen::Vector2d(t1.x(), t1.z()), Eigen::Vector2d(t2.x(), t2.z()));
        double scale = GENERATE(0, 1e-12, 1e-4, 1, 2, 11, 1000);
        p.x() = closest_point.x() + scale * perp.x();
        p.z() = closest_point.z() + scale * perp.y();
    }
    SECTION("closest to t2t0")
    {
        double alpha = GENERATE(0.0, 1e-4, 0.5, 1.0 - 1e-4, 1.0);
        closest_point = (t0 - t2) * alpha + t2;
        Eigen::Vector2d perp = segment_normal(
            Eigen::Vector2d(t2.x(), t2.z()), Eigen::Vector2d(t0.x(), t0.z()));
        double scale = GENERATE(0, 1e-12, 1e-4, 1, 2, 11, 1000);
        p.x() = closest_point.x() + scale * perp.x();
        p.z() = closest_point.z() + scale * perp.y();
    }

    double distance = point_triangle_distance(p, t0, t1, t2);
    CAPTURE(py, closest_point.x(), closest_point.y(), closest_point.z());
    CHECK(
        distance
        == Approx(point_point_distance(p, closest_point)).margin(1e-12));
}

//-----------------------------------------------------------------------------
// Signed Distances
//-----------------------------------------------------------------------------

template <typename T> int sign(T val) { return (T(0) < val) - (val < T(0)); }

TEST_CASE("Point-line signed distance", "[distance]")
{
    double expected_distance = GENERATE(-10, -1, -1e-4, 0, 1e-4, 1, 10);
    Eigen::Vector2d p = Eigen::Vector2d::Random();
    p.y() = expected_distance;
    Eigen::Vector2d s0(-9, 0);
    Eigen::Vector2d s1(-10, 0);

    double distance = point_line_signed_distance(p, s0, s1);
    CAPTURE(distance, expected_distance);
    CHECK(sign(distance) == sign(expected_distance));
}

TEST_CASE("Line-line signed distance", "[distance]")
{
    double expected_distance = GENERATE(-10, -1, -1e-4, 0, 1e-4, 1, 10);
    Eigen::Vector3d line0_point0(-9.9, expected_distance, 0);
    Eigen::Vector3d line0_point1(-10, expected_distance, 0);
    Eigen::Vector3d line1_point0(0, 0, -10);
    Eigen::Vector3d line1_point1(0, 0, -9.9);

    double distance = line_line_signed_distance(
        line0_point0, line0_point1, line1_point0, line1_point1);
    CAPTURE(distance, expected_distance);
    CHECK(sign(distance) == sign(expected_distance));
}

TEST_CASE("Point-plane signed distance", "[distance]")
{
    double expected_distance = GENERATE(-10, -1, -1e-4, 0, 1e-4, 1, 10);
    Eigen::Vector3d p = Eigen::Vector3d::Random();
    p.y() = expected_distance;
    Eigen::Vector3d p0 = Eigen::Vector3d::Random();
    Eigen::Vector3d p1 = Eigen::Vector3d::Random();
    Eigen::Vector3d p2 = Eigen::Vector3d::Random();
    p0.y() = p1.y() = p2.y() = 0;
    if (Eigen::Vector3d::UnitY().dot((p1 - p0).cross(p2 - p0)) < 0) {
        std::swap(p1, p2);
    }

    double distance = point_plane_signed_distance(p, p0, p1, p2);
    CAPTURE(distance, expected_distance);
    CHECK(sign(distance) == sign(expected_distance));
}
