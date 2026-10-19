#include "hlbvh_accel.h"

namespace godot_rt {

HlbvhAccel::HlbvhAccel() : tree(BvhSplitMethod::Hlbvh) {}

void HlbvhAccel::build(const Scene& scene) {
    tree.build(scene);
}

bool HlbvhAccel::intersect(const Ray& ray, Hit* hit, real_t t_max) const {
    return tree.intersect(ray, hit, t_max);
}

bool HlbvhAccel::intersect_p(const Ray& ray, real_t t_max) const {
    return tree.intersect_p(ray, t_max);
}

}
