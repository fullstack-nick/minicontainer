# MiniContainer Planning Research

Research date: 2026-07-12  
Purpose: close architecture, platform, security, tooling, and GCP decisions before implementation  
Authority: supporting evidence for the locked choices in `PLAN.md`

## Product rationale

The Linux Foundation's tenth Open Source Jobs Report reports cloud/container skills as the most demanded category in its survey and Linux skills immediately behind them. That supports MiniContainer's recruiter-facing focus on Linux process, resource, network, filesystem, and security internals rather than another application-layer demo.

Source: [Linux Foundation — The 10th Annual Open Source Jobs Report](https://www.linuxfoundation.org/research/the-10th-annual-open-source-jobs-report)

## Linux process and namespace APIs

The Linux man-pages project confirms that:

- glibc has no `clone3()` wrapper, so the runtime must call `syscall(SYS_clone3, ...)`;
- `CLONE_PIDFD` returns a stable process reference;
- `CLONE_INTO_CGROUP` creates the child directly in a cgroups-v2 target and removes initial accounting jitter;
- user namespaces created in the same clone call are created first, giving the child capabilities over the other new namespaces;
- PID, mount, UTS, IPC, network, user, and cgroup namespace flags can be requested together under the documented restrictions;
- `pidfd_send_signal()` avoids signaling a recycled PID;
- `openat2()` resolution flags can reject paths that leave a dirfd root and can reject procfs magic links and symlinks for security-sensitive operations.

Locked result: raw `clone3` with `CLONE_PIDFD`, `CLONE_INTO_CGROUP`, all seven planned namespaces, a parent/child mapping/network barrier, pidfd signaling, and dirfd-relative `openat2` path resolution.

Sources:

- [clone(2)/clone3(2)](https://man7.org/linux/man-pages/man2/clone3.2.html)
- [pidfd_open(2)](https://man7.org/linux/man-pages/man2/pidfd_open.2.html)
- [pidfd_send_signal(2)](https://man7.org/linux/man-pages/man2/pidfd_send_signal.2.html)
- [user_namespaces(7)](https://man7.org/linux/man-pages/man7/user_namespaces.7.html)
- [namespaces(7)](https://man7.org/linux/man-pages/man7/namespaces.7.html)
- [openat2(2)](https://man7.org/linux/man-pages/man2/openat2.2.html)
- [pivot_root(2)](https://man7.org/linux/man-pages/man2/pivot_root.2.html)

## cgroups v2 and systemd ownership

The kernel cgroups-v2 documentation defines controller delegation and the no-internal-process behavior. systemd's container-manager guidance adds the single-writer rule: software must not create children under the root cgroup managed by PID 1. Delegation is supported on service and scope units, not slice units. A manager must create child cgroups only under a unit with `Delegate=yes`, move its supervisor into a leaf, then enable controllers for payload siblings.

Locked result: each shim is registered in a transient delegated systemd scope. It discovers the scope path through D-Bus, creates `supervisor` and `payload` children, and clones init directly into `payload`. There is no direct cgroup-root backend in v1.0.

Sources:

- [Linux kernel — Control Group v2](https://docs.kernel.org/admin-guide/cgroup-v2.html)
- [systemd — Control Group APIs and Delegation](https://systemd.io/CGROUP_DELEGATION/)
- [systemd — New Control Group Interfaces](https://systemd.io/CONTROL_GROUP_INTERFACE/)

## Filesystem isolation

Kernel overlayfs documentation describes lower/upper/work layers, copy-up behavior, whiteouts, permission checks, and the danger of untrusted metacopy/redirect metadata. The `pivot_root(2)` example requires private mount propagation and a mount point for the new root.

Locked result: imported image entries are securely extracted with shifted subordinate ownership and unsafe metadata stripped. The immutable image directory is the overlay lower layer; each container owns private upper/work directories. `metacopy`, redirect, index, and untrusted overlay xattrs are disabled/rejected. Setup uses recursive `MS_PRIVATE`, a bind-mounted new root, `pivot_root`, old-root detach, and controlled proc/dev/tmpfs mounts.

Sources:

- [Linux kernel — Overlay Filesystem](https://docs.kernel.org/filesystems/overlayfs.html)
- [pivot_root(2)](https://man7.org/linux/man-pages/man2/pivot_root.2.html)
- [mount_namespaces(7)](https://man7.org/linux/man-pages/man7/mount_namespaces.7.html)

## Seccomp and capabilities

The seccomp API supports synchronized filters and requires `no_new_privs` for an unprivileged filter install. Classic seccomp cannot dereference the `clone3` argument structure, so allowing `clone3` while excluding namespace flags is not sound. A mask comparison can restrict the legacy `clone` flags.

Locked result: default action `EPERM`, architecture mismatch kill, special `clone3 -> ENOSYS` fallback, mask-restricted `clone`, raw-socket denial, and allowlists separated for container init and workload. Capability sets default empty; requested namespaced capabilities are constructed before `no_new_privs` and bounding-set teardown.

Sources:

- [seccomp(2)](https://man7.org/linux/man-pages/man2/seccomp.2.html)
- [seccomp_attr_set(3)](https://man7.org/linux/man-pages/man3/seccomp_attr_set.3.html)
- [capabilities(7)](https://man7.org/linux/man-pages/man7/capabilities.7.html)

## Networking

The nftables tool accepts a ruleset from stdin with `nft -f -`, supports a validation-only pass, identifiers/comments, NAT chains, sets, and stable exit statuses. nftables batches are applied transactionally by the kernel. The runtime can own one dedicated table without touching unrelated host firewall state.

Locked result: rtnetlink configures bridge/veth/address/route objects. Validated nftables text is passed directly to `/usr/sbin/nft -f -` through pipes and `execve`, never through a shell. MiniContainer owns only `inet minicontainer` objects marked with full container IDs.

Sources:

- [Netfilter — nftables man page](https://netfilter.org/projects/nftables/manpage.html)
- [nftables internals — atomic transactions](https://wiki.nftables.org/wiki-nftables/index.php/Portal%3ADeveloperDocs/nftables_internals)

## GCP free-tier and access decisions

Current Google Cloud documentation gives the Compute Engine Free Tier as one non-preemptible `e2-micro` per month across `us-west1`, `us-central1`, and `us-east1`, plus 30 GB-month standard persistent disk and 1 GB outbound transfer from North America. Current VPC pricing charges USD 0.005/hour for an in-use static or ephemeral external IPv4 on a standard VM. External IPv4 therefore prevents a zero-compute/network-idle-cost final topology.

IAP TCP forwarding explicitly supports VMs without external IPs and arbitrary allowed TCP ports, using source range `35.235.240.0/20`. GCP lists Ubuntu 24.04 LTS family `ubuntu-2404-lts-amd64` as GA, free-license, OS Login capable, and Shielded-VM capable.

Locked result: project `minicontainer-r7m5o9ld`, `us-west1-a`, one private `e2-micro`, 30 GB `pd-standard`, Shielded VM, OS Login, IAP-only TCP 22/8080, no final external IP, and a short-lived Terraform Cloud NAT only when package or outbound proof traffic is required. The final application call is live workstation-to-GCP traffic through `start-iap-tunnel`, not a simulated local-only call.

Sources:

- [Google Cloud Free Program](https://cloud.google.com/free/docs/free-cloud-features)
- [VPC pricing — external IP addresses](https://cloud.google.com/vpc/pricing)
- [Use IAP for TCP forwarding](https://cloud.google.com/iap/docs/using-tcp-forwarding)
- [gcloud start-iap-tunnel](https://cloud.google.com/sdk/gcloud/reference/compute/start-iap-tunnel)
- [Compute Engine OS details](https://cloud.google.com/compute/docs/images/os-details)
- [Shielded VM](https://cloud.google.com/compute/docs/about-shielded-vm)
- [Create and manage projects](https://cloud.google.com/resource-manager/docs/creating-managing-projects)
- [Cloud Billing budgets](https://cloud.google.com/billing/docs/how-to/budgets)

Read-only live discovery on 2026-07-12 confirmed:

- active account: `nickaccturk@gmail.com`;
- one open billing account is visible;
- `e2-micro` exists in `us-west1-a` with 2 shared vCPUs and 1,024 MiB memory;
- `n2-standard-16` exists in `us-west1-a` with 16 vCPUs and 65,536 MiB memory;
- `ubuntu-2404-lts-amd64` resolved to ready image `ubuntu-2404-noble-amd64-v20260707` during research;
- `fullstack-nick/minicontainer` did not exist;
- the requested GCP project ID was not accessible and will be claimed during guarded Stage 0 bootstrap.

## GitHub CI

GitHub documents public-repository `ubuntu-24.04` runners as full x64 VMs with four CPUs, 16 GB memory, 14 GB SSD, unlimited public-repository standard-runner usage, and passwordless sudo. The single-CPU `ubuntu-slim` runner is an unprivileged container and cannot mount filesystems or access required low-level kernel features.

Locked result: use `ubuntu-24.04`, never `ubuntu-slim`. Hosted CI runs compilation, analysis, fuzz smoke, and privileged namespace/mount/network/seccomp smoke. Full systemd/cgroup lifecycle remains required on local WSL2 and GCP.

Sources:

- [GitHub-hosted runners reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
- [Ubuntu 24.04 runner image](https://github.com/actions/runner-images/blob/main/images/ubuntu/Ubuntu2404-Readme.md)

## Root filesystem fixture

Alpine's official download page identifies the minirootfs as intended for containers and minimal chroots. The current stable release is 3.24.1. The downloaded x86-64 archive was checked locally against Alpine's published checksum.

Locked artifact:

```text
URL: https://dl-cdn.alpinelinux.org/alpine/v3.24/releases/x86_64/alpine-minirootfs-3.24.1-x86_64.tar.gz
SHA-256: 41f73e3cf5fa919b8aa5ca6b30dc48f0da2720776d7423e2a7748211456fe081
Size observed: 3,698,422 bytes
```

Source: [Alpine Linux downloads](https://www.alpinelinux.org/downloads/)

## Terraform

The current official Google provider is 7.39.0. Its registry documentation recommends explicit version constraints. Terraform v1.15 remains within the stable v1 compatibility line.

Locked result: Terraform `~> 1.15.0`, Google provider exactly `7.39.0` in the initial lock file, and committed `.terraform.lock.hcl`. Provider upgrades require a dedicated plan change and clean live drift proof.

Sources:

- [HashiCorp Google provider 7.39.0](https://registry.terraform.io/providers/hashicorp/google/latest)
- [Terraform v1.15 upgrade guide](https://developer.hashicorp.com/terraform/language/upgrade-guides)

## Local environment audit

The local workstation audit on 2026-07-12 established:

```text
Windows orchestration
  Git 2.53.0.windows.2
  GitHub CLI authenticated as fullstack-nick
  gcloud authenticated as nickaccturk@gmail.com
  CMake present at C:\Program Files\CMake\bin\cmake.exe
  Ninja present through WinGet
  Clang present at C:\Program Files\LLVM\bin\clang.exe
  GCC present at C:\msys64\ucrt64\bin\gcc.exe
  Terraform 1.14.8 before the required Stage 0 upgrade

Canonical Linux development environment
  Ubuntu 24.04 under WSL2
  Linux 6.6.87.2-microsoft-standard-WSL2
  systemd as PID 1
  cgroup2 with cpu, io, memory, pids, cpuset, hugetlb, and rdma controllers
  overlayfs available
  user namespace creation succeeds
```

The Ubuntu development packages were absent at the planning boundary. Stage 0 installs them from official repositories and records exact versions and executable paths in `PLAN.md` and the stage proof.

## Final research verdict

The architecture is implementable on both canonical environments without Docker or another container runtime. The two most important research corrections were:

1. cgroup management must use a delegated systemd scope rather than writing under the hierarchy root;
2. the final GCP VM must avoid external IPv4 and use IAP tunnels to meet the free-tier objective without sacrificing live workstation-to-cloud verification.

The planning gate is closed. Implementation can proceed stage by stage under the four-gate completion protocol.
