/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "profilers/SyscallProfiler.h"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

#include "ConfigParser.h"
#include "syscalls/generated_syscall_names.h"


std::unordered_map<uint32_t, Inflight> SyscallProfiler::inflight{};
std::vector<double> SyscallProfiler::energy_per_syscall(SyscallProfiler::MAX_SYSCALL, 0.0);
std::vector<uint64_t> SyscallProfiler::count_per_syscall(SyscallProfiler::MAX_SYSCALL, 0);
RegisterReader SyscallProfiler::raplReader{0};

SyscallProfiler::SyscallProfiler() : Profiler("SYSCALL") {}

/**
 * TGID ignore mapper to handle syscalls from this tool. Otherwise, we would cause an infinite loop of syscalls.
 * @param skel BPF Skeleton
 * @param tgid_to_ignore Task ID to ignore
 * @return 0 on success, -1 on failure
 */
int SyscallProfiler::set_ignore_tgid_map(syscall_trace_bpf* skel, uint32_t tgid_to_ignore) {
    // ignore_tgid_map is a 1-element BPF_MAP_TYPE_ARRAY with key=0 -> value=tgid
    uint32_t key = 0;
    uint32_t val = tgid_to_ignore;

    int map_fd = bpf_map__fd(skel->maps.ignore_tgid_map);
    if (map_fd < 0) {
        std::fprintf(stderr, "failed to get ignore_tgid_map fd\n");
        return -1;
    }

    if (bpf_map_update_elem(map_fd, &key, &val, BPF_ANY) != 0) {
        std::fprintf(stderr,
                     "bpf_map_update_elem(ignore_tgid_map) failed: %s\n",
                     std::strerror(errno));
        return -1;
    }

    return 0;
}

/**
 * Helper: add energy delta for a running segment and stop the segment.
 * Safe if called only when inf.running == true.
 */
void SyscallProfiler::stop_segment_and_accumulate(Inflight& inf) {
    const double endEng = raplReader.getEnergy();
    const double dE = endEng - inf.start_energy;

    if (inf.syscall_id < MAX_SYSCALL) {
        energy_per_syscall.at(inf.syscall_id) += dE;
    }

    inf.running = false;
}

/**
 * Helper: start a new running segment
 * (after sys_enter or after switch_in while still in syscall)
 */
void SyscallProfiler::start_segment(Inflight& inf) {
    inf.start_energy = raplReader.getEnergy();
    inf.running = true;
}

/**
 * Event handler that will be called for syscall enter/exit and sched_switch gating
 * @param ctx Unused callback context
 * @param data Data inserted into the ring buffer
 * @param data_sz Size of the corresponding data
 * @return Returns 0
 */
int SyscallProfiler::handle_event(void* /* ctx */, void* data, size_t data_sz) {
    if (data_sz < sizeof(evt)) {
        return 0;
    }

    const auto* e = static_cast<const evt*>(data);

    // Ensure we have a state entry for this TID (also for switch events)
    auto& inf = inflight[e->tid];  // default-constructs if missing

    switch (e->type) {
        case 0: {  // sys_enter
            if (e->id >= MAX_SYSCALL) {
                inf = Inflight{};
                return 0;
            }

            inf.syscall_id = e->id;
            inf.in_syscall = true;
            inf.running = false;

            // At sys_enter we are on CPU, start measuring immediately
            start_segment(inf);
            break;
        }

        case 2: {  // switch_out (prev_tid)
            // If the thread is switched out while still inside a syscall,
            // stop measuring so we do not measure sleep time.
            if (inf.in_syscall && inf.running) {
                stop_segment_and_accumulate(inf);
            }
            break;
        }

        case 3: {  // switch_in (next_tid)
            // If the thread is scheduled back in and still inside the same syscall,
            // restart measuring for the next on-CPU segment.
            if (inf.in_syscall && !inf.running) {
                start_segment(inf);
            }
            break;
        }

        case 1: {  // sys_exit
            // Finish the last on-CPU segment
            if (inf.in_syscall && inf.running) {
                stop_segment_and_accumulate(inf);
            }

            // Count one syscall completion
            if (inf.in_syscall && inf.syscall_id < MAX_SYSCALL) {
                count_per_syscall.at(inf.syscall_id) += 1;
            }

            // Clear state for this TID to avoid stale entries
            inflight.erase(e->tid);
            break;
        }

        default:
            break;
    }

    return 0;
}

json SyscallProfiler::profile() {
    this->log("Executing SyscallProfiler");

    // Reset static state so repeated runs do not accumulate old data
    inflight.clear();
    std::fill(energy_per_syscall.begin(), energy_per_syscall.end(), 0.0);
    std::fill(count_per_syscall.begin(), count_per_syscall.end(), 0);

    json syscalls;

    // Define bpf skeleton and parameters
    libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

    std::unique_ptr<syscall_trace_bpf, SkelDeleter> skel(syscall_trace_bpf__open());
    if (!skel) {
        throw std::runtime_error("failed to open syscall trace bpf");
    }

    if (syscall_trace_bpf__load(skel.get())) {
        throw std::runtime_error("failed to load syscall trace bpf");
    }

    // Ignore our own process so RaplReader's open/read/close syscalls
    // do not generate events and cause feedback loops.
    if (set_ignore_tgid_map(skel.get(), static_cast<uint32_t>(getpid())) != 0) {
        throw std::runtime_error("failed to configure ignore_tgid_map");
    }

    // Attach the BPF program to the kernel tracepoints
    if (syscall_trace_bpf__attach(skel.get())) {
        throw std::runtime_error("failed to attach syscall trace bpf");
    }

    std::unique_ptr<ring_buffer, RingBufDeleter> eventRingBuffer(
        ring_buffer__new(bpf_map__fd(skel->maps.rb), handle_event, nullptr, nullptr));

    if (!eventRingBuffer) {
        throw std::runtime_error("failed to create ring buffer");
    }

    this->log("Successfully attached syscall trace bpf");
    this->log("Starting SyscallProfiler");

    auto syscallconfig = ConfigParser::getProfilingConfiguration().syscallconfig;

    // Run for x seconds
    int seconds = syscallconfig.runtime;

    this->log("Running for " + std::to_string(seconds) + " seconds...");
    this->log("Expecting to find up to " + std::to_string(syscallconfig.maxSyscallId) + " syscall IDs...");

    using clock = std::chrono::steady_clock;

    const auto start = clock::now();
    const auto end = start + std::chrono::seconds(seconds);

    while (true) {
        const auto now = clock::now();
        if (now >= end) {
            break;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(end - now).count();
        const int timeout_ms = static_cast<int>(std::max<int64_t>(0, remaining));

        int err = ring_buffer__poll(eventRingBuffer.get(), timeout_ms);

        if (err == -EINTR) {
            continue;
        }

        if (err < 0) {
            std::fprintf(stderr, "poll error: %d\n", err);
            break;
        }
    }

    for (size_t i = 0; i < energy_per_syscall.size(); ++i) {
        if (count_per_syscall[i] > 0) {
            if (energy_per_syscall.at(i) > 0.0) {
                syscalls[getSyscallName(i)] = energy_per_syscall.at(i) / count_per_syscall.at(i);
            } else {
                // If we have count > 0 but no energy, we are likely measuring very short syscalls that are below the
                // resolution of our measurement. In this case, we can still report the default energy as a lower bound,
                // which is better than reporting 0.
                syscalls[getSyscallName(i)] = syscallconfig.defaultEnergy;
            }
        }
    }

    this->log("Finishing SyscallProfiler");

    return syscalls;
}
