/**
 * @file SoundSystem.h
 * @brief Low-level SDL sound device system with callback mixing
 */
#ifndef SOUND_SYSTEM_H
#define SOUND_SYSTEM_H

#include <string>
#include <vector>

#include <SDL.h>

#include "Sound.h"

/**
 * @brief Playback state for an active sound stream
 */
struct SoundState {
    // current byte position inside the converted sound buffer
    const Uint8* cursor;
    // number of mono sample bytes still left to mix from this sound
    Uint32 remaining;
};

/**
 * @brief Sound device manager with preload library and active playback queue
 */
class SoundSystem {
public:
    /**
     * @brief Opens audio device and starts callback stream
     */
    SoundSystem();

    /**
     * @brief Stops callback stream and cleans all audio resources
     */
    ~SoundSystem();

    SoundSystem(const SoundSystem&) = delete;
    SoundSystem& operator=(const SoundSystem&) = delete;

    /**
     * @brief Loads a WAV into preloaded sound library
     * @param path WAV filepath
     * @return True on success
     */
    bool loadSound(const std::string& path);

    /**
     * @brief Queues a sound by preloaded index
     * @param index Sound library index
     * @return True when queued
     */
    bool playSound(int index);

    /**
     * @brief Loads and queues one runtime WAV track
     * @param path WAV filepath
     * @return True when queued
     */
    bool playSound(const std::string& path);

    /**
     * @brief Reports whether audio device is open
     * @return True when device is ready
     */
    bool isReady() const;

    /**
     * @brief Audio callback implementation used by free callback bridge
     * @param stream Device output stream buffer
     * @param len Bytes available in output buffer
     */
    void mixToStream(Uint8* stream, int len);

private:
    /**
     * @brief Queues a sound object into playback
     * @param sound Sound object source
     * @return True when queued
     */
    bool queueSound(const Sound& sound);

    // preloaded sounds kept alive for indexed playback
    std::vector<Sound> sounds_;
    // SDL device handle returned by SDL_OpenAudioDevice
    SDL_AudioDeviceID device_;
    // actual audio format accepted by the opened device
    SDL_AudioSpec obtained_spec_;
    // active playback cursors mixed by the callback each audio tick
    std::vector<SoundState> playback_;
    // scratch mix buffer used to assemble one callback chunk before copying to SDL
    Uint8* mix_;
    // byte capacity of the scratch mix buffer
    int mix_capacity_bytes_;
    // one temporary sound object for path-based playSound calls
    Sound runtime_sound_;
    // quick flag so callers can tell whether the audio device opened successfully
    bool ready_;
};

#endif
