#ifndef GDEXTENSION_CPP_EXAMPLE_COLOR_POSTPROCESS_H
#define GDEXTENSION_CPP_EXAMPLE_COLOR_POSTPROCESS_H

#include <cstdint>

namespace godot_rt {

    // 预留 Godot 可选 tone mapper 的分发接口；本需求第一阶段只实际使用 Reinhard。
    enum class ToneMapperMode : std::uint8_t {
        Linear,
        Reinhard,
        Filmic,
        ACES,
        AgX,
    };

    // 该设置是导出边界的纯标量快照，不持有 Godot Resource 或 Node。
    struct OutputPostprocessSettings {
        float exposure = 1.0f;
        float white = 1.0f;
        ToneMapperMode tone_mapper = ToneMapperMode::Reinhard;
    };

    struct RgbColor {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
    };

    // 由 Godot Environment 读取后立刻复制的纯标量候选值。
    struct EnvironmentTonemapValues {
        float exposure = 1.0f;
        float white = 1.0f;
    };

    float apply_exposure(float linear_radiance, float exposure);
    float tonemap_reinhard(float color, float white);
    float linear_to_srgb(float color);
    RgbColor postprocess_linear_radiance(const RgbColor& radiance,
                                         const OutputPostprocessSettings& settings);
    OutputPostprocessSettings resolve_output_postprocess_settings(
        const EnvironmentTonemapValues* camera_environment,
        const EnvironmentTonemapValues* world_environment
    );

} // namespace godot_rt

#endif // GDEXTENSION_CPP_EXAMPLE_COLOR_POSTPROCESS_H
