#include "uifont.h"

#include <cstddef>
#include <stdexcept>
#include <string_view>

#include <glow/Buffer.h>
#include <glow/Program.h>
#include <glow/Texture.h>
#include <glow/VertexArrayObject.h>
#include <glow/VertexAttributeBinding.h>
#include <glowutils/global.h>

#include "camera/camera.h"
#include "resource/ddstexture.h"

namespace {
    constexpr std::string_view Characters =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.,!?:;'\"-+()0123456789/%*=_$";
    constexpr float CellSize = 32.0f;
    constexpr float CapHeight = 16.0f;
    constexpr float Advance = 12.0f;

    float alignedOffset(std::size_t length, float advance, FontAlign align) {
        const float width = length == 0 ? 0.0f : (length - 1) * advance;
        return align == FontAlign::RIGHT ? -width
            : align == FontAlign::CENTER ? -width * 0.5f : 0.0f;
    }
}

UiFont::~UiFont() = default;

UiFont* UiFont::instance() {
    static UiFont font;
    return &font;
}

void UiFont::setCamera(const Camera& camera) {
    m_viewProjection = camera.viewProjection();
}

int UiFont::letterWidth(FontSize size) const {
    return size == FontSize::SIZE3x5 ? 5 : 7;
}

int UiFont::letterHeight(FontSize size) const {
    return size == FontSize::SIZE3x5 ? 5 : 7;
}

void UiFont::append(
    std::vector<Vertex>& vertices, char character, const glm::vec3& center,
    const glm::vec3& right, const glm::vec3& up)
{
    if (character == ' ') {
        return;
    }
    auto index = Characters.find(character);
    if (index == std::string_view::npos) {
        index = Characters.find('?');
    }
    const float u = static_cast<float>(index % 8) / 8.0f;
    const float v = static_cast<float>(index / 8) / 10.0f;
    // The generated atlas has 8 columns, with cap tops/baselines at y=6/22.
    const glm::vec3 left = center - right * 17.0f;
    const glm::vec3 bottomLeft = left - up * 18.0f;
    const glm::vec3 topLeft = left + up * 14.0f;
    const glm::vec3 bottomRight = bottomLeft + right * CellSize;
    const glm::vec3 topRight = topLeft + right * CellSize;
    const glm::vec2 uvTopLeft(u, v);
    const glm::vec2 uvBottomLeft(u, v + 0.1f);
    const glm::vec2 uvTopRight(u + 0.125f, v);
    const glm::vec2 uvBottomRight(u + 0.125f, v + 0.1f);
    vertices.insert(vertices.end(), {
        {bottomLeft, uvBottomLeft}, {bottomRight, uvBottomRight}, {topLeft, uvTopLeft},
        {topLeft, uvTopLeft}, {bottomRight, uvBottomRight}, {topRight, uvTopRight}
    });
}

void UiFont::drawString(
    const std::string& text, glm::vec3 position, glm::quat orientation,
    FontSize size, float scale, FontAlign align)
{
    const float advance = letterWidth(size) * scale;
    const glm::vec3 right = orientation * glm::vec3(advance / Advance, 0.0f, 0.0f);
    const glm::vec3 up = orientation *
        glm::vec3(0.0f, letterHeight(size) * scale / CapHeight, 0.0f);
    const float offset = alignedOffset(text.size(), advance, align);
    std::vector<Vertex> vertices;
    vertices.reserve(text.size() * 6);
    for (std::size_t index = 0; index < text.size(); ++index) {
        append(vertices, text[index], position + orientation *
            glm::vec3(offset + index * advance, 0.0f, 0.0f), right, up);
    }
    draw(vertices, m_viewProjection, glm::vec4(1.0f));
}

void UiFont::drawScreen(
    const std::string& text, glm::vec2 position, float height,
    const glm::vec4& color, FontAlign align, float maxWidth)
{
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0 || text.empty()) {
        return;
    }
    const float aspect = static_cast<float>(viewport[2]) / viewport[3];
    const float width = text.size() * height * Advance / CapHeight / aspect;
    if (width > maxWidth) {
        height *= maxWidth / width;
    }
    const float advance = height * Advance / CapHeight / aspect * 2.0f;
    const glm::vec3 right(advance / Advance, 0.0f, 0.0f);
    const glm::vec3 up(0.0f, height * 2.0f / CapHeight, 0.0f);
    const float offset = alignedOffset(text.size(), advance, align);
    std::vector<Vertex> vertices;
    vertices.reserve(text.size() * 6);
    for (std::size_t index = 0; index < text.size(); ++index) {
        append(vertices, text[index], glm::vec3(
            position.x * 2.0f - 1.0f + offset + index * advance,
            1.0f - position.y * 2.0f - height, 0.0f), right, up);
    }
    draw(vertices, glm::mat4(1.0f), color);
}

void UiFont::initialize() {
    m_program = new glow::Program();
    m_program->attach(
        glowutils::createShaderFromFile(GL_VERTEX_SHADER, "data/shader/ui/font.vert"),
        glowutils::createShaderFromFile(GL_FRAGMENT_SHADER, "data/shader/ui/font.frag"));
    m_atlas = new glow::Texture(GL_TEXTURE_2D);
    if (!DdsTexture::loadImage2d(m_atlas, "data/ui/fonts/voxellancer-flight.dds")) {
        throw std::runtime_error("Could not load the Voxellancer Flight UI font atlas.");
    }
    m_atlas->setParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_atlas->setParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_atlas->setParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_atlas->setParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_buffer = new glow::Buffer(GL_ARRAY_BUFFER);
    m_array = new glow::VertexArrayObject();
    auto* positions = m_array->binding(0);
    positions->setAttribute(0);
    positions->setBuffer(m_buffer, 0, sizeof(Vertex));
    positions->setFormat(3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    m_array->enable(0);
    auto* uv = m_array->binding(1);
    uv->setAttribute(1);
    uv->setBuffer(m_buffer, 0, sizeof(Vertex));
    uv->setFormat(2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv));
    m_array->enable(1);
}

void UiFont::draw(
    const std::vector<Vertex>& vertices, const glm::mat4& viewProjection,
    const glm::vec4& color, glow::Texture* atlas)
{
    if (vertices.empty()) {
        return;
    }
    GLint program, array, buffer, activeTexture, texture, sourceRgb, destinationRgb;
    GLint sourceAlpha, destinationAlpha;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &array);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    glGetIntegerv(GL_BLEND_SRC_RGB, &sourceRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &destinationRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &sourceAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &destinationAlpha);
    const bool blend = glIsEnabled(GL_BLEND);
    const bool depth = glIsEnabled(GL_DEPTH_TEST);
    const bool cull = glIsEnabled(GL_CULL_FACE);
    if (!m_program) {
        initialize();
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    m_program->setUniform("viewProjection", viewProjection);
    m_program->setUniform("textColor", color);
    m_program->setUniform("colored", atlas != nullptr);
    m_program->setUniform("atlas", 0);
    m_program->use();
    (atlas ? atlas : m_atlas.get())->bind();
    m_buffer->setData(vertices.size() * sizeof(Vertex), vertices.data(), GL_DYNAMIC_DRAW);
    m_array->drawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glUseProgram(program);
    glBindVertexArray(array);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBindTexture(GL_TEXTURE_2D, texture);
    glActiveTexture(activeTexture);
    glBlendFuncSeparate(sourceRgb, destinationRgb, sourceAlpha, destinationAlpha);
    if (!blend) glDisable(GL_BLEND);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE);
}

void UiFont::beforeContextDestroy() {
    m_array = nullptr;
    m_buffer = nullptr;
    m_atlas = nullptr;
    m_icons = nullptr;
    m_program = nullptr;
}

void UiFont::drawWeaponIcon(unsigned id, const glm::vec4& bounds) {
    if (id < 1 || id > 6) throw std::invalid_argument("Invalid weapon icon ID.");
    if (!m_program) initialize();
    if (!m_icons) {
        m_icons = new glow::Texture(GL_TEXTURE_2D);
        if (!DdsTexture::loadImage2d(m_icons, "data/ui/weapons/icons.dds"))
            throw std::runtime_error("Could not load weapon icon atlas.");
        m_icons->setParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        m_icons->setParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        m_icons->setParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_icons->setParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    const float l = bounds.x * 2 - 1, r = (bounds.x + bounds.z) * 2 - 1;
    const float t = 1 - bounds.y * 2, b = 1 - (bounds.y + bounds.w) * 2;
    const float u = (id - 1) / 6.0f, v = id / 6.0f;
    draw({{{l,b,0},{u,1}}, {{r,b,0},{v,1}}, {{l,t,0},{u,0}},
          {{l,t,0},{u,0}}, {{r,b,0},{v,1}}, {{r,t,0},{v,0}}},
        glm::mat4(1), glm::vec4(1), m_icons.get());
}

void UiFont::afterContextRebuild() {
}
