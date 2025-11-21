#include "DisplayRenderer.h"
#include <array>

// "IBM 5153" CGA palette (16 colors) in 8-bit per channel RGB
// See https://int10h.org/blog/2022/06/ibm-5153-color-true-cga-palette/
static constexpr std::array<std::array<uint8_t, 3>, 16> CGA_PALETTE = {
    std::array<uint8_t, 3>{0x00, 0x00, 0x00}, // 0 black
    std::array<uint8_t, 3>{0x00, 0x00, 0xC4}, // 1 blue
    std::array<uint8_t, 3>{0x00, 0xC4, 0x00}, // 2 green
    std::array<uint8_t, 3>{0x00, 0xC4, 0xC4}, // 3 cyan
    std::array<uint8_t, 3>{0xC4, 0x00, 0x00}, // 4 red
    std::array<uint8_t, 3>{0xC4, 0x00, 0xC4}, // 5 magenta
    std::array<uint8_t, 3>{0xC4, 0x7E, 0x00}, // 6 brown
    std::array<uint8_t, 3>{0xC4, 0xC4, 0xC4}, // 7 light gray
    std::array<uint8_t, 3>{0x4E, 0x4E, 0x4E}, // 8 dark gray
    std::array<uint8_t, 3>{0x4E, 0x4E, 0xDC}, // 9 bright blue
    std::array<uint8_t, 3>{0x4E, 0xDC, 0x4E}, // A bright green
    std::array<uint8_t, 3>{0x4E, 0xF3, 0xF3}, // B bright cyan
    std::array<uint8_t, 3>{0xDC, 0x4E, 0x4E}, // C bright red
    std::array<uint8_t, 3>{0xF3, 0x4E, 0xF3}, // D bright magenta
    std::array<uint8_t, 3>{0xF3, 0xF3, 0x4E}, // E yellow
    std::array<uint8_t, 3>{0xFF, 0xFF, 0xFF}, // F white
};

DisplayRenderer::DisplayRenderer() {
    pixelBuffer_.resize(WIDTH * HEIGHT * BYTES_PER_PIXEL);
    compositeLine_.resize(WIDTH);
}

void DisplayRenderer::render(CGA* cga) {
    if (!cga) {
        return;
    }
    uint8_t* front = cga->getFrontBuffer();
    const size_t front_size = cga->getFrontBufferSize();
    if (!front || front_size < static_cast<size_t>(WIDTH) * static_cast<size_t>(HEIGHT)) {
        return;
    }

    // The CGA front buffer is WIDTH*HEIGHT bytes where each byte is 0..15
    const size_t count = static_cast<size_t>(WIDTH) * static_cast<size_t>(HEIGHT);
    uint8_t* dst = pixelBuffer_.data();

    if (!composite_enabled_) {
        for (size_t i = 0; i < count; ++i) {
            const uint8_t idx = front[i] & 0x0F;
            const auto& c = CGA_PALETTE[idx];

            // Unpack 0xRRGGBB into RGBA bytes (A=0xFF)
            dst[i * 4 + 0] = c[0]; // R
            dst[i * 4 + 1] = c[1]; // G
            dst[i * 4 + 2] = c[2]; // B
            dst[i * 4 + 3] = 0xFF; // A
        }
    }
    else {
        // ReSharper disable once CppDFAUnreachableCode

        // Update composite color tables based on current mode byte once per frame
        const uint8_t mode = cga->getModeByte();
        const uint8_t border = cga->getOverscanColor();
        compositeRenderer_.update_cga16_color(mode);
        const uint32_t blocks = WIDTH / 4; // composite routine expects blocks of 4 pixels

        for (int y = 0; y < HEIGHT; ++y) {
            uint8_t* src_line = front + (y * WIDTH);
            uint32_t* out_line_temp = compositeLine_.data();
            compositeRenderer_.Composite_Process(mode, border, blocks, src_line, out_line_temp);

            // Unpack 0xRRGGBB into RGBA bytes (A=0xFF)
            uint8_t* lineDst = dst + (y * WIDTH * 4);
            for (int x = 0; x < WIDTH; ++x) {
                const uint32_t pix = out_line_temp[x];
                lineDst[x * 4 + 0] = static_cast<uint8_t>((pix >> 16) & 0xFF); // R
                lineDst[x * 4 + 1] = static_cast<uint8_t>((pix >> 8) & 0xFF); // G
                lineDst[x * 4 + 2] = static_cast<uint8_t>(pix & 0xFF); // B
                lineDst[x * 4 + 3] = 0xFF; // A
            }
        }
    }
}
