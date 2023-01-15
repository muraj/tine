#pragma once

#include <memory>

namespace tine {

class Renderer;
class Scene;

class Engine {
  public:
    Engine();
    ~Engine();
    Engine(const Engine &) = delete;
    bool init(int argc = 0, const char **argv = NULL);
    void loop();
    void cleanup();
    inline Renderer *get_renderer() const { return m_renderer.get(); }

    // Events
    void on_exit();

  private:
    std::unique_ptr<tine::Renderer> m_renderer;
    std::unique_ptr<tine::Scene> m_scene;
    bool done = false;
};

} // namespace tine