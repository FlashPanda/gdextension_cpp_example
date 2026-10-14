#include "bvh_accel.h"

namespace godot_rt {

BvhAccel::BvhAccel() : tree(BvhSplitMethod::Median) {}

void BvhAccel::build(const Scene& scene) {
    tree.build(scene);
}

bool BvhAccel::intersect(const Ray& ray, Hit* hit, real_t t_max) const {
    return tree.intersect(ray, hit, t_max);
}

bool BvhAccel::intersect_p(const Ray& ray, real_t t_max) const {
    return tree.intersect_p(ray, t_max);
}

}
