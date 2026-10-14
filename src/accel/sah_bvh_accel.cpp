#include "sah_bvh_accel.h"

namespace godot_rt {

SahBvhAccel::SahBvhAccel() : tree(BvhSplitMethod::Sah) {}

void SahBvhAccel::build(const Scene& scene) {
    tree.build(scene);
}

bool SahBvhAccel::intersect(const Ray& ray, Hit* hit, real_t t_max) const {
    return tree.intersect(ray, hit, t_max);
}

bool SahBvhAccel::intersect_p(const Ray& ray, real_t t_max) const {
    return tree.intersect_p(ray, t_max);
}

}
