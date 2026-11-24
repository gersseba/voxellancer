#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <list>

#include "combatcuebudget.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>


namespace sf {
    class SoundBuffer;
}

class Sound;
class SoundProperties;

class SoundManager {
public:
    SoundManager();
    ~SoundManager();

    void setListener(const glm::vec3& position, const glm::quat& orientation);
    std::shared_ptr<Sound> create(std::string soundFile);
    std::shared_ptr<Sound> play(std::string soundFile, const glm::vec3& position, bool relative = false);
    std::shared_ptr<Sound> play(const SoundProperties& soundProperties, const glm::vec3& position, bool relative = false);
    void playCombatCues(CombatCueBudget& cues);
    void stopCombatCues();

    void activate();
    void deactivate();


    void stopAll();

    static SoundManager* current();


protected:
    struct CombatVoice {
        CombatCue cue;
        std::shared_ptr<Sound> sound;
    };
    std::list<CombatVoice> m_combatVoices;
    glm::vec3 m_listenerPosition{0.0f};
    std::unordered_map<std::string, sf::SoundBuffer*> m_buffer;
    std::list<std::shared_ptr<Sound>> m_sounds;
    int m_nextCleanup;

    sf::SoundBuffer* obtain(std::string soundFile);
    void cleanUp();
    void forcedCleanup();
    std::shared_ptr<Sound> createNullSound();

    static SoundManager* s_current;
};

