#include "tine_engine.h"

int main(int argc, const char **argv) {
    tine::Engine eng;
    if (!eng.init(argc, argv)) {
        return 1;
    }
    eng.loop();
    eng.cleanup();
    return 0;
}
