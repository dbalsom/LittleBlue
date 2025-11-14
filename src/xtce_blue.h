#pragma once

#include <string>
#include <format>
#include <functional>

#define AUDIO_SAMPLE_RATE 44100
#define BLIP_SAMPLE_COUNT 20000

inline std::string decimal(int n, int w = 0) {
    return std::format("{:0{}}", n, w);
}

using PcSpeakerCallback = std::function<void(uint64_t tick, bool state, bool enabled)>;
