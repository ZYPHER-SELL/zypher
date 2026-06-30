#include "gui/gui_runtime.h"

namespace gui { void RunWindowThread(); }

int wmain() {
    gui::RunWindowThread();
    return 0;
}
