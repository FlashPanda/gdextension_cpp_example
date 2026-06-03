//
// Created by caizh on 2026/5/22.
//

#ifndef GDEXTENSION_CPP_EXAMPLE_INDEX_RT_SCENE_H
#define GDEXTENSION_CPP_EXAMPLE_INDEX_RT_SCENE_H

#include <vector>

#include "../core/rt_light.h"
#include "../core/rt_types.h"

namespace godot_rt {

    class Scene {
    public:
        void clear() {
            triangles.clear();
            materials.clear();
            lights.clear();
        }

        int add_triangle(const Triangle& triangle) {
            triangles.push_back(triangle);
            return static_cast<int>(triangles.size() - 1);
        }

        int add_material(const Material& material) {
            materials.push_back(material);
            return static_cast<int>(materials.size() - 1);
        }

        int add_light(const Light& light) {
            lights.push_back(light);
            return static_cast<int>(lights.size() - 1);
        }

        const std::vector<Triangle>& get_triangles() const {
            return triangles;
        }

        const std::vector<Material>& get_materials() const {
            return materials;
        }

        const std::vector<Light>& get_lights() const {
            return lights;
        }

        int triangle_count() const {
            return static_cast<int>(triangles.size());
        }

        int material_count() const {
            return static_cast<int>(materials.size());
        }

        int light_count() const {
            return static_cast<int>(lights.size());
        }

    private:
        std::vector<Triangle> triangles;
        std::vector<Material> materials;
        std::vector<Light> lights;
    };

}

#endif //GDEXTENSION_CPP_EXAMPLE_INDEX_RT_SCENE_H
