# Security policy and threat model

MiniContainer is a root-operated, same-kernel Linux container runtime. It provides defense
in depth for trusted and moderately untrusted workloads, but it is not a virtual machine or
a hardened hostile multi-tenant sandbox. Do not treat a container boundary as protection
against an unknown kernel vulnerability.

## Trust boundaries

- The host kernel, root operator, installed MiniContainer binaries, state directory, and
  `/etc/minicontainer/config.json` are trusted.
- Container images, commands, arguments, environment values, and workload processes are
  untrusted.
- A bind source becomes intentionally visible only when its resolved directory is covered
  by `allowed_bind_sources`. Bind targets, image paths, and tmpfs targets remain untrusted.
- The private GCP VM and authenticated IAP control path are trusted operational surfaces;
  published demo ports are not exposed through an internet-ingress rule.

## Enforced controls

Workloads run in user, PID, mount, UTS, IPC, network, and cgroup namespaces. They default
to empty effective, permitted, inheritable, ambient, and bounding capability sets. Explicit
`--cap-add` values must use kernel-known canonical names and remain scoped to the container
user namespace. `no_new_privs` and locked securebits prevent privilege regain across exec.

The built-in seccomp filter defaults to `EPERM` and allows only the syscall surface required
by representative Alpine workloads. Namespace creation, mounting, raw sockets, ptrace,
kernel modules, BPF, perf, keyrings, reboot, and related kernel-control interfaces remain
blocked. `clone3` returns `ENOSYS` for libc fallback, while `clone` rejects namespace flags.
Custom version-1 JSON profiles can only remove syscall names from the built-in allowlist;
they cannot broaden it. Profile files must be absolute, root-owned regular files and must
not be group- or world-writable.

The root filesystem may be remounted read-only. Tmpfs targets are created beneath the
container root. Bind mounts accept allowlisted directories only, canonicalize their host
source, reject sensitive runtime/kernel paths, and resolve targets with `openat2` beneath
the container root without symlinks or magic links. Bind mounts are `nosuid,nodev` and may
be read-only. Workload file descriptors above stderr are closed before exec. Stored config
is root-only, and human-facing inspection excludes environment secrets.

## Residual risks

Kernel vulnerabilities, hardware attacks, denial of service within configured resource
limits, a malicious root operator, compromised trusted binaries/configuration, and data
explicitly exposed by an allowlisted writable bind mount remain outside the guarantee.
The syscall allowlist is intentionally small and may reject legitimate software; expand it
only with a reviewed test and threat analysis.

## Reporting a vulnerability

Report vulnerabilities privately through GitHub Security Advisories with affected commit,
reproduction steps, impact, and any proposed mitigation. Do not open a public issue with
exploit details. Maintainers will acknowledge a complete report, reproduce it privately,
coordinate a fix and release, and credit the reporter unless anonymity is requested.
