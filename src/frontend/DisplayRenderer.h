#pragma once

#include <cstdint>
#include <vector>
#include "../core/Cga.h"
#include "Composite.h"

class DisplayRenderer
{
public:
    static constexpr int WIDTH = 912; // front buffer width
    static constexpr int HEIGHT = 262; // front buffer height
    static constexpr int BYTES_PER_PIXEL = 4; // RGBA

    DisplayRenderer();

    // Render the CGA front buffer into our internal RGBA framebuffer.
    // This will read WIDTH*HEIGHT bytes from cga->getFrontBuffer(), each 0-15 palette index.
    void render(CGA* cga);

    void setComposite(bool v) { composite_enabled_ = v; }

    // Accessors for the framebuffer data
    const uint8_t* pixels() const { return pixelBuffer_.data(); }
    uint8_t* pixels() { return pixelBuffer_.data(); }
    int width() const { return WIDTH; }
    int height() const { return HEIGHT; }

private:
    std::vector<uint8_t> pixelBuffer_; // size WIDTH * HEIGHT * 4
    bool composite_enabled_ = false; // keep existing API flag
    CompositeRenderer compositeRenderer_{};
    std::vector<uint32_t> compositeLine_; // temp line buffer WIDTH entries
};
