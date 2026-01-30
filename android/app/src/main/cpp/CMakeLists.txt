cmake_minimum_required(VERSION 3.30)
project(xemu_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- SDL2 ---
option(XEMU_FETCH_SDL2 "Download and build SDL2 for Android" ON)
set(SDL2_VERSION "2.32.10")

if(DEFINED SDL2_LOCAL_DIR)
  message(STATUS "Using local SDL2 at ${SDL2_LOCAL_DIR}")
  add_subdirectory("${SDL2_LOCAL_DIR}" "${CMAKE_BINARY_DIR}/sdl2-local")
elseif(XEMU_FETCH_SDL2)
  FetchContent_Declare(
    SDL2
    URL "https://github.com/libsdl-org/SDL/archive/refs/tags/release-${SDL2_VERSION}.zip"
  )
  FetchContent_MakeAvailable(SDL2)
endif()

add_library(xemu SHARED
  xemu_android.cpp
)

target_compile_definitions(xemu PRIVATE ANDROID __ANDROID__)

target_compile_options(xemu PRIVATE -fexceptions -frtti)

find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(glesv3-lib GLESv3)

if(TARGET SDL2::SDL2)
  target_link_libraries(xemu PRIVATE SDL2::SDL2)
endif()

target_link_libraries(xemu PRIVATE
  ${log-lib}
  ${android-lib}
  ${egl-lib}
  ${glesv3-lib}
)
