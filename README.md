# MiniContainer

MiniContainer is a daemonless, Docker-like Linux container runtime written in C. Development is underway under the locked architecture and live-verification contract in [PLAN.md](PLAN.md).

The live-proven v0.1 runtime provides secure digest-addressed rootfs import, a daemonless shim, seven Linux namespaces, subordinate UID/GID mapping, overlayfs plus `pivot_root`, private proc/dev/tmpfs mounts, PID-1 supervision, foreground and detached execution, numeric workload identity, sanitized environments, logs, state, signal forwarding, and deterministic cleanup. Resource limits, complete lifecycle reconciliation, bridge networking, and seccomp/capability hardening remain stage-gated and are not claimed before their GCP proof exists.

## Local build

```bash
cmake --preset dev-gcc
cmake --build --preset dev-gcc
ctest --preset dev-gcc
./build/dev-gcc/minicontainer version
./build/dev-gcc/minicontainer info
```
