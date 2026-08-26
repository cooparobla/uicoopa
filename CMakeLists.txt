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

# --- Add executables ---
# `uicoopa` is the headless suite ctest runs below — test.cpp, not test_window.cpp,
# so `ctest`/CI never needs a display.
add_executable(uicoopa test.cpp)
target_link_libraries(uicoopa PRIVATE coopa::gfx)

# Windowed interactive demo (RectTransform anchors, layout groups, Button) — separate
# from the headless `uicoopa` test target so `ctest`/CI never needs a display.
add_executable(uicoopa_test_window test_window.cpp)
target_link_libraries(uicoopa_test_window PRIVATE coopa::gfx)

enable_testing()
add_test(NAME uicoopa_tests COMMAND uicoopa)

# --- Vulkan/GLFW leak gate ---
# uicoopa names zero Vk*/VK_*/vk*/GLFW*/glfw* symbols outside its one
# documented gfx-allow-vulkan escape (test_window.cpp's ad-hoc capture
# framebuffer -- gfxcoopa has no Framebuffer wrapper). ON by default: this
# repo is done, and the gate's job is making sure it can't regress.
option(GFX_LEAK_CHECK "Fail the build if consumer source names raw Vulkan/GLFW symbols" ON)
if(GFX_LEAK_CHECK)
    add_custom_target(uicoopa_leak_check ALL
        COMMAND ${GFXCOOPA_DIR}/tools/check_no_vulkan.sh ${CMAKE_SOURCE_DIR})
    add_dependencies(uicoopa uicoopa_leak_check)
    add_dependencies(uicoopa_test_window uicoopa_leak_check)
    add_test(NAME uicoopa_no_vulkan COMMAND ${GFXCOOPA_DIR}/tools/check_no_vulkan.sh ${CMAKE_SOURCE_DIR})
endif()
