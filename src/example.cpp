#include "example.h"

#include <godot_cpp/classes/render_scene_buffers.hpp>
#include <godot_cpp/classes/render_data.hpp>

namespace godot {
MyPostProcess::MyPostProcess(){
	set_effect_callback_type(CompositorEffect::EFFECT_CALLBACK_TYPE_POST_TRANSPARENT);

	rd = RenderingServer::get_singleton()->get_rendering_device();

	// call on render thread
	RenderingServer::get_singleton()->call_on_render_thread(Callable(this, "_initialize_compute"));
}

void MyPostProcess::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_initialize_compute"), &MyPostProcess::_initialize_compute);
	ClassDB::bind_method(D_METHOD("_free_compute"), &MyPostProcess::_free_compute);

	// _notification / _render_callback 是虚函数，不需要 bind 也能工作
	// 但 bind 了也没坏处（便于调试/从脚本调用）。
	ClassDB::bind_method(D_METHOD("_notification", "what"), &MyPostProcess::_notification);
	ClassDB::bind_method(D_METHOD("_render_callback", "effect_callback_type", "render_data"),
			&MyPostProcess::_render_callback);
}

void MyPostProcess::_notification(int p_what) {
	if (p_what == NOTIFICATION_PREDELETE) {
		// 注意：RenderingDevice::free_rid 需要在 render thread 调用
		// GDScript 示例里直接 free，有些情况下会触发“只能在渲染线程 free”的报错
		// 这里严格丢回渲染线程执行。
		if (shader.is_valid()) {
			RenderingServer::get_singleton()->call_on_render_thread(Callable(this, "_free_compute"));
		}
	}
}

void MyPostProcess::_free_compute() {
	// 运行在渲染线程
	rd = RenderingServer::get_singleton()->get_rendering_device();
	if (!rd) {
		return;
	}

	if (shader.is_valid()) {
		// Freeing shader will also free dependents such as pipeline.
		rd->free_rid(shader);
		shader = RID();
		pipeline = RID();
	}
}

void MyPostProcess::_initialize_compute() {
	// 运行在渲染线程
	rd = RenderingServer::get_singleton()->get_rendering_device();
	if (!rd) {
		return;
	}

	Ref<RDShaderFile> shader_file = ResourceLoader::get_singleton()->load("res://post_process_grayscale.glsl");
	if (shader_file.is_null()){
		return;
	}

	Ref<RDShaderSPIRV> shader_spirv = shader_file->get_spirv();
	if (shader_spirv.is_null()){
		return;
	}

	shader = rd->shader_create_from_spirv(shader_spirv);

	// pipeline
	if (shader.is_valid()) {
		pipeline = rd->compute_pipeline_create(shader);
	}
}

void MyPostProcess::_render_callback(int p_type, const RenderData* p_render_data) {
	print_line(" MyPostProcess::_render_callback called");
}

}