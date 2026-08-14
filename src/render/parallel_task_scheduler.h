#ifndef GDEXTENSION_CPP_EXAMPLE_PARALLEL_TASK_SCHEDULER_H
#define GDEXTENSION_CPP_EXAMPLE_PARALLEL_TASK_SCHEDULER_H

#include <cstddef>
#include <functional>

namespace godot_rt {

    using ParallelTask = std::function<void(std::size_t)>;
    using CancellationCheck = std::function<bool()>;

    // std::thread 创建失败通过异常报告，而项目的生产构建不启用异常展开。
    // 该轻量句柄使用平台线程 API 返回显式成功/失败，并保证析构前完成 join。
    class JoinableThread {
    public:
        JoinableThread() = default;
        ~JoinableThread();
        JoinableThread(const JoinableThread&) = delete;
        JoinableThread& operator=(const JoinableThread&) = delete;
        JoinableThread(JoinableThread&& other) noexcept;
        JoinableThread& operator=(JoinableThread&& other) noexcept;

        static bool start(std::function<void()> entry, JoinableThread* out_thread) noexcept;
        bool joinable() const noexcept;
        void join() noexcept;

    private:
        void* state = nullptr;
    };

    using ThreadLauncher = std::function<bool(std::function<void()>, JoinableThread*)>;

    struct ParallelTaskResult {
        std::size_t completed_task_count = 0;
        unsigned int compute_thread_count = 0;
        bool cancelled = false;
        bool thread_creation_failed = false;
    };

    // 本机逻辑线程数为 0 或 1 时至少保留一个计算线程；大于 1 时减一，给系统和编辑器留出一个逻辑线程。
    unsigned int resolve_render_thread_count(unsigned int hardware_concurrency) noexcept;
    unsigned int recommended_render_thread_count() noexcept;

    // 调用线程始终参与计算并计入 `compute_thread_count`，任务数会进一步限制实际线程数。
    // 函数返回前会 join 全部已启动的辅助线程，因此调用方随后可以安全读取任务写入的结果。
    ParallelTaskResult run_parallel_tasks(
        std::size_t task_count,
        unsigned int requested_thread_count,
        const ParallelTask& task,
        const CancellationCheck& cancellation_check = {},
        const ThreadLauncher& thread_launcher = {}
    );

}

#endif // GDEXTENSION_CPP_EXAMPLE_PARALLEL_TASK_SCHEDULER_H
