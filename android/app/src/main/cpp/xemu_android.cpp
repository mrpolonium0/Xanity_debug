#include <SDL.h>
#include <SDL_main.h>
#include <SDL_system.h>

#include <GLES3/gl3.h>

#include <android/log.h>

namespace {
constexpr const char* kLogTag = "xemu-android";
}

extern "C" int xemu_android_main(int argc, char** argv) __attribute__((weak));

static void LogInfo(const char* msg) {
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", msg);
}

extern "C" int SDL_main(int argc, char* argv[]) {
  if (xemu_android_main) {
    return xemu_android_main(argc, argv);
  }

  (void)argc;
  (void)argv;

  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  SDL_DisableScreenSaver();

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "SDL_Init failed: %s", SDL_GetError());
    return 1;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  SDL_Window* window = SDL_CreateWindow(
    "xemu (Android bootstrap)",
    SDL_WINDOWPOS_CENTERED,
    SDL_WINDOWPOS_CENTERED,
    1280,
    720,
    SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
  );

  if (!window) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "SDL_CreateWindow failed: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_GLContext gl = SDL_GL_CreateContext(window);
  if (!gl) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "SDL_GL_CreateContext failed: %s", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  SDL_GL_MakeCurrent(window, gl);
  SDL_GL_SetSwapInterval(1);

  LogInfo("xemu Android bootstrap running (core not wired yet)");

  bool running = true;
  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) {
        running = false;
      } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_AC_BACK) {
        running = false;
      }
    }

    int w = 0;
    int h = 0;
    SDL_GL_GetDrawableSize(window, &w, &h);
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;

    glViewport(0, 0, w, h);
    glClearColor(0.05f, 0.07f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    SDL_GL_SwapWindow(window);
  }

  SDL_GL_DeleteContext(gl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
