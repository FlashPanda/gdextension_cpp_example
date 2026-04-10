#include "mesh.h"
#include <godot_cpp/core/error_macros.hpp>
#include "buffer_cache.h"
#include <sstream>

namespace godot
{
    //STAT_RATIO("Geometry/Triangles per mesh", nTris, nTriMeshes);
    //STAT_MEMORY_COUNTER("Memory/Triangles", triangleBytes);
    TriangleMesh::TriangleMesh(const Transform3D& p_render_from_object, bool p_reverse_orientation,
                     std::vector<int> p_vertex_indices, std::vector<Vector3> p,
                     std::vector<Vector3> s, std::vector<Vector3> n,
                     std::vector<Vector2> uv, std::vector<int> face_indices)
    {
        DEV_ASSERT(p_vertex_indices.size() % 3 == 0);

        n_triangles = p_vertex_indices.size() / 3;
        n_vertices = p.size();
        //++nTriMeshes;
        //nTris += nTriangles;
        //triangleBytes += sizeof(*this);

        vertex_indices = int_buffer_cache->lookup_or_add({p_vertex_indices.data(), p_vertex_indices.size()});

		// Transform mesh vertices to rendering space and initialize mesh _p_
		for (Vector3& pt : p)
			pt = p_render_from_object.xform(pt);

		this->p = point3_buffer_cache ->lookup_or_add({p.data(), p.size()});

		this->reverse_orientation = p_reverse_orientation;
		this->transform_swaps_handedness = p_render_from_object.basis.determinant() < 0;

        if (!uv.empty()) {
            DEV_ASSERT(uv.size() == n_vertices);
            this->uv = point2_buffer_cache->lookup_or_add({uv.data(), uv.size()});
        }

        if (!n.empty()) {
            DEV_ASSERT(n.size() == n_vertices);
            for (Vector3& nn : n){
                nn = p_render_from_object.basis.xform(nn);
                if (reverse_orientation)
                    nn = -nn;
            }
            this->n = normal3_buffer_cache->lookup_or_add({n.data(), n.size()});
        }

        if (!s.empty()) {
            DEV_ASSERT(s.size() == n_vertices);
            for (Vector3& ss : s){
                ss = p_render_from_object.basis.xform(ss);
            }
            this->s = vector3_buffer_cache->lookup_or_add({s.data(), s.size()});
        }

        if (!face_indices.empty()) {
            DEV_ASSERT(face_indices.size() == n_triangles * 3);
            this->face_indices = int_buffer_cache->lookup_or_add({face_indices.data(), face_indices.size()});
        }
    }

    std::string TriangleMesh::to_string() const {
        std::ostringstream oss;
        oss << "[ TriangleMesh "
            << "reverse_orientation: " << (reverse_orientation ? "true" : "false")
            << " transform_swaps_handedness: " << (transform_swaps_handedness ? "true" : "false")
            << " n_triangles: " << n_triangles
            << " n_vertices: " << n_vertices
            << " ]";

        return oss.str();
    }
}