#pragma once

#include <memory>
#include <optional>
#include <random>
#include <vector>
#include <glm/glm.hpp>

#include "equipment/weapons/shotstatistics.h"

class BoardComputer;
class Hardpoint;
class Weapon;

struct ShootingSkill {
    float lead = 1;
    float tracking = 1;
    float accuracy = 1;

    void validate() const;
    static ShootingSkill defaults();
};

class ShootingController {
public:
    ShootingController(BoardComputer* computer, const ShootingSkill& skill);
    void setSkill(const ShootingSkill& skill);
    const ShootingSkill& skill() const { return m_skill; }
    void seed(std::uint32_t seed);
    void update(float deltaSec, std::vector<Handle<WorldObject>>& targets,
        WorldObject* preferredTarget, bool fire = true);
    std::optional<glm::vec3> aimPoint(const Hardpoint* mount) const;
    std::shared_ptr<ShotStatistics> statistics() const { return m_statistics; }

private:
    struct Aim {
        Hardpoint* mount = nullptr;
        Weapon* weapon = nullptr;
        Handle<WorldObject> target;
        glm::vec3 direction{0,0,-1};
        glm::vec3 point{0};
        glm::vec3 projectedAreas{0};
        std::size_t voxelCount = 0;
    };

    BoardComputer* m_computer;
    ShootingSkill m_skill;
    std::vector<Aim> m_aims;
    std::mt19937 m_random;
    std::normal_distribution<float> m_normal{0,1};
    std::shared_ptr<ShotStatistics> m_statistics;
    bool m_recharging = false;
    bool m_seeded = false;
};
