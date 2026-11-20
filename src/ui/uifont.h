#pragma once

#include <string>
#include <vector>

#include <glow/ref_ptr.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "etc/contextdependant.h"
#include "ui/voxelfontconstants.h"

namespace glow {
    class Buffer;
    class Program;
    class Texture;
    class VertexArrayObject;
}
class Camera;

class UiFont : public ContextDependant {
public:
    static UiFont* instance();
    ~UiFont();

    void setCamera(const Camera& camera);
    void drawString(
        const std::string& text, glm::vec3 position, glm::quat orientation,
        FontSize size = FontSize::SIZE5x7, float scale = 1.0f,
        FontAlign align = FontAlign::CENTER);
    void drawScreen(
        const std::string& text, glm::vec2 position, float height,
        const glm::vec4& color = glm::vec4(1.0f),
        FontAlign align = FontAlign::LEFT, float maxWidth = 1.0f);

    int letterWidth(FontSize size) const;
    int letterHeight(FontSize size) const;
    void drawWeaponIcon(unsigned id, const glm::vec4& bounds);

private:
    UiFont() = default;
    struct Vertex {
        glm::vec3 position;
        glm::vec2 uv;
    };
    void append(
        std::vector<Vertex>& vertices, char character,
        const glm::vec3& center, const glm::vec3& right,
        const glm::vec3& up);
    void draw(
        const std::vector<Vertex>& vertices, const glm::mat4& viewProjection,
        const glm::vec4& color, glow::Texture* atlas = nullptr);
    void initialize();
    void beforeContextDestroy() override;
    void afterContextRebuild() override;

    glm::mat4 m_viewProjection = glm::mat4(1.0f);
    glow::ref_ptr<glow::Program> m_program;
    glow::ref_ptr<glow::Texture> m_atlas;
    glow::ref_ptr<glow::Texture> m_icons;
    glow::ref_ptr<glow::Buffer> m_buffer;
    glow::ref_ptr<glow::VertexArrayObject> m_array;
};
