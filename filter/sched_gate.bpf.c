// sched_gate.bpf.c
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

struct evt {
    __u32 pid;   // TGID (process id)
    __u8  type;  // 2=switch_out, 3=switch_in
};

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24);
} rb SEC(".maps");

// 1-element array: key=0 -> target tgid
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u32);
} target_tgid_map SEC(".maps");

static __always_inline __u32 target_tgid(void) {
    __u32 key = 0;
    __u32 *val = bpf_map_lookup_elem(&target_tgid_map, &key);
    return val ? *val : 0;
}

SEC("tracepoint/sched/sched_switch")
int tp_sched_switch(struct trace_event_raw_sched_switch *ctx)
{
    __u32 tgt = target_tgid();
    if (!tgt) return 0;

    // ctx->prev_pid / next_pid are TIDs; for single-threaded benchmarks PID==TID.
    // If your benchmark is multi-threaded, you probably want TGID filtering:
    // you'd need to read task->tgid (more work). For common microbench binaries,
    // single-thread is typical.
    __u32 prev = (__u32)ctx->prev_pid;
    __u32 next = (__u32)ctx->next_pid;

    if (prev == tgt) {
        struct evt *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
        if (e) {
            e->pid = prev;
            e->type = 2; // switch_out
            bpf_ringbuf_submit(e, 0);
        }
    }

    if (next == tgt) {
        struct evt *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
        if (e) {
            e->pid = next;
            e->type = 3; // switch_in
            bpf_ringbuf_submit(e, 0);
        }
    }

    return 0;
}
