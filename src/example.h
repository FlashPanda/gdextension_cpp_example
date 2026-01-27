#ifndef POST_PROCESS_GRAYSCALE_H
#define POST_PROCESS_GRAYSCALE_H
#pragma once

#include <godot_cpp/classes/compositor_effect.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/rd_shader_file.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/uniform_set_cache_rd.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/render_data.hpp>

#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/array.hpp>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/binder_common.hpp>

namespace godot {

	class MyPostProcess : public CompositorEffect {
		GDCLASS(MyPostProcess, CompositorEffect);

	private:
		RenderingDevice* rd = nullptr;
		RID shader;
		RID pipeline;

		// Render-thread only: compile shader + create pipeline.
		void _initialize_compute();

		// Render-thread only: free shader/pipeline.
		void _free_compute();

	protected:
		static void _bind_methods();

	public:
		MyPostProcess();
		~MyPostProcess() override = default;

		void _notification(int p_what);

		// Called from the rendering thread every frame.
		void _render_callback(int p_effect_callback_type, const RenderData* p_render_data);
	};

} // namespace godot
#endif