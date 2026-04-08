#include "mesh.h"
#include <godot_cpp/core/error_macros.hpp>


namespace godot
{
    //STAT_RATIO("Geometry/Triangles per mesh", nTris, nTriMeshes);
    //STAT_MEMORY_COUNTER("Memory/Triangles", triangleBytes);
    TriangleMesh::TriangleMesh(const Transform3D& p_render_from_object, bool p_reverse_orientation,
                     std::vector<int> p_vertex_indices, std::vector<Vector3> p,
                     std::vector<Vector3> S, std::vector<Vector3> N,
                     std::vector<Vector2> uv, std::vector<int> face_indices)
    {
        DEV_ASSERT(p_vertex_indices.size() % 3 == 0);

        n_triangles = p_vertex_indices.size() / 3;
        n_vertices = p.size();
        //++nTriMeshes;
        //nTris += nTriangles;
        //triangleBytes += sizeof(*this);

        //vertexIndices = intBufferCache->lookup_or_add(indices);
    }
}