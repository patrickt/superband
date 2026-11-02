cmake_minimum_required(VERSION 3.16)
project(superband LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# SuperCollider plugin headers
include(FetchContent)
set(SC_TAG "3.13.0" CACHE STRING "SuperCollider tag to fetch headers from")
FetchContent_Declare(
    supercollider
    GIT_REPOSITORY https://github.com/supercollider/supercollider.git
    GIT_TAG ${SC_TAG}
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(supercollider)

set(SC_INCLUDE_DIR "${supercollider_SOURCE_DIR}/include" CACHE PATH "SuperCollider include dir")

# rubberband
find_package(PkgConfig REQUIRED)
pkg_check_modules(RUBBERBAND REQUIRED rubberband)

add_library(superband MODULE src/SuperBand.cpp)
target_include_directories(superband PRIVATE
    ${SC_INCLUDE_DIR}
    ${RUBBERBAND_INCLUDE_DIRS}
)
target_link_libraries(superband PRIVATE ${RUBBERBAND_LIBRARIES})
target_compile_options(superband PRIVATE ${RUBBERBAND_CFLAGS_OTHER})

if(APPLE)
    set(PLUGIN_EXT ".scx")
else()
    set(PLUGIN_EXT ".so")
endif()

set_target_properties(superband PROPERTIES
    PREFIX ""
    SUFFIX ${PLUGIN_EXT}
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

install(TARGETS superband DESTINATION lib/SuperCollider/plugins)