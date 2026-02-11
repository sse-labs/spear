cmake_minimum_required(VERSION 3.16)
project(spear VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(USE_EXTERNAL_JSON "Use external JSON lib" OFF)

# ========= LLVM DANGER ZONE =========
find_package(LLVM REQUIRED CONFIG)
find_package(phasar REQUIRED)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${LLVM_INCLUDE_PATH})

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
llvm_map_components_to_libnames(llvm_libs support core irreader passes)
# ========= LLVM DANGER ZONE =========

add_subdirectory(external)

set(Z3_TARGET "")

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(Z3 QUIET IMPORTED_TARGET z3)
    if(TARGET PkgConfig::Z3)
        message(STATUS "Found Z3 via pkg-config")
        set(Z3_TARGET PkgConfig::Z3)
    endif()
endif()

if(Z3_TARGET STREQUAL "")
    # Manual fallback
    find_path(Z3_INCLUDE_DIR
            NAMES z3++.h z3.h
            PATHS /usr/include /usr/local/include
    )
    find_library(Z3_LIBRARY
            NAMES z3
            PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/local/lib64 /usr/lib64
    )

    if(NOT Z3_INCLUDE_DIR OR NOT Z3_LIBRARY)
        message(FATAL_ERROR
                "Z3 not found.\n"
                "Install it (e.g. z3 + z3-dev) or provide paths:\n"
                "  -DZ3_INCLUDE_DIR=/path/to/include -DZ3_LIBRARY=/path/to/libz3.so\n"
        )
    endif()

    message(STATUS "Found Z3 include: ${Z3_INCLUDE_DIR}")
    message(STATUS "Found Z3 library: ${Z3_LIBRARY}")
endif()

# -----------------------
# Sources
# -----------------------
file(GLOB_RECURSE SPEAR_HEADERS
        src/spear/*.h
        lib/spear/HLAC/*.h
        src/spear/profilers/*.h
        src/spear/analyses/*.h
)

file(GLOB_RECURSE SPEAR_SOURCES
        lib/spear/*.cpp
        lib/spear/profilers/*.cpp
        lib/spear/HLAC/*.cpp
        lib/passes/*.cpp
        src/spear/analyses/*.cpp
)

add_library(SpearLib
        ${SPEAR_HEADERS}
        ${SPEAR_SOURCES}
)

# Link deps
target_link_libraries(SpearLib
        PRIVATE
        LLVM
        nlohmann_json
        phasar
)

if(Z3_TARGET STREQUAL "")
    target_include_directories(SpearLib PRIVATE ${Z3_INCLUDE_DIR})
    target_link_libraries(SpearLib PRIVATE ${Z3_LIBRARY})
else()
    target_link_libraries(SpearLib PRIVATE ${Z3_TARGET})
endif()

target_include_directories(SpearLib
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/spear
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/passes
)

# Executable
add_executable(spear main.cpp)
target_link_libraries(spear PRIVATE SpearLib LLVM nlohmann_json phasar)

enable_testing()

# Tests
file(GLOB_RECURSE SPEAR_TEST_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/test/*/*.cpp
)

if(SPEAR_TEST_SOURCES)
    add_executable(spear_tests
            lib/test/main.cpp
            ${SPEAR_TEST_SOURCES}
    )

    target_link_libraries(spear_tests PRIVATE SpearLib LLVM nlohmann_json phasar Catch2)

    target_include_directories(spear_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/spear
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/passes
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/test
    )

    target_compile_definitions(spear_tests PRIVATE TEST_INPUT_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME spear_tests COMMAND spear_tests)
else()
    message(STATUS "No test sources found under lib/test/*/*.cpp; skipping spear_tests target.")
endif()
