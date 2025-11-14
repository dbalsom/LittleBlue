#include <cmath>
#include <string_view>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <iterator>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_init.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_sdl3.h>
#include <imgui/backends/imgui_impl_sdlrenderer3.h>

#include "xtce_blue.h"

#include "gui/imgui_memory_editor.h"
#include "gui/DebuggerWindow.h"
#include "gui/DebuggerManager.h"
#include "gui/DisassemblyWindow.h"
#include "gui/MemoryViewerWindow.h"
#include "gui/CycleLogWindow.h"
#include "gui/StackViewerWindow.h"
#include "gui/VideoCardStatusWindow.h"
#include "gui/PicStatusWindow.h"
#include "gui/DmacStatusWindow.h"
#include "gui/DisplayDebugWindow.h"
#include "gui/CpuStatusWindow.h"

#include "core/Machine.h"

#include "frontend/DisplayRenderer.h"
#include "frontend/keyboard.h"


// Forward declare the callback we'll register with SDL_ShowOpenFileDialog
static void FileDialogCallback(void* userdata, const char* const* filelist, int filter_index);

constexpr uint32_t windowStartWidth = 1280;
constexpr uint32_t windowStartHeight = 1024;

bool init_audio(SDL_AudioDeviceID* outAudioDevice, MIX_Mixer** outMixer);

struct AppContext
{
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_AudioDeviceID audioDevice = 0;
    MIX_Mixer* mixer = nullptr;
    SDL_AppResult app_quit = SDL_APP_CONTINUE;
    Machine* machine = nullptr;
    DebuggerManager dbgManager;
    bool running{true};

    // FPS tracking
    Uint64 last_counter{0};
    float fps_timer{0.0f};
    int frame_count{0};
    float fps{0.0f};

    // Fixed-timestep CPU timing (14.31818 MHz crystal)
    double crystal_hz{14318180.0}; // 14.31818 MHz
    double tick_accumulator{0.0}; // accumulated CPU ticks (fractional)
    int ticks_per_frame{0}; // precomputed ticks per 1/60s frame
    int max_frame_burst{5}; // max number of frames worth of ticks to run in a single iterate to avoid spiral of death

    // Display renderer and texture
    DisplayRenderer displayRenderer;
    SDL_Texture* displayTexture{nullptr};

    // Memory editor UI
    MemoryEditor memEditor;
    bool showMemoryViewer{false};
    bool showVramViewer{false};
    bool showStackViewer{false};
    bool showCpuViewer{false};
    bool showVideoCardViewer{false};
    bool showPicViewer{false};
    bool showDmaViewer{false};
    bool showDisplayDebug{false};
    bool cpuRunning{true};
    bool showDisassembly{false};

    // CPU timing display
    uint64_t lastCycleCount{0};
    double smoothedMhz{0.0};

    // Cycle log UI
    bool showCycleLog{false};
    bool cycleLogAutoScroll{true};
    int cycleLogCapacityUI{10000};
    size_t lastSeenCycleLogSize{0};

    // File dialog pending result handling
    std::mutex fileDialogMutex;
    SDL_DialogFileFilter* pendingFilters{nullptr}; // owned until dialog callback
    std::string pendingDiskPath;
    bool pendingDiskLoadFlag{false};
};

SDL_AppResult SDL_Fail() {
    SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "Error %s", SDL_GetError());
    return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    // init the library, here we make a window so we only need the Video capabilities.
    if (not SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        return SDL_Fail();
    }

    // init TTF
    if (not TTF_Init()) {
        return SDL_Fail();
    }

    // Create a window
    SDL_Window* window =
        SDL_CreateWindow(
            "XTCE-Blue",
            windowStartWidth,
            windowStartHeight,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    if (not window) {
        return SDL_Fail();
    }

    // create a renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (not renderer) {
        return SDL_Fail();
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    auto basePathPtr = SDL_GetBasePath();
    if (not basePathPtr) {
        return SDL_Fail();
    }
    const std::filesystem::path basePath = basePathPtr;

    // const auto fontPath = basePath / "Inter-VariableFont.ttf";
    // TTF_Font* font = TTF_OpenFont(fontPath.string().c_str(), 36);
    // if (not font) {
    //     return SDL_Fail();
    // }
    //
    // // render the font to a surface
    // const std::string_view text = "Hello SDL!";
    // SDL_Surface* surfaceMessage = TTF_RenderText_Solid(font, text.data(), text.length(), { 255,255,255 });
    //
    // // make a texture from the surface
    // SDL_Texture* messageTex = SDL_CreateTextureFromSurface(renderer, surfaceMessage);
    //
    // // we no longer need the font or the surface, so we can destroy those now.
    // TTF_CloseFont(font);
    // SDL_DestroySurface(surfaceMessage);

    // get the on-screen dimensions of the text. this is necessary for rendering it
    // auto messageTexProps = SDL_GetTextureProperties(messageTex);
    // SDL_FRect text_rect{
    //         .x = 0,
    //         .y = 0,
    //         .w = float(SDL_GetNumberProperty(messageTexProps, SDL_PROP_TEXTURE_WIDTH_NUMBER, 0)),
    //         .h = float(SDL_GetNumberProperty(messageTexProps, SDL_PROP_TEXTURE_HEIGHT_NUMBER, 0))
    // };

    // Initialize audio
    SDL_AudioDeviceID audioDevice;
    MIX_Mixer* mixer;
    init_audio(&audioDevice, &mixer);

    // // load the music
    // auto musicPath = basePath / "the_entertainer.ogg";
    // auto music = Mix_LoadMUS(musicPath.string().c_str());
    // if (not music) {
    //     return SDL_Fail();
    // }
    //
    // // play the music (does not loop)
    // Mix_PlayMusic(music, 0);

    // Create the emulator
    auto machine = new Machine();

    // We'll allocate AppContext below and register windows into its dbgManager member.

    // print some information about the window
    SDL_ShowWindow(window);
    {
        int width, height, bbwidth, bbheight;
        SDL_GetWindowSize(window, &width, &height);
        SDL_GetWindowSizeInPixels(window, &bbwidth, &bbheight);
        SDL_Log("Window size: %ix%i", width, height);
        SDL_Log("Backbuffer size: %ix%i", bbwidth, bbheight);
        if (width != bbwidth) {
            SDL_Log("This is a highdpi environment.");
        }
    }

    // set up the application data (allocate on heap so we can register windows into the dbgManager member)
    auto* ctx = new AppContext();
    ctx->window = window;
    ctx->renderer = renderer;
    ctx->audioDevice = audioDevice;
    ctx->mixer = mixer;
    ctx->machine = machine;
    ctx->last_counter = SDL_GetPerformanceCounter();

    // Create the display texture used to show the CGA framebuffer. Use RGBA and streaming access for frequent updates.
    ctx->displayTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                            DisplayRenderer::WIDTH, DisplayRenderer::HEIGHT);
    if (!ctx->displayTexture) {
        SDL_Log("Failed to create display texture: %s", SDL_GetError());
        // Non-fatal; continue without texture
    }

    // Register our various debug windows with the AppContext's dbgManager
    ctx->dbgManager.addWindow("Disassembly", std::make_unique<DisassemblyWindow>(machine), &ctx->showDisassembly);
    ctx->dbgManager.addWindow("Cpu Status", std::make_unique<CpuStatusWindow>(machine), &ctx->showCpuViewer);
    ctx->dbgManager.addWindow("Memory Viewer", std::make_unique<MemoryViewerWindow>(machine), &ctx->showMemoryViewer);
    ctx->dbgManager.addWindow("VRAM Viewer", std::make_unique<MemoryViewerWindow>(machine, true), &ctx->showVramViewer);
    ctx->dbgManager.addWindow("Stack Viewer", std::make_unique<StackViewerWindow>(machine), &ctx->showStackViewer);
    ctx->dbgManager.addWindow("Cycle Log", std::make_unique<CycleLogWindow>(machine), &ctx->showCycleLog);
    ctx->dbgManager.addWindow("Video Card Status", std::make_unique<VideoCardStatusWindow>(machine),
                              &ctx->showVideoCardViewer);
    ctx->dbgManager.addWindow("PIC Status", std::make_unique<PicStatusWindow>(machine), &ctx->showPicViewer);
    ctx->dbgManager.addWindow("DMA Status", std::make_unique<DmacStatusWindow>(machine), &ctx->showDmaViewer);
    // Display debug window uses the app's displayTexture pointer
    ctx->dbgManager.addWindow("Display Debug", std::make_unique<DisplayDebugWindow>(&ctx->displayTexture),
                              &ctx->showDisplayDebug);
    *appstate = ctx;

    // Initialize cycle count baseline for MHz measurement
    static_cast<AppContext*>(*appstate)->lastCycleCount = static_cast<AppContext*>(*appstate)->machine->cycleCount();

    // compute ticks_per_frame based on 60 FPS
    auto* appc = static_cast<AppContext*>(*appstate);
    const double fps = 60.0;
    appc->ticks_per_frame = static_cast<int>(std::llround(appc->crystal_hz / fps));
    // Initialize cycle log UI capacity from machine state
    static_cast<AppContext*>(*appstate)->cycleLogCapacityUI = static_cast<int>(static_cast<AppContext*>(*appstate)->
                                                                               machine->getCycleLogCapacity());

    SDL_SetRenderVSync(renderer, 1); // enable vysnc
    SDL_Log("Application started successfully!");

    // start the machine
    appc->machine->run();


    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    auto* app = static_cast<AppContext*>(appstate);
    uint8_t sc{};
    ImGui_ImplSDL3_ProcessEvent(event);
    const ImGuiIO& io = ImGui::GetIO();

    switch (event->type) {
        // case SDL_EVENT_MOUSE_MOTION:
        //     SDL_Log("MOUSE MOTION: x=%f y=%f rel=(%f,%f)",
        //             event->motion.x, event->motion.y,
        //             event->motion.xrel, event->motion.yrel);
        //     break;
        //
        // case SDL_EVENT_MOUSE_BUTTON_DOWN:
        //     SDL_Log("MOUSE BUTTON DOWN: button=%d at (%f,%f)",
        //             event->button.button, event->button.x, event->button.y);
        //     break;
        //
        // case SDL_EVENT_MOUSE_BUTTON_UP:
        //     SDL_Log("MOUSE BUTTON UP: button=%d at (%f,%f)",
        //             event->button.button, event->button.x, event->button.y);
        //     break;
        //
        // case SDL_EVENT_MOUSE_WHEEL:
        //     SDL_Log("MOUSE WHEEL: x=%f y=%f", event->wheel.x, event->wheel.y);
        //     break;

        case SDL_EVENT_KEY_DOWN:
            if (!io.WantCaptureKeyboard) {
                sc = translate_SDL_key(event->key.key, true);
                app->machine->sendScanCode(sc);
            }
            break;

        case SDL_EVENT_KEY_UP:
            if (!io.WantCaptureKeyboard) {
                sc = translate_SDL_key(event->key.key, false);
                app->machine->sendScanCode(sc);
            }
            break;

        case SDL_EVENT_QUIT:
            SDL_Log("QUIT event");
            app->running = false;
            break;

        default:
            break;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* app = static_cast<AppContext*>(appstate);
    if (!app || !app->machine) {
        return SDL_APP_FAILURE;
    }

    const Uint64 now = SDL_GetPerformanceCounter();
    const double delta = static_cast<double>(now - app->last_counter) / static_cast<double>(
        SDL_GetPerformanceFrequency());
    app->last_counter = now;

    // accumulate time and frames for smoother FPS
    app->fps_timer += static_cast<float>(delta);
    app->frame_count++;

    // Update CPU cycle / MHz measurement
    uint64_t cycles = app->machine->cycleCount();
    uint64_t deltaCycles = cycles >= app->lastCycleCount ? cycles - app->lastCycleCount : 0;
    if (delta > 0.0) {
        double mhz = (static_cast<double>(deltaCycles) / delta) / 1e6; // MHz
        // Exponential smoothing: 90% previous, 10% new (adjust as needed)
        const double alpha = 0.10;
        app->smoothedMhz = (1.0 - alpha) * app->smoothedMhz + alpha * mhz;
    }
    app->lastCycleCount = cycles;

    if (app->fps_timer >= 0.5f) {
        // update twice per second
        app->fps = static_cast<float>(app->frame_count) / app->fps_timer;
        app->frame_count = 0;
        app->fps_timer = 0.0f;

        char title[128];
        snprintf(title, sizeof(title), "LittleBlue — %.1f FPS", app->fps);
        SDL_SetWindowTitle(app->window, title);
    }

    // Convert elapsed wall-clock time to CPU ticks (crystal cycles) and run the machine
    // Use an accumulator of fractional ticks to preserve long-term accuracy.
    // Cap the amount of work per frame to avoid spiral-of-death if the app stalls.
    double ticks_this_frame = delta * app->crystal_hz;
    app->tick_accumulator += ticks_this_frame;

    // Compute integer ticks to execute this iteration
    if (long ticks_to_run = static_cast<long>(std::floor(app->tick_accumulator)); ticks_to_run > 0) {
        // Cap ticks_to_run to a reasonable burst (e.g., max_frame_burst * ticks_per_frame)
        if (const long max_ticks = static_cast<long>(app->max_frame_burst) * static_cast<long>(
            std::max(1, app->ticks_per_frame)); ticks_to_run > max_ticks) {
            ticks_to_run = max_ticks;
        }

        // Execute ticks on the machine
        if (app->machine->isRunning()) {
            app->machine->run_for(static_cast<uint64_t>(ticks_to_run));
            // Remove executed ticks from accumulator
            app->tick_accumulator -= static_cast<double>(ticks_to_run);
        }
        else {
            // If CPU is paused, prevent tick_accumulator from growing without bound by capping it
            double max_accum = static_cast<long>(app->max_frame_burst) * std::max(1, app->ticks_per_frame);
            if (app->tick_accumulator > max_accum)
                app->tick_accumulator = max_accum;
        }
    }

    // If nothing to run, we still yield to UI and rendering below
    if (!app->running) {
        return SDL_APP_SUCCESS;
    }

    // Ensure no stale scaling state
    SDL_SetRenderViewport(app->renderer, nullptr);
    SDL_SetRenderClipRect(app->renderer, nullptr);

    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplSDLRenderer3_NewFrame();

    static bool show_about = false; // persistent flag
    static bool show_demo = false; // persistent flag
    static bool show_io_debug = false; // persistent flag

    ImGui::NewFrame();

    // Debug: show the raw CGA texture in an ImGui window to verify texture contents.
    // moved into dbgManager as a managed window

    // --- Main menu bar ---
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Emulator")) {
            if (ImGui::MenuItem("About...")) {
                show_about = true; // open modal dialog
            }
            ImGui::Separator();

            if (ImGui::MenuItem("Quit", "Alt+F4")) {
                app->running = false; // signals to stop in next iteration
            }
            ImGui::EndMenu();
        }


        if (ImGui::BeginMenu("Machine")) {
            if (ImGui::MenuItem("Reboot")) {
                app->machine->resetMachine();
            }
            ImGui::EndMenu();
        }

        // Media menu for loading disk images
        if (ImGui::BeginMenu("Media")) {
            if (ImGui::MenuItem("Load floppy image")) {
                // Allocate filters on the heap; they must remain valid until the callback runs.
                auto filters = new SDL_DialogFileFilter[1];
                filters[0].name = "IMG files";
                filters[0].pattern = "img;ima;dsk;bin";

                // Store pointer so callback can free it
                {
                    std::lock_guard<std::mutex> lk(app->fileDialogMutex);
                    app->pendingFilters = filters;
                }

                // Launch asynchronous native open-file dialog. allow_many=false
                SDL_ShowOpenFileDialog(FileDialogCallback, app, app->window, filters, 1, nullptr, false);
            }
            ImGui::EndMenu();
        }

        // Debug menu contains development and inspection tools
        if (ImGui::BeginMenu("Debug")) {
            // Add debugger windows (managed by DebuggerManager) - this will include
            // Memory Viewer, VRAM Viewer, Cycle Log, etc., because they've been registered.
            app->dbgManager.drawMenuItems();
            ImGui::EndMenu();
        }

        // Example: add more top-level menus later
        // if (ImGui::BeginMenu("Edit")) { ... ImGui::EndMenu(); }

        ImGui::EndMainMenuBar();
    }

    // --- About dialog ---
    if (show_about) {
        ImGui::OpenPopup("About LittleBlue");
    }

    if (show_demo) {
        ImGui::ShowDemoWindow();
    }

    if (ImGui::BeginPopupModal("About LittleBlue", &show_about,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("LittleBlue\n");
        ImGui::Separator();
        ImGui::Text("Version: 0.1.0");
        ImGui::Text("Authors:");
        ImGui::BulletText("Andrew Jenner (reenigne) - original XTCE CPU core");
        ImGui::BulletText("Daniel Balsom (gloriouscow) - SDL3 frontend, CGA implementation");

        ImGui::NewLine();

        ImGui::NewLine();
        ImGui::TextWrapped("LittleBlue is a cycle-accurate IBM XT emulator written in C++ using SDL3 and ImGui. "
            "LittleBlue's 8088 emulation is powered by the XTCE 8088 CPU core - "
            "which emulates the 8088 at the microcode level.");

        ImGui::NewLine();

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            show_about = false;
        }

        ImGui::EndPopup();
    }

    if (show_io_debug) {
        const ImVec2 p = ImGui::GetMousePos();
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::Begin("Input debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("ImGui::GetIO: (%.1f, %.1f)", io.MousePos.x, io.MousePos.y);
        ImGui::Text("ImGui::GetMousePos(): (%.1f, %.1f)", p.x, p.y);
        ImGui::Text("WantCaptureMouse: %s", io.WantCaptureMouse ? "true" : "false");
        ImGui::End();
    }

    // Memory viewers are registered into DebuggerManager and will be drawn via dbgManager.showAll().
    // (No manual DrawWindow call here to avoid duplicate windows/menu items.)

    // Show any registered debug windows
    app->dbgManager.showAll();

    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);

    ImGui::Render();

    // Update and render display texture (CGA) before ImGui is drawn so UI overlays appear on top.
    if (app->displayTexture && app->machine) {
        if (auto* bus = app->machine->getBus()) {
            if (auto* cga = bus->cga()) {
                app->displayRenderer.render(cga);

                // Integer rect for texture lock/update
                SDL_Rect src_rect_i{0, 0, static_cast<int>(DisplayRenderer::WIDTH),
                                    static_cast<int>(DisplayRenderer::HEIGHT)};
                // Floating-point rect for rendering APIs (SDL_RenderTexture expects SDL_FRect)
                SDL_FRect src_rect_f{0.0f, 0.0f, static_cast<float>(DisplayRenderer::WIDTH),
                                     static_cast<float>(DisplayRenderer::HEIGHT)};
                // Try locking the streaming texture and memcpy rows to the texture memory for reliable updates.
                void* tex_pixels = nullptr;
                int tex_pitch = 0;
                if (SDL_LockTexture(app->displayTexture, &src_rect_i, &tex_pixels, &tex_pitch) && (tex_pixels !=
                    nullptr)) {
                    const uint8_t* src = app->displayRenderer.pixels();
                    constexpr int row_bytes = DisplayRenderer::WIDTH * DisplayRenderer::BYTES_PER_PIXEL;
                    //SDL_Log("SDL_LockTexture succeeded: pitch=%d, rowBytes=%d", tex_pitch, row_bytes);
                    for (int y = 0; y < DisplayRenderer::HEIGHT; ++y) {
                        uint8_t* dstRow = static_cast<uint8_t*>(tex_pixels) + static_cast<size_t>(y) * tex_pitch;
                        const uint8_t* srcRow = src + static_cast<size_t>(y) * row_bytes;
                        memcpy(dstRow, srcRow, row_bytes);
                    }
                    SDL_UnlockTexture(app->displayTexture);
                    //SDL_Log("Texture updated via Lock/Unlock");
                }
                else {
                    // Fallback to SDL_UpdateTexture if lock failed
                    constexpr int pitch = DisplayRenderer::WIDTH * DisplayRenderer::BYTES_PER_PIXEL;
                    if (!SDL_UpdateTexture(app->displayTexture, &src_rect_i, app->displayRenderer.pixels(), pitch)) {
                        SDL_Log("SDL_UpdateTexture failed: %s", SDL_GetError());
                    }
                }
                // Render the texture stretched to the window viewport
                SDL_Rect dst;
                int ww, wh;
                SDL_GetWindowSize(app->window, &ww, &wh);
                dst.x = 0;
                dst.y = 0;
                dst.w = ww;
                dst.h = wh;
                // Convert dst to floating rect for rendering
                SDL_FRect dstF{0.0f, 0.0f, static_cast<float>(ww), static_cast<float>(wh)};
                if (!SDL_RenderTexture(app->renderer, app->displayTexture, &src_rect_f, &dstF)) {
                    SDL_Log("SDL_RenderTexture failed: %s", SDL_GetError());
                    // Fallback: draw a magenta rectangle so user sees something instead of a black screen
                    SDL_Log("Drawing fallback magenta rectangle to indicate texture render failure");
                    SDL_SetRenderDrawColor(app->renderer, 0xFF, 0x00, 0xFF, 0xFF);
                    SDL_RenderFillRect(app->renderer, &dstF);
                    // Restore draw color for ImGui
                    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
                }
            }
        }
    }
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), app->renderer);
    SDL_RenderPresent(app->renderer);

    // Process pending file dialog result (if any)
    {
        std::lock_guard<std::mutex> lk(app->fileDialogMutex);
        if (app->pendingDiskLoadFlag) {
            app->pendingDiskLoadFlag = false;

            // Load the selected disk image into the emulator
            if (auto* bus = app->machine->getBus()) {
                try {
                    std::ifstream in(app->pendingDiskPath, std::ios::binary);
                    if (!in) {
                        SDL_Log("Failed to open selected floppy image: %s", app->pendingDiskPath.c_str());
                    }
                    else {
                        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                                  std::istreambuf_iterator<char>());
                        bus->fdc()->loadDisk(0, data, true);
                        SDL_Log("Loaded floppy image '%s' into FDC drive 0 (%zu bytes)", app->pendingDiskPath.c_str(),
                                data.size());
                    }
                }
                catch (const std::exception& e) {
                    SDL_Log("Exception while loading floppy image: %s", e.what());
                }
            }
        }
    }

    return app->app_quit;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    if (const auto* app = static_cast<AppContext*>(appstate)) {
        if (app->displayTexture) {
            SDL_DestroyTexture(app->displayTexture);
        }
        SDL_DestroyRenderer(app->renderer);
        SDL_DestroyWindow(app->window);

        MIX_DestroyMixer(app->mixer);
        SDL_CloseAudioDevice(app->audioDevice);

        delete app;
    }

    TTF_Quit();
    MIX_Quit();

    SDL_Log("Application quit successfully!");
    SDL_Quit();
}

bool init_audio(SDL_AudioDeviceID* outAudioDevice, MIX_Mixer** outMixer) {
    // Desired audio spec
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.freq = 44100;
    spec.format = SDL_AUDIO_S16; // signed 16-bit samples
    spec.channels = 2; // stereo

    // 1) Init SDL audio
    const SDL_AudioDeviceID audioDevice = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (not audioDevice) {
        SDL_Log("Failed to open audio device: %s", SDL_GetError());
        return false;
    }

    // 2) Init SDL_mixer
    if (not MIX_Init()) {
        // returns bool in SDL3_mixer
        SDL_Log("MIX_Init failed: %s", SDL_GetError());
        return false;
    }

    // 3) Create a mixer bound to the default playback device.
    MIX_Mixer* mixer = MIX_CreateMixerDevice(audioDevice, &spec);
    if (not mixer) {
        SDL_Log("Mix_OpenAudioDevice failed: %s", SDL_GetError());
        SDL_CloseAudioDevice(audioDevice);
        return false;
    }

    SDL_Log("Audio initialized successfully (device %u)", audioDevice);
    if (outAudioDevice) {
        *outAudioDevice = audioDevice;
        *outMixer = mixer;
    }
    return true;
}

// Callback invoked by SDL when the open-file dialog completes. It may be called on another thread.
static void FileDialogCallback(void* userdata, const char* const* filelist, int /*filter_index*/) {
    if (!userdata) {
        return;
    }
    auto* app = static_cast<AppContext*>(userdata);
    if (!filelist) {
        SDL_Log("File dialog error: %s", SDL_GetError());
    }
    else if (!filelist[0]) {
        SDL_Log("File dialog canceled by user.");
    }
    else {
        // Store the first selected path for processing on the main thread
        {
            const char* path = filelist[0];
            std::lock_guard<std::mutex> lk(app->fileDialogMutex);
            app->pendingDiskPath = path;
            app->pendingDiskLoadFlag = true;
        }
    }

    // Free any filters array we allocated earlier and stored in the context
    if (app->pendingFilters) {
        delete[] app->pendingFilters;
        app->pendingFilters = nullptr;
    }
}
