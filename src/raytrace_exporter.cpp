#include "raytrace_exporter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>

#include "accel/brute_force_accel.h"
#include "render/color_postprocess.h"
#include "render/cpu_path_tracer.h"
#include "render/film.h"
#include "render/frame_accumulator.h"
#include "render/parallel_task_scheduler.h"
#include "render/render_execution_policy.h"
#include "scene/scene_extractor.h"
#include "util/logger.h"

using namespace godot;

namespace {

    // 本文件的匿名命名空间集中放置 RayTraceExporter 的私有实现：
    // 包括渲染请求/结果的数据快照、同步与异步共用的 helper、耗时日志、
    // 以及跨 Godot 调用保存的后台渲染任务表。这里的符号不暴露给 GDExtension API。
    constexpr int TILE_SIZE = 16;
    constexpr const char DEFAULT_OUTPUT_DIR[] = "user://raytrace_output";
    constexpr const char DEFAULT_OUTPUT_PREFIX[] = "current_scene";
    using TimingClock = std::chrono::steady_clock;

    enum class RenderMode {
        Full,
        Pixel,
        Tile,
    };

    struct RenderSelection {
        RenderMode mode = RenderMode::Full;
        Vector2i target_pixel = Vector2i(0, 0);
        Vector2i target_tile = Vector2i(0, 0);
    };

    // 单个阶段的耗时记录。name 使用字符串常量，避免在渲染热路径中额外分配。
    struct TimingEntry {
        const char* name = "";
        double elapsed_ms = 0.0;
    };

    struct LightRecord {
        int32_t index = 0;
        String type;
        Color color;
        double energy = 0.0;
        double range = 0.0;
        double attenuation = 0.0;
        double spot_angle_degrees = 0.0;
        double spot_attenuation = 0.0;
        bool casts_shadow = false;
        Vector3 position;
        Vector3 direction;
    };

    struct LightStatistics {
        int32_t light_count = 0;
        int32_t directional_light_count = 0;
        int32_t omni_light_count = 0;
        int32_t spot_light_count = 0;
        int32_t shadow_light_count = 0;
        std::vector<LightRecord> lights;
    };

    // RenderRequest 是一次渲染提交时的完整快照。
    // 场景、相机、settings 和输出路径在主线程准备好后传入渲染流程；
    // 异步渲染时 worker 只读取这份快照，避免继续依赖 Godot 场景树的实时状态。
    struct RenderRequest {
        godot_rt::Scene scene;
        godot_rt::Camera camera;
        godot_rt::CpuPathTracerSettings settings;
        godot_rt::OutputPostprocessSettings output_postprocess_settings;
        RenderSelection selection;
        String save_path;
        String primary_hit_mask_path;
        String timing_log_path;
        TimingClock::time_point total_start;
        std::vector<TimingEntry> timing_entries;
        std::thread::id submission_thread_id;
    };

    // RenderOutcome 记录渲染结束或提前失败后的结果。
    // 它同时服务同步返回值、异步轮询状态和 timing log，因此保留了路径、
    // 图像参数、tile 进度、错误信息和各阶段耗时。
    struct RenderOutcome {
        bool ok = false;
        bool cancelled = false;
        String path;
        String primary_hit_mask_path;
        String timing_log_path;
        String error;
        Vector2i image_size = Vector2i(0, 0);
        int32_t samples_per_pixel = 1;
        int32_t max_depth = 0;
        int32_t triangle_count = 0;
        LightStatistics light_statistics;
        RenderSelection selection;
        int tiles_done = 0;
        int total_tiles = 0;
        int64_t primary_ray_count = 0;
        int64_t primary_ray_hit_count = 0;
        int64_t primary_ray_miss_count = 0;
        double intersection_ms = 0.0;
        double total_ms = 0.0;
        std::vector<TimingEntry> timing_entries;
    };

    // 工作线程只填充纯计算结果；Film 由 tracer 持有，等待提交线程完成图像与文件导出。
    struct RenderComputation {
        RenderOutcome outcome;
        std::unique_ptr<godot_rt::CpuPathTracer> tracer;
        std::vector<String> deferred_logs;
        bool render_completed = false;
    };

    // RenderJob 保存一个后台渲染任务的跨调用状态。
    // 原子字段用于 worker 与 Godot 主线程之间的轻量通信；outcome 由 result_mutex 保护，
    // 因为完成状态、轮询和释放可能从不同的 Godot 调用路径访问它。
    struct RenderJob {
        int64_t id = 0;
        RenderRequest request;
        std::thread worker;
        std::atomic_bool worker_can_start{false};
        std::atomic_bool cancel_requested{false};
        std::atomic_bool compute_done{false};
        std::atomic_bool done{false};
        std::atomic_int tiles_done{0};
        int total_tiles = 0;
        std::mutex result_mutex;
        RenderOutcome outcome;
        std::unique_ptr<RenderComputation> computation;
    };

    // 全局任务表保存仍可被 poll/cancel/release 访问的后台任务。
    // g_jobs_mutex 保护任务表结构，g_timing_log_mutex 避免多个渲染同时追加同一个日志文件。
    std::mutex g_jobs_mutex;
    std::mutex g_timing_log_mutex;
    std::unordered_map<int64_t, std::shared_ptr<RenderJob>> g_render_jobs;
    std::atomic<int64_t> g_next_job_id{1};
    std::atomic<int64_t> g_next_output_sequence{1};

    const char* light_type_name(godot_rt::LightType type) {
        switch (type) {
            case godot_rt::LightType::Directional:
                return "DirectionalLight";
            case godot_rt::LightType::Omni:
                return "OmniLight";
            case godot_rt::LightType::Spot:
                return "SpotLight";
        }

        return "Light";
    }

    Vector3 light_forward_direction(const godot_rt::Light& light) {
        Vector3 direction = light.transform.basis.xform(Vector3(0.0f, 0.0f, -1.0f));
        if (direction.length_squared() <= 0.0f) {
            return Vector3(0.0f, 0.0f, -1.0f);
        }

        direction.normalize();
        return direction;
    }

    // 光源统计在场景提取完成后立即变成一份纯数据快照。
    // 这样同步返回、异步轮询和 timing log 都读取同一份结果，避免 worker 再触碰 Godot 场景树。
    LightStatistics collect_light_statistics(const godot_rt::Scene& scene) {
        LightStatistics statistics;
        const std::vector<godot_rt::Light>& lights = scene.get_lights();
        statistics.light_count = static_cast<int32_t>(lights.size());
        statistics.lights.reserve(lights.size());

        for (int32_t index = 0; index < static_cast<int32_t>(lights.size()); ++index) {
            const godot_rt::Light& light = lights[index];

            switch (light.type) {
                case godot_rt::LightType::Directional:
                    ++statistics.directional_light_count;
                    break;
                case godot_rt::LightType::Omni:
                    ++statistics.omni_light_count;
                    break;
                case godot_rt::LightType::Spot:
                    ++statistics.spot_light_count;
                    break;
            }

            if (light.casts_shadow) {
                ++statistics.shadow_light_count;
            }

            LightRecord record;
            record.index = index;
            record.type = String(light_type_name(light.type));
            record.color = light.color;
            record.energy = static_cast<double>(light.energy);
            record.range = static_cast<double>(light.range);
            record.attenuation = static_cast<double>(light.attenuation);
            record.spot_angle_degrees = static_cast<double>(Math::rad_to_deg(light.spot_angle_radians));
            record.spot_attenuation = static_cast<double>(light.spot_attenuation);
            record.casts_shadow = light.casts_shadow;
            record.position = light.transform.origin;
            record.direction = light_forward_direction(light);
            statistics.lights.push_back(record);
        }

        return statistics;
    }

    Dictionary make_light_record_dictionary(const LightRecord& light) {
        Dictionary result;
        result["index"] = light.index;
        result["type"] = light.type;
        result["color"] = light.color;
        result["energy"] = light.energy;
        result["range"] = light.range;
        result["attenuation"] = light.attenuation;
        result["spot_angle_degrees"] = light.spot_angle_degrees;
        result["spot_attenuation"] = light.spot_attenuation;
        result["casts_shadow"] = light.casts_shadow;
        result["position"] = light.position;
        result["direction"] = light.direction;
        return result;
    }

    Array make_light_array(const LightStatistics& statistics) {
        Array lights;
        for (const LightRecord& light : statistics.lights) {
            lights.push_back(make_light_record_dictionary(light));
        }
        return lights;
    }

    // 返回字典中的光源字段保持扁平汇总 + lights 明细数组，方便脚本侧只读需要的粒度。
    void add_light_statistics_to_result(Dictionary& result, const LightStatistics& statistics) {
        result["light_count"] = statistics.light_count;
        result["directional_light_count"] = statistics.directional_light_count;
        result["omni_light_count"] = statistics.omni_light_count;
        result["spot_light_count"] = statistics.spot_light_count;
        result["shadow_light_count"] = statistics.shadow_light_count;
        result["lights"] = make_light_array(statistics);
    }

    void add_primary_ray_statistics_to_result(Dictionary& result,
                                              int64_t primary_ray_count,
                                              int64_t primary_ray_hit_count,
                                              int64_t primary_ray_miss_count) {
        result["primary_ray_count"] = primary_ray_count;
        result["primary_ray_hit_count"] = primary_ray_hit_count;
        result["primary_ray_miss_count"] = primary_ray_miss_count;
    }

    String format_bool(bool value) {
        return value ? String("true") : String("false");
    }

    String format_vector3(const Vector3& value) {
        return String("(") +
               String::num(value.x, 3) + ", " +
               String::num(value.y, 3) + ", " +
               String::num(value.z, 3) + ")";
    }

    String format_color(const Color& value) {
        return String("(") +
               String::num(value.r, 3) + ", " +
               String::num(value.g, 3) + ", " +
               String::num(value.b, 3) + ", " +
               String::num(value.a, 3) + ")";
    }

    const char* render_mode_name(RenderMode mode) {
        switch (mode) {
            case RenderMode::Pixel:
                return "pixel";
            case RenderMode::Tile:
                return "tile";
            case RenderMode::Full:
            default:
                return "full";
        }
    }

    // 统一生成返回给 GDScript/编辑器侧的基础结果字典。
    // 字段名保持稳定，调用方可以用 ok/error 判断失败，用 path/timing_log_path 找到产物。
    String make_tile_debug_path(const String& output_path, const RenderSelection& selection);

    Dictionary make_result(bool ok,
                           const String& path,
                           const String& primary_hit_mask_path,
                           const String& error,
                           const Vector2i& image_size,
                           int32_t samples_per_pixel,
                           const String& timing_log_path,
                           double total_ms,
                           const RenderSelection& selection,
                           int32_t triangle_count,
                           const LightStatistics& light_statistics,
                           int64_t primary_ray_count,
                           int64_t primary_ray_hit_count,
                           int64_t primary_ray_miss_count,
                           double intersection_ms) {
        Dictionary result;
        result["ok"] = ok;
        result["path"] = path;
        result["primary_hit_mask_path"] = primary_hit_mask_path;
        result["timing_log_path"] = timing_log_path;
        result["tile_debug_path"] = make_tile_debug_path(path, selection);
        result["error"] = error;
        result["width"] = image_size.x;
        result["height"] = image_size.y;
        result["samples_per_pixel"] = samples_per_pixel;
        result["total_ms"] = total_ms;
        result["render_mode"] = String(render_mode_name(selection.mode));
        result["target_pixel"] = selection.target_pixel;
        result["target_tile"] = selection.target_tile;
        result["triangle_count"] = triangle_count;
        add_light_statistics_to_result(result, light_statistics);
        add_primary_ray_statistics_to_result(result, primary_ray_count, primary_ray_hit_count,
                                             primary_ray_miss_count);
        result["intersection_ms"] = intersection_ms;
        return result;
    }

    // 异步任务的返回值在基础结果上增加任务生命周期字段。
    // exists/done/cancelled/progress 用于编辑器 UI 轮询，不要求调用方读取内部 job 状态。
    Dictionary make_job_result(int64_t job_id,
                               bool exists,
                               bool done,
                               bool cancelled,
                               double progress,
                               bool ok,
                               const String& path,
                               const String& primary_hit_mask_path,
                               const String& error,
                               const Vector2i& image_size,
                               int32_t samples_per_pixel,
                               const String& timing_log_path,
                               double total_ms,
                               const RenderSelection& selection,
                               int32_t triangle_count,
                               const LightStatistics& light_statistics,
                               int64_t primary_ray_count,
                               int64_t primary_ray_hit_count,
                               int64_t primary_ray_miss_count,
                               double intersection_ms) {
        Dictionary result = make_result(ok, path, primary_hit_mask_path, error, image_size, samples_per_pixel,
                                        timing_log_path, total_ms, selection, triangle_count,
                                        light_statistics, primary_ray_count, primary_ray_hit_count,
                                        primary_ray_miss_count, intersection_ms);
        result["job_id"] = job_id;
        result["exists"] = exists;
        result["done"] = done;
        result["cancelled"] = cancelled;
        result["progress"] = progress;
        return result;
    }

    // 所有找不到 job 的路径都返回同一种状况，减少脚本侧分支处理。
    Dictionary make_missing_job_result(int64_t job_id) {
        return make_job_result(job_id, false, true, false, 0.0, false, String(), String(),
                               "Render job not found.", Vector2i(0, 0), 1, String(), 0.0, RenderSelection(), 0,
                               LightStatistics(), 0, 0, 0, 0.0);
    }

    float clamp01(float value) {
        return std::max(0.0f, std::min(value, 1.0f));
    }

    double clamp_progress(double value) {
        return std::max(0.0, std::min(value, 1.0));
    }

    int ceil_div(int value, int divisor) {
        return divisor > 0 ? (value + divisor - 1) / divisor : 0;
    }

    int total_tile_count(const Vector2i& image_size) {
        return ceil_div(std::max(image_size.x, 0), TILE_SIZE) *
               ceil_div(std::max(image_size.y, 0), TILE_SIZE);
    }

    String get_string_option(const Dictionary& options, const char* key, const String& default_value) {
        const String option_key(key);
        return options.has(option_key) ? static_cast<String>(options[option_key]) : default_value;
    }

    int32_t get_int_option(const Dictionary& options, const char* key, int32_t default_value) {
        const String option_key(key);
        return options.has(option_key) ? static_cast<int32_t>(static_cast<int64_t>(options[option_key])) : default_value;
    }

    int64_t get_int64_option(const Dictionary& options, const char* key, int64_t default_value) {
        const String option_key(key);
        return options.has(option_key) ? static_cast<int64_t>(options[option_key]) : default_value;
    }

    Vector2i get_vector2i_option(const Dictionary& options, const char* key, const Vector2i& default_value) {
        const String option_key(key);
        return options.has(option_key) ? static_cast<Vector2i>(options[option_key]) : default_value;
    }

    bool parse_render_selection(const Dictionary& options, RenderSelection* out_selection, String* out_error) {
        RenderSelection selection;
        const String mode = get_string_option(options, "render_mode", "full").to_lower();
        if (mode == "pixel") {
            selection.mode = RenderMode::Pixel;
        } else if (mode == "tile") {
            selection.mode = RenderMode::Tile;
        } else if (mode != "full") {
            if (out_error != nullptr) {
                *out_error = "Invalid render_mode. Expected \"full\", \"pixel\", or \"tile\".";
            }
            return false;
        }

        selection.target_pixel = get_vector2i_option(options, "target_pixel", Vector2i(0, 0));
        selection.target_tile = get_vector2i_option(options, "target_tile", Vector2i(0, 0));
        if (out_selection != nullptr) {
            *out_selection = selection;
        }
        return true;
    }

    // timing helper 只记录毫秒级耗时，既用于同步渲染，也用于后台 worker。
    double elapsed_ms(TimingClock::time_point start, TimingClock::time_point end = TimingClock::now()) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    void add_timing(std::vector<TimingEntry>* timings, const char* name, double milliseconds) {
        if (timings == nullptr) {
            return;
        }

        timings->push_back(TimingEntry{name, milliseconds});
    }

    bool fill_local_time(std::time_t time, std::tm* out_time) {
        if (out_time == nullptr) {
            return false;
        }

#ifdef _WIN32
        return localtime_s(out_time, &time) == 0;
#else
        return localtime_r(&time, out_time) != nullptr;
#endif
    }

    // 默认输出路径按点击时间生成唯一 PNG 名称，避免多次导出覆盖 current_scene.png。
    // 同一毫秒内的极端连续提交用原子序号区分，文件名只包含路径安全字符。
    String make_unique_default_output_path() {
        const auto now = std::chrono::system_clock::now();
        const auto milliseconds_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        );
        const int milliseconds = static_cast<int>(milliseconds_since_epoch.count() % 1000);
        const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm local_time = {};
        fill_local_time(now_time, &local_time);

        const int sequence = static_cast<int>(
            g_next_output_sequence.fetch_add(1, std::memory_order_relaxed) % 1000000
        );

        char path[160];
        const int written = std::snprintf(
            path,
            sizeof(path),
            "%s/%s_%04d%02d%02d_%02d%02d%02d_%03d_%06d.png",
            DEFAULT_OUTPUT_DIR,
            DEFAULT_OUTPUT_PREFIX,
            local_time.tm_year + 1900,
            local_time.tm_mon + 1,
            local_time.tm_mday,
            local_time.tm_hour,
            local_time.tm_min,
            local_time.tm_sec,
            milliseconds,
            sequence
        );
        if (written <= 0 || written >= static_cast<int>(sizeof(path))) {
            return String(DEFAULT_OUTPUT_DIR) + String("/") + String(DEFAULT_OUTPUT_PREFIX) + String("_") +
                   String::num_int64(sequence) + String(".png");
        }

        return String(path);
    }

    String resolve_output_path(const String& output_path) {
        if (output_path.is_empty()) {
            return make_unique_default_output_path();
        }

        return output_path;
    }

    // timing log 默认与输出 PNG 同名，便于用户从渲染产物反查性能记录。
    String make_timing_log_path(const String& output_path) {
        const String basename = output_path.get_basename();
        if (basename.is_empty()) {
            return output_path + String(".timings.log");
        }

        return basename + String(".timings.log");
    }

    String make_primary_hit_mask_path(const String& output_path) {
        const String basename = output_path.get_basename();
        if (basename.is_empty()) {
            return output_path + String("_primary_hit_mask.png");
        }

        return basename + String("_primary_hit_mask.png");
    }

    String make_tile_debug_path(const String& output_path, const RenderSelection& selection) {
        if (selection.mode != RenderMode::Tile) {
            return String();
        }

        const String suffix = String("_tile_") +
                              String::num_int64(selection.target_tile.x) + String("_") +
                              String::num_int64(selection.target_tile.y) + String("_debug.png");
        const String basename = output_path.get_basename();
        if (basename.is_empty()) {
            return output_path + suffix;
        }

        return basename + suffix;
    }

    // timing log 中使用稳定的短状态名，避免把 Godot Dictionary 的字段格式耦合到日志文本。
    const char* status_name(const RenderOutcome& outcome) {
        if (outcome.ok) {
            return "ok";
        }
        if (outcome.cancelled) {
            return "cancelled";
        }
        return "error";
    }

    // 将 Godot 的 Camera3D 转成渲染器内部相机。
    // 这里保留 Godot keep-aspect 选择的 FOV 轴向，否则导出的视角会和编辑器视口不一致。
    godot_rt::Camera make_render_camera(const Camera3D* camera) {
        godot_rt::Camera render_camera;
        render_camera.camera_to_world = camera->get_camera_transform();
        render_camera.fov_y_radians = Math::deg_to_rad(camera->get_fov());
        render_camera.fov_axis = camera->get_keep_aspect_mode() == Camera3D::KEEP_WIDTH
                                     ? godot_rt::CameraFovAxis::Horizontal
                                     : godot_rt::CameraFovAxis::Vertical;
        return render_camera;
    }

    // 在主线程读取 Godot Environment，再把纯标量复制到 RenderRequest。
    // 对齐 RendererSceneCull::_render_get_environment()：Camera override 优先于 World3D。
    godot_rt::OutputPostprocessSettings capture_output_postprocess_settings(const Camera3D* camera) {
        if (camera == nullptr) {
            return godot_rt::resolve_output_postprocess_settings(nullptr, nullptr);
        }

        const Ref<Environment> camera_environment = camera->get_environment();
        if (camera_environment.is_valid()) {
            const godot_rt::EnvironmentTonemapValues camera_values{
                camera_environment->get_tonemap_exposure(),
                camera_environment->get_tonemap_white(),
            };
            return godot_rt::resolve_output_postprocess_settings(&camera_values, nullptr);
        }

        Viewport* viewport = camera->get_viewport();
        if (viewport != nullptr) {
            const Ref<World3D> world = viewport->find_world_3d();
            if (world.is_valid()) {
                const Ref<Environment> world_environment = world->get_environment();
                if (world_environment.is_valid()) {
                    const godot_rt::EnvironmentTonemapValues world_values{
                        world_environment->get_tonemap_exposure(),
                        world_environment->get_tonemap_white(),
                    };
                    return godot_rt::resolve_output_postprocess_settings(nullptr, &world_values);
                }
            }
        }

        return godot_rt::resolve_output_postprocess_settings(nullptr, nullptr);
    }

    // Tile debug overlay drawing helpers.
    bool is_inside_image(const Vector2i& image_size, int x, int y) {
        return x >= 0 && y >= 0 && x < image_size.x && y < image_size.y;
    }

    void blend_pixel(const Ref<Image>& image, const Vector2i& image_size, int x, int y, const Color& color) {
        if (image.is_null() || !is_inside_image(image_size, x, y)) {
            return;
        }

        const float alpha = clamp01(color.a);
        const float inverse_alpha = 1.0f - alpha;
        const Color base = image->get_pixel(x, y);
        image->set_pixel(
            x,
            y,
            Color(
                clamp01(base.r * inverse_alpha + color.r * alpha),
                clamp01(base.g * inverse_alpha + color.g * alpha),
                clamp01(base.b * inverse_alpha + color.b * alpha),
                1.0f
            )
        );
    }

    void blend_rect(const Ref<Image>& image,
                    const Vector2i& image_size,
                    const Vector2i& origin,
                    const Vector2i& size,
                    const Color& color) {
        if (image.is_null() || size.x <= 0 || size.y <= 0) {
            return;
        }

        const int x_begin = std::max(origin.x, 0);
        const int y_begin = std::max(origin.y, 0);
        const int x_end = std::min(origin.x + size.x, image_size.x);
        const int y_end = std::min(origin.y + size.y, image_size.y);
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = x_begin; x < x_end; ++x) {
                blend_pixel(image, image_size, x, y, color);
            }
        }
    }

    void blend_outline_rect(const Ref<Image>& image,
                            const Vector2i& image_size,
                            const Vector2i& origin,
                            const Vector2i& size,
                            const Color& color,
                            int thickness) {
        if (size.x <= 0 || size.y <= 0 || thickness <= 0) {
            return;
        }

        for (int i = 0; i < thickness; ++i) {
            blend_rect(image, image_size, Vector2i(origin.x, origin.y + i), Vector2i(size.x, 1), color);
            blend_rect(image, image_size, Vector2i(origin.x, origin.y + size.y - 1 - i), Vector2i(size.x, 1), color);
            blend_rect(image, image_size, Vector2i(origin.x + i, origin.y), Vector2i(1, size.y), color);
            blend_rect(image, image_size, Vector2i(origin.x + size.x - 1 - i, origin.y), Vector2i(1, size.y), color);
        }
    }

    const char* const* debug_glyph_rows(char32_t ch) {
        static const char* const glyph_0[7] = { "#####", "#...#", "#..##", "#.#.#", "##..#", "#...#", "#####" };
        static const char* const glyph_1[7] = { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###." };
        static const char* const glyph_2[7] = { "#####", "....#", "....#", "#####", "#....", "#....", "#####" };
        static const char* const glyph_3[7] = { "#####", "....#", "....#", ".####", "....#", "....#", "#####" };
        static const char* const glyph_4[7] = { "#...#", "#...#", "#...#", "#####", "....#", "....#", "....#" };
        static const char* const glyph_5[7] = { "#####", "#....", "#....", "#####", "....#", "....#", "#####" };
        static const char* const glyph_6[7] = { "#####", "#....", "#....", "#####", "#...#", "#...#", "#####" };
        static const char* const glyph_7[7] = { "#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..." };
        static const char* const glyph_8[7] = { "#####", "#...#", "#...#", "#####", "#...#", "#...#", "#####" };
        static const char* const glyph_9[7] = { "#####", "#...#", "#...#", "#####", "....#", "....#", "#####" };
        static const char* const glyph_t[7] = { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..##." };
        static const char* const glyph_i[7] = { ".###.", "..#..", ".....", ".##..", "..#..", "..#..", ".###." };
        static const char* const glyph_l[7] = { ".##..", "..#..", "..#..", "..#..", "..#..", "..#..", ".###." };
        static const char* const glyph_e[7] = { ".....", ".....", ".###.", "#...#", "#####", "#....", ".###." };
        static const char* const glyph_p[7] = { ".....", ".....", "####.", "#...#", "####.", "#....", "#...." };
        static const char* const glyph_x[7] = { ".....", ".....", "#...#", ".#.#.", "..#..", ".#.#.", "#...#" };
        static const char* const glyph_equal[7] = { ".....", ".....", "#####", ".....", "#####", ".....", "....." };
        static const char* const glyph_comma[7] = { ".....", ".....", ".....", ".....", ".....", "..#..", ".#..." };
        static const char* const glyph_minus[7] = { ".....", ".....", ".....", "#####", ".....", ".....", "....." };
        static const char* const glyph_blank[7] = { ".....", ".....", ".....", ".....", ".....", ".....", "....." };

        switch (ch) {
            case '0':
                return glyph_0;
            case '1':
                return glyph_1;
            case '2':
                return glyph_2;
            case '3':
                return glyph_3;
            case '4':
                return glyph_4;
            case '5':
                return glyph_5;
            case '6':
                return glyph_6;
            case '7':
                return glyph_7;
            case '8':
                return glyph_8;
            case '9':
                return glyph_9;
            case 't':
                return glyph_t;
            case 'i':
                return glyph_i;
            case 'l':
                return glyph_l;
            case 'e':
                return glyph_e;
            case 'p':
                return glyph_p;
            case 'x':
                return glyph_x;
            case '=':
                return glyph_equal;
            case ',':
                return glyph_comma;
            case '-':
                return glyph_minus;
            default:
                return glyph_blank;
        }
    }

    int debug_text_width(const String& text, int scale) {
        const int64_t length = text.length();
        if (length <= 0 || scale <= 0) {
            return 0;
        }

        constexpr int glyph_width = 5;
        return static_cast<int>(length * (glyph_width + 1) * scale - scale);
    }

    void draw_debug_text(const Ref<Image>& image,
                         const Vector2i& image_size,
                         const Vector2i& origin,
                         const String& text,
                         const Color& color,
                         int scale) {
        if (image.is_null() || scale <= 0) {
            return;
        }

        constexpr int glyph_width = 5;
        constexpr int glyph_height = 7;
        int cursor_x = origin.x;
        for (int64_t index = 0; index < text.length(); ++index) {
            const char* const* rows = debug_glyph_rows(text[index]);
            for (int row = 0; row < glyph_height; ++row) {
                for (int column = 0; column < glyph_width; ++column) {
                    if (rows[row][column] != '#') {
                        continue;
                    }

                    blend_rect(
                        image,
                        image_size,
                        Vector2i(cursor_x + column * scale, origin.y + row * scale),
                        Vector2i(scale, scale),
                        color
                    );
                }
            }
            cursor_x += (glyph_width + 1) * scale;
        }
    }

    Ref<Image> tile_debug_overlay_to_image(const Ref<Image>& source,
                                           const Vector2i& image_size,
                                           const Vector2i& target_tile) {
        Ref<Image> image = Image::create_empty(image_size.x, image_size.y, false, Image::FORMAT_RGBA8);
        if (image.is_null() || source.is_null()) {
            return image;
        }

        image->copy_from(source);

        const Vector2i tile_origin(target_tile.x * TILE_SIZE, target_tile.y * TILE_SIZE);
        const Vector2i selected_tile_size(
            std::min(TILE_SIZE, image_size.x - tile_origin.x),
            std::min(TILE_SIZE, image_size.y - tile_origin.y)
        );

        const Color grid_color(1.0f, 1.0f, 1.0f, 0.24f);
        const Color selected_fill_color(1.0f, 0.05f, 0.03f, 0.24f);
        const Color selected_border_color(1.0f, 0.02f, 0.02f, 1.0f);
        const Color selected_inner_border_color(1.0f, 0.95f, 0.05f, 0.85f);
        const Color label_background_color(0.0f, 0.0f, 0.0f, 0.76f);
        const Color label_border_color(1.0f, 0.95f, 0.05f, 0.85f);
        const Color label_title_color(1.0f, 0.95f, 0.05f, 1.0f);
        const Color label_detail_color(1.0f, 1.0f, 1.0f, 1.0f);

        blend_rect(image, image_size, tile_origin, selected_tile_size, selected_fill_color);

        for (int x = 0; x < image_size.x; x += TILE_SIZE) {
            blend_rect(image, image_size, Vector2i(x, 0), Vector2i(1, image_size.y), grid_color);
        }
        for (int y = 0; y < image_size.y; y += TILE_SIZE) {
            blend_rect(image, image_size, Vector2i(0, y), Vector2i(image_size.x, 1), grid_color);
        }

        blend_outline_rect(image, image_size, tile_origin, selected_tile_size, selected_border_color, 2);
        if (selected_tile_size.x > 4 && selected_tile_size.y > 4) {
            blend_outline_rect(
                image,
                image_size,
                Vector2i(tile_origin.x + 2, tile_origin.y + 2),
                Vector2i(selected_tile_size.x - 4, selected_tile_size.y - 4),
                selected_inner_border_color,
                1
            );
        }

        const int x_end = tile_origin.x + selected_tile_size.x - 1;
        const int y_end = tile_origin.y + selected_tile_size.y - 1;
        const String tile_label = String("tile=") +
                                  String::num_int64(target_tile.x) + String(",") +
                                  String::num_int64(target_tile.y);
        const String pixel_label = String("px=") +
                                   String::num_int64(tile_origin.x) + String(",") +
                                   String::num_int64(tile_origin.y) + String("-") +
                                   String::num_int64(x_end) + String(",") +
                                   String::num_int64(y_end);

        constexpr int padding = 3;
        constexpr int title_scale = 2;
        constexpr int detail_scale = 1;
        constexpr int glyph_height = 7;
        constexpr int line_gap = 3;
        const int title_height = glyph_height * title_scale;
        const int detail_height = glyph_height * detail_scale;
        const int label_width = std::max(debug_text_width(tile_label, title_scale),
                                         debug_text_width(pixel_label, detail_scale)) +
                                padding * 2;
        const int label_height = title_height + line_gap + detail_height + padding * 2;
        Vector2i label_origin(tile_origin.x + 3, tile_origin.y + 3);
        if (label_origin.x + label_width > image_size.x) {
            label_origin.x = std::max(0, image_size.x - label_width);
        }
        if (label_origin.y + label_height > image_size.y) {
            label_origin.y = std::max(0, image_size.y - label_height);
        }

        blend_rect(image, image_size, label_origin, Vector2i(label_width, label_height), label_background_color);
        blend_outline_rect(image, image_size, label_origin, Vector2i(label_width, label_height), label_border_color, 1);
        draw_debug_text(
            image,
            image_size,
            Vector2i(label_origin.x + padding, label_origin.y + padding),
            tile_label,
            label_title_color,
            title_scale
        );
        draw_debug_text(
            image,
            image_size,
            Vector2i(label_origin.x + padding, label_origin.y + padding + title_height + line_gap),
            pixel_label,
            label_detail_color,
            detail_scale
        );

        return image;
    }

    // 保存 PNG 前先确保目标目录存在；失败信息通过 error 返回给 Dictionary。
    // output_path 可以是 res:// 这样的 Godot 路径，因此使用 DirAccess 的 absolute helper。
    bool ensure_output_dir(const String& output_path, String* error) {
        const String output_dir = output_path.get_base_dir();
        if (output_dir.is_empty()) {
            return true;
        }

        const Error mkdir_error = DirAccess::make_dir_recursive_absolute(output_dir);
        if (mkdir_error != OK && mkdir_error != ERR_ALREADY_EXISTS) {
            if (error != nullptr) {
                *error = "Could not create output directory: " + output_dir;
            }
            return false;
        }

        return true;
    }

    // 将 Film 中的线性 HDR radiance 均值转换成显示编码后的 RGBA8 Image。
    // Film 本身仍保留原始线性累积；曝光、tone mapping 与 sRGB 只在这里发生。
    Ref<Image> film_to_image(const godot_rt::Film& film,
                             const Vector2i& image_size,
                             const godot_rt::OutputPostprocessSettings& output_postprocess_settings) {
        Ref<Image> image = Image::create_empty(image_size.x, image_size.y, false, Image::FORMAT_RGBA8);
        if (image.is_null()) {
            return image;
        }

        for (int y = 0; y < image_size.y; ++y) {
            for (int x = 0; x < image_size.x; ++x) {
                const Color radiance = film.get_average(Vector2i(x, y));
                const godot_rt::RgbColor display_color = godot_rt::postprocess_linear_radiance(
                    godot_rt::RgbColor{radiance.r, radiance.g, radiance.b},
                    output_postprocess_settings
                );
                image->set_pixel(
                    x,
                    y,
                    Color(display_color.r, display_color.g, display_color.b, 1.0f)
                );
            }
        }

        return image;
    }

    // Tile debug 是诊断产物，保留 REQ-001 之前的线性 clamp 底图，
    // 不受 Environment exposure、tone mapping 或 sRGB 编码影响。
    Ref<Image> film_to_linear_clamped_debug_image(const godot_rt::Film& film,
                                                  const Vector2i& image_size) {
        Ref<Image> image = Image::create_empty(image_size.x, image_size.y, false, Image::FORMAT_RGBA8);
        if (image.is_null()) {
            return image;
        }

        for (int y = 0; y < image_size.y; ++y) {
            for (int x = 0; x < image_size.x; ++x) {
                const Color radiance = film.get_average(Vector2i(x, y));
                image->set_pixel(
                    x,
                    y,
                    Color(clamp01(radiance.r), clamp01(radiance.g), clamp01(radiance.b), 1.0f)
                );
            }
        }

        return image;
    }

    Ref<Image> primary_hit_mask_to_image(const godot_rt::CpuPathTracer& tracer, const Vector2i& image_size) {
        Ref<Image> image = Image::create_empty(image_size.x, image_size.y, false, Image::FORMAT_RGBA8);
        if (image.is_null()) {
            return image;
        }

        const Color hit_color(0.0f, 1.0f, 0.0f, 1.0f);
        const Color miss_color(1.0f, 0.0f, 1.0f, 1.0f);
        const Color mixed_color(1.0f, 1.0f, 0.0f, 1.0f);
        const Color no_sample_color(0.0f, 0.4f, 1.0f, 1.0f);

        for (int y = 0; y < image_size.y; ++y) {
            for (int x = 0; x < image_size.x; ++x) {
                const Vector2i pixel(x, y);
                const int hit_count = tracer.get_primary_hit_sample_count(pixel);
                const int miss_count = tracer.get_primary_miss_sample_count(pixel);

                Color mask_color = no_sample_color;
                if (hit_count > 0 && miss_count > 0) {
                    mask_color = mixed_color;
                } else if (hit_count > 0) {
                    mask_color = hit_color;
                } else if (miss_count > 0) {
                    mask_color = miss_color;
                }

                image->set_pixel(x, y, mask_color);
            }
        }

        return image;
    }

    // 初始 outcome 用于异步任务刚创建、尚未完成时的轮询结果。
    // 它复制请求中的静态信息，让 UI 能在 worker 产出最终结果前显示尺寸、路径和总 tile 数。
    RenderOutcome make_initial_outcome(const RenderRequest& request) {
        RenderOutcome outcome;
        outcome.path = request.save_path;
        outcome.primary_hit_mask_path = request.primary_hit_mask_path;
        outcome.timing_log_path = request.timing_log_path;
        outcome.image_size = request.settings.image_size;
        outcome.samples_per_pixel = request.settings.samples_per_pixel;
        outcome.max_depth = request.settings.max_depth;
        outcome.triangle_count = request.scene.triangle_count();
        outcome.light_statistics = collect_light_statistics(request.scene);
        outcome.selection = request.selection;
        outcome.total_tiles = request.selection.mode == RenderMode::Full
                                  ? total_tile_count(request.settings.image_size)
                                  : 1;
        outcome.timing_entries = request.timing_entries;
        return outcome;
    }

    void copy_render_statistics(const godot_rt::CpuPathTracer& tracer, RenderOutcome* outcome) {
        if (outcome == nullptr) {
            return;
        }

        const godot_rt::RenderStatistics& statistics = tracer.get_statistics();
        outcome->intersection_ms = statistics.intersection_ms;
        outcome->primary_ray_count = statistics.primary_ray_count;
        outcome->primary_ray_hit_count = statistics.primary_ray_hit_count;
        outcome->primary_ray_miss_count = statistics.primary_ray_miss_count;
    }

    // 追加写入一次渲染的耗时日志。
    // 多个同步/异步渲染可能写同一个文件，因此用全局日志锁串行化文件打开、seek 和写入。
    void write_timing_log(const RenderRequest& request, const RenderOutcome& outcome, const char* mode) {
        if (request.timing_log_path.is_empty()) {
            return;
        }

        std::lock_guard<std::mutex> log_lock(g_timing_log_mutex);

        Ref<FileAccess> file;
        if (FileAccess::file_exists(request.timing_log_path)) {
            file = FileAccess::open(request.timing_log_path, FileAccess::READ_WRITE);
            if (!file.is_null()) {
                file->seek_end();
            }
        } else {
            file = FileAccess::open(request.timing_log_path, FileAccess::WRITE);
        }

        if (file.is_null() || !file->is_open()) {
            return;
        }

        file->store_line("[raytrace timing]");
        file->store_line(String("status: ") + String(status_name(outcome)));
        file->store_line(String("mode: ") + String(mode));
        file->store_line(String("output: ") + request.save_path);
        file->store_line(String("primary_hit_mask_path: ") + outcome.primary_hit_mask_path);
        const String tile_debug_path = make_tile_debug_path(request.save_path, request.selection);
        if (!tile_debug_path.is_empty()) {
            file->store_line(String("tile_debug_path: ") + tile_debug_path);
        }
        file->store_line(String("timing_log: ") + request.timing_log_path);
        file->store_line(String("image_size: ") + String::num_int64(request.settings.image_size.x) + "x" +
                         String::num_int64(request.settings.image_size.y));
        file->store_line(String("samples_per_pixel: ") + String::num_int64(request.settings.samples_per_pixel));
        file->store_line(String("max_depth: ") + String::num_int64(request.settings.max_depth));
        file->store_line(String("triangle_count: ") + String::num_int64(outcome.triangle_count));
        file->store_line(String("primary_ray_count: ") + String::num_int64(outcome.primary_ray_count));
        file->store_line(String("primary_ray_hit_count: ") + String::num_int64(outcome.primary_ray_hit_count));
        file->store_line(String("primary_ray_miss_count: ") + String::num_int64(outcome.primary_ray_miss_count));
        file->store_line(String("light_count: ") + String::num_int64(outcome.light_statistics.light_count));
        file->store_line(
            String("light_summary: directional=") +
            String::num_int64(outcome.light_statistics.directional_light_count) +
            String(" omni=") + String::num_int64(outcome.light_statistics.omni_light_count) +
            String(" spot=") + String::num_int64(outcome.light_statistics.spot_light_count) +
            String(" shadow=") + String::num_int64(outcome.light_statistics.shadow_light_count)
        );
        // 明细行只记录提取后的光源快照，不重新访问 Godot 节点，保证异步日志和返回值一致。
        for (const LightRecord& light : outcome.light_statistics.lights) {
            file->store_line(
                String("light[") + String::num_int64(light.index) + "]: " +
                String("type=") + light.type +
                String(" color=") + format_color(light.color) +
                String(" energy=") + String::num(light.energy, 3) +
                String(" range=") + String::num(light.range, 3) +
                String(" attenuation=") + String::num(light.attenuation, 3) +
                String(" spot_angle_degrees=") + String::num(light.spot_angle_degrees, 3) +
                String(" spot_attenuation=") + String::num(light.spot_attenuation, 3) +
                String(" casts_shadow=") + format_bool(light.casts_shadow) +
                String(" position=") + format_vector3(light.position) +
                String(" direction=") + format_vector3(light.direction)
            );
        }
        file->store_line(String("render_mode: ") + render_mode_name(request.selection.mode));
        if (request.selection.mode == RenderMode::Pixel) {
            file->store_line(String("target_pixel: ") +
                             String::num_int64(request.selection.target_pixel.x) + "," +
                             String::num_int64(request.selection.target_pixel.y));
        } else if (request.selection.mode == RenderMode::Tile) {
            file->store_line(String("target_tile: ") +
                             String::num_int64(request.selection.target_tile.x) + "," +
                             String::num_int64(request.selection.target_tile.y));
        }
        file->store_line(String("tiles: ") + String::num_int64(outcome.tiles_done) + "/" +
                         String::num_int64(outcome.total_tiles));
        if (!outcome.error.is_empty()) {
            file->store_line(String("error: ") + outcome.error);
        }
        file->store_line(String("total_ms: ") + String::num(outcome.total_ms, 3));
        file->store_line(String("intersection_ms: ") + String::num(outcome.intersection_ms, 3));

        for (const TimingEntry& entry : outcome.timing_entries) {
            file->store_line(String(entry.name) + String("_ms: ") + String::num(entry.elapsed_ms, 3));
        }

        file->store_line("[/raytrace timing]");
        file->store_line("");
        file->flush();
        file->close();
    }

    // 准备渲染请求是同步和异步入口的共同前置阶段：
    // 先校验 Godot 侧输入，再创建输出目录、提取场景，最后归一化相机和渲染 settings。
    // 失败时直接构造对外 Dictionary，避免调用方再根据内部状态推断错误。
    bool prepare_render_request(Node* root,
                                Camera3D* camera,
                                Vector2i image_size,
                                const String& output_path,
                                int32_t samples_per_pixel,
                                int32_t max_depth,
                                int64_t seed,
                                const RenderSelection& selection,
                                TimingClock::time_point total_start,
                                RenderRequest* out_request,
                                Dictionary* out_error_result) {
        std::vector<TimingEntry> timing_entries;
        const auto validate_start = TimingClock::now();
        const int32_t normalized_spp = std::max(samples_per_pixel, 1);
        const String save_path = resolve_output_path(output_path);
        const String primary_hit_mask_path = make_primary_hit_mask_path(save_path);
        const String timing_log_path = make_timing_log_path(save_path);

        if (root == nullptr) {
            add_timing(&timing_entries, "validate_inputs", elapsed_ms(validate_start));
            if (out_error_result != nullptr) {
                *out_error_result = make_result(false, save_path, primary_hit_mask_path, "No edited scene root.",
                                                image_size, normalized_spp, timing_log_path, 0.0, selection, 0,
                                                LightStatistics(), 0, 0, 0, 0.0);
            }
            return false;
        }
        if (camera == nullptr) {
            add_timing(&timing_entries, "validate_inputs", elapsed_ms(validate_start));
            if (out_error_result != nullptr) {
                *out_error_result = make_result(false, save_path, primary_hit_mask_path,
                                                "No editor viewport camera.", image_size, normalized_spp,
                                                timing_log_path, 0.0, selection, 0,
                                                LightStatistics(), 0, 0, 0, 0.0);
            }
            return false;
        }
        if (camera->get_projection() != Camera3D::PROJECTION_PERSPECTIVE) {
            add_timing(&timing_entries, "validate_inputs", elapsed_ms(validate_start));
            if (out_error_result != nullptr) {
                *out_error_result = make_result(false, save_path, primary_hit_mask_path,
                                                "Only perspective editor cameras are supported.", image_size,
                                                normalized_spp, timing_log_path, 0.0, selection, 0,
                                                LightStatistics(), 0, 0, 0, 0.0);
            }
            return false;
        }
        if (image_size.x <= 0 || image_size.y <= 0) {
            add_timing(&timing_entries, "validate_inputs", elapsed_ms(validate_start));
            if (out_error_result != nullptr) {
                *out_error_result = make_result(false, save_path, primary_hit_mask_path,
                                                "Invalid viewport image size.", image_size, normalized_spp,
                                                timing_log_path, 0.0, selection, 0, LightStatistics(), 0, 0, 0,
                                                0.0);
            }
            return false;
        }
        if (selection.mode == RenderMode::Pixel &&
            (selection.target_pixel.x < 0 || selection.target_pixel.y < 0 ||
             selection.target_pixel.x >= image_size.x || selection.target_pixel.y >= image_size.y)) {
            add_timing(&timing_entries, "validate_inputs", elapsed_ms(validate_start));
            if (out_error_result != nullptr) {
                *out_error_result = make_result(false, save_path, primary_hit_mask_path,
                                                "Target pixel is outside the viewport image.", image_size,
                                                normalized_spp, timing_log_path, 0.0, selection, 0,
                                                LightStatistics(), 0, 0, 0, 0.0);
            }
            return false;
        }
        const Vector2i tile_count(
            ceil_div(image_size.x, TILE_SIZE),
            ceil_div(image_size.y, TILE_SIZE)
        );
        if (selection.mode == RenderMode::Tile &&
            (selection.target_tile.x < 0 || selection.target_tile.y < 0 ||
             selection.target_tile.x >= tile_count.x || selection.target_tile.y >= tile_count.y)) {
            add_timing(&timing_entries, "validate_inputs", elapsed_ms(validate_start));
            if (out_error_result != nullptr) {
                *out_error_result = make_result(false, save_path, primary_hit_mask_path,
                                                "Target tile is outside the viewport tile grid.", image_size,
                                                normalized_spp, timing_log_path, 0.0, selection, 0,
                                                LightStatistics(), 0, 0, 0, 0.0);
            }
            return false;
        }
        add_timing(&timing_entries, "validate_inputs", elapsed_ms(validate_start));

        String error;
        const auto output_dir_start = TimingClock::now();
        if (!ensure_output_dir(save_path, &error)) {
            add_timing(&timing_entries, "ensure_output_dir", elapsed_ms(output_dir_start));
            if (out_error_result != nullptr) {
                *out_error_result = make_result(false, save_path, primary_hit_mask_path, error, image_size,
                                                normalized_spp, timing_log_path, 0.0, selection, 0,
                                                LightStatistics(), 0, 0, 0, 0.0);
            }
            return false;
        }
        add_timing(&timing_entries, "ensure_output_dir", elapsed_ms(output_dir_start));

        const auto extract_scene_start = TimingClock::now();
        godot_rt::SceneExtractor extractor;
        RenderRequest request;
        request.scene = extractor.extract(root);
        add_timing(&timing_entries, "extract_scene", elapsed_ms(extract_scene_start));

        const auto prepare_settings_start = TimingClock::now();
        request.camera = make_render_camera(camera);
        request.output_postprocess_settings = capture_output_postprocess_settings(camera);
        request.selection = selection;
        request.save_path = save_path;
        request.primary_hit_mask_path = primary_hit_mask_path;
        request.timing_log_path = timing_log_path;
        request.total_start = total_start;
        request.submission_thread_id = std::this_thread::get_id();
        request.settings.image_size = image_size;
        request.settings.tile_size = Vector2i(TILE_SIZE, TILE_SIZE);
        request.settings.samples_per_pixel = selection.mode == RenderMode::Pixel ? 1 : normalized_spp;
        request.settings.max_depth = std::max(max_depth, 0);
        request.settings.seed = static_cast<std::uint64_t>(std::max<int64_t>(seed, 0));
        add_timing(&timing_entries, "prepare_camera_settings", elapsed_ms(prepare_settings_start));
        request.timing_entries = timing_entries;

        if (out_request != nullptr) {
            *out_request = request;
        }
        return true;
    }

    // 将完整图像拆成互不重叠的 tile 并行渲染。调用线程计入计算线程总数；
    // 对异步任务而言，它就是现有 job worker，因此不会在外层 worker 之外再多算一整组线程。
    unsigned int render_compute_thread_count(const RenderRequest& request) {
        if (request.selection.mode != RenderMode::Full) {
            return 1;
        }

        const int task_count = total_tile_count(request.settings.image_size);
        return static_cast<unsigned int>(std::min(
            std::max(task_count, 0),
            static_cast<int>(godot_rt::recommended_render_thread_count())
        ));
    }

    bool render_one_pass(godot_rt::CpuPathTracer& tracer,
                         const Vector2i& image_size,
                         unsigned int compute_thread_count,
                         const std::atomic_bool* cancel_requested,
                         std::atomic_int* tiles_done) {
        const int tile_columns = ceil_div(std::max(image_size.x, 0), TILE_SIZE);
        const int tile_rows = ceil_div(std::max(image_size.y, 0), TILE_SIZE);
        const std::size_t tile_count = static_cast<std::size_t>(tile_columns) *
                                       static_cast<std::size_t>(tile_rows);

        const godot_rt::CancellationCheck cancellation_check = cancel_requested == nullptr
            ? godot_rt::CancellationCheck()
            : godot_rt::CancellationCheck([cancel_requested]() {
                  return cancel_requested->load(std::memory_order_relaxed);
              });

        const godot_rt::ParallelTaskResult result = godot_rt::run_parallel_tasks(
            tile_count,
            compute_thread_count,
            [&](std::size_t tile_index) {
                // 线性任务索引与二维 tile 坐标一一对应，唯一领取即可保证不同线程不会写同一像素。
                const int tile_x = static_cast<int>(tile_index % static_cast<std::size_t>(tile_columns));
                const int tile_y = static_cast<int>(tile_index / static_cast<std::size_t>(tile_columns));

                godot_rt::Tile tile;
                tile.origin = Vector2i(tile_x * TILE_SIZE, tile_y * TILE_SIZE);
                tile.size = Vector2i(
                    std::min(TILE_SIZE, image_size.x - tile.origin.x),
                    std::min(TILE_SIZE, image_size.y - tile.origin.y)
                );
                tile.pass_index = 0;
                tracer.render_tile(tile);

                if (tiles_done != nullptr) {
                    tiles_done->fetch_add(1, std::memory_order_relaxed);
                }
            },
            cancellation_check
        );

        // 调度器返回时所有 helper 均已 join，tile 计数和 Film 不会再被后台计算线程修改。
        return !result.cancelled && result.completed_task_count == tile_count;
    }

    bool render_selected_pixel(godot_rt::CpuPathTracer& tracer,
                               const Vector2i& target_pixel,
                               const std::atomic_bool* cancel_requested,
                               std::atomic_int* tiles_done) {
        if (cancel_requested != nullptr && cancel_requested->load(std::memory_order_relaxed)) {
            return false;
        }

        if (tracer.render_pixel(target_pixel) && tiles_done != nullptr) {
            tiles_done->fetch_add(1, std::memory_order_relaxed);
        }

        return true;
    }

    bool render_selected_tile(godot_rt::CpuPathTracer& tracer,
                              const Vector2i& image_size,
                              const Vector2i& target_tile,
                              const std::atomic_bool* cancel_requested,
                              std::atomic_int* tiles_done) {
        if (cancel_requested != nullptr && cancel_requested->load(std::memory_order_relaxed)) {
            return false;
        }

        godot_rt::Tile tile;
        tile.origin = Vector2i(target_tile.x * TILE_SIZE, target_tile.y * TILE_SIZE);
        tile.size = Vector2i(
            std::min(TILE_SIZE, image_size.x - tile.origin.x),
            std::min(TILE_SIZE, image_size.y - tile.origin.y)
        );
        tile.pass_index = 0;
        tracer.render_tile(tile);

        if (tiles_done != nullptr) {
            tiles_done->fetch_add(1, std::memory_order_relaxed);
        }
        return true;
    }

    // 同步入口在提交线程执行完整渲染管线，包括计算、Image 转换、PNG 保存和 timing log。
    // 所有提前返回都会补齐 outcome，保持既有同步返回字段和失败语义。
    RenderOutcome execute_sync_render_request(const RenderRequest& request,
                                              const char* mode,
                                              const std::atomic_bool* cancel_requested,
                                              std::atomic_int* tiles_done) {
        RenderOutcome outcome = make_initial_outcome(request);
        std::vector<TimingEntry> timing_entries = request.timing_entries;
        std::atomic_int local_tiles_done{0};
        std::atomic_int* effective_tiles_done = tiles_done != nullptr ? tiles_done : &local_tiles_done;
        const unsigned int compute_thread_count = render_compute_thread_count(request);

        godot_rt::CpuPathTracer tracer;
        if (godot_rt::select_render_log_delivery(compute_thread_count, true) ==
            godot_rt::RenderLogDelivery::Direct) {
            tracer.set_log_sink([](const String& message) {
                godot_rt::Logger::info(message);
            });
        }
        const auto tracer_reset_start = TimingClock::now();
        tracer.reset(request.scene, request.camera, request.settings);
        add_timing(&timing_entries, "tracer_reset", elapsed_ms(tracer_reset_start));

        const auto build_accel_start = TimingClock::now();
        tracer.set_accel(std::make_unique<godot_rt::BruteForceAccel>());
        add_timing(&timing_entries, "build_accel", elapsed_ms(build_accel_start));

        const auto render_start = TimingClock::now();
        bool render_completed = false;
        const char* render_timing_name = "render_tiles";
        switch (request.selection.mode) {
            case RenderMode::Pixel:
                render_timing_name = "render_pixel";
                render_completed = render_selected_pixel(
                    tracer,
                    request.selection.target_pixel,
                    cancel_requested,
                    effective_tiles_done
                );
                break;
            case RenderMode::Tile:
                render_timing_name = "render_selected_tile";
                render_completed = render_selected_tile(
                    tracer,
                    request.settings.image_size,
                    request.selection.target_tile,
                    cancel_requested,
                    effective_tiles_done
                );
                break;
            case RenderMode::Full:
            default:
                render_completed = render_one_pass(
                    tracer,
                    request.settings.image_size,
                    compute_thread_count,
                    cancel_requested,
                    effective_tiles_done
                );
                break;
        }
        add_timing(
            &timing_entries,
            render_timing_name,
            elapsed_ms(render_start)
        );
        // `render_one_pass` 返回时所有 helper 均已 join；此后才读取统计和 Film，并创建 Godot `Image`。
        copy_render_statistics(tracer, &outcome);
        if (!render_completed) {
            outcome.cancelled = true;
            outcome.error = "Render cancelled.";
            outcome.tiles_done = effective_tiles_done->load(std::memory_order_relaxed);
            outcome.timing_entries = timing_entries;
            outcome.total_ms = elapsed_ms(request.total_start);
            write_timing_log(request, outcome, mode);
            return outcome;
        }
        if (cancel_requested != nullptr && cancel_requested->load(std::memory_order_relaxed)) {
            outcome.cancelled = true;
            outcome.error = "Render cancelled.";
            outcome.tiles_done = effective_tiles_done->load(std::memory_order_relaxed);
            outcome.timing_entries = timing_entries;
            outcome.total_ms = elapsed_ms(request.total_start);
            write_timing_log(request, outcome, mode);
            return outcome;
        }

        const auto film_to_image_start = TimingClock::now();
        Ref<Image> image = film_to_image(
            tracer.get_film(),
            request.settings.image_size,
            request.output_postprocess_settings
        );
        add_timing(&timing_entries, "film_to_image", elapsed_ms(film_to_image_start));
        if (image.is_null()) {
            outcome.error = "Could not create output image.";
            outcome.tiles_done = effective_tiles_done->load(std::memory_order_relaxed);
            outcome.timing_entries = timing_entries;
            outcome.total_ms = elapsed_ms(request.total_start);
            write_timing_log(request, outcome, mode);
            return outcome;
        }

        const auto save_png_start = TimingClock::now();
        const Error save_error = image->save_png(request.save_path);
        add_timing(&timing_entries, "save_png", elapsed_ms(save_png_start));
        if (save_error != OK) {
            outcome.error = "Could not save PNG.";
            outcome.tiles_done = effective_tiles_done->load(std::memory_order_relaxed);
            outcome.timing_entries = timing_entries;
            outcome.total_ms = elapsed_ms(request.total_start);
            write_timing_log(request, outcome, mode);
            return outcome;
        }

        const auto primary_hit_mask_to_image_start = TimingClock::now();
        Ref<Image> primary_hit_mask = primary_hit_mask_to_image(tracer, request.settings.image_size);
        add_timing(&timing_entries, "primary_hit_mask_to_image", elapsed_ms(primary_hit_mask_to_image_start));
        if (primary_hit_mask.is_null()) {
            outcome.error = "Could not create primary hit mask image.";
            outcome.tiles_done = effective_tiles_done->load(std::memory_order_relaxed);
            outcome.timing_entries = timing_entries;
            outcome.total_ms = elapsed_ms(request.total_start);
            write_timing_log(request, outcome, mode);
            return outcome;
        }

        const auto save_primary_hit_mask_png_start = TimingClock::now();
        const Error save_primary_hit_mask_error = primary_hit_mask->save_png(request.primary_hit_mask_path);
        add_timing(&timing_entries, "save_primary_hit_mask_png", elapsed_ms(save_primary_hit_mask_png_start));
        if (save_primary_hit_mask_error != OK) {
            outcome.error = "Could not save primary hit mask PNG.";
            outcome.tiles_done = effective_tiles_done->load(std::memory_order_relaxed);
            outcome.timing_entries = timing_entries;
            outcome.total_ms = elapsed_ms(request.total_start);
            write_timing_log(request, outcome, mode);
            return outcome;
        }

        if (request.selection.mode == RenderMode::Tile) {
            const auto tile_debug_to_image_start = TimingClock::now();
            Ref<Image> tile_debug_base = film_to_linear_clamped_debug_image(
                tracer.get_film(),
                request.settings.image_size
            );
            Ref<Image> tile_debug_image = tile_debug_overlay_to_image(
                tile_debug_base,
                request.settings.image_size,
                request.selection.target_tile
            );
            add_timing(&timing_entries, "tile_debug_to_image", elapsed_ms(tile_debug_to_image_start));
            if (tile_debug_image.is_null()) {
                outcome.error = "Could not create tile debug image.";
                outcome.tiles_done = effective_tiles_done->load(std::memory_order_relaxed);
                outcome.timing_entries = timing_entries;
                outcome.total_ms = elapsed_ms(request.total_start);
                write_timing_log(request, outcome, mode);
                return outcome;
            }

            const auto save_tile_debug_png_start = TimingClock::now();
            const Error save_tile_debug_error = tile_debug_image->save_png(
                make_tile_debug_path(request.save_path, request.selection)
            );
            add_timing(&timing_entries, "save_tile_debug_png", elapsed_ms(save_tile_debug_png_start));
            if (save_tile_debug_error != OK) {
                outcome.error = "Could not save tile debug PNG.";
                outcome.tiles_done = effective_tiles_done->load(std::memory_order_relaxed);
                outcome.timing_entries = timing_entries;
                outcome.total_ms = elapsed_ms(request.total_start);
                write_timing_log(request, outcome, mode);
                return outcome;
            }
        }

        outcome.ok = true;
        outcome.tiles_done = effective_tiles_done->load(std::memory_order_relaxed);
        outcome.timing_entries = timing_entries;
        outcome.total_ms = elapsed_ms(request.total_start);
        write_timing_log(request, outcome, mode);
        return outcome;
    }

    // 异步计算阶段仅操作渲染器自有数据，不创建 Image、FileAccess 等 Godot Object。
    std::unique_ptr<RenderComputation> compute_async_render_request(
        const RenderRequest& request,
        const std::atomic_bool* cancel_requested,
        std::atomic_int* tiles_done
    ) {
        auto computation = std::make_unique<RenderComputation>();
        computation->outcome = make_initial_outcome(request);
        computation->tracer = std::make_unique<godot_rt::CpuPathTracer>();

        std::vector<TimingEntry>& timing_entries = computation->outcome.timing_entries;
        std::atomic_int local_tiles_done{0};
        std::atomic_int* effective_tiles_done = tiles_done != nullptr ? tiles_done : &local_tiles_done;
        const unsigned int compute_thread_count = render_compute_thread_count(request);
        const godot_rt::RenderLogDelivery log_delivery = godot_rt::select_render_log_delivery(
            compute_thread_count,
            false
        );
        if (log_delivery == godot_rt::RenderLogDelivery::Deferred) {
            computation->tracer->set_log_sink([computation_ptr = computation.get()](const String& message) {
                computation_ptr->deferred_logs.push_back(message);
            });
        }

        const auto tracer_reset_start = TimingClock::now();
        computation->tracer->reset(request.scene, request.camera, request.settings);
        add_timing(&timing_entries, "tracer_reset", elapsed_ms(tracer_reset_start));

        const auto build_accel_start = TimingClock::now();
        computation->tracer->set_accel(std::make_unique<godot_rt::BruteForceAccel>());
        add_timing(&timing_entries, "build_accel", elapsed_ms(build_accel_start));

        const auto render_start = TimingClock::now();
        const char* render_timing_name = "render_tiles";
        switch (request.selection.mode) {
            case RenderMode::Pixel:
                render_timing_name = "render_pixel";
                computation->render_completed = render_selected_pixel(
                    *computation->tracer,
                    request.selection.target_pixel,
                    cancel_requested,
                    effective_tiles_done
                );
                break;
            case RenderMode::Tile:
                render_timing_name = "render_selected_tile";
                computation->render_completed = render_selected_tile(
                    *computation->tracer,
                    request.settings.image_size,
                    request.selection.target_tile,
                    cancel_requested,
                    effective_tiles_done
                );
                break;
            case RenderMode::Full:
            default:
                computation->render_completed = render_one_pass(
                    *computation->tracer,
                    request.settings.image_size,
                    compute_thread_count,
                    cancel_requested,
                    effective_tiles_done
                );
                break;
        }
        add_timing(&timing_entries, render_timing_name, elapsed_ms(render_start));

        // 所有辅助线程均已结束，清空 sink 后才允许提交线程接管计算结果。
        computation->tracer->set_log_sink({});
        copy_render_statistics(*computation->tracer, &computation->outcome);
        computation->outcome.tiles_done = effective_tiles_done->load(std::memory_order_relaxed);
        if (!computation->render_completed ||
            (cancel_requested != nullptr && cancel_requested->load(std::memory_order_relaxed))) {
            computation->outcome.cancelled = true;
            computation->outcome.error = "Render cancelled.";
        }
        return computation;
    }

    // 收尾阶段必须在提交请求的原线程执行，集中完成日志投递、Image 创建和文件写入。
    RenderOutcome finalize_async_render_computation(
        const RenderRequest& request,
        RenderComputation* computation,
        const char* mode,
        const std::atomic_bool* cancel_requested
    ) {
        RenderOutcome outcome = computation != nullptr ? computation->outcome : make_initial_outcome(request);
        if (std::this_thread::get_id() != request.submission_thread_id) {
            outcome.ok = false;
            outcome.error = "Render finalization must run on the submission thread.";
            return outcome;
        }

        if (computation == nullptr || computation->tracer == nullptr) {
            outcome.ok = false;
            outcome.error = "Render computation is unavailable.";
            outcome.total_ms = elapsed_ms(request.total_start);
            write_timing_log(request, outcome, mode);
            return outcome;
        }

        for (const String& message : computation->deferred_logs) {
            godot_rt::Logger::info(message);
        }
        computation->deferred_logs.clear();

        std::vector<TimingEntry>& timing_entries = outcome.timing_entries;
        const auto finish_outcome = [&]() -> RenderOutcome {
            outcome.total_ms = elapsed_ms(request.total_start);
            write_timing_log(request, outcome, mode);
            return outcome;
        };

        if (!computation->render_completed ||
            (cancel_requested != nullptr && cancel_requested->load(std::memory_order_relaxed))) {
            outcome.ok = false;
            outcome.cancelled = true;
            outcome.error = "Render cancelled.";
            return finish_outcome();
        }

        const godot_rt::CpuPathTracer& tracer = *computation->tracer;
        const auto film_to_image_start = TimingClock::now();
        Ref<Image> image = film_to_image(
            tracer.get_film(),
            request.settings.image_size,
            request.output_postprocess_settings
        );
        add_timing(&timing_entries, "film_to_image", elapsed_ms(film_to_image_start));
        if (image.is_null()) {
            outcome.error = "Could not create output image.";
            return finish_outcome();
        }

        const auto save_png_start = TimingClock::now();
        const Error save_error = image->save_png(request.save_path);
        add_timing(&timing_entries, "save_png", elapsed_ms(save_png_start));
        if (save_error != OK) {
            outcome.error = "Could not save PNG.";
            return finish_outcome();
        }

        const auto primary_hit_mask_to_image_start = TimingClock::now();
        Ref<Image> primary_hit_mask = primary_hit_mask_to_image(tracer, request.settings.image_size);
        add_timing(&timing_entries, "primary_hit_mask_to_image", elapsed_ms(primary_hit_mask_to_image_start));
        if (primary_hit_mask.is_null()) {
            outcome.error = "Could not create primary hit mask image.";
            return finish_outcome();
        }

        const auto save_primary_hit_mask_png_start = TimingClock::now();
        const Error save_primary_hit_mask_error = primary_hit_mask->save_png(request.primary_hit_mask_path);
        add_timing(&timing_entries, "save_primary_hit_mask_png", elapsed_ms(save_primary_hit_mask_png_start));
        if (save_primary_hit_mask_error != OK) {
            outcome.error = "Could not save primary hit mask PNG.";
            return finish_outcome();
        }

        if (request.selection.mode == RenderMode::Tile) {
            const auto tile_debug_to_image_start = TimingClock::now();
            Ref<Image> tile_debug_base = film_to_linear_clamped_debug_image(
                tracer.get_film(),
                request.settings.image_size
            );
            Ref<Image> tile_debug_image = tile_debug_overlay_to_image(
                tile_debug_base,
                request.settings.image_size,
                request.selection.target_tile
            );
            add_timing(&timing_entries, "tile_debug_to_image", elapsed_ms(tile_debug_to_image_start));
            if (tile_debug_image.is_null()) {
                outcome.error = "Could not create tile debug image.";
                return finish_outcome();
            }

            const auto save_tile_debug_png_start = TimingClock::now();
            const Error save_tile_debug_error = tile_debug_image->save_png(
                make_tile_debug_path(request.save_path, request.selection)
            );
            add_timing(&timing_entries, "save_tile_debug_png", elapsed_ms(save_tile_debug_png_start));
            if (save_tile_debug_error != OK) {
                outcome.error = "Could not save tile debug PNG.";
                return finish_outcome();
            }
        }

        outcome.ok = true;
        return finish_outcome();
    }

    // 异步入口在前置校验失败时仍返回 job 字段，方便脚本侧复用同一套状态读取逻辑。
    Dictionary make_validation_job_error(Dictionary error_result) {
        error_result["job_id"] = 0;
        error_result["exists"] = false;
        error_result["done"] = true;
        error_result["cancelled"] = false;
        error_result["progress"] = 0.0;
        return error_result;
    }

    // 从 RenderJob 生成当前轮询结果。
    // outcome 可能正在 worker 完成阶段被写入，因此复制 outcome 时需要短暂持有 result_mutex。
    Dictionary make_running_job_result(const std::shared_ptr<RenderJob>& job) {
        RenderOutcome outcome;
        {
            std::lock_guard<std::mutex> result_lock(job->result_mutex);
            outcome = job->outcome;
        }

        const bool done = job->done.load(std::memory_order_acquire);
        const bool cancel_requested = job->cancel_requested.load(std::memory_order_relaxed);
        const bool cancelled = outcome.cancelled || (cancel_requested && !outcome.ok);
        double progress = 0.0;
        if (job->total_tiles > 0) {
            progress = static_cast<double>(job->tiles_done.load(std::memory_order_relaxed)) /
                       static_cast<double>(job->total_tiles);
        }
        if (done && outcome.ok && !cancelled) {
            progress = 1.0;
        }

        return make_job_result(job->id, true, done, cancelled, clamp_progress(progress), outcome.ok,
                               outcome.path, outcome.primary_hit_mask_path, outcome.error, outcome.image_size,
                               outcome.samples_per_pixel, outcome.timing_log_path, outcome.total_ms,
                               outcome.selection, outcome.triangle_count, outcome.light_statistics,
                               outcome.primary_ray_count, outcome.primary_ray_hit_count, outcome.primary_ray_miss_count,
                               outcome.intersection_ms);
    }

    Dictionary make_selection_error_result(const Dictionary& options,
                                           const Vector2i& image_size,
                                           const RenderSelection& selection,
                                           const String& error) {
        const String save_path = resolve_output_path(get_string_option(options, "output_path", String()));
        return make_result(
            false,
            save_path,
            make_primary_hit_mask_path(save_path),
            error,
            image_size,
            std::max(get_int_option(options, "samples_per_pixel", 1), 1),
            make_timing_log_path(save_path),
            0.0,
            selection,
            0,
            LightStatistics(),
            0,
            0,
            0,
            0.0
        );
    }

    Dictionary render_scene_to_png_impl(Node* root,
                                        Camera3D* camera,
                                        Vector2i image_size,
                                        const String& output_path,
                                        int32_t samples_per_pixel,
                                        int32_t max_depth,
                                        int64_t seed,
                                        const RenderSelection& selection) {
        const auto total_start = TimingClock::now();
        RenderRequest request;
        Dictionary error_result;
        if (!prepare_render_request(root, camera, image_size, output_path, samples_per_pixel, max_depth, seed,
                                    selection, total_start, &request, &error_result)) {
            return error_result;
        }

        const RenderOutcome outcome = execute_sync_render_request(request, "sync", nullptr, nullptr);
        return make_result(outcome.ok, outcome.path, outcome.primary_hit_mask_path, outcome.error, outcome.image_size,
                           outcome.samples_per_pixel, outcome.timing_log_path, outcome.total_ms,
                           outcome.selection, outcome.triangle_count, outcome.light_statistics,
                           outcome.primary_ray_count, outcome.primary_ray_hit_count, outcome.primary_ray_miss_count,
                           outcome.intersection_ms);
    }

    Dictionary start_render_scene_to_png_impl(Node* root,
                                              Camera3D* camera,
                                              Vector2i image_size,
                                              const String& output_path,
                                              int32_t samples_per_pixel,
                                              int32_t max_depth,
                                              int64_t seed,
                                              const RenderSelection& selection) {
        const auto total_start = TimingClock::now();
        RenderRequest request;
        Dictionary error_result;
        if (!prepare_render_request(root, camera, image_size, output_path, samples_per_pixel, max_depth, seed,
                                    selection, total_start, &request, &error_result)) {
            return make_validation_job_error(error_result);
        }

        const auto start_worker_start = TimingClock::now();
        std::shared_ptr<RenderJob> job = std::make_shared<RenderJob>();
        job->id = g_next_job_id.fetch_add(1, std::memory_order_relaxed);
        job->request = request;
        job->total_tiles = request.selection.mode == RenderMode::Full
                               ? total_tile_count(request.settings.image_size)
                               : 1;

        job->worker = std::thread([job]() {
            while (!job->worker_can_start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            std::unique_ptr<RenderComputation> computation = compute_async_render_request(
                job->request,
                &job->cancel_requested,
                &job->tiles_done
            );
            {
                std::lock_guard<std::mutex> result_lock(job->result_mutex);
                job->outcome = computation->outcome;
                job->computation = std::move(computation);
            }
            job->compute_done.store(true, std::memory_order_release);
        });

        {
            std::lock_guard<std::mutex> jobs_lock(g_jobs_mutex);
            g_render_jobs[job->id] = job;
        }
        add_timing(&job->request.timing_entries, "start_worker", elapsed_ms(start_worker_start));
        job->outcome = make_initial_outcome(job->request);
        job->worker_can_start.store(true, std::memory_order_release);

        return make_job_result(job->id, true, false, false, 0.0, true, request.save_path,
                               request.primary_hit_mask_path, String(), request.settings.image_size,
                               request.settings.samples_per_pixel, request.timing_log_path, 0.0,
                               request.selection, request.scene.triangle_count(),
                               job->outcome.light_statistics, job->outcome.primary_ray_count,
                               job->outcome.primary_ray_hit_count, job->outcome.primary_ray_miss_count, 0.0);
    }

    // 调用方已经持有 g_jobs_mutex 时使用这个 helper。
    // 纯计算完成后先 join worker；仅原提交线程可以执行 Godot Object 收尾并发布最终 done 状态。
    Dictionary make_job_status_locked(const std::shared_ptr<RenderJob>& job) {
        if (job->compute_done.load(std::memory_order_acquire) && job->worker.joinable()) {
            job->worker.join();
        }

        if (job->compute_done.load(std::memory_order_acquire) &&
            !job->done.load(std::memory_order_relaxed) &&
            std::this_thread::get_id() == job->request.submission_thread_id) {
            std::unique_ptr<RenderComputation> computation;
            {
                std::lock_guard<std::mutex> result_lock(job->result_mutex);
                computation = std::move(job->computation);
            }

            const RenderOutcome outcome = finalize_async_render_computation(
                job->request,
                computation.get(),
                "async",
                &job->cancel_requested
            );
            {
                std::lock_guard<std::mutex> result_lock(job->result_mutex);
                job->outcome = outcome;
            }
            job->done.store(true, std::memory_order_release);
        }

        return make_running_job_result(job);
    }

} // namespace

// 绑定给 Godot 的静态方法保持薄包装：参数、默认值和返回 Dictionary 形状都在这里固定。
void RayTraceExporter::_bind_methods() {
    ClassDB::bind_static_method(
        "RayTraceExporter",
        D_METHOD("render_scene_to_png", "root", "camera", "image_size", "output_path",
                 "samples_per_pixel", "max_depth", "seed"),
        &RayTraceExporter::render_scene_to_png,
        DEFVAL(String()),
        DEFVAL(1),
        DEFVAL(2),
        DEFVAL(1)
    );
    ClassDB::bind_static_method(
        "RayTraceExporter",
        D_METHOD("render_scene_to_png_with_options", "root", "camera", "image_size", "options"),
        &RayTraceExporter::render_scene_to_png_with_options
    );
    ClassDB::bind_static_method(
        "RayTraceExporter",
        D_METHOD("start_render_scene_to_png", "root", "camera", "image_size", "output_path",
                 "samples_per_pixel", "max_depth", "seed"),
        &RayTraceExporter::start_render_scene_to_png,
        DEFVAL(String()),
        DEFVAL(1),
        DEFVAL(2),
        DEFVAL(1)
    );
    ClassDB::bind_static_method(
        "RayTraceExporter",
        D_METHOD("start_render_scene_to_png_with_options", "root", "camera", "image_size", "options"),
        &RayTraceExporter::start_render_scene_to_png_with_options
    );
    ClassDB::bind_static_method(
        "RayTraceExporter",
        D_METHOD("poll_render_job", "job_id"),
        &RayTraceExporter::poll_render_job
    );
    ClassDB::bind_static_method(
        "RayTraceExporter",
        D_METHOD("cancel_render_job", "job_id"),
        &RayTraceExporter::cancel_render_job
    );
    ClassDB::bind_static_method(
        "RayTraceExporter",
        D_METHOD("release_render_job", "job_id"),
        &RayTraceExporter::release_render_job
    );
}

// 同步导出当前场景到 PNG。
// 这个路径会在调用线程完成全部工作，适合简单脚本调用或不需要进度 UI 的场景。
Dictionary RayTraceExporter::render_scene_to_png(Node* root,
                                                 Camera3D* camera,
                                                 Vector2i image_size,
                                                 const String& output_path,
                                                 int32_t samples_per_pixel,
                                                 int32_t max_depth,
                                                 int64_t seed) {
    return render_scene_to_png_impl(
        root,
        camera,
        image_size,
        output_path,
        samples_per_pixel,
        max_depth,
        seed,
        RenderSelection()
    );
}

// GDScript 的 ClassDB.class_call_static 最多只能传 5 个方法参数。
// options 包装入口把导出参数收进 Dictionary，避免脚本侧因为参数数量超过上限而无法调用。
Dictionary RayTraceExporter::render_scene_to_png_with_options(Node* root,
                                                              Camera3D* camera,
                                                              Vector2i image_size,
                                                              const Dictionary& options) {
    RenderSelection selection;
    String error;
    if (!parse_render_selection(options, &selection, &error)) {
        return make_selection_error_result(options, image_size, selection, error);
    }

    return render_scene_to_png_impl(
        root,
        camera,
        image_size,
        get_string_option(options, "output_path", String()),
        get_int_option(options, "samples_per_pixel", 1),
        get_int_option(options, "max_depth", 2),
        get_int64_option(options, "seed", 1),
        selection
    );
}

// 启动后台渲染任务并立即返回 job_id。
// worker 创建后先等待 worker_can_start，确保主线程已经把 job 放进全局表并初始化 outcome。
Dictionary RayTraceExporter::start_render_scene_to_png(Node* root,
                                                       Camera3D* camera,
                                                       Vector2i image_size,
                                                       const String& output_path,
                                                       int32_t samples_per_pixel,
                                                       int32_t max_depth,
                                                       int64_t seed) {
    return start_render_scene_to_png_impl(
        root,
        camera,
        image_size,
        output_path,
        samples_per_pixel,
        max_depth,
        seed,
        RenderSelection()
    );
}

// 供编辑器插件使用的低参数包装入口；实际渲染流程仍委托给原有 start_render_scene_to_png。
Dictionary RayTraceExporter::start_render_scene_to_png_with_options(Node* root,
                                                                    Camera3D* camera,
                                                                    Vector2i image_size,
                                                                    const Dictionary& options) {
    RenderSelection selection;
    String error;
    if (!parse_render_selection(options, &selection, &error)) {
        return make_validation_job_error(make_selection_error_result(options, image_size, selection, error));
    }

    return start_render_scene_to_png_impl(
        root,
        camera,
        image_size,
        get_string_option(options, "output_path", String()),
        get_int_option(options, "samples_per_pixel", 1),
        get_int_option(options, "max_depth", 2),
        get_int64_option(options, "seed", 1),
        selection
    );
}

// 查询后台任务状态。
// jobs_lock 保护任务表查找；单个 job 的 outcome 在 helper 中再用 result_mutex 保护。
Dictionary RayTraceExporter::poll_render_job(int64_t job_id) {
    std::lock_guard<std::mutex> jobs_lock(g_jobs_mutex);
    const auto it = g_render_jobs.find(job_id);
    if (it == g_render_jobs.end()) {
        return make_missing_job_result(job_id);
    }

    return make_job_status_locked(it->second);
}

// 请求取消后台任务。
// 取消是协作式的：这里只设置原子标记，worker 会在下一个 tile 前观察并提前结束。
Dictionary RayTraceExporter::cancel_render_job(int64_t job_id) {
    std::lock_guard<std::mutex> jobs_lock(g_jobs_mutex);
    const auto it = g_render_jobs.find(job_id);
    if (it == g_render_jobs.end()) {
        return make_missing_job_result(job_id);
    }

    it->second->cancel_requested.store(true, std::memory_order_relaxed);
    return make_job_status_locked(it->second);
}

// 释放已完成的后台任务。
// 仍在运行的任务不会被移除，避免丢失 worker 和取消入口；完成后先从表里摘除，再在锁外 join。
Dictionary RayTraceExporter::release_render_job(int64_t job_id) {
    std::shared_ptr<RenderJob> job;
    {
        std::lock_guard<std::mutex> jobs_lock(g_jobs_mutex);
        const auto it = g_render_jobs.find(job_id);
        if (it == g_render_jobs.end()) {
            return make_missing_job_result(job_id);
        }

        if (!it->second->done.load(std::memory_order_acquire)) {
            Dictionary result = make_running_job_result(it->second);
            result["error"] = "Render job is still running.";
            return result;
        }

        job = it->second;
        g_render_jobs.erase(it);
    }

    if (job->worker.joinable()) {
        job->worker.join();
    }

    Dictionary result;
    result["ok"] = true;
    result["released"] = true;
    result["job_id"] = job_id;
    result["primary_hit_mask_path"] = job->outcome.primary_hit_mask_path;
    result["tile_debug_path"] = make_tile_debug_path(job->outcome.path, job->outcome.selection);
    result["timing_log_path"] = job->outcome.timing_log_path;
    result["total_ms"] = job->outcome.total_ms;
    result["render_mode"] = String(render_mode_name(job->outcome.selection.mode));
    result["target_pixel"] = job->outcome.selection.target_pixel;
    result["target_tile"] = job->outcome.selection.target_tile;
    result["triangle_count"] = job->outcome.triangle_count;
    add_light_statistics_to_result(result, job->outcome.light_statistics);
    add_primary_ray_statistics_to_result(result, job->outcome.primary_ray_count,
                                         job->outcome.primary_ray_hit_count,
                                         job->outcome.primary_ray_miss_count);
    result["intersection_ms"] = job->outcome.intersection_ms;
    return result;
}

// 扩展卸载或退出时清理所有后台任务。
// 先在锁内请求取消并复制 shared_ptr，再清空任务表；随后在锁外 join，避免阻塞期间卡住 poll/cancel。
void RayTraceExporter::shutdown_render_jobs() {
    std::vector<std::shared_ptr<RenderJob>> jobs;
    {
        std::lock_guard<std::mutex> jobs_lock(g_jobs_mutex);
        jobs.reserve(g_render_jobs.size());
        for (const auto& entry : g_render_jobs) {
            entry.second->cancel_requested.store(true, std::memory_order_relaxed);
            jobs.push_back(entry.second);
        }
        g_render_jobs.clear();
    }

    for (const std::shared_ptr<RenderJob>& job : jobs) {
        if (job->worker.joinable()) {
            job->worker.join();
        }
    }
}
