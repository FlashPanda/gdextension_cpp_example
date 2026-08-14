#include "parallel_task_scheduler.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <new>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <process.h>
#else
#include <pthread.h>
#endif

namespace godot_rt {
namespace {

    struct ThreadEntry {
        std::function<void()> callback;
    };

#ifdef _WIN32
    struct NativeThreadState {
        HANDLE handle = nullptr;
    };

    unsigned int __stdcall run_thread_entry(void* raw_entry) {
        std::unique_ptr<ThreadEntry> entry(static_cast<ThreadEntry*>(raw_entry));
        entry->callback();
        return 0;
    }
#else
    struct NativeThreadState {
        pthread_t handle{};
    };

    void* run_thread_entry(void* raw_entry) {
        std::unique_ptr<ThreadEntry> entry(static_cast<ThreadEntry*>(raw_entry));
        entry->callback();
        return nullptr;
    }
#endif

}

JoinableThread::~JoinableThread() {
    join();
}

JoinableThread::JoinableThread(JoinableThread&& other) noexcept : state(other.state) {
    other.state = nullptr;
}

JoinableThread& JoinableThread::operator=(JoinableThread&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    join();
    state = other.state;
    other.state = nullptr;
    return *this;
}

bool JoinableThread::start(std::function<void()> entry, JoinableThread* out_thread) noexcept {
    if (out_thread == nullptr || out_thread->joinable() || !entry) {
        return false;
    }

    NativeThreadState* new_state = new (std::nothrow) NativeThreadState();
    ThreadEntry* new_entry = new (std::nothrow) ThreadEntry{std::move(entry)};
    if (new_state == nullptr || new_entry == nullptr) {
        delete new_state;
        delete new_entry;
        return false;
    }

    // 平台 API 创建成功后，线程入口接管 `new_entry` 的唯一所有权；失败时仍由当前线程清理。
#ifdef _WIN32
    const uintptr_t handle = _beginthreadex(nullptr, 0, run_thread_entry, new_entry, 0, nullptr);
    if (handle == 0) {
        delete new_state;
        delete new_entry;
        return false;
    }
    new_state->handle = reinterpret_cast<HANDLE>(handle);
#else
    if (pthread_create(&new_state->handle, nullptr, run_thread_entry, new_entry) != 0) {
        delete new_state;
        delete new_entry;
        return false;
    }
#endif

    out_thread->state = new_state;
    return true;
}

bool JoinableThread::joinable() const noexcept {
    return state != nullptr;
}

void JoinableThread::join() noexcept {
    if (state == nullptr) {
        return;
    }

    NativeThreadState* native_state = static_cast<NativeThreadState*>(state);
#ifdef _WIN32
    WaitForSingleObject(native_state->handle, INFINITE);
    CloseHandle(native_state->handle);
#else
    pthread_join(native_state->handle, nullptr);
#endif
    delete native_state;
    state = nullptr;
}

unsigned int resolve_render_thread_count(unsigned int hardware_concurrency) noexcept {
    return hardware_concurrency > 1 ? hardware_concurrency - 1 : 1;
}

unsigned int recommended_render_thread_count() noexcept {
    return resolve_render_thread_count(std::thread::hardware_concurrency());
}

ParallelTaskResult run_parallel_tasks(
    std::size_t task_count,
    unsigned int requested_thread_count,
    const ParallelTask& task,
    const CancellationCheck& cancellation_check,
    const ThreadLauncher& thread_launcher
) {
    ParallelTaskResult result;
    if (task_count == 0) {
        result.cancelled = cancellation_check && cancellation_check();
        return result;
    }
    if (!task) {
        return result;
    }
    if (cancellation_check && cancellation_check()) {
        result.cancelled = true;
        return result;
    }

    const unsigned int normalized_thread_count = std::max(requested_thread_count, 1u);
    const unsigned int target_thread_count = static_cast<unsigned int>(std::min<std::size_t>(
        normalized_thread_count,
        task_count
    ));

    // 原子递增让每个任务索引最多只会被一个线程领取；索引可乱序执行，但不会重复或重叠。
    std::atomic_size_t next_task_index{0};
    std::atomic_size_t completed_task_count{0};
    std::atomic_bool stop_requested{false};
    std::atomic_bool cancellation_observed{false};

    const auto should_cancel = [&]() {
        if (!cancellation_check || !cancellation_check()) {
            return false;
        }

        cancellation_observed.store(true, std::memory_order_relaxed);
        stop_requested.store(true, std::memory_order_release);
        return true;
    };

    const auto worker = [&]() {
        while (!stop_requested.load(std::memory_order_acquire)) {
            if (should_cancel()) {
                break;
            }

            const std::size_t task_index = next_task_index.fetch_add(1, std::memory_order_relaxed);
            if (task_index >= task_count) {
                break;
            }

            // 取消可能恰好发生在索引领取之后；执行回调前再检查一次，避免开始新的 tile。
            if (should_cancel()) {
                break;
            }

            task(task_index);
            completed_task_count.fetch_add(1, std::memory_order_relaxed);
        }
    };

    const ThreadLauncher launch_thread = thread_launcher
        ? thread_launcher
        : ThreadLauncher([](std::function<void()> entry, JoinableThread* out_thread) {
              return JoinableThread::start(std::move(entry), out_thread);
          });

    std::vector<JoinableThread> helper_threads;
    helper_threads.reserve(target_thread_count > 0 ? target_thread_count - 1 : 0);
    for (unsigned int index = 1; index < target_thread_count; ++index) {
        JoinableThread helper;
        if (!launch_thread(worker, &helper) || !helper.joinable()) {
            // 部分创建失败时保留已启动 helper，由调用线程共同完成剩余任务。
            result.thread_creation_failed = true;
            break;
        }
        helper_threads.push_back(std::move(helper));
    }

    // 已成功启动的 helper 与调用线程执行同一领取循环；调用线程本身也占一个计算线程名额。
    result.compute_thread_count = static_cast<unsigned int>(helper_threads.size()) + 1;
    worker();

    // 无论任务完成、取消还是部分创建失败，返回前都要回收所有已启动线程及其平台句柄。
    for (JoinableThread& helper : helper_threads) {
        if (helper.joinable()) {
            helper.join();
        }
    }

    // 到达这里时所有 helper 都已 join，计数和任务写入结果不再被后台线程修改。
    result.completed_task_count = completed_task_count.load(std::memory_order_relaxed);
    result.cancelled = cancellation_observed.load(std::memory_order_relaxed);

    return result;
}

}
