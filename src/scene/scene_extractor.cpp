#include "scene_extractor.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/light3d.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/spot_light3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <unordered_map>

namespace godot_rt {

struct SceneExtractionCache {
    std::unordered_map<const godot::Material*, int> material_ids;
    std::unordered_map<const godot::Texture2D*, MaterialTexture> texture_snapshots;
    int default_material_id = -1;
};

namespace {

    MaterialTextureChannel convert_texture_channel(godot::BaseMaterial3D::TextureChannel channel) {
        switch (channel) {
            case godot::BaseMaterial3D::TEXTURE_CHANNEL_RED:
                return MaterialTextureChannel::Red;
            case godot::BaseMaterial3D::TEXTURE_CHANNEL_GREEN:
                return MaterialTextureChannel::Green;
            case godot::BaseMaterial3D::TEXTURE_CHANNEL_BLUE:
                return MaterialTextureChannel::Blue;
            case godot::BaseMaterial3D::TEXTURE_CHANNEL_ALPHA:
                return MaterialTextureChannel::Alpha;
            case godot::BaseMaterial3D::TEXTURE_CHANNEL_GRAYSCALE:
                return MaterialTextureChannel::Grayscale;
        }

        return MaterialTextureChannel::Red;
    }

    MaterialTexture snapshot_texture(const godot::Ref<godot::Texture2D>& texture) {
        MaterialTexture snapshot;
        if (texture.is_null()) {
            return snapshot;
        }

        godot::Ref<godot::Image> image = texture->get_image();
        if (image.is_null()) {
            return snapshot;
        }
        if (image->is_compressed()) {
            image->decompress();
            if (image->is_compressed()) {
                return snapshot;
            }
        }

        const int width = image->get_width();
        const int height = image->get_height();
        if (width <= 0 || height <= 0) {
            return snapshot;
        }

        auto pixels = std::make_shared<std::vector<godot::Color>>(
            static_cast<std::size_t>(width * height)
        );

        // 异步渲染只读取这份像素快照，避免 worker 线程继续访问 Godot 资源对象。
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                (*pixels)[static_cast<std::size_t>(y * width + x)] = image->get_pixel(x, y);
            }
        }

        snapshot.width = width;
        snapshot.height = height;
        snapshot.pixels = pixels;
        return snapshot;
    }

    MaterialTexture snapshot_texture_cached(const godot::Ref<godot::Texture2D>& texture,
                                            SceneExtractionCache& cache) {
        if (texture.is_null()) {
            return MaterialTexture();
        }

        const godot::Texture2D* texture_key = texture.ptr();
        if (texture_key == nullptr) {
            return MaterialTexture();
        }

        const auto found = cache.texture_snapshots.find(texture_key);
        if (found != cache.texture_snapshots.end()) {
            return found->second;
        }

        MaterialTexture snapshot = snapshot_texture(texture);
        const auto inserted = cache.texture_snapshots.emplace(texture_key, snapshot);
        return inserted.first->second;
    }

    MaterialTexture snapshot_material_texture(const godot::BaseMaterial3D* material,
                                              godot::BaseMaterial3D::TextureParam texture_param,
                                              SceneExtractionCache& cache) {
        if (material == nullptr) {
            return MaterialTexture();
        }

        return snapshot_texture_cached(material->get_texture(texture_param), cache);
    }

    Material extract_material(const godot::Ref<godot::Material>& godot_material,
                              SceneExtractionCache& cache) {
        Material material;

        if (godot_material.is_null()) {
            return material;
        }

        const godot::BaseMaterial3D* base_material = godot::Object::cast_to<godot::BaseMaterial3D>(godot_material.ptr());
        if (base_material == nullptr) {
            return material;
        }

        material.albedo = base_material->get_albedo();
        material.metallic = base_material->get_metallic();
        material.roughness = base_material->get_roughness();
        material.specular = base_material->get_specular();
        if (base_material->get_specular_mode() == godot::BaseMaterial3D::SPECULAR_DISABLED) {
            material.specular = 0.0f;
        }

        material.albedo_texture = snapshot_material_texture(base_material, godot::BaseMaterial3D::TEXTURE_ALBEDO, cache);
        material.metallic_texture = snapshot_material_texture(base_material, godot::BaseMaterial3D::TEXTURE_METALLIC, cache);
        material.roughness_texture = snapshot_material_texture(base_material, godot::BaseMaterial3D::TEXTURE_ROUGHNESS, cache);
        material.orm_texture = snapshot_material_texture(base_material, godot::BaseMaterial3D::TEXTURE_ORM, cache);
        material.metallic_texture_channel = convert_texture_channel(base_material->get_metallic_texture_channel());
        material.roughness_texture_channel = convert_texture_channel(base_material->get_roughness_texture_channel());

        if (base_material->get_feature(godot::BaseMaterial3D::FEATURE_EMISSION)) {
            material.emission = base_material->get_emission() * base_material->get_emission_energy_multiplier();
            material.emission_texture = snapshot_material_texture(base_material, godot::BaseMaterial3D::TEXTURE_EMISSION, cache);
        }

        return material;
    }

    int get_or_add_material_id(Scene& scene,
                               const godot::Ref<godot::Material>& godot_material,
                               SceneExtractionCache& cache) {
        if (godot_material.is_null()) {
            if (cache.default_material_id < 0) {
                cache.default_material_id = scene.add_material(Material());
            }
            return cache.default_material_id;
        }

        const godot::Material* material_key = godot_material.ptr();
        if (material_key == nullptr) {
            if (cache.default_material_id < 0) {
                cache.default_material_id = scene.add_material(Material());
            }
            return cache.default_material_id;
        }

        const auto found = cache.material_ids.find(material_key);
        if (found != cache.material_ids.end()) {
            return found->second;
        }

        const Material material = extract_material(godot_material, cache);
        const int material_id = scene.add_material(material);
        cache.material_ids.emplace(material_key, material_id);
        return material_id;
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

    bool light_is_visible(godot::Light3D* godot_light) {
        if (godot_light->is_inside_tree()) {
            return godot_light->is_visible_in_tree();
        }
        return godot_light->is_visible();
    }

    bool uses_physical_light_units() {
        const godot::ProjectSettings* project_settings = godot::ProjectSettings::get_singleton();
        if (project_settings == nullptr) {
            return false;
        }

        static const godot::StringName setting_name(
            "rendering/lights_and_shadows/use_physical_light_units"
        );
        return static_cast<bool>(project_settings->get_setting_with_override(setting_name));
    }

    bool extract_light_type(godot::Light3D* godot_light, LightType& out_type) {
        if (godot::Object::cast_to<godot::DirectionalLight3D>(godot_light) != nullptr) {
            out_type = LightType::Directional;
            return true;
        }
        if (godot::Object::cast_to<godot::SpotLight3D>(godot_light) != nullptr) {
            out_type = LightType::Spot;
            return true;
        }
        if (godot::Object::cast_to<godot::OmniLight3D>(godot_light) != nullptr) {
            out_type = LightType::Omni;
            return true;
        }

        return false;
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
    SceneExtractionCache cache;
    extract_node(root, scene, nullptr, cache);
    return scene;
}

ExtractedScene SceneExtractor::extract_with_camera(godot::Node* root) const {
    ExtractedScene extracted_scene;
    CameraSearch camera_search;
    SceneExtractionCache cache;
    extract_node(root, extracted_scene.scene, &camera_search, cache);

    extracted_scene.has_camera = camera_search.has_camera;
    extracted_scene.camera = camera_search.camera;
    return extracted_scene;
}

void SceneExtractor::extract_node(godot::Node* node,
                                  Scene& scene,
                                  CameraSearch* camera_search,
                                  SceneExtractionCache& cache) const {
    if (node == nullptr) {
        return;
    }

    godot::MeshInstance3D* mesh_instance = godot::Object::cast_to<godot::MeshInstance3D>(node);
    if (mesh_instance != nullptr) {
        extract_mesh_instance(mesh_instance, scene, cache);
    }

    godot::Light3D* godot_light = godot::Object::cast_to<godot::Light3D>(node);
    if (godot_light != nullptr) {
        extract_light(godot_light, scene);
    }

    if (camera_search != nullptr) {
        godot::Camera3D* godot_camera = godot::Object::cast_to<godot::Camera3D>(node);
        if (godot_camera != nullptr) {
            extract_camera(godot_camera, *camera_search);
        }
    }

    const int32_t child_count = node->get_child_count();
    for (int32_t i = 0; i < child_count; ++i) {
        extract_node(node->get_child(i), scene, camera_search, cache);
    }
}

void SceneExtractor::extract_light(godot::Light3D* godot_light, Scene& scene) const {
    if (godot_light == nullptr ||
        godot_light->is_editor_only() ||
        godot_light->is_negative() ||
        !light_is_visible(godot_light)) {
        return;
    }

    Light light;
    if (!extract_light_type(godot_light, light.type)) {
        return;
    }

    light.transform = godot_light->get_global_transform();
    // Match Godot's renderer light upload: Light3D color is sRGB and radiance is linear.
    light.color = godot_light->get_color().srgb_to_linear();

    const real_t energy_multiplier = uses_physical_light_units()
        ? static_cast<real_t>(1.0)
        : static_cast<real_t>(Math_PI);
    light.energy = std::max(
        godot_light->get_param(godot::Light3D::PARAM_ENERGY) * energy_multiplier,
        static_cast<real_t>(0.0)
    );
    light.range = std::max(godot_light->get_param(godot::Light3D::PARAM_RANGE), 0.0f);
    // Godot uses PARAM_ATTENUATION directly as the distance decay exponent.
    light.attenuation = godot_light->get_param(godot::Light3D::PARAM_ATTENUATION);
    light.spot_angle_radians = godot::Math::deg_to_rad(godot_light->get_param(godot::Light3D::PARAM_SPOT_ANGLE));
    light.spot_attenuation = std::max(godot_light->get_param(godot::Light3D::PARAM_SPOT_ATTENUATION), 0.0f);
    light.casts_shadow = godot_light->has_shadow();

    if (light.energy <= 0.0) {
        return;
    }

    scene.add_light(light);
}

void SceneExtractor::extract_mesh_instance(godot::MeshInstance3D* mesh_instance,
                                           Scene& scene,
                                           SceneExtractionCache& cache) const {
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
        const int material_id = get_or_add_material_id(scene, godot_material, cache);

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
