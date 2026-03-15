/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_PROFILERS_SYSCALLPROFILER_H_
#define SRC_SPEAR_PROFILERS_SYSCALLPROFILER_H_

#include <bpf/libbpf.h>

#include <unordered_map>
#include <vector>

#include "Profiler.h"
#include "RegisterReader.h"
#include "bpf/syscall_trace.skel.h"

// Custom deleters so we don't forget cleanup
struct RingBufDeleter {
    void operator()(ring_buffer* rb) const {
        ring_buffer__free(rb);
    }
};

struct SkelDeleter {
    void operator()(syscall_trace_bpf* skel) const {
        syscall_trace_bpf__destroy(skel);
    }
};

/**
 * Event struct to handle syscall events
 */
struct evt {
    uint32_t tid;
    uint32_t id;   // syscall id for enter/exit, unused for switch
    uint8_t  type;  // 0 enter, 1 exit, 2 switch_out, 3 switch_in
};

/**
 * Per-TID inflight state (supports sched_switch gating)
 */
struct Inflight {
    uint32_t syscall_id = 0;
    double start_energy = 0.0;  // valid only while "running" segment is active
    bool in_syscall = false;    // between sys_enter and sys_exit
    bool running = false;       // currently on CPU segment we are measuring
};

/**
 * Component to gather system specific information based on the profiler architecture
 */
class SyscallProfiler : public Profiler {
 public:
    static constexpr uint32_t MAX_SYSCALL = 462;

    // Data structures to save recorded values
    static std::unordered_map<uint32_t, Inflight> inflight;
    static std::vector<double> energy_per_syscall;
    static std::vector<uint64_t> count_per_syscall;

    /**
     * Generic constructor
     */
    SyscallProfiler();

    static int set_ignore_tgid_map(syscall_trace_bpf* skel, uint32_t tgid_to_ignore);
    static int handle_event(void* ctx, void* data, size_t data_sz);

    /**
     * Gather information about syscalls and return them as JSON object
     * @return JSON object containing per-syscall energy statistics
     */
    json profile() override;

 private:
    static RegisterReader raplReader;

    static void stop_segment_and_accumulate(Inflight& inf);
    static void start_segment(Inflight& inf);
};

#endif  // SRC_SPEAR_PROFILERS_SYSCALLPROFILER_H_
