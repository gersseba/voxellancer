#include "stationoutfitter.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glow/Buffer.h>
#include <glow/Program.h>
#include <glow/VertexArrayObject.h>
#include <glow/VertexAttributeBinding.h>
#include <glowutils/global.h>

#include "display/rendering/screenquad.h"
#include "equipment/weaponcatalog.h"
#include "equipment/shieldcatalog.h"
#include "property/property.h"
#include "simulation/contractlevel.h"
#include "ui/uifont.h"
#include "utils/simulationtickrunner.h"

using namespace voxellancer::protocol;
namespace {
    const glm::vec3 Accent(0.30f,0.83f,0.90f), Muted(0.48f,0.63f,0.71f);
    const glm::vec3 Panel(0.035f,0.065f,0.10f), Surface(0.055f,0.105f,0.15f);
    bool contains(glm::vec4 r, glm::vec2 p) {
        return p.x >= r.x && p.x <= r.x+r.z && p.y >= r.y && p.y <= r.y+r.w;
    }
    std::string number(float value, int precision = 0) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(precision) << value;
        return stream.str();
    }
    std::string resultText(ContractResult result) {
        switch (result) {
        case ContractResult::None: return "WELCOME ABOARD. PREPARE YOUR NEXT SORTIE.";
        case ContractResult::Accepted: return "SERVER CONFIRMED - READY WHEN YOU ARE";
        case ContractResult::InsufficientCredits: return "NOT ENOUGH CREDITS - COMPLETE A CONTRACT";
        case ContractResult::AlreadyOwned: return "OFFER SOLD OR EQUIPMENT ALREADY FITTED";
        case ContractResult::WrongPhase: return "ACTION UNAVAILABLE DURING THIS RUN PHASE";
        case ContractResult::Incompatible: return "THAT EQUIPMENT DOES NOT FIT THIS SHIP";
        case ContractResult::StaleRequest: return "SESSION CHANGED - PLEASE TRY AGAIN";
        case ContractResult::Unavailable: return "WEAPON OR PILOT NO LONGER AVAILABLE";
        case ContractResult::MissionCompleted: return "ATTACK COMPLETE - SURVIVORS MAY CHOOSE SALVAGE";
        case ContractResult::MissionAborted: return "CONTRACT ABORTED - NO REWARD";
        case ContractResult::InventoryFull: return "INVENTORY FULL - 64 STORED WEAPONS MAXIMUM";
        case ContractResult::InvalidSlot: return "MOUNT NO LONGER EXISTS - SELECT ANOTHER";
        case ContractResult::InvalidRoute: return "CHOOSE A CONNECTED DESTINATION";
        case ContractResult::HullIntact: return "NO SURVIVING HULL BLOCKS NEED REPAIR";
        case ContractResult::RewardAlreadyClaimed: return "YOUR REWARD HAS ALREADY BEEN CLAIMED";
        case ContractResult::InvalidReward: return "THIS REWARD IS UNAVAILABLE - CHOOSE ANOTHER";
        case ContractResult::RunDefeated: return "ALL PILOTS LOST - RUN ENDED";
        }
        return "UNKNOWN SERVER RESULT";
    }
}

StationOutfitter::StationOutfitter(Submit submit):
    m_submit(std::move(submit)), m_quad(new ScreenQuad()) {}
StationOutfitter::~StationOutfitter() = default;

void StationOutfitter::update(const std::optional<ContractStatus>& state,
    const EntityContentSnapshot* ship, float deltaSec) {
    const bool station = state && state->phase != ContractPhase::Mission;
    if (station != m_station || (state && state->phase != m_status.phase) ||
        (state && state->currentNode != m_status.currentNode) ||
        (state && state->run != m_status.run) ||
        (state && state->alive != m_status.alive)) {
        m_visible = station;
        m_equipment = false;
        m_defense = false;
        m_mount = -1;
        m_pending = 0;
        m_sendFailed = false;
        m_map = state && (state->phase != ContractPhase::Station || !state->alive);
        m_rewardSelection = 0;
    }
    m_station = station;
    if (!state) { m_status = ContractStatus{}; return; }
    m_status = *state;
    int width, height;
    glfwGetWindowSize(glfwGetCurrentContext(), &width, &height);
    if (width > 0 && height > 0) m_aspect = static_cast<float>(width) / height;
    if (m_pending) {
        m_pendingTime += deltaSec;
        if (state->acknowledgedSequence >= m_pending) {
            m_pending = 0;
            m_message = resultText(state->result);
        }
    } else if (!m_sendFailed) {
        m_message = resultText(state->result);
    }
    m_voxels = ship ? ship->voxels : std::vector<EntityVoxelState>{};
    if (m_mount >= static_cast<int>(m_status.mounts.size())) m_mount = -1;
    if (m_weapon > 0 && !m_status.inventory[m_weapon-1]) m_weapon = 0;
    if (!m_voxels.empty()) {
        glm::vec3 lower(255), upper(0);
        for (const auto& voxel : m_voxels) {
            lower = glm::min(lower, glm::vec3(voxel.cell));
            upper = glm::max(upper, glm::vec3(voxel.cell));
        }
        m_center = (lower + upper) * 0.5f;
        m_scale = std::min(0.32f, 0.42f/m_aspect) /
            std::max(1.0f, glm::length(upper - lower));
    }
    layout();
}

void StationOutfitter::layout() {
    m_buttons.clear();
    const bool idle = m_pending == 0;
    m_buttons.push_back({{0.87f,0.12f,0.065f,0.04f},"HIDE",1,true});
    if (m_status.phase == ContractPhase::Complete || m_status.phase == ContractPhase::Defeat) {
        m_buttons.push_back({{0.61f,0.855f,0.32f,0.06f},
            m_status.ready ? "WAITING FOR PARTY" : "NEW RUN - ENTER",23,idle && !m_status.ready});
    } else if (m_status.phase == ContractPhase::Reward) {
        unsigned count = 0;
        for (auto quantity : m_status.inventory) count += quantity;
        for (int i = 0; i < 3; ++i)
            m_buttons.push_back({{0.085f+i*0.29f,0.72f,0.25f,0.065f},
                !m_status.alive ? "SPECTATING" : m_status.rewardClaimed ? "CHOICE LOCKED" :
                    i == 1 && count >= ContractInventoryCapacity ? "INVENTORY FULL" :
                    "CLAIM - "+std::to_string(i+1),20+i,
                idle && m_status.alive && !m_status.rewardClaimed &&
                    (i != 1 || count < ContractInventoryCapacity)});
    } else if (m_map) {
        std::string destination = "TRAVEL - ENTER";
        if (m_status.selectedNode < m_status.route.size()) {
            const auto& node = m_status.route[m_status.selectedNode];
            destination = node.kind == RouteNodeKind::Base ? "TRAVEL - BASE" :
                node.kind == RouteNodeKind::Encounter ? "INVESTIGATE - ENTER" :
                    "TRAVEL - ATK "+std::to_string(node.level);
        }
        m_buttons.push_back({{0.69f,0.855f,0.24f,0.06f},
            m_status.phase == ContractPhase::Complete ? "ROUTE COMPLETE" :
            m_status.ready ? "WAITING FOR PARTY" : destination,9,
            idle && m_status.alive && !m_status.ready && m_status.selectedNode != NoRouteNode &&
                m_status.phase != ContractPhase::Complete});
        if (m_status.phase == ContractPhase::Station && m_status.alive)
            m_buttons.push_back({{0.065f,0.855f,0.23f,0.06f},"BACK TO BASE - M",10,true});
        for (std::size_t i = 0; i < m_status.route.size(); ++i) {
            const auto& node = m_status.route[i];
            const auto p = nodePosition(node);
            const bool reachable = m_status.currentNode < m_status.route.size() &&
                (m_status.route[m_status.currentNode].links & (1u << i));
            const auto label = node.kind == RouteNodeKind::Attack ? "ATK "+std::to_string(node.level)
                : node.kind == RouteNodeKind::Base ? "BASE" : "SIGNAL ?";
            m_buttons.push_back({{p.x-0.032f,p.y-0.028f,0.064f,0.056f},label,
                300+static_cast<int>(i),idle && m_status.alive && reachable &&
                    m_status.phase != ContractPhase::Complete});
        }
    } else if (m_defense) {
        for (std::uint8_t id = 1; id <= ShieldCatalogSize; ++id)
            m_buttons.push_back({{0.085f+(id-1)*0.29f,0.64f,0.25f,0.06f},
                m_shieldSelection == id ? "SELECTED" : "SELECT - "+std::to_string(id),
                29+id,idle});
        const auto selected = catalogShield(m_shieldSelection);
        m_buttons.push_back({{0.065f,0.855f,0.23f,0.06f},"BACK TO BASE - N",3,true});
        m_buttons.push_back({{0.64f,0.855f,0.29f,0.06f},
            m_status.shieldBuild == m_shieldSelection ? "CURRENTLY EQUIPPED" :
            "BUY AND EQUIP - "+std::to_string(selected.price)+" CR",33,
            idle && m_status.alive && m_status.shieldBuild != m_shieldSelection &&
                m_status.credits >= selected.price});
    } else if (!m_equipment) {
        unsigned count = 0;
        for (auto quantity : m_status.inventory) count += quantity;
        m_buttons.push_back({{0.085f,0.65f,0.32f,0.06f},"EQUIPMENT - E",2,true});
        m_buttons.push_back({{0.085f,0.73f,0.32f,0.055f},
            "SHIELDS / ENERGY - N",3,true});
        m_buttons.push_back({{0.085f,0.56f,0.32f,0.06f},
            m_status.repairPrice == 0 ? "NO REPAIR NEEDED" :
                "REPAIR - H / "+std::to_string(m_status.repairPrice)+" CR",34,
            idle && m_status.alive && !m_status.ready && m_status.repairPrice > 0 &&
                m_status.credits >= m_status.repairPrice});
        m_buttons.push_back({{0.51f,0.68f,0.395f,0.065f},
            m_status.gun ? "SOLD - NEXT OFFER AT ANOTHER BASE" :
            "BUY - " + std::to_string(m_status.gunPrice) + " CR",4,
            idle && !m_status.gun && count < ContractInventoryCapacity &&
            m_status.credits >= m_status.gunPrice});
        m_buttons.push_back({{0.69f,0.855f,0.24f,0.06f},
            "ROUTE MAP - ENTER",5,idle});
    } else {
        m_buttons.push_back({{0.065f,0.855f,0.22f,0.06f},"BACK TO BASE - E",2,true});
        m_buttons.push_back({{0.30f,0.855f,0.09f,0.06f},"Q - TURN",6,true});
        m_buttons.push_back({{0.40f,0.855f,0.09f,0.06f},"R - TURN",7,true});
        const bool canEquip = idle && m_mount >= 0 && m_weapon > 0 &&
            m_status.inventory[m_weapon-1] > 0 &&
            m_status.mounts[m_mount].weapon != m_weapon;
        m_buttons.push_back({{0.70f,0.855f,0.23f,0.06f},"FIT WEAPON - ENTER",8,canEquip});
        for (int i = 0; i < WeaponCatalogSize; ++i) {
            const auto weapon = catalogWeapon(i+1);
            m_buttons.push_back({{0.63f,0.31f+i*0.052f,0.275f,0.047f},
                weapon.name + "  x" + std::to_string(m_status.inventory[i]),
                100+i, m_status.inventory[i] > 0 && idle});
        }
    }
    m_mountPositions.clear();
    for (const auto& mount : m_status.mounts) m_mountPositions.push_back(project(glm::vec3(mount.cell)));
}

void StationOutfitter::request(ContractActionKind kind, std::uint8_t weapon, glm::ivec3 cell,
    std::uint8_t node, RewardChoice choice, std::uint8_t shield) {
    if (m_pending) return;
    const auto sequence = m_submit(kind, weapon, cell, node, choice, shield);
    if (!sequence) {
        m_sendFailed = true;
        m_message = "COULD NOT SEND REQUEST - CHECK CONNECTION";
        return;
    }
    m_sendFailed = false;
    m_pending = *sequence;
    m_pendingTime = 0;
    layout();
}

void StationOutfitter::activate(int id) {
    const auto button = std::find_if(m_buttons.begin(), m_buttons.end(),
        [id](const auto& b) { return b.id == id; });
    if (button == m_buttons.end() || !button->enabled) return;
    if (id == 1) { m_visible = false; m_equipment = false; }
    if (id == 2) m_equipment = !m_equipment;
    if (id == 3) { m_defense = !m_defense; m_equipment = false; }
    if (id == 4) request(ContractActionKind::BuyGun);
    if (id == 5) m_map = true;
    if (id == 9) request(ContractActionKind::Ready);
    if (id == 10) m_map = false;
    if (id >= 20 && id <= 22) {
        m_rewardSelection = id - 20;
        request(ContractActionKind::ClaimReward,0,glm::ivec3(0),NoRouteNode,
            static_cast<RewardChoice>(id-19));
    }
    if (id == 23) request(ContractActionKind::NewRun);
    if (id >= 30 && id <= 32) m_shieldSelection = static_cast<std::uint8_t>(id-29);
    if (id == 33) request(ContractActionKind::BuyShield,0,glm::ivec3(0),
        NoRouteNode,RewardChoice::None,m_shieldSelection);
    if (id == 34) request(ContractActionKind::RepairHull);
    if (id == 6) m_rotation -= 0.4f;
    if (id == 7) m_rotation += 0.4f;
    if (id >= 100 && id < 200) m_weapon = id - 99;
    if (id >= 300) request(ContractActionKind::SelectNode,0,glm::ivec3(0),
        static_cast<std::uint8_t>(id-300));
    if (id == 8) request(ContractActionKind::EquipGun,
        static_cast<std::uint8_t>(m_weapon), m_status.mounts[m_mount].cell);
    layout();
}

bool StationOutfitter::closeEquipment() {
    if (visible() && m_defense) {
        m_defense = false;
        layout();
        return true;
    }
    if (!m_status.alive || m_status.phase == ContractPhase::Reward ||
        m_status.phase == ContractPhase::Complete || m_status.phase == ContractPhase::Defeat)
        return false;
    if (visible() && m_map && m_status.phase == ContractPhase::Station) {
        m_map = false;
        layout();
        return true;
    }
    if (!equipmentVisible()) return false;
    m_equipment = false;
    layout();
    return true;
}

bool StationOutfitter::key(int key) {
    if (!m_station) return false;
    if (key == GLFW_KEY_TAB || key == GLFW_KEY_O) {
        m_visible = key == GLFW_KEY_O || !m_visible;
        if (!m_visible) m_equipment = false;
        return true;
    }
    if (m_status.phase == ContractPhase::Complete || m_status.phase == ContractPhase::Defeat) {
        if (key == GLFW_KEY_ENTER) activate(23);
        return true;
    }
    if (m_status.phase == ContractPhase::Reward) {
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_3) activate(20+key-GLFW_KEY_1);
        if (key == GLFW_KEY_LEFT) m_rewardSelection = (m_rewardSelection+2)%3;
        if (key == GLFW_KEY_RIGHT) m_rewardSelection = (m_rewardSelection+1)%3;
        if (key == GLFW_KEY_ENTER) activate(20+m_rewardSelection);
        return true;
    }
    if (!m_status.alive) return true;
    if (key == GLFW_KEY_N && m_status.phase == ContractPhase::Station && !m_map) {
        m_visible = true;
        m_defense = !m_defense;
        m_equipment = false;
        layout();
        return true;
    }
    if (m_defense) {
        if (m_pending) return true;
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_3)
            m_shieldSelection = static_cast<std::uint8_t>(key-GLFW_KEY_1+1);
        if (key == GLFW_KEY_LEFT) m_shieldSelection = (m_shieldSelection+1)%3+1;
        if (key == GLFW_KEY_RIGHT) m_shieldSelection = m_shieldSelection%3+1;
        layout();
        if (key == GLFW_KEY_ENTER) activate(33);
        return true;
    }
    if (key == GLFW_KEY_M && m_status.phase == ContractPhase::Station) {
        m_visible = true; m_map = !m_map; m_equipment = false; layout(); return true;
    }
    if (m_map || m_status.phase != ContractPhase::Station) {
        if (key == GLFW_KEY_ENTER) activate(9);
        if (!m_pending && (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT ||
                key == GLFW_KEY_UP || key == GLFW_KEY_DOWN)) {
            const int count = static_cast<int>(m_status.route.size());
            const int step = key == GLFW_KEY_LEFT || key == GLFW_KEY_UP ? -1 : 1;
            for (int i = 1; i <= count; ++i) {
                const int next = (int(m_status.selectedNode) + step*i + count*2) % count;
                const auto button = std::find_if(m_buttons.begin(),m_buttons.end(),
                    [next](const auto& b) { return b.id == 300+next && b.enabled; });
                if (button != m_buttons.end()) { activate(button->id); break; }
            }
        }
        return true;
    }
    if (key == GLFW_KEY_E) { m_visible = true; m_equipment = !m_equipment; layout(); return true; }
    if (!m_equipment) {
        if (key == GLFW_KEY_ENTER) { m_map = true; m_visible = true; layout(); }
        if (key == GLFW_KEY_B) request(ContractActionKind::BuyGun);
        if (key == GLFW_KEY_H) activate(34);
    } else {
        if (key == GLFW_KEY_Q) activate(6);
        if (key == GLFW_KEY_R) activate(7);
        const int mounts = static_cast<int>(m_status.mounts.size());
        if (mounts && (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT))
            m_mount = (m_mount + (key == GLFW_KEY_LEFT ? mounts-1 : 1) + mounts) % mounts;
        if (key == GLFW_KEY_UP || key == GLFW_KEY_DOWN) {
            const int step = key == GLFW_KEY_UP ? 5 : 1;
            for (int i = 0; i < 6; ++i) {
                m_weapon = (m_weapon + step + 5) % 6 + 1;
                if (m_status.inventory[m_weapon-1]) break;
            }
        }
        layout();
        if (key == GLFW_KEY_ENTER) activate(8);
    }
    return true;
}

bool StationOutfitter::mouse(int button, int action, glm::vec2 pointer) {
    if (!visible()) return false;
    m_hover = -1;
    for (const auto& b : m_buttons)
        if (contains(b.bounds,pointer)) m_hover = b.id;
    if (button != GLFW_MOUSE_BUTTON_LEFT) return true;
    int hit = m_hover;
    if (m_equipment) {
        float nearest = 0.025f;
        for (std::size_t i = 0; i < m_mountPositions.size(); ++i) {
            const float distance = glm::length(pointer-m_mountPositions[i]);
            if (distance < nearest) { hit = 200+static_cast<int>(i); nearest = distance; }
        }
    }
    if (action == GLFW_PRESS) m_pressed = hit;
    if (action == GLFW_RELEASE) {
        const int pressed = m_pressed;
        m_pressed = -1;
        if (hit == pressed && hit >= 0) {
            if (hit >= 200 && hit < 300) m_mount = hit-200;
            else activate(hit);
        }
    }
    return true;
}

glm::vec2 StationOutfitter::project(glm::vec3 cell) const {
    const auto p = cell - m_center;
    const float x = p.x*std::cos(m_rotation) + p.z*std::sin(m_rotation);
    const float z = -p.x*std::sin(m_rotation) + p.z*std::cos(m_rotation);
    return {0.33f+x*m_scale, 0.51f+(-p.y*0.85f+z*0.55f)*m_scale*m_aspect};
}

void StationOutfitter::initialize() {
    m_panel = new glow::Program();
    m_panel->attach(glowutils::createShaderFromFile(GL_VERTEX_SHADER,"data/shader/hud/hudoverlay.vert"),
        glowutils::createShaderFromFile(GL_FRAGMENT_SHADER,"data/shader/hud/hudoverlay.frag"));
    m_wire = new glow::Program();
    m_wire->attach(glowutils::createShaderFromFile(GL_VERTEX_SHADER,"data/shader/ui/wire.vert"),
        glowutils::createShaderFromFile(GL_FRAGMENT_SHADER,"data/shader/ui/wire.frag"));
    m_buffer = new glow::Buffer(GL_ARRAY_BUFFER);
    m_array = new glow::VertexArrayObject();
    auto* binding = m_array->binding(0);
    binding->setAttribute(0);
    binding->setBuffer(m_buffer,0,sizeof(glm::vec2));
    binding->setFormat(2,GL_FLOAT,GL_FALSE,0);
    m_array->enable(0);
}

void StationOutfitter::rectangle(glm::vec4 b, glm::vec3 color) {
    m_panel->use();
    m_panel->setUniform("panelBounds",glm::vec4(b.x*2-1,1-(b.y+b.w)*2,b.z*2,b.w*2));
    m_panel->setUniform("panelColor",color);
    m_panel->setUniform("panelOpacity",1.0f);
    m_quad->draw();
}

void StationOutfitter::text(const std::string& value, glm::vec2 position,
    float height, glm::vec3 color, float width) {
    UiFont::instance()->drawScreen(value,position,height,glm::vec4(color,1),FontAlign::LEFT,width);
}

void StationOutfitter::drawStats(std::uint8_t id, glm::vec2 position, std::uint8_t comparison,
    float scale) {
    const auto p = catalogWeapon(id).profile;
    const auto old = comparison ? catalogWeapon(comparison).profile : p;
    const float bonus = 1.0f + m_status.damageUpgrades *
        Property<float>("contracts.damageRewardBonus").get();
    const float values[]{p.speed,p.range,p.shield*p.pellets*bonus,p.armor*p.pellets*bonus,p.normal*p.pellets*bonus};
    const float previous[]{old.speed,old.range,old.shield*old.pellets*bonus,old.armor*old.pellets*bonus,old.normal*old.pellets*bonus};
    const char* names[]{"VELOCITY","TRAVEL DISTANCE","SHIELD DAMAGE","ARMOR DAMAGE","BLOCK DAMAGE"};
    for (int i = 0; i < 5; ++i) {
        std::string value = number(values[i],i >= 2 ? 1 : 0);
        if (i == 0) value += " U/S";
        if (i == 1) value += " U";
        if (comparison && values[i] != previous[i]) {
            const float delta = values[i]-previous[i];
            value += " (" + std::string(delta>0 ? "+" : "") + number(delta,i >= 2 ? 1 : 0) + ")";
        }
        text(names[i],position+glm::vec2(0,i*0.03f)*scale,0.015f*scale,Muted,0.15f*scale);
        text(value,position+glm::vec2(0.17f,i*0.03f)*scale,0.016f*scale,
            values[i]>previous[i] ? Accent : values[i]<previous[i] ? glm::vec3(1,0.65f,0.4f)
            : glm::vec3(0.9f),0.13f*scale);
    }
}

void StationOutfitter::drawShip() {
    m_lines.clear();
    for (const auto& voxel : m_voxels) {
        for (int axis = 0; axis < 3; ++axis) for (int first : {-1,1}) for (int second : {-1,1}) {
            glm::vec3 a(voxel.cell), b(voxel.cell);
            a[axis] -= 0.5f; b[axis] += 0.5f;
            a[(axis+1)%3] += first*0.5f; b[(axis+1)%3] += first*0.5f;
            a[(axis+2)%3] += second*0.5f; b[(axis+2)%3] += second*0.5f;
            for (auto p : {project(a),project(b)}) m_lines.push_back({p.x*2-1,1-p.y*2});
        }
    }
    drawLines({0.19f,0.44f,0.52f,0.34f});
    for (std::size_t i = 0; i < m_mountPositions.size(); ++i) {
        const auto p = m_mountPositions[i];
        const bool selected = static_cast<int>(i) == m_mount;
        rectangle({p.x-0.012f,p.y-0.018f,0.024f,0.036f},selected ? Accent : glm::vec3(0.9f,0.62f,0.26f));
        rectangle({p.x-0.009f,p.y-0.014f,0.018f,0.028f},Panel);
        text(std::to_string(i+1),p-glm::vec2(0.004f,0.010f),0.018f,selected ? Accent : glm::vec3(1),0.016f);
    }
}

void StationOutfitter::drawLines(glm::vec4 color) {
    m_wire->use();
    m_wire->setUniform("lineColor",color);
    m_buffer->setData(m_lines.size()*sizeof(glm::vec2),m_lines.data(),GL_DYNAMIC_DRAW);
    m_array->drawArrays(GL_LINES,0,static_cast<GLsizei>(m_lines.size()));
}

glm::vec2 StationOutfitter::nodePosition(const RouteNode& node) const {
    return {0.105f+node.row*0.079f,0.51f+(int(node.lane)-1)*0.155f};
}

void StationOutfitter::drawRoute() {
    rectangle({0.065f,0.26f,0.57f,0.55f},Surface*0.65f);
    rectangle({0.66f,0.26f,0.275f,0.55f},Surface*0.65f);
    text("1 PATROL / 2 CROSSFIRE / 3 BLOCKADE / BASE SERVICES / SIGNAL ?",
        {0.085f,0.285f},0.013f,Muted,0.53f);
    for (int pass = 0; pass < 2; ++pass) {
        m_lines.clear();
        for (std::size_t i = 0; i < m_status.route.size(); ++i) {
            for (std::size_t j = 0; j < m_status.route.size(); ++j) {
                if (!(m_status.route[i].links & (1u << j))) continue;
                const bool path = ((m_status.visitedNodes & (1u << i)) &&
                    (m_status.visitedNodes & (1u << j))) ||
                    (i == m_status.currentNode && j == m_status.selectedNode);
                if (path != (pass == 1)) continue;
                for (auto p : {nodePosition(m_status.route[i]),nodePosition(m_status.route[j])})
                    m_lines.push_back({p.x*2-1,1-p.y*2});
            }
        }
        drawLines(pass ? glm::vec4(Accent,0.9f) : glm::vec4(Muted,0.25f));
    }
    text("CURRENT / VISITED = CYAN   SELECTED = AMBER",{0.085f,0.755f},0.012f,Muted,0.53f);
    auto nodeId = m_status.selectedNode;
    if (m_hover >= 300) nodeId = static_cast<std::uint8_t>(m_hover-300);
    if (nodeId < m_status.route.size()) {
        const auto& node = m_status.route[nodeId];
        text(nodeId == m_status.selectedNode ? "SELECTED DESTINATION" : "HOVER PREVIEW",
            {0.68f,0.275f},0.012f,Muted,0.23f);
        text(node.kind == RouteNodeKind::Attack ? contractLevel(node.level).name :
            node.kind == RouteNodeKind::Base ? "STATION STOP" : contractSignal(node.level).name,
            {0.68f,0.32f},0.022f,Accent,0.23f);
        if (node.kind != RouteNodeKind::Base) {
            const auto level = node.kind == RouteNodeKind::Attack ? contractLevel(node.level) : contractSignal(node.level);
            const auto split = level.description.find(' ',level.description.size()/2);
            text(level.description.substr(0,split),{0.68f,0.40f},0.016f,Muted,0.23f);
            if (split != std::string::npos)
                text(level.description.substr(split+1),{0.68f,0.43f},0.016f,Muted,0.23f);
            text(std::to_string(level.reward)+" CR OR SALVAGE",
                {0.68f,0.48f},0.017f,glm::vec3(1),0.23f);
            text(level.enemiesPerPlayer == 0 ? "NO HOSTILES" : std::to_string(level.enemiesPerPlayer)+
                (level.enemiesPerPlayer == 1 ? " ENEMY PER PILOT" : " ENEMIES PER PILOT"),
                {0.68f,0.54f},0.016f,Muted,0.23f);
            text(level.enemiesPerPlayer ? "DESTROY ALL HOSTILES" : "CHOOSE ONE SALVAGE REWARD",
                {0.68f,0.60f},0.015f,Muted,0.23f);
        } else {
            text("ONE NEW PERSONAL GUN OFFER",
                {0.68f,0.42f},0.015f,Muted,0.23f);
            text("SHIELDS AND ENERGY REFILL",
                {0.68f,0.49f},0.015f,Muted,0.23f);
            text("PAID REPAIRS / LOST BLOCKS STAY LOST",
                {0.68f,0.56f},0.015f,Muted,0.23f);
        }
        text("PILOTS READY "+std::to_string(m_status.readyPlayers)+"/"+std::to_string(m_status.livingPlayers),
            {0.68f,0.70f},0.018f,glm::vec3(1),0.23f);
        text("CHANGING ROUTE CLEARS PARTY READY",{0.68f,0.755f},0.012f,Muted,0.23f);
    }
}

void StationOutfitter::drawRewards() {
    const char* headings[]{"CREDITS","WEAPON SALVAGE","RUN UPGRADE"};
    for (int i = 0; i < 3; ++i) {
        const float x = 0.075f+i*0.29f;
        const bool selected = i == m_rewardSelection;
        rectangle({x,0.27f,0.27f,0.53f},selected ? Accent*0.45f : Surface);
        rectangle({x+0.002f,0.273f,0.266f,0.524f},Panel);
        text(headings[i],{x+0.014f,0.30f},0.019f,Accent,0.24f);
    }
    text("+"+std::to_string(m_status.reward)+" CR",{0.09f,0.40f},0.039f,glm::vec3(1),0.23f);
    text("SAVE FOR A STATION VISIT",{0.09f,0.50f},0.014f,Muted,0.24f);
    text("BUY GUNS OR A SHIELD",{0.09f,0.54f},0.014f,Muted,0.24f);
    if (m_status.rewardWeapon) {
        UiFont::instance()->drawWeaponIcon(m_status.rewardWeapon,{0.46f,0.35f,0.09f,0.12f});
        const auto weapon = catalogWeapon(m_status.rewardWeapon);
        text(weapon.name,{0.38f,0.49f},0.021f,glm::vec3(1),0.24f);
        text(weapon.role,{0.38f,0.525f},0.013f,Accent,0.24f);
        drawStats(m_status.rewardWeapon,{0.38f,0.565f},0,0.8f);
        text(number(weapon.energyCost)+" ENERGY / SHOT / FIT AT BASE",
            {0.38f,0.695f},0.010f,Muted,0.24f);
    }
    const bool damage = m_status.rewardUpgrade == 1;
    const int percent = static_cast<int>(std::lround(100 *
        Property<float>(damage ? "contracts.damageRewardBonus" : "contracts.shieldRewardBonus").get()));
    text(damage ? "PAYLOAD AMPLIFIER" : "SHIELD CAPACITOR",
        {0.67f,0.38f},0.021f,glm::vec3(1),0.24f);
    text("+"+std::to_string(percent)+(damage ? "% GUN DAMAGE" : "% SHIELD CAPACITY"),
        {0.67f,0.45f},0.020f,Accent,0.24f);
    text(damage ? "ALL GUNS, INCLUDING REFITS" : "RETAINS CURRENT CHARGE RATIO",
        {0.67f,0.52f},0.012f,Muted,0.24f);
    text("STACKS FOR THIS RUN ONLY",{0.67f,0.57f},0.014f,Muted,0.24f);
    text("ACTIVE: "+std::to_string(m_status.damageUpgrades)+" PAYLOAD / "+
        std::to_string(m_status.shieldUpgrades)+" SHIELD",{0.67f,0.63f},0.013f,Muted,0.24f);
    text(!m_status.alive ? "SHIP LOST - WAITING FOR SURVIVORS. NO REVIVES THIS RUN." :
        m_status.rewardClaimed ? "REWARD CLAIMED - WAITING FOR THE OTHER PILOTS" :
        "ONE PERSONAL CHOICE. THE OTHER TWO REWARDS ARE LEFT BEHIND.",
        {0.085f,0.86f},0.018f,Accent,0.84f);
}

void StationOutfitter::drawSummary() {
    rectangle({0.065f,0.27f,0.87f,0.53f},Surface*0.65f);
    text(m_status.phase == ContractPhase::Complete ? "SECTOR SECURED" : "PARTY WIPED",
        {0.09f,0.31f},0.038f,Accent,0.65f);
    text(m_status.alive ? "YOUR PILOT SURVIVED" : "YOUR SHIP WAS LOST",
        {0.09f,0.39f},0.019f,glm::vec3(1),0.7f);
    const auto seconds = m_status.elapsedTicks /
        static_cast<std::uint64_t>(SimulationTickRunner::TickRate);
    const std::string left[]{
        "ATTACKS CLEARED  "+std::to_string(m_status.attacksCleared),
        "HOSTILES DEFEATED  "+std::to_string(m_status.enemiesDefeated),
        "BASES VISITED  "+std::to_string(m_status.basesVisited),
        "RUN TIME  "+std::to_string(seconds/60)+" M "+std::to_string(seconds%60)+" S"};
    const std::string right[]{
        "CREDITS EARNED  "+std::to_string(m_status.creditsEarned),
        "WEAPONS SALVAGED  "+std::to_string(m_status.weaponsFound),
        "PAYLOAD / SHIELD UPGRADES  "+std::to_string(m_status.damageUpgrades)+" / "+
            std::to_string(m_status.shieldUpgrades),
        "RESTART READY  "+std::to_string(m_status.readyPlayers)+" / "+std::to_string(m_status.players)};
    for (int i = 0; i < 4; ++i) {
        text(left[i],{0.09f,0.48f+i*0.065f},0.018f,glm::vec3(1),0.39f);
        text(right[i],{0.53f,0.48f+i*0.065f},0.018f,glm::vec3(1),0.38f);
    }
    text("NEW RUN RESETS SHIPS, CREDITS, INVENTORY, UPGRADES AND THE MAP.",
        {0.065f,0.865f},0.013f,Muted,0.53f);
}

void StationOutfitter::drawDefense() {
    const auto current = catalogShield(m_status.shieldBuild);
    const float bonus = 1.0f + m_status.shieldUpgrades *
        Property<float>("contracts.shieldRewardBonus").get();
    for (std::uint8_t id = 1; id <= ShieldCatalogSize; ++id) {
        const auto shield = catalogShield(id);
        const float x = 0.075f+(id-1)*0.29f;
        rectangle({x,0.27f,0.27f,0.45f},id == m_shieldSelection ? Accent*0.65f : Surface);
        rectangle({x+0.002f,0.273f,0.266f,0.444f},Panel);
        text(shield.name,{x+0.015f,0.30f},0.027f,Accent,0.24f);
        text(shield.role,{x+0.015f,0.355f},0.013f,Muted,0.24f);
        text("CAPACITY  "+number(shield.capacity*bonus)+" HP",
            {x+0.015f,0.415f},0.018f,glm::vec3(1),0.24f);
        text("REGEN  "+number(shield.regeneration)+" HP/S",
            {x+0.015f,0.465f},0.018f,glm::vec3(1),0.24f);
        text("AFTER HIT  "+number(shield.rechargeDelay,1)+" S",
            {x+0.015f,0.515f},0.018f,glm::vec3(1),0.24f);
        text(m_status.shieldBuild == id ? "EQUIPPED" : std::to_string(shield.price)+" CR",
            {x+0.015f,0.575f},0.021f,Accent,0.24f);
    }
    text("FITTED: "+current.name+" / "+number(m_status.shieldCapacity)+" HP",
        {0.085f,0.74f},0.016f,Accent,0.84f);
    text("REACTOR  "+number(m_status.weaponEnergyCapacity)+" ENERGY / "+
        number(m_status.weaponEnergyRegeneration)+" E/S / "+
        number(Property<float>("contractship.general.weaponEnergyDelay"),1)+" S AFTER FIRING",
        {0.085f,0.775f},0.015f,glm::vec3(1),0.84f);
}

void StationOutfitter::drawFlightResources() {
    const auto bar = [&](float x, const std::string& title, float value, float maximum,
            const std::string& detail, glm::vec3 color) {
        rectangle({x,0.79f,0.30f,0.115f},Panel);
        text(title,{x+0.012f,0.802f},0.016f,color,0.275f);
        const float fraction = maximum > 0 ? glm::clamp(value/maximum,0.0f,1.0f) : 0;
        rectangle({x+0.012f,0.835f,0.276f,0.012f},Surface*2.0f);
        if (fraction > 0)
            rectangle({x+0.012f,0.835f,0.276f*fraction,0.012f},color);
        text(number(value)+" / "+number(maximum),{x+0.012f,0.858f},0.016f,glm::vec3(1),0.125f);
        text(detail,{x+0.145f,0.86f},0.012f,color,0.14f);
    };
    const auto shield = catalogShield(m_status.shieldBuild);
    const bool broken = m_status.shieldHP <= 0;
    const auto shieldDetail = m_status.shieldRechargeRemaining > 0
        ? "WAIT "+number(m_status.shieldRechargeRemaining,1)+" S"
        : m_status.shieldHP < m_status.shieldCapacity
            ? "+"+number(m_status.shieldRegeneration)+" HP/S" : "FULL CHARGE";
    bar(0.04f,(broken ? "SHIELD BROKEN / " : "SHIELD / ")+shield.name,
        m_status.shieldHP,m_status.shieldCapacity,shieldDetail,
        broken ? glm::vec3(1,0.55f,0.3f) : Accent);
    const bool low = m_status.weaponEnergy < m_status.weaponEnergyCapacity*0.25f;
    const auto energyDetail = m_status.weaponEnergyRechargeRemaining > 0
        ? "WAIT "+number(m_status.weaponEnergyRechargeRemaining,1)+" S"
        : m_status.weaponEnergy < m_status.weaponEnergyCapacity
            ? "+"+number(m_status.weaponEnergyRegeneration)+" E/S" : "FULL RESERVE";
    bar(0.66f,low ? "ENERGY LOW / RELEASE FIRE" : "WEAPON ENERGY",
        m_status.weaponEnergy,m_status.weaponEnergyCapacity,energyDetail,
        low ? glm::vec3(1,0.72f,0.3f) : glm::vec3(0.65f,0.65f,1));
}

void StationOutfitter::draw() {
    const bool flight = m_status.phase == ContractPhase::Mission && m_status.alive &&
        Property<bool>("vfx.drawhud").get();
    if (!visible() && !flight) return;
    if (!m_panel) initialize();
    GLint program, array, buffer, sourceRgb, destinationRgb, sourceAlpha, destinationAlpha;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &array);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
    glGetIntegerv(GL_BLEND_SRC_RGB, &sourceRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &destinationRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &sourceAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &destinationAlpha);
    const bool depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    double x,y; int w,h;
    glfwGetCursorPos(glfwGetCurrentContext(),&x,&y);
    glfwGetWindowSize(glfwGetCurrentContext(),&w,&h);
    if (w>0 && h>0) mouse(-1,-1,{x/w,y/h});
    if (flight) {
        drawFlightResources();
    } else {
    rectangle({0.035f,0.085f,0.93f,0.87f},Panel);
    rectangle({0.035f,0.085f,0.004f,0.87f},Accent);
    rectangle({0.065f,0.23f,0.87f,0.002f},Surface*2.0f);
    const bool summary = m_status.phase == ContractPhase::Complete || m_status.phase == ContractPhase::Defeat;
    const bool rewards = m_status.phase == ContractPhase::Reward;
    text(summary ? "RUN "+std::to_string(m_status.run)+" / DEBRIEF" : rewards ? "CHOOSE YOUR SALVAGE" :
        m_map ? "SECTOR ROUTE" :         m_defense ? "SHIELDS / REACTOR" :
        m_equipment ? "SHIP EQUIPMENT" : "STATION SERVICES",
        {0.065f,0.12f},0.031f,Accent,0.52f);
    text("BALANCE  "+std::to_string(m_status.credits)+" CR",{0.65f,0.175f},0.02f,glm::vec3(1),0.28f);
    if (m_status.damageUpgrades || m_status.shieldUpgrades)
        text("RUN STACKS: "+std::to_string(m_status.damageUpgrades)+" PAYLOAD / "+
            std::to_string(m_status.shieldUpgrades)+" SHIELD",
            {0.65f,0.21f},0.012f,Accent,0.28f);
    text(summary ? "EVERY CONNECTED PILOT MUST CONFIRM A FRESH RUN." :
        rewards ? "EACH SURVIVING PILOT CHOOSES ONE REWARD." :
        !m_status.alive ? "SHIP LOST. SPECTATING THE SURVIVORS - NO REVIVES THIS RUN." :
        m_defense ? "BUY AND EQUIP A BUILD. REPLACES THE OLD SHIELD; NO REFUND. ENERGY IS SEPARATE." :
        m_map ? "CHOOSE A CONNECTED NODE. SURVIVING PILOTS CONFIRM BEFORE TRAVEL."
        : m_equipment ? "SELECT A WEAPON MOUNT, THEN CHOOSE A STORED WEAPON"
        : "REARM. REFIT. RETURN TO THE FIGHT.",{0.065f,0.183f},0.016f,Muted,0.55f);
    if (!m_map && !summary && !rewards && !m_defense) {
        rectangle({0.065f,0.26f,m_equipment ? 0.53f : 0.37f,0.55f},Surface*0.65f);
        rectangle({m_equipment ? 0.615f : 0.485f,0.26f,m_equipment ? 0.32f : 0.45f,0.55f},Surface*0.65f);
    }
    if (summary) drawSummary();
    else if (rewards) drawRewards();
    else if (m_defense) drawDefense();
    else if (m_map) {
        drawRoute();
    } else if (!m_equipment) {
        const auto offer = catalogWeapon(m_status.offer);
        text("SAFE HARBOR",{0.085f,0.29f},0.018f,Accent,0.3f);
        text("CHOOSE YOUR NEXT STOP",{0.085f,0.35f},0.022f,glm::vec3(1),0.30f);
        text("ATTACK / BASE / UNKNOWN SIGNAL",{0.085f,0.40f},0.016f,Muted,0.32f);
        text("ENTER - OPEN THE ROUTE MAP",{0.085f,0.45f},0.018f,glm::vec3(1),0.31f);
        text("SURVIVING BLOCKS ONLY / "+std::to_string(m_status.missingHullBlocks)+" LOST",
            {0.085f,0.51f},0.014f,Muted,0.32f);
        text("ONE PERSONAL WEAPON OFFER PER VISIT",{0.51f,0.285f},0.016f,Accent,0.39f);
        UiFont::instance()->drawWeaponIcon(m_status.offer,{0.51f,0.325f,0.09f,0.14f});
        text(offer.name,{0.62f,0.34f},0.022f,glm::vec3(1),0.28f);
        text(offer.role,{0.62f,0.385f},0.014f,Accent,0.28f);
        text(offer.profile.pellets > 1 ? "8 PELLETS / VOLLEY TOTALS" :
            offer.profile.radius > 0 ? "SPLASH / SHARED TOTAL BUDGET" : "REFERENCE IMPACT DAMAGE",
            {0.62f,0.42f},0.013f,Muted,0.28f);
        drawStats(m_status.offer,{0.51f,0.49f});
        text("ENERGY / SHOT  "+number(offer.energyCost),
            {0.51f,0.645f},0.015f,Accent,0.39f);
        text("PURCHASES GO TO INVENTORY. EQUIP BEFORE LAUNCH.",{0.51f,0.77f},0.012f,Muted,0.40f);
    } else {
        drawShip();
        unsigned stored = 0;
        for (auto quantity : m_status.inventory) stored += quantity;
        text("INVENTORY  "+std::to_string(stored)+"/64",{0.63f,0.275f},
            0.015f,Accent,0.27f);
        text("HOLLOW HULL / WEAPON MOUNTS ONLY",{0.085f,0.285f},0.015f,Accent,0.45f);
        if (m_voxels.empty()) text("WAITING FOR SHIP GEOMETRY",{0.12f,0.5f},0.019f,Muted,0.40f);
        text(m_mount < 0 ? "CLICK A NUMBERED MOUNT" : "MOUNT "+std::to_string(m_mount+1)+" SELECTED",
            {0.085f,0.735f},0.017f,Accent,0.45f);
        const auto mounted = m_mount >= 0 ? m_status.mounts[m_mount].weapon : 0;
        if (mounted) text("FITTED: "+catalogWeapon(mounted).name,{0.085f,0.77f},0.016f,Muted,0.45f);
        if (m_weapon > 0) {
            const auto profile = catalogWeapon(m_weapon).profile;
            text(profile.pellets > 1 ? "8 PELLETS / VOLLEY TOTALS" :
                profile.radius > 0 ? "SPLASH / SHARED TOTAL BUDGET" : "REFERENCE IMPACT DAMAGE",
                {0.63f,0.628f},0.011f,Muted,0.28f);
            drawStats(static_cast<std::uint8_t>(m_weapon),{0.63f,0.65f},mounted);
            text("ENERGY / SHOT  "+number(catalogWeapon(m_weapon).energyCost),
                {0.63f,0.808f},0.012f,Accent,0.28f);
            UiFont::instance()->drawWeaponIcon(m_weapon,{0.50f,0.70f,0.075f,0.10f});
        } else {
            text(stored ? "SELECT AN INVENTORY WEAPON" : "NO STORED WEAPONS",
                {0.63f,0.69f},0.015f,Muted,0.27f);
            if (!stored) text("BUY A WEAPON AT BASE",{0.63f,0.73f},0.015f,Muted,0.27f);
        }
    }
    for (const auto& button : m_buttons) {
        const bool routeNode = button.id >= 300;
        const int node = button.id-300;
        const bool chosen = routeNode && node == m_status.selectedNode;
        const bool visited = routeNode && (m_status.visitedNodes & (1u << node));
        const bool selected = button.id == m_hover || (rewards && button.id == 20+m_rewardSelection) ||
            (button.id>=100 && button.id<200 && button.id==m_weapon+99);
        if (routeNode) rectangle(button.bounds+glm::vec4(-0.002f,-0.003f,0.004f,0.006f),
            chosen ? glm::vec3(1,0.68f,0.3f) : visited ? Accent : Muted*0.3f);
        rectangle(button.bounds,button.enabled && selected ? Accent*0.6f : Surface);
        if (selected) rectangle(button.bounds+glm::vec4(0.001f,0.002f,-0.002f,-0.004f),Panel);
        const bool inventory = button.id >= 100 && button.id < 200;
        if (inventory) UiFont::instance()->drawWeaponIcon(button.id-99,
            {button.bounds.x+0.004f,button.bounds.y+0.002f,0.035f,0.045f});
        text(button.label,{button.bounds.x+(inventory ? 0.047f : 0.012f),
            button.bounds.y+button.bounds.w*0.32f},inventory ? 0.015f : 0.017f,
            visited ? Accent : button.enabled ? glm::vec3(0.90f,0.97f,1) : Muted*0.7f,
            button.bounds.z-(inventory ? 0.055f : 0.024f));
    }
    text(m_pending ? m_pendingTime>3 ? "SERVER RESPONSE DELAYED - WAITING FOR CONFIRMATION"
        : "WAITING FOR SERVER CONFIRMATION..." : m_message,
        {0.065f,0.825f},0.014f,Accent,0.85f);
    text(summary ? "ENTER NEW RUN   ESC MAIN MENU   TAB HIDE" :
        rewards ? "1 / 2 / 3 CLAIM   LEFT / RIGHT SELECT   ENTER CLAIM   TAB HIDE" :
        m_defense ? "1 / 2 / 3 SELECT   LEFT / RIGHT SELECT   ENTER BUY   N / ESC BACK" :
        m_map ? "ARROWS SELECT NEXT STOP   ENTER CONFIRM   M BACK AT BASE   TAB HIDE"
        : m_equipment ? "LEFT / RIGHT MOUNT   UP / DOWN INVENTORY   ENTER FIT   ESC BACK"
        : "B BUY WEAPON   N SHIELD BUILDS   E EQUIPMENT   ENTER ROUTE MAP   TAB HIDE",
        {0.30f,0.93f},0.012f,Muted,0.62f);
    }
    glUseProgram(program);
    glBindVertexArray(array);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBlendFuncSeparate(sourceRgb, destinationRgb, sourceAlpha, destinationAlpha);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (!blend) glDisable(GL_BLEND);
    if (cull) glEnable(GL_CULL_FACE);
}

void StationOutfitter::beforeContextDestroy() {
    m_panel = nullptr; m_wire = nullptr; m_buffer = nullptr; m_array = nullptr;
}
