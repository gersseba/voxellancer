#include "soundmanager.h"

#ifndef __EMSCRIPTEN__
#include <SFML/Audio.hpp>
#endif

#include <glow/logging.h>

#include "sound.h"
#include "soundproperties.h"
#include "property/property.h"

#ifdef __EMSCRIPTEN__
extern "C" void setWebSoundListener(float x, float y, float z);
#endif

SoundManager* SoundManager::s_current;
static const int CLEANUP_PERIOD = 100;

SoundManager::SoundManager() :
    m_buffer(),
    m_sounds(),
    m_nextCleanup(0)
{
    setListener(glm::vec3(0), glm::quat());
}

SoundManager::~SoundManager() {
    if (s_current == this) {
        s_current = nullptr;
    }
}

void SoundManager::setListener(const glm::vec3& p, const glm::quat& orientation) {
    const bool music = Property<bool>("audio.musicEnabled",true).get();
    const bool effects = Property<bool>("audio.effectsEnabled",true).get();
    if (music != m_musicEnabled || effects != m_effectsEnabled) {
        m_musicEnabled = music;
        m_effectsEnabled = effects;
        for (auto& entry : m_sounds) entry.sound->setMuted(muted(entry.category));
    }
    m_listenerPosition = p;
#ifndef __EMSCRIPTEN__
    glm::vec3 d = orientation * glm::vec3(0, 0, -1);
    sf::Listener::setPosition(sf::Vector3f(p.x, p.y, p.z));
    sf::Listener::setDirection(sf::Vector3f(d.x, d.y, d.z));
#else
    (void)orientation;
    setWebSoundListener(p.x, p.y, p.z);
#endif
}

bool SoundManager::muted(Category category) const {
    return category == Category::Music ? !m_musicEnabled :
        category == Category::Effects ? !m_effectsEnabled : false;
}

std::shared_ptr<Sound> SoundManager::play(std::string soundFile, const glm::vec3& position,
    bool relative, Category category) {
    if (soundFile == "null") {
        return std::make_shared<Sound>();
    }
    std::shared_ptr<Sound> sound = create(soundFile,category);
    sound->setPosition(position).setRelativeToListener(relative).play();
    return sound;
}

std::shared_ptr<Sound> SoundManager::play(const SoundProperties& soundProperties, const glm::vec3& position,
    bool relative, Category category) {
    std::shared_ptr<Sound> sound = play(soundProperties.sound(), position, relative,category);
    sound->setVolume(soundProperties.volume());
    sound->setLooping(soundProperties.looping());
    sound->setAttenuation(soundProperties.attenuation());
    return sound;
}

void SoundManager::playCombatCues(CombatCueBudget& cues) {
    m_combatVoices.remove_if([](const auto& voice) {
        return voice.sound->status() != Sound::Status::Playing;
    });
    for (auto& voice : m_combatVoices) {
        const auto offset = voice.cue.position - m_listenerPosition;
        voice.cue.distanceSquared = glm::dot(offset, offset);
    }
    cues.prioritize();
    for (const auto& cue : cues) {
        auto replace = m_combatVoices.end();
        if (!CombatCueBudget::admit(cue, m_combatVoices.begin(), m_combatVoices.end(), replace)) continue;

        const char* file = cue.kind == CombatCueKind::Fire ? "data/sound/laser.ogg" :
            cue.kind == CombatCueKind::Shield ? "data/sound/hit.wav" : "data/sound/hit2.ogg";
        const float gain = cue.personal ? (cue.kind == CombatCueKind::Fire ? 0.10f : 0.18f) : 0.06f;
        auto sound = create(file);
        if (sound->status() == Sound::Status::Null) continue;
        if (replace != m_combatVoices.end()) {
            replace->sound->stop();
            m_combatVoices.erase(replace);
        }
        sound->setRelativeToListener(cue.personal)
            .setPosition(cue.personal ? glm::vec3(0, 0, -1) : cue.position)
            .setMinDistance(22.0f).setAttenuation(0.45f).setVolume(gain).play();
        m_combatVoices.push_back({cue, std::move(sound)});
    }
}

void SoundManager::stopCombatCues() {
    for (auto& voice : m_combatVoices) voice.sound->stop();
    m_combatVoices.clear();
}

std::shared_ptr<Sound> SoundManager::create(std::string soundFile, Category category) {
    cleanUp();

#ifdef __EMSCRIPTEN__
    std::shared_ptr<Sound> sound(std::make_shared<Sound>(soundFile));
    if (sound->status() == Sound::Status::Null) {
        glow::warning("SoundManager: could not load %;", soundFile);
        return sound;
    }
#else
    sf::SoundBuffer* buffer = obtain(soundFile);
    if (!buffer) {
        return createNullSound();
    }
    if (buffer->getChannelCount() != 1) {
        glow::warning("SoundManager: %; sounds don't work with more than one channel!", soundFile);
        return createNullSound();
    }

    std::shared_ptr<Sound> sound(std::make_shared<Sound>(*buffer));
#endif
    sound->setAttenuation(0.1f);
    sound->setMinDistance(5.0f);
    sound->setMuted(muted(category));
    m_sounds.push_back({sound,category});

    return sound;
}

#ifndef __EMSCRIPTEN__
sf::SoundBuffer* SoundManager::obtain(std::string soundFile) {
    sf::SoundBuffer* buffer = m_buffer[soundFile];
    if (buffer == nullptr) {
        buffer = new sf::SoundBuffer();
        if (!buffer->loadFromFile(soundFile)) {
            glow::warning("SoundManager: could not load %;", soundFile);
            return nullptr;
        }
        m_buffer[soundFile] = buffer;
    }
    return buffer;
}
#endif

void SoundManager::cleanUp() {
    if (m_nextCleanup++ < CLEANUP_PERIOD) {
        return;
    }
    for (auto iter = m_sounds.begin(); iter != m_sounds.end();)
    {
        if (iter->sound.use_count() == 1 && iter->sound->status() == Sound::Status::Stopped) {
            iter = m_sounds.erase(iter);
        } else {
            ++iter;
        }
    }
    m_nextCleanup = 0;
}

void SoundManager::forcedCleanup() {
    m_nextCleanup = CLEANUP_PERIOD;
    cleanUp();
}

SoundManager* SoundManager::current() {
    assert(s_current != nullptr);
    return s_current;
}

void SoundManager::activate() {
    if (s_current == this) {
        return;
    }
    assert(s_current == nullptr);

    s_current = this;

    for (auto& entry : m_sounds) {
        const auto& sound = entry.sound;
        if (sound->status() == Sound::Status::Paused) {
            sound->play();
        }
    }
}

void SoundManager::deactivate() {
    assert(s_current == this);
    s_current = nullptr;

    forcedCleanup();


    for (auto& entry : m_sounds) {
        const auto& sound = entry.sound;
        if (sound->status() == Sound::Status::Playing) {
            sound->pause();
        }
    }
}

void SoundManager::stopAll() {
    for (auto& entry : m_sounds) {
        const auto& sound = entry.sound;
        if (sound->status() != Sound::Status::Stopped) {
            sound->stop();
        }
    }
    forcedCleanup();
}

std::shared_ptr<Sound> SoundManager::createNullSound() {
    return std::make_shared<Sound>();
}


