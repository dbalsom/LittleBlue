#pragma once

#include <cstdint>
#include <vector>
#include "../core/Cga.h"

class DisplayRenderer {
public:
    static constexpr int WIDTH = 912;
    static constexpr int HEIGHT = 262;
    static constexpr int BYTES_PER_PIXEL = 4; // RGBA

    DisplayRenderer();

    // Render the CGA front buffer into our internal RGBA framebuffer.
    // This will read WIDTH*HEIGHT bytes from cga->getFrontBuffer(), each 0-15 palette index.
    void render(CGA* cga);

    // Accessors for the framebuffer data
    const uint8_t* pixels() const { return pixelBuffer_.data(); }
    uint8_t* pixels() { return pixelBuffer_.data(); }
    int width() const { return WIDTH; }
    int height() const { return HEIGHT; }

private:
    std::vector<uint8_t> pixelBuffer_; // size WIDTH * HEIGHT * 4
};

