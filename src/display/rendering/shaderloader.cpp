#include "shaderloader.h"

#include <glow/CompositeStringSource.h>
#include <glow/Shader.h>
#include <glowutils/File.h>

#ifdef __EMSCRIPTEN__
#include <stdexcept>

#include <glow/ChangeListener.h>
#include <glow/ref_ptr.h>

namespace {
class WebShaderSource : public glow::AbstractStringSource, protected glow::ChangeListener {
public:
    explicit WebShaderSource(glow::AbstractStringSource* source): m_source(source) {
        m_source->registerListener(this);
    }

    ~WebShaderSource() {
        m_source->deregisterListener(this);
    }

    std::string string() const override {
        std::string source = m_source->string();
        const std::string version = "#version 330";
        if (source.compare(0, version.size(), version) != 0 ||
            (source.size() > version.size() &&
             source[version.size()] != '\n' && source[version.size()] != '\r')) {
            throw std::runtime_error("Expected GLSL 330 shader source: " + m_source->shortInfo());
        }
        source.replace(0, version.size(),
            "#version 300 es\nprecision highp float;\nprecision highp int;");
        return source;
    }

    std::string shortInfo() const override {
        return m_source->shortInfo();
    }

private:
    void notifyChanged(glow::Changeable*) override {
        changed();
    }

    glow::ref_ptr<glow::AbstractStringSource> m_source;
};
}
#endif

namespace {
glow::Shader* createShader(GLenum type, glow::AbstractStringSource* source) {
#ifdef __EMSCRIPTEN__
    return new glow::Shader(type, new WebShaderSource(source));
#else
    return new glow::Shader(type, source);
#endif
}
}

glow::Shader* createShaderFromFiles(
    GLenum type, std::initializer_list<const char*> paths)
{
    auto* source = new glow::CompositeStringSource();
    for (const char* path : paths) {
        source->appendSource(new glowutils::File(path));
    }
    return createShader(type, source);
}

glow::Shader* createShaderFromFile(GLenum type, const std::string& path) {
    return createShader(type, new glowutils::File(path));
}
