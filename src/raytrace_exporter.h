#pragma once

#include <cstdint>

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2i.hpp>

namespace godot {

    class RayTraceExporter : public Object {
        GDCLASS(RayTraceExporter, Object)

    protected:
        static void _bind_methods();

    public:
        static Dictionary render_scene_to_png(
            Node* root,
            Camera3D* camera,
            Vector2i image_size,
            const String& output_path,
            int32_t samples_per_pixel,
            int32_t max_depth,
            int64_t seed
        );
    };

} // namespace godot
