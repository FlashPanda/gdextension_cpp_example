#include "rt_light.h"

#include <algorithm>
#include <cmath>

namespace godot_rt {
namespace {

    constexpr real_t LIGHT_EPSILON = 0.0001;

    godot::Color scaled_color(const godot::Color& color, real_t scale) {
        return godot::Color(
            color.r * scale,
            color.g * scale,
            color.b * scale,
            1.0f
        );
    }

    real_t distance_attenuation(real_t distance, real_t range, real_t attenuation) {
        if (range <= 0.0) {
            return 1.0;
        }
        if (distance >= range) {
            return 0.0;
        }

        const real_t normalized_distance = std::max(distance / range, static_cast<real_t>(0.0));
        const real_t falloff = std::max(static_cast<real_t>(1.0) - normalized_distance, static_cast<real_t>(0.0));
        return std::pow(falloff, std::max(attenuation, static_cast<real_t>(0.0)));
    }

    real_t inverse_square(real_t distance) {
        return static_cast<real_t>(1.0) / std::max(distance * distance, static_cast<real_t>(1.0));
    }

    godot::Vector3 safe_normalized(const godot::Vector3& value, const godot::Vector3& fallback) {
        if (value.length_squared() == 0.0) {
            return fallback;
        }
        return value.normalized();
    }

    const char* light_type_name(LightType type) {
        switch (type) {
            case LightType::Directional:
                return "DirectionalLight";
            case LightType::Omni:
                return "OmniLight";
            case LightType::Spot:
                return "SpotLight";
        }

        return "Light";
    }

}

LightSample Light::sample_li(const godot::Vector3& point) const {
    LightSample sample;
    if (energy <= 0.0) {
        return sample;
    }

    if (type == LightType::Directional) {
        const godot::Vector3 light_direction = safe_normalized(
            transform.basis.xform(godot::Vector3(0.0f, 0.0f, -1.0f)),
            godot::Vector3(0.0f, -1.0f, 0.0f)
        );

        sample.valid = true;
        sample.wi = -light_direction;
        sample.distance = Math_INF;
        sample.radiance = scaled_color(color, energy);
        return sample;
    }

    const godot::Vector3 to_light = transform.origin - point;
    const real_t distance = to_light.length();
    if (distance <= LIGHT_EPSILON) {
        return sample;
    }

    const real_t range_factor = distance_attenuation(distance, range, attenuation);
    if (range_factor <= 0.0) {
        return sample;
    }

    real_t spot_factor = 1.0;
    if (type == LightType::Spot) {
        const godot::Vector3 light_forward = safe_normalized(
            transform.basis.xform(godot::Vector3(0.0f, 0.0f, -1.0f)),
            godot::Vector3(0.0f, 0.0f, -1.0f)
        );
        const godot::Vector3 light_to_point = (point - transform.origin) / distance;
        const real_t cos_angle = light_forward.dot(light_to_point);
        const real_t cos_outer = std::cos(std::max(spot_angle_radians, static_cast<real_t>(0.0)));

        if (cos_angle <= cos_outer) {
            return sample;
        }

        const real_t cone_width = std::max(static_cast<real_t>(1.0) - cos_outer, static_cast<real_t>(0.0001));
        const real_t normalized_cone = std::max((cos_angle - cos_outer) / cone_width, static_cast<real_t>(0.0));
        spot_factor = std::pow(normalized_cone, std::max(spot_attenuation, static_cast<real_t>(0.0)));
    }

    const real_t scale = energy * range_factor * spot_factor * inverse_square(distance);
    if (scale <= 0.0) {
        return sample;
    }

    sample.valid = true;
    sample.wi = to_light / distance;
    sample.distance = distance;
    sample.radiance = scaled_color(color, scale);
    return sample;
}

std::string Light::to_string() const {
    return light_type_name(type);
}

}
