#ifndef GDEXTENSION_CPP_EXAMPLE_RENDER_EXECUTION_POLICY_H
#define GDEXTENSION_CPP_EXAMPLE_RENDER_EXECUTION_POLICY_H

namespace godot_rt {

    enum class RenderLogDelivery {
        Disabled,
        Direct,
        Deferred,
    };

    // 多线程渲染完全禁用日志；单线程仅在提交线程直接输出，其他线程延迟到提交线程输出。
    RenderLogDelivery select_render_log_delivery(
        unsigned int compute_thread_count,
        bool on_submission_thread
    ) noexcept;

}

#endif // GDEXTENSION_CPP_EXAMPLE_RENDER_EXECUTION_POLICY_H
