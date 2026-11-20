#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glow/ref_ptr.h>
#include <glm/glm.hpp>

#include "etc/contextdependant.h"
#include "input/inputhandler.h"
#include "ui/mainmenustate.h"

namespace glow { class Program; }
class ScreenQuad;

class MainMenu : public InputHandler, public ContextDependant {
public:
    MainMenu();
    ~MainMenu() override;

    bool visible() const;
    void toggle();
    void setConnection(bool ready, const std::string& note);
    void draw();
    void update(float deltaSec) override;
    void onKeyEvent(int key, int scancode, int action, int mods) override;
    void onMouseButtonEvent(int button, int action, int mods) override;

private:
    std::vector<std::string> labels() const;
    bool enabled(int index) const;
    int itemAtPointer() const;
    float buttonTop() const;
    float buttonStep() const;
    void activate(int index);
    void selectNext(int direction);
    void saveSettings();
    void rectangle(const glm::vec4& bounds, const glm::vec3& color);
    void initialize();
    void beforeContextDestroy() override;
    void afterContextRebuild() override;

    MainMenuState m_state;
    int m_selected = 2;
    int m_pressed = -1;
    glm::dvec2 m_lastPointer = glm::dvec2(-1.0);
    std::string m_note = "Connecting to server. Start and Resume are unavailable until setup completes.";
    std::string m_settingsError;
    std::unique_ptr<ScreenQuad> m_quad;
    glow::ref_ptr<glow::Program> m_program;
};
