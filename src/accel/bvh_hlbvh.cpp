#include "bvh_tree.h"
#include "hlbvh_morton.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace godot_rt {
namespace {

constexpr int MORTON_BITS = 30;
constexpr int RADIX_BITS = 6;
constexpr int TREELET_PREFIX_BITS = 12;
constexpr std::size_t MORTON_BATCH_SIZE = 1024;
constexpr std::size_t LEAF_THRESHOLD = 4;
constexpr int SAH_BUCKETS = 12;

double component(const godot::Vector3& v, int axis) {
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

uint32_t quantize(double value, double low, double high) {
    // 使用 double 计算跨度，避免有限 float 端点相减溢出后转换为无效整数。
    if (!(high > low) || !std::isfinite(value) || !std::isfinite(low) || !std::isfinite(high)) return 0;
    return static_cast<uint32_t>(std::clamp((value - low) / (high - low) * 1024.0, 0.0, 1023.0));
}

uint32_t interleave(uint32_t value) {
    value = (value | (value << 16)) & 0x030000ffu;
    value = (value | (value << 8)) & 0x0300f00fu;
    value = (value | (value << 4)) & 0x030c30c3u;
    return (value | (value << 2)) & 0x09249249u;
}

double area(const AxisAlignedBounds& bounds) {
    if (!bounds.valid) return 0;
    const double x = component(bounds.maximum, 0) - component(bounds.minimum, 0);
    const double y = component(bounds.maximum, 1) - component(bounds.minimum, 1);
    const double z = component(bounds.maximum, 2) - component(bounds.minimum, 2);
    return 2 * (x * y + x * z + y * z);
}

double center(const AxisAlignedBounds& bounds, int axis) {
    return (component(bounds.minimum, axis) + component(bounds.maximum, axis)) * 0.5;
}

} // 匿名命名空间

uint32_t detail::encode_morton_3d(const godot::Vector3& centroid, const AxisAlignedBounds& bounds) {
    uint32_t code = 0;
    for (int axis = 0; axis < 3; ++axis) {
        code |= interleave(quantize(component(centroid, axis), component(bounds.minimum, axis),
                                   component(bounds.maximum, axis))) << axis;
    }
    return code;
}

void detail::radix_sort_morton(std::vector<MortonPrimitive>& primitives) {
    std::vector<MortonPrimitive> scratch(primitives.size());
    for (int bit = 0; bit < MORTON_BITS; bit += RADIX_BITS) {
        std::array<std::size_t, 1 << RADIX_BITS> offsets{};
        for (const auto& primitive : primitives) ++offsets[(primitive.code >> bit) & 63u];
        std::size_t next = 0;
        for (auto& count : offsets) {
            const std::size_t size = count;
            count = next;
            next += size;
        }
        for (const auto& primitive : primitives) scratch[offsets[(primitive.code >> bit) & 63u]++] = primitive;
        primitives.swap(scratch);
    }
}

int BvhTree::build_hlbvh(const std::vector<PrimitiveInfo>& info, const BvhBuildOptions& options) {
    AxisAlignedBounds centroid_bounds;
    for (const auto& primitive : info) {
        AxisAlignedBounds point;
        point.minimum = point.maximum = primitive.centroid;
        point.valid = true;
        centroid_bounds = union_bounds(centroid_bounds, point);
    }

    const unsigned int threads = options.thread_count ? options.thread_count : recommended_render_thread_count();
    std::vector<detail::MortonPrimitive> morton(info.size());
    // 线程边界：预分配后每个任务独占最多 1024 个槽位，只读取纯值场景快照；返回时已 join。
    run_parallel_tasks((info.size() + MORTON_BATCH_SIZE - 1) / MORTON_BATCH_SIZE, threads,
        [&](std::size_t batch) {
            const std::size_t end = std::min(info.size(), (batch + 1) * MORTON_BATCH_SIZE);
            for (std::size_t i = batch * MORTON_BATCH_SIZE; i < end; ++i) {
                morton[i] = {static_cast<int>(i), detail::encode_morton_3d(info[i].centroid, centroid_bounds)};
            }
        }, {}, options.thread_launcher);
    detail::radix_sort_morton(morton);
    for (std::size_t i = 0; i < morton.size(); ++i) primitive_indices[i] = morton[i].primitive_index;

    struct Treelet {
        std::size_t first;
        std::size_t count;
        std::vector<Node> nodes;
    };
    std::vector<Treelet> treelets;
    for (std::size_t first = 0; first < morton.size();) {
        std::size_t end = first + 1;
        while (end < morton.size() && (morton[first].code >> (MORTON_BITS - TREELET_PREFIX_BITS)) ==
                                     (morton[end].code >> (MORTON_BITS - TREELET_PREFIX_BITS))) ++end;
        treelets.push_back({first, end - first, {}});
        first = end;
    }

    // 生命周期：treelets 容器在调度前固定大小，每个任务独占一个局部 nodes；没有共享 push_back。
    // 调度器在部分/全部启动失败时仍由剩余线程完成所有任务；这里不传取消回调，避免发布半成品树。
    run_parallel_tasks(treelets.size(), threads, [&](std::size_t index) {
        Treelet& treelet = treelets[index];
        auto& local = treelet.nodes;
        local.reserve(treelet.count * 2 - 1);
        const auto emit = [&](const auto& self, std::size_t first, std::size_t count, int bit) -> int {
            // 高位相同就跳过该位；位耗尽时碰撞图元必须共用一个叶，不能无限递归。
            while (bit >= 0 && ((morton[first].code ^ morton[first + count - 1].code) & (1u << bit)) == 0) --bit;
            const int node_index = static_cast<int>(local.size());
            local.push_back(Node());
            if (bit < 0 || count < LEAF_THRESHOLD) {
                local[node_index].first = first;
                local[node_index].count = count;
                for (std::size_t i = first; i < first + count; ++i)
                    local[node_index].bounds = union_bounds(local[node_index].bounds, info[morton[i].primitive_index].bounds);
                return node_index;
            }
            const uint32_t mask = 1u << bit;
            const auto begin = morton.begin() + static_cast<std::ptrdiff_t>(first);
            const auto end = begin + static_cast<std::ptrdiff_t>(count);
            const auto middle = std::partition_point(begin, end, [mask](const auto& primitive) { return !(primitive.code & mask); });
            const std::size_t split = static_cast<std::size_t>(middle - morton.begin());
            const int left = self(self, first, split - first, bit - 1);
            const int right = self(self, split, first + count - split, bit - 1);
            local[node_index].left = left;
            local[node_index].right = right;
            local[node_index].bounds = union_bounds(local[left].bounds, local[right].bounds);
            return node_index;
        };
        emit(emit, treelet.first, treelet.count, MORTON_BITS - TREELET_PREFIX_BITS - 1);
    }, {}, options.thread_launcher);

    std::vector<int> roots;
    roots.reserve(treelets.size());
    // 所有 helper 已退出，按 Morton 前缀顺序合并；保留原始 triangleIndex，仅重映射局部节点索引。
    for (auto& treelet : treelets) {
        const int base = static_cast<int>(nodes.size());
        roots.push_back(base);
        for (Node node : treelet.nodes) {
            if (!node.is_leaf()) {
                node.left += base;
                node.right += base;
            }
            nodes.push_back(node);
        }
        std::vector<Node>().swap(treelet.nodes);
    }
    return build_upper_sah(roots, 0, roots.size());
}

int BvhTree::build_upper_sah(std::vector<int>& roots, std::size_t first, std::size_t count) {
    if (count == 1) return roots[first];
    AxisAlignedBounds bounds;
    std::array<double, 3> minimum{Math_INF, Math_INF, Math_INF};
    std::array<double, 3> maximum{-Math_INF, -Math_INF, -Math_INF};
    for (std::size_t i = first; i < first + count; ++i) {
        const auto& b = nodes[roots[i]].bounds;
        bounds = union_bounds(bounds, b);
        for (int axis = 0; axis < 3; ++axis) {
            minimum[axis] = std::min(minimum[axis], center(b, axis));
            maximum[axis] = std::max(maximum[axis], center(b, axis));
        }
    }
    int axis = 0;
    for (int i = 1; i < 3; ++i) if (maximum[i] - minimum[i] > maximum[axis] - minimum[axis]) axis = i;
    const double extent = maximum[axis] - minimum[axis];
    const double parent_area = area(bounds);
    const auto bucket_index = [&](int root) {
        return std::clamp(static_cast<int>((center(nodes[root].bounds, axis) - minimum[axis]) / extent * SAH_BUCKETS), 0, SAH_BUCKETS - 1);
    };
    struct Bucket { std::size_t count = 0; AxisAlignedBounds bounds; };
    std::array<Bucket, SAH_BUCKETS> buckets;
    bool usable = extent > 0 && std::isfinite(extent) && parent_area > 0 && std::isfinite(parent_area);
    for (std::size_t i = first; usable && i < first + count; ++i) {
        if (!std::isfinite(center(nodes[roots[i]].bounds, axis))) { usable = false; break; }
        auto& bucket = buckets[bucket_index(roots[i])];
        ++bucket.count;
        bucket.bounds = union_bounds(bucket.bounds, nodes[roots[i]].bounds);
    }
    int best_bucket = -1;
    double best_cost = std::numeric_limits<double>::infinity();
    if (usable) {
        for (int split = 0; split < SAH_BUCKETS - 1; ++split) {
            AxisAlignedBounds left, right;
            std::size_t left_count = 0, right_count = 0;
            for (int b = 0; b < SAH_BUCKETS; ++b) {
                if (b <= split) { left = union_bounds(left, buckets[b].bounds); left_count += buckets[b].count; }
                else { right = union_bounds(right, buckets[b].bounds); right_count += buckets[b].count; }
            }
            if (!left_count || !right_count) continue;
            // 上层每个 treelet 作为一个整体计权，与已有逐三角形 SAH 的权重及遍历代价不同。
            const double cost = 0.125 + (left_count * area(left) + right_count * area(right)) / parent_area;
            if (cost < best_cost) { best_cost = cost; best_bucket = split; }
        }
    }
    const auto begin = roots.begin() + static_cast<std::ptrdiff_t>(first);
    const auto end = begin + static_cast<std::ptrdiff_t>(count);
    auto middle = begin;
    if (best_bucket >= 0) middle = std::partition(begin, end, [&](int root) { return bucket_index(root) <= best_bucket; });
    if (middle == begin || middle == end) {
        // 重合质心、零面积或无效代价时按数量拆分；固定节点编号打破平局，确保递归推进与可复现。
        middle = begin + static_cast<std::ptrdiff_t>(count / 2);
        std::nth_element(begin, middle, end, [&](int a, int b) {
            const double ca = center(nodes[a].bounds, axis), cb = center(nodes[b].bounds, axis);
            const double left = std::isfinite(ca) ? ca : 0, right = std::isfinite(cb) ? cb : 0;
            return left == right ? a < b : left < right;
        });
    }
    const std::size_t split = static_cast<std::size_t>(middle - roots.begin());
    const int left = build_upper_sah(roots, first, split - first);
    const int right = build_upper_sah(roots, split, first + count - split);
    Node node;
    node.bounds = bounds;
    node.left = left;
    node.right = right;
    const int index = static_cast<int>(nodes.size());
    nodes.push_back(node);
    return index;
}

}
