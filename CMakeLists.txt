cmake_minimum_required(VERSION 3.16)
project(spear VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
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
set(Z3_RPATH_DIR "")

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(Z3 QUIET IMPORTED_TARGET z3)
    if(TARGET PkgConfig::Z3)
        message(STATUS "Found Z3 via pkg-config")
        set(Z3_TARGET PkgConfig::Z3)
    endif()
endif()

if(Z3_TARGET STREQUAL "")
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

    get_filename_component(Z3_RPATH_DIR "${Z3_LIBRARY}" DIRECTORY)
endif()

# -----------------------
# eBPF / bpftool artifacts
# -----------------------
find_program(BPFTOOL_EXECUTABLE bpftool)
find_program(CLANG_EXECUTABLE clang-17)
find_program(BASH_EXECUTABLE bash)

if(NOT BPFTOOL_EXECUTABLE)
    message(FATAL_ERROR "bpftool not found")
endif()

if(NOT CLANG_EXECUTABLE)
    message(FATAL_ERROR "clang not found")
endif()

if(NOT BASH_EXECUTABLE)
    message(FATAL_ERROR "bash not found")
endif()

if(NOT EXISTS "/sys/kernel/btf/vmlinux")
    message(FATAL_ERROR "/sys/kernel/btf/vmlinux not found")
endif()

set(BPF_SRC "${CMAKE_CURRENT_SOURCE_DIR}/lib/spear/bpf/syscall_trace.bpf.c")
set(BPF_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/bpf")

file(MAKE_DIRECTORY "${BPF_GEN_DIR}")

set(VMLINUX_H "${BPF_GEN_DIR}/vmlinux.h")
set(BPF_OBJ "${BPF_GEN_DIR}/syscall_trace.bpf.o")
set(BPF_SKEL_H "${BPF_GEN_DIR}/syscall_trace.skel.h")

if(NOT EXISTS "${BPF_SRC}")
    message(FATAL_ERROR "BPF source file not found: ${BPF_SRC}")
endif()

# Generate vmlinux.h
add_custom_command(
        OUTPUT "${VMLINUX_H}"
        COMMAND "${BASH_EXECUTABLE}" -c
        "${BPFTOOL_EXECUTABLE} btf dump file /sys/kernel/btf/vmlinux format c > \"${VMLINUX_H}\""
        VERBATIM
        COMMENT "Generating vmlinux.h from kernel BTF"
)

# Compile syscall_trace.bpf.c -> syscall_trace.bpf.o
add_custom_command(
        OUTPUT "${BPF_OBJ}"
        COMMAND "${CLANG_EXECUTABLE}"
        -O2
        -g
        -target bpf
        -c "${BPF_SRC}"
        -o "${BPF_OBJ}"
        -I "${BPF_GEN_DIR}"
        -I "${CMAKE_CURRENT_SOURCE_DIR}/lib/spear/bpf"
        DEPENDS "${BPF_SRC}" "${VMLINUX_H}"
        VERBATIM
        COMMENT "Compiling syscall_trace.bpf.c to BPF object"
)

# Generate syscall_trace.skel.h from BPF object
add_custom_command(
        OUTPUT "${BPF_SKEL_H}"
        COMMAND "${BASH_EXECUTABLE}" -c
        "${BPFTOOL_EXECUTABLE} gen skeleton \"${BPF_OBJ}\" > \"${BPF_SKEL_H}\""
        DEPENDS "${BPF_OBJ}"
        VERBATIM
        COMMENT "Generating syscall_trace.skel.h"
)

add_custom_target(generate_bpf_artifacts
        DEPENDS "${VMLINUX_H}" "${BPF_OBJ}" "${BPF_SKEL_H}"
)

# Generate syscall table
# Uses python script to parse syscall numbers and names from unistd_64.h and generate a header with a lookup table

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(SYSCALL_HEADER_CANDIDATES
        /usr/include/x86_64-linux-gnu/asm/unistd_64.h
        /usr/include/asm/unistd_64.h
)

set(SYSCALL_HEADER "")
foreach(candidate IN LISTS SYSCALL_HEADER_CANDIDATES)
    if(EXISTS "${candidate}")
        set(SYSCALL_HEADER "${candidate}")
        break()
    endif()
endforeach()

if(NOT SYSCALL_HEADER)
    message(FATAL_ERROR "Could not find unistd_64.h for x86_64")
endif()

set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(GENERATED_SYSCALL_HEADER "${GENERATED_DIR}/syscalls/generated_syscall_names.h")

add_custom_command(
        OUTPUT "${GENERATED_SYSCALL_HEADER}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GENERATED_DIR}"
        COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/util/syscalls/syscalltablegenerator.py"
        "${SYSCALL_HEADER}"
        "${GENERATED_SYSCALL_HEADER}"
        DEPENDS
        "${CMAKE_SOURCE_DIR}/util/syscalls/syscalltablegenerator.py"
        "${SYSCALL_HEADER}"
        COMMENT "Generating syscall name lookup table"
        VERBATIM
)

add_custom_target(generate_syscall_table DEPENDS "${GENERATED_SYSCALL_HEADER}")

# -----------------------
# Sources
# -----------------------
file(GLOB_RECURSE SPEAR_HEADERS
        src/passes/*.h
        src/spear/*.h
        lib/spear/HLAC/*.h
        src/spear/profilers/*.h
        src/spear/analyses/*.h
)

file(GLOB_RECURSE SPEAR_SOURCES
        lib/passes/*.cpp
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

add_dependencies(SpearLib generate_bpf_artifacts generate_syscall_table)

# Link deps
target_link_libraries(SpearLib
        PRIVATE
        LLVM
        nlohmann_json
        phasar
        Cgl
        Cbc
        CbcSolver
        OsiClp
        Osi
        Clp
        CoinUtils
)

if(Z3_TARGET STREQUAL "")
    target_include_directories(SpearLib PUBLIC ${Z3_INCLUDE_DIR})
    target_link_libraries(SpearLib PRIVATE ${Z3_LIBRARY})
    target_link_options(SpearLib PUBLIC "-Wl,-rpath,${Z3_RPATH_DIR}")
else()
    target_link_libraries(SpearLib PRIVATE ${Z3_TARGET})
endif()

target_include_directories(SpearLib
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/passes
        ${CMAKE_CURRENT_SOURCE_DIR}/src/spear
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/passes
        ${CMAKE_CURRENT_SOURCE_DIR}/src/spear/bpf
        ${CMAKE_CURRENT_BINARY_DIR}/generated
        /usr/include/coin
)

# Executable
add_executable(spear main.cpp)
target_link_libraries(spear PRIVATE SpearLib LLVM nlohmann_json phasar bpf elf z)

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
            ${CMAKE_CURRENT_SOURCE_DIR}/src/passes
            ${CMAKE_CURRENT_SOURCE_DIR}/src/spear
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/passes
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/test
            ${CMAKE_CURRENT_BINARY_DIR}/generated
    )

    target_compile_definitions(spear_tests PRIVATE TEST_INPUT_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME spear_tests COMMAND spear_tests)
else()
    message(STATUS "No test sources found under lib/test/*/*.cpp; skipping spear_tests target.")
endif()