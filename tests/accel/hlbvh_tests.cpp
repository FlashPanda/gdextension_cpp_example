#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

#include "accel/brute_force_accel.h"
#include "accel/acceleration_structure.h"
#include "accel/hlbvh_accel.h"
#include "accel/hlbvh_morton.h"

namespace godot_rt {

// 仅测试读取内部拓扑，生产接口不暴露节点或可变缓冲区。
struct BvhTreeTestAccess {
    static bool valid(const BvhTree& tree) {
        if (tree.triangles.empty()) return tree.root_index == -1 && tree.nodes.empty();
        std::vector<int> visited(tree.nodes.size(), 0);
        std::vector<int> primitives(tree.triangles.size(), 0);
        const auto visit = [&](const auto& self, int index) -> bool {
            if (index < 0 || static_cast<std::size_t>(index) >= visited.size() || visited[index]++) return false;
            const auto& node = tree.nodes[index];
            if (node.is_leaf()) {
                if (!node.count || node.first + node.count > tree.primitive_indices.size()) return false;
                for (std::size_t i = node.first; i < node.first + node.count; ++i) {
                    const int primitive = tree.primitive_indices[i];
                    if (primitive < 0 || static_cast<std::size_t>(primitive) >= primitives.size()) return false;
                    if (primitives[primitive]++ || !bounds_contains(node.bounds, triangle_bounds(tree.triangles[primitive]))) return false;
                }
                return true;
            }
            if (!self(self, node.left) || !self(self, node.right)) return false;
            return bounds_contains(node.bounds, tree.nodes[node.left].bounds) &&
                   bounds_contains(node.bounds, tree.nodes[node.right].bounds);
        };
        return visit(visit, tree.root_index) &&
            std::all_of(visited.begin(), visited.end(), [](int n) { return n == 1; }) &&
            std::all_of(primitives.begin(), primitives.end(), [](int n) { return n == 1; });
    }

    static std::vector<std::size_t> topology(const BvhTree& tree) {
        std::vector<std::size_t> result{static_cast<std::size_t>(tree.root_index)};
        for (const auto& node : tree.nodes) {
            result.insert(result.end(), {static_cast<std::size_t>(node.left),
                static_cast<std::size_t>(node.right), node.first, node.count});
        }
        for (int index : tree.primitive_indices) result.push_back(static_cast<std::size_t>(index));
        return result;
    }

    static std::vector<int> order(const BvhTree& tree) { return tree.primitive_indices; }

    static void rebuild_coincident_upper_roots(BvhTree& tree) {
        tree.nodes.clear();
        std::vector<int> roots;
        for (std::size_t i = 0; i < tree.triangles.size(); ++i) {
            BvhTree::Node node;
            node.bounds = triangle_bounds(tree.triangles[i]);
            node.first = i;
            node.count = 1;
            roots.push_back(static_cast<int>(tree.nodes.size()));
            tree.nodes.push_back(node);
        }
        tree.root_index = tree.build_upper_sah(roots, 0, roots.size());
    }
};

}

namespace {
using namespace godot_rt;

bool check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAILED HLBVH: " << message << '\n';
    return condition;
}

Triangle triangle(float x, float y, float z, float size = 0.2f) {
    Triangle t;
    t.p0 = godot::Vector3(x - size, y - size, z);
    t.p1 = godot::Vector3(x + size, y - size, z);
    t.p2 = godot::Vector3(x, y + size, z);
    t.n0 = t.n1 = t.n2 = godot::Vector3(0, 0, 1);
    t.uv0 = godot::Vector2(0, 0);
    t.uv1 = godot::Vector2(1, 0);
    t.uv2 = godot::Vector2(0.5f, 1);
    t.materialId = 7;
    return t;
}

bool same_hit(const Hit& a, const Hit& b) {
    const auto near = [](real_t x, real_t y) { return std::abs(x - y) < 0.0001f; };
    return a.hit == b.hit && near(a.t, b.t) && a.triangleIndex == b.triangleIndex && a.materialId == b.materialId &&
        near(a.position.x, b.position.x) && near(a.position.y, b.position.y) && near(a.position.z, b.position.z) &&
        near(a.normal.x, b.normal.x) && near(a.normal.y, b.normal.y) && near(a.normal.z, b.normal.z) &&
        near(a.wo.x, b.wo.x) && near(a.wo.y, b.wo.y) && near(a.wo.z, b.wo.z) &&
        near(a.uv.x, b.uv.x) && near(a.uv.y, b.uv.y);
}

bool equivalent(const Scene& scene, const BvhTree& tree) {
    BruteForceAccel brute;
    brute.build(scene);
    std::vector<Ray> rays{
        Ray(godot::Vector3(0, 0, 5), godot::Vector3(0, 0, -1)),
        Ray(godot::Vector3(0, 0, 5), godot::Vector3(1, 0, 0))
    };
    for (const auto& t : scene.get_triangles()) {
        const auto center = (t.p0 + t.p1 + t.p2) / 3.0f;
        rays.emplace_back(center + godot::Vector3(0, 0, 5), godot::Vector3(0, 0, -1));
    }
    std::mt19937 rng(20261017);
    std::uniform_real_distribution<float> position(-20, 20);
    for (int i = 0; i < 300; ++i) {
        const float x = position(rng), y = position(rng);
        rays.emplace_back(godot::Vector3(x, y, 10), godot::Vector3(0.1f, -0.1f, -1));
    }
    for (const auto& ray : rays) {
        for (real_t limit : {real_t(-1), real_t(0), real_t(4.99), real_t(5), real_t(5.01), real_t(Math_INF)}) {
            Hit expected, actual;
            const bool hit = brute.intersect(ray, &expected, limit);
            if (!check(tree.intersect(ray, &actual, limit) == hit, "closest-hit result")) return false;
            if (hit && !check(same_hit(expected, actual), "all Hit fields including tie-breaking")) {
                std::cerr << "scene=" << scene.triangle_count() << " ray=" << ray.o.x << ',' << ray.o.y << ',' << ray.o.z
                          << " limit=" << limit << " expected=" << expected.triangleIndex << ':' << expected.t
                          << " actual=" << actual.triangleIndex << ':' << actual.t << '\n';
                return false;
            }
            if (!check(tree.intersect_p(ray, limit) == brute.intersect_p(ray, limit), "predicate and t_max")) return false;
        }
    }
    return true;
}

bool morton_tests() {
    AxisAlignedBounds bounds;
    bounds.valid = true;
    bounds.minimum = godot::Vector3(0, 0, 0);
    bounds.maximum = godot::Vector3(1, 1, 1);
    bool ok = check(detail::encode_morton_3d(bounds.minimum, bounds) == 0, "minimum Morton code");
    ok &= check(detail::encode_morton_3d(bounds.maximum, bounds) == 0x3fffffffu, "maximum Morton clamp");
    ok &= check(detail::encode_morton_3d(godot::Vector3(1.0f / 1024, 0, 0), bounds) == 1, "x bit");
    ok &= check(detail::encode_morton_3d(godot::Vector3(0, 1.0f / 1024, 0), bounds) == 2, "y bit");
    ok &= check(detail::encode_morton_3d(godot::Vector3(0, 0, 1.0f / 1024), bounds) == 4, "z bit");
    bounds.maximum.y = bounds.maximum.z = 0;
    ok &= check(detail::encode_morton_3d(bounds.maximum, bounds) == 0x09249249u, "zero extent axes");
    std::vector<detail::MortonPrimitive> morton;
    std::mt19937 rng(17);
    for (int i = 0; i < 1000; ++i) morton.push_back({i, rng() & 0x3fffffffu});
    morton.push_back({1000, morton[0].code});
    auto expected = morton;
    std::stable_sort(expected.begin(), expected.end(), [](const auto& a, const auto& b) { return a.code < b.code; });
    detail::radix_sort_morton(morton);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        ok &= check(expected[i].primitive_index == morton[i].primitive_index && expected[i].code == morton[i].code,
                    "stable full 30-bit radix sort");
    }
    return ok;
}

bool boundary_tests() {
    BvhTree tree(BvhSplitMethod::Hlbvh);
    Scene scene;
    tree.build(scene);
    bool ok = check(BvhTreeTestAccess::valid(tree), "empty build") && equivalent(scene, tree);
    const auto flat_bounds = triangle_bounds(triangle(0, 0, 0));
    ok &= check(intersect_bounds(flat_bounds, Ray(godot::Vector3(0, 0, 5), godot::Vector3(0, 0, -1)),
                               std::nextafter(real_t(5), real_t(0))), "conservative slab includes rounded triangle distance");
    scene.add_triangle(triangle(0, 0, 0));
    tree.build(scene);
    ok &= check(tree.node_count() == 1, "single primitive") && equivalent(scene, tree);
    for (int i = 1; i < 9; ++i) scene.add_triangle(triangle(0, 0, 0));
    tree.build(scene);
    ok &= check(tree.node_count() == 1, "Morton collision terminates with oversized leaf");
    std::vector<int> expected_order(9);
    std::iota(expected_order.begin(), expected_order.end(), 0);
    ok &= check(BvhTreeTestAccess::order(tree) == expected_order, "collision order is stable");
    ok &= equivalent(scene, tree);

    BvhTreeTestAccess::rebuild_coincident_upper_roots(tree);
    ok &= check(tree.node_count() == 17 && tree.root_left_primitive_count() == 4 && BvhTreeTestAccess::valid(tree),
                "upper coincident-centroid fallback makes two nonempty subtrees");
    ok &= equivalent(scene, tree);

    scene.clear();
    for (int i = 0; i < 8; ++i) scene.add_triangle(triangle(i * 0.5f, 0, 0));
    scene.add_triangle(triangle(100, 0, 0));
    tree.build(scene);
    ok &= check(tree.root_left_primitive_count() == 8, "upper SAH separates distant treelet");
    ok &= check(tree.node_count() > 3 && BvhTreeTestAccess::valid(tree), "LBVH splits low bits within treelet");
    ok &= equivalent(scene, tree);

    // 大三角形产生高度重叠的子树包围盒，遍历仍需选择原始索引对应的最近命中。
    scene.clear();
    for (float sign : {-1.0f, 1.0f}) {
        scene.add_triangle(triangle(sign * 3, 0, 0, 7));
        scene.add_triangle(triangle(sign * 6, 0, 0, 16));
    }
    tree.build(scene);
    ok &= check(BvhTreeTestAccess::valid(tree), "upper tree remains valid for overlapping bounds");
    ok &= equivalent(scene, tree);

    scene.clear();
    for (int i = 0; i < 20; ++i) scene.add_triangle(triangle(float(i), 0, 0, 0));
    tree.build(scene);
    ok &= check(BvhTreeTestAccess::valid(tree), "zero-area upper SAH falls back") && equivalent(scene, tree);
    scene.clear();
    tree.build(scene);
    ok &= check(BvhTreeTestAccess::valid(tree), "rebuild clears old root") && equivalent(scene, tree);
    return ok;
}

bool parallel_tests() {
    Scene scene;
    for (int i = 0; i < 2300; ++i) scene.add_triangle(triangle(float(i % 47) - 23, float(i / 47) - 24, float(i % 5)));
    BvhTree serial(BvhSplitMethod::Hlbvh), parallel(BvhSplitMethod::Hlbvh);
    BvhBuildOptions options;
    options.thread_count = 1;
    serial.build(scene, options);
    options.thread_count = 3;
    std::atomic<int> finished_helpers{0};
    options.thread_launcher = [&](std::function<void()> entry, JoinableThread* out) {
        const int before = finished_helpers.load();
        const bool started = JoinableThread::start([&, entry] { entry(); ++finished_helpers; }, out);
        // 暂不返回 launcher，让首个 helper 独立完成任务，验证构建任务确实能在辅助线程执行。
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (started && finished_helpers.load() == before && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
        return started;
    };
    parallel.build(scene, options);
    bool ok = check(finished_helpers >= 2, "Morton and treelet helpers executed and joined");
    ok &= check(BvhTreeTestAccess::valid(parallel), "parallel tree topology and primitive coverage");
    ok &= check(BvhTreeTestAccess::topology(serial) == BvhTreeTestAccess::topology(parallel), "parallel deterministic topology");
    options.thread_launcher = {};
    parallel.build(scene, options);
    ok &= check(BvhTreeTestAccess::topology(serial) == BvhTreeTestAccess::topology(parallel), "concurrent repeated build");
    int launch_attempts = 0;
    options.thread_launcher = [&](std::function<void()> entry, JoinableThread* out) {
        ++launch_attempts;
        return launch_attempts % 2 == 1 && JoinableThread::start(std::move(entry), out);
    };
    parallel.build(scene, options);
    ok &= check(launch_attempts == 4, "partial startup failure in both phases");
    ok &= check(BvhTreeTestAccess::topology(serial) == BvhTreeTestAccess::topology(parallel), "partial failure completes all primitives");
    options.thread_launcher = [](std::function<void()>, JoinableThread*) { return false; };
    parallel.build(scene, options);
    ok &= check(BvhTreeTestAccess::topology(serial) == BvhTreeTestAccess::topology(parallel), "total startup failure falls back to caller");
    ok &= equivalent(scene, parallel);
    std::atomic<bool> readers_ok{true};
    run_parallel_tasks(8, 3, [&](std::size_t) {
        Hit hit;
        const Ray ray(godot::Vector3(0, 0, 10), godot::Vector3(0, 0, -1));
        for (int i = 0; i < 100; ++i) {
            Hit reference;
            if (parallel.intersect(ray, &hit) != serial.intersect(ray, &reference) ||
                (hit.hit && !same_hit(hit, reference))) readers_ok = false;
        }
    });
    ok &= check(readers_ok, "concurrent read-only traversal after build");
    return ok;
}
}

bool run_hlbvh_tests() {
    static_assert(static_cast<int>(godot_rt::AccelerationType::BruteForce) == 0);
    static_assert(static_cast<int>(godot_rt::AccelerationType::Bvh) == 1);
    static_assert(static_cast<int>(godot_rt::AccelerationType::Octree) == 2);
    static_assert(static_cast<int>(godot_rt::AccelerationType::Sah) == 3);
    bool ok = morton_tests();
    ok &= boundary_tests();
    ok &= parallel_tests();
    auto accel = godot_rt::create_acceleration_structure(godot_rt::AccelerationType::Hlbvh);
    ok &= check(dynamic_cast<godot_rt::HlbvhAccel*>(accel.get()) != nullptr, "dedicated factory class");
    godot_rt::Scene scene;
    scene.add_triangle(triangle(0, 0, 0));
    accel->build(scene);
    godot_rt::Hit hit;
    const godot_rt::Ray ray(godot::Vector3(0, 0, 1), godot::Vector3(0, 0, -1));
    ok &= check(accel->intersect(ray, &hit) && hit.triangleIndex == 0 && accel->intersect_p(ray), "wrapper delegates traversal");
    return ok;
}
