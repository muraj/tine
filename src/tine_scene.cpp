#include "tine_log.h"
#include "tine_scene.h"
#include "tine_component.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>

struct tine::Scene::Pimpl {
    entt::registry m_registry;
    entt::entity m_primary_camera;
};

tine::Scene::Scene() : m_pimpl(new Pimpl) {}
tine::Scene::~Scene() {}

void tine::Scene::on_update(tine::Renderer *) {}

void tine::Scene::on_render(tine::Renderer *) {}

glm::vec3 convert_to_glm(const aiVector3D &v) { return glm::vec3(v.x, v.y, v.z); }

static bool load_cameras(tine::Scene::Pimpl &scene, aiCamera **cameras, unsigned int camera_cnt) {
    entt::registry &registry = scene.m_registry;
    for (unsigned int i = 0; i < camera_cnt; i++) {
        aiCamera &imported_camera = *cameras[i];
        entt::entity camera_entity = registry.create();
        tine::CameraComponent &camera =
            registry.get_or_emplace<tine::CameraComponent>(scene.m_primary_camera);
        if (imported_camera.mOrthographicWidth != 0) {
            TINE_ERROR("Ortho camera not supported");
            goto Error;
        } else {
            camera.set_perspective(imported_camera.mHorizontalFOV, imported_camera.mAspect,
                                   imported_camera.mClipPlaneNear, imported_camera.mClipPlaneNear);
            camera.look_at(convert_to_glm(imported_camera.mPosition), convert_to_glm(imported_camera.mLookAt), convert_to_glm(imported_camera.mUp));
        }
        if (scene.m_primary_camera == entt::entity{}) {
            scene.m_primary_camera = camera_entity;
        }
    }
    if (scene.m_primary_camera == entt::entity{}) {
        entt::entity camera_entity = registry.create();
        tine::CameraComponent &camera =
            registry.get_or_emplace<tine::CameraComponent>(scene.m_primary_camera);
        camera.look_at({0.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
        camera.set_perspective(90, 16.0f / 4.0f, 0.1f, 100.0f);
        scene.m_primary_camera = camera_entity;
    }
    return true;
Error:
    return false;
}

static bool load_meshes(tine::Scene::Pimpl &scene, aiMesh **meshes, uint32_t mesh_cnt) {
    entt::registry &registry = scene.m_registry;
    for (unsigned int i = 0; i < mesh_cnt; i++) {
        aiMesh &mesh = *meshes[i];
        // Preprocess all the meshes, find out the size needed for the vertex, index, texture coordinate, and transform buffers
        // Then add the mesh as an entity component
    }
    return false;
}

bool tine::Scene::load_from_file(std::unique_ptr<tine::Scene> &scene, const std::string &fname) {
    ::Assimp::Importer importer;
    const aiScene *i_scene = nullptr;

    TINE_TRACE("Loading scene {0}", fname);

    scene.reset(new Scene());
    TINE_CHECK(scene != nullptr, "Failed to create scene", Error);
    if (!scene) {
        goto Error;
    }

    // TODO: Put this in an asynchronous task...
    // TODO: figure out how to cache the same textures, etc
    i_scene = importer.ReadFile(fname, 0);
    TINE_CHECK(i_scene != nullptr, "Failed to load file", Error);

    TINE_CHECK(load_cameras(*scene->m_pimpl, i_scene->mCameras, i_scene->mNumCameras), "Failed to load cameras", Error);
    TINE_CHECK(load_meshes(*scene->m_pimpl, i_scene->mMeshes, i_scene->mNumMeshes), "Failed to load meshes", Error);
    //TINE_CHECK(load_textures(*scene->m_pimpl, i_scene->mTextures, i_scene->mNumTextures), "Failed to load textures", Error);
    //TINE_CHECK(load_lights(*scene->m_pimpl, i_scene->mLights, i_scene->mNumLights), "Failed to load lights", Error);
    //TINE_CHECK(load_materials(*scene->m_pimpl, i_scene->mMaterials, i_scene->mNumMaterials), "Failed to load materials", Error);

    return true;
Error:
    if (scene) {
        scene.release();
    }
    return false;
}