#include "scene_extractor.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace godot_rt {
namespace {

    Material extract_material(const godot::Ref<godot::Material>& godot_material) {
        Material material;

        if (godot_material.is_null()) {
            return material;
        }

        const godot::BaseMaterial3D* base_material = godot::Object::cast_to<godot::BaseMaterial3D>(godot_material.ptr());
        if (base_material == nullptr) {
            return material;
        }

        material.albedo = base_material->get_albedo();
        material.roughness = base_material->get_roughness();

        if (base_material->get_feature(godot::BaseMaterial3D::FEATURE_EMISSION)) {
            material.emission = base_material->get_emission() * base_material->get_emission_energy_multiplier();
            if (material.emission.get_luminance() > 0.0f) {
                material.type = MaterialType::Emissive;
            }
        }

        return material;
    }

    bool is_triangle_surface(const godot::Ref<godot::Mesh>& mesh, int32_t surface_index) {
        const godot::ArrayMesh* array_mesh = godot::Object::cast_to<godot::ArrayMesh>(mesh.ptr());
        if (array_mesh == nullptr) {
            return true;
        }

        return array_mesh->surface_get_primitive_type(surface_index) == godot::Mesh::PRIMITIVE_TRIANGLES;
    }

    godot::Vector3 fallback_normal(const godot::Vector3& p0, const godot::Vector3& p1, const godot::Vector3& p2) {
        godot::Vector3 normal = (p1 - p0).cross(p2 - p0);
        if (normal.length_squared() == 0.0f) {
            return godot::Vector3(0.0f, 1.0f, 0.0f);
        }
        return normal.normalized();
    }

    godot::Vector3 transform_normal(const godot::Basis& normal_basis, const godot::Vector3& normal,
                                    const godot::Vector3& fallback) {
        godot::Vector3 transformed = normal_basis.xform(normal);
        if (transformed.length_squared() == 0.0f) {
            return fallback;
        }
        return transformed.normalized();
    }

    bool get_vertex_index(const godot::PackedInt32Array& indices, int64_t index_position, int64_t vertex_count,
                          int64_t& out_index) {
        const int32_t index = indices[index_position];
        if (index < 0 || index >= vertex_count) {
            return false;
        }

        out_index = index;
        return true;
    }

    void add_triangle(Scene& scene,
                      const godot::Transform3D& world_from_mesh,
                      const godot::Basis& normal_basis,
                      const godot::PackedVector3Array& vertices,
                      const godot::PackedVector3Array& normals,
                      const godot::PackedVector2Array& uvs,
                      bool has_normals,
                      bool has_uvs,
                      int64_t i0,
                      int64_t i1,
                      int64_t i2,
                      int material_id) {
        Triangle triangle;
        triangle.p0 = world_from_mesh.xform(vertices[i0]);
        triangle.p1 = world_from_mesh.xform(vertices[i1]);
        triangle.p2 = world_from_mesh.xform(vertices[i2]);
        triangle.materialId = material_id;

        const godot::Vector3 generated_normal = fallback_normal(triangle.p0, triangle.p1, triangle.p2);

        if (has_normals) {
            triangle.n0 = transform_normal(normal_basis, normals[i0], generated_normal);
            triangle.n1 = transform_normal(normal_basis, normals[i1], generated_normal);
            triangle.n2 = transform_normal(normal_basis, normals[i2], generated_normal);
        } else {
            triangle.n0 = generated_normal;
            triangle.n1 = generated_normal;
            triangle.n2 = generated_normal;
        }

        if (has_uvs) {
            triangle.uv0 = uvs[i0];
            triangle.uv1 = uvs[i1];
            triangle.uv2 = uvs[i2];
        }

        scene.add_triangle(triangle);
    }

}

Scene SceneExtractor::extract(godot::Node* root) const {
    Scene scene;
    extract_node(root, scene, nullptr);
    return scene;
}

ExtractedScene SceneExtractor::extract_with_camera(godot::Node* root) const {
    ExtractedScene extracted_scene;
    CameraSearch camera_search;
    extract_node(root, extracted_scene.scene, &camera_search);

    extracted_scene.has_camera = camera_search.has_camera;
    extracted_scene.camera = camera_search.camera;
    return extracted_scene;
}

void SceneExtractor::extract_node(godot::Node* node, Scene& scene, CameraSearch* camera_search) const {
    if (node == nullptr) {
        return;
    }

    godot::MeshInstance3D* mesh_instance = godot::Object::cast_to<godot::MeshInstance3D>(node);
    if (mesh_instance != nullptr) {
        extract_mesh_instance(mesh_instance, scene);
    }

    if (camera_search != nullptr) {
        godot::Camera3D* godot_camera = godot::Object::cast_to<godot::Camera3D>(node);
        if (godot_camera != nullptr) {
            extract_camera(godot_camera, *camera_search);
        }
    }

    const int32_t child_count = node->get_child_count();
    for (int32_t i = 0; i < child_count; ++i) {
        extract_node(node->get_child(i), scene, camera_search);
    }
}

void SceneExtractor::extract_mesh_instance(godot::MeshInstance3D* mesh_instance, Scene& scene) const {
    godot::Ref<godot::Mesh> mesh = mesh_instance->get_mesh();
    if (mesh.is_null()) {
        return;
    }

    const godot::Transform3D world_from_mesh = mesh_instance->get_global_transform();
    godot::Basis normal_basis = world_from_mesh.basis.inverse();
    normal_basis.transpose();

    const int32_t surface_count = mesh->get_surface_count();
    for (int32_t surface_index = 0; surface_index < surface_count; ++surface_index) {
        if (!is_triangle_surface(mesh, surface_index)) {
            continue;
        }

        godot::Array arrays = mesh->surface_get_arrays(surface_index);
        if (arrays.size() <= godot::Mesh::ARRAY_VERTEX ||
            arrays[godot::Mesh::ARRAY_VERTEX].get_type() != godot::Variant::PACKED_VECTOR3_ARRAY) {
            continue;
        }

        const godot::PackedVector3Array vertices = arrays[godot::Mesh::ARRAY_VERTEX];
        if (vertices.size() < 3) {
            continue;
        }

        godot::PackedInt32Array indices;
        if (arrays.size() > godot::Mesh::ARRAY_INDEX &&
            arrays[godot::Mesh::ARRAY_INDEX].get_type() == godot::Variant::PACKED_INT32_ARRAY) {
            indices = arrays[godot::Mesh::ARRAY_INDEX];
        }

        godot::PackedVector3Array normals;
        bool has_normals = false;
        if (arrays.size() > godot::Mesh::ARRAY_NORMAL &&
            arrays[godot::Mesh::ARRAY_NORMAL].get_type() == godot::Variant::PACKED_VECTOR3_ARRAY) {
            normals = arrays[godot::Mesh::ARRAY_NORMAL];
            has_normals = normals.size() >= vertices.size();
        }

        godot::PackedVector2Array uvs;
        bool has_uvs = false;
        if (arrays.size() > godot::Mesh::ARRAY_TEX_UV &&
            arrays[godot::Mesh::ARRAY_TEX_UV].get_type() == godot::Variant::PACKED_VECTOR2_ARRAY) {
            uvs = arrays[godot::Mesh::ARRAY_TEX_UV];
            has_uvs = uvs.size() >= vertices.size();
        }

        godot::Ref<godot::Material> godot_material = mesh_instance->get_active_material(surface_index);
        if (godot_material.is_null()) {
            godot_material = mesh->surface_get_material(surface_index);
        }
        const int material_id = scene.add_material(extract_material(godot_material));

        if (indices.size() > 0) {
            for (int64_t i = 0; i + 2 < indices.size(); i += 3) {
                int64_t i0 = 0;
                int64_t i1 = 0;
                int64_t i2 = 0;
                if (!get_vertex_index(indices, i, vertices.size(), i0) ||
                    !get_vertex_index(indices, i + 1, vertices.size(), i1) ||
                    !get_vertex_index(indices, i + 2, vertices.size(), i2)) {
                    continue;
                }

                add_triangle(scene, world_from_mesh, normal_basis, vertices, normals, uvs, has_normals, has_uvs,
                             i0, i1, i2, material_id);
            }
        } else {
            for (int64_t i = 0; i + 2 < vertices.size(); i += 3) {
                add_triangle(scene, world_from_mesh, normal_basis, vertices, normals, uvs, has_normals, has_uvs,
                             i, i + 1, i + 2, material_id);
            }
        }
    }
}

void SceneExtractor::extract_camera(godot::Camera3D* godot_camera, CameraSearch& camera_search) const {
    if (godot_camera == nullptr ||
        godot_camera->get_projection() != godot::Camera3D::PROJECTION_PERSPECTIVE) {
        return;
    }

    const bool is_current = godot_camera->is_current();
    if (camera_search.has_current_camera || (camera_search.has_camera && !is_current)) {
        return;
    }

    Camera camera;
    camera.camera_to_world = godot_camera->get_camera_transform();
    camera.fov_y_radians = godot::Math::deg_to_rad(godot_camera->get_fov());
    camera.fov_axis = godot_camera->get_keep_aspect_mode() == godot::Camera3D::KEEP_WIDTH
                          ? CameraFovAxis::Horizontal
                          : CameraFovAxis::Vertical;

    camera_search.camera = camera;
    camera_search.has_camera = true;
    camera_search.has_current_camera = is_current;
}

}
