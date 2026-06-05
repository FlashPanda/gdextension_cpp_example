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
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/color.hpp>

#include "accel/brute_force_accel.h"
#include "render/cpu_path_tracer.h"
#include "render/film.h"
#include "render/frame_accumulator.h"
#include "scene/scene_extractor.h"

using namespace godot;

namespace {

    // 本文件的匿名命名空间集中放置 RayTraceExporter 的私有实现：
    // 包括渲染请求/结果的数据快照、同步与异步共用的 helper、耗时日志、
    // 以及跨 Godot 调用保存的后台渲染任务表。这里的符号不暴露给 GDExtension API。
    constexpr int TILE_SIZE = 16;
    constexpr const char DEFAULT_OUTPUT_DIR[] = "res://raytrace_output";
    constexpr const char DEFAULT_OUTPUT_PREFIX[] = "current_scene";
    constexpr const char LEGACY_DEFAULT_OUTPUT_PATH[] = "res://raytrace_output/current_scene.png";
    using TimingClock = std::chrono::steady_clock;

    // 单个阶段的耗时记录。name 使用字符串常量，避免在渲染热路径中额外分配。
    struct TimingEntry {
        const char* name = "";
        double elapsed_ms = 0.0;
    };

    // RenderRequest 是一次渲染提交时的完整快照。
    // 场景、相机、settings 和输出路径在主线程准备好后传入渲染流程；
    // 异步渲染时 worker 只读取这份快照，避免继续依赖 Godot 场景树的实时状态。
    struct RenderRequest {
        godot_rt::Scene scene;
        godot_rt::Camera camera;
        godot_rt::CpuPathTracerSettings settings;
        String save_path;
        String timing_log_path;
        TimingClock::time_point total_start;
        std::vector<TimingEntry> timing_entries;
    };

    // RenderOutcome 记录渲染结束或提前失败后的结果。
    // 它同时服务同步返回值、异步轮询状态和 timing log，因此保留了路径、
    // 图像参数、tile 进度、错误信息和各阶段耗时。
    struct RenderOutcome {
        bool ok = false;
        bool cancelled = false;
        String path;
        String timing_log_path;
        String error;
        Vector2i image_size = Vector2i(0, 0);
        int32_t samples_per_pixel = 1;
        int32_t max_depth = 0;
        int tiles_done = 0;
        int total_tiles = 0;
        double total_ms = 0.0;
        std::vector<TimingEntry> timing_entries;
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
        std::atomic_bool done{false};
        std::atomic_int tiles_done{0};
        int total_tiles = 0;
        std::mutex result_mutex;
        RenderOutcome outcome;
    };

    // 全局任务表保存仍可被 poll/cancel/release 访问的后台任务。
    // g_jobs_mutex 保护任务表结构，g_timing_log_mutex 避免多个渲染同时追加同一个日志文件。
    std::mutex g_jobs_mutex;
    std::mutex g_timing_log_mutex;
    std::unordered_map<int64_t, std::shared_ptr<RenderJob>> g_render_jobs;
    std::atomic<int64_t> g_next_job_id{1};
    std::atomic<int64_t> g_next_output_sequence{1};

    // 统一生成返回给 GDScript/编辑器侧的基础结果字典。
    // 字段名保持稳定，调用方可以用 ok/error 判断失败，用 path/timing_log_path 找到产物。
    Dictionary make_result(bool ok,
                           const String& path,
                           const String& error,
                           const Vector2i& image_size,
                           int32_t samples_per_pixel,
                           const String& timing_log_path) {
        Dictionary result;
        result["ok"] = ok;
        result["path"] = path;
        result["timing_log_path"] = timing_log_path;
        result["error"] = error;
        result["width"] = image_size.x;
        result["height"] = image_size.y;
        result["samples_per_pixel"] = samples_per_pixel;
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
                               const String& error,
                               const Vector2i& image_size,
                               int32_t samples_per_pixel,
                               const String& timing_log_path) {
        Dictionary result = make_result(ok, path, error, image_size, samples_per_pixel, timing_log_path);
        result["job_id"] = job_id;
        result["exists"] = exists;
        result["done"] = done;
        result["cancelled"] = cancelled;
        result["progress"] = progress;
        return result;
    }

    // 所有找不到 job 的路径都返回同一种状况，减少脚本侧分支处理。
    Dictionary make_missing_job_result(int64_t job_id) {
        return make_job_result(job_id, false, true, false, 0.0, false, String(),
                               "Render job not found.", Vector2i(0, 0), 1, String());
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
        if (output_path.is_empty() || output_path == String(LEGACY_DEFAULT_OUTPUT_PATH)) {
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

    // 将 Film 中的线性 radiance 均值写入 RGBA8 Image。
    // 当前导出路径只做 0..1 clamp，不在这里引入 tone mapping，以免改变 Film 的原始累积语义。
    Ref<Image> film_to_image(const godot_rt::Film& film, const Vector2i& image_size) {
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

    // 初始 outcome 用于异步任务刚创建、尚未完成时的轮询结果。
    // 它复制请求中的静态信息，让 UI 能在 worker 产出最终结果前显示尺寸、路径和总 tile 数。
    RenderOutcome make_initial_outcome(const RenderRequest& request) {
        RenderOutcome outcome;
        outcome.path = request.save_path;
        outcome.timing_log_path = request.timing_log_path;
        outcome.image_size = request.settings.image_size;
        outcome.samples_per_pixel = request.settings.samples_per_pixel;
        outcome.max_depth = request.settings.max_depth;
        outcome.total_tiles = total_tile_count(request.settings.image_size);
        outcome.timing_entries = request.timing_entries;
        return outcome;
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
        file->store_line(String("timing_log: ") + request.timing_log_path);
        file->store_line(String("image_size: ") + String::num_int64(request.settings.image_size.x) + "x" +
                         String::num_int64(request.settings.image_size.y));
        file->store_line(String("samples_per_pixel: ") + String::num_int64(request.settings.samples_per_pixel));
        file->store_line(String("max_depth: ") + String::num_int64(request.settings.max_depth));
        file->store_line(String("tiles: ") + String::num_int64(outcome.tiles_done) + "/" +
                         String::num_int64(outcome.total_tiles));
        if (!outcome.error.is_empty()) {
            file->store_line(String("error: ") + outcome.error);
        }
        file->store_line(String("total_ms: ") + String::num(outcome.total_ms, 3));

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
                                TimingClock::time_point total_start,
                                RenderRequest* out_request,
                                Dictionary* out_error_result) {
        std::vector<TimingEntry> timing_entries;
        const auto validate_start = TimingClock::now();
        const int32_t normalized_spp = std::max(samples_per_pixel, 1);
        const String save_path = resolve_output_path(output_path);
        const String timing_log_path = make_timing_log_path(save_path);

        if (root == nullptr) {
            add_timing(&timing_entries, "validate_inputs", elapsed_ms(validate_start));
            if (out_error_result != nullptr) {
                *out_error_result = make_result(false, save_path, "No edited scene root.", image_size, normalized_spp,
                                                timing_log_path);
            }
            return false;
        }
        if (camera == nullptr) {
            add_timing(&timing_entries, "validate_inputs", elapsed_ms(validate_start));
            if (out_error_result != nullptr) {
                *out_error_result = make_result(false, save_path, "No editor viewport camera.", image_size,
                                                normalized_spp, timing_log_path);
            }
            return false;
        }
        if (camera->get_projection() != Camera3D::PROJECTION_PERSPECTIVE) {
            add_timing(&timing_entries, "validate_inputs", elapsed_ms(validate_start));
            if (out_error_result != nullptr) {
                *out_error_result = make_result(false, save_path, "Only perspective editor cameras are supported.",
                                                image_size, normalized_spp, timing_log_path);
            }
            return false;
        }
        if (image_size.x <= 0 || image_size.y <= 0) {
            add_timing(&timing_entries, "validate_inputs", elapsed_ms(validate_start));
            if (out_error_result != nullptr) {
                *out_error_result = make_result(false, save_path, "Invalid viewport image size.",
                                                image_size, normalized_spp, timing_log_path);
            }
            return false;
        }
        add_timing(&timing_entries, "validate_inputs", elapsed_ms(validate_start));

        String error;
        const auto output_dir_start = TimingClock::now();
        if (!ensure_output_dir(save_path, &error)) {
            add_timing(&timing_entries, "ensure_output_dir", elapsed_ms(output_dir_start));
            if (out_error_result != nullptr) {
                *out_error_result = make_result(false, save_path, error, image_size, normalized_spp, timing_log_path);
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
        request.save_path = save_path;
        request.timing_log_path = timing_log_path;
        request.total_start = total_start;
        request.settings.image_size = image_size;
        request.settings.tile_size = Vector2i(TILE_SIZE, TILE_SIZE);
        request.settings.samples_per_pixel = normalized_spp;
        request.settings.max_depth = std::max(max_depth, 0);
        request.settings.seed = static_cast<std::uint64_t>(std::max<int64_t>(seed, 0));
        add_timing(&timing_entries, "prepare_camera_settings", elapsed_ms(prepare_settings_start));
        request.timing_entries = timing_entries;

        if (out_request != nullptr) {
            *out_request = request;
        }
        return true;
    }

    // 以固定 TILE_SIZE 扫描整张图并逐块渲染。
    // 取消检查放在每个 tile 前，让后台渲染可以在较短时间内响应 cancel_render_job。
    bool render_one_pass(godot_rt::CpuPathTracer& tracer,
                         const Vector2i& image_size,
                         const std::atomic_bool* cancel_requested,
                         std::atomic_int* tiles_done) {
        for (int y = 0; y < image_size.y; y += TILE_SIZE) {
            for (int x = 0; x < image_size.x; x += TILE_SIZE) {
                if (cancel_requested != nullptr && cancel_requested->load(std::memory_order_relaxed)) {
                    return false;
                }

                godot_rt::Tile tile;
                tile.origin = Vector2i(x, y);
                tile.size = Vector2i(
                    std::min(TILE_SIZE, image_size.x - x),
                    std::min(TILE_SIZE, image_size.y - y)
                );
                tile.pass_index = 0;
                tracer.render_tile(tile);

                if (tiles_done != nullptr) {
                    tiles_done->fetch_add(1, std::memory_order_relaxed);
                }
            }
        }

        return true;
    }

    // 执行真正的渲染管线：reset tracer、设置加速结构、渲染 tile、把 Film 转 Image 并保存 PNG。
    // 所有提前返回都会补齐 outcome、写 timing log，保证同步和异步路径的失败信息一致。
    RenderOutcome execute_render_request(const RenderRequest& request,
                                         const char* mode,
                                         const std::atomic_bool* cancel_requested,
                                         std::atomic_int* tiles_done) {
        RenderOutcome outcome = make_initial_outcome(request);
        std::vector<TimingEntry> timing_entries = request.timing_entries;
        std::atomic_int local_tiles_done{0};
        std::atomic_int* effective_tiles_done = tiles_done != nullptr ? tiles_done : &local_tiles_done;

        godot_rt::CpuPathTracer tracer;
        const auto tracer_reset_start = TimingClock::now();
        tracer.reset(request.scene, request.camera, request.settings);
        add_timing(&timing_entries, "tracer_reset", elapsed_ms(tracer_reset_start));

        const auto build_accel_start = TimingClock::now();
        tracer.set_accel(std::make_unique<godot_rt::BruteForceAccel>());
        add_timing(&timing_entries, "build_accel", elapsed_ms(build_accel_start));

        const auto render_tiles_start = TimingClock::now();
        if (!render_one_pass(tracer, request.settings.image_size, cancel_requested, effective_tiles_done)) {
            add_timing(&timing_entries, "render_tiles", elapsed_ms(render_tiles_start));
            outcome.cancelled = true;
            outcome.error = "Render cancelled.";
            outcome.tiles_done = effective_tiles_done->load(std::memory_order_relaxed);
            outcome.timing_entries = timing_entries;
            outcome.total_ms = elapsed_ms(request.total_start);
            write_timing_log(request, outcome, mode);
            return outcome;
        }
        add_timing(&timing_entries, "render_tiles", elapsed_ms(render_tiles_start));
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
        Ref<Image> image = film_to_image(tracer.get_film(), request.settings.image_size);
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

        outcome.ok = true;
        outcome.tiles_done = effective_tiles_done->load(std::memory_order_relaxed);
        outcome.timing_entries = timing_entries;
        outcome.total_ms = elapsed_ms(request.total_start);
        write_timing_log(request, outcome, mode);
        return outcome;
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
                               outcome.path, outcome.error, outcome.image_size, outcome.samples_per_pixel,
                               outcome.timing_log_path);
    }

    // 调用方已经持有 g_jobs_mutex 时使用这个 helper。
    // 如果 worker 已结束，则在这里 join，避免线程对象在任务仍保留时长期处于 joinable 状态。
    Dictionary make_job_status_locked(const std::shared_ptr<RenderJob>& job) {
        if (job->done.load(std::memory_order_acquire) && job->worker.joinable()) {
            job->worker.join();
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
    const auto total_start = TimingClock::now();
    RenderRequest request;
    Dictionary error_result;
    if (!prepare_render_request(root, camera, image_size, output_path, samples_per_pixel, max_depth, seed,
                                total_start, &request, &error_result)) {
        return error_result;
    }

    const RenderOutcome outcome = execute_render_request(request, "sync", nullptr, nullptr);
    return make_result(outcome.ok, outcome.path, outcome.error, outcome.image_size, outcome.samples_per_pixel,
                       outcome.timing_log_path);
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
    const auto total_start = TimingClock::now();
    RenderRequest request;
    Dictionary error_result;
    if (!prepare_render_request(root, camera, image_size, output_path, samples_per_pixel, max_depth, seed,
                                total_start, &request, &error_result)) {
        return make_validation_job_error(error_result);
    }

    const auto start_worker_start = TimingClock::now();
    std::shared_ptr<RenderJob> job = std::make_shared<RenderJob>();
    job->id = g_next_job_id.fetch_add(1, std::memory_order_relaxed);
    job->request = request;
    job->total_tiles = total_tile_count(request.settings.image_size);

    job->worker = std::thread([job]() {
        // 等主线程完成任务登记后再开始渲染，避免极快失败时 poll/release 看不到 job。
        while (!job->worker_can_start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        const RenderOutcome outcome = execute_render_request(job->request, "async",
                                                             &job->cancel_requested, &job->tiles_done);
        {
            std::lock_guard<std::mutex> result_lock(job->result_mutex);
            job->outcome = outcome;
        }
        job->done.store(true, std::memory_order_release);
    });

    {
        std::lock_guard<std::mutex> jobs_lock(g_jobs_mutex);
        g_render_jobs[job->id] = job;
    }
    add_timing(&job->request.timing_entries, "start_worker", elapsed_ms(start_worker_start));
    job->outcome = make_initial_outcome(job->request);
    job->worker_can_start.store(true, std::memory_order_release);

    return make_job_result(job->id, true, false, false, 0.0, true, request.save_path, String(),
                           request.settings.image_size, request.settings.samples_per_pixel, request.timing_log_path);
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
    result["timing_log_path"] = job->outcome.timing_log_path;
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
