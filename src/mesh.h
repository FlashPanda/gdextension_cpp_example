
#ifndef GDEXTENSION_CPP_EXAMPLE_MESH_H
#define GDEXTENSION_CPP_EXAMPLE_MESH_H

#include<godot/variant/transform3d.hpp>

namespace godot
{
    class TriangleMesh {
    public:
        TriangleMesh(const Transform3D& p_render_from_object, bool p_reverse_orientation,
                     std::vector<int> p_vertex_indices, std::vector<Vector3> p,
                     std::vector<Vector3> S, std::vector<Vector3> N,
                     std::vector<Vector2> uv, std::vector<int> faceIndices, Allocator alloc);

        std::string ToString() const;

        bool WritePLY(std::string filename) const;

        static void Init(Allocator alloc);

        int nTriangles, nVertices;
        const int *vertexIndices = nullptr;
        const Point3f *p = nullptr;
        const Normal3f *n = nullptr;
        const Vector3f *s = nullptr;
        const Point2f *uv = nullptr;
        const int *faceIndices = nullptr;
        bool reverseOrientation, transformSwapsHandedness;
    };
}


#endif //GDEXTENSION_CPP_EXAMPLE_MESH_H