cmake_minimum_required(VERSION 3.16)
project(spear VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)

# Should we use the external version of the JSON lib or use the locally installed version
option(USE_EXTERNAL_JSON "Use external JSON lib" OFF)


# ========= LLVM DANGER ZONE =========
# ===== PLEASE EDIT WITH CAUTION =====

find_package(LLVM REQUIRED CONFIG)
find_package(phasar REQUIRED)
find_package(Z3 REQUIRED)

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

# -----------------------
# Catch2 test executable
# -----------------------

# Collect test sources (lib/test/*/*.cpp)
file(GLOB_RECURSE SPEAR_TEST_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/test/*/*.cpp
)

if (SPEAR_TEST_SOURCES)
    add_executable(spear_tests
            lib/test/main.cpp
            ${SPEAR_TEST_SOURCES}
    )

    target_link_libraries(spear_tests
            PRIVATE
            SpearLib
            LLVM
            nlohmann_json
            phasar
    )

    # Ensure tests see your public headers like the main targets do
    target_include_directories(spear_tests
            PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/spear
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/passes
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/test
    )

    # Catch2 linking: prefer Catch2::Catch2WithMain if available.
    target_link_libraries(spear_tests PRIVATE Catch2)

    target_compile_definitions(spear_tests
            PRIVATE
            TEST_INPUT_DIR="${CMAKE_SOURCE_DIR}"
    )

    # Register with CTest
    add_test(NAME spear_tests COMMAND spear_tests)

    # (Optional) nicer output on failure
    # set_tests_properties(spear_tests PROPERTIES
    #     FAIL_REGULAR_EXPRESSION "FAILED"
    # )

else()
    message(STATUS "No test sources found under lib/test/*/*.cpp; skipping spear_tests target.")
endif()
