#include "mainmenu.h"

#include <fstream>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glow/Program.h>
#include <glowutils/global.h>

#include "display/rendering/screenquad.h"
#include "property/property.h"
#include "ui/uifont.h"
#include "utils/filesystem.h"

namespace {
    constexpr float ButtonLeft = 0.34f;
    constexpr float ButtonTop = 0.40f;
    constexpr float ButtonWidth = 0.32f;
    constexpr float ButtonHeight = 0.065f;
    constexpr float ButtonStep = 0.09f;
    const glm::vec3 Background(0.025f, 0.045f, 0.075f);
    const glm::vec3 Surface(0.055f, 0.09f, 0.13f);
    const glm::vec3 Accent(0.30f, 0.80f, 0.90f);
}

MainMenu::MainMenu(): m_quad(new ScreenQuad()) {}
MainMenu::~MainMenu() = default;

bool MainMenu::visible() const {
    return m_state.visible();
}

void MainMenu::toggle() {
    if (m_state.page() == MainMenuState::Page::Settings) {
        m_state.show();
        m_selected = 2;
    } else if (visible()) {
        m_state.resume();
    } else {
        m_state.show();
        m_selected = 1;
    }
    m_pressed = -1;
}

void MainMenu::setConnection(bool ready, const std::string& note) {
    const bool couldStart = m_state.startEnabled();
    m_state.setConnectionReady(ready);
    m_note = note;
    if (!couldStart && m_state.startEnabled() &&
        m_state.page() == MainMenuState::Page::Main) {
        m_selected = 0;
    }
    if (!enabled(m_selected)) {
        selectNext(1);
    }
}

std::vector<std::string> MainMenu::labels() const {
    if (m_state.page() != MainMenuState::Page::Settings) {
        return {"Start", "Resume", "Settings"};
    }
    const std::string aa = Property<std::string>("vfx.antialiasing");
    const std::string aaLabel = aa == "ssaa" ? "SSAA" : aa == "fxaa" ? "FXAA" : "Off";
    return {
        std::string("VSync: ") + (Property<bool>("window.vsync") ? "On" : "Off"),
        "Antialiasing: " + aaLabel,
        std::string("Flight HUD: ") + (Property<bool>("vfx.drawhud") ? "On" : "Off"),
        std::string("Invulnerability: ") +
            (Property<bool>("general.playerShipUndestroyable") ? "On" : "Off"),
        "Back"
    };
}

bool MainMenu::enabled(int index) const {
    if (index < 0 || index >= static_cast<int>(labels().size())) return false;
    if (m_state.page() == MainMenuState::Page::Settings) return true;
    return index == 0 ? m_state.startEnabled()
        : index == 1 ? m_state.resumeEnabled() : true;
}

void MainMenu::selectNext(int direction) {
    const int count = static_cast<int>(labels().size());
    for (int attempt = 0; attempt < count; ++attempt) {
        m_selected = (m_selected + direction + count) % count;
        if (enabled(m_selected)) return;
    }
}

float MainMenu::buttonTop() const {
    return m_state.page() == MainMenuState::Page::Settings ? 0.36f : ButtonTop;
}

float MainMenu::buttonStep() const {
    return m_state.page() == MainMenuState::Page::Settings ? 0.08f : ButtonStep;
}

int MainMenu::itemAtPointer() const {
    auto* window = glfwGetCurrentContext();
    int width, height;
    double x, y;
    glfwGetWindowSize(window, &width, &height);
    glfwGetCursorPos(window, &x, &y);
    if (width <= 0 || height <= 0) return -1;
    x /= width;
    y /= height;
    if (x < ButtonLeft || x > ButtonLeft + ButtonWidth) return -1;
    const int index = static_cast<int>((y - buttonTop()) / buttonStep());
    if (y < buttonTop() || y > buttonTop() + index * buttonStep() + ButtonHeight ||
        index >= static_cast<int>(labels().size())) return -1;
    return index;
}

void MainMenu::update(float deltaSec) {
    (void)deltaSec;
    double x, y;
    glfwGetCursorPos(glfwGetCurrentContext(), &x, &y);
    const glm::dvec2 pointer(x, y);
    if (pointer != m_lastPointer) {
        m_lastPointer = pointer;
        const int item = itemAtPointer();
        if (enabled(item)) m_selected = item;
    }
}

void MainMenu::onKeyEvent(int key, int scancode, int action, int mods) {
    (void)scancode;
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_UP) selectNext(-1);
    if (key == GLFW_KEY_DOWN) selectNext(1);
    if (key == GLFW_KEY_TAB) selectNext(mods & GLFW_MOD_SHIFT ? -1 : 1);
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_SPACE) activate(m_selected);
}

void MainMenu::onMouseButtonEvent(int button, int action, int mods) {
    (void)mods;
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    const int item = itemAtPointer();
    if (action == GLFW_PRESS) {
        m_pressed = enabled(item) ? item : -1;
    } else if (action == GLFW_RELEASE) {
        const int pressed = m_pressed;
        m_pressed = -1;
        if (pressed >= 0 && item == pressed) activate(item);
    }
}

void MainMenu::activate(int index) {
    if (!enabled(index)) return;
    if (m_state.page() != MainMenuState::Page::Settings) {
        if (index == 0) m_state.start();
        if (index == 1) m_state.resume();
        if (index == 2) {
            m_state.settings();
            m_selected = 0;
        }
        return;
    }
    if (index == 4) {
        m_state.show();
        m_selected = 2;
        return;
    }
    if (index == 0) {
        const bool vsync = !Property<bool>("window.vsync").get();
        glfwSwapInterval(vsync ? 1 : 0);
        const char* description = nullptr;
        if (glfwGetError(&description) != GLFW_NO_ERROR) {
            glow::warning("Could not change VSync: %;", description);
            m_settingsError = "Could not change VSync. See the log for details.";
            return;
        }
        Property<bool>("window.vsync").set(vsync);
    } else if (index == 1) {
        Property<std::string> aa("vfx.antialiasing");
        aa.set(aa.get() == "ssaa" ? "fxaa" : aa.get() == "fxaa" ? "none" : "ssaa");
    } else if (index == 2) {
        Property<bool> hud("vfx.drawhud");
        hud.set(!hud.get());
    } else {
        Property<bool> protection("general.playerShipUndestroyable");
        protection.set(!protection.get());
    }
    saveSettings();
}

void MainMenu::saveSettings() {
    std::ofstream file(FileSystem::userConfigDir() + "/settings.ini");
    file << "[window]\nvsync=" << (Property<bool>("window.vsync") ? "true" : "false")
        << "\n[vfx]\nantialiasing=" << Property<std::string>("vfx.antialiasing").get()
        << "\ndrawhud=" << (Property<bool>("vfx.drawhud") ? "true" : "false")
        << "\n[general]\nplayerShipUndestroyable="
        << (Property<bool>("general.playerShipUndestroyable") ? "true" : "false") << '\n';
    file.close();
    if (!file) {
        glow::warning("Could not save menu settings to %;", FileSystem::userConfigDir());
        m_settingsError = "Settings applied, but could not be saved. See the log.";
    } else {
        m_settingsError.clear();
    }
}

void MainMenu::initialize() {
    m_program = new glow::Program();
    m_program->attach(
        glowutils::createShaderFromFile(GL_VERTEX_SHADER, "data/shader/hud/hudoverlay.vert"),
        glowutils::createShaderFromFile(GL_FRAGMENT_SHADER, "data/shader/hud/hudoverlay.frag"));
}

void MainMenu::rectangle(const glm::vec4& bounds, const glm::vec3& color) {
    m_program->use();
    m_program->setUniform("panelBounds", glm::vec4(
        bounds.x * 2.0f - 1.0f, 1.0f - (bounds.y + bounds.w) * 2.0f,
        bounds.z * 2.0f, bounds.w * 2.0f));
    m_program->setUniform("panelColor", color);
    m_program->setUniform("panelOpacity", 1.0f);
    m_quad->draw();
}

void MainMenu::draw() {
    if (!m_program) initialize();
    int width, height;
    glfwGetFramebufferSize(glfwGetCurrentContext(), &width, &height);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    rectangle(glm::vec4(0, 0, 1, 1), Background);
    rectangle(glm::vec4(0.08f, 0.12f, 0.002f, 0.72f), Accent * 0.35f);
    rectangle(glm::vec4(0.918f, 0.12f, 0.002f, 0.72f), Accent * 0.35f);
    rectangle(glm::vec4(0.30f, 0.355f, 0.40f, 0.405f), Surface * 0.70f);
    const auto items = labels();
    for (int index = 0; index < static_cast<int>(items.size()); ++index) {
        const glm::vec4 bounds(ButtonLeft, buttonTop() + index * buttonStep(),
            ButtonWidth, ButtonHeight);
        const bool selected = index == m_selected && enabled(index);
        rectangle(bounds, selected ? Accent * 0.55f : Surface);
        if (selected) {
            rectangle(bounds + glm::vec4(0.002f, 0.002f, -0.004f, -0.004f),
                glm::vec3(0.065f, 0.15f, 0.20f));
        }
    }
    m_program->release();
    auto* font = UiFont::instance();
    font->drawScreen("VOXELLANCER", glm::vec2(0.5f, 0.19f), 0.052f,
        glm::vec4(0.90f, 0.97f, 1.0f, 1), FontAlign::CENTER, 0.8f);
    const bool settings = m_state.page() == MainMenuState::Page::Settings;
    font->drawScreen(settings ? "SETTINGS" : "CONTRACT OPERATIONS",
        glm::vec2(0.5f, 0.285f), 0.019f, glm::vec4(Accent, 1),
        FontAlign::CENTER, 0.8f);
    for (int index = 0; index < static_cast<int>(items.size()); ++index) {
        font->drawScreen(items[index],
            glm::vec2(0.5f, buttonTop() + index * buttonStep() + 0.019f), 0.025f,
            enabled(index) ? glm::vec4(0.9f, 0.96f, 1, 1)
                : glm::vec4(0.40f, 0.47f, 0.53f, 1),
            FontAlign::CENTER, ButtonWidth - 0.03f);
    }
    const std::string help = !m_settingsError.empty() ? m_settingsError
        : settings && m_selected == 3
            ? "Invulnerability protects only your pilot. Saved across launches."
        : settings ? "Settings apply immediately and are saved on this computer."
        : m_state.sessionStarted() && !m_state.resumeEnabled()
            ? "Session unavailable. Start and Resume require a ready server."
        : m_state.sessionStarted() ? "Session active. Use Resume. Online simulation continues in the menu."
        : "Start opens the contract. Solo runs continue from the latest base checkpoint.";
    font->drawScreen(help, glm::vec2(0.5f, 0.81f), 0.017f,
        glm::vec4(0.65f, 0.75f, 0.82f, 1), FontAlign::CENTER, 0.85f);
    font->drawScreen("Arrow keys / Tab: select    Enter: confirm    Esc: menu / back",
        glm::vec2(0.5f, 0.865f), 0.014f, glm::vec4(0.65f, 0.75f, 0.82f, 1),
        FontAlign::CENTER, 0.85f);
    rectangle(glm::vec4(0.08f, 0.925f, 0.84f, 0.0015f), Accent * 0.35f);
    font->drawScreen(m_note, glm::vec2(0.5f, 0.95f), 0.016f,
        glm::vec4(0.75f, 0.85f, 0.90f, 1), FontAlign::CENTER, 0.88f);
}

void MainMenu::beforeContextDestroy() {
    m_program = nullptr;
}

void MainMenu::afterContextRebuild() {
}
