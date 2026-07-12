# Comparison and scope

| Capability | MiniContainer | Docker Engine | runc |
|---|---|---|---|
| Resident daemon | No | Yes | No |
| OCI compatibility | No | Yes | Yes |
| Image registry/layers | Pinned rootfs import | Full registry and layers | External |
| Lifecycle persistence | Built in | Built in | Caller-owned |
| Cgroup v2 limits/stats | Memory, swap, CPU, PIDs | Broad | OCI-configured |
| Bridge and published ports | Built-in rtnetlink/nftables | Built-in networking | External |
| Default security | Empty caps + restrictive seccomp | Hardened defaults | OCI-configured |
| Orchestration/plugins | Intentionally absent | Large ecosystem | Intentionally absent |
| Primary purpose | Auditable educational runtime | Production platform | OCI runtime primitive |

MiniContainer does not attempt OCI compatibility, registry distribution, volumes, Compose,
multi-host networking, Kubernetes integration, or a plugin ecosystem. Its value is a small,
reviewable implementation of the kernel/runtime boundary with live operational evidence.
