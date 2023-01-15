#include "tine_log.h"
#include "tine_engine.h"
#include "tine_renderer.h"
#include "tine_scene.h"

tine::Engine::Engine() : m_renderer(new tine::Renderer(this)) {}
tine::Engine::~Engine() {}

bool tine::Engine::init(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    std::string filename("../../src/assets/box.obj");

    #ifndef NDEBUG
    // TODO: Factor this into arg processing
    spdlog::set_level(spdlog::level::trace);
    #endif

    if (argc > 1) {
        filename = argv[1];
    }

    if (!m_renderer->init(1280, 768)) {
        return false;
    }

    if (!tine::Scene::load_from_file(m_scene, filename)) {
        return false;
    }

    return true;
}

void tine::Engine::cleanup() { m_renderer->cleanup(); }

void tine::Engine::loop() {
    while (!done) {
        m_renderer->render(m_scene.get());
    }
}

void tine::Engine::on_exit() {
    done = true;
}