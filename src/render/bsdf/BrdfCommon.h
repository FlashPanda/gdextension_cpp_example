#ifndef GDEXTENSION_CPP_EXAMPLE_BRDF_COMMON_H
#define GDEXTENSION_CPP_EXAMPLE_BRDF_COMMON_H

#include <algorithm>
#include <cmath>

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace godot_rt {
namespace brdf {

    constexpr float PI = 3.14159265358979323846f;
    constexpr float INV_PI = 0.31830988618379067154f;
    constexpr float MIN_ROUGHNESS = 0.001f;

    // 方向变换
    struct ShadingFrame {
        godot::Vector3 tangent;
        godot::Vector3 bitangent;
        godot::Vector3 normal;

        godot::Vector3 to_world(const godot::Vector3& local) const {
            return tangent * local.x + bitangent * local.y + normal * local.z;
        }

        godot::Vector3 to_local(const godot::Vector3& world) const {
            return godot::Vector3(
                tangent.dot(world),
                bitangent.dot(world),
                normal.dot(world)
            );
        }
    };

    inline float saturate(float value) {
        return std::max(0.0f, std::min(value, 1.0f));
    }

    inline float square(float value) {
        return value * value;
    }

    inline float pow5(float value) {
        const float value2 = value * value;
        return value2 * value2 * value;
    }

    inline float luminance(const godot::Color& color) {
        return color.r * 0.2126f + color.g * 0.7152f + color.b * 0.0722f;
    }

    inline float max_rgb(const godot::Color& color) {
        return std::max(color.r, std::max(color.g, color.b));
    }

    inline bool is_black(const godot::Color& color) {
        return max_rgb(color) <= 0.0f;
    }

    inline godot::Color color(float r, float g, float b) {
        return godot::Color(r, g, b, 1.0f);
    }

    inline godot::Color color(float value) {
        return color(value, value, value);
    }

    inline godot::Color clamp_color01(const godot::Color& value) {
        return color(saturate(value.r), saturate(value.g), saturate(value.b));
    }

    inline godot::Color add(const godot::Color& a, const godot::Color& b) {
        return color(a.r + b.r, a.g + b.g, a.b + b.b);
    }

    inline godot::Color subtract(const godot::Color& a, const godot::Color& b) {
        return color(a.r - b.r, a.g - b.g, a.b - b.b);
    }

    inline godot::Color multiply(const godot::Color& a, const godot::Color& b) {
        return color(a.r * b.r, a.g * b.g, a.b * b.b);
    }

    inline godot::Color scale(const godot::Color& value, float factor) {
        return color(value.r * factor, value.g * factor, value.b * factor);
    }

    inline godot::Color mix(const godot::Color& a, const godot::Color& b, float t) {
        return add(scale(a, 1.0f - t), scale(b, t));
    }

    inline godot::Vector3 safe_normalized(godot::Vector3 value, const godot::Vector3& fallback) {
        if (value.length_squared() == 0.0f) {
            return fallback;
        }
        value.normalize();
        return value;
    }

    inline ShadingFrame make_shading_frame(godot::Vector3 normal) {
        normal = safe_normalized(normal, godot::Vector3(0.0f, 1.0f, 0.0f));

        godot::Vector3 tangent;
        if (std::abs(normal.x) > std::abs(normal.z)) {
            tangent = godot::Vector3(-normal.y, normal.x, 0.0f);
        } else {
            tangent = godot::Vector3(0.0f, -normal.z, normal.y);
        }
        tangent = safe_normalized(tangent, godot::Vector3(1.0f, 0.0f, 0.0f));

        godot::Vector3 bitangent = normal.cross(tangent);
        bitangent = safe_normalized(bitangent, godot::Vector3(0.0f, 1.0f, 0.0f));

        ShadingFrame frame;
        frame.tangent = tangent;
        frame.bitangent = bitangent;
        frame.normal = normal;
        return frame;
    }

    inline godot::Color fresnel_schlick(float cos_theta, const godot::Color& f0) {
        const float factor = pow5(1.0f - saturate(cos_theta));
        return add(f0, scale(subtract(color(1.0f), f0), factor));
    }

    inline float d_ggx(float no_h, float alpha) {
        const float a = std::max(alpha, MIN_ROUGHNESS);
        const float a2 = a * a;
        const float denom = no_h * no_h * (a2 - 1.0f) + 1.0f;
        return a2 / std::max(PI * denom * denom, 0.000001f);
    }

    // Matches Godot renderer_rd/shaders/scene_forward_lights_inc.glsl::V_GGX().
    // Earl Hammon, Jr. "PBR Diffuse Lighting for GGX+Smith Microsurfaces".
    inline float v_ggx_hammon(float no_l, float no_v, float alpha) {
        if (no_l <= 0.0f || no_v <= 0.0f) {
            return 0.0f;
        }

        const float denominator =
            (1.0f - alpha) * (2.0f * no_l * no_v) +
            alpha * (no_l + no_v);
        return 0.5f / std::max(denominator, 0.000001f);
    }

    inline godot::Color eval_burley_diffuse(const godot::Color& base_color,
                                            float roughness,
                                            float no_v,
                                            float no_l,
                                            float lo_h) {
        const float fd90 = 0.5f + 2.0f * roughness * lo_h * lo_h;
        const float light_scatter = 1.0f + (fd90 - 1.0f) * pow5(1.0f - saturate(no_l));
        const float view_scatter = 1.0f + (fd90 - 1.0f) * pow5(1.0f - saturate(no_v));
        return scale(base_color, INV_PI * light_scatter * view_scatter);
    }

    inline godot::Vector3 sample_cosine_hemisphere_local(const godot::Vector2& u) {
        const float r = std::sqrt(std::max(u.x, 0.0f));
        const float theta = 2.0f * PI * u.y;
        const float x = r * std::cos(theta);
        const float y = r * std::sin(theta);
        const float z = std::sqrt(std::max(0.0f, 1.0f - u.x));
        return godot::Vector3(x, y, z);
    }

    inline float pdf_cosine_hemisphere(float no_l) {
        return no_l > 0.0f ? no_l * INV_PI : 0.0f;
    }

    inline godot::Vector3 sample_ggx_half_vector_local(float alpha, const godot::Vector2& u) {
        const float a = std::max(alpha, MIN_ROUGHNESS);
        const float a2 = a * a;
        const float phi = 2.0f * PI * u.x;
        const float cos_theta = std::sqrt(
            std::max(0.0f, (1.0f - u.y) / std::max(1.0f + (a2 - 1.0f) * u.y, 0.000001f))
        );
        const float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
        return godot::Vector3(
            sin_theta * std::cos(phi),
            sin_theta * std::sin(phi),
            cos_theta
        );
    }

}
}

#endif // GDEXTENSION_CPP_EXAMPLE_BRDF_COMMON_H
