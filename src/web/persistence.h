#pragma once

#include <emscripten.h>

inline void persistBrowserFiles() {
    EM_ASM({ Module.persistChanges(); });
}
