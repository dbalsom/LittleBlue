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

#include "Blip_Buffer.h"

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

// Default window size (TODO: Read from config)
constexpr uint32_t windowStartWidth = 1280;
constexpr uint32_t windowStartHeight = 1024;

bool init_audio(SDL_AudioDeviceID* outAudioDevice, MIX_Mixer** outMixer, SDL_AudioStream** outStream);

// Main application context. Holds SDL objects, Machine instance, and UI state.
struct AppContext
{
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_AudioDeviceID audio_device = 0;
    MIX_Mixer* mixer = nullptr;
    SDL_AudioStream* pc_speaker_stream = nullptr;
    SDL_AppResult app_quit = SDL_APP_CONTINUE;
    Machine* machine = nullptr;
    DebuggerManager dbg_manager;
    bool running{true};

    // Blip buffer for audio
    Blip_Buffer blip_buf{};
    Blip_Synth<blip_good_quality, 20> blip_synth;
    blip_sample_t samples[BLIP_SAMPLE_COUNT];

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
    DisplayRenderer display_renderer;
    SDL_Texture* display_texture{nullptr};

    bool show_about{false};
    bool show_demo{false};
    bool show_io_debug{false};

    // Emulator Debugger flags.
    MemoryEditor mem_editor;
    bool show_memory_viewer{false};
    bool show_vram_viewer{false};
    bool show_stack_viewer{false};
    bool show_cpu_viewer{false};
    bool show_video_card_viewer{false};
    bool show_pic_viewer{false};
    bool show_dma_viewer{false};
    bool show_display_debug{false};
    bool cpu_running{true};
    bool show_disassembly{false};

    // CPU timing display
    uint64_t last_cycle_count{0};
    double smoothed_mhz{0.0};

    // Cycle log UI
    bool show_cycle_log{false};
    bool cycle_log_auto_scroll{true};
    int cycle_log_capacity_ui{10000};
    size_t last_seen_cycle_log_size{0};

    // File dialog pending result handling
    std::mutex file_dialog_mutex;
    SDL_DialogFileFilter* pending_filters{nullptr}; // owned until dialog callback
    std::string pending_disk_path;
    bool pending_disk_load_flag{false};

    void resetMachine() {
        last_cycle_count = 0;
        machine->resetMachine();
    }
};

// SDL failure callback - logs the error and returns failure code.
SDL_AppResult SDL_Fail() {
    SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "Error %s", SDL_GetError());
    return SDL_APP_FAILURE;
}

// SDL Application Initialization callback. We do all our emulator initialization here.
SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {

    // First step is to initialize SDL with the services we need specified in flags. We want to use Video and Audio.
    if (not SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        return SDL_Fail();
    }

    // Initialize SDL TTF (unused at the moment...)
    if (not TTF_Init()) {
        return SDL_Fail();
    }

    // Create our main window. We want it to be resizable and high-DPI aware.
    SDL_Window* window =
        SDL_CreateWindow(
            "XTCE-Blue",
            windowStartWidth,
            windowStartHeight,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    if (not window) {
        return SDL_Fail();
    }

    // If we want to show graphics, we need a renderer, so create one now.
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (not renderer) {
        return SDL_Fail();
    }

    // Enable Vsync. Running at 1000fps is impressive, but it can cause headaches for time stepping.
    SDL_SetRenderVSync(renderer, 1);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls

    // Initialize ImGui's SDL3 integrations.
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // Get the directory our executable ran from - we can use this base path for loading resources, etc.
    const auto basePathPtr = SDL_GetBasePath();
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
    SDL_AudioDeviceID audio_device;
    MIX_Mixer* mixer;
    SDL_AudioStream* stream;
    init_audio(&audio_device, &mixer, &stream);


    // Create a Machine - this represents our emulator core.
    auto machine = new Machine();

    // Set up our emulator application context.
    auto* ctx = new AppContext();
    ctx->window = window;
    ctx->renderer = renderer;
    ctx->audio_device = audio_device;
    ctx->mixer = mixer;
    ctx->pc_speaker_stream = stream;
    ctx->machine = machine;
    ctx->last_counter = SDL_GetPerformanceCounter();

    // Initialize Blip_Buffer
    ctx->blip_buf.sample_rate(AUDIO_SAMPLE_RATE);
    ctx->blip_buf.bass_freq(200); // 200Hz high-pass filter to reduce bass rumble
    // PIT clock is (crystal / 12) or ~ 1.19318 MHz
    ctx->blip_buf.clock_rate(static_cast<long>(ctx->crystal_hz / 12.0));
    ctx->blip_synth.volume(0.35);
    ctx->blip_synth.output(&ctx->blip_buf);

    const blip_eq_t eq(
        0.0, // 0dB = fairly flat highs
        8000, // roll off above ~8kHz
        AUDIO_SAMPLE_RATE);

    ctx->blip_synth.treble_eq(eq);

    // Attach our callback
    ctx->machine->getBus()->setSpeakerCallback(
        [ctx](uint64_t tick, bool state, bool enabled)
        {
            // Calculate elapsed ticks
            const auto elapsed_ticks = static_cast<blip_time_t>(ctx->machine->getElapsedPitTicks(false));
            if (enabled) {
                ctx->blip_synth.offset(elapsed_ticks, state ? 1 : -1);
            }
            else {
                // If the speaker is disabled, drive 0 level.
                ctx->blip_synth.offset(elapsed_ticks, 0);
            }
        });

    // Show our new SDL window, and print some debugs about its size and DPI.
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

    // Create the display texture used to show the CGA framebuffer. Use RGBA and streaming access for frequent updates.
    ctx->display_texture =
        SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STREAMING,
            DisplayRenderer::WIDTH,
            DisplayRenderer::HEIGHT);
    if (!ctx->display_texture) {
        SDL_Log("Failed to create display texture: %s", SDL_GetError());
        //return SDL_APP_FAILURE;
    }

    // Register our various debug windows with the AppContext's dbgManager
    ctx->dbg_manager.addWindow("Disassembly", std::make_unique<DisassemblyWindow>(machine),
                               &ctx->show_disassembly);
    ctx->dbg_manager.addWindow("Cpu Status", std::make_unique<CpuStatusWindow>(machine), &ctx->show_cpu_viewer);
    ctx->dbg_manager.addWindow("Memory Viewer", std::make_unique<MemoryViewerWindow>(machine),
                               &ctx->show_memory_viewer);
    ctx->dbg_manager.addWindow("VRAM Viewer", std::make_unique<MemoryViewerWindow>(machine, true),
                               &ctx->show_vram_viewer);
    ctx->dbg_manager.addWindow("Stack Viewer", std::make_unique<StackViewerWindow>(machine),
                               &ctx->show_stack_viewer);
    ctx->dbg_manager.addWindow("Cycle Log", std::make_unique<CycleLogWindow>(machine), &ctx->show_cycle_log);
    ctx->dbg_manager.addWindow("Video Card Status", std::make_unique<VideoCardStatusWindow>(machine),
                               &ctx->show_video_card_viewer);
    ctx->dbg_manager.addWindow("PIC Status", std::make_unique<PicStatusWindow>(machine), &ctx->show_pic_viewer);
    ctx->dbg_manager.addWindow("DMA Status", std::make_unique<DmacStatusWindow>(machine),
                               &ctx->show_dma_viewer);
    // Display debug window uses the app's displayTexture pointer
    ctx->dbg_manager.addWindow("Display Debug", std::make_unique<DisplayDebugWindow>(&ctx->display_texture),
                               &ctx->show_display_debug);

    // Initialize cycle count baseline for MHz measurement
    ctx->last_cycle_count = ctx->machine->cycleCount();

    // Calculate a ticks_per_frame based on the main system crystal frequency (14.3181818Mhz) and 60 FPS (CGA refresh rate).
    // If you ever wanted to support other systems or video cards (MDA: 50Hz, VGA: 60/70Hz), you'd need to make
    // these values configurable.
    constexpr double fps = 60.0;
    ctx->ticks_per_frame = static_cast<int>(std::llround(ctx->crystal_hz / fps));

    // Initialize cycle log UI capacity from machine state
    ctx->cycle_log_capacity_ui = static_cast<int>(ctx->machine->getCycleLogCapacity());

    // Assign our application state via the pointer passed in.
    *appstate = ctx;

    SDL_Log("Application started successfully!");

    // Start the emulator!
    ctx->machine->run();

    return SDL_APP_CONTINUE;
}

// SDL's event callback. Handle UI events and pass relevant input to the Machine.
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    auto* app = static_cast<AppContext*>(appstate);
    uint8_t sc{};

    // Let ImGui process the event first
    ImGui_ImplSDL3_ProcessEvent(event);
    // Get a ImGuiIO for input capture checks
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
            // Only send key events to the machine if ImGui is not capturing keyboard input.
            if (!io.WantCaptureKeyboard) {
                sc = translate_SDL_key(event->key.key, true);
                app->machine->sendScanCode(sc);
            }
            break;

        case SDL_EVENT_KEY_UP:
            // Only send key events to the machine if ImGui is not capturing keyboard input.
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

// Main SDL loop. This is called repeatedly to run our program - we do our emulation stepping and rendering here.
SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* app = static_cast<AppContext*>(appstate);
    if (!app || !app->machine) {
        return SDL_APP_FAILURE;
    }

    // Get the time delta since last update.
    const Uint64 now = SDL_GetPerformanceCounter();
    const double delta = static_cast<double>(now - app->last_counter) / static_cast<double>(
        SDL_GetPerformanceFrequency());
    app->last_counter = now;

    // Accumulate time and frames for smoother FPS
    app->fps_timer += static_cast<float>(delta);
    app->frame_count++;

    // Update CPU cycle / MHz measurement
    uint64_t cycles = app->machine->cycleCount();
    uint64_t deltaCycles = cycles >= app->last_cycle_count ? cycles - app->last_cycle_count : 0;
    if (delta > 0.0) {
        double mhz = (static_cast<double>(deltaCycles) / delta) / 1e6; // MHz
        // Exponential smoothing: 90% previous, 10% new (adjust as needed)
        constexpr double alpha = 0.10;
        app->smoothed_mhz = (1.0 - alpha) * app->smoothed_mhz + alpha * mhz;
    }
    app->last_cycle_count = cycles;

    if (app->fps_timer >= 0.5f) {
        // Update FPS twice per second
        app->fps = static_cast<float>(app->frame_count) / app->fps_timer;
        app->frame_count = 0;
        app->fps_timer = 0.0f;

        char title[128];
        snprintf(title, sizeof(title), "XTCE-Blue — %.1f FPS", app->fps);
        SDL_SetWindowTitle(app->window, title);
    }

    // Convert elapsed wall-clock time to CPU ticks (crystal cycles) and run the machine.
    // Use an accumulator of fractional ticks to preserve long-term accuracy.
    // Cap the amount of work per frame to avoid spiral-of-death if the app stalls.
    double ticks_this_frame = delta * app->crystal_hz;
    app->tick_accumulator += ticks_this_frame;

    // Compute integer ticks to execute this iteration
    if (long ticks_to_run = static_cast<long>(std::floor(app->tick_accumulator)); ticks_to_run > 0) {
        // Cap ticks_to_run to a reasonable burst (e.g., max_frame_burst * ticks_per_frame)
        const long max_ticks =
            static_cast<long>(app->max_frame_burst) * static_cast<long>(std::max(1, app->ticks_per_frame));
        if (ticks_to_run > max_ticks) {
            ticks_to_run = max_ticks;
        }

        // Calculate audio latency
        constexpr int max_queued_samples = AUDIO_SAMPLE_RATE * AUDIO_MAX_LATENCY_MS / 1000;
        constexpr int max_queued_bytes = max_queued_samples * sizeof(int16_t);
        int queued_bytes = SDL_GetAudioStreamAvailable(app->pc_speaker_stream);
        if (queued_bytes > max_queued_bytes) {
            // Too much audio queued, skip this frame's execution to let audio drain.
            // This is not the best way to do this - better to dynamically adjust the audio stream's sample rate.
            return SDL_APP_CONTINUE;
        }

        // Run the emulator.
        if (app->machine->isRunning()) {

            // We break up the total per-frame ticks_to_run into smaller slices to allow more frequent audio and input
            // updates. For example, it is quite possible for a fast typist to generate multiple scancodes within a single
            // 16.67ms frame (remember scancodes are sent on both key down and key up).
            for (int si = 0; si < EMU_FRAME_SLICES; si++) {
                auto slice_ticks = ticks_to_run / EMU_FRAME_SLICES;
                if (si == EMU_FRAME_SLICES - 1) {
                    // Last slice takes any remainder
                    slice_ticks += ticks_to_run % EMU_FRAME_SLICES;
                }

                // Run emulator time slice.
                app->machine->run_for(static_cast<uint64_t>(slice_ticks));
                app->tick_accumulator -= static_cast<double>(slice_ticks);

                // The PIT clock drives audio sync. Get the number of PIT ticks elapsed this slice.
                // Passing true resets the tick counter for the next slice.
                auto pit_ticks_elapsed = app->machine->getElapsedPitTicks(true);
                //SDL_Log("PIT ticks elapsed this frame: %llu", static_cast<unsigned long long>(pit_ticks_elapsed));

                // Tell Blip_Buffer we have completed an audio frame (emulator time slice).
                app->blip_buf.end_frame(static_cast<blip_time_t>(pit_ticks_elapsed));

                static int16_t temp[2048];
                for (;;) {
                    int n = app->blip_buf.read_samples(temp, 2048);
                    if (n <= 0) {
                        break;
                    }
                    SDL_PutAudioStreamData(app->pc_speaker_stream, temp, n * sizeof(int16_t));
                }
            }
        }
        else {
            // If CPU is paused, prevent tick_accumulator from growing without bound by capping it
            double max_accum = static_cast<long>(app->max_frame_burst) * std::max(1, app->ticks_per_frame);
            if (app->tick_accumulator > max_accum) {
                app->tick_accumulator = max_accum;
            }
        }
    }


    // If nothing to run, we still yield to UI and rendering below
    if (!app->running) {
        return SDL_APP_SUCCESS;
    }

    // Ensure no stale scaling state
    SDL_SetRenderViewport(app->renderer, nullptr);
    SDL_SetRenderClipRect(app->renderer, nullptr);

    // Start a new ImGui frame - SDL integrations first, native NewFrame() second.
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui::NewFrame();

    // Draw the main menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Emulator")) {
            if (ImGui::MenuItem("About...")) {
                app->show_about = true; // open modal dialog
            }
            ImGui::Separator();

            if (ImGui::MenuItem("Quit", "Alt+F4")) {
                app->running = false; // signals to stop in next iteration
            }
            ImGui::EndMenu();
        }


        if (ImGui::BeginMenu("Machine")) {
            if (ImGui::MenuItem("Reboot")) {
                app->resetMachine();
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
                    std::lock_guard<std::mutex> lk(app->file_dialog_mutex);
                    app->pending_filters = filters;
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
            app->dbg_manager.drawMenuItems();
            ImGui::EndMenu();
        }

        // Example: add more top-level menus later
        // if (ImGui::BeginMenu("Edit")) { ... ImGui::EndMenu(); }

        ImGui::EndMainMenuBar();
    }

    // --- About dialog ---
    if (app->show_about) {
        ImGui::OpenPopup("About XTCE-Blue");
    }

    if (app->show_demo) {
        ImGui::ShowDemoWindow();
    }

    if (ImGui::BeginPopupModal("About XTCE-Blue", &app->show_about,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("XTCE-Blue\n");
        ImGui::Separator();
        ImGui::Text("Version: 0.1.0");
        ImGui::Text("Authors:");
        ImGui::BulletText("Andrew Jenner (reenigne) - original XTCE CPU core");
        ImGui::BulletText("Daniel Balsom (gloriouscow) - SDL3 frontend, CGA implementation");

        ImGui::NewLine();

        ImGui::NewLine();
        ImGui::TextWrapped("XTCE-Blue is a cycle-accurate IBM XT emulator written in C++ using SDL3 and ImGui. "
            "XTCE-Blue's 8088 emulation is powered by the XTCE 8088 CPU core - "
            "which emulates the 8088 at the microcode level.");

        ImGui::NewLine();

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            app->show_about = false;
        }

        ImGui::EndPopup();
    }

    if (app->show_io_debug) {
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
    app->dbg_manager.showAll();

    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);

    ImGui::Render();

    // Update and render display texture (CGA) before ImGui is drawn so UI overlays appear on top.
    if (app->display_texture && app->machine) {
        if (auto* bus = app->machine->getBus()) {
            if (auto* cga = bus->cga()) {
                app->display_renderer.render(cga);

                // Integer rect for texture lock/update
                SDL_Rect src_rect_i{0, 0, static_cast<int>(DisplayRenderer::WIDTH),
                                    static_cast<int>(DisplayRenderer::HEIGHT)};
                // Floating-point rect for rendering APIs (SDL_RenderTexture expects SDL_FRect)
                SDL_FRect src_rect_f{0.0f, 0.0f, static_cast<float>(DisplayRenderer::WIDTH),
                                     static_cast<float>(DisplayRenderer::HEIGHT)};
                // Try locking the streaming texture and memcpy rows to the texture memory for reliable updates.
                void* tex_pixels = nullptr;
                int tex_pitch = 0;
                if (SDL_LockTexture(app->display_texture, &src_rect_i, &tex_pixels, &tex_pitch) && (tex_pixels !=
                    nullptr)) {
                    const uint8_t* src = app->display_renderer.pixels();
                    constexpr int row_bytes = DisplayRenderer::WIDTH * DisplayRenderer::BYTES_PER_PIXEL;
                    //SDL_Log("SDL_LockTexture succeeded: pitch=%d, rowBytes=%d", tex_pitch, row_bytes);
                    for (int y = 0; y < DisplayRenderer::HEIGHT; ++y) {
                        uint8_t* dstRow = static_cast<uint8_t*>(tex_pixels) + static_cast<size_t>(y) * tex_pitch;
                        const uint8_t* srcRow = src + static_cast<size_t>(y) * row_bytes;
                        memcpy(dstRow, srcRow, row_bytes);
                    }
                    SDL_UnlockTexture(app->display_texture);
                    //SDL_Log("Texture updated via Lock/Unlock");
                }
                else {
                    // Fallback to SDL_UpdateTexture if lock failed
                    constexpr int pitch = DisplayRenderer::WIDTH * DisplayRenderer::BYTES_PER_PIXEL;
                    if (!SDL_UpdateTexture(app->display_texture, &src_rect_i, app->display_renderer.pixels(),
                                           pitch)) {
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
                if (!SDL_RenderTexture(app->renderer, app->display_texture, &src_rect_f, &dstF)) {
                    SDL_Log("SDL_RenderTexture failed: %s", SDL_GetError());
                    // Fallback: Draw a magenta rectangle so user sees something obvious instead of a black screen
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
        std::lock_guard<std::mutex> lk(app->file_dialog_mutex);
        if (app->pending_disk_load_flag) {
            app->pending_disk_load_flag = false;

            // Load the selected disk image into the emulator
            if (auto* bus = app->machine->getBus()) {
                try {
                    std::ifstream in(app->pending_disk_path, std::ios::binary);
                    if (!in) {
                        SDL_Log("Failed to open selected floppy image: %s", app->pending_disk_path.c_str());
                    }
                    else {
                        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                                  std::istreambuf_iterator<char>());
                        bus->fdc()->loadDisk(0, data, true);
                        SDL_Log("Loaded floppy image '%s' into FDC drive 0 (%zu bytes)",
                                app->pending_disk_path.c_str(),
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

// Called when our SDL app needs to exit. We should clean up all our resources here.
void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    if (const auto* app = static_cast<AppContext*>(appstate)) {
        if (app->display_texture) {
            SDL_DestroyTexture(app->display_texture);
        }
        SDL_DestroyRenderer(app->renderer);
        SDL_DestroyWindow(app->window);

        MIX_DestroyMixer(app->mixer);
        SDL_CloseAudioDevice(app->audio_device);

        delete app;
    }

    TTF_Quit();
    MIX_Quit();

    SDL_Log("Application quit successfully!");
    SDL_Quit();
}

bool init_audio(SDL_AudioDeviceID* outAudioDevice, MIX_Mixer** outMixer, SDL_AudioStream** outStream) {

    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");

    // Desired audio spec
    constexpr auto out_spec = SDL_AudioSpec{
        .format = SDL_AUDIO_S16,
        .channels = 2,
        .freq = AUDIO_SAMPLE_RATE,
    };

    // 1) Init SDL audio
    const SDL_AudioDeviceID audio_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &out_spec);
    if (not audio_device) {
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
    MIX_Mixer* mixer = MIX_CreateMixerDevice(audio_device, &out_spec);
    if (not mixer) {
        SDL_Log("Mix_OpenAudioDevice failed: %s", SDL_GetError());
        SDL_CloseAudioDevice(audio_device);
        return false;
    }

    SDL_Log("Audio initialized successfully (device %u)", audio_device);

    // 4) Create an audio stream
    // Create an Audio stream for the PC speaker.
    constexpr auto in_spec = SDL_AudioSpec{
        .format = SDL_AUDIO_S16,
        .channels = 1,
        .freq = AUDIO_SAMPLE_RATE,
    };
    SDL_AudioStream* stream = SDL_CreateAudioStream(&in_spec, &out_spec);
    if (not stream) {
        SDL_Log("SDL_CreateAudioStream failed: %s", SDL_GetError());
        MIX_DestroyMixer(mixer);
        SDL_CloseAudioDevice(audio_device);
        return false;
    }

    SDL_BindAudioStream(audio_device, stream);
    SDL_ResumeAudioDevice(audio_device);
    *outAudioDevice = audio_device;
    *outMixer = mixer;
    *outStream = stream;

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
            std::lock_guard<std::mutex> lk(app->file_dialog_mutex);
            app->pending_disk_path = path;
            app->pending_disk_load_flag = true;
        }
    }

    // Free any filters array we allocated earlier and stored in the context
    if (app->pending_filters) {
        delete[] app->pending_filters;
        app->pending_filters = nullptr;
    }
}
