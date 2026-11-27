#pragma once

#include <cstddef>
#include <map>
#include <array>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "procedural/coverlayout.h"
#include "protocol/serverprotocol.h"

class World;

// Replication radius for contract arenas, which open with enemies beyond weapon range.
inline constexpr float ContractInterestRadius = 2200.0f;

class ContractRun {
public:
    static std::vector<voxellancer::protocol::EntityId> createPlayers(
        World& world, std::size_t count);

    ContractRun(World& world, const std::vector<voxellancer::protocol::EntityId>& players,
        std::filesystem::path checkpoint = {}, bool allowClientShieldShape = false);
    void update();
    void act(voxellancer::protocol::EntityId player,
        const voxellancer::protocol::ContractAction& action);
    void removePlayer(voxellancer::protocol::EntityId player);
    bool contains(voxellancer::protocol::EntityId player) const;
    bool isEnemy(voxellancer::protocol::EntityId actor) const;
    voxellancer::protocol::ContractStatus status(voxellancer::protocol::EntityId player) const;
    bool inMission() const;
    voxellancer::protocol::EntityId controlledActor(voxellancer::protocol::EntityId player) const;
    voxellancer::protocol::EntityId viewActor(voxellancer::protocol::EntityId player) const;
    // The cover layout of the current or most recent sortie.
    procedural::CoverLayout coverLayout() const;

private:
    struct PlayerState {
        voxellancer::protocol::EntityId actor;
        bool alive = true, protection = false, rewardClaimed = false;
        std::uint32_t credits = 0;
        std::size_t originalHullBlocks = 0;
        bool gun = false;
        bool shield = false;
        bool ready = false;
        std::uint8_t offer = 1;
        std::uint8_t shieldBuild = 0;
        std::array<std::uint16_t, voxellancer::protocol::ContractWeaponKinds> inventory{};
        std::uint64_t lastSequence = 0;
        std::uint32_t creditsEarned = 0;
        std::uint16_t weaponsFound = 0;
        std::uint8_t damageUpgrades = 0, shieldUpgrades = 0;
        std::uint8_t rewardWeapon = 0, rewardUpgrade = 0;
        voxellancer::protocol::ContractResult result =
            voxellancer::protocol::ContractResult::None;
    };

    World& m_world;
    std::filesystem::path m_checkpoint;
    bool m_allowClientShieldShape;
    // Participant keys survive ship loss; PlayerState::actor changes only on a fresh run.
    std::map<std::uint64_t, PlayerState> m_players;
    std::vector<voxellancer::protocol::EntityId> m_enemies;
    voxellancer::protocol::EntityId m_wingman;
    std::uint32_t m_rescueBonus = 0;
    procedural::CoverLayout m_coverLayout = procedural::CoverLayout::Empty;
    std::vector<std::pair<voxellancer::protocol::EntityId, glm::ivec3>> m_projectors;
    std::uint32_t m_mission = 0;
    voxellancer::protocol::ContractPhase m_phase = voxellancer::protocol::ContractPhase::Station;
    std::vector<voxellancer::protocol::RouteNode> m_route;
    std::uint8_t m_currentNode = 0, m_selectedNode = 1;
    std::uint32_t m_visitedNodes = 1, m_reward = 0;
    std::uint32_t m_run = 1;
    std::uint64_t m_startTick = 0, m_endTick = 0;
    std::uint64_t m_previousElapsedTicks = 0;
    std::uint16_t m_attacksCleared = 0, m_enemiesDefeated = 0, m_basesVisited = 1;
    std::size_t m_remainingEnemies = 0;
    float m_damageBonus, m_shieldBonus;
    std::mt19937 m_offerRandom{std::random_device{}()};

    void deploy();
    void applyRequestedShieldShape(ShieldType shape);
    void alertSquad();
    std::uint32_t encounterReward() const;
    void finishEncounter(bool success);
    void generateRoute();
    void chooseNext();
    void visitBase();
    void saveCheckpoint() const;
    bool loadCheckpoint();
    bool tryCheckpoint();
    voxellancer::protocol::ContractStatus statusForPlayer(const PlayerState& personal) const;
    std::uint32_t repairPrice(const PlayerState& player) const;
    voxellancer::protocol::ContractResult repair(PlayerState& player);
    void advanceRevision();
    voxellancer::protocol::ContractResult purchase(PlayerState& player, bool gun,
        std::uint8_t shield = 1);
    voxellancer::protocol::ContractResult equip(PlayerState& player,
        const voxellancer::protocol::ContractAction& action);
    voxellancer::protocol::ContractResult sell(PlayerState& player, std::uint8_t weapon);
    void refreshOffer(PlayerState& player);
    bool alive(const PlayerState& player) const;
    void clearEncounter(bool removePlayers = false);
    void startNewRun();
    void finishRewards();
    void applyUpgrades(PlayerState& player);
    voxellancer::protocol::ContractResult claimReward(PlayerState& player,
        voxellancer::protocol::RewardChoice choice);
};
