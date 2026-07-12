# MiniContainer v1.0.0

MiniContainer is a daemonless Linux container runtime written in C17. This release builds
containers directly from namespaces, cgroup v2, overlayfs, pidfds, rtnetlink, nftables,
capabilities, and seccomp without Docker, containerd, or runc.

## Highlights

- Docker-shaped lifecycle: image import, create/start/run, ps/inspect, namespace-preserving
  exec, stop/kill, rotating logs, stats, remove, and garbage collection.
- Seven namespaces, subordinate UID/GID maps, PID-1 supervision, crash-safe state, and
  two-pass boot reconciliation.
- Memory/swap, CPU, and PID enforcement with cgroup-v2 accounting and OOM visibility.
- Overlay/pivot-root filesystems, read-only roots, safe binds, and bounded tmpfs mounts.
- Bridge networking, serialized IPAM, egress masquerade, and declared TCP/UDP publication.
- Empty-by-default capabilities, locked securebits, `no_new_privs`, FD closure, and a
  default-deny seccomp policy.
- Reproducible Debian package, detached debug symbols, checksums, SPDX SBOM, and an embedded
  source commit.

## Verification

The exact candidate passed GCC and Clang builds, sanitizers, Valgrind, clang-tidy, cppcheck,
CodeQL, privileged integration tests, package upgrade/rollback, reboot recovery, a 200-cycle
N2 stress gate, and a bounded 30-minute `e2-micro` soak. The private GCP deployment uses IAP
and has no external IP. See the committed
[release proof](https://github.com/fullstack-nick/minicontainer/blob/v1.0.0/docs/proofs/stage-07-v1-release/README.md)
and [benchmark JSON](https://github.com/fullstack-nick/minicontainer/blob/v1.0.0/docs/proofs/stage-07-v1-release/benchmark-n2.json).

MiniContainer is an educational and controlled-workload runtime, not a hostile multi-tenant
sandbox or an OCI-compatible replacement for Docker/runc. See `SECURITY.md` for the threat
model and residual risks.
