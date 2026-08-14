#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include "render/parallel_task_scheduler.h"
#include "render/render_execution_policy.h"
#include "render/render_statistics.h"

namespace {

bool expect_true(bool condition, const char* description) {
    if (condition) {
        return true;
    }

    std::cerr << "FAILED: " << description << "\n";
    return false;
}

template <typename T>
bool expect_equal(const T& actual, const T& expected, const char* description) {
    if (actual == expected) {
        return true;
    }

    std::cerr << "FAILED: " << description
              << " (expected " << expected << ", got " << actual << ")\n";
    return false;
}

bool test_thread_count_policy() {
    bool passed = true;
    passed &= expect_equal(
        godot_rt::resolve_render_thread_count(0),
        1u,
        "unknown hardware concurrency falls back to one thread"
    );
    passed &= expect_equal(
        godot_rt::resolve_render_thread_count(1),
        1u,
        "one hardware thread never underflows to zero"
    );
    passed &= expect_equal(
        godot_rt::resolve_render_thread_count(2),
        1u,
        "two hardware threads reserve one for the system"
    );
    passed &= expect_equal(
        godot_rt::resolve_render_thread_count(8),
        7u,
        "N hardware threads use N minus one render threads"
    );

    const unsigned int detected = std::thread::hardware_concurrency();
    passed &= expect_equal(
        godot_rt::recommended_render_thread_count(),
        godot_rt::resolve_render_thread_count(detected),
        "recommended thread count reads the host hardware concurrency"
    );
    return passed;
}

bool test_render_log_delivery_policy() {
    using godot_rt::RenderLogDelivery;

    bool passed = true;
    passed &= expect_true(
        godot_rt::select_render_log_delivery(0, true) == RenderLogDelivery::Disabled,
        "zero compute threads disable render logging"
    );
    passed &= expect_true(
        godot_rt::select_render_log_delivery(1, true) == RenderLogDelivery::Direct,
        "one compute thread on the submission thread logs directly"
    );
    passed &= expect_true(
        godot_rt::select_render_log_delivery(1, false) == RenderLogDelivery::Deferred,
        "one asynchronous compute thread defers logs to the submission thread"
    );
    passed &= expect_true(
        godot_rt::select_render_log_delivery(2, true) == RenderLogDelivery::Disabled,
        "two compute threads disable render logging"
    );
    passed &= expect_true(
        godot_rt::select_render_log_delivery(19, false) == RenderLogDelivery::Disabled,
        "asynchronous multithreaded rendering disables logging"
    );
    return passed;
}

bool test_unique_bounded_parallel_execution() {
    constexpr std::size_t task_count = 12;
    std::vector<int> executions(task_count, 0);
    std::mutex executions_mutex;
    std::set<std::thread::id> thread_ids;
    std::mutex thread_ids_mutex;

    std::mutex gate_mutex;
    std::condition_variable gate_changed;
    int arrived = 0;
    bool release_gate = false;

    const godot_rt::ParallelTaskResult result = godot_rt::run_parallel_tasks(
        task_count,
        3,
        [&](std::size_t task_index) {
            {
                std::lock_guard<std::mutex> lock(executions_mutex);
                ++executions[task_index];
            }
            {
                std::lock_guard<std::mutex> lock(thread_ids_mutex);
                thread_ids.insert(std::this_thread::get_id());
            }

            std::unique_lock<std::mutex> lock(gate_mutex);
            ++arrived;
            if (arrived >= 3) {
                release_gate = true;
                gate_changed.notify_all();
            } else {
                gate_changed.wait_for(
                    lock,
                    std::chrono::seconds(2),
                    [&]() { return release_gate; }
                );
            }
        }
    );

    bool passed = true;
    passed &= expect_equal(result.completed_task_count, task_count, "all tasks complete");
    passed &= expect_equal(result.compute_thread_count, 3u, "caller plus two helpers form three compute threads");
    passed &= expect_true(!result.cancelled, "completed execution is not cancelled");
    passed &= expect_true(!result.thread_creation_failed, "normal execution creates every helper");
    passed &= expect_equal(thread_ids.size(), static_cast<std::size_t>(3), "three threads execute concurrently");
    for (std::size_t index = 0; index < executions.size(); ++index) {
        passed &= expect_equal(executions[index], 1, "each task is claimed exactly once");
    }
    return passed;
}

bool test_task_count_caps_threads() {
    std::atomic_int completed{0};
    const godot_rt::ParallelTaskResult result = godot_rt::run_parallel_tasks(
        2,
        32,
        [&](std::size_t) { completed.fetch_add(1, std::memory_order_relaxed); }
    );

    bool passed = true;
    passed &= expect_equal(result.compute_thread_count, 2u, "task count caps compute threads");
    passed &= expect_equal(result.completed_task_count, static_cast<std::size_t>(2), "both capped tasks complete");
    passed &= expect_equal(completed.load(std::memory_order_relaxed), 2, "callback runs for both tasks");
    return passed;
}

bool test_cancellation_stops_new_claims() {
    std::atomic_bool cancel_requested{false};
    std::vector<std::size_t> executed;

    const godot_rt::ParallelTaskResult result = godot_rt::run_parallel_tasks(
        8,
        1,
        [&](std::size_t task_index) {
            executed.push_back(task_index);
            cancel_requested.store(true, std::memory_order_release);
        },
        [&]() { return cancel_requested.load(std::memory_order_acquire); }
    );

    bool passed = true;
    passed &= expect_true(result.cancelled, "cancellation is reported");
    passed &= expect_equal(result.completed_task_count, static_cast<std::size_t>(1), "only the claimed task completes");
    passed &= expect_equal(executed.size(), static_cast<std::size_t>(1), "no task is claimed after cancellation");
    return passed;
}

bool test_partial_thread_creation_failure_is_joined_and_recovers() {
    std::atomic_int launch_attempts{0};
    std::atomic_int helper_exits{0};
    godot_rt::ThreadLauncher launcher = [&helper_exits, &launch_attempts](
        std::function<void()> entry,
        godot_rt::JoinableThread* out_thread
    ) {
        const int attempt = launch_attempts.fetch_add(1, std::memory_order_relaxed);
        if (attempt == 1) {
            return false;
        }

        return godot_rt::JoinableThread::start(
            [entry = std::move(entry), &helper_exits]() mutable {
                entry();
                helper_exits.fetch_add(1, std::memory_order_relaxed);
            },
            out_thread
        );
    };

    std::atomic_int completed{0};
    const godot_rt::ParallelTaskResult result = godot_rt::run_parallel_tasks(
        32,
        4,
        [&](std::size_t) { completed.fetch_add(1, std::memory_order_relaxed); },
        {},
        launcher
    );

    bool passed = true;
    passed &= expect_true(result.thread_creation_failed, "partial thread creation failure is reported");
    passed &= expect_equal(result.compute_thread_count, 2u, "one helper plus the caller continue rendering");
    passed &= expect_equal(result.completed_task_count, static_cast<std::size_t>(32), "remaining threads finish every task");
    passed &= expect_equal(completed.load(std::memory_order_relaxed), 32, "no work is lost after launch failure");
    passed &= expect_equal(helper_exits.load(std::memory_order_relaxed), 1, "created helper exits before return");
    return passed;
}

bool test_parallel_output_matches_serial_reference() {
    constexpr std::size_t tile_count = 19;
    constexpr std::size_t values_per_tile = 37;
    constexpr std::size_t value_count = tile_count * values_per_tile;
    std::vector<unsigned int> serial(value_count, 0);
    std::vector<unsigned int> parallel(value_count, 0);

    const auto render_tile = [=](std::vector<unsigned int>* output, std::size_t tile_index) {
        const std::size_t begin = tile_index * values_per_tile;
        const std::size_t end = begin + values_per_tile;
        for (std::size_t index = begin; index < end; ++index) {
            output->at(index) = static_cast<unsigned int>((index * 2654435761u) ^ (tile_index * 97u) ^ 0x5a17u);
        }
    };

    for (std::size_t tile_index = 0; tile_index < tile_count; ++tile_index) {
        render_tile(&serial, tile_index);
    }
    const godot_rt::ParallelTaskResult result = godot_rt::run_parallel_tasks(
        tile_count,
        6,
        [&](std::size_t tile_index) { render_tile(&parallel, tile_index); }
    );

    bool passed = true;
    passed &= expect_equal(result.completed_task_count, tile_count, "deterministic parallel render completes");
    passed &= expect_true(parallel == serial, "parallel disjoint-tile output matches serial reference exactly");
    return passed;
}

bool test_render_statistics_merge() {
    godot_rt::RenderStatistics total;
    const godot_rt::RenderStatistics first{1.25, 12, 7, 5};
    const godot_rt::RenderStatistics second{2.75, 20, 11, 9};

    godot_rt::merge_render_statistics(&total, first);
    godot_rt::merge_render_statistics(&total, second);

    bool passed = true;
    passed &= expect_equal(total.intersection_ms, 4.0, "tile intersection times merge once per tile");
    passed &= expect_equal(total.primary_ray_count, static_cast<std::int64_t>(32), "primary ray counts merge");
    passed &= expect_equal(total.primary_ray_hit_count, static_cast<std::int64_t>(18), "hit counts merge");
    passed &= expect_equal(total.primary_ray_miss_count, static_cast<std::int64_t>(14), "miss counts merge");
    return passed;
}

} // namespace

int main() {
    bool passed = true;
    passed &= test_thread_count_policy();
    passed &= test_render_log_delivery_policy();
    passed &= test_unique_bounded_parallel_execution();
    passed &= test_task_count_caps_threads();
    passed &= test_cancellation_stops_new_claims();
    passed &= test_partial_thread_creation_failure_is_joined_and_recovers();
    passed &= test_parallel_output_matches_serial_reference();
    passed &= test_render_statistics_merge();
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
