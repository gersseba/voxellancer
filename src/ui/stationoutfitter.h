#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <map>
#include <glow/ref_ptr.h>

#include "etc/contextdependant.h"
#include "protocol/serverprotocol.h"
#include "equipment/weapontuning.h"
#include "configurationmenu.h"

namespace glow { class Program; class Buffer; class Texture; class VertexArrayObject; }
class ScreenQuad;
class Ship;
class CombatEffects;
class SettingsStore;

class StationOutfitter : public ContextDependant {
public:
    using Submit = std::function<std::optional<std::uint64_t>(
        voxellancer::protocol::ContractActionKind, std::uint8_t, glm::ivec3, std::uint8_t,
        voxellancer::protocol::RewardChoice, std::uint8_t)>;
    StationOutfitter(SettingsStore& settings, CombatEffects& effects, Submit submit);
    ~StationOutfitter();
    void update(const std::optional<voxellancer::protocol::ContractStatus>& status,
        const voxellancer::protocol::EntityContentSnapshot* ship, float deltaSec);
    void draw();
    bool visible() const { return m_station && m_visible; }
    bool equipmentVisible() const { return visible() && m_equipment; }
    bool closeEquipment();
    bool key(int key);
    bool mouse(int button, int action, glm::vec2 pointer);
    void setDeveloper(bool enabled);
    bool developerVisible() const { return m_developer && visible(); }
    bool developerActive() const { return m_developer; }
    void updateDeveloper(Ship* ship, float deltaSec);
    void refreshDeveloper(World& world);
    bool effectsVisible() const { return developerVisible() && m_effects; }
    std::size_t effectGroup() const { return m_effectGroup; }

private:
    struct Button { glm::vec4 bounds; std::string label; int id; bool enabled; };
    Submit m_submit;
    SettingsStore& m_settings;
    CombatEffects& m_combatEffects;
    voxellancer::protocol::ContractStatus m_status;
    std::vector<voxellancer::protocol::EntityVoxelState> m_voxels;
    std::vector<Button> m_buttons;
    std::vector<glm::vec2> m_mountPositions;
    bool m_station = false, m_visible = true, m_equipment = false, m_sendFailed = false;
    bool m_map = false;
    bool m_defense = false;
    bool m_market = false, m_bar = false;
    bool m_developer = false, m_tuning = false;
    bool m_effects = false;
    bool m_saveAttempted = false;
    std::string m_saveError;
    std::size_t m_effectGroup = 0;
    int m_tuningField = 0, m_slider = -1;
    std::map<std::string, WeaponTuning> m_tunings;
    std::uint8_t m_shieldSelection = 1;
    int m_mount = -1, m_weapon = 0, m_pressed = -1, m_hover = -1;
    int m_rewardSelection = 0;
    float m_rotation = 0.6f, m_pendingTime = 0;
    std::uint64_t m_pending = 0;
    std::string m_message;
    std::unique_ptr<ScreenQuad> m_quad;
    glow::ref_ptr<glow::Program> m_panel, m_wire, m_backgroundProgram;
    std::array<glow::ref_ptr<glow::Texture>, 5> m_backgrounds;
    glow::ref_ptr<glow::Buffer> m_buffer;
    glow::ref_ptr<glow::VertexArrayObject> m_array;
    std::vector<glm::vec2> m_lines;
    glm::vec3 m_center{0};
    float m_scale = 1, m_aspect = 1.6f;
    void initialize();
    void layout();
    void activate(int id);
    void request(voxellancer::protocol::ContractActionKind kind,
        std::uint8_t weapon = 0, glm::ivec3 cell = glm::ivec3(0),
        std::uint8_t node = voxellancer::protocol::NoRouteNode,
        voxellancer::protocol::RewardChoice choice = voxellancer::protocol::RewardChoice::None,
        std::uint8_t shield = 0);
    void rectangle(glm::vec4 bounds, glm::vec3 color, float opacity = 1.0f);
    void background(std::size_t index, glm::vec4 bounds);
    void text(const std::string& value, glm::vec2 position,
        float height = 0.018f, glm::vec3 color = glm::vec3(0.85f,0.93f,1),
        float width = 0.85f);
    glm::vec2 project(glm::vec3 cell) const;
    void drawShip();
    void drawDeveloper();
    ConfigurationMenu configuration();
    ConfigurationLayout configurationLayout() const;
    void configurationChanged();
    void drawConfiguration();
    WeaponTuning& tuning();
    void tuneSlider(int index, float x);
    void drawRoute();
    void drawRewards();
    void drawSummary();
    void drawDefense();
    void drawFlightResources();
    void drawLines(glm::vec4 color);
    glm::vec2 nodePosition(const voxellancer::protocol::RouteNode& node) const;
    void drawStats(std::uint8_t weapon, glm::vec2 position,
        std::uint8_t comparison = 0, float scale = 1.0f);
    void beforeContextDestroy() override;
    void afterContextRebuild() override {}
};
