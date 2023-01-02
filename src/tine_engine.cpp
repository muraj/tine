#include "tine_log.h"
#include "tine_engine.h"
#include "tine_renderer.h"

tine::Engine::Engine() : m_renderer(new tine::Renderer(this)) {}
tine::Engine::~Engine() {}

bool tine::Engine::init(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    #ifndef NDEBUG
    // TODO: Factor this into arg processing
    spdlog::set_level(spdlog::level::trace);
    #endif

    return m_renderer->init(1280, 768); // TODO: parse width/height from cmdline
}

void tine::Engine::cleanup() { m_renderer->cleanup(); }

void tine::Engine::loop() {
    while (1) {
        m_renderer->render();
    }
}