cmake_minimum_required(VERSION 3.16)
project(spear VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)

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
    filter/*.h
    src/spear/*.h
    src/spear/profilers/*.h
)

file(GLOB_RECURSE SPEAR_SOURCES
    lib/spear/*.cpp
    lib/spear/profilers/*.cpp
    lib/passes/*.cpp
)


# Build Spear library
add_library(SpearLib
    ${SPEAR_HEADERS}
    ${SPEAR_SOURCES}
)

# --- libbpf finden ---
find_path(LIBBPF_INCLUDE_DIR bpf/libbpf.h)
find_library(LIBBPF_LIBRARY bpf)

if (NOT LIBBPF_INCLUDE_DIR OR NOT LIBBPF_LIBRARY)
    message(FATAL_ERROR "libbpf not found. Install libbpf-dev (or equivalent).")
endif()

# include path für libbpf
target_include_directories(SpearLib PRIVATE ${LIBBPF_INCLUDE_DIR})

# WICHTIG: bei statischen libs müssen die Link-Dependencies am Executable ankommen.
# Deshalb PUBLIC (propagiert) oder zusätzlich beim Executable verlinken.
target_link_libraries(SpearLib PUBLIC ${LIBBPF_LIBRARY} elf z)

# Link JSON + LLVM to SpearLib
target_link_libraries(SpearLib
    PRIVATE
        LLVM
        nlohmann_json
        phasar
)

target_include_directories(SpearLib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/filter
        ${CMAKE_CURRENT_SOURCE_DIR}/src/spear
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/passes
)


# ===== eBPF / libbpf integration =====
find_program(CLANG_EXECUTABLE clang REQUIRED)
find_program(BPFTOOL_EXECUTABLE bpftool REQUIRED)

# libbpf (headers + library)
find_path(LIBBPF_INCLUDE_DIR filter/libbpf.h)
find_library(LIBBPF_LIBRARY filter)

if (NOT LIBBPF_INCLUDE_DIR OR NOT LIBBPF_LIBRARY)
    message(FATAL_ERROR "libbpf not found. Install libbpf-dev (or equivalent).")
endif()

# Where BPF sources live
set(BPF_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/filter)
set(BPF_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/bpf)
file(MAKE_DIRECTORY ${BPF_BUILD_DIR})

# vmlinux.h location (you can generate it once with bpftool btf dump ...)
set(VMLINUX_H ${BPF_SRC_DIR}/vmlinux.h)

# Build sched_gate.bpf.o
set(SCHED_GATE_BPF_C ${BPF_SRC_DIR}/sched_gate.bpf.c)
set(SCHED_GATE_BPF_O ${BPF_BUILD_DIR}/sched_gate.bpf.o)
add_custom_command(
        OUTPUT ${SCHED_GATE_BPF_O}
        COMMAND ${CLANG_EXECUTABLE}
        -O2 -g -target filter
        -D__TARGET_ARCH_x86   # <-- change to __TARGET_ARCH_arm64 on aarch64
        -I ${BPF_SRC_DIR}
        -I ${CMAKE_CURRENT_SOURCE_DIR}
        -c ${SCHED_GATE_BPF_C}
        -o ${SCHED_GATE_BPF_O}
        DEPENDS ${SCHED_GATE_BPF_C} ${VMLINUX_H}
        COMMENT "Compiling eBPF object sched_gate.bpf.o"
        VERBATIM
)

# Generate skeleton header sched_gate.skel.h
set(SCHED_GATE_SKEL_H ${BPF_BUILD_DIR}/sched_gate.skel.h)
add_custom_command(
        OUTPUT ${SCHED_GATE_SKEL_H}
        COMMAND ${BPFTOOL_EXECUTABLE} gen skeleton ${SCHED_GATE_BPF_O} > ${SCHED_GATE_SKEL_H}
        DEPENDS ${SCHED_GATE_BPF_O}
        COMMENT "Generating eBPF skeleton sched_gate.skel.h"
        VERBATIM
)

# Custom target so other targets can depend on skeleton generation
add_custom_target(bpf_skeletons ALL
        DEPENDS ${SCHED_GATE_SKEL_H}
)
# ===== end eBPF integration =====



# Spear executable
add_executable(spear main.cpp
        src/spear/profilers/SchedGatedEnergy.h)

target_link_libraries(spear
    PRIVATE
        SpearLib
        LLVM
        nlohmann_json
        phasar
)

enable_testing()
