#pragma once

#include <memory>

namespace tine {

class Engine;
class Scene;

class Renderer {
  public:
    struct Pimpl;

    Renderer(tine::Engine *eng);
    ~Renderer();
    Renderer(const Renderer &) = delete;
    void render(Scene *scene);
    bool init(int width, int height);
    void cleanup();
    void get_extents(int &w, int &h) { w = m_width; h = m_height; }
    tine::Engine *get_engine() const { return m_engine; }

    void on_resize();

  private:
    tine::Engine *m_engine = nullptr;
    std::unique_ptr<Pimpl> m_pimpl;
    int m_width, m_height;
    size_t m_frame = 0;
};

} // namespace tine