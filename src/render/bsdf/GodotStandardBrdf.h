#ifndef GDEXTENSION_CPP_EXAMPLE_GODOT_STANDARD_BRDF_H
#define GDEXTENSION_CPP_EXAMPLE_GODOT_STANDARD_BRDF_H

#include <algorithm>
#include <cstddef>
#include <cmath>

#include "BrdfCommon.h"
#include "Bsdf.h"
#include "../../core/rt_types.h"

namespace godot_rt {

    struct GodotStandardParams {
        godot::Color base_color = godot::Color(1.0f, 1.0f, 1.0f, 1.0f);
        godot::Color emission = godot::Color(0.0f, 0.0f, 0.0f, 1.0f);
        godot::Vector3 normal = godot::Vector3(0.0f, 1.0f, 0.0f);
        float metallic = 0.0f;
        float roughness = 0.5f;
        float specular = 0.5f;
    };

namespace godot_standard_detail {

    inline int wrap_index(int value, int size) {
        if (size <= 0) {
            return 0;
        }

        int wrapped = value % size;
        if (wrapped < 0) {
            wrapped += size;
        }
        return wrapped;
    }

    inline float wrap_repeat(float value) {
        return value - std::floor(value);
    }

    inline godot::Color texel(const MaterialTexture& texture, int x, int y) {
        if (!texture.is_valid()) {
            return brdf::color(1.0f);
        }

        const int wrapped_x = wrap_index(x, texture.width);
        const int wrapped_y = wrap_index(y, texture.height);
        return (*texture.pixels)[static_cast<std::size_t>(wrapped_y * texture.width + wrapped_x)];
    }

    inline godot::Color mix_rgba(const godot::Color& a, const godot::Color& b, float t) {
        return godot::Color(
            a.r + (b.r - a.r) * t,
            a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t
        );
    }

    inline godot::Color sample_texture_repeat_bilinear(const MaterialTexture& texture, const godot::Vector2& uv) {
        if (!texture.is_valid()) {
            return brdf::color(1.0f);
        }

        const float x = wrap_repeat(uv.x) * static_cast<float>(texture.width) - 0.5f;
        const float y = wrap_repeat(uv.y) * static_cast<float>(texture.height) - 0.5f;

        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const int x1 = x0 + 1;
        const int y1 = y0 + 1;

        const float tx = x - std::floor(x);
        const float ty = y - std::floor(y);

        const godot::Color c00 = texel(texture, x0, y0);
        const godot::Color c10 = texel(texture, x1, y0);
        const godot::Color c01 = texel(texture, x0, y1);
        const godot::Color c11 = texel(texture, x1, y1);

        const godot::Color cx0 = mix_rgba(c00, c10, tx);
        const godot::Color cx1 = mix_rgba(c01, c11, tx);
        return mix_rgba(cx0, cx1, ty);
    }

    inline float channel_value(const godot::Color& color, MaterialTextureChannel channel) {
        switch (channel) {
            case MaterialTextureChannel::Red:
                return color.r;
            case MaterialTextureChannel::Green:
                return color.g;
            case MaterialTextureChannel::Blue:
                return color.b;
            case MaterialTextureChannel::Alpha:
                return color.a;
            case MaterialTextureChannel::Grayscale:
                return brdf::luminance(color);
        }

        return color.r;
    }

    inline godot::Color material_emission_at_uv(const Material& material, const godot::Vector2& uv) {
        godot::Color emission = material.emission;
        if (material.emission_texture.is_valid()) {
            emission = brdf::multiply(emission, sample_texture_repeat_bilinear(material.emission_texture, uv));
        }
        emission.a = 1.0f;
        return emission;
    }

    inline godot::Color godot_f0(const GodotStandardParams& params) {
        const float dielectric_f0 = 0.16f * params.specular * params.specular;
        return brdf::mix(brdf::color(dielectric_f0), params.base_color, params.metallic);
    }

}

    inline GodotStandardParams make_godot_standard_params(const Material& material,
                                                          const godot::Vector2& uv,
                                                          const godot::Vector3& normal) {
        GodotStandardParams params;
        params.normal = normal;
        params.base_color = brdf::clamp_color01(material.albedo);
        params.emission = godot_standard_detail::material_emission_at_uv(material, uv);
        params.metallic = brdf::saturate(material.metallic);
        params.roughness = brdf::saturate(material.roughness);
        params.specular = brdf::saturate(material.specular);

        if (material.albedo_texture.is_valid()) {
            params.base_color = brdf::multiply(
                params.base_color,
                brdf::clamp_color01(godot_standard_detail::sample_texture_repeat_bilinear(material.albedo_texture, uv))
            );
        }

        if (material.orm_texture.is_valid()) {
            const godot::Color orm = godot_standard_detail::sample_texture_repeat_bilinear(material.orm_texture, uv);
            params.roughness = brdf::saturate(orm.g);
            params.metallic = brdf::saturate(orm.b);
        } else {
            if (material.roughness_texture.is_valid()) {
                const godot::Color roughness_sample =
                    godot_standard_detail::sample_texture_repeat_bilinear(material.roughness_texture, uv);
                params.roughness = brdf::saturate(
                    params.roughness *
                    godot_standard_detail::channel_value(roughness_sample, material.roughness_texture_channel)
                );
            }

            if (material.metallic_texture.is_valid()) {
                const godot::Color metallic_sample =
                    godot_standard_detail::sample_texture_repeat_bilinear(material.metallic_texture, uv);
                params.metallic = brdf::saturate(
                    params.metallic *
                    godot_standard_detail::channel_value(metallic_sample, material.metallic_texture_channel)
                );
            }
        }

        params.roughness = std::max(params.roughness, brdf::MIN_ROUGHNESS);
        return params;
    }

    inline godot::Color sample_material_emission(const Material& material, const godot::Vector2& uv) {
        return godot_standard_detail::material_emission_at_uv(material, uv);
    }

    class GodotStandardBRDF final : public Bsdf {
    public:
        explicit GodotStandardBRDF(const GodotStandardParams& new_params)
            : params(new_params),
              frame(brdf::make_shading_frame(new_params.normal)) {}

        godot::Color eval(const godot::Vector3& wo, const godot::Vector3& wi) const final {
            const godot::Vector3 local_wo = frame.to_local(brdf::safe_normalized(wo, frame.normal));
            const godot::Vector3 local_wi = frame.to_local(brdf::safe_normalized(wi, frame.normal));

            const float no_v = local_wo.z;
            const float no_l = local_wi.z;
            if (no_v <= 0.0f || no_l <= 0.0f) {
                return brdf::color(0.0f);
            }

            godot::Vector3 local_h = local_wo + local_wi;
            if (local_h.length_squared() == 0.0f) {
                return brdf::color(0.0f);
            }
            local_h.normalize();

            const float no_h = brdf::saturate(local_h.z);
            const float lo_h = brdf::saturate(local_wi.dot(local_h));
            const float vo_h = brdf::saturate(local_wo.dot(local_h));
            if (no_h <= 0.0f || vo_h <= 0.0f) {
                return brdf::color(0.0f);
            }

            const float roughness = brdf::saturate(params.roughness);
            const float alpha = std::max(roughness * roughness, brdf::MIN_ROUGHNESS);
            const float metallic = brdf::saturate(params.metallic);

            const godot::Color diffuse = brdf::scale(
                brdf::eval_burley_diffuse(params.base_color, roughness, no_v, no_l, lo_h),
                1.0f - metallic
            );

            const godot::Color fresnel = brdf::fresnel_schlick(vo_h, godot_standard_detail::godot_f0(params));
            const float specular_scale = brdf::d_ggx(no_h, alpha) * brdf::v_smith_ggx(no_l, no_v, alpha);
            const godot::Color specular = brdf::scale(fresnel, specular_scale);

            return brdf::add(diffuse, specular);
        }

        float pdf(const godot::Vector3& wo, const godot::Vector3& wi) const final {
            const godot::Vector3 local_wo = frame.to_local(brdf::safe_normalized(wo, frame.normal));
            const godot::Vector3 local_wi = frame.to_local(brdf::safe_normalized(wi, frame.normal));
            if (local_wo.z <= 0.0f || local_wi.z <= 0.0f) {
                return 0.0f;
            }

            float diffuse_probability = 0.0f;
            float specular_probability = 0.0f;
            lobe_probabilities(diffuse_probability, specular_probability);
            if (diffuse_probability <= 0.0f && specular_probability <= 0.0f) {
                return 0.0f;
            }

            const float diffuse_pdf = brdf::pdf_cosine_hemisphere(local_wi.z);
            float specular_pdf = 0.0f;

            godot::Vector3 local_h = local_wo + local_wi;
            if (local_h.length_squared() != 0.0f) {
                local_h.normalize();
                const float no_h = brdf::saturate(local_h.z);
                const float vo_h = brdf::saturate(local_wo.dot(local_h));
                if (no_h > 0.0f && vo_h > 0.0f) {
                    const float roughness = brdf::saturate(params.roughness);
                    const float alpha = std::max(roughness * roughness, brdf::MIN_ROUGHNESS);
                    specular_pdf = brdf::d_ggx(no_h, alpha) * no_h / std::max(4.0f * vo_h, 0.000001f);
                }
            }

            return diffuse_probability * diffuse_pdf + specular_probability * specular_pdf;
        }

        BsdfSample sample(const godot::Vector3& wo, Rng& rng) const final {
            BsdfSample result;
            const godot::Vector3 local_wo = frame.to_local(brdf::safe_normalized(wo, frame.normal));
            if (local_wo.z <= 0.0f) {
                return result;
            }

            float diffuse_probability = 0.0f;
            float specular_probability = 0.0f;
            lobe_probabilities(diffuse_probability, specular_probability);
            if (diffuse_probability <= 0.0f && specular_probability <= 0.0f) {
                return result;
            }

            godot::Vector3 local_wi;
            if (rng.next_float() < diffuse_probability) {
                local_wi = brdf::sample_cosine_hemisphere_local(rng.next_2d());
            } else {
                const float roughness = brdf::saturate(params.roughness);
                const float alpha = std::max(roughness * roughness, brdf::MIN_ROUGHNESS);
                godot::Vector3 local_h = brdf::sample_ggx_half_vector_local(alpha, rng.next_2d());
                if (local_wo.dot(local_h) < 0.0f) {
                    local_h = -local_h;
                }
                local_wi = -local_wo + local_h * (2.0f * local_wo.dot(local_h));
                if (local_wi.length_squared() == 0.0f) {
                    return result;
                }
                local_wi.normalize();
            }

            if (local_wi.z <= 0.0f) {
                return result;
            }

            result.wi = frame.to_world(local_wi);
            if (result.wi.length_squared() == 0.0f) {
                return BsdfSample();
            }
            result.wi.normalize();
            result.f = eval(wo, result.wi);
            result.pdf = pdf(wo, result.wi);
            return result;
        }

    private:
        void lobe_probabilities(float& diffuse_probability, float& specular_probability) const {
            const float diffuse_weight =
                (1.0f - brdf::saturate(params.metallic)) * brdf::luminance(params.base_color);
            const float specular_weight = brdf::luminance(godot_standard_detail::godot_f0(params));
            const float sum = diffuse_weight + specular_weight;
            if (sum <= 0.0f) {
                diffuse_probability = 0.0f;
                specular_probability = 0.0f;
                return;
            }

            diffuse_probability = diffuse_weight / sum;
            specular_probability = specular_weight / sum;
        }

        GodotStandardParams params;
        brdf::ShadingFrame frame;
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_GODOT_STANDARD_BRDF_H
