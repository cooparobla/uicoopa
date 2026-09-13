/**
 * @file ui_impl.cpp
 * @brief The single translation unit that compiles stb_truetype's implementation
 * block for the entire dependency graph.
 *
 * stb_truetype is a single-header library whose body must be compiled in EXACTLY
 * ONE translation unit per final binary. uicoopa used to arrange that with a
 * repo-wide `add_definitions(-DSTB_TRUETYPE_IMPLEMENTATION)`, which worked only
 * because every target in this repo is a single-TU executable -- and which
 * reached nothing at all once the repo became add_subdirectory()-able, since
 * add_definitions() is directory-scoped and never propagates to a parent
 * project. A consumer that included <uicoopa/text/font_atlas.h> (reached from
 * ui_yaml.h via text/font.h) therefore failed to link on every stbtt_* symbol.
 *
 * The `uicoopa_impl` STATIC target owns it once now, exactly mirroring
 * gfxcoopa's own src/gfx_impl.cpp, and consumers link `coopa::ui` (an INTERFACE
 * target depending on this one) without ever naming the macro themselves.
 *
 * The macro is supplied via target_compile_definitions() on `uicoopa_impl` in
 * CMakeLists.txt, not defined here, so this file stays a pure list of includes.
 */

#include <stb/stb_truetype.h>
