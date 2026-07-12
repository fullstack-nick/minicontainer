# Syscall walkthrough

This is the high-level kernel path for `minicontainer run --image alpine -- /bin/true`.

1. `flock`, `openat`, `fsync`, and `rename` serialize and persist configuration.
2. A systemd D-Bus transient scope establishes the cgroup-v2 ownership boundary.
3. `clone3` creates user, PID, mount, UTS, IPC, network, and cgroup namespaces and returns a
   pidfd. The parent writes `/proc/PID/{uid_map,gid_map}` and moves the child to the payload.
4. Rtnetlink messages create and move the veth peer; nftables receives an atomic batch over
   fixed `execve` arguments and stdin.
5. `mount` makes propagation private, assembles overlayfs and private proc/dev/tmpfs mounts,
   then `pivot_root` and `umount2` remove access to the old host view.
6. The init supervisor forks. `setgroups`, `setresgid`, `setresuid`, `capset`, `prctl`, and
   `seccomp` establish workload identity and policy. `close_range` removes inherited FDs.
7. `execve` starts the command. PID 1 waits and forwards signals to its process group.
8. The shim records exit status atomically and removes cgroup, network, mount, socket, and
   runtime resources in reverse order.

Dangerous workload syscalls such as `unshare`, `mount`, `ptrace`, `bpf`, raw sockets, module
operations, reboot, and keyring control fall through to the seccomp default `EPERM` action.
`clone3` returns `ENOSYS` so libc can use a mask-checked `clone` without namespace flags.
