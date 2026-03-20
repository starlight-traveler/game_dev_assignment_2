#include "SoundSystem.h"

#include <algorithm>
#include <cstddef>

namespace {
/**
 * @brief Clamps mixed signed byte sample to valid range
 * @param value Mixed integer sample
 * @return Clamped signed byte sample
 */
Sint8 clamp_s8(int value) {
    // audio samples are stored as signed 8-bit values, so clamp into that legal range
    return static_cast<Sint8>(std::max(-128, std::min(127, value)));
}

/**
 * @brief Free callback bridge for SDL audio device
 * @param userdata SoundSystem pointer
 * @param stream Device output stream
 * @param len Byte count available in stream
 */
void sound_callback(void* userdata, Uint8* stream, int len) {
    if (!userdata) {
        // without the owning SoundSystem object there is nothing valid to mix
        return;
    }
    // SDL hands back the userdata pointer that was stored during device creation
    auto* sound_system = static_cast<SoundSystem*>(userdata);
    // forward the real work into the class method so the callback stays tiny
    sound_system->mixToStream(stream, len);
}
}  // namespace

SoundSystem::SoundSystem()
    : sounds_(),
      device_(0),
      obtained_spec_{},
      playback_(),
      mix_(nullptr),
      mix_capacity_bytes_(0),
      runtime_sound_(),
      ready_(false) {
    // describe the audio format we would like SDL to open
    SDL_AudioSpec desired{};
    // 48 kHz sample rate is common and good enough for this assignment
    desired.freq = 48000;
    // signed 8-bit samples keep mixing math simple in the callback
    desired.format = AUDIO_S8;
    // stereo output lets the mixer write left and right channels
    desired.channels = 2;
    // callback chunk size, larger means more latency but fewer callbacks
    desired.samples = 2048;
    // free function callback used by SDL when it needs more audio
    desired.callback = sound_callback;
    // pass this object through SDL so the callback can call back into it
    desired.userdata = this;

    // ask SDL to open the default playback device
    // obtained_spec_ will contain what SDL actually gave us
    device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained_spec_, 0);
    if (device_ == 0) {
        // if device creation fails, leave ready_ false and let the app continue silently
        return;
    }

    // SDL may tell us the exact callback buffer size in bytes
    mix_capacity_bytes_ = static_cast<int>(obtained_spec_.size);
    if (mix_capacity_bytes_ <= 0) {
        // if not, estimate bytes as sample frames times channel count for AUDIO_S8
        mix_capacity_bytes_ = static_cast<int>(obtained_spec_.samples) *
                              static_cast<int>(obtained_spec_.channels);
    }
    if (mix_capacity_bytes_ <= 0) {
        // a non-positive mix size means we cannot safely allocate the scratch buffer
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        return;
    }

    // allocate a scratch buffer that the callback can mix into before copying to SDL's stream
    mix_ = static_cast<Uint8*>(SDL_malloc(static_cast<size_t>(mix_capacity_bytes_)));
    if (!mix_) {
        // if scratch allocation fails, fully tear down the device and stay unavailable
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        mix_capacity_bytes_ = 0;
        return;
    }

    // unpause the device so SDL starts invoking the callback
    SDL_PauseAudioDevice(device_, 0);
    // audio is now fully usable
    ready_ = true;
}

SoundSystem::~SoundSystem() {
    if (device_ != 0) {
        // stop future callbacks before mutating playback state
        SDL_PauseAudioDevice(device_, 1);
        // lock the device so the callback cannot access playback_ while we clear it
        SDL_LockAudioDevice(device_);
        // drop all active play cursors
        playback_.clear();
        // release the device lock once shared state is safe
        SDL_UnlockAudioDevice(device_);
    }

    // release the optional one-shot runtime sound buffer
    runtime_sound_.clear();
    // release all preloaded sounds
    sounds_.clear();

    if (mix_) {
        // free the scratch mix buffer used by the callback
        SDL_free(mix_);
        mix_ = nullptr;
        mix_capacity_bytes_ = 0;
    }

    if (device_ != 0) {
        // finally close the SDL device itself
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }

    // advertise that the audio system is no longer operational
    ready_ = false;
}

bool SoundSystem::loadSound(const std::string& path) {
    if (!ready_) {
        // refuse to load if the audio device never initialized
        return false;
    }

    // load into a temporary Sound so failure does not corrupt the stored library
    Sound sound{};
    if (!sound.loadWavMonoS8(path, obtained_spec_.freq)) {
        // loading or conversion failed
        return false;
    }
    // move the successfully loaded sound into the persistent library
    sounds_.push_back(std::move(sound));
    return true;
}

bool SoundSystem::playSound(int index) {
    if (!ready_ || index < 0 || static_cast<std::size_t>(index) >= sounds_.size()) {
        // invalid device state or invalid library index means no playback request
        return false;
    }
    // queue the selected preloaded sound for mixing
    return queueSound(sounds_[static_cast<std::size_t>(index)]);
}

bool SoundSystem::playSound(const std::string& path) {
    if (!ready_) {
        // path-based playback also depends on a valid device
        return false;
    }

    // remove active states that reference the old runtime-loaded sound buffer
    if (runtime_sound_.data() != nullptr && runtime_sound_.length() > 0) {
        // remember the address range of the current runtime sound buffer
        const Uint8* start = runtime_sound_.data();
        const Uint8* end = start + runtime_sound_.length();
        // lock the audio device before mutating playback_ because the callback reads it too
        SDL_LockAudioDevice(device_);
        playback_.erase(
            std::remove_if(playback_.begin(), playback_.end(),
                           [start, end](const SoundState& state) {
                               // remove any playback cursor still pointing into the old runtime buffer
                               return state.cursor >= start && state.cursor < end;
                           }),
            playback_.end());
        // unlock so the callback can resume
        SDL_UnlockAudioDevice(device_);
    }

    // load the requested file into the reusable runtime sound object
    if (!runtime_sound_.loadWavMonoS8(path, obtained_spec_.freq)) {
        return false;
    }
    // queue the newly loaded runtime sound just like a preloaded sound
    return queueSound(runtime_sound_);
}

bool SoundSystem::isReady() const {
    // tiny getter used by callers before attempting loads or playback
    return ready_;
}

void SoundSystem::mixToStream(Uint8* stream, int len) {
    if (!stream || len <= 0) {
        // SDL should not call this with an invalid buffer, but guard anyway
        return;
    }

    if (!mix_ || len > mix_capacity_bytes_) {
        // grow the scratch buffer when SDL asks for a larger callback chunk than before
        Uint8* resized = static_cast<Uint8*>(SDL_realloc(mix_, static_cast<size_t>(len)));
        if (!resized) {
            // on allocation failure output silence rather than garbage
            SDL_memset(stream, 0, static_cast<size_t>(len));
            return;
        }
        // adopt the newly resized buffer and remember the new capacity
        mix_ = resized;
        mix_capacity_bytes_ = len;
    }

    // clear the scratch buffer to silence before adding any active sounds into it
    SDL_memset(mix_, 0, static_cast<size_t>(len));
    // reinterpret the byte buffer as signed 8-bit audio samples
    auto* mix_samples = reinterpret_cast<Sint8*>(mix_);
    // AUDIO_S8 stereo uses one byte per channel, so each frame is two bytes
    const int output_frames = len / 2;

    // walk every currently active playback cursor
    for (std::size_t i = 0; i < playback_.size();) {
        // take the current sound state by reference because we will update its cursor
        SoundState& state = playback_[i];
        if (!state.cursor || state.remaining == 0) {
            // drop invalid or exhausted sounds immediately
            playback_.erase(playback_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }

        // only mix as many mono samples as both the output buffer and this sound allow
        const Uint32 frames_to_mix = std::min(static_cast<Uint32>(output_frames), state.remaining);
        // the sound buffer is stored as signed 8-bit mono samples
        const auto* mono_samples = reinterpret_cast<const Sint8*>(state.cursor);
        for (Uint32 frame = 0; frame < frames_to_mix; ++frame) {
            // stereo output uses two sample slots per frame
            const int out_index = static_cast<int>(frame * 2);
            // read one mono sample from the source sound
            const int mono_value = static_cast<int>(mono_samples[frame]);
            // add that mono sample equally into the left channel
            const int left = static_cast<int>(mix_samples[out_index]) + mono_value;
            // add that same mono sample equally into the right channel
            const int right = static_cast<int>(mix_samples[out_index + 1]) + mono_value;
            // clamp to signed 8-bit range before writing back
            mix_samples[out_index] = clamp_s8(left);
            mix_samples[out_index + 1] = clamp_s8(right);
        }

        // advance the sound cursor by the number of mono bytes we just consumed
        state.cursor += frames_to_mix;
        // reduce the number of samples remaining for this sound
        state.remaining -= frames_to_mix;
        if (state.remaining == 0) {
            // remove the sound once all of its samples have been consumed
            playback_.erase(playback_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            // otherwise keep it for the next callback chunk
            ++i;
        }
    }

    // copy the completed mixed buffer into SDL's output stream
    SDL_memcpy(stream, mix_, static_cast<size_t>(len));
}

bool SoundSystem::queueSound(const Sound& sound) {
    if (!ready_ || sound.data() == nullptr || sound.length() == 0) {
        // cannot queue invalid or empty sounds
        return false;
    }

    // lock the audio device before modifying playback_ because the callback reads it concurrently
    SDL_LockAudioDevice(device_);
    // create a playback cursor starting at the first byte of the sound buffer
    SoundState state{};
    state.cursor = sound.data();
    // total remaining mono sample bytes for this sound
    state.remaining = sound.length();
    // append to the active playback list so future callbacks will mix it
    playback_.push_back(state);
    // release the lock so the callback can see the new sound
    SDL_UnlockAudioDevice(device_);
    return true;
}
