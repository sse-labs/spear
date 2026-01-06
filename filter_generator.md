Commands to genereate the filter files
```
bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h
```

```
clang -O2 -g -target bpf -c sched_gate.bpf.c -o sched_gate.bpf.o
```

```
bpftool gen skeleton sched_gate.bpf.o > sched_gate.skel.h
```