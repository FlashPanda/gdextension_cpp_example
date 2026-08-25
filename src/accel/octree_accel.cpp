#include "octree_accel.h"

#include <algorithm>
#include <numeric>
#include <utility>

namespace godot_rt {
namespace {

    constexpr std::size_t LEAF_TRIANGLE_COUNT = 8;
    constexpr int MAX_DEPTH = 16;
    constexpr real_t MIN_NODE_EXTENT = 0.000001;

    AxisAlignedBounds make_root_cube(const std::vector<Triangle>& triangles) {
        AxisAlignedBounds scene_bounds;
        for (const Triangle& triangle : triangles) {
            scene_bounds = union_bounds(scene_bounds, triangle_bounds(triangle));
        }
        if (!scene_bounds.valid) {
            return scene_bounds;
        }

        const godot::Vector3 center = bounds_centroid(scene_bounds);
        const godot::Vector3 extent = bounds_extent(scene_bounds);
        const real_t half_size = std::max({extent.x, extent.y, extent.z}) * 0.5;
        const godot::Vector3 half_extent(half_size, half_size, half_size);
        scene_bounds.minimum = center - half_extent;
        scene_bounds.maximum = center + half_extent;
        return scene_bounds;
    }

    AxisAlignedBounds child_bounds(const AxisAlignedBounds& parent, int child_index) {
        const godot::Vector3 center = bounds_centroid(parent);
        AxisAlignedBounds child;
        child.minimum = godot::Vector3(
            (child_index & 1) != 0 ? center.x : parent.minimum.x,
            (child_index & 2) != 0 ? center.y : parent.minimum.y,
            (child_index & 4) != 0 ? center.z : parent.minimum.z
        );
        child.maximum = godot::Vector3(
            (child_index & 1) != 0 ? parent.maximum.x : center.x,
            (child_index & 2) != 0 ? parent.maximum.y : center.y,
            (child_index & 4) != 0 ? parent.maximum.z : center.z
        );
        child.valid = parent.valid;
        return child;
    }

    int containing_child(const AxisAlignedBounds& parent, const AxisAlignedBounds& primitive) {
        for (int child_index = 0; child_index < 8; ++child_index) {
            if (bounds_contains(child_bounds(parent, child_index), primitive)) {
                return child_index;
            }
        }
        return -1;
    }

}

void OctreeAccel::build(const Scene& scene) {
    triangles = scene.get_triangles();
    nodes.clear();
    if (triangles.empty()) {
        return;
    }

    std::vector<int> primitive_indices(triangles.size());
    std::iota(primitive_indices.begin(), primitive_indices.end(), 0);
    nodes.reserve(triangles.size());
    build_node(make_root_cube(triangles), std::move(primitive_indices), 0);
}

int OctreeAccel::build_node(const AxisAlignedBounds& bounds,
                            std::vector<int> primitive_indices,
                            int depth) {
    const int node_index = static_cast<int>(nodes.size());
    nodes.push_back(Node());
    nodes[node_index].bounds = bounds;

    const godot::Vector3 extent = bounds_extent(bounds);
    if (primitive_indices.size() <= LEAF_TRIANGLE_COUNT || depth >= MAX_DEPTH ||
        std::max({extent.x, extent.y, extent.z}) <= MIN_NODE_EXTENT) {
        nodes[node_index].primitive_indices = std::move(primitive_indices);
        return node_index;
    }

    std::array<std::vector<int>, 8> child_primitives;
    for (int primitive_index : primitive_indices) {
        const int child_index = containing_child(bounds, triangle_bounds(triangles[primitive_index]));
        if (child_index < 0) {
            nodes[node_index].primitive_indices.push_back(primitive_index);
        } else {
            child_primitives[child_index].push_back(primitive_index);
        }
    }

    // 跨越分割面的三角形保留在父节点，避免复制到多个子树导致内存和重复求交膨胀。
    // 只有完全落入单一八分区的三角形才递归下沉，build 完成后所有节点仅供并发只读。
    for (int child_index = 0; child_index < 8; ++child_index) {
        if (child_primitives[child_index].empty()) {
            continue;
        }
        const int child_node_index = build_node(
            child_bounds(bounds, child_index),
            std::move(child_primitives[child_index]),
            depth + 1
        );
        nodes[node_index].children[child_index] = child_node_index;
    }
    return node_index;
}

bool OctreeAccel::intersect(const Ray& ray, Hit* hit, real_t t_max) const {
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

void OctreeAccel::intersect_node(int node_index, const Ray& ray, real_t* closest_t,
                                 int* closest_index, TriangleIntersection* closest_intersection) const {
    const Node& node = nodes[node_index];
    if (!intersect_bounds(node.bounds, ray, *closest_t)) {
        return;
    }

    for (int primitive_index : node.primitive_indices) {
        TriangleIntersection intersection;
        if (intersect_triangle(triangles[primitive_index], ray, *closest_t, &intersection) &&
            is_better_triangle_hit(intersection.t, primitive_index, *closest_t, *closest_index)) {
            *closest_t = intersection.t;
            *closest_index = primitive_index;
            *closest_intersection = intersection;
        }
    }

    std::array<std::pair<real_t, int>, 8> ordered_children;
    int child_count = 0;
    for (int child_index : node.children) {
        real_t t_enter = 0.0;
        if (child_index >= 0 && intersect_bounds(nodes[child_index].bounds, ray, *closest_t, &t_enter)) {
            ordered_children[child_count++] = std::make_pair(t_enter, child_index);
        }
    }
    std::sort(ordered_children.begin(), ordered_children.begin() + child_count,
              [](const auto& left, const auto& right) { return left.first < right.first; });
    for (int index = 0; index < child_count; ++index) {
        intersect_node(ordered_children[index].second, ray, closest_t, closest_index, closest_intersection);
    }
}

bool OctreeAccel::intersect_p(const Ray& ray, real_t t_max) const {
    return !nodes.empty() && intersect_node_p(0, ray, t_max);
}

bool OctreeAccel::intersect_node_p(int node_index, const Ray& ray, real_t t_max) const {
    const Node& node = nodes[node_index];
    if (!intersect_bounds(node.bounds, ray, t_max)) {
        return false;
    }

    for (int primitive_index : node.primitive_indices) {
        if (intersect_triangle(triangles[primitive_index], ray, t_max, nullptr)) {
            return true;
        }
    }
    for (int child_index : node.children) {
        if (child_index >= 0 && intersect_node_p(child_index, ray, t_max)) {
            return true;
        }
    }
    return false;
}

}
