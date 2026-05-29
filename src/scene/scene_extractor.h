#ifndef GDEXTENSION_CPP_EXAMPLE_SCENE_EXTRACTOR_H
#define GDEXTENSION_CPP_EXAMPLE_SCENE_EXTRACTOR_H

#include <godot_cpp/classes/node.hpp>

#include "rt_scene.h"

namespace godot {
    class MeshInstance3D;
}

namespace godot_rt {

    class SceneExtractor {
    public:
        Scene extract(godot::Node* root) const;

    private:
        void extract_node(godot::Node* node, Scene& scene) const;
        void extract_mesh_instance(godot::MeshInstance3D* mesh_instance, Scene& scene) const;
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_SCENE_EXTRACTOR_H
