#pragma once

#include <string>
#include <format>
#include <functional>

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_SAMPLE_BUFFER "512" // String for SDL_SetHint
#define BLIP_SAMPLE_COUNT 20000
#define AUDIO_MAX_LATENCY_MS 40

#define EMU_FRAME_SLICES 16


inline std::string decimal(int n, int w = 0) {
    return std::format("{:0{}}", n, w);
}

using PcSpeakerCallback = std::function<void(uint64_t tick, bool state, bool enabled)>;
