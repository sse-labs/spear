//
// Created by mkrebs on 06.01.26.
//

#ifndef SPEAR_SCHEDGATEDENERGY_H
#define SPEAR_SCHEDGATEDENERGY_H

// SchedGatedEnergy.h
#pragma once

#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unistd.h>

#include "sched_gate.skel.h"   // <-- skeleton for sched_gate.bpf.o
#include "RegisterReader.h"

struct SchedEvt {
    uint32_t pid;
    uint8_t  type; // 2=switch_out, 3=switch_in
};

class SchedGatedEnergy {
public:
    SchedGatedEnergy() = default;

    RegisterReader powReader {0};

    bool start(uint32_t target_pid) {
        target_pid_ = target_pid;
        running_ = false;
        energy_sum_ = 0.0;

        libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

        skel_.reset(sched_gate_bpf__open());
        if (!skel_) {
            std::fprintf(stderr, "sched_gate: failed to open skel\n");
            return false;
        }

        if (sched_gate_bpf__load(skel_.get())) {
            std::fprintf(stderr, "sched_gate: failed to load bpf\n");
            return false;
        }

        if (!set_target_pid_map(target_pid_)) return false;

        if (sched_gate_bpf__attach(skel_.get())) {
            std::fprintf(stderr, "sched_gate: failed to attach bpf\n");
            return false;
        }

        rb_.reset(ring_buffer__new(bpf_map__fd(skel_->maps.rb), &SchedGatedEnergy::on_event_thunk, this, nullptr));
        if (!rb_) {
            std::fprintf(stderr, "sched_gate: ring_buffer__new failed\n");
            return false;
        }

        return true;
    }

    // Poll ringbuf; call this in a loop while benchmark is running
    void poll(int timeout_ms = 10) {
        if (!rb_) return;
        int err = ring_buffer__poll(rb_.get(), timeout_ms);
        if (err < 0 && err != -EINTR) {
            std::fprintf(stderr, "sched_gate: poll error: %d\n", err);
        }
    }

    // Call after benchmark ended; it will close an “open segment” if still running
    double stop_and_get_energy() {
        // If the target is currently running and we never saw a switch_out before stopping,
        // close the segment now.
        if (running_) {
            const double endE = powReader.getEnergy();
            energy_sum_ += (endE - start_energy_);
            running_ = false;
        }
        return energy_sum_;
    }

private:
    struct RingBufDeleter {
        void operator()(ring_buffer* rb) const { ring_buffer__free(rb); }
    };
    struct SkelDeleter {
        void operator()(sched_gate_bpf* skel) const { sched_gate_bpf__destroy(skel); }
    };

    std::unique_ptr<sched_gate_bpf, SkelDeleter> skel_{nullptr};
    std::unique_ptr<ring_buffer, RingBufDeleter> rb_{nullptr};

    uint32_t target_pid_{0};
    bool running_{false};
    double start_energy_{0.0};
    double energy_sum_{0.0};

    bool set_target_pid_map(uint32_t pid) {
        uint32_t key = 0;
        uint32_t val = pid;

        int map_fd = bpf_map__fd(skel_->maps.target_tgid_map);
        if (map_fd < 0) {
            std::fprintf(stderr, "sched_gate: failed to get target_tgid_map fd\n");
            return false;
        }

        if (bpf_map_update_elem(map_fd, &key, &val, BPF_ANY) != 0) {
            std::fprintf(stderr, "sched_gate: bpf_map_update_elem failed: %s\n", std::strerror(errno));
            return false;
        }
        return true;
    }

    static int on_event_thunk(void* ctx, void* data, size_t size) {
        return static_cast<SchedGatedEnergy*>(ctx)->on_event(data, size);
    }

    int on_event(void* data, size_t size) {
        RegisterReader powReader(0);
        if (size < sizeof(SchedEvt)) return 0;
        const auto* e = static_cast<const SchedEvt*>(data);

        if (e->pid != target_pid_) return 0;

        if (e->type == 3) { // switch_in
            if (!running_) {
                //std::fprintf(stderr, "SWITCHIN\n");
                start_energy_ = powReader.getEnergy();
                running_ = true;
            }
        } else if (e->type == 2) { // switch_out
            if (running_) {
                //std::fprintf(stderr, "SWITCHOUT\n");
                const double endE = powReader.getEnergy();
                double delta = (endE - start_energy_);
                energy_sum_ += delta;

                running_ = false;
            }
        }
        return 0;
    }
};


#endif //SPEAR_SCHEDGATEDENERGY_H