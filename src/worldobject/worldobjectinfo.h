#pragma once

#include <string>

#include <glm/glm.hpp>


class WorldObject;

class WorldObjectInfo {
public:
    WorldObjectInfo(WorldObject& worldObject);

    std::string name();
    void setName(const std::string& name);
    const std::string& definitionKey() const;
    void setDefinitionKey(const std::string& key);

    bool showOnHud();
    void setShowOnHud(bool show);

    bool canLockOn();
    void setCanLockOn(bool canLockOn);

    std::string shieldStatus();


protected:
    WorldObject& m_worldObject;
    std::string m_name;
    std::string m_definitionKey;
    bool m_showOnHud;
    bool m_canLockOn;
};
