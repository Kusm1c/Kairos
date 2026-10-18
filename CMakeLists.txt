cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(Kairos VERSION 1.0.0 LANGUAGES CXX)

if (MSVC)
    foreach (kind EXE SHARED)
        string(REGEX REPLACE "/INCREMENTAL[^ ]*" "" CMAKE_${kind}_LINKER_FLAGS_RELWITHDEBINFO
               "${CMAKE_${kind}_LINKER_FLAGS_RELWITHDEBINFO}")
        string(APPEND CMAKE_${kind}_LINKER_FLAGS_RELWITHDEBINFO " /INCREMENTAL:NO /OPT:REF /OPT:ICF")
    endforeach()
endif()

# gdsolver (MIT, see gdsolver/LICENSE): the solver core, its loop and its hooks.
add_subdirectory(gdsolver/dp)
file(GLOB_RECURSE GDSOLVER_SOURCES CONFIGURE_DEPENDS gdsolver/mod/*.cpp gdsolver/solver/*.cpp)
list(REMOVE_ITEM GDSOLVER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/gdsolver/mod/dp_bridge2.cpp)

# Kairos.
file(GLOB_RECURSE KAIROS_SOURCES CONFIGURE_DEPENDS src/kairos/*.cpp src/kpsim/*.cpp)

add_library(${PROJECT_NAME} SHARED ${KAIROS_SOURCES} ${GDSOLVER_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gdsolver)
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /MP)
endif()

# A second copy of the solver core under a renamed namespace, compiled with the core's options.
add_library(gdsolver_dpcore2 STATIC gdsolver/mod/dp_bridge2.cpp)
target_include_directories(gdsolver_dpcore2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gdsolver)
target_link_libraries(gdsolver_dpcore2 PRIVATE gdsolver_dp)
set_target_properties(gdsolver_dpcore2 PROPERTIES POSITION_INDEPENDENT_CODE ON)
gdsolver_dp_options(gdsolver_dpcore2)

target_link_libraries(${PROJECT_NAME} gdsolver_dpcore gdsolver_dpcore2)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
endif()

# Bindings pinned at f050fd74 (newer ones make GJGameState non-copyable): a clone beside the
# repository, else downloaded. GEODE_BINDINGS_REPO_PATH in the environment still wins.
get_filename_component(KAIROS_BINDINGS ${CMAKE_CURRENT_SOURCE_DIR}/../Kairos-bindings ABSOLUTE)
if (EXISTS ${KAIROS_BINDINGS}/bindings)
    set(GEODE_BINDINGS_REPO_PATH ${KAIROS_BINDINGS})
else()
    include($ENV{GEODE_SDK}/cmake/CPM.cmake)
    CPMAddPackage(NAME "bindings" GITHUB_REPOSITORY "geode-sdk/bindings"
                  GIT_TAG f050fd74ad531b0827377c97510cb807903ff1b3 DOWNLOAD_ONLY YES NO_CACHE YES)
    set(GEODE_BINDINGS_REPO_PATH ${bindings_SOURCE_DIR})
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(${PROJECT_NAME})
