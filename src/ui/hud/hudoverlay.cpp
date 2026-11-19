#include "hudoverlay.h"

#include <algorithm>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glow/Program.h>
#include <glow/Shader.h>
#include <glowutils/global.h>

#include "camera/camera.h"
#include "display/rendering/screenquad.h"
#include "ui/voxelfont.h"

namespace {
    constexpr float OverlayDistance = 8.0f;
    constexpr float PanelLeft = 0.025f;
    constexpr float PanelTop = 0.69f;
    constexpr float PanelWidth = 0.38f;
    constexpr float PanelHeight = 0.28f;
    constexpr float MinimizedTop = 0.94f;
    constexpr float MinimizedWidth = 0.18f;
    constexpr float MinimizedHeight = 0.035f;
    constexpr float MinimizeButtonLeft = 0.80f;
    constexpr float MinimizeButtonWidth = 0.09f;
    constexpr float CloseButtonLeft = 0.91f;
    constexpr float CloseButtonWidth = 0.08f;
    constexpr float ControlTop = 0.0f;
    constexpr float ControlHeight = 0.38f;
    constexpr float MinimizedControlHeight = 1.0f;
    constexpr float TitleBarHeight = 0.12f;
    constexpr float ResizeGripWidth = 0.07f;
    constexpr float ResizeGripHeight = 0.12f;
    constexpr float MinimumPanelWidth = 0.20f;
    constexpr float MinimumPanelHeight = 0.14f;

    glm::vec4 panelNdcBounds(const glm::vec4& bounds) {
        return glm::vec4(
            bounds.x * 2.0f - 1.0f,
            1.0f - 2.0f * (bounds.y + bounds.w),
            bounds.z * 2.0f,
            bounds.w * 2.0f
        );
    }
}

HudOverlay::HudOverlay():
    m_bounds(PanelLeft, PanelTop, PanelWidth, PanelHeight),
    m_text(),
    m_visible(false),
    m_minimized(false),
    m_focused(false),
    m_interaction(Interaction::None),
    m_interactionStartPointer(0.0f),
    m_dragOffset(0.0f),
    m_interactionStartBounds(PanelLeft, PanelTop, PanelWidth, PanelHeight),
    m_focusChangedCallback(),
    m_quad(std::make_shared<ScreenQuad>()),
    m_program(nullptr)
{
}

HudOverlay::~HudOverlay() = default;

void HudOverlay::setBounds(const glm::vec4& bounds) {
    m_bounds.z = glm::clamp(bounds.z, MinimumPanelWidth, 1.0f);
    m_bounds.w = glm::clamp(bounds.w, MinimumPanelHeight, 1.0f);
    m_bounds.x = glm::clamp(bounds.x, 0.0f, 1.0f - m_bounds.z);
    m_bounds.y = glm::clamp(bounds.y, 0.0f, 1.0f - m_bounds.w);
}

void HudOverlay::setVisible(bool visible) {
    m_visible = visible;
    m_interaction = Interaction::None;
    if (!m_visible) {
        setFocused(false);
    }
}

bool HudOverlay::visible() const {
    return m_visible;
}

void HudOverlay::setMinimized(bool minimized) {
    m_minimized = minimized;
    m_interaction = Interaction::None;
}

bool HudOverlay::minimized() const {
    return m_minimized;
}

void HudOverlay::setFocused(bool focused) {
    if (m_focused == focused) {
        return;
    }

    m_focused = focused;
    if (!m_focused) {
        m_interaction = Interaction::None;
    }
    if (m_focused) {
        m_visible = true;
    }
    if (m_focusChangedCallback) {
        m_focusChangedCallback();
    }
}

bool HudOverlay::focused() const {
    return m_focused;
}

void HudOverlay::setFocusChangedCallback(
    const std::function<void()>& callback)
{
    m_focusChangedCallback = callback;
}

bool HudOverlay::hasContent() const {
    return !m_text.empty();
}

glm::vec4 HudOverlay::displayedBounds() const {
    if (m_minimized) {
        return glm::vec4(
            PanelLeft,
            MinimizedTop,
            MinimizedWidth,
            MinimizedHeight);
    }
    return m_bounds;
}

void HudOverlay::clearText() {
    m_text.clear();
}

void HudOverlay::addText(
    const std::string& text,
    const glm::vec2& position,
    float relativeHeight,
    FontSize size)
{
    m_text.push_back(TextElement{text, position, relativeHeight, size});
}

void HudOverlay::setTextLines(const std::vector<std::string>& lines) {
    clearText();
    if (lines.empty()) {
        return;
    }

    constexpr float firstLineY = 0.16f;
    constexpr float maxLineSpacing = 0.082f;
    constexpr float availableHeight = 0.74f;
    const float lineSpacing = std::min(
        maxLineSpacing,
        availableHeight / static_cast<float>(lines.size()));
    const float relativeFontHeight = lineSpacing * 0.62f;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        addText(
            lines[index],
            glm::vec2(
                0.05f,
                firstLineY + lineSpacing * static_cast<float>(index)),
            relativeFontHeight
        );
    }
}

bool HudOverlay::handleMouseButtonEvent(
    int button,
    int action,
    double cursorX,
    double cursorY,
    int viewportWidth,
    int viewportHeight,
    bool allowFocus)
{
    if (button != GLFW_MOUSE_BUTTON_LEFT ||
        (action != GLFW_PRESS && action != GLFW_RELEASE) ||
        viewportWidth <= 0 || viewportHeight <= 0) {
        return m_focused;
    }

    const glm::vec2 pointer(
        static_cast<float>(cursorX / viewportWidth),
        static_cast<float>(cursorY / viewportHeight));
    const glm::vec4 bounds = displayedBounds();
    const auto contains = [&pointer, &bounds](
        float left,
        float top,
        float width,
        float height)
    {
        return pointer.x >= bounds.x + bounds.z * left &&
            pointer.x <= bounds.x + bounds.z * (left + width) &&
            pointer.y >= bounds.y + bounds.w * top &&
            pointer.y <= bounds.y + bounds.w * (top + height);
    };
    if (!m_focused) {
        if (action != GLFW_PRESS || !allowFocus || !m_visible ||
            !contains(0.0f, 0.0f, 1.0f, 1.0f)) {
            return false;
        }
        setFocused(true);
    }

    const float controlHeight = m_minimized
        ? MinimizedControlHeight
        : ControlHeight;
    const bool minimizeHit = contains(
        MinimizeButtonLeft, ControlTop, MinimizeButtonWidth, controlHeight);
    const bool closeHit = contains(
        CloseButtonLeft, ControlTop, CloseButtonWidth, controlHeight);

    if (action == GLFW_PRESS) {
        m_interactionStartPointer = pointer;
        m_interactionStartBounds = m_bounds;
        if (minimizeHit) {
            m_interaction = Interaction::MinimizeButton;
        } else if (closeHit) {
            m_interaction = Interaction::CloseButton;
        } else if (!m_minimized &&
            pointer.x >= m_bounds.x + m_bounds.z * (1.0f - ResizeGripWidth) &&
            pointer.y >= m_bounds.y + m_bounds.w *
                (1.0f - ResizeGripHeight)) {
            m_interaction = Interaction::Resizing;
        } else if (!m_minimized &&
            pointer.y <= m_bounds.y + m_bounds.w * TitleBarHeight) {
            m_interaction = Interaction::Dragging;
            m_dragOffset = pointer - glm::vec2(m_bounds.x, m_bounds.y);
        } else {
            m_interaction = Interaction::None;
        }
        return true;
    }

    const Interaction pressedInteraction = m_interaction;
    m_interaction = Interaction::None;
    if (pressedInteraction == Interaction::MinimizeButton && minimizeHit) {
        setMinimized(!m_minimized);
    } else if (pressedInteraction == Interaction::CloseButton && closeHit) {
        setVisible(false);
    }
    return true;
}

void HudOverlay::updatePointer(float cursorX, float cursorY) {
    if (!m_focused) {
        return;
    }

    if (m_interaction == Interaction::Dragging) {
        m_bounds.x = glm::clamp(
            cursorX - m_dragOffset.x,
            0.0f,
            1.0f - std::max(m_bounds.z, MinimumPanelWidth));
        m_bounds.y = glm::clamp(
            cursorY - m_dragOffset.y,
            0.0f,
            1.0f - std::max(m_bounds.w, MinimumPanelHeight));
    } else if (m_interaction == Interaction::Resizing) {
        const float widthLimit = 1.0f - m_interactionStartBounds.x;
        const float heightLimit = 1.0f - m_interactionStartBounds.y;
        m_bounds.z = glm::clamp(
            m_interactionStartBounds.z +
                cursorX - m_interactionStartPointer.x,
            MinimumPanelWidth,
            widthLimit);
        m_bounds.w = glm::clamp(
            m_interactionStartBounds.w +
                cursorY - m_interactionStartPointer.y,
            MinimumPanelHeight,
            heightLimit);
    }
}

void HudOverlay::drawBackground() {
    if (!m_visible || (!hasContent() && !m_focused && !m_minimized)) {
        return;
    }
    if (!m_program) {
        initialize();
    }

    const GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLint oldBlendSourceRgb = GL_ONE;
    GLint oldBlendDestinationRgb = GL_ZERO;
    GLint oldBlendSourceAlpha = GL_ONE;
    GLint oldBlendDestinationAlpha = GL_ZERO;
    glGetIntegerv(GL_BLEND_SRC_RGB, &oldBlendSourceRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &oldBlendDestinationRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &oldBlendSourceAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &oldBlendDestinationAlpha);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(
        GL_SRC_ALPHA,
        GL_ONE_MINUS_SRC_ALPHA,
        GL_ONE,
        GL_ONE_MINUS_SRC_ALPHA);
    m_program->use();
    const auto drawPanelRect = [this](
        const glm::vec4& bounds,
        const glm::vec3& color,
        float opacity)
    {
        m_program->setUniform("panelBounds", panelNdcBounds(bounds));
        m_program->setUniform("panelColor", color);
        m_program->setUniform("panelOpacity", opacity);
        m_quad->draw();
    };
    drawPanelRect(
        displayedBounds(),
        glm::vec3(0.18f, 0.48f, 0.64f),
        0.88f);
    if (!m_minimized) {
        glm::vec4 headerBounds = m_bounds;
        headerBounds.w *= TitleBarHeight;
        drawPanelRect(
            headerBounds,
            glm::vec3(0.11f, 0.37f, 0.53f),
            0.96f);
    }
    m_program->release();

    glBlendFuncSeparate(
        oldBlendSourceRgb,
        oldBlendDestinationRgb,
        oldBlendSourceAlpha,
        oldBlendDestinationAlpha);
    if (!blendEnabled) {
        glDisable(GL_BLEND);
    }
    if (depthTestEnabled) {
        glEnable(GL_DEPTH_TEST);
    }
}

void HudOverlay::drawText(const Camera& camera) const {
    if (!m_visible) {
        return;
    }

    const glm::vec4 bounds = displayedBounds();
    const glm::mat4 projection = camera.projection();
    const float halfWidth = OverlayDistance / projection[0][0];
    const float halfHeight = OverlayDistance / projection[1][1];
    const float panelWidth = bounds.z * 2.0f * halfWidth;
    const float panelHeight = bounds.w * 2.0f * halfHeight;
    const float centerX = (bounds.x + bounds.z * 0.5f) * 2.0f - 1.0f;
    const float centerY = 1.0f -
        (bounds.y + bounds.w * 0.5f) * 2.0f;
    const glm::quat screenOrientation = camera.orientation();
    const glm::vec3 panelCenter = camera.eye() + screenOrientation *
        glm::vec3(centerX * halfWidth, centerY * halfHeight, -OverlayDistance);

    const auto drawElement = [&](const TextElement& element) {
        const float glyphHeight = panelHeight * element.relativeHeight;
        const float scale = glyphHeight /
            VoxelFont::instance()->letterHeight(element.size);
        const glm::vec3 localPosition(
            -panelWidth * 0.5f + panelWidth * element.position.x,
            panelHeight * 0.5f - panelHeight * element.position.y,
            0.0f);
        VoxelFont::instance()->drawString(
            element.text,
            panelCenter + screenOrientation * localPosition,
            screenOrientation,
            element.size,
            scale,
            FontAlign::LEFT);
    };

    const float buttonFontHeight = m_minimized ? 0.52f : 0.13f;
    drawElement(TextElement{
        "-",
        glm::vec2(MinimizeButtonLeft + 0.02f, 0.04f),
        buttonFontHeight,
        FontSize::SIZE3x5
    });
    if (m_focused) {
        drawElement(TextElement{
            "X",
            glm::vec2(CloseButtonLeft + 0.015f, 0.04f),
            buttonFontHeight,
            FontSize::SIZE3x5
        });
        if (!m_minimized) {
            drawElement(TextElement{
                "///",
                glm::vec2(0.94f, 0.92f),
                0.035f,
                FontSize::SIZE3x5
            });
        }
    }
    if (!m_minimized) {
        for (const TextElement& element : m_text) {
            drawElement(element);
        }
    }
}

void HudOverlay::initialize() {
    m_program = new glow::Program();
    m_program->attach(
        glowutils::createShaderFromFile(
            GL_VERTEX_SHADER, "data/shader/hud/hudoverlay.vert"),
        glowutils::createShaderFromFile(
            GL_FRAGMENT_SHADER, "data/shader/hud/hudoverlay.frag"));
}

void HudOverlay::beforeContextDestroy() {
    m_program = nullptr;
}

void HudOverlay::afterContextRebuild() {
    initialize();
}
