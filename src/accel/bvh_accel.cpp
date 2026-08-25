#include "bvh_accel.h"

#include <algorithm>
#include <numeric>

namespace godot_rt {
namespace {

    constexpr std::size_t LEAF_TRIANGLE_COUNT = 4;
    constexpr real_t CENTROID_EPSILON = 0.000000001;

    real_t component(const godot::Vector3& value, int axis) {
        switch (axis) {
            case 0:
                return value.x;
            case 1:
                return value.y;
            default:
                return value.z;
        }
    }

    int largest_axis(const godot::Vector3& extent) {
        if (extent.y > extent.x && extent.y >= extent.z) {
            return 1;
        }
        return extent.z > extent.x ? 2 : 0;
    }

}

void BvhAccel::build(const Scene& scene) {
    // 生命周期：build 复制 world-space 三角形并建立只读节点；后续 tile 线程只共享读取这些数组。
    triangles = scene.get_triangles();
    primitive_indices.resize(triangles.size());
    std::iota(primitive_indices.begin(), primitive_indices.end(), 0);
    nodes.clear();
    nodes.reserve(triangles.empty() ? 0 : triangles.size() * 2 - 1);
    if (!triangles.empty()) {
        build_node(0, triangles.size());
    }
}

int BvhAccel::build_node(std::size_t first, std::size_t count) {
    AxisAlignedBounds bounds;
    AxisAlignedBounds centroid_bounds;
    for (std::size_t offset = 0; offset < count; ++offset) {
        const AxisAlignedBounds primitive_bounds = triangle_bounds(triangles[primitive_indices[first + offset]]);
        bounds = union_bounds(bounds, primitive_bounds);

        const godot::Vector3 centroid = bounds_centroid(primitive_bounds);
        AxisAlignedBounds point_bounds;
        point_bounds.minimum = centroid;
        point_bounds.maximum = centroid;
        point_bounds.valid = true;
        centroid_bounds = union_bounds(centroid_bounds, point_bounds);
    }

    const int node_index = static_cast<int>(nodes.size());
    nodes.push_back(Node());
    nodes[node_index].bounds = bounds;
    nodes[node_index].first = first;
    nodes[node_index].count = count;

    if (count <= LEAF_TRIANGLE_COUNT) {
        return node_index;
    }

    const godot::Vector3 centroid_extent = bounds_extent(centroid_bounds);
    const int axis = largest_axis(centroid_extent);
    if (component(centroid_extent, axis) <= CENTROID_EPSILON) {
        return node_index;
    }

    const std::size_t middle = first + count / 2;
    std::nth_element(
        primitive_indices.begin() + static_cast<std::ptrdiff_t>(first),
        primitive_indices.begin() + static_cast<std::ptrdiff_t>(middle),
        primitive_indices.begin() + static_cast<std::ptrdiff_t>(first + count),
        [this, axis](int left, int right) {
            const real_t left_centroid = component(bounds_centroid(triangle_bounds(triangles[left])), axis);
            const real_t right_centroid = component(bounds_centroid(triangle_bounds(triangles[right])), axis);
            return left_centroid == right_centroid ? left < right : left_centroid < right_centroid;
        }
    );

    // 节点只保存索引，递归期间 `nodes` 扩容不会留下失效引用。
    const int left = build_node(first, middle - first);
    const int right = build_node(middle, first + count - middle);
    nodes[node_index].left = left;
    nodes[node_index].right = right;
    nodes[node_index].count = 0;
    return node_index;
}

bool BvhAccel::intersect(const Ray& ray, Hit* hit, real_t t_max) const {
    if (nodes.empty()) {
        return false;
    }

    real_t closest_t = t_max;
    int closest_index = -1;
    TriangleIntersection closest_intersection;
    intersect_node(0, ray, &closest_t, &closest_index, &closest_intersection);
    if (closest_index < 0) {
        return false;
    }

    fill_triangle_hit(triangles[closest_index], closest_index, ray, closest_intersection, hit);
    return true;
}

void BvhAccel::intersect_node(int node_index, const Ray& ray, real_t* closest_t,
                              int* closest_index, TriangleIntersection* closest_intersection) const {
    const Node& node = nodes[node_index];
    if (!intersect_bounds(node.bounds, ray, *closest_t)) {
        return;
    }

    if (node.is_leaf()) {
        for (std::size_t offset = 0; offset < node.count; ++offset) {
            const int primitive_index = primitive_indices[node.first + offset];
            TriangleIntersection intersection;
            if (intersect_triangle(triangles[primitive_index], ray, *closest_t, &intersection) &&
                is_better_triangle_hit(intersection.t, primitive_index, *closest_t, *closest_index)) {
                *closest_t = intersection.t;
                *closest_index = primitive_index;
                *closest_intersection = intersection;
            }
        }
        return;
    }

    real_t left_enter = 0.0;
    real_t right_enter = 0.0;
    const bool hit_left = intersect_bounds(nodes[node.left].bounds, ray, *closest_t, &left_enter);
    const bool hit_right = intersect_bounds(nodes[node.right].bounds, ray, *closest_t, &right_enter);
    if (hit_left && hit_right && right_enter < left_enter) {
        intersect_node(node.right, ray, closest_t, closest_index, closest_intersection);
        intersect_node(node.left, ray, closest_t, closest_index, closest_intersection);
    } else {
        if (hit_left) {
            intersect_node(node.left, ray, closest_t, closest_index, closest_intersection);
        }
        if (hit_right) {
            intersect_node(node.right, ray, closest_t, closest_index, closest_intersection);
        }
    }
}

bool BvhAccel::intersect_p(const Ray& ray, real_t t_max) const {
    return !nodes.empty() && intersect_node_p(0, ray, t_max);
}

bool BvhAccel::intersect_node_p(int node_index, const Ray& ray, real_t t_max) const {
    const Node& node = nodes[node_index];
    if (!intersect_bounds(node.bounds, ray, t_max)) {
        return false;
    }

    if (node.is_leaf()) {
        for (std::size_t offset = 0; offset < node.count; ++offset) {
            const int primitive_index = primitive_indices[node.first + offset];
            if (intersect_triangle(triangles[primitive_index], ray, t_max, nullptr)) {
                return true;
            }
        }
        return false;
    }

    return intersect_node_p(node.left, ray, t_max) || intersect_node_p(node.right, ray, t_max);
}

}
