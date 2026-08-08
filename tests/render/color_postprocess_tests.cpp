#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "render/color_postprocess.h"

namespace {

bool expect_equal(float actual, float expected, const char* description) {
    if (actual == expected) {
        return true;
    }

    std::cerr << "FAILED: " << description
              << " (expected " << expected << ", got " << actual << ")\n";
    return false;
}

bool expect_near(float actual, float expected, float tolerance, const char* description) {
    if (std::fabs(actual - expected) <= tolerance) {
        return true;
    }

    std::cerr << "FAILED: " << description
              << " (expected " << expected << ", got " << actual << ")\n";
    return false;
}

bool expect_equal(godot_rt::ToneMapperMode actual,
                  godot_rt::ToneMapperMode expected,
                  const char* description) {
    if (actual == expected) {
        return true;
    }

    std::cerr << "FAILED: " << description << "\n";
    return false;
}

} // namespace

int main() {
    bool passed = true;

    const godot_rt::OutputPostprocessSettings settings;
    passed &= expect_equal(settings.exposure, 1.0f, "default exposure is one");
    passed &= expect_equal(settings.white, 1.0f, "default white point is one");
    passed &= expect_equal(
        settings.tone_mapper,
        godot_rt::ToneMapperMode::Reinhard,
        "default tone mapper is Reinhard"
    );
    passed &= expect_equal(
        godot_rt::apply_exposure(0.25f, 2.0f),
        0.5f,
        "exposure multiplies linear radiance"
    );
    passed &= expect_equal(
        godot_rt::tonemap_reinhard(2.0f, 4.0f),
        0.75f,
        "extended Reinhard matches Godot's white-point formula"
    );
    passed &= expect_equal(
        godot_rt::tonemap_reinhard(2.0f, 1.0f),
        2.0f,
        "extended Reinhard preserves positive values when the white point is one"
    );
    passed &= expect_near(
        godot_rt::linear_to_srgb(0.0031307f),
        0.040448644f,
        0.000001f,
        "linear-to-sRGB uses the low segment below Godot's threshold"
    );
    passed &= expect_near(
        godot_rt::linear_to_srgb(0.0031308f),
        0.040449907f,
        0.000001f,
        "linear-to-sRGB uses the power segment at Godot's threshold"
    );
    passed &= expect_near(
        godot_rt::linear_to_srgb(0.21404114f),
        0.5f,
        0.000001f,
        "linear-to-sRGB matches the middle-gray reference value"
    );

    const godot_rt::OutputPostprocessSettings bright_settings{
        2.0f,
        4.0f,
        godot_rt::ToneMapperMode::Reinhard,
    };
    const godot_rt::RgbColor fully_processed = godot_rt::postprocess_linear_radiance(
        godot_rt::RgbColor{1.0f, 1.0f, 1.0f},
        bright_settings
    );
    passed &= expect_near(
        fully_processed.r,
        0.880825021f,
        0.000001f,
        "full output processing applies exposure before extended Reinhard and sRGB"
    );
    passed &= expect_near(
        fully_processed.g,
        0.880825021f,
        0.000001f,
        "full output processing applies the same transform to green"
    );
    passed &= expect_near(
        fully_processed.b,
        0.880825021f,
        0.000001f,
        "full output processing applies the same transform to blue"
    );

    const godot_rt::RgbColor preserved_hdr = godot_rt::postprocess_linear_radiance(
        godot_rt::RgbColor{4.0f, 4.0f, 4.0f},
        godot_rt::OutputPostprocessSettings{1.0f, 4.0f, godot_rt::ToneMapperMode::Reinhard}
    );
    passed &= expect_near(
        preserved_hdr.r,
        1.0f,
        0.000001f,
        "HDR radiance is not upper-clamped before Reinhard"
    );

    const godot_rt::RgbColor negative_radiance = godot_rt::postprocess_linear_radiance(
        godot_rt::RgbColor{-1.0f, -0.25f, 0.0f},
        bright_settings
    );
    passed &= expect_near(
        negative_radiance.r,
        0.0f,
        0.000001f,
        "negative red radiance is protected before output encoding"
    );
    passed &= expect_near(
        negative_radiance.g,
        0.0f,
        0.000001f,
        "negative green radiance is protected before output encoding"
    );

    const godot_rt::OutputPostprocessSettings default_environment_settings =
        godot_rt::resolve_output_postprocess_settings(nullptr, nullptr);
    passed &= expect_equal(
        default_environment_settings.exposure,
        1.0f,
        "missing environments use default exposure"
    );
    passed &= expect_equal(
        default_environment_settings.white,
        1.0f,
        "missing environments use default white point"
    );

    const godot_rt::EnvironmentTonemapValues camera_values{2.0f, 4.0f};
    const godot_rt::EnvironmentTonemapValues world_values{3.0f, 5.0f};
    const godot_rt::OutputPostprocessSettings camera_priority_settings =
        godot_rt::resolve_output_postprocess_settings(&camera_values, &world_values);
    passed &= expect_equal(
        camera_priority_settings.exposure,
        2.0f,
        "camera environment exposure overrides world environment"
    );
    passed &= expect_equal(
        camera_priority_settings.white,
        4.0f,
        "camera environment white point overrides world environment"
    );

    const godot_rt::OutputPostprocessSettings world_fallback_settings =
        godot_rt::resolve_output_postprocess_settings(nullptr, &world_values);
    passed &= expect_equal(
        world_fallback_settings.exposure,
        3.0f,
        "world environment provides exposure when camera override is absent"
    );
    passed &= expect_equal(
        world_fallback_settings.white,
        5.0f,
        "world environment provides white point when camera override is absent"
    );

    const godot_rt::EnvironmentTonemapValues zero_exposure{0.0f, 3.0f};
    const godot_rt::OutputPostprocessSettings zero_exposure_settings =
        godot_rt::resolve_output_postprocess_settings(&zero_exposure, nullptr);
    passed &= expect_equal(
        zero_exposure_settings.exposure,
        0.0f,
        "valid zero exposure is preserved"
    );

    const godot_rt::EnvironmentTonemapValues invalid_values{
        std::numeric_limits<float>::quiet_NaN(),
        0.0f,
    };
    const godot_rt::OutputPostprocessSettings sanitized_settings =
        godot_rt::resolve_output_postprocess_settings(&invalid_values, nullptr);
    passed &= expect_equal(
        sanitized_settings.exposure,
        1.0f,
        "non-finite exposure falls back to the default"
    );
    passed &= expect_equal(
        sanitized_settings.white,
        1.0f,
        "non-positive white point falls back to the default"
    );

    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
