cmake_minimum_required(VERSION 3.16)
project(superband VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(SUPERBAND_BUILD_TESTS "Build the offline engine test harness" ON)

set(SC_TAG "Version-3.14.1" CACHE STRING
    "SuperCollider release tag to fetch plugin headers from when none are installed")

# ---------------------------------------------------------------------------
# SuperCollider plugin headers
# ---------------------------------------------------------------------------
set(SC_INCLUDE_DIR "" CACHE PATH
    "SuperCollider include tree (a directory containing plugin_interface/, common/, server/)")

if(NOT SC_INCLUDE_DIR)
    set(_sc_candidates
        "$ENV{SC_HEADERS}"
        /usr/include/SuperCollider
        /usr/local/include/SuperCollider
        /opt/homebrew/include/SuperCollider
        /opt/local/include/SuperCollider)
    foreach(_cand IN LISTS _sc_candidates)
        if(_cand AND EXISTS "${_cand}/plugin_interface/SC_PlugIn.hpp")
            set(SC_INCLUDE_DIR "${_cand}")
            break()
        endif()
    endforeach()
endif()

if(NOT SC_INCLUDE_DIR)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ScHeaders.cmake)
    superband_fetch_sc_headers("${SC_TAG}" "${CMAKE_BINARY_DIR}/sc_headers" SC_INCLUDE_DIR)
    set(SC_INCLUDE_DIR "${SC_INCLUDE_DIR}" CACHE PATH "SuperCollider include tree" FORCE)
endif()

set(SC_INCLUDE_DIRS "${SC_INCLUDE_DIR}/plugin_interface"
                    "${SC_INCLUDE_DIR}/common"
                    "${SC_INCLUDE_DIR}/server")
foreach(_dir IN LISTS SC_INCLUDE_DIRS)
    if(NOT EXISTS "${_dir}")
        message(FATAL_ERROR "SuperCollider headers not found in ${_dir}")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# RubberBand
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
# RubberBand 3.0 added getStartDelay()/getPreferredStartPad(), which the engine
# uses to time-align the output.
pkg_check_modules(RUBBERBAND REQUIRED IMPORTED_TARGET "rubberband>=3.0.0")

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
add_library(superband MODULE
    src/SuperBand.cpp
    src/SuperBandCore.cpp
)
target_include_directories(superband PRIVATE ${SC_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(superband PRIVATE PkgConfig::RUBBERBAND)

if(APPLE)
    set(PLUGIN_SUFFIX ".scx")
else()
    set(PLUGIN_SUFFIX ".so")
endif()

set_target_properties(superband PROPERTIES
    OUTPUT_NAME SuperBand
    PREFIX ""
    SUFFIX ${PLUGIN_SUFFIX}
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

# ---------------------------------------------------------------------------
# Offline test harness: drives the engine without SuperCollider
# ---------------------------------------------------------------------------
if(SUPERBAND_BUILD_TESTS)
    enable_testing()
    add_executable(superband_core_test tests/SuperBandCoreTest.cpp src/SuperBandCore.cpp)
    target_include_directories(superband_core_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(superband_core_test PRIVATE PkgConfig::RUBBERBAND)
    add_test(NAME superband_core COMMAND superband_core_test)
endif()

install(TARGETS superband DESTINATION lib/SuperCollider/plugins)
install(FILES sc/classes/SuperBand.sc DESTINATION share/SuperCollider/Extensions)
