cmake_minimum_required(VERSION 3.21)

# Gradle's externalNativeBuild entry point.  Reuse the desktop CMakeLists so the
# Android and desktop builds share sources and SDL settings.
set(ANGELSOUND_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(ANGELSOUND_USE_SYSTEM_SDL3 OFF CACHE BOOL "" FORCE)
set(ANGELSOUND_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory("${ANGELSOUND_ROOT}" angelsound_build)