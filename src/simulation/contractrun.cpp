#include "contractrun.h"
#include "contractlevel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <stdexcept>
#include <utility>

#include "ai/basictasks/fighttask.h"
#include "ai/character.h"
#include "equipment/hardpoint.h"
#include "equipment/shield.h"
#include "equipment/shieldslot.h"
#include "equipment/shieldcatalog.h"
#include "equipment/weapon.h"
#include "equipment/weaponcatalog.h"
#include "voxel/specialvoxels/hardpointvoxel.h"
#include "geometry/speed.h"
#include "physics/physics.h"
#include "procedural/covergenerator.h"
#include "procedural/coverlayout.h"
#include "property/property.h"
#include "resource/shieldbuilder.h"
#include "resource/weaponbuilder.h"
#include "resource/worldobjectbuilder.h"
#include "world/god.h"
#include "world/world.h"
#include "worldobject/ship.h"
#include "worldobject/worldobjectcomponents.h"
#include "voxel/voxel.h"
#include "voxel/voxelclusterbounds.h"

using namespace voxellancer::protocol;

namespace {
    constexpr char CheckpointHeader[] = "VOXELLANCER SOLO CHECKPOINT 1";
    // Checkpoint 1 uses the contract-status layout shared by protocols 30 and 31.
    // A changed status layout requires a new checkpoint format and migration.
    constexpr std::uint8_t CheckpointStatusVersion = 0;
    static_assert(ContractRouteOffset == 183 && ContractRunOffset == 326 &&
        ContractResourceOffset == 366 && ContractStatusPacketSize == 407,
        "Checkpoint status layout changed; add a checkpoint format migration.");

    std::uint64_t playerKey(EntityId actor) {
        return (static_cast<std::uint64_t>(actor.generation) << 32) | actor.index;
    }

    glm::vec3 berth(std::size_t index) {
        return glm::vec3(static_cast<float>(index) * 70.0f, 0.0f, 0.0f);
    }
}

std::vector<EntityId> ContractRun::createPlayers(World& world, std::size_t count) {
    if (count == 0 || count > 4) {
        throw std::invalid_argument("Contracts require between one and four players.");
    }
    std::vector<Ship*> ships;
    for (std::size_t index = 0; index < count; ++index) {
        Ship* ship = WorldObjectBuilder("contractship", world).buildShip();
        ship->transform().setPosition(berth(index));
        ship->info().setName("Pilot " + std::to_string(index + 1));
        world.god().scheduleSpawn(ship);
        ships.push_back(ship);
    }
    world.god().spawn();
    std::vector<EntityId> actors;
    for (Ship* ship : ships) {
        const auto id = world.entityId(ship);
        if (!id) {
            throw std::runtime_error("Could not spawn a contract player ship.");
        }
        actors.push_back(*id);
    }
    return actors;
}

ContractRun::ContractRun(World& world, const std::vector<EntityId>& players,
    std::filesystem::path checkpoint):
    m_world(world),
    m_checkpoint(std::move(checkpoint)),
    m_damageBonus(Property<float>("contracts.damageRewardBonus")),
    m_shieldBonus(Property<float>("contracts.shieldRewardBonus"))
{
    if (players.empty() || players.size() > 4 ||
        !std::isfinite(m_damageBonus) || m_damageBonus <= 0 ||
        !std::isfinite(m_shieldBonus) || m_shieldBonus <= 0) {
        throw std::invalid_argument("Invalid contract party or economy configuration.");
    }
    for (const auto actor : players) {
        const auto object = m_world.worldObject(actor);
        if (!object || object->objectType() != WorldObjectType::Ship ||
            !m_players.emplace(playerKey(actor), PlayerState{actor}).second) {
            throw std::invalid_argument("Invalid or duplicate contract player.");
        }
        object->info().setInvulnerable(Property<bool>("general.playerShipUndestroyable"));
        m_players.at(playerKey(actor)).protection = object->info().invulnerable();
        if (object->components().hardpoints().size() > MaxContractMounts)
            throw std::invalid_argument("Contract ship has too many hardpoints.");
        refreshOffer(m_players.at(playerKey(actor)));
    }
    for (std::uint8_t id = 1; id <= 3; ++id) {
        contractLevel(id);
        contractSignal(id);
    }
    contractLevel(FlagshipLevel);
    for (std::uint8_t id = 0; id <= ShieldCatalogSize; ++id) catalogShield(id);
    m_startTick = m_world.simulationTickIndex();
    if (!m_checkpoint.empty() && players.size() != 1)
        throw std::invalid_argument("Checkpoints require a solo contract.");
    if (!loadCheckpoint()) {
        generateRoute();
        if (!m_checkpoint.empty()) saveCheckpoint();
    }
}

void ContractRun::saveCheckpoint() const {
    const auto& player = m_players.begin()->second;
    const auto* ship = m_world.worldObject(player.actor);
    auto state = statusForPlayer(player);
    state.ready = false;
    state.readyPlayers = 0;
    state.result = ContractResult::None;
    state.viewActor = {};
    state.acknowledgedSequence = 0;
    state.elapsedTicks = m_previousElapsedTicks + m_world.simulationTickIndex() - m_startTick;
    ContractStatusPacket packet{};
    if (m_phase != ContractPhase::Station || !ship || !encodeContractStatus(state, packet))
        throw std::logic_error("Cannot checkpoint outside a live base visit.");
    packet[0] = CheckpointStatusVersion;

    auto temporary = m_checkpoint;
    temporary += "." + std::to_string(std::random_device{}()) + ".tmp";
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.imbue(std::locale::classic());
        out << CheckpointHeader << '\n';
        out.write(reinterpret_cast<const char*>(packet.data()), packet.size());
        out << '\n' << m_offerRandom << '\n'
            << std::setprecision(std::numeric_limits<float>::max_digits10);
        out << std::count_if(ship->voxelMap().begin(), ship->voxelMap().end(),
            [](const auto& entry) { return entry.second->hp() > 0; }) << '\n';
        for (const auto& entry : ship->voxelMap()) {
            const auto& voxel = *entry.second;
            if (voxel.hp() > 0) {
                const auto cell = voxel.cell();
                out << cell.x << ' ' << cell.y << ' ' << cell.z << ' ' << voxel.hp() << '\n';
            }
        }
        out.close();
        if (!out) throw std::runtime_error("Could not write " + temporary.string());
        std::filesystem::rename(temporary, m_checkpoint);
    } catch (const std::runtime_error&) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        if (cleanupError) std::cerr << "Could not remove temporary checkpoint: " << cleanupError.message() << '\n';
        throw;
    }
}

bool ContractRun::tryCheckpoint() {
    if (m_checkpoint.empty()) return true;
    try {
        saveCheckpoint();
        return true;
    } catch (const std::runtime_error& error) {
        std::cerr << "Solo checkpoint save failed: " << error.what() << '\n';
        return false;
    }
}

bool ContractRun::loadCheckpoint() {
    if (m_checkpoint.empty() || !std::filesystem::exists(m_checkpoint)) return false;
    const auto invalid = [this]() {
        return std::runtime_error("Invalid or incompatible solo checkpoint: " + m_checkpoint.string());
    };
    if (std::filesystem::file_size(m_checkpoint) > 1024 * 1024) throw invalid();
    std::ifstream in(m_checkpoint, std::ios::binary);
    in.imbue(std::locale::classic());
    std::string header;
    ContractStatusPacket packet{};
    ContractStatus state;
    if (!std::getline(in, header) || header != CheckpointHeader ||
        !in.read(reinterpret_cast<char*>(packet.data()), packet.size()) ||
        (packet[0] != CheckpointStatusVersion && packet[0] != 30 && packet[0] != 31))
        throw invalid();
    packet[0] = ProtocolVersion;
    if (decodeContractStatus(packet.data(), packet.size(), state) != DecodeError::None ||
        state.phase != ContractPhase::Station || !state.alive || state.players != 1 ||
        state.route.empty() || state.route[state.currentNode].kind != RouteNodeKind::Base ||
        state.selectedNode == NoRouteNode ||
        state.mission == std::numeric_limits<std::uint32_t>::max() ||
        state.run == std::numeric_limits<std::uint32_t>::max() ||
        state.elapsedTicks > std::numeric_limits<std::uint64_t>::max() / 2)
        throw invalid();
    std::mt19937 random;
    if (!(in >> random)) throw invalid();

    auto& player = m_players.begin()->second;
    auto* ship = m_world.worldObject(player.actor);
    std::map<Voxel*, float> hull;
    std::size_t voxelCount;
    if (!(in >> voxelCount) || voxelCount == 0 || voxelCount > ship->voxelMap().size())
        throw invalid();
    for (std::size_t i = 0; i < voxelCount; ++i) {
        glm::ivec3 cell;
        float hp;
        if (!(in >> cell.x >> cell.y >> cell.z >> hp) ||
            glm::any(glm::lessThan(cell, glm::ivec3(0))) ||
            glm::any(glm::greaterThan(cell, glm::ivec3(255))))
            throw invalid();
        auto* voxel = ship->voxel(cell);
        if (!voxel || !std::isfinite(hp) || hp <= 0 || hp > voxel->maxHP() ||
            !hull.emplace(voxel, hp).second) throw invalid();
    }
    in >> std::ws;
    if (!in.eof() || (ship->crucialVoxel() && !hull.count(ship->crucialVoxel())))
        throw invalid();

    auto& components = ship->components();
    std::size_t savedMounts = 0;
    for (const auto& slot : components.hardpoints()) {
        if (!hull.count(slot->voxel())) continue;
        const auto mount = std::find_if(state.mounts.begin(), state.mounts.end(),
            [&slot](const auto& entry) { return entry.cell == slot->voxel()->cell(); });
        if (mount == state.mounts.end() ||
            (mount->weapon && !slot->mountable(catalogWeapon(mount->weapon).key)))
            throw invalid();
        ++savedMounts;
    }
    if (savedMounts != state.mounts.size()) throw invalid();

    if (state.shieldBuild) {
        const auto key = catalogShield(state.shieldBuild).key;
        for (auto& slot : components.shieldSlots()) {
            if (!slot->mountable(key)) throw invalid();
            slot->setShield(std::shared_ptr<Shield>(ShieldBuilder(key).build()));
        }
    }
    std::vector<Voxel*> lost;
    for (const auto& entry : ship->voxelMap()) {
        auto* voxel = entry.second;
        const auto saved = hull.find(voxel);
        if (saved == hull.end()) lost.push_back(voxel);
        else voxel->applyDamage(voxel->maxHP() - saved->second);
    }
    for (auto* voxel : lost) ship->removeVoxel(voxel);
    for (const auto& slot : components.hardpoints()) {
        const auto mount = std::find_if(state.mounts.begin(), state.mounts.end(),
            [&slot](const auto& entry) { return entry.cell == slot->voxel()->cell(); });
        slot->setWeapon(mount->weapon
            ? std::shared_ptr<Weapon>(WeaponBuilder(catalogWeapon(mount->weapon).key).build())
            : nullptr);
    }
    player.credits = state.credits;
    player.gun = state.gun;
    player.shield = state.shield;
    player.offer = state.offer;
    player.shieldBuild = state.shieldBuild;
    player.inventory = state.inventory;
    player.creditsEarned = state.creditsEarned;
    player.weaponsFound = state.weaponsFound;
    player.damageUpgrades = state.damageUpgrades;
    player.shieldUpgrades = state.shieldUpgrades;
    player.rewardWeapon = state.rewardWeapon;
    player.rewardUpgrade = state.rewardUpgrade;
    applyUpgrades(player);
    components.refillWeaponEnergy();
    for (auto& slot : components.shieldSlots())
        if (slot->shield()) slot->shield()->refill();
    m_run = state.run;
    m_mission = state.mission;
    m_route = std::move(state.route);
    m_currentNode = state.currentNode;
    m_selectedNode = state.selectedNode;
    m_visitedNodes = state.visitedNodes;
    m_attacksCleared = state.attacksCleared;
    m_enemiesDefeated = state.enemiesDefeated;
    m_basesVisited = state.basesVisited;
    m_previousElapsedTicks = state.elapsedTicks;
    m_offerRandom = random;
    return true;
}

bool ContractRun::alive(const PlayerState& player) const {
    return player.alive && m_world.worldObject(player.actor) != nullptr;
}

EntityId ContractRun::controlledActor(EntityId player) const {
    const auto& state = m_players.at(playerKey(player));
    return alive(state) ? state.actor : EntityId{};
}

EntityId ContractRun::viewActor(EntityId player) const {
    const auto own = controlledActor(player);
    if (own.valid()) return own;
    for (const auto& entry : m_players)
        if (alive(entry.second)) return entry.second.actor;
    return {};
}

bool ContractRun::contains(EntityId player) const {
    return m_players.find(playerKey(player)) != m_players.end();
}

bool ContractRun::inMission() const {
    return m_phase == ContractPhase::Mission;
}

bool ContractRun::isEnemy(EntityId actor) const {
    return std::find(m_enemies.begin(), m_enemies.end(), actor) != m_enemies.end();
}

ContractStatus ContractRun::status(EntityId player) const {
    return statusForPlayer(m_players.at(playerKey(player)));
}

ContractStatus ContractRun::statusForPlayer(const PlayerState& personal) const {
    ContractStatus state;
    state.phase = m_phase;
    state.run = m_run;
    state.alive = alive(personal);
    if (state.alive) {
        state.viewActor = personal.actor;
    } else {
        for (const auto& entry : m_players) {
            if (alive(entry.second)) {
                state.viewActor = entry.second.actor;
                break;
            }
        }
    }
    state.livingPlayers = static_cast<std::uint8_t>(std::count_if(
        m_players.begin(), m_players.end(), [this](const auto& entry) { return alive(entry.second); }));
    state.pendingRewards = m_phase == ContractPhase::Reward
        ? static_cast<std::uint8_t>(std::count_if(m_players.begin(), m_players.end(),
            [this](const auto& entry) { return alive(entry.second) && !entry.second.rewardClaimed; }))
        : 0;
    state.rewardClaimed = personal.rewardClaimed;
    state.rewardWeapon = personal.rewardWeapon;
    state.rewardUpgrade = personal.rewardUpgrade;
    state.damageUpgrades = personal.damageUpgrades;
    state.shieldUpgrades = personal.shieldUpgrades;
    state.creditsEarned = personal.creditsEarned;
    state.weaponsFound = personal.weaponsFound;
    state.attacksCleared = m_attacksCleared;
    state.enemiesDefeated = m_enemiesDefeated;
    state.basesVisited = m_basesVisited;
    state.elapsedTicks = m_phase == ContractPhase::Complete || m_phase == ContractPhase::Defeat
        ? m_previousElapsedTicks + m_endTick - m_startTick : 0;
    state.route = m_route;
    state.currentNode = m_currentNode;
    state.selectedNode = m_selectedNode;
    state.visitedNodes = m_visitedNodes;
    state.mission = m_mission;
    state.credits = personal.credits;
    state.gun = personal.gun;
    state.shield = personal.shield;
    state.ready = personal.ready;
    state.players = static_cast<std::uint8_t>(m_players.size());
    state.readyPlayers = static_cast<std::uint8_t>(std::count_if(
        m_players.begin(), m_players.end(),
        [](const auto& entry) { return entry.second.ready; }));
    state.remaining = static_cast<std::uint16_t>(std::count_if(
        m_enemies.begin(), m_enemies.end(),
        [this](EntityId enemy) { return m_world.worldObject(enemy) != nullptr; }));
    state.result = personal.result;
    state.acknowledgedSequence = personal.lastSequence;
    state.offer = personal.offer;
    state.inventory = personal.inventory;
    state.gunPrice = catalogWeapon(personal.offer).price;
    state.shieldPrice = catalogShield(1).price;
    state.shieldBuild = personal.shieldBuild;
    if (m_phase == ContractPhase::Reward || inMission()) {
        state.reward = encounterReward();
    } else if (m_selectedNode != NoRouteNode) {
        const auto& node = m_route[m_selectedNode];
        if (node.kind == RouteNodeKind::Attack) state.reward = contractLevel(node.level).reward;
        if (node.kind == RouteNodeKind::Encounter) state.reward = contractSignal(node.level).reward;
    }
    auto* object = m_world.worldObject(personal.actor);
    state.invulnerable = personal.protection;
    if (object) {
        const auto blocks = object->voxelMap().size();
        if (personal.originalHullBlocks > blocks)
            state.missingHullBlocks = static_cast<std::uint32_t>(personal.originalHullBlocks - blocks);
        if (m_phase == ContractPhase::Station && state.alive)
            state.repairPrice = repairPrice(personal);
        for (const auto& slot : object->components().shieldSlots()) if (slot->shield()) {
            state.shieldHP += slot->shield()->hp();
            state.shieldCapacity += slot->shield()->maxHP();
            state.shieldRegeneration += slot->shield()->regeneration();
            state.shieldRechargeRemaining = std::max(
                state.shieldRechargeRemaining,slot->shield()->rechargeRemaining());
        }
        state.weaponEnergy = object->components().weaponEnergy();
        state.weaponEnergyCapacity = object->components().weaponEnergyCapacity();
        state.weaponEnergyRegeneration = object->components().weaponEnergyRegeneration();
        state.weaponEnergyRechargeRemaining = object->components().weaponEnergyRechargeRemaining();
        for (const auto& slot : object->components().hardpoints()) {
            state.mounts.push_back({slot->voxel()->cell(), slot->weapon()
                ? catalogWeaponId(slot->weapon()->equipmentKey()) : std::uint8_t(0)});
        }
        std::sort(state.mounts.begin(), state.mounts.end(), [](const auto& a, const auto& b) {
            return a.cell.x != b.cell.x ? a.cell.x < b.cell.x
                : a.cell.y != b.cell.y ? a.cell.y < b.cell.y : a.cell.z < b.cell.z;
        });
    }
    return state;
}

void ContractRun::act(EntityId actor, const ContractAction& action) {
    if (action.kind != ContractActionKind::Ready &&
        action.kind != ContractActionKind::BuyGun &&
        action.kind != ContractActionKind::BuyShield &&
        action.kind != ContractActionKind::EnableInvulnerability &&
        action.kind != ContractActionKind::DisableInvulnerability &&
        action.kind != ContractActionKind::EquipGun &&
        action.kind != ContractActionKind::SelectNode &&
        action.kind != ContractActionKind::ClaimReward &&
        action.kind != ContractActionKind::NewRun &&
        action.kind != ContractActionKind::RepairHull &&
        action.kind != ContractActionKind::SellGun) {
        throw std::invalid_argument("Unknown contract action.");
    }
    auto& player = m_players.at(playerKey(actor));
    const bool protectionSetting =
        action.kind == ContractActionKind::EnableInvulnerability ||
        action.kind == ContractActionKind::DisableInvulnerability;
    if (action.sequence <= player.lastSequence) {
        player.result = ContractResult::StaleRequest;
        return;
    }
    player.lastSequence = action.sequence;
    if (!protectionSetting && action.mission != m_mission) {
        player.result = ContractResult::StaleRequest;
        return;
    }
    auto* object = m_world.worldObject(player.actor);
    if (protectionSetting) {
        player.protection = action.kind == ContractActionKind::EnableInvulnerability;
        if (object) object->info().setInvulnerable(player.protection);
        player.result = ContractResult::Accepted;
        return;
    }
    if (action.kind == ContractActionKind::NewRun) {
        if (m_phase != ContractPhase::Complete && m_phase != ContractPhase::Defeat) {
            player.result = ContractResult::WrongPhase;
            return;
        }
        player.ready = true;
        player.result = ContractResult::Accepted;
        if (std::all_of(m_players.begin(), m_players.end(),
                [](const auto& entry) { return entry.second.ready; }))
            startNewRun();
        return;
    }
    if (!alive(player)) {
        player.result = ContractResult::Unavailable;
        return;
    }
    if (action.kind == ContractActionKind::ClaimReward) {
        player.result = m_phase == ContractPhase::Reward
            ? claimReward(player, action.choice) : ContractResult::WrongPhase;
        if (player.result == ContractResult::Accepted) finishRewards();
        return;
    }
    if (m_phase != ContractPhase::Station && m_phase != ContractPhase::Route) {
        player.result = ContractResult::WrongPhase;
        return;
    }
    if (action.kind == ContractActionKind::SelectNode) {
        if (action.node >= m_route.size() ||
            !(m_route[m_currentNode].links & (1u << action.node))) {
            player.result = ContractResult::InvalidRoute;
        } else {
            if (m_selectedNode != action.node) {
                advanceRevision();
                m_selectedNode = action.node;
                for (auto& entry : m_players) entry.second.ready = false;
            }
            player.result = ContractResult::Accepted;
        }
        return;
    }
    if (action.kind == ContractActionKind::Ready) {
        if (m_selectedNode == NoRouteNode) {
            player.result = ContractResult::InvalidRoute;
            return;
        }
        player.ready = true;
        player.result = ContractResult::Accepted;
        if (std::all_of(m_players.begin(), m_players.end(),
                [this](const auto& entry) { return !alive(entry.second) || entry.second.ready; })) {
            if (m_phase == ContractPhase::Station && !tryCheckpoint()) {
                player.ready = false;
                player.result = ContractResult::CheckpointFailed;
                return;
            }
            advanceRevision();
            if (m_route[m_selectedNode].kind == RouteNodeKind::Base) {
                m_currentNode = m_selectedNode;
                m_visitedNodes |= 1u << m_currentNode;
                chooseNext();
                ++m_basesVisited;
                visitBase();
            } else {
                deploy();
            }
        }
    } else {
        if (m_phase != ContractPhase::Station) {
            player.result = ContractResult::WrongPhase;
            return;
        }
        player.result = action.kind == ContractActionKind::RepairHull ? repair(player)
            : action.kind == ContractActionKind::EquipGun
            ? equip(player, action)
            : action.kind == ContractActionKind::SellGun
            ? sell(player, action.weapon)
            : purchase(player, action.kind == ContractActionKind::BuyGun, action.shield ? action.shield : 1);
        if (player.result == ContractResult::Accepted) {
            player.ready = false;
        }
    }
}

void ContractRun::advanceRevision() {
    if (m_mission == std::numeric_limits<std::uint32_t>::max())
        throw std::overflow_error("Contract route revision exhausted.");
    ++m_mission;
}

void ContractRun::generateRoute() {
    m_route.clear();
    m_route.push_back({RouteNodeKind::Base, 0, 0, 1, 0});
    const auto distress = static_cast<int>(SignalKind::Distress);
    const auto otherSignal = static_cast<int>(std::bernoulli_distribution{}(m_offerRandom)
        ? SignalKind::Derelict : SignalKind::Decoy);
    const bool distressFirst = std::bernoulli_distribution{}(m_offerRandom);
    const auto firstSignal = distressFirst ? distress : otherSignal;
    const auto secondSignal = distressFirst ? otherSignal : distress;
    for (std::uint8_t row = 1; row <= 5; ++row) {
        const int baseLane = std::uniform_int_distribution<int>(0, 2)(m_offerRandom);
        for (std::uint8_t lane = 0; lane < 3; ++lane) {
            auto kind = RouteNodeKind::Attack;
            if (row == 2 || row == 4) {
                if (lane == baseLane) kind = RouteNodeKind::Base;
                else if (lane == (baseLane + 1) % 3) kind = RouteNodeKind::Encounter;
            }
            const auto level = kind == RouteNodeKind::Base ? 0
                : kind == RouteNodeKind::Encounter ? (row == 2 ? firstSignal : secondSignal) : row == 1 ? lane + 1
                : std::uniform_int_distribution<int>(1, 3)(m_offerRandom);
            m_route.push_back({kind, static_cast<std::uint8_t>(level), row, lane, 0});
        }
    }
    m_route.push_back({RouteNodeKind::Attack, FlagshipLevel, 6, 1, 0});
    for (auto& node : m_route) {
        for (std::size_t next = 0; next < m_route.size(); ++next) {
            const auto& candidate = m_route[next];
            if (candidate.row == node.row + 1 &&
                (node.row == 0 || candidate.row == 6 ||
                    std::abs(static_cast<int>(node.lane) - static_cast<int>(candidate.lane)) <= 1))
                node.links |= 1u << next;
        }
    }
    chooseNext();
}

void ContractRun::chooseNext() {
    m_selectedNode = NoRouteNode;
    for (std::size_t i = 0; i < m_route.size(); ++i)
        if (m_route[m_currentNode].links & (1u << i)) {
            m_selectedNode = static_cast<std::uint8_t>(i);
            break;
        }
}

std::uint32_t ContractRun::repairPrice(const PlayerState& player) const {
    const auto* ship = m_world.worldObject(player.actor);
    double missingHP = 0;
    for (const auto& entry : ship->voxelMap()) {
        const auto& voxel = *entry.second;
        if (voxel.hp() > 0) missingHP += voxel.maxHP() - voxel.hp();
    }
    return missingHP > 0 ? static_cast<std::uint32_t>(std::ceil(missingHP / 100.0)) : 0;
}

ContractResult ContractRun::repair(PlayerState& player) {
    const auto price = repairPrice(player);
    if (price == 0) return ContractResult::HullIntact;
    if (player.credits < price) return ContractResult::InsufficientCredits;
    for (const auto& entry : m_world.worldObject(player.actor)->voxelMap())
        entry.second->restoreHP();
    player.credits -= price;
    return ContractResult::Accepted;
}

void ContractRun::visitBase() {
    m_phase = ContractPhase::Station;
    for (auto& entry : m_players) {
        if (!alive(entry.second)) continue;
        refreshOffer(entry.second);
        entry.second.ready = false;
        entry.second.result = ContractResult::Accepted;
        auto* ship = m_world.worldObject(entry.second.actor);
        if (ship) {
            ship->components().refillWeaponEnergy();
            for (auto& slot : ship->components().shieldSlots())
                if (slot->shield()) slot->shield()->refill();
        }
        if (!tryCheckpoint()) m_players.begin()->second.result = ContractResult::CheckpointFailed;
    }
}

ContractResult ContractRun::purchase(PlayerState& player, bool gun, std::uint8_t shieldID) {
    if (!gun && (shieldID == 0 || shieldID > ShieldCatalogSize)) return ContractResult::Incompatible;
    if (gun ? player.gun : player.shieldBuild == shieldID) {
        return ContractResult::AlreadyOwned;
    }
    const auto price = gun ? catalogWeapon(player.offer).price
        : catalogShield(shieldID).price;
    if (player.credits < price) {
        return ContractResult::InsufficientCredits;
    }
    auto* ship = static_cast<Ship*>(m_world.worldObject(player.actor));
    if (gun) {
        unsigned count = 0;
        for (auto quantity : player.inventory) count += quantity;
        if (count >= ContractInventoryCapacity) return ContractResult::InventoryFull;
        ++player.inventory[player.offer - 1];
        player.gun = true;
    } else {
        const auto key = catalogShield(shieldID).key;
        std::vector<std::pair<std::shared_ptr<ShieldSlot>, std::shared_ptr<Shield>>> shields;
        for (auto& slot : ship->components().shieldSlots()) {
            if (!slot->mountable(key)) {
                return ContractResult::Incompatible;
            }
            auto shield = std::shared_ptr<Shield>(ShieldBuilder(key).build());
            if (!shield) {
                throw std::runtime_error("Could not build the contract shop shield.");
            }
            shields.emplace_back(slot, std::move(shield));
        }
        if (shields.empty()) {
            return ContractResult::Incompatible;
        }
        for (auto& entry : shields) {
            entry.first->setShield(entry.second);
        }
        player.shield = true;
        player.shieldBuild = shieldID;
        applyUpgrades(player);
    }
    player.credits -= price;
    return ContractResult::Accepted;
}

ContractResult ContractRun::sell(PlayerState& player, std::uint8_t weapon) {
    if (weapon == 0 || weapon > WeaponCatalogSize) return ContractResult::Incompatible;
    if (player.inventory[weapon - 1] == 0) return ContractResult::Unavailable;
    const auto price = catalogWeapon(weapon).price / 2;
    if (player.credits > std::numeric_limits<std::uint32_t>::max() - price)
        throw std::overflow_error("Contract sale exceeds the credit limit.");
    --player.inventory[weapon - 1];
    player.credits += price;
    return ContractResult::Accepted;
}

void ContractRun::refreshOffer(PlayerState& player) {
    if (player.originalHullBlocks == 0)
        player.originalHullBlocks = m_world.worldObject(player.actor)->voxelMap().size();
    player.offer = static_cast<std::uint8_t>(
        std::uniform_int_distribution<int>(1, WeaponCatalogSize)(m_offerRandom));
    player.gun = false;
}

ContractResult ContractRun::equip(PlayerState& player, const ContractAction& action) {
    if (action.weapon == 0 || action.weapon > WeaponCatalogSize)
        return ContractResult::Incompatible;
    if (player.inventory[action.weapon - 1] == 0) return ContractResult::Unavailable;
    auto* ship = m_world.worldObject(player.actor);
    const auto slot = std::find_if(ship->components().hardpoints().begin(),
        ship->components().hardpoints().end(), [&](const auto& candidate) {
            return candidate->voxel()->cell() == action.cell;
        });
    if (slot == ship->components().hardpoints().end()) return ContractResult::InvalidSlot;
    const auto key = catalogWeapon(action.weapon).key;
    if (!(*slot)->mountable(key)) return ContractResult::Incompatible;
    const auto previous = (*slot)->weapon()
        ? catalogWeaponId((*slot)->weapon()->equipmentKey()) : 0;
    if (previous == action.weapon) return ContractResult::AlreadyOwned;
    if ((*slot)->weapon() && previous == 0)
        throw std::logic_error("Contract hardpoint contains a non-catalog weapon.");
    auto weapon = std::shared_ptr<Weapon>(WeaponBuilder(key).build());
    if (!weapon) throw std::runtime_error("Could not build inventory weapon.");
    (*slot)->setWeapon(weapon);
    --player.inventory[action.weapon - 1];
    if (previous) ++player.inventory[previous - 1];
    return ContractResult::Accepted;
}

namespace {
    float shipRadius(WorldObject& ship) {
        return ship.bounds().sphere().radius();
    }

    std::uint64_t mix(std::uint64_t value) {
        value += 0x9E3779B97F4A7C15ull;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31);
    }
}

void ContractRun::deploy() {
    const auto& node = m_route[m_selectedNode];
    const auto level = node.kind == RouteNodeKind::Encounter ? contractSignal(node.level) : contractLevel(node.level);
    m_reward = level.reward;
    if (level.enemiesPerPlayer == 0) {
        finishEncounter(true);
        return;
    }
    const float engagement = contractEngagementRange();
    // Enemies start beyond engagement range of every berth and close in once alerted.
    const float depth = engagement + 260.0f;
    std::vector<Handle<WorldObject>> targets;
    std::vector<procedural::CoverKeepOut> obstacles;
    for (auto& entry : m_players) {
        WorldObject* ship = m_world.worldObject(entry.second.actor);
        if (!alive(entry.second)) continue;
        targets.push_back(ship->handle<WorldObject>());
        obstacles.push_back({ship->position(), shipRadius(*ship) + 60.0f});
        entry.second.ready = false;
    }
    const bool finale = node.kind == RouteNodeKind::Attack && node.level == FlagshipLevel;
    const std::size_t count = finale ? 1 + targets.size() : targets.size() * level.enemiesPerPlayer;
    Ship* wingman = nullptr;
    if (level.rescueBonus) {
        wingman = WorldObjectBuilder(DistressWingmanDefinition, m_world).buildShip();
        wingman->transform().setPosition(glm::vec3(-110.0f, 0.0f, -90.0f));
        m_world.god().scheduleSpawn(wingman);
        targets.push_back(wingman->handle<WorldObject>());
        obstacles.push_back({wingman->position(), shipRadius(*wingman) + 40.0f});
    }
    std::vector<Ship*> enemies;
    for (std::size_t index = 0; index < count; ++index) {
        const bool flagship = finale && index == 0;
        const auto definition = finale && !flagship ? "contractraider" : level.enemy;
        Ship* enemy = WorldObjectBuilder(definition, m_world).buildShip();
        glm::vec3 position(static_cast<float>(index) * 70.0f, 0.0f, -depth);
        if (level.formation == 2)
            position = glm::vec3(index % 2 ? 150.0f : -150.0f,
                (index / 2) * 30.0f, -depth - (index / 2) * 60.0f);
        if (level.formation == 3)
            position = glm::vec3(static_cast<float>(index) * 90.0f, 30.0f, -depth - 100.0f);
        if (finale) position = flagship ? glm::vec3(35.0f, 0.0f, -depth - 110.0f) :
            glm::vec3(index % 2 ? -115.0f : 135.0f, 35.0f * (index / 2), -depth - 30.0f);
        enemy->transform().setPosition(position);
        enemy->info().setName(finale && !flagship ? "REGENT ESCORT" : level.name);
        auto task = flagship
            ? std::make_shared<FightTask>(enemy->boardComputer(), targets,
                AiArchetype::Cruiser, ShootingSkill{0.80f,0.55f,0.60f})
            : std::make_shared<FightTask>(enemy->boardComputer(), targets, AiArchetype::Interceptor);
        task->setEngagementRange(engagement);
        enemy->character()->setTask(task);
        m_world.god().scheduleSpawn(enemy);
        enemies.push_back(enemy);
        obstacles.push_back({position, shipRadius(*enemy) + 60.0f});
    }
    // Kept apart from m_offerRandom so cover never shifts reward or route rolls.
    const std::uint64_t coverSeed = mix(m_world.simulationTickIndex() ^ mix(m_selectedNode + 1));
    m_coverLayout = level.layouts[mix(coverSeed) % level.layouts.size()];
    procedural::CoverArena arena;
    arena.nearZ = -170.0f;
    arena.farZ = -(depth - 150.0f);
    arena.centerX = 80.0f;
    arena.halfWidth = 520.0f;
    arena.keepOut = std::move(obstacles);
    for (const auto& cover : procedural::generateCoverLayout(m_coverLayout, coverSeed, arena))
        m_world.god().scheduleSpawn(procedural::buildCover(m_world, cover.piece, cover.position));
    m_world.god().spawn();
    if (wingman) {
        const auto id = m_world.entityId(wingman);
        if (!id) throw std::runtime_error("Could not spawn the distress wingman.");
        m_wingman = *id;
        m_rescueBonus = level.rescueBonus;
        std::vector<Handle<WorldObject>> hostiles;
        for (auto* enemy : enemies) hostiles.push_back(enemy->handle<WorldObject>());
        auto task = std::make_shared<FightTask>(wingman->boardComputer(),
            hostiles, AiArchetype::Interceptor, ShootingSkill{0.65f,0.55f,0.55f});
        task->setEngagementRange(engagement);
        wingman->character()->setTask(task);
    }
    m_enemies.clear();
    for (Ship* enemy : enemies) {
        const auto id = m_world.entityId(enemy);
        if (!id) {
            throw std::runtime_error("Could not spawn a contract enemy.");
        }
        m_enemies.push_back(*id);
        const int group = Property<int>::get(enemy->info().definitionKey() + ".general.shieldProjectorGroup", -1);
        if (group >= 0) {
            const auto& mounts = enemy->components().hardpoints();
            const auto projector = std::find_if(mounts.begin(), mounts.end(),
                [group](const auto& mount) { return mount->group() == group; });
            if (projector == mounts.end()) throw std::logic_error("Contract shield projector has no matching mount.");
            m_projectors.emplace_back(*id, (*projector)->voxel()->cell());
        }
    }
    m_phase = ContractPhase::Mission;
    m_remainingEnemies = m_enemies.size();
}

procedural::CoverLayout ContractRun::coverLayout() const {
    return m_coverLayout;
}

std::uint32_t ContractRun::encounterReward() const {
    return m_reward + (m_wingman.valid() && m_world.worldObject(m_wingman) ? m_rescueBonus : 0);
}

void ContractRun::update() {
    if (m_phase == ContractPhase::Complete || m_phase == ContractPhase::Defeat) return;
    for (auto& entry : m_players) {
        if (!alive(entry.second)) {
            entry.second.alive = false;
            entry.second.ready = false;
        }
    }
    if (inMission()) {
        m_projectors.erase(std::remove_if(m_projectors.begin(), m_projectors.end(),
            [this](const auto& projector) {
                auto* enemy = m_world.worldObject(projector.first);
                if (!enemy) return true;
                if (enemy->voxel(projector.second)) return false;
                for (auto& slot : enemy->components().shieldSlots()) slot->setShield(nullptr);
                return true;
            }), m_projectors.end());
        alertSquad();
        const auto remaining = static_cast<std::size_t>(std::count_if(m_enemies.begin(), m_enemies.end(),
            [this](EntityId enemy) { return m_world.worldObject(enemy) != nullptr; }));
        m_enemiesDefeated += static_cast<std::uint16_t>(m_remainingEnemies - remaining);
        m_remainingEnemies = remaining;
    }
    if (std::none_of(m_players.begin(), m_players.end(),
            [this](const auto& entry) { return alive(entry.second); }))
        finishEncounter(false);
    else if (inMission() && m_remainingEnemies == 0) finishEncounter(true);
    else if (m_phase == ContractPhase::Reward) finishRewards();
}

void ContractRun::alertSquad() {
    // Once any hostile engages, the whole squad and the stranded wingman join the fight.
    std::vector<std::shared_ptr<FightTask>> tasks;
    bool alerted = false;
    auto collect = [&](EntityId id) {
        auto* ship = dynamic_cast<Ship*>(m_world.worldObject(id));
        if (!ship || !ship->character()) return;
        auto task = std::dynamic_pointer_cast<FightTask>(ship->character()->task());
        if (!task) return;
        alerted = alerted || task->engaged();
        tasks.push_back(task);
    };
    for (EntityId enemy : m_enemies) collect(enemy);
    if (m_wingman.valid()) collect(m_wingman);
    if (alerted) for (auto& task : tasks) task->engage();
}

void ContractRun::finishEncounter(bool success) {
    advanceRevision();
    if (success) {
        m_reward = encounterReward();
        m_currentNode = m_selectedNode;
        m_visitedNodes |= 1u << m_currentNode;
        chooseNext();
    }
    m_phase = success ? ContractPhase::Reward : ContractPhase::Defeat;
    if (success) {
        if (m_route[m_currentNode].kind == RouteNodeKind::Attack) ++m_attacksCleared;
    } else m_endTick = m_world.simulationTickIndex();
    clearEncounter();
    std::size_t index = 0;
    for (auto& entry : m_players) {
        auto& player = entry.second;
        player.ready = false;
        player.rewardClaimed = false;
        player.result = success ? ContractResult::MissionCompleted : ContractResult::RunDefeated;
        if (success) {
            player.rewardWeapon = static_cast<std::uint8_t>(
                std::uniform_int_distribution<int>(2, WeaponCatalogSize)(m_offerRandom));
            player.rewardUpgrade = player.rewardUpgrade
                ? 3 - player.rewardUpgrade : static_cast<std::uint8_t>(
                    std::uniform_int_distribution<int>(1, 2)(m_offerRandom));
            if (player.damageUpgrades == MaxRunUpgradeStacks) player.rewardUpgrade = 2;
            if (player.shieldUpgrades == MaxRunUpgradeStacks) player.rewardUpgrade = 1;
        }
        auto* ship = m_world.worldObject(player.actor);
        if (ship) {
            ship->updateTransformAndGeode(
                berth(index++), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            ship->physics().setSpeed(Speed());
            ship->components().setEngineState(EngineState());
        }
    }
}

void ContractRun::clearEncounter(bool removePlayers) {
    m_enemies.clear();
    m_wingman = {};
    m_rescueBonus = 0;
    m_projectors.clear();
    m_world.god().spawn();
    for (WorldObject* object : m_world.worldObjects()) {
        const auto id = m_world.entityId(object);
        const bool pilot = id && std::any_of(m_players.begin(), m_players.end(),
            [id](const auto& entry) { return entry.second.actor == *id; });
        if (removePlayers || !pilot) {
            m_world.god().scheduleRemoval(object);
        }
    }
    m_world.god().remove();
}

void ContractRun::applyUpgrades(PlayerState& player) {
    auto* ship = m_world.worldObject(player.actor);
    if (!ship) throw std::logic_error("Cannot upgrade a missing contract ship.");
    ship->info().setWeaponDamageMultiplier(1.0f + player.damageUpgrades * m_damageBonus);
    ship->components().setWeaponEnergyUpgradeBonuses(
        1.0f + player.damageUpgrades * PayloadRegenBonusPerStack,
        std::min(MaxShieldedEnergyDiscount, player.shieldUpgrades * ShieldedEnergyDiscountPerStack));
    for (auto& slot : ship->components().shieldSlots()) {
        auto shield = slot->shield();
        if (!shield) continue;
        const float fraction = shield->maxHP() > 0 ? shield->hp() / shield->maxHP() : 0;
        shield->setMaxHP(Property<float>(shield->equipmentKey() + ".general.hp") *
            (1.0f + player.shieldUpgrades * m_shieldBonus));
        shield->setHP(shield->maxHP() * fraction);
    }
}

ContractResult ContractRun::claimReward(PlayerState& player, RewardChoice choice) {
    if (player.rewardClaimed) return ContractResult::RewardAlreadyClaimed;
    if (choice == RewardChoice::Credits) {
        if (player.credits > std::numeric_limits<std::uint32_t>::max() - m_reward ||
            player.creditsEarned > std::numeric_limits<std::uint32_t>::max() - m_reward)
            throw std::overflow_error("Contract reward credits overflow.");
        player.credits += m_reward;
        player.creditsEarned += m_reward;
    } else if (choice == RewardChoice::Weapon) {
        unsigned count = 0;
        for (auto quantity : player.inventory) count += quantity;
        if (count >= ContractInventoryCapacity) return ContractResult::InventoryFull;
        ++player.inventory[player.rewardWeapon - 1];
        ++player.weaponsFound;
    } else if (choice == RewardChoice::Upgrade) {
        auto& stacks = player.rewardUpgrade == 1 ? player.damageUpgrades : player.shieldUpgrades;
        if (stacks >= MaxRunUpgradeStacks) return ContractResult::InvalidReward;
        ++stacks;
        applyUpgrades(player);
    } else return ContractResult::InvalidReward;
    player.rewardClaimed = true;
    return ContractResult::Accepted;
}

void ContractRun::finishRewards() {
    if (std::any_of(m_players.begin(), m_players.end(),
            [this](const auto& entry) { return alive(entry.second) && !entry.second.rewardClaimed; }))
        return;
    advanceRevision();
    m_phase = m_route[m_currentNode].links ? ContractPhase::Route : ContractPhase::Complete;
    if (m_phase == ContractPhase::Complete) m_endTick = m_world.simulationTickIndex();
}

void ContractRun::startNewRun() {
    if (m_run == std::numeric_limits<std::uint32_t>::max())
        throw std::overflow_error("Contract run counter exhausted.");
    advanceRevision();
    clearEncounter(true);
    const auto actors = createPlayers(m_world, m_players.size());
    std::size_t index = 0;
    for (auto& entry : m_players) {
        const auto sequence = entry.second.lastSequence;
        const bool protection = entry.second.protection;
        entry.second = PlayerState{actors[index++]};
        entry.second.lastSequence = sequence;
        entry.second.protection = protection;
        m_world.worldObject(entry.second.actor)->info().setInvulnerable(protection);
        refreshOffer(entry.second);
    }
    ++m_run;
    m_currentNode = 0;
    m_visitedNodes = 1;
    m_reward = 0;
    m_attacksCleared = m_enemiesDefeated = 0;
    m_basesVisited = 1;
    m_remainingEnemies = 0;
    m_startTick = m_world.simulationTickIndex();
    m_endTick = 0;
    m_previousElapsedTicks = 0;
    generateRoute();
    m_phase = ContractPhase::Station;
    if (!tryCheckpoint()) m_players.begin()->second.result = ContractResult::CheckpointFailed;
}

void ContractRun::removePlayer(EntityId player) {
    auto* ship = m_world.worldObject(m_players.at(playerKey(player)).actor);
    if (ship) {
        m_world.god().scheduleRemoval(ship);
        m_world.god().remove();
    }
    m_players.erase(playerKey(player));
    for (auto& entry : m_players) {
        entry.second.ready = false;
    }
    update();
}
