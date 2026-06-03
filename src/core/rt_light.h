#ifndef GDEXTENSION_CPP_EXAMPLE_INDEX_RT_LIGHT_H
#define GDEXTENSION_CPP_EXAMPLE_INDEX_RT_LIGHT_H

#include <cstdint>
#include <string>

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "rt_math.h"

namespace godot_rt {

    enum class LightType : std::uint8_t {
        Directional,
        Omni,
        Spot
    };

    struct LightSample {
        bool valid = false;
        godot::Vector3 wi;
        real_t distance = Math_INF;
        godot::Color radiance = godot::Color(0.0f, 0.0f, 0.0f, 1.0f);
    };

    struct Light {
        LightSample sample_li(const godot::Vector3& point) const;
        std::string to_string() const;

        LightType type = LightType::Omni;
        godot::Transform3D transform;
        godot::Color color = godot::Color(1.0f, 1.0f, 1.0f, 1.0f);
        real_t energy = 1.0;
        real_t range = 0.0;
        real_t attenuation = 1.0;
        real_t spot_angle_radians = 0.0;
        real_t spot_attenuation = 1.0;
        bool casts_shadow = false;
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_INDEX_RT_LIGHT_H
