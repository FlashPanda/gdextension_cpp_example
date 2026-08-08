#include "color_postprocess.h"

#include <algorithm>
#include <cmath>

namespace godot_rt {

    float apply_exposure(float linear_radiance, float exposure) {
        return linear_radiance * exposure;
    }

    float tonemap_reinhard(float color, float white) {
        // Match Godot tonemap.glsl:210-216 (Reinhard extended with white point).
        const float white_squared = white * white;
        const float white_squared_color = white_squared * color;
        return (white_squared_color + color * color) /
               (white_squared_color + white_squared);
    }

    float linear_to_srgb(float color) {
        // Match Godot tonemap.glsl:340-345. Clamp only at the LDR encoding boundary.
        const float clamped_color = std::clamp(color, 0.0f, 1.0f);
        constexpr float threshold = 0.0031308f;
        constexpr float a = 0.055f;
        if (clamped_color < threshold) {
            return 12.92f * clamped_color;
        }

        return (1.0f + a) * std::pow(clamped_color, 1.0f / 2.4f) - a;
    }

    namespace {

        float postprocess_channel(float linear_radiance,
                                  const OutputPostprocessSettings& settings) {
            const float nonnegative_radiance = std::max(linear_radiance, 0.0f);
            const float exposed_radiance = apply_exposure(
                nonnegative_radiance,
                settings.exposure
            );
            // 当前阶段固定使用 Reinhard；ToneMapperMode 为后续 Godot 模式扩展保留。
            return linear_to_srgb(tonemap_reinhard(exposed_radiance, settings.white));
        }

    } // namespace

    RgbColor postprocess_linear_radiance(const RgbColor& radiance,
                                         const OutputPostprocessSettings& settings) {
        return RgbColor{
            postprocess_channel(radiance.r, settings),
            postprocess_channel(radiance.g, settings),
            postprocess_channel(radiance.b, settings),
        };
    }

    OutputPostprocessSettings resolve_output_postprocess_settings(
        const EnvironmentTonemapValues* camera_environment,
        const EnvironmentTonemapValues* world_environment
    ) {
        // Match Godot's renderer precedence: camera override, then World3D, then defaults.
        // See renderer_scene_cull.cpp::RendererSceneCull::_render_get_environment().
        const EnvironmentTonemapValues* values = camera_environment != nullptr
                                                     ? camera_environment
                                                     : world_environment;
        if (values == nullptr) {
            return OutputPostprocessSettings{};
        }

        OutputPostprocessSettings settings;
        settings.exposure = std::isfinite(values->exposure) ? values->exposure : 1.0f;
        settings.white = std::isfinite(values->white) && values->white > 0.0f
                             ? values->white
                             : 1.0f;
        return settings;
    }

} // namespace godot_rt
