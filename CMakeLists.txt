cmake_minimum_required(VERSION 3.21)
project(AngelSound VERSION 0.3.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ANGELSOUND_USE_SYSTEM_SDL3 "Prefer an installed SDL3 over FetchContent" ON)
option(ANGELSOUND_BUILD_TESTS "Build the unit tests" ON)
option(ANGELSOUND_WINDOWS_GUI "Build as a GUI (no console) app on Windows" ON)

if(MSVC)
    add_compile_options(/W4)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# --- SDL3 ---------------------------------------------------------------------
if(ANDROID AND ANGELSOUND_USE_SYSTEM_SDL3)
    # Android uses the SDL3 AAR's Prefab package; it supplies the headers and
    # the shared libSDL3.so that SDLActivity loads.
    find_package(SDL3 REQUIRED CONFIG)
elseif(ANGELSOUND_USE_SYSTEM_SDL3)
    find_package(SDL3 3.2 CONFIG QUIET)
endif()
if(NOT TARGET SDL3::SDL3)
    include(FetchContent)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    if(ANDROID)
        # SDLActivity loads libSDL3.so before libmain.so.
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz
        URL_HASH SHA256=f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f)
    FetchContent_MakeAvailable(SDL3)
endif()

# --- targets ------------------------------------------------------------------
add_library(angelsound_common STATIC
    src/wav.c
    src/practice.c
    src/recordlist.c
    src/ui_layout.c
    src/storage_pure.c)
target_include_directories(angelsound_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(angelsound_audio STATIC src/audio.c src/audio_format.c)
target_link_libraries(angelsound_audio PUBLIC angelsound_common SDL3::SDL3)

if(ANDROID)
    add_library(AngelSound SHARED
        src/engine.c
        src/ui.c
        src/storage.c
        src/android_storage.c)
    target_link_libraries(AngelSound PRIVATE angelsound_audio log android)
    target_include_directories(AngelSound PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set_target_properties(AngelSound PROPERTIES OUTPUT_NAME "main")
else()
    add_executable(AngelSound src/main.c src/engine.c src/ui.c src/storage.c)
    target_link_libraries(AngelSound PRIVATE angelsound_audio)
    target_include_directories(AngelSound PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

if(WIN32 AND ANGELSOUND_WINDOWS_GUI)
    set_target_properties(AngelSound PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(APPLE AND NOT ANDROID)
    set_target_properties(AngelSound PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in")
endif()

# Ship the SDL3 DLL next to the Windows executable so `AngelSound.exe` runs
# straight from the build directory.
if(WIN32 AND TARGET SDL3::SDL3)
    get_target_property(_sdl3_type SDL3::SDL3 TYPE)
    if(_sdl3_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET AngelSound POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:SDL3::SDL3>"
                    "$<TARGET_FILE_DIR:AngelSound>")
    endif()
endif()

# --- tests --------------------------------------------------------------------
if(ANGELSOUND_BUILD_TESTS AND NOT ANDROID)
    enable_testing()
    add_subdirectory(tests)
endif()