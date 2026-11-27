#pragma once

#include <cstddef>
#include <string>

#include "scenarios/basescenario.h"
#include "utils/handle/handle.h"

class Ship;

class WeaponRangeScenario : public BaseScenario {
public:
    static constexpr int ScenarioId = 10;
    static constexpr std::size_t GunCount = 10;
    static std::string gunKey(std::size_t index);
    static std::string gunName(std::size_t index);

    explicit WeaponRangeScenario(World& world);
    void selectGun(std::size_t index);
    void selectNumber(unsigned number);
    void update(float deltaSec);
    std::size_t selectedGun() const { return m_selectedGun; }
    const std::string& selectedName() const { return m_selectedName; }
    bool showSelection() const { return m_noticeRemaining > 0; }
    Ship* playerShip() { return m_player.get(); }
    Ship* targetShip() { return m_target.get(); }

protected:
    void populateWorld() override;

private:
    Handle<Ship> m_player;
    Handle<Ship> m_target;
    std::size_t m_selectedGun = 0;
    std::string m_selectedName;
    float m_noticeRemaining = 0;
};
