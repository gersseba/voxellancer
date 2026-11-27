#include "sound/sound.h"

#include <algorithm>
#include <emscripten.h>

EM_JS(int, createWebSound, (const char* path), {
    const name = UTF8ToString(path);
    if (!Module.voxelAudio) {
        Module.voxelAudio = {next: 1, sounds: new Map(), urls: new Map(), listener: [0, 0, 0]};
        console.info("Browser audio uses distance attenuation without spatial panning.");
    }
    const state = Module.voxelAudio;
    try {
        let url = state.urls.get(name);
        if (!url) {
            const mime = name.endsWith(".ogg") ? "audio/ogg" : "audio/wav";
            url = URL.createObjectURL(new Blob([FS.readFile(name)], {type: mime}));
            state.urls.set(name, url);
        }
        const audio = new Audio(url);
        audio.addEventListener("error", () => console.error("Could not play audio: " + name));
        const id = state.next++;
        state.sounds.set(id, {audio, started: false, stopped: true, volume: 1,
            attenuation: 0.1, minDistance: 5, relative: false, position: [0, 0, 0]});
        return id;
    } catch (error) {
        console.error("Could not load audio " + name + ": " + error);
        return 0;
    }
});

EM_JS(void, updateWebSoundVolume, (int id), {
    const state = Module.voxelAudio;
    const sound = state.sounds.get(id);
    if (!sound) return;
    const p = sound.position;
    const l = state.listener;
    const distance = sound.relative ? Math.hypot(p[0], p[1], p[2]) :
        Math.hypot(p[0] - l[0], p[1] - l[1], p[2] - l[2]);
    const gain = sound.minDistance / (sound.minDistance +
        sound.attenuation * Math.max(0, distance - sound.minDistance));
    sound.audio.volume = Math.min(1, Math.max(0, sound.volume * gain));
});

extern "C" {
EM_JS(void, setWebSoundListener, (float x, float y, float z), {
    const state = Module.voxelAudio;
    if (!state) return;
    state.listener = [x, y, z];
    for (const sound of state.sounds.values()) {
        const p = sound.position;
        const distance = sound.relative ? Math.hypot(p[0], p[1], p[2]) :
            Math.hypot(p[0] - x, p[1] - y, p[2] - z);
        const gain = sound.minDistance / (sound.minDistance +
            sound.attenuation * Math.max(0, distance - sound.minDistance));
        sound.audio.volume = Math.min(1, Math.max(0, sound.volume * gain));
    }
});
}

EM_JS(void, webSoundCommand, (int id, int command), {
    const sound = Module.voxelAudio.sounds.get(id);
    if (!sound) return;
    if (command === 0) {
        sound.started = true;
        sound.stopped = false;
        sound.audio.play().catch(error => {
            if (error.name === "AbortError" || sound.stopped) return;
            sound.stopped = true;
            console.warn("Browser audio playback failed: " + error);
            if (Module.showNotice) Module.showNotice("Audio could not start. Interact with the game to enable sound.");
        });
    } else if (command === 1) {
        sound.audio.pause();
        sound.audio.currentTime = 0;
        sound.stopped = true;
    } else {
        sound.audio.pause();
    }
});

EM_JS(int, webSoundStatus, (int id), {
    const sound = Module.voxelAudio.sounds.get(id);
    if (!sound) return 3;
    if (sound.stopped || sound.audio.ended || !sound.started) return 2;
    return sound.audio.paused ? 0 : 1;
});

EM_JS(void, deleteWebSound, (int id), {
    const sound = Module.voxelAudio.sounds.get(id);
    if (!sound) return;
    sound.audio.pause();
    Module.voxelAudio.sounds.delete(id);
});

EM_JS(void, setWebSoundPosition, (int id, float x, float y, float z), {
    Module.voxelAudio.sounds.get(id).position = [x, y, z];
});
EM_JS(void, setWebSoundProperty, (int id, int property, float value), {
    const sound = Module.voxelAudio.sounds.get(id);
    if (property === 0) sound.volume = value;
    if (property === 1) sound.attenuation = value;
    if (property === 2) sound.audio.loop = !!value;
    if (property === 3) sound.relative = !!value;
    if (property === 4) sound.minDistance = value;
});

Sound::Sound(): m_sound(0) {}
Sound::Sound(const std::string& path): m_sound(createWebSound(path.c_str())) {}
Sound::~Sound() { if (m_sound) deleteWebSound(m_sound); }
Sound::Status Sound::status() {
    return m_sound ? static_cast<Status>(webSoundStatus(m_sound)) : Status::Null;
}
void Sound::play() { if (m_sound) webSoundCommand(m_sound, 0); }
void Sound::stop() { if (m_sound) webSoundCommand(m_sound, 1); }
void Sound::pause() { if (m_sound) webSoundCommand(m_sound, 2); }
Sound& Sound::setPosition(const glm::vec3& position) {
    if (m_sound) {
        setWebSoundPosition(m_sound, position.x, position.y, position.z);
        updateWebSoundVolume(m_sound);
    }
    return *this;
}
Sound& Sound::setVolume(float volume) {
    m_volume = volume;
    if (m_sound) { setWebSoundProperty(m_sound, 0, m_muted ? 0 : volume); updateWebSoundVolume(m_sound); }
    return *this;
}
Sound& Sound::setMuted(bool muted) {
    m_muted = muted;
    return setVolume(m_volume);
}
Sound& Sound::setAttenuation(float attenuation) {
    if (m_sound) { setWebSoundProperty(m_sound, 1, attenuation); updateWebSoundVolume(m_sound); }
    return *this;
}
Sound& Sound::setLooping(bool loop) {
    if (m_sound) setWebSoundProperty(m_sound, 2, loop);
    return *this;
}
Sound& Sound::setRelativeToListener(bool relative) {
    if (m_sound) { setWebSoundProperty(m_sound, 3, relative); updateWebSoundVolume(m_sound); }
    return *this;
}
Sound& Sound::setMinDistance(float distance) {
    if (m_sound) { setWebSoundProperty(m_sound, 4, std::max(distance, 0.001f)); updateWebSoundVolume(m_sound); }
    return *this;
}
