cmake_minimum_required(VERSION 3.16)
cmake_policy(VERSION 3.16)

project(uicoopa_test_window)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Standalone vs consumed ---
# UICOOPA_STANDALONE is true only when this file IS the top-level project. Everything
# that belongs to "the uicoopa repo as an application" -- its six demo executables, its
# ctest registration, its generated root_directory.h, and its leak gate -- is gated on it,
# so a consumer that add_subdirectory()'s this repo gets exactly one thing: the coopa::ui
# target below. Same guard physxcoopa/CMakeLists.txt:36 and sfxcoopa/CMakeLists.txt:53
# already use.
#
# Every path below is derived from CMAKE_CURRENT_SOURCE_DIR / CMAKE_CURRENT_BINARY_DIR,
# never CMAKE_SOURCE_DIR / CMAKE_BINARY_DIR. Those two mean "the TOP-LEVEL project", so
# under add_subdirectory() they pointed this repo's own includes, shader dir and generated
# header at the consumer's tree instead of at uicoopa -- the exact bug
# libcoopa/CMakeLists.txt:11-18 documents. They are still correct to use for the
# standalone test itself, which is precisely a question about the top-level project.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(UICOOPA_STANDALONE TRUE)
else()
    set(UICOOPA_STANDALONE FALSE)
endif()

get_filename_component(ROOT_DIR_PARENT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)

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
set(UICOOPA_AUDIO_DEFS "")
if(UICOOPA_WITH_AUDIO AND EXISTS "${SFXCOOPA_DIR}/CMakeLists.txt")
    if(NOT TARGET coopa::sfx)
        add_subdirectory(${SFXCOOPA_DIR} ${CMAKE_CURRENT_BINARY_DIR}/sfxcoopa-build)
    endif()
    set(UICOOPA_AUDIO_LIBS coopa::sfx)
    set(UICOOPA_AUDIO_DEFS UICOOPA_HAS_AUDIO)
elseif(UICOOPA_WITH_AUDIO)
    message(WARNING "UICOOPA_WITH_AUDIO is ON but ${SFXCOOPA_DIR} was not found -- building without audio.")
endif()

# --- coopa::ui: the public, header-only consumable target. ---
# Named uicoopa_lib (not `uicoopa`) because that bare name is already the headless test
# executable below, and because `cplay` resolves the run binary as `./build/<project
# name>`. Mirrors coopa::gfx / coopa::lib exactly: INTERFACE, since everything under
# uicoopa/ is header-only with no .cpp files at all.
#
# The include dirs, compile definitions and linked libraries live HERE rather than in
# directory-scoped include_directories()/add_definitions() calls, because those propagate
# only downward into subdirectories -- a parent project that add_subdirectory()'s this
# repo would never see them. Carrying them on the target is what makes
# `target_link_libraries(myapp PRIVATE coopa::ui)` sufficient.
# uicoopa_impl: the one non-header-only piece -- the single TU that compiles
# stb_truetype's implementation block. See src/ui_impl.cpp for why this must be
# singular, and why the old repo-wide add_definitions() could not survive becoming
# add_subdirectory()-able. Mirrors gfxcoopa_impl exactly.
add_library(uicoopa_impl STATIC src/ui_impl.cpp)
target_compile_features(uicoopa_impl PRIVATE cxx_std_20)
target_compile_definitions(uicoopa_impl PRIVATE STB_TRUETYPE_IMPLEMENTATION)
target_include_directories(uicoopa_impl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/includes)

add_library(uicoopa_lib INTERFACE)
add_library(coopa::ui ALIAS uicoopa_lib)
target_compile_features(uicoopa_lib INTERFACE cxx_std_20)
# NOTE for consumers: includes/ here also carries stb_image.h / stb_image_write.h,
# whose *implementations* live in gfxcoopa_impl. Link coopa::ui AFTER coopa::gfx so
# gfxcoopa's copies of those two headers win the include search -- otherwise a
# consumer could compile against uicoopa's header while linking gfxcoopa's body.
# stb_truetype is unique to this repo, so it has no such ambiguity.
target_include_directories(uicoopa_lib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}                 # uicoopa/
    ${CMAKE_CURRENT_SOURCE_DIR}/includes        # stb/stb_truetype.h
    ${CAML_DIR}/includes                        # caml/ + fkYAML
    ${CAML_DIR})                                # caml.h root
# UICOOPA_HAS_AUDIO must be INTERFACE, not the old directory-scoped
# add_compile_definitions(): a consumer compiling uicoopa/audio/*.h without it would
# silently get the no-audio stubs while linking against a coopa::sfx that is present.
target_compile_definitions(uicoopa_lib INTERFACE ${UICOOPA_AUDIO_DEFS})
target_link_libraries(uicoopa_lib INTERFACE uicoopa_impl coopa::gfx ${UICOOPA_AUDIO_LIBS})

# --- Shader compilation ---
# Previously relied entirely on cbuild --vulkan (checked-in .spv, plain `glslc <file> -o
# <file>.spv`, no -I). That stops working the moment a uicoopa shader #includes
# <gfx/surface2d/...> from gfxcoopa's shared 2D quad backbone -- gfx_add_shader_target()
# (owned by gfxcoopa, see its own file doc) always adds gfxcoopa's assets/shaders/ to the
# -I search path and tracks .glsl body dependencies via glslc -MD depfiles, which cbuild's
# plain invocation does neither of. .glslc_flags (already present, for cbuild) keeps that
# path working too.
#
# Deliberately NOT gated on UICOOPA_STANDALONE: a consumer needs these .spv files at
# runtime just as much as the demos do (UiPass/UiWorldPass take .spv paths), so it
# add_dependencies() its own targets on uicoopa_shaders. The target name is already
# unique across a combined build.
include(${GFXCOOPA_DIR}/cmake/GfxShaders.cmake)
gfx_add_shader_target(uicoopa_shaders SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders")

if(UICOOPA_STANDALONE)
    # --- Configure and include root directory header ---
    # Only the demo/test .cpp files include <root_directory.h>; nothing under uicoopa/
    # does. Generating it under a consumer would put a second, conflicting ROOT_DIR macro
    # on the include path alongside the consumer's own.
    configure_file(configuration/root_directory.h.in configuration/root_directory.h)
    include_directories(${CMAKE_CURRENT_BINARY_DIR}/configuration)

    # --- Add executables ---
    # `uicoopa` is the headless suite ctest runs below — test.cpp, not test_window.cpp,
    # so `ctest`/CI never needs a display.
    add_executable(uicoopa test.cpp)
    target_link_libraries(uicoopa PRIVATE coopa::ui)
    add_dependencies(uicoopa uicoopa_shaders)

    # Windowed interactive demo (RectTransform anchors, layout groups, Button) — separate
    # from the headless `uicoopa` test target so `ctest`/CI never needs a display.
    add_executable(uicoopa_test_window test_window.cpp)
    target_link_libraries(uicoopa_test_window PRIVATE coopa::ui)
    add_dependencies(uicoopa_test_window uicoopa_shaders)

    # Windowed demo: the settings_demo-shaped UI built entirely through UIBuilder
    # (no scene YAML except the theme file) -- see test_settings_builder.cpp.
    add_executable(uicoopa_settings_builder test_settings_builder.cpp)
    target_link_libraries(uicoopa_settings_builder PRIVATE coopa::ui)
    add_dependencies(uicoopa_settings_builder uicoopa_shaders)

    # Windowed demo dedicated to UIBuilder::dialog()/split_rows()/split_columns()/
    # tab_view() -- see test_dialog_builder.cpp.
    add_executable(uicoopa_dialog_builder test_dialog_builder.cpp)
    target_link_libraries(uicoopa_dialog_builder PRIVATE coopa::ui)
    add_dependencies(uicoopa_dialog_builder uicoopa_shaders)

    # Windowed demo for gamepad-style navigation (InputMode::Hybrid) -- see
    # test_gamepad_builder.cpp.
    add_executable(uicoopa_gamepad_builder test_gamepad_builder.cpp)
    target_link_libraries(uicoopa_gamepad_builder PRIVATE coopa::ui)
    add_dependencies(uicoopa_gamepad_builder uicoopa_shaders)

    # Windowed demo: a gameplay HUD bound to coopa::item / coopa::stat models --
    # see test_hud_builder.cpp.
    add_executable(uicoopa_hud_builder test_hud_builder.cpp)
    target_link_libraries(uicoopa_hud_builder PRIVATE coopa::ui)
    add_dependencies(uicoopa_hud_builder uicoopa_shaders)

    enable_testing()
    add_test(NAME uicoopa_tests COMMAND uicoopa)

    # --- Vulkan/GLFW leak gate ---
    # uicoopa names zero Vk*/VK_*/vk*/GLFW*/glfw* symbols outside its documented
    # gfx-allow-vulkan escapes -- each windowed demo's own save_screenshot()'s
    # ad-hoc capture framebuffer (gfxcoopa has no Framebuffer wrapper). ON by default: this
    # repo is done, and the gate's job is making sure it can't regress.
    #
    # The option() itself is inside the standalone guard, not just the block it controls:
    # option() writes a CACHE entry, and consumers legitimately run the gate with a
    # different default over their own source (toyengine keeps it OFF while its render
    # pipeline still names raw Vulkan symbols). Whichever add_subdirectory ran first would
    # otherwise win the cache and silently flip the other repo's policy.
    option(GFX_LEAK_CHECK "Fail the build if consumer source names raw Vulkan/GLFW symbols" ON)
    if(GFX_LEAK_CHECK)
        add_custom_target(uicoopa_leak_check ALL
            COMMAND ${GFXCOOPA_DIR}/tools/check_no_vulkan.sh ${CMAKE_CURRENT_SOURCE_DIR})
        add_dependencies(uicoopa uicoopa_leak_check)
        add_dependencies(uicoopa_test_window uicoopa_leak_check)
        add_dependencies(uicoopa_settings_builder uicoopa_leak_check)
        add_dependencies(uicoopa_dialog_builder uicoopa_leak_check)
        add_dependencies(uicoopa_gamepad_builder uicoopa_leak_check)
        add_dependencies(uicoopa_hud_builder uicoopa_leak_check)
        add_test(NAME uicoopa_no_vulkan
                 COMMAND ${GFXCOOPA_DIR}/tools/check_no_vulkan.sh ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
endif()
