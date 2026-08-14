#include "render_execution_policy.h"

namespace godot_rt {

RenderLogDelivery select_render_log_delivery(
    unsigned int compute_thread_count,
    bool on_submission_thread
) noexcept {
    if (compute_thread_count == 0 || compute_thread_count > 1) {
        return RenderLogDelivery::Disabled;
    }

    return on_submission_thread ? RenderLogDelivery::Direct : RenderLogDelivery::Deferred;
}

}
