#ifndef GDEXTENSION_CPP_EXAMPLE_SCENE_EXTRACTOR_H
#define GDEXTENSION_CPP_EXAMPLE_SCENE_EXTRACTOR_H

#include <godot_cpp/classes/node.hpp>

#include "rt_scene.h"

namespace godot {
    class Camera3D;
    class MeshInstance3D;
}

namespace godot_rt {

    struct ExtractedScene {
        Scene scene;
        bool has_camera = false;
        Camera camera;
    };

    class SceneExtractor {
    public:
        Scene extract(godot::Node* root) const;
        ExtractedScene extract_with_camera(godot::Node* root) const;

    private:
        struct CameraSearch {
            bool has_camera = false;
            bool has_current_camera = false;
            Camera camera;
        };

        void extract_node(godot::Node* node, Scene& scene, CameraSearch* camera_search) const;
        void extract_mesh_instance(godot::MeshInstance3D* mesh_instance, Scene& scene) const;
        void extract_camera(godot::Camera3D* godot_camera, CameraSearch& camera_search) const;
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_SCENE_EXTRACTOR_H
