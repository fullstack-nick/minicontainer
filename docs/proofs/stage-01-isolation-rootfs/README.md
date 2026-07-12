# Stage 1 — Isolation and rootfs live proof

Date: 2026-07-12  
Implementation commit: `8ee8ccb3262cee7b0e5b38998ebc91e4e9a6075e`  
Artifact SHA-256: `e2a013feeae566414bf4feca7e969fccafa03b9f3129a80bffcab1946e9ba8ab`  
Image SHA-256: `41f73e3cf5fa919b8aa5ca6b30dc48f0da2720776d7423e2a7748211456fe081`  
VM: `minicontainer-vm`, `us-west1-a`, `e2-micro`, no external IPv4

## Verdict

**PASS.** Secure Alpine import, daemonless shim launch, synchronized `clone3`, user/PID/mount/UTS/IPC/network/cgroup namespaces, subordinate identity mapping, overlay root, `pivot_root`, hardened proc/dev/tmpfs mounts, PID-1 supervision, foreground and detached execution, environment/workdir/user controls, exact exit and signal propagation, logs, state, and cleanup were proven locally, in CI, and on GCP from the exact packaged commit.

Container and host namespace inode sets differed for every required namespace. The container saw only loopback, workload PID 2 with parent PID 1, UID 0 mapped to host subordinate range `200000:65536`, and no host-only environment. Post-run inspection found zero residual mounts, running shims, or MiniContainer host interfaces.

Proof files redact credentials, billing identifiers, SSH material, workstation paths, personal addresses, and transient IAP addresses.

