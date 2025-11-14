#pragma once

#include "DebuggerWindow.h"
#include <SDL3/SDL.h>

class DisplayDebugWindow : public DebuggerWindow
{
public:
    explicit DisplayDebugWindow(SDL_Texture** texPtr) :
        _texPtr(texPtr) {
    }

    ~DisplayDebugWindow() override = default;

    void show(bool* open) override;
    [[nodiscard]] const char* name() const override { return "Display Debug"; }

private:
    SDL_Texture** _texPtr{nullptr};
};

