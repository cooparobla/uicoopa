# CMake generated Testfile for 
# Source directory: /Users/tblaney/Code/toyengine/libs/gfxcoopa
# Build directory: /Users/tblaney/Code/toyengine/libs/uicoopa/build-tests/gfxcoopa-build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("gfxcoopa_render" "/Users/tblaney/Code/toyengine/libs/uicoopa/build-tests/gfxcoopa-build/gfxcoopa")
set_tests_properties("gfxcoopa_render" PROPERTIES  WORKING_DIRECTORY "/Users/tblaney/Code/toyengine/libs/gfxcoopa" _BACKTRACE_TRIPLES "/Users/tblaney/Code/toyengine/libs/gfxcoopa/CMakeLists.txt;131;add_test;/Users/tblaney/Code/toyengine/libs/gfxcoopa/CMakeLists.txt;0;")
subdirs("libcoopa-build")
subdirs("../_deps/meshoptimizer-build")
