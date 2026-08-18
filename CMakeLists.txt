cmake_minimum_required(VERSION 3.16)
cmake_policy(VERSION 3.16)

project(uicoopa_test_window)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(ROOT_DIR_PARENT "${CMAKE_SOURCE_DIR}" DIRECTORY)

# --- Vulkan ---
find_package(Vulkan REQUIRED)

# --- Window system (GLFW) ---
find_package(glfw3 REQUIRED)

# --- Sibling library paths (header-only, no build targets required) ---
set(LIBCOOPA_DIR "${ROOT_DIR_PARENT}/libcoopa")
set(GFXCOOPA_DIR "${ROOT_DIR_PARENT}/gfxcoopa")
set(CAML_DIR     "${ROOT_DIR_PARENT}/caml")

# --- Include directories ---
include_directories(${CMAKE_SOURCE_DIR}/)                  # uicoopa/
include_directories(${CMAKE_SOURCE_DIR}/includes/)         # stb_truetype, stb_image
include_directories(${LIBCOOPA_DIR}/includes/)             # fkYAML, glm, phmap
include_directories(${LIBCOOPA_DIR})                       # coopa/
include_directories(${GFXCOOPA_DIR}/includes/)             # volk, vma
include_directories(${GFXCOOPA_DIR})                       # gfxcoopa/
include_directories(${CAML_DIR}/includes/)                 # caml/ + fkYAML
include_directories(${CAML_DIR})                           # caml.h root

# --- Configure and include root directory header ---
configure_file(configuration/root_directory.h.in configuration/root_directory.h)
include_directories(${CMAKE_BINARY_DIR}/configuration)

# --- Vulkan loader (volk compiled once here) ---
add_definitions(-DVOLK_IMPLEMENTATION)

# --- VMA compiled once here ---
add_definitions(-DVMA_IMPLEMENTATION)

# --- stb_truetype / stb_image / stb_image_write: implementation compiled once here ---
add_definitions(-DSTB_TRUETYPE_IMPLEMENTATION)
add_definitions(-DSTB_IMAGE_IMPLEMENTATION)
add_definitions(-DSTB_IMAGE_WRITE_IMPLEMENTATION)

# --- GLM: use Vulkan depth convention [0, 1] instead of OpenGL [-1, 1] ---
add_definitions(-DGLM_FORCE_DEPTH_ZERO_TO_ONE)

# --- Add executables ---
# `uicoopa` is the headless suite ctest runs below — test.cpp, not test_window.cpp,
# so `ctest`/CI never needs a display.
add_executable(uicoopa test.cpp)
target_link_libraries(uicoopa
    PRIVATE
    Vulkan::Vulkan
    glfw
    dl
)

# Windowed interactive demo (RectTransform anchors, layout groups, Button) — separate
# from the headless `uicoopa` test target so `ctest`/CI never needs a display.
add_executable(uicoopa_test_window test_window.cpp)
target_link_libraries(uicoopa_test_window
    PRIVATE
    Vulkan::Vulkan
    glfw
    dl
)

enable_testing()
add_test(NAME uicoopa_tests COMMAND uicoopa)
