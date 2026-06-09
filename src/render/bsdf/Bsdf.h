#ifndef GDEXTENSION_CPP_EXAMPLE_BSDF_H
#define GDEXTENSION_CPP_EXAMPLE_BSDF_H

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "../../core/rt_random.h"

namespace godot_rt {

    struct BsdfSample {
        godot::Vector3 wi;
        godot::Color f = godot::Color(0.0f, 0.0f, 0.0f, 1.0f);
        float pdf = 0.0f;
    };

    class Bsdf {
    public:
        virtual ~Bsdf() = default;

        virtual godot::Color eval(const godot::Vector3& wo,
                                  const godot::Vector3& wi) const = 0;

        virtual float pdf(const godot::Vector3& wo,
                          const godot::Vector3& wi) const = 0;

        virtual BsdfSample sample(const godot::Vector3& wo,
                                  Rng& rng) const = 0;
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_BSDF_H
