#pragma once

#include <initializer_list>
#include <string>

#include <GL/glew.h>

namespace glow {
    class Shader;
}

glow::Shader* createShaderFromFiles(
    GLenum type, std::initializer_list<const char*> paths);
glow::Shader* createShaderFromFile(GLenum type, const std::string& path);
