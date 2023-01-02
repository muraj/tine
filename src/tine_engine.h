#pragma once

#include <memory>

namespace tine {

class Renderer;

class Engine {
  public:
    Engine();
    ~Engine();
    Engine(const Engine &) = delete;
    bool init(int argc = 0, const char **argv = NULL);
    void loop();
    void cleanup();
    Renderer *get_renderer() const { return m_renderer.get(); }

  private:
    std::unique_ptr<tine::Renderer> m_renderer;
};

} // namespace tine