/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */


/**
 * Syscall tracing utility file based on linux bpftools package
 * Adds handlers for entering and leaving syscalls.
 * Records syscalls in a ringbuffer.
 *
**/

// NOLINTBEGIN

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "";

/**
 * Syscall Event
 *
 */
struct evt {
    __u32 tid;
    __u32 id;
    __u8  type;   // 0 = enter, 1 = exit, 2=switch_out, 3=switch_in
};

/**
 * Ringbuffer mapping
**/

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24);
} rb SEC(".maps");

// 1-element array map holding the TGID to ignore
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u32);
} ignore_tgid_map SEC(".maps");

/**
 * Task ID ingoring handler
 */
static __always_inline bool should_ignore(void) {
    __u32 key = 0;
    __u32 *ignore = bpf_map_lookup_elem(&ignore_tgid_map, &key);
    if (!ignore || *ignore == 0)
        return false;

    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u32 tgid = pid_tgid >> 32;
    return tgid == *ignore;
}

/**
 * Enter tracepoint function
 */
SEC("tracepoint/raw_syscalls/sys_enter")
int tp_sys_enter(struct trace_event_raw_sys_enter *ctx) {
    if (should_ignore()) {
        return 0;
    }

    struct evt *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e) {
        return 0;
    }

    e->tid  = (__u32) bpf_get_current_pid_tgid();
    e->id   = (__u32) ctx->id;
    e->type = 0;

    bpf_ringbuf_submit(e, 0);
    return 0;
}

SEC("tracepoint/sched/sched_switch")
int tp_sched_switch(struct trace_event_raw_sched_switch *ctx) {
    // ctx->prev_pid  : TID being switched out
    // ctx->next_pid  : TID being switched in

    // Emit SWITCH_OUT for prev
    struct evt *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (e) {
        e->tid  = (__u32) ctx->prev_pid;
        e->id   = 0;
        e->type = 2;  // switch_out
        bpf_ringbuf_submit(e, 0);
    }

    // Emit SWITCH_IN for next
    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (e) {
        e->tid  = (__u32) ctx->next_pid;
        e->id   = 0;
        e->type = 3;  // switch_in
        bpf_ringbuf_submit(e, 0);
    }

    return 0;
}

/**
 * Leave tracepoint function
 */
SEC("tracepoint/raw_syscalls/sys_exit")
int tp_sys_exit(struct trace_event_raw_sys_exit *ctx) {
    if (should_ignore()) {
        return 0;
    }

    struct evt *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e) {
        return 0;
    }

    e->tid  = (__u32) bpf_get_current_pid_tgid();
    e->id   = (__u32) ctx->id;
    e->type = 1;

    bpf_ringbuf_submit(e, 0);
    return 0;
}

// NOLINTEND
