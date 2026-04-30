#include <SDL.h>

#include "Application.h"
#include "core/log.h"

int main(int /*argc*/, char* /*argv*/[]) {
    pengine::Application app;
    if (!app.init()) {
        PE_ERROR("Application::init failed");
        return 1;
    }
    int rc = app.run();
    app.shutdown();
    PE_INFO("clean exit");
    return rc;
}
