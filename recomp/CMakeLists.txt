cmake_minimum_required(VERSION 3.20)
# Framework version receipts have a single output path. Use one configuration
# with Visual Studio so CMake does not generate conflicting per-config files.
set(CMAKE_CONFIGURATION_TYPES Release CACHE STRING "Native game configuration" FORCE)
project(RageRacerRecompiled LANGUAGES C CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(BUILD_TESTING OFF CACHE BOOL "Skip dependency test/example programs" FORCE)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "Skip zlib example executables" FORCE)
set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/psxrecomp" CACHE PATH "Pinned framework checkout")
set(PSX_RECOMP_UI OFF CACHE BOOL "Launcher integration")
set(PSX_NETPLAY OFF CACHE BOOL "Online features")
set(PSX_REWIND OFF CACHE BOOL "Rewind requires optional rbengine submodule")
set(PSXRECOMP_REQUIRE_GAME_C ON CACHE BOOL "Require actual translated game code")
set(PSXRECOMP_BIOS_STEMS OpenBIOS CACHE STRING "Bundled open source BIOS")
include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")
psxrecomp_add_game_runtime(rage-racer
    PRELOADED_MODS_DIR NONE
    WINDOW_TITLE "Rage Racer - PC"
    APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/../assets/Game.ico"
    DEFAULT_GAME_CONFIG_PATH "${CMAKE_CURRENT_SOURCE_DIR}/game.toml"
    MAX_PLAYERS 1
    GEN_MARKER "generated/SCES_006.50_dispatch.c"
    GEN_FULL_GLOB "generated/SCES_006.50_full_*.c"
)
# Each disc uses its own translated routines and original, unmodified assets.
file(GLOB region_sources CONFIGURE_DEPENDS "generated/jp/*_full_*.c" "generated/jp/*_dispatch.c" "generated/us/*_full_*.c" "generated/us/*_dispatch.c")
target_sources(rage-racer PRIVATE ${region_sources} src/rage_region_dispatch.cpp)
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/generated/SCES_006.50_dispatch.c" APPEND PROPERTY COMPILE_DEFINITIONS
    psx_game_address_in_text=rage_eu_psx_game_address_in_text
    psx_game_text_native_ok=rage_eu_psx_game_text_native_ok
    psx_game_text_native_ok_full=rage_eu_psx_game_text_native_ok_full
    psx_dispatch_game_compiled=rage_eu_psx_dispatch_game_compiled
    psx_game_is_function_entry=rage_eu_psx_game_is_function_entry)

target_sources(rage-racer PRIVATE src/rage_graphics_options.cpp)
target_sources(rage-racer PRIVATE src/rage_music.cpp src/song_catalog.cpp)
target_sources(rage-racer PRIVATE src/rage_bundle.cpp)
target_sources(rage-racer PRIVATE src/rage_scene.cpp)
target_sources(rage-racer PRIVATE src/rage_car_lamps.c)
target_sources(rage-racer PRIVATE src/rage_minimap.cpp)
target_sources(rage-racer PRIVATE src/rage_free_run.cpp)
target_sources(rage-racer PRIVATE src/rage_multiplayer.cpp)
target_sources(rage-racer PRIVATE src/rage_free_world.cpp)
target_sources(rage-racer PRIVATE src/rage_track.cpp src/rage_track_topology.cpp src/rage_oval.cpp src/track_texture_snapshot.c src/native_visibility.c)
target_link_libraries(rage-racer PRIVATE bcrypt comctl32 shell32)
target_include_directories(rage-racer PRIVATE include)
target_compile_definitions(rage-racer PRIVATE RAGE_GRAPHICS_OPTIONS=1 PSX_PGXP=1)
set(RAGE_EXECUTABLE_NAME "RageRacer" CACHE STRING "Windows executable name")
set_target_properties(rage-racer PROPERTIES OUTPUT_NAME "${RAGE_EXECUTABLE_NAME}")
if(MSVC)
    # Visual Studio cannot express language-specific options on mixed targets.
    # Set C11 atomics on individual C sources, including diagnostic rings.
    get_target_property(rage_sources rage-racer SOURCES)
    foreach(rage_source IN LISTS rage_sources)
        if(rage_source MATCHES "\\.c$")
            set_property(SOURCE "${rage_source}" APPEND PROPERTY
                COMPILE_OPTIONS /std:c11 /experimental:c11atomics)
        endif()
    endforeach()
endif()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../vendor/audio" "${CMAKE_CURRENT_BINARY_DIR}/audio")
target_link_libraries(rage-racer PRIVATE rage_mp3_decoder)
target_sources(rage-racer PRIVATE ../vendor/audio/cdda_archive.cpp)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../generator" "${CMAKE_CURRENT_BINARY_DIR}/generator")
