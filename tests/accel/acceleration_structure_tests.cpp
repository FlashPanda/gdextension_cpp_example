#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "accel/acceleration_structure.h"
#include "accel/brute_force_accel.h"
#include "accel/bvh_accel.h"
#include "accel/octree_accel.h"

namespace {

constexpr float EPSILON = 0.0001f;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool nearly_equal(float left, float right) {
    return std::abs(left - right) <= EPSILON;
}

bool vector_nearly_equal(const godot::Vector3& left, const godot::Vector3& right) {
    return nearly_equal(left.x, right.x) &&
           nearly_equal(left.y, right.y) &&
           nearly_equal(left.z, right.z);
}

bool vector_nearly_equal(const godot::Vector2& left, const godot::Vector2& right) {
    return nearly_equal(left.x, right.x) && nearly_equal(left.y, right.y);
}

godot_rt::Triangle make_triangle(float z, int material_id) {
    godot_rt::Triangle triangle;
    triangle.p0 = godot::Vector3(-1.0f, -1.0f, z);
    triangle.p1 = godot::Vector3(1.0f, -1.0f, z);
    triangle.p2 = godot::Vector3(0.0f, 1.0f, z);
    triangle.n0 = godot::Vector3(0.0f, 0.0f, 1.0f);
    triangle.n1 = godot::Vector3(0.0f, 0.0f, 1.0f);
    triangle.n2 = godot::Vector3(0.0f, 0.0f, 1.0f);
    triangle.uv0 = godot::Vector2(0.0f, 0.0f);
    triangle.uv1 = godot::Vector2(1.0f, 0.0f);
    triangle.uv2 = godot::Vector2(0.5f, 1.0f);
    triangle.materialId = material_id;
    return triangle;
}

godot_rt::Scene make_reference_scene() {
    godot_rt::Scene scene;
    scene.add_triangle(make_triangle(0.0f, 10));
    scene.add_triangle(make_triangle(-2.0f, 20));

    godot_rt::Triangle spanning = make_triangle(-4.0f, 30);
    spanning.p0 = godot::Vector3(-8.0f, -8.0f, -4.0f);
    spanning.p1 = godot::Vector3(8.0f, -8.0f, -4.0f);
    spanning.p2 = godot::Vector3(0.0f, 8.0f, -4.0f);
    scene.add_triangle(spanning);

    godot_rt::Triangle degenerate;
    degenerate.p0 = godot::Vector3(4.0f, 4.0f, 4.0f);
    degenerate.p1 = degenerate.p0;
    degenerate.p2 = degenerate.p0;
    degenerate.materialId = 40;
    scene.add_triangle(degenerate);
    return scene;
}

godot_rt::Scene make_partitioned_scene(std::vector<godot_rt::Ray>* out_rays) {
    godot_rt::Scene scene;
    int material_id = 100;
    for (int y = -2; y <= 1; ++y) {
        for (int x = -2; x <= 2; ++x) {
            const float center_x = static_cast<float>(x) * 2.0f;
            const float center_y = static_cast<float>(y) * 2.0f;
            godot_rt::Triangle triangle = make_triangle(0.0f, material_id++);
            triangle.p0 = godot::Vector3(center_x - 0.4f, center_y - 0.4f, 0.0f);
            triangle.p1 = godot::Vector3(center_x + 0.4f, center_y - 0.4f, 0.0f);
            triangle.p2 = godot::Vector3(center_x, center_y + 0.4f, 0.0f);
            scene.add_triangle(triangle);
            if (out_rays != nullptr) {
                out_rays->push_back(godot_rt::Ray(
                    godot::Vector3(center_x, center_y, 3.0f),
                    godot::Vector3(0.0f, 0.0f, -1.0f)
                ));
            }
        }
    }

    godot_rt::Triangle spanning = make_triangle(-2.0f, material_id);
    spanning.p0 = godot::Vector3(-8.0f, -8.0f, -2.0f);
    spanning.p1 = godot::Vector3(8.0f, -8.0f, -2.0f);
    spanning.p2 = godot::Vector3(0.0f, 8.0f, -2.0f);
    scene.add_triangle(spanning);
    if (out_rays != nullptr) {
        out_rays->push_back(godot_rt::Ray(
            godot::Vector3(1.0f, 1.0f, -0.5f),
            godot::Vector3(0.0f, 0.0f, -1.0f)
        ));
    }
    return scene;
}

bool hits_equal(const godot_rt::Hit& expected, const godot_rt::Hit& actual) {
    return expected.hit == actual.hit &&
           nearly_equal(expected.t, actual.t) &&
           expected.triangleIndex == actual.triangleIndex &&
           expected.materialId == actual.materialId &&
           vector_nearly_equal(expected.position, actual.position) &&
           vector_nearly_equal(expected.wo, actual.wo) &&
           vector_nearly_equal(expected.normal, actual.normal) &&
           vector_nearly_equal(expected.uv, actual.uv);
}

bool compare_against_brute_force(const godot_rt::Scene& scene,
                                 const std::vector<godot_rt::Ray>& rays,
                                 float t_max) {
    godot_rt::BruteForceAccel brute_force;
    godot_rt::BvhAccel bvh;
    godot_rt::OctreeAccel octree;
    brute_force.build(scene);
    bvh.build(scene);
    octree.build(scene);

    bool passed = true;
    for (const godot_rt::Ray& ray : rays) {
        godot_rt::Hit expected;
        godot_rt::Hit bvh_hit;
        godot_rt::Hit octree_hit;
        const bool expected_result = brute_force.intersect(ray, &expected, t_max);
        const bool bvh_result = bvh.intersect(ray, &bvh_hit, t_max);
        const bool octree_result = octree.intersect(ray, &octree_hit, t_max);

        passed &= expect(bvh_result == expected_result, "BVH hit result matches brute force");
        passed &= expect(octree_result == expected_result, "Octree hit result matches brute force");
        if (expected_result) {
            passed &= expect(hits_equal(expected, bvh_hit), "BVH nearest Hit matches brute force");
            passed &= expect(hits_equal(expected, octree_hit), "Octree nearest Hit matches brute force");
        }

        passed &= expect(
            bvh.intersect_p(ray, t_max) == brute_force.intersect_p(ray, t_max),
            "BVH predicate hit matches brute force"
        );
        passed &= expect(
            octree.intersect_p(ray, t_max) == brute_force.intersect_p(ray, t_max),
            "Octree predicate hit matches brute force"
        );
    }
    return passed;
}

bool test_acceleration_type_selection() {
    bool passed = true;
    godot_rt::AccelerationType type = godot_rt::AccelerationType::BruteForce;

    passed &= expect(godot_rt::parse_acceleration_type("brute_force", &type), "parse brute_force");
    passed &= expect(type == godot_rt::AccelerationType::BruteForce, "brute_force enum");
    passed &= expect(godot_rt::parse_acceleration_type("none", &type), "parse none alias");
    passed &= expect(type == godot_rt::AccelerationType::BruteForce, "none maps to brute force");
    passed &= expect(godot_rt::parse_acceleration_type("BVH", &type), "parse BVH case-insensitively");
    passed &= expect(type == godot_rt::AccelerationType::Bvh, "BVH enum");
    passed &= expect(godot_rt::parse_acceleration_type("octree", &type), "parse octree");
    passed &= expect(type == godot_rt::AccelerationType::Octree, "octree enum");
    passed &= expect(!godot_rt::parse_acceleration_type("grid", &type), "reject unknown acceleration");

    const std::vector<godot_rt::AccelerationType> types{
        godot_rt::AccelerationType::BruteForce,
        godot_rt::AccelerationType::Bvh,
        godot_rt::AccelerationType::Octree,
    };
    for (const godot_rt::AccelerationType selected : types) {
        std::unique_ptr<godot_rt::AccelInterface> accel = godot_rt::create_acceleration_structure(selected);
        passed &= expect(accel != nullptr, "factory creates every acceleration structure");
        passed &= expect(std::string(godot_rt::acceleration_type_name(selected)).size() > 0,
                         "every acceleration type has a stable name");
    }
    return passed;
}

bool test_intersection_equivalence() {
    const godot_rt::Scene scene = make_reference_scene();
    std::vector<godot_rt::Ray> rays{
        godot_rt::Ray(godot::Vector3(0.0f, 0.0f, 3.0f), godot::Vector3(0.0f, 0.0f, -1.0f)),
        godot_rt::Ray(godot::Vector3(0.8f, -0.8f, 3.0f), godot::Vector3(0.0f, 0.0f, -1.0f)),
        godot_rt::Ray(godot::Vector3(5.0f, -5.0f, 3.0f), godot::Vector3(0.0f, 0.0f, -1.0f)),
        godot_rt::Ray(godot::Vector3(20.0f, 20.0f, 3.0f), godot::Vector3(0.0f, 0.0f, -1.0f)),
        godot_rt::Ray(godot::Vector3(0.0f, 0.0f, 3.0f), godot::Vector3(1.0f, 0.0f, 0.0f)),
        godot_rt::Ray(godot::Vector3(0.0f, 0.0f, -1.0f), godot::Vector3(0.0f, 0.0f, -1.0f)),
    };

    bool passed = compare_against_brute_force(scene, rays, Math_INF);
    passed &= compare_against_brute_force(scene, rays, 2.5f);

    godot_rt::Scene empty_scene;
    passed &= compare_against_brute_force(empty_scene, rays, Math_INF);

    std::vector<godot_rt::Ray> partition_rays;
    const godot_rt::Scene partitioned_scene = make_partitioned_scene(&partition_rays);
    passed &= expect(partitioned_scene.triangle_count() > 8, "partition scene forces Octree subdivision");
    passed &= compare_against_brute_force(partitioned_scene, partition_rays, Math_INF);
    passed &= compare_against_brute_force(partitioned_scene, partition_rays, 1.0f);
    return passed;
}

} // namespace

int main() {
    bool passed = true;
    passed &= test_acceleration_type_selection();
    passed &= test_intersection_equivalence();
    if (!passed) {
        return 1;
    }

    std::cout << "All acceleration structure tests passed.\n";
    return 0;
}
