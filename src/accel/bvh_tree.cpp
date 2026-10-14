#include "bvh_tree.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace godot_rt {
namespace {

constexpr std::size_t MEDIAN_LEAF_TRIANGLE_COUNT = 4;
constexpr real_t CENTROID_EPSILON = 0.000000001;
constexpr int SAH_BUCKET_COUNT = 12;

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

real_t surface_area(const AxisAlignedBounds& bounds) {
    if (!bounds.valid) {
        return 0.0;
    }
    const godot::Vector3 extent = bounds_extent(bounds);
    return 2.0 * (extent.x * extent.y + extent.x * extent.z + extent.y * extent.z);
}

int sah_bucket(const godot::Vector3& centroid, int axis, real_t minimum, real_t extent) {
    const real_t offset = (component(centroid, axis) - minimum) / extent;
    return std::clamp(static_cast<int>(offset * SAH_BUCKET_COUNT), 0, SAH_BUCKET_COUNT - 1);
}

} // namespace

BvhTree::BvhTree(BvhSplitMethod method) : split_method(method) {}

void BvhTree::build(const Scene& scene) {
    // 生命周期：构建时复制 world-space 三角形；完成后两种 BVH 均只读，供 tile 线程共享。
    triangles = scene.get_triangles();
    primitive_indices.resize(triangles.size());
    std::iota(primitive_indices.begin(), primitive_indices.end(), 0);
    nodes.clear();
    nodes.reserve(triangles.empty() ? 0 : triangles.size() * 2 - 1);

    std::vector<PrimitiveInfo> primitive_info(triangles.size());
    for (std::size_t index = 0; index < triangles.size(); ++index) {
        primitive_info[index].bounds = triangle_bounds(triangles[index]);
        primitive_info[index].centroid = bounds_centroid(primitive_info[index].bounds);
    }
    if (!triangles.empty()) {
        build_node(0, triangles.size(), primitive_info);
    }
}

int BvhTree::build_node(std::size_t first, std::size_t count,
                        const std::vector<PrimitiveInfo>& primitive_info) {
    AxisAlignedBounds bounds;
    AxisAlignedBounds centroid_bounds;
    for (std::size_t offset = 0; offset < count; ++offset) {
        const PrimitiveInfo& info = primitive_info[primitive_indices[first + offset]];
        bounds = union_bounds(bounds, info.bounds);

        AxisAlignedBounds point_bounds;
        point_bounds.minimum = info.centroid;
        point_bounds.maximum = info.centroid;
        point_bounds.valid = true;
        centroid_bounds = union_bounds(centroid_bounds, point_bounds);
    }

    const int node_index = static_cast<int>(nodes.size());
    nodes.push_back(Node());
    nodes[node_index].bounds = bounds;
    nodes[node_index].first = first;
    nodes[node_index].count = count;

    const bool use_sah = split_method == BvhSplitMethod::Sah;
    if ((!use_sah && count <= MEDIAN_LEAF_TRIANGLE_COUNT) ||
        (use_sah && (count == 1 || !(surface_area(bounds) > 0.0)))) {
        return node_index;
    }

    const godot::Vector3 centroid_extent = bounds_extent(centroid_bounds);
    const int axis = largest_axis(centroid_extent);
    const real_t axis_extent = component(centroid_extent, axis);
    if (use_sah ? !(axis_extent > 0.0) : axis_extent <= CENTROID_EPSILON) {
        return node_index;
    }

    std::size_t middle = first;
    if (use_sah && count > 2) {
        middle = split_sah(first, count, axis, bounds, centroid_bounds, primitive_info);
    }
    if (middle == first || middle == first + count) {
        // 桶划分无法产生两个非空子树时，用确定性中位数回退以保证递归前进。
        middle = split_median(first, count, axis, primitive_info);
    }

    // 节点只保存索引，递归期间 nodes 扩容不会留下失效引用。
    const int left = build_node(first, middle - first, primitive_info);
    const int right = build_node(middle, first + count - middle, primitive_info);
    nodes[node_index].left = left;
    nodes[node_index].right = right;
    nodes[node_index].count = 0;
    return node_index;
}

std::size_t BvhTree::split_median(std::size_t first, std::size_t count, int axis,
                                  const std::vector<PrimitiveInfo>& primitive_info) {
    const std::size_t middle = first + count / 2;
    std::nth_element(
        primitive_indices.begin() + static_cast<std::ptrdiff_t>(first),
        primitive_indices.begin() + static_cast<std::ptrdiff_t>(middle),
        primitive_indices.begin() + static_cast<std::ptrdiff_t>(first + count),
        [&primitive_info, axis](int left, int right) {
            const real_t left_centroid = component(primitive_info[left].centroid, axis);
            const real_t right_centroid = component(primitive_info[right].centroid, axis);
            return left_centroid == right_centroid ? left < right : left_centroid < right_centroid;
        }
    );
    return middle;
}

std::size_t BvhTree::split_sah(std::size_t first, std::size_t count, int axis,
                               const AxisAlignedBounds& bounds,
                               const AxisAlignedBounds& centroid_bounds,
                               const std::vector<PrimitiveInfo>& primitive_info) {
    struct Bucket {
        std::size_t count = 0;
        AxisAlignedBounds bounds;
    };
    std::array<Bucket, SAH_BUCKET_COUNT> buckets;
    const real_t minimum = component(centroid_bounds.minimum, axis);
    const real_t extent = component(bounds_extent(centroid_bounds), axis);
    if (!std::isfinite(extent) || !std::isfinite(minimum)) {
        return first;
    }

    for (std::size_t offset = 0; offset < count; ++offset) {
        const PrimitiveInfo& info = primitive_info[primitive_indices[first + offset]];
        if (!std::isfinite(component(info.centroid, axis))) {
            return first;
        }
        Bucket& bucket = buckets[sah_bucket(info.centroid, axis, minimum, extent)];
        ++bucket.count;
        bucket.bounds = union_bounds(bucket.bounds, info.bounds);
    }

    // pbrt 的 12 桶近似 SAH：前后缀面积之和除以父面积，再加遍历代价 0.5。
    std::array<real_t, SAH_BUCKET_COUNT - 1> left_area_cost{};
    std::array<std::size_t, SAH_BUCKET_COUNT - 1> left_counts{};
    AxisAlignedBounds left_bounds;
    std::size_t left_count = 0;
    for (int index = 0; index < SAH_BUCKET_COUNT - 1; ++index) {
        left_bounds = union_bounds(left_bounds, buckets[index].bounds);
        left_count += buckets[index].count;
        left_counts[index] = left_count;
        left_area_cost[index] = static_cast<real_t>(left_count) * surface_area(left_bounds);
    }

    const real_t parent_area = surface_area(bounds);
    real_t best_cost = std::numeric_limits<real_t>::infinity();
    int best_bucket = -1;
    AxisAlignedBounds right_bounds;
    std::size_t right_count = 0;
    for (int index = SAH_BUCKET_COUNT - 1; index >= 1; --index) {
        right_bounds = union_bounds(right_bounds, buckets[index].bounds);
        right_count += buckets[index].count;
        if (left_counts[index - 1] == 0 || right_count == 0) {
            continue;
        }
        const real_t cost = 0.5 +
            (left_area_cost[index - 1] + static_cast<real_t>(right_count) * surface_area(right_bounds)) /
                parent_area;
        if (cost < best_cost || (cost == best_cost && index - 1 < best_bucket)) {
            best_cost = cost;
            best_bucket = index - 1;
        }
    }
    if (best_bucket < 0) {
        return first;
    }

    // maxPrimsInNode=1 时非退化多三角形节点必须分裂；代价仅决定分裂位置。
    const auto middle = std::partition(
        primitive_indices.begin() + static_cast<std::ptrdiff_t>(first),
        primitive_indices.begin() + static_cast<std::ptrdiff_t>(first + count),
        [&primitive_info, axis, minimum, extent, best_bucket](int primitive_index) {
            return sah_bucket(primitive_info[primitive_index].centroid, axis, minimum, extent) <= best_bucket;
        }
    );
    return static_cast<std::size_t>(middle - primitive_indices.begin());
}

std::size_t BvhTree::node_count() const {
    return nodes.size();
}

std::size_t BvhTree::root_left_primitive_count() const {
    if (nodes.empty() || nodes[0].is_leaf()) {
        return 0;
    }
    return nodes[nodes[0].right].first - nodes[0].first;
}

bool BvhTree::intersect(const Ray& ray, Hit* hit, real_t t_max) const {
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

void BvhTree::intersect_node(int node_index, const Ray& ray, real_t* closest_t,
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

bool BvhTree::intersect_p(const Ray& ray, real_t t_max) const {
    return !nodes.empty() && intersect_node_p(0, ray, t_max);
}

bool BvhTree::intersect_node_p(int node_index, const Ray& ray, real_t t_max) const {
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
