#include "mainmenu.h"

#include <algorithm>
#include <utility>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glow/Program.h>
#include <glowutils/global.h>

#include "display/rendering/screenquad.h"
#include "display/rendering/shaderloader.h"
#include "property/property.h"
#include "ui/uifont.h"
#include "ui/scenariomenu.h"
#include "settings/settingsstore.h"
namespace {
    struct Preference {
        std::string label, group, key;
        std::vector<std::pair<std::string,std::string>> choices;
        std::string property() const { return group+"."+key; }
        std::string value() const {
            return choices.empty() ? (Property<bool>(property()).get() ? "true" : "false") :
                Property<std::string>(property()).get();
        }
    };
    const std::vector<Preference> Preferences{
        {"VSync","window","vsync",{}},
        {"Antialiasing","vfx","antialiasing",{{"ssaa","SSAA"},{"fxaa","FXAA"},{"none","Off"}}},
        {"Flight HUD","vfx","drawhud",{}},
        {"Invulnerability","general","playerShipUndestroyable",{}},
        {"Music","audio","musicEnabled",{}},
        {"Sound effects","audio","effectsEnabled",{}},
        {"Shield shape","gameplay","shieldShape",{{"SPHERE","Sphere"},{"HULL","Hull"}}}
    };
    constexpr float ButtonLeft = 0.34f;
    constexpr float ButtonWidth = 0.32f;
    constexpr float ButtonHeight = 0.065f;
    constexpr float ButtonStep = 0.09f;
    const glm::vec3 Background(0.025f, 0.045f, 0.075f);
    const glm::vec3 Surface(0.055f, 0.09f, 0.13f);
    const glm::vec3 Accent(0.30f, 0.80f, 0.90f);
}

MainMenu::MainMenu(SettingsStore& settings, std::function<void()> contracts, std::function<void(int)> scenario):
    m_settings(settings),
    m_contracts(std::move(contracts)),
    m_scenario(std::move(scenario)),
    m_quad(new ScreenQuad()) {}
MainMenu::~MainMenu() = default;

bool MainMenu::visible() const {
    return m_state.visible();
}

void MainMenu::toggle() {
    if (m_state.page() == MainMenuState::Page::Settings ||
        m_state.page() == MainMenuState::Page::Scenarios) {
        m_state.show();
        m_selected = 1;
    } else if (visible()) {
        m_state.resume();
    } else {
        m_state.show();
        m_selected = m_state.resumeEnabled() ? 2 : 1;
    }
    m_pressed = -1;
}

void MainMenu::setConnection(bool ready, const std::string& note) {
    m_state.setConnectionReady(ready);
    m_note = note;
    if (!enabled(m_selected)) {
        selectNext(1);
    }
}

std::vector<std::string> MainMenu::labels() const {
    if (m_state.page() == MainMenuState::Page::Scenarios) {
        std::vector<std::string> items;
        for (const auto& entry : ScenarioMenuEntries) items.emplace_back(entry.name);
        items.emplace_back("Back");
        return items;
    }
    if (m_state.page() != MainMenuState::Page::Settings) {
        return {"Contracts", "Scenarios", "Resume", "Settings", "Exit"};
    }
    std::vector<std::string> items;
    for (const auto& preference : Preferences) {
        const auto value = preference.value();
        std::string label = value == "true" ? "On" : "Off";
        if (!preference.choices.empty()) {
            const auto found = std::find_if(preference.choices.begin(),preference.choices.end(),
                [&](const auto& choice) { return choice.first == value; });
            label = found == preference.choices.end() ? "Unknown: "+value : found->second;
        }
        items.push_back(preference.label+": "+label);
    }
    items.emplace_back("Save settings");
    items.emplace_back("Back");
    return items;
}

bool MainMenu::enabled(int index) const {
    if (index < 0 || index >= static_cast<int>(labels().size())) return false;
    if (m_state.page() == MainMenuState::Page::Settings && index == static_cast<int>(Preferences.size()))
        return m_settings.status() != SettingsStore::Status::Saving;
    if (m_state.page() != MainMenuState::Page::Main) return true;
    return index == 0 ? m_state.contractsEnabled()
        : index == 2 ? m_state.resumeEnabled() : true;
}

void MainMenu::selectNext(int direction) {
    const int count = static_cast<int>(labels().size());
    for (int attempt = 0; attempt < count; ++attempt) {
        m_selected = (m_selected + direction + count) % count;
        if (enabled(m_selected)) return;
    }
}

float MainMenu::buttonTop() const {
    return m_state.page() != MainMenuState::Page::Main ? 0.31f
        : 0.36f;
}

float MainMenu::buttonStep() const {
    return m_state.page() == MainMenuState::Page::Scenarios ? 0.047f
        : m_state.page() == MainMenuState::Page::Settings ? 0.055f : ButtonStep;
}

float MainMenu::buttonHeight() const {
    return m_state.page() == MainMenuState::Page::Scenarios ? 0.041f :
        m_state.page() == MainMenuState::Page::Settings ? 0.052f : ButtonHeight;
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
    if (y < buttonTop() || y > buttonTop() + index * buttonStep() + buttonHeight() ||
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
    if (key == GLFW_KEY_P && m_state.page() == MainMenuState::Page::Main &&
        m_state.scenarioActive()) m_state.resume();
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
    if (m_state.page() == MainMenuState::Page::Scenarios) {
        if (index == static_cast<int>(ScenarioMenuEntries.size())) {
            m_state.show();
            m_selected = 1;
        } else {
            m_scenario(ScenarioMenuEntries[index].id);
            m_state.startScenario();
        }
        return;
    }
    if (m_state.page() == MainMenuState::Page::Main) {
        if (index == 0) {
            m_contracts();
            m_state.selectContracts();
        }
        if (index == 1) {
            m_state.scenarios();
            m_selected = 0;
        }
        if (index == 2) m_state.resume();
        if (index == 3) {
            m_state.settings();
            m_selected = 0;
        }
        if (index == 4) glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);
        return;
    }
    if (index == static_cast<int>(Preferences.size()+1)) {
        m_state.show();
        m_selected = 3;
        return;
    }
    if (index == static_cast<int>(Preferences.size())) { saveSettings(); return; }
    m_saveAttempted = false;
    m_settingsError.clear();
    const auto& preference = Preferences.at(index);
    if (preference.property() == "window.vsync") {
#ifdef __EMSCRIPTEN__
        m_settingsError = "Display timing is controlled by the browser.";
        return;
#else
        const bool vsync = !Property<bool>("window.vsync").get();
        glfwSwapInterval(vsync ? 1 : 0);
        const char* description = nullptr;
        if (glfwGetError(&description) != GLFW_NO_ERROR) {
            glow::warning("Could not change VSync: %;", description);
            m_settingsError = "Could not change VSync. See the log for details.";
            return;
        }
        Property<bool>("window.vsync").set(vsync);
#endif
    } else if (!preference.choices.empty()) {
        const auto current = std::find_if(preference.choices.begin(),preference.choices.end(),
            [&](const auto& choice) { return choice.first == preference.value(); });
        if (current == preference.choices.end())
            glow::warning("Replacing unknown preference value for %;",preference.property());
        const auto next = current == preference.choices.end() || current+1 == preference.choices.end()
            ? preference.choices.begin() : current+1;
        Property<std::string>(preference.property()).set(next->first);
    } else {
        Property<bool> toggle(preference.property());
        toggle.set(!toggle.get());
    }
}

void MainMenu::saveSettings() {
    try {
        SettingsStore::Groups groups;
        for (const auto& preference : Preferences)
            groups[preference.group][preference.key] = preference.value();
        m_settings.save(groups);
        m_saveAttempted = true;
        m_settingsError.clear();
    } catch (const std::exception& error) {
        glow::warning("Could not save settings: %;",error.what());
        m_settingsError = "Could not save settings. See the log and retry.";
    }
}

void MainMenu::initialize() {
    m_program = new glow::Program();
    m_program->attach(
        createShaderFromFile(GL_VERTEX_SHADER, "data/shader/hud/hudoverlay.vert"),
        createShaderFromFile(GL_FRAGMENT_SHADER, "data/shader/hud/hudoverlay.frag"));
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
    const bool scenarios = m_state.page() == MainMenuState::Page::Scenarios;
    const bool settings = m_state.page() == MainMenuState::Page::Settings;
    rectangle(scenarios || settings ? glm::vec4(0.30f, 0.30f, 0.40f, 0.53f)
                        : glm::vec4(0.30f, 0.35f, 0.40f, 0.44f), Surface * 0.70f);
    const auto items = labels();
    for (int index = 0; index < static_cast<int>(items.size()); ++index) {
        const glm::vec4 bounds(ButtonLeft, buttonTop() + index * buttonStep(),
            ButtonWidth, buttonHeight());
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
    font->drawScreen(settings ? "SETTINGS" : scenarios ? "SCENARIO SELECTION" : "MAIN MENU",
        glm::vec2(0.5f, scenarios || settings ? 0.265f : 0.285f), 0.019f, glm::vec4(Accent, 1),
        FontAlign::CENTER, 0.8f);
    for (int index = 0; index < static_cast<int>(items.size()); ++index) {
        font->drawScreen(items[index],
            glm::vec2(0.5f, buttonTop() + index * buttonStep() +
                (scenarios || settings ? 0.010f : 0.019f)), scenarios || settings ? 0.019f : 0.025f,
            enabled(index) ? glm::vec4(0.9f, 0.96f, 1, 1)
                : glm::vec4(0.40f, 0.47f, 0.53f, 1),
            FontAlign::CENTER, ButtonWidth - 0.03f);
    }
    const std::string help = !m_settingsError.empty() ? m_settingsError
        : settings && m_saveAttempted && m_settings.status() == SettingsStore::Status::Saving
            ? "Saving settings in this browser..."
        : settings && m_saveAttempted && m_settings.status() == SettingsStore::Status::Failed
            ? "Could not save in this browser. Check storage permissions and retry."
        : settings && m_saveAttempted ? "Settings saved."
        : settings && m_selected == 3
            ? "Invulnerability protects only your pilot. Use Save settings to keep it."
        : settings && m_selected == 6
            ? "Shield shape applies to the next local scenario or Contracts sortie."
        : settings ? "Changes apply live except shield shape. Save settings to keep them."
        : scenarios ? "Choose a standalone scenario. The contract server continues running."
#ifdef __EMSCRIPTEN__
        : "Contracts and scenarios run locally. Scenarios pause in the menu.";
#else
        : "Contracts require a server. Scenarios run locally and pause in the menu.";
#endif
    font->drawScreen(help, glm::vec2(0.5f, scenarios || settings ? 0.84f : 0.81f), 0.017f,
        glm::vec4(0.65f, 0.75f, 0.82f, 1), FontAlign::CENTER, 0.85f);
    font->drawScreen("Arrow keys / Tab: select    Enter: confirm    Esc: menu / back",
        glm::vec2(0.5f, scenarios || settings ? 0.89f : 0.865f), 0.014f, glm::vec4(0.65f, 0.75f, 0.82f, 1),
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
