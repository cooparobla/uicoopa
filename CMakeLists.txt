cmake_minimum_required(VERSION 3.16)
cmake_policy(VERSION 3.16)

project(uicoopa_test_window)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(ROOT_DIR_PARENT "${CMAKE_SOURCE_DIR}" DIRECTORY)

# --- Sibling library paths ---
set(GFXCOOPA_DIR "${ROOT_DIR_PARENT}/gfxcoopa")
set(CAML_DIR     "${ROOT_DIR_PARENT}/caml")
if(NOT TARGET coopa::gfx)
    add_subdirectory(${GFXCOOPA_DIR} ${CMAKE_CURRENT_BINARY_DIR}/gfxcoopa-build)
endif()

# --- Optional audio support (sfxcoopa) ---
# ON by default, but degrades to a no-audio build (rather than a configure
# failure) if the sibling repo simply isn't checked out. Every audio call
# site in this repo is guarded by #ifdef UICOOPA_HAS_AUDIO, so both configs
# build cleanly -- see uicoopa/audio/ui_audio.h's file doc.
set(SFXCOOPA_DIR "${ROOT_DIR_PARENT}/sfxcoopa")
option(UICOOPA_WITH_AUDIO "Build uicoopa with sfxcoopa audio support" ON)
set(UICOOPA_AUDIO_LIBS "")
if(UICOOPA_WITH_AUDIO AND EXISTS "${SFXCOOPA_DIR}/CMakeLists.txt")
    if(NOT TARGET coopa::sfx)
        add_subdirectory(${SFXCOOPA_DIR} ${CMAKE_CURRENT_BINARY_DIR}/sfxcoopa-build)
    endif()
    set(UICOOPA_AUDIO_LIBS coopa::sfx)
    add_compile_definitions(UICOOPA_HAS_AUDIO)
elseif(UICOOPA_WITH_AUDIO)
    message(WARNING "UICOOPA_WITH_AUDIO is ON but ${SFXCOOPA_DIR} was not found -- building without audio.")
endif()

# --- Include directories ---
include_directories(${CMAKE_SOURCE_DIR}/)                  # uicoopa/
include_directories(${CMAKE_SOURCE_DIR}/includes/)         # stb_truetype (stb_image/_write now come from gfxcoopa)
include_directories(${CAML_DIR}/includes/)                 # caml/ + fkYAML
include_directories(${CAML_DIR})                           # caml.h root

# --- Configure and include root directory header ---
configure_file(configuration/root_directory.h.in configuration/root_directory.h)
include_directories(${CMAKE_BINARY_DIR}/configuration)

# --- stb_truetype: implementation compiled once here (stb_image/_write now
# live in gfxcoopa's single implementation TU; only stb_truetype is unique
# to uicoopa) ---
add_definitions(-DSTB_TRUETYPE_IMPLEMENTATION)

# --- Shader compilation ---
# Previously relied entirely on cbuild --vulkan (checked-in .spv, plain `glslc <file> -o
# <file>.spv`, no -I). That stops working the moment a uicoopa shader #includes
# <gfx/surface2d/...> from gfxcoopa's shared 2D quad backbone -- gfx_add_shader_target()
# (owned by gfxcoopa, see its own file doc) always adds gfxcoopa's assets/shaders/ to the
# -I search path and tracks .glsl body dependencies via glslc -MD depfiles, which cbuild's
# plain invocation does neither of. .glslc_flags (already present, for cbuild) keeps that
# path working too.
include(${GFXCOOPA_DIR}/cmake/GfxShaders.cmake)
gfx_add_shader_target(uicoopa_shaders SHADER_DIR "${CMAKE_SOURCE_DIR}/assets/shaders")

# --- Add executables ---
# `uicoopa` is the headless suite ctest runs below — test.cpp, not test_window.cpp,
# so `ctest`/CI never needs a display.
add_executable(uicoopa test.cpp)
target_link_libraries(uicoopa PRIVATE coopa::gfx ${UICOOPA_AUDIO_LIBS})
add_dependencies(uicoopa uicoopa_shaders)

# Windowed interactive demo (RectTransform anchors, layout groups, Button) — separate
# from the headless `uicoopa` test target so `ctest`/CI never needs a display.
add_executable(uicoopa_test_window test_window.cpp)
target_link_libraries(uicoopa_test_window PRIVATE coopa::gfx)
add_dependencies(uicoopa_test_window uicoopa_shaders)

# Windowed demo: the settings_demo-shaped UI built entirely through UIBuilder
# (no scene YAML except the theme file) -- see test_settings_builder.cpp.
add_executable(uicoopa_settings_builder test_settings_builder.cpp)
target_link_libraries(uicoopa_settings_builder PRIVATE coopa::gfx ${UICOOPA_AUDIO_LIBS})
add_dependencies(uicoopa_settings_builder uicoopa_shaders)

# Windowed demo dedicated to UIBuilder::dialog()/split_rows()/split_columns()/
# tab_view() -- see test_dialog_builder.cpp.
add_executable(uicoopa_dialog_builder test_dialog_builder.cpp)
target_link_libraries(uicoopa_dialog_builder PRIVATE coopa::gfx ${UICOOPA_AUDIO_LIBS})
add_dependencies(uicoopa_dialog_builder uicoopa_shaders)

# Windowed demo for gamepad-style navigation (InputMode::Hybrid) -- see
# test_gamepad_builder.cpp.
add_executable(uicoopa_gamepad_builder test_gamepad_builder.cpp)
target_link_libraries(uicoopa_gamepad_builder PRIVATE coopa::gfx ${UICOOPA_AUDIO_LIBS})
add_dependencies(uicoopa_gamepad_builder uicoopa_shaders)

# Windowed demo: a gameplay HUD bound to coopa::item / coopa::stat models --
# see test_hud_builder.cpp.
add_executable(uicoopa_hud_builder test_hud_builder.cpp)
target_link_libraries(uicoopa_hud_builder PRIVATE coopa::gfx ${UICOOPA_AUDIO_LIBS})
add_dependencies(uicoopa_hud_builder uicoopa_shaders)

enable_testing()
add_test(NAME uicoopa_tests COMMAND uicoopa)

# --- Vulkan/GLFW leak gate ---
# uicoopa names zero Vk*/VK_*/vk*/GLFW*/glfw* symbols outside its documented
# gfx-allow-vulkan escapes -- each windowed demo's own save_screenshot()'s
# ad-hoc capture framebuffer (gfxcoopa has no Framebuffer wrapper). ON by default: this
# repo is done, and the gate's job is making sure it can't regress.
option(GFX_LEAK_CHECK "Fail the build if consumer source names raw Vulkan/GLFW symbols" ON)
if(GFX_LEAK_CHECK)
    add_custom_target(uicoopa_leak_check ALL
        COMMAND ${GFXCOOPA_DIR}/tools/check_no_vulkan.sh ${CMAKE_SOURCE_DIR})
    add_dependencies(uicoopa uicoopa_leak_check)
    add_dependencies(uicoopa_test_window uicoopa_leak_check)
    add_dependencies(uicoopa_settings_builder uicoopa_leak_check)
    add_dependencies(uicoopa_dialog_builder uicoopa_leak_check)
    add_dependencies(uicoopa_gamepad_builder uicoopa_leak_check)
    add_dependencies(uicoopa_hud_builder uicoopa_leak_check)
    add_test(NAME uicoopa_no_vulkan COMMAND ${GFXCOOPA_DIR}/tools/check_no_vulkan.sh ${CMAKE_SOURCE_DIR})
endif()
