cmake_minimum_required(VERSION 3.16)
project(spear VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)

# Should we use the external version of the JSON lib or use the locally installed version
option(USE_EXTERNAL_JSON "Use external JSON lib" OFF)


# ========= LLVM DANGER ZONE =========
# ===== PLEASE EDIT WITH CAUTION =====

find_package(LLVM REQUIRED CONFIG)
find_package(phasar REQUIRED)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# LLVM compile defs
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${LLVM_INCLUDE_PATH})

# LLVM components
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
llvm_map_components_to_libnames(llvm_libs support core irreader passes)

# ========= LLVM DANGER ZONE =========


# Bring in external third-party deps
add_subdirectory(external)


# Collect spear source files
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


# Build Spear library
add_library(SpearLib
    ${SPEAR_HEADERS}
    ${SPEAR_SOURCES}
)

# Link JSON + LLVM to SpearLib
target_link_libraries(SpearLib
    PRIVATE
        LLVM
        nlohmann_json
        phasar
)

target_include_directories(SpearLib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/spear
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/passes
)



# Spear executable
add_executable(spear main.cpp)

target_link_libraries(spear
    PRIVATE
        SpearLib
        LLVM
        nlohmann_json
        phasar
)

enable_testing()
