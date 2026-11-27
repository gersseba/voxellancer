#pragma once

#include <GLFW/glfw3.h>

#ifdef __EMSCRIPTEN__
extern "C" int browserGameFocused();
#endif

inline bool gameWindowFocused() {
#ifdef __EMSCRIPTEN__
    return browserGameFocused() != 0;
#else
    return glfwGetWindowAttrib(glfwGetCurrentContext(), GLFW_FOCUSED) != 0;
#endif
}
