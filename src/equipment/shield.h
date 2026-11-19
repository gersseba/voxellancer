#pragma once

#include "equipment.h"

#include <glm/glm.hpp>

#include "shieldtype.h"

class ShieldSlot;
class WorldObject;

class Shield : public Equipment {
public:
    Shield(const std::string& key);
    virtual ~Shield();

    ShieldSlot* shieldSlot();
    void setShieldSlot(ShieldSlot* shieldSlot);

    float hp() const;
    void setHP(float hp);

    float maxHP() const;
    void setMaxHP(float maxHP);

    int maxHits() const;
    void setMaxHits(int maxHits);

    float regeneration() const;
    void setRegeneration(float regeneration);

    bool depleted() const;
    bool registerHit();
    bool intersectsSegment(
        WorldObject& protectedObject,
        ShieldType shieldType,
        const glm::vec3& start,
        const glm::vec3& end,
        float projectileRadius,
        glm::ivec3& impactCell,
        float& hitParameter
    ) const;

    float compensate(float damage);

    virtual void update(float deltaSec);


protected:
    float m_hp;
    float m_regeneration;
    float m_maxHP;
    ShieldSlot* m_shieldSlot;
    unsigned int m_hitCount;
    int m_maxHits;

    virtual void onDamageTaken(float damage);
};
