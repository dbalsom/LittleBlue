#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_init.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3_image/SDL_image.h>
#include <cmath>
#include <string_view>
#include <filesystem>

#include "littleblue.h"
#include "core/Machine.h"

constexpr uint32_t windowStartWidth = 640;
constexpr uint32_t windowStartHeight = 400;

bool init_audio(SDL_AudioDeviceID *outAudioDevice, MIX_Mixer **outMixer);

struct AppContext {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* messageTex, *imageTex;
    SDL_FRect messageDest;
    SDL_AudioDeviceID audioDevice;
    MIX_Mixer* mixer;
    SDL_AppResult app_quit = SDL_APP_CONTINUE;
    Machine* machine;
};

SDL_AppResult SDL_Fail(){
    SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "Error %s", SDL_GetError());
    return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    // init the library, here we make a window so we only need the Video capabilities.
    if (not SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)){
        return SDL_Fail();
    }
    
    // init TTF
    if (not TTF_Init()) {
        return SDL_Fail();
    }
    
    // create a window
   
    SDL_Window* window = SDL_CreateWindow("LittleBlue", windowStartWidth, windowStartHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (not window){
        return SDL_Fail();
    }
    
    // create a renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    if (not renderer){
        return SDL_Fail();
    }

    auto basePathPtr = SDL_GetBasePath();
    if (not basePathPtr){
        return SDL_Fail();
    }
    const std::filesystem::path basePath = basePathPtr;

    const auto fontPath = basePath / "Inter-VariableFont.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.string().c_str(), 36);
    if (not font) {
        return SDL_Fail();
    }

    // render the font to a surface
    const std::string_view text = "Hello SDL!";
    SDL_Surface* surfaceMessage = TTF_RenderText_Solid(font, text.data(), text.length(), { 255,255,255 });

    // make a texture from the surface
    SDL_Texture* messageTex = SDL_CreateTextureFromSurface(renderer, surfaceMessage);

    // we no longer need the font or the surface, so we can destroy those now.
    TTF_CloseFont(font);
    SDL_DestroySurface(surfaceMessage);

    // load the SVG
    auto svg_surface = IMG_Load((basePath / "gs_tiger.svg").string().c_str());
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, svg_surface);
    SDL_DestroySurface(svg_surface);
    

    // get the on-screen dimensions of the text. this is necessary for rendering it
    auto messageTexProps = SDL_GetTextureProperties(messageTex);
    SDL_FRect text_rect{
            .x = 0,
            .y = 0,
            .w = float(SDL_GetNumberProperty(messageTexProps, SDL_PROP_TEXTURE_WIDTH_NUMBER, 0)),
            .h = float(SDL_GetNumberProperty(messageTexProps, SDL_PROP_TEXTURE_HEIGHT_NUMBER, 0))
    };

    // Initialize audio
    SDL_AudioDeviceID audioDevice;
    MIX_Mixer *mixer;
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

    // print some information about the window
    SDL_ShowWindow(window);
    {
        int width, height, bbwidth, bbheight;
        SDL_GetWindowSize(window, &width, &height);
        SDL_GetWindowSizeInPixels(window, &bbwidth, &bbheight);
        SDL_Log("Window size: %ix%i", width, height);
        SDL_Log("Backbuffer size: %ix%i", bbwidth, bbheight);
        if (width != bbwidth){
            SDL_Log("This is a highdpi environment.");
        }
    }

    // set up the application data
    *appstate = new AppContext{
       .window = window,
       .renderer = renderer,
       .messageTex = messageTex,
       .imageTex = tex,
       .messageDest = text_rect,
       .audioDevice = audioDevice,
        .mixer = mixer,
        .machine = machine,
    };
    
    SDL_SetRenderVSync(renderer, -1);   // enable vysnc
    
    SDL_Log("Application started successfully!");

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event* event) {
    auto* app = (AppContext*)appstate;
    
    if (event->type == SDL_EVENT_QUIT) {
        app->app_quit = SDL_APP_SUCCESS;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    auto* app = (AppContext*)appstate;

    // Run the machine
    app->machine->run_for(10000);

    // draw a color
    auto time = SDL_GetTicks() / 1000.f;
    auto red = (std::sin(time) + 1) / 2.0 * 255;
    auto green = (std::sin(time / 2) + 1) / 2.0 * 255;
    auto blue = (std::sin(time) * 2 + 1) / 2.0 * 255;
    
    SDL_SetRenderDrawColor(app->renderer, red, green, blue, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(app->renderer);

    // Renderer uses the painter's algorithm to make the text appear above the image, we must render the image first.
    SDL_RenderTexture(app->renderer, app->imageTex, NULL, NULL);
    SDL_RenderTexture(app->renderer, app->messageTex, NULL, &app->messageDest);

    SDL_RenderPresent(app->renderer);

    return app->app_quit;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    auto* app = (AppContext*)appstate;
    if (app) {
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

bool init_audio(SDL_AudioDeviceID *outAudioDevice, MIX_Mixer **outMixer)
{
    // Desired audio spec
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.freq = 44100;
    spec.format = SDL_AUDIO_S16;   // signed 16-bit samples
    spec.channels = 2;             // stereo

    // 1) Init SDL audio
    SDL_AudioDeviceID audioDevice = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (not audioDevice) {
        SDL_Log("Failed to open audio device: %s", SDL_GetError());
        return false;
    }

    // 2) Init SDL_mixer
    if (not MIX_Init()) {  // returns bool in SDL3_mixer
        SDL_Log("MIX_Init failed: %s", SDL_GetError());
        return false;
    }

    // 3) Create a mixer bound to the default playback device.
    MIX_Mixer *mixer = MIX_CreateMixerDevice(audioDevice, &spec);
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
