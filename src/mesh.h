
#ifndef GDEXTENSION_CPP_EXAMPLE_MESH_H
#define GDEXTENSION_CPP_EXAMPLE_MESH_H

#include <vector>
#include <string>

#include<godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace godot
{
    class TriangleMesh {
    public:
        TriangleMesh(const Transform3D& p_render_from_object, bool p_reverse_orientation,
                     std::vector<int> p_vertex_indices, std::vector<Vector3> p,
                     std::vector<Vector3> S, std::vector<Vector3> N,
                     std::vector<Vector2> uv, std::vector<int> face_indices);

        std::string to_string() const;

        static void init();

        int n_triangles, n_vertices;
        const int* vertex_indices = nullptr;
        const Vector3 *p = nullptr;
        const Vector3 *n = nullptr;
        const Vector3 *s = nullptr;
        const Vector2 *uv = nullptr;
        const int *face_indices = nullptr;
        bool reverse_orientation, transform_swaps_handedness;
    };
}

#endif //GDEXTENSION_CPP_EXAMPLE_MESH_H