#include "DisplayRenderer.h"
#include <array>

// Standard CGA palette (16 colors) in 8-bit per channel RGB, we'll set A=255
static constexpr std::array<std::array<uint8_t,3>, 16> CGA_PALETTE = {
    std::array<uint8_t,3>{0x00, 0x00, 0x00}, // 0 black
    std::array<uint8_t,3>{0x00, 0x00, 0xC4}, // 1 blue
    std::array<uint8_t,3>{0x00, 0xC4, 0x00}, // 2 green
    std::array<uint8_t,3>{0x00, 0xC4, 0xC4}, // 3 cyan
    std::array<uint8_t,3>{0xC4, 0x00, 0x00}, // 4 red
    std::array<uint8_t,3>{0xC4, 0x00, 0xC4}, // 5 magenta
    std::array<uint8_t,3>{0xC4, 0x7E, 0x00}, // 6 brown
    std::array<uint8_t,3>{0xC4, 0xC4, 0xC4}, // 7 light gray
    std::array<uint8_t,3>{0x4E, 0x4E, 0x4E}, // 8 dark gray
    std::array<uint8_t,3>{0x4E, 0x4E, 0xDC}, // 9 bright blue
    std::array<uint8_t,3>{0x4E, 0xDC, 0x4E}, // A bright green
    std::array<uint8_t,3>{0x4E, 0xF3, 0xF3}, // B bright cyan
    std::array<uint8_t,3>{0xDC, 0x4E, 0x4E}, // C bright red
    std::array<uint8_t,3>{0xF3, 0x4E, 0xF3}, // D bright magenta
    std::array<uint8_t,3>{0xF3, 0xF3, 0x4E}, // E yellow
    std::array<uint8_t,3>{0xFF, 0xFF, 0xFF}, // F white
};

DisplayRenderer::DisplayRenderer()
{
    pixelBuffer_.resize(WIDTH * HEIGHT * BYTES_PER_PIXEL);
}

void DisplayRenderer::render(CGA* cga)
{
    if (!cga) return;
    uint8_t* front = cga->getFrontBuffer();
    const size_t frontSize = cga->getFrontBufferSize();
    if (!front || frontSize < static_cast<size_t>(WIDTH) * static_cast<size_t>(HEIGHT)) return;

    // The CGA front buffer is WIDTH*HEIGHT bytes where each byte is 0..15
    const size_t count = static_cast<size_t>(WIDTH) * static_cast<size_t>(HEIGHT);
    // Fill pixelBuffer_ using palette
    uint8_t* dst = pixelBuffer_.data();

    for (size_t i = 0; i < count; ++i) {
        uint8_t idx = front[i] & 0x0F;
        const auto &c = CGA_PALETTE[idx];
        // Write as B, G, R, A to match SDL texture memory layout used by the renderer
        dst[i*4 + 0] = c[0]; // R
        dst[i*4 + 1] = c[1]; // G
        dst[i*4 + 2] = c[2]; // B
        dst[i*4 + 3] = 0xFF; // A
    }
}
