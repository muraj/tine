#pragma once

#include <string>
#include <entt/entt.hpp>

namespace tine {

class Engine;
class Renderer;

class Scene {
public:
    struct Pimpl;

    Scene();
    ~Scene();
    entt::registry &get_registry();
    void on_update(tine::Renderer *renderer);
    void on_render(tine::Renderer *renderer);

    static bool load_from_file(std::unique_ptr<tine::Scene> &scene, const std::string &fname);
private:
    std::unique_ptr<Pimpl> m_pimpl;
    // Camera component
    // TransformComponents
    // RenderableComponents
    // LightComponents
    // VertexBuffer
    // IndexBuffer
    // Meshes
    // Materials
    // Textures
    // Shaders
};

}