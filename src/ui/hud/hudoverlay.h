#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glow/ref_ptr.h>
#include <glm/glm.hpp>

#include "etc/contextdependant.h"
#include "ui/voxelfontconstants.h"

namespace glow {
    class Program;
}

class Camera;
class ScreenQuad;

class HudOverlay : public ContextDependant {
public:
    HudOverlay();
    ~HudOverlay();

    void setBounds(const glm::vec4& bounds);
    void setVisible(bool visible);
    bool visible() const;
    void setMinimized(bool minimized);
    bool minimized() const;
    void setFocused(bool focused);
    bool focused() const;
    void setFocusChangedCallback(const std::function<void()>& callback);
    bool hasContent() const;

    void clearText();
    void addText(
        const std::string& text,
        const glm::vec2& position,
        float relativeHeight,
        FontSize size = FontSize::SIZE3x5
    );
    void setTextLines(const std::vector<std::string>& lines);

    bool handleMouseButtonEvent(
        int button,
        int action,
        double cursorX,
        double cursorY,
        int viewportWidth,
        int viewportHeight,
        bool allowFocus = false
    );
    void updatePointer(float cursorX, float cursorY);

    void drawBackground();
    void drawText(const Camera& camera) const;

protected:
    struct TextElement {
        std::string text;
        glm::vec2 position;
        float relativeHeight;
        FontSize size;
    };

    enum class Interaction {
        None,
        MinimizeButton,
        CloseButton,
        Dragging,
        Resizing
    };

    glm::vec4 m_bounds;
    std::vector<TextElement> m_text;
    bool m_visible;
    bool m_minimized;
    bool m_focused;
    Interaction m_interaction;
    glm::vec2 m_interactionStartPointer;
    glm::vec2 m_dragOffset;
    glm::vec4 m_interactionStartBounds;
    std::function<void()> m_focusChangedCallback;
    std::shared_ptr<ScreenQuad> m_quad;
    glow::ref_ptr<glow::Program> m_program;

    void initialize();
    glm::vec4 displayedBounds() const;
    void beforeContextDestroy() override;
    void afterContextRebuild() override;
};
