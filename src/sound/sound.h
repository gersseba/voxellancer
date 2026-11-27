#pragma once

#include <memory>
#include <string>
#include <glm/glm.hpp>


namespace sf {
    class Sound;
    class SoundBuffer;
}

class Sound {
public:
    enum class Status {
        Paused, Playing, Stopped, Null
    };

    Sound();
#ifdef __EMSCRIPTEN__
    explicit Sound(const std::string& path);
#else
    Sound(const sf::SoundBuffer& sound);
#endif
    ~Sound();

    Status status();

    void play();
    void stop();
    void pause();

    Sound& setPosition(const glm::vec3& position);
    Sound& setVolume(float volume);
    Sound& setMuted(bool muted);
    Sound& setAttenuation(float attenuation);
    Sound& setLooping(bool loop);
    Sound& setRelativeToListener(bool relative);
    Sound& setMinDistance(float distance);

protected:
    float m_volume = 1;
    bool m_muted = false;
#ifdef __EMSCRIPTEN__
    int m_sound;
#else
    std::unique_ptr<sf::Sound> m_sound;
#endif
};

