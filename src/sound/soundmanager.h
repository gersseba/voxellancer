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
    enum class Category { Effects, Music, Voice };
    SoundManager();
    ~SoundManager();

    void setListener(const glm::vec3& position, const glm::quat& orientation);
    std::shared_ptr<Sound> create(std::string soundFile, Category category = Category::Effects);
    std::shared_ptr<Sound> play(std::string soundFile, const glm::vec3& position,
        bool relative = false, Category category = Category::Effects);
    std::shared_ptr<Sound> play(const SoundProperties& soundProperties, const glm::vec3& position,
        bool relative = false, Category category = Category::Effects);
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
    struct ManagedSound {
        std::shared_ptr<Sound> sound;
        Category category;
    };
    std::list<CombatVoice> m_combatVoices;
    glm::vec3 m_listenerPosition{0.0f};
    std::unordered_map<std::string, sf::SoundBuffer*> m_buffer;
    std::list<ManagedSound> m_sounds;
    bool m_musicEnabled = true, m_effectsEnabled = true;
    int m_nextCleanup;

    sf::SoundBuffer* obtain(std::string soundFile);
    void cleanUp();
    void forcedCleanup();
    std::shared_ptr<Sound> createNullSound();
    bool muted(Category category) const;

    static SoundManager* s_current;
};

