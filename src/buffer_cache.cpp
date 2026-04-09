#include "buffer_cache.h"

#include <godot_cpp/core/error_macros.hpp>

namespace godot
{
BufferCache<int> *int_buffer_cache;
BufferCache<Vector2> *point2_buffer_cache;
BufferCache<Vector3> *point3_buffer_cache;
BufferCache<Vector3> *vector3_buffer_cache;
BufferCache<Vector3> *normal3_buffer_cache;

void init_buffer_caches() {
    DEV_ASSERT(int_buffer_cache == nullptr);
    int_buffer_cache = new BufferCache<int>;
    point2_buffer_cache = new BufferCache<Vector2>;
    point3_buffer_cache = new BufferCache<Vector3>;
    vector3_buffer_cache = new BufferCache<Vector3>;
    normal3_buffer_cache = new BufferCache<Vector3>;
}
}