# MiniContainer — Authoritative Development Plan

Status: **planning gate closed on 2026-07-12; implementation is authorized under the locked decisions below**  
Product: **MiniContainer, a tiny Docker-like Linux container runtime written in C**  
Target release: **v1.0.0, deployed and live-proven on Google Cloud**  
Primary operator account: **nickaccturk@gmail.com**  
Plan rule: **this is the only authoritative project plan**

## 1. Mission and completion contract

MiniContainer will be a daemonless, Linux-only container runtime that demonstrates the mechanisms behind a modern container engine without hiding them behind Docker, containerd, runc, Kubernetes, or a managed compute product. It will create and supervise isolated processes, construct their root filesystems, enforce resource limits, configure networking, harden the execution boundary, expose a clear CLI, and leave inspectable evidence for every important action.

The project is complete only when all of the following are true:

1. The full v1.0 feature set in this plan is implemented in C and passes the required local test suites.
2. Every implementation stage has one reviewed commit boundary, is pushed to the public GitHub repository, is deployed from that exact commit and packaged artifact to GCP, and has durable live-proof files committed back to the repository.
3. The final GCP project contains one running free-tier-eligible `e2-micro` VM and no development or benchmark VMs.
4. The live VM runs the same packaged MiniContainer build that was tested locally, identified by version, Git commit, and SHA-256 digest.
5. The live proof includes external calls where applicable, SSH inspection, runtime logs, namespace inspection, cgroup inspection, network inspection, negative security tests, stress tests, reboot recovery, and resource cleanup.
6. CI is green on the final public commit and the release artifact is reproducible from the repository.
7. The README is recruiter-ready: features, differentiators, architecture, internals, security boundary, local launch instructions, live GCP proof, benchmarks, comparison with Docker/runc, and a concise demonstration.
8. Secrets, credentials, billing identifiers, private keys, operator home paths, and the operator's personal IP address never enter Git history or proof artifacts.

Local success alone never completes a stage. A GitHub push alone never completes a stage. A Terraform apply alone never completes a stage. Every stage requires all four gates: **local verification → GitHub/CI → GCP deployment → live behavioral and host-level proof**.

## 2. Locked product boundary

### 2.1 v1.0 commands

The installed program is `minicontainer`; the per-container supervisor is `minicontainer-shim`.

```text
minicontainer version [--json]
minicontainer info [--json]
minicontainer image import NAME ROOTFS_TAR
minicontainer image list [--json]
minicontainer image inspect NAME [--json]
minicontainer image remove NAME
minicontainer run [flags] --image NAME -- COMMAND [ARG...]
minicontainer create [flags] --image NAME -- COMMAND [ARG...]
minicontainer start ID
minicontainer ps [--all] [--json]
minicontainer inspect ID [--json]
minicontainer exec ID -- COMMAND [ARG...]
minicontainer stop [--time SECONDS] ID
minicontainer kill [--signal SIGNAL] ID
minicontainer logs [--follow] [--tail LINES] ID
minicontainer stats [--no-stream] [--json] ID...
minicontainer rm [--force] ID
minicontainer gc
```

`run` is `create + start + attach`; `run --detach` returns after the container reaches the running state. Container names are supported through `--name`. A full 128-bit random ID is generated with `getrandom(2)` and a collision-free 12-hex prefix is accepted by commands.

### 2.2 Runtime flags

The stable v1.0 run/create surface is:

```text
--image NAME
--name NAME
--hostname NAME
--detach
--env KEY=VALUE
--workdir ABSOLUTE_PATH
--user UID[:GID]
--read-only
--tmpfs CONTAINER_PATH[:SIZE]
--mount type=bind,src=HOST_PATH,dst=CONTAINER_PATH,ro=true|false
--memory BYTES | --mem BYTES
--swap BYTES
--cpus DECIMAL | --cpu DECIMAL
--pids-limit COUNT
--network bridge|none
--publish HOST_IP:HOST_PORT:CONTAINER_PORT/tcp|udp
--cap-add CAPABILITY
--seccomp-profile default|PATH
--log-driver file|none
--debug
```

Defaults are locked: foreground execution, generated hostname derived from the ID, writable overlay root, 128 MiB memory, no swap, 0.5 CPU, 128 PIDs, bridge networking, no published ports, zero retained Linux capabilities, built-in default-deny seccomp profile, and file logging.

### 2.3 Explicit exclusions

The v1.0 product does not implement an OCI registry client, Dockerfile builder, layered-image distribution protocol, CRI, Kubernetes integration, checkpoint/restore, live migration, multi-host networking, Windows containers, macOS containers, a web control plane, or a long-running privileged API daemon. Image input is a checksum-verifiable Linux rootfs tar archive. The architecture keeps image ingestion, runtime setup, and state management separated so these exclusions do not distort the implemented runtime.

MiniContainer is an educational systems runtime with production-quality engineering discipline. It will not claim to be a production-grade hostile multi-tenant sandbox.

## 3. Locked architecture

### 3.1 Platform and compatibility

- Language: ISO C17 plus documented Linux/GNU extensions.
- Supported CPU for v1.0: x86-64.
- Minimum Linux kernel: 5.15 with unified cgroups v2, user namespaces, seccomp, pidfds, `clone3`, `openat2`, and overlayfs.
- Init/cgroup manager: systemd 255 or newer. MiniContainer uses systemd's D-Bus API for transient delegated scopes and never writes into a systemd-owned cgroup.
- Canonical local environment: Ubuntu 24.04 LTS under WSL2 with systemd and cgroups v2 enabled.
- Canonical live environment: Ubuntu 24.04 LTS on GCE.
- Build system: CMake with Ninja.
- Compilers: GCC and Clang; both compile with warnings promoted to errors in CI.
- License: Apache-2.0.
- Installed paths:
  - `/usr/bin/minicontainer`
  - `/usr/libexec/minicontainer/minicontainer-shim`
  - `/etc/minicontainer/`
  - `/usr/share/minicontainer/`
  - `/var/lib/minicontainer/`
  - `/run/minicontainer/`
  - `/var/log/minicontainer/`

The program refuses to run on non-Linux systems, on cgroups v1, or without the required kernel features. It reports each failed prerequisite precisely through `minicontainer info`.

### 3.2 Daemonless process model

The CLI never talks to a global privileged daemon and is never installed setuid. Administrative commands require explicit root execution. Each running container has one small host-side `minicontainer-shim` process started by the CLI.

The shim:

1. owns the container init process as its child;
2. keeps a pidfd and records `/proc/<pid>/stat` start time to prevent PID-reuse mistakes;
3. forwards signals and terminal resize events;
4. captures stdout/stderr into a bounded, rotated file log;
5. reaps the container and records the exact exit status;
6. performs idempotent network, mount, cgroup, and runtime-directory cleanup;
7. exposes a root-only `SOCK_SEQPACKET` Unix control socket in `/run/minicontainer/<id>/control.sock`, validates `SO_PEERCRED`, and uses versioned bounded JSON messages;
8. updates state through atomic replace plus `fsync`.

Inside the PID namespace, the container init process runs as PID 1, forwards termination signals to the workload process group, and reaps orphaned descendants. The workload never inherits control-plane file descriptors.

### 3.3 Namespace model

Every bridge-network container receives new user, PID, mount, UTS, IPC, network, and cgroup namespaces. A `network=none` container still receives an isolated network namespace with only the loopback interface configured. Namespace creation uses the raw `clone3(2)` syscall with `CLONE_PIDFD`, `CLONE_INTO_CGROUP`, and a parent/child synchronization barrier. The target payload cgroup exists before cloning, so accounting begins at the first instruction. The parent completes UID/GID mapping and veth configuration before the child can mount or execute the workload.

Container root maps to a dedicated subordinate UID/GID range owned by the `minicontainer` system identity. All containers use the configured 65,536-ID range; filesystem and namespace isolation prevent cross-container path access. `/etc/subuid` and `/etc/subgid` are validated before execution. The runtime never maps container root to host root.

### 3.4 Filesystem model

Image import uses libarchive and rejects absolute paths, `..` traversal, escaping symlinks, unsafe hardlinks, device nodes, sockets, and ownership outside the configured subordinate range. Setuid, setgid, and file-capability metadata is stripped. Each imported tar archive is addressed by SHA-256 digest and extracted once into an immutable image directory with archive UIDs/GIDs shifted into the dedicated subordinate range.

The canonical test image is Alpine Linux 3.24.1 x86-64 minirootfs, file `alpine-minirootfs-3.24.1-x86_64.tar.gz`, SHA-256 `41f73e3cf5fa919b8aa5ca6b30dc48f0da2720776d7423e2a7748211456fe081`. The fetch script verifies both this digest and the detached signature against Alpine's pinned release-key fingerprint before import.

Each container receives:

- a read-only image lower directory;
- a private overlayfs upper directory and work directory;
- a private mount namespace with recursive propagation set to `MS_PRIVATE`;
- `pivot_root(2)` into the overlay mount, followed by unmount and removal of the old root;
- a new `/proc` mounted with `nosuid,nodev,noexec`;
- a tmpfs `/dev` containing only controlled bind mounts for `null`, `zero`, `random`, `urandom`, `tty`, and a private `devpts` instance;
- tmpfs mounts for `/run` and `/tmp` with bounded sizes;
- generated `/etc/hostname`, `/etc/hosts`, and `/etc/resolv.conf` files;
- bind mounts opened with `openat2(2)` resolution constraints and remounted with the requested read-only flags.

`--read-only` makes the overlay root read-only and preserves writable `/run`, `/tmp`, and declared tmpfs mounts. All mount targets are created beneath an already-open root fd; path handling never follows an attacker-controlled path back onto the host.

### 3.5 State model

The immutable container specification lives at `/var/lib/minicontainer/containers/<full-id>/config.json`; mutable state lives beside it at `state.json`; runtime state lives at `/run/minicontainer/<full-id>/`; logs live at `/var/log/minicontainer/<full-id>/`. JSON parsing and serialization use `json-c` with explicit schema versions. Directories are root-owned mode `0700`; files are mode `0600`. Stored environment values are required for `create/start`, remain root-readable at rest, and are always redacted from CLI, logs, and proof output.

The state machine is locked:

```text
creating -> created -> running -> stopped -> removed
     |          |          |
     +--------> failed <----+
```

Every mutating command takes a global registry lock and a per-container `flock`. State changes use a write-to-temporary-file, `fsync`, atomic `rename`, directory `fsync` sequence. The stored record includes the schema version, IDs, name, image digest, command, sanitized environment, namespace inode numbers, host PID, PID start time, cgroup path, IP allocation, published ports, timestamps, exit status, build version, and originating Git commit. Secrets passed in environment variables are redacted from human output and never copied into proof files.

At startup, commands reconcile stored state against pidfds, `/proc`, namespace inode numbers, mounts, veth devices, nftables objects, and cgroups. `gc` removes only resources that carry MiniContainer ownership markers and have no live matching process.

### 3.6 Resource control

The runtime follows systemd's cgroup-v2 single-writer contract. For each container, the CLI registers the already-forked shim in a transient `minicontainer-<full-id>.scope` through `org.freedesktop.systemd1.Manager.StartTransientUnit`, with `Delegate=yes`, `CollectMode=inactive-or-failed`, and an exact description. The shim reads the scope's `ControlGroup` property rather than deriving a path from the unit name. It creates sibling `supervisor` and `payload` cgroups inside that delegated subtree, moves itself into `supervisor`, enables `cpu`, `memory`, `pids`, and `io` in `cgroup.subtree_control`, and clones container init directly into `payload` with `CLONE_INTO_CGROUP`. MiniContainer never creates cgroups at the hierarchy root and never modifies the delegated scope's systemd-owned attributes.

- `memory.max` enforces `--memory`.
- `memory.swap.max` enforces `--swap`; the default is zero swap.
- `cpu.max` uses a fixed 100000 µs period and a quota derived without floating-point drift.
- `pids.max` enforces `--pids-limit`.
- `cgroup.procs` receives init before the synchronization barrier is released.
- `cgroup.kill` is used for forced teardown.
- `memory.events`, `cpu.stat`, `pids.current`, `io.stat`, and pressure data feed `stats` and proof artifacts.

Numeric parsing is overflow-checked and accepts documented IEC units. Requested limits are read back after writes. Cleanup waits for `populated 0` before removing the cgroup.

### 3.7 Networking

The host owns one bridge named `mcbr0` with subnet `10.44.0.0/24` and gateway `10.44.0.1`. The usable allocation pool is `10.44.0.2` through `10.44.0.254`. Allocation is serialized and persisted; stale leases are reclaimed only after process and interface reconciliation.

The runtime uses rtnetlink directly for bridge creation, veth creation, namespace movement, link state, addresses, and routes. Host veth names are deterministic, length-safe, and collision-checked. The container side is renamed `eth0`; loopback is enabled; the default route points to `10.44.0.1`.

NAT and port forwarding use nftables, never iptables compatibility mode. MiniContainer owns a dedicated `inet minicontainer` table. Updates are sent as atomic nft batches through `execve` with fixed arguments and stdin; no shell interprets generated content. Per-container chains and comments carry the full container ID. Published-port conflicts are checked under the registry lock. Both TCP and UDP mappings are implemented, including host-originated connections and external DNAT. Forwarding rules default-deny container ingress except established traffic and declared ports.

The live demo publishes an HTTP workload from container port 8080 to host port 8080 and proves authenticated workstation-to-GCP reachability through an IAP TCP tunnel plus namespace isolation. It is deliberately not an unauthenticated public endpoint.

### 3.8 Security model

The fixed setup order is:

1. validate configuration and all host paths;
2. allocate ID, UID/GID mapping, cgroup, IP, and ports under locks;
3. create namespaces and synchronize parent/child;
4. build mounts and pivot root;
5. set hostname, working directory, user, groups, and sanitized environment;
6. fork the workload beneath the container-init supervisor;
7. in the workload child, clear supplementary groups, set the requested UID/GID, construct only the requested namespaced permitted/inheritable/effective/ambient capability sets, and drop every other bounding capability;
8. set `PR_SET_NO_NEW_PRIVS`, install the workload seccomp filter with thread synchronization, close all non-standard file descriptors, and `execve` the workload;
9. in container init, clear all capability sets and the bounding set, set `PR_SET_NO_NEW_PRIVS`, install the narrower init seccomp filter, then reap and forward signals.

The built-in seccomp policy is a default-deny allowlist generated from a versioned source file. Unsupported syscalls return `EPERM`; architecture mismatches terminate the process. `clone3` returns `ENOSYS` so libc can fall back to a mask-checked `clone`; namespace-creation flags on `clone` are denied. Socket rules deny raw sockets and unsupported families. The policy blocks namespace creation, mounting, kernel module operations, `bpf`, `perf_event_open`, `ptrace`, keyring operations, reboot, kexec, raw I/O, and newer kernel attack surface not required by ordinary workloads. Unit tests validate the generated BPF through libseccomp; live negative tests verify blocked syscalls from inside a container.

Capabilities default to empty. `--cap-add` accepts only canonical names known to the running kernel and never grants capabilities outside the container user namespace. Bind mounts reject `/proc`, `/sys`, `/dev`, `/run/minicontainer`, `/var/lib/minicontainer`, the cgroup filesystem, namespace handles, and host root as sources. Host paths must be explicitly allowlisted in `/etc/minicontainer/config.json`.

The security documentation will state the remaining boundary clearly: a kernel vulnerability can escape all same-kernel containers; shared subordinate IDs increase the impact of a filesystem disclosure; the runtime has not received an independent security audit; denial-of-service classes exist beyond the implemented controllers; and MiniContainer is not a replacement for gVisor, Kata Containers, runc, or Docker in hostile production multi-tenancy.

### 3.9 Diagnostics and developer experience

- Human-readable output goes to stdout; diagnostics go to stderr.
- `--json` has a stable versioned schema and never mixes log text into stdout.
- Errors include a stable code, failed operation, relevant resource, and saved `errno` text.
- `--debug` emits timestamped structured JSON Lines with source component and container ID.
- File logs rotate at 10 MiB with three retained files.
- Exit codes distinguish usage, prerequisites, conflicts, not-found, permission, runtime setup, workload exit, and internal errors.
- All cleanup operations report failures and continue through the remaining cleanup stack.
- `minicontainer info` reports kernel, namespace, cgroup, overlayfs, seccomp, user mapping, bridge, nftables, and storage readiness.

### 3.10 Source tree

```text
.
|-- CMakeLists.txt
|-- CMakePresets.json
|-- LICENSE
|-- PLAN.md
|-- README.md
|-- SECURITY.md
|-- CHANGELOG.md
|-- cmake/
|-- config/
|   |-- default-seccomp.json
|   `-- minicontainer.example.json
|-- include/minicontainer/
|-- src/
|   |-- cli/
|   |-- runtime/
|   |-- linux/
|   |-- state/
|   |-- image/
|   |-- network/
|   |-- security/
|   `-- util/
|-- tests/
|   |-- unit/
|   |-- integration/
|   |-- security/
|   |-- stress/
|   `-- fixtures/
|-- packaging/debian/
|-- scripts/
|-- infra/terraform/
|-- docs/
|   |-- architecture/
|   |-- operations/
|   |-- benchmarks/
|   |-- research.md
|   `-- proofs/
`-- .github/workflows/
```

Modules have private implementation headers wherever possible. Linux syscall wrappers are isolated under `src/linux`. Allocation and cleanup follow a single-owner pattern with cleanup stacks; no module calls `exit` outside `main` and the shim entry point.

## 4. Build, quality, and test strategy

### 4.1 Dependencies

Runtime-linked dependencies are limited to glibc, libseccomp, json-c, libarchive, OpenSSL's libcrypto for SHA-256, and libsystemd for transient-scope D-Bus integration. Linux capability manipulation uses kernel syscalls directly. Network configuration uses rtnetlink directly. All dependencies come from Ubuntu repositories and are recorded with exact package names in the toolchain manifest. Unit tests use CMocka as a development-only dependency.

Development tooling includes CMake, Ninja, GCC, Clang, GDB, LLDB, clang-format, clang-tidy, clang static analyzer, cppcheck, gcovr, Valgrind, strace, ltrace, pkg-config, dpkg-buildpackage/debhelper/lintian, Git, GitHub CLI, Google Cloud CLI, Terraform, tflint, ShellCheck, shfmt, actionlint, jq, nftables, iproute2, socat, curl, hyperfine, stress-ng, Syft, and gitleaks.

### 4.2 Compiler policy

The baseline flags are `-std=c17 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wformat=2 -Wundef -Wstrict-prototypes -Wmissing-prototypes -Wcast-qual -Wwrite-strings`. Release packages add `-O2 -fstack-protector-strong -D_FORTIFY_SOURCE=3`, PIE, `-Wl,-z,relro,-z,now,-z,noexecstack`, and a linker build ID. Sanitizer builds use `-fno-omit-frame-pointer` and do not combine incompatible fortification flags. Each warning suppression requires a comment and plan amendment.

CMake presets provide `dev-gcc`, `dev-clang`, `asan-ubsan`, `coverage`, `fuzz`, and `release`. Release builds use `-O2 -g`, not stripped-at-build binaries; Debian packaging separates debug symbols. `dpkg-buildpackage` receives a Git-derived `SOURCE_DATE_EPOCH`, normalized locale/timezone, and deterministic archive settings. The release gate rebuilds twice in clean directories and requires byte-identical `.deb` output.

### 4.3 Required test layers

1. **Unit tests:** parsers, sizes/CPU values, IDs, state transitions, JSON schema, atomic files, path resolution, cleanup stacks, seccomp generation, IP allocation, port rules, and error mapping.
2. **Component tests:** temporary image import, overlay creation, cgroup file programming, rtnetlink messages, nft batch generation, state locking, shim protocol, log rotation, and process supervision.
3. **Privileged integration tests:** namespace inode separation, PID view, hostname, IPC, mount propagation, `pivot_root`, proc mount flags, UID/GID maps, cgroup enforcement, lifecycle commands, exec, signals, and cleanup.
4. **Network tests:** bridge address, veth movement, outbound NAT, network-none isolation, TCP/UDP port publishing, collision rejection, and nft cleanup.
5. **Security tests:** malicious archives, traversal paths, forbidden bind mounts, capability absence, `no_new_privs`, seccomp denied/allowed probes, FD leakage, symlink races, PID reuse simulation, and non-root refusal.
6. **Failure-injection tests:** kill the CLI or shim at each setup checkpoint, rerun reconciliation, and prove no unsafe mounts, veths, nft rules, cgroups, leases, or locked state remain.
7. **Stress tests:** 100 sequential containers, bounded parallel create/run/rm cycles, repeated exec/stop races, log pressure, PID pressure, memory OOM, CPU throttling, and 24-hour live soak.
8. **Fuzz tests:** libFuzzer targets for numeric/CLI parsing, JSON state/config parsing, archive-entry validation, seccomp-profile parsing, IP/port parsing, and shim messages, with fixed CI smoke duration and longer release corpus runs.
9. **Static and dynamic analysis:** clang-tidy, clang static analyzer, cppcheck, GitHub CodeQL, ASan, UBSan, Valgrind, gcovr branch coverage, ShellCheck, actionlint, Terraform fmt/validate/tflint, gitleaks, and dependency review.

Unit-test line coverage must be at least 85%; branch coverage must be at least 75%. Security- and cleanup-critical modules require direct tests for every error branch that can be deterministically injected. Sanitizer and Valgrind runs must have zero known findings.

### 4.4 CI gates

GitHub Actions on `ubuntu-24.04` runs:

- formatting and generated-file drift checks;
- GCC and Clang builds;
- unit tests and coverage thresholds;
- ASan/UBSan and Valgrind;
- clang-tidy and cppcheck;
- privileged namespace, mount, network, and seccomp smoke tests on the full Ubuntu VM runner; systemd/cgroup lifecycle integration remains a mandatory WSL2 and GCP gate rather than relying on hosted-runner cgroup ownership;
- fixed-duration libFuzzer smoke tests and CodeQL;
- shell, workflow, Terraform, documentation, and secret checks;
- reproducible Debian package creation and lintian;
- SHA-256 and SBOM generation;
- artifact provenance metadata containing Git commit and toolchain versions.

No deployment is accepted while required CI checks are red. GCP deployment remains an explicit operator action from the verified workstation so the exact account, project, Terraform plan, SSH transcript, and cleanup can be inspected before mutation.

## 5. GCP architecture and control policy

### 5.1 Account guard

Every script that can mutate GCP first reads the active `gcloud` account and fails closed unless it equals `nickaccturk@gmail.com`. It also verifies the expected project ID, Terraform workspace, and Git commit. The guard has tests and cannot be bypassed by a truthy environment variable.

### 5.2 Cloud resources

The final topology is:

- dedicated project name `MiniContainer` and globally unique project ID `minicontainer-r7m5o9ld`, linked to the user's open free-trial billing account;
- region `us-west1` and zone `us-west1-a`, which satisfy the current Compute Engine Always Free region rule and expose both `e2-micro` and `n2-standard-16` machine types;
- Compute Engine, IAM, Service Usage, OS Login, IAP, Cloud Resource Manager, Cloud Billing Budget, and Cloud Storage APIs enabled;
- one custom-mode VPC `minicontainer-vpc` and subnet `minicontainer-us-west1`;
- one `e2-micro` VM named `minicontainer-vm` with `can_ip_forward=true` and no external IP address;
- Ubuntu 24.04 LTS from `ubuntu-os-cloud/ubuntu-2404-lts-amd64` on one 30 GB `pd-standard` boot disk;
- Shielded VM with Secure Boot, vTPM, and integrity monitoring;
- OS Login enabled, project-wide SSH keys blocked, serial-port access disabled, and a dedicated VM service account with no project roles and no OAuth scopes;
- IAP-only ingress from `35.235.240.0/20` to TCP 22 and demo TCP 8080; no internet ingress firewall rule;
- authenticated live calls through `gcloud compute start-iap-tunnel minicontainer-vm 8080`, with SSH and SCP forced through `--tunnel-through-iap`;
- one private, public-access-prevented, versioned Standard GCS bucket in `us-west1` for the Terraform backend, with noncurrent versions deleted after 30 days;
- one project-scoped monthly billing budget denominated in the billing account's required currency: TRY 470, locked on 2026-07-12 as approximately USD 10 at the observed USD/TRY rate; actual-spend thresholds at 50%, 80%, and 100% and forecast threshold at 100%;
- no managed database, load balancer, GKE, Cloud Run, reserved external address, or always-on paid service.

Development capacity policy is locked: begin each stage on the final `e2-micro`, but if host metrics, kernel logs, build/test duration, or repeated resource-pressure failures show that the VM is constraining development, stop it and resize that same VM immediately to the smallest sufficient machine type, up to `n2-standard-16`. Development must not pause merely because `e2-micro` is choking. Heavy stress and benchmark gates may use `n2-standard-16` deliberately. Every resize is recorded in the stage proof. Before the whole project can be declared complete, the VM must be stopped, resized back to `e2-micro`, restarted and reverified; every additional development or load-generator VM must be stopped and deleted so exactly one running `e2-micro` remains.

Project creation and billing linkage use guarded `gcloud` commands because the project must exist before its provider can be initialized. Terraform owns every in-project resource. A Terraform-controlled Cloud Router/NAT pair exists only during Stage 0 OS bootstrap, Stage 4 outbound-network proof, and final security-update windows; it is destroyed before the applicable stage can pass. This accepts a small free-trial-credit charge to avoid a permanently billed external IPv4 address or Cloud NAT. All other artifact transfers use IAP. The final inventory contains no Cloud NAT and no external IPv4.

### 5.3 Deployment artifact

The release unit is a reproducible Debian `.deb` package plus SHA-256, SBOM, build manifest, default seccomp profile, and scripts. `scripts/build-release.sh` creates it once. `scripts/deploy.ps1` verifies account/project/commit, verifies the package digest, uploads the exact package and the already-verified Alpine archive through `gcloud compute scp --tunnel-through-iap`, installs it through SSH, imports the minirootfs, runs reconciliation, and reads back the installed version and digest.

Terraform owns cloud resources; configuration and artifact installation use auditable SSH commands after Terraform apply. Provisioners are not embedded in Terraform. The VM bootstrap installs only OS prerequisites and creates the dedicated subordinate-ID configuration.

### 5.4 Live proof standard

Each stage writes `docs/proofs/stage-NN-<name>/` containing:

- `README.md`: timestamp, commit, artifact SHA-256, VM identity, commands, expected results, actual results, and verdict;
- `local-tests.txt`: summarized local commands and results;
- `ci.txt`: workflow URL and job conclusions;
- `terraform.txt`: redacted plan/apply outputs and resource inventory;
- `deploy.txt`: package upload/install/version/digest readback;
- `live-commands.txt`: workstation-to-GCP calls through IAP plus host and in-container calls;
- `host-inspection.txt`: SSH inspection of logs, processes, namespaces, mounts, cgroups, networking, and cleanup;
- `cleanup.txt`: post-test orphan scan and current billable resource list.

Proof capture scripts redact access tokens, credentials, billing IDs, SSH material, personal IP addresses, sensitive environment values, and Terraform state. The proof README explains every redaction. Raw sensitive transcripts remain outside Git.

Every proof finishes with an explicit `PASS` or `FAIL`. A failed stage remains open and the next stage does not start.

## 6. Implementation stages

### Stage 0 — Blueprint, workstation, repository, and cloud foundation

Deliverables:

- preserve `docs/research.md` and the 2026-07-12 closed planning baseline as the implementation contract;
- record ADRs for process model, user mapping, filesystem, state, networking, seccomp, artifact, and GCP topology;
- audit/install all workstation and WSL tooling and record exact versions;
- initialize Git with hooks, `.editorconfig`, `.gitignore`, license, contribution/security policies, CMake skeleton, test harness, and documentation tree;
- create the public GitHub repository `fullstack-nick/minicontainer`, add its short description and topics, push the initial commit, and configure Actions, CodeQL, Dependabot, and secret scanning;
- create the guarded GCP project and Terraform foundation;
- deploy a signed/digested `minicontainer version` and `minicontainer info` skeleton to the `e2-micro` VM;
- collect Stage 0 proof and push it.

Exit gate: clean local checks, green CI, public repo readable, account guard proven, Terraform idempotent, live binary digest matches local artifact, SSH host inspection clean, one VM present.

### Stage 1 — v0.1: isolated rootfs execution

Deliverables:

- CLI parser, error model, structured diagnostics, ID/name validation, and prerequisite detection;
- secure image import and immutable digest store;
- shim launch and synchronization protocol;
- user/PID/mount/UTS/IPC/network/cgroup namespace creation;
- subordinate ID mapping;
- overlay root, private mounts, `pivot_root`, `/proc`, `/dev`, `/run`, and `/tmp`;
- container PID 1 reaper/signal forwarder;
- foreground `run`, detach `run`, environment/workdir/user handling, exit status propagation, logs, and complete cleanup;
- Alpine minirootfs fixture fetched by pinned URL and checksum.

Live proof: isolated hostname and namespace inode numbers, container PID view, read/write overlay behavior, proc mount flags, UID mapping, command exit status, signal handling, foreground/detached execution, logs, and zero residual resources after exit.

### Stage 2 — v0.2: cgroups v2 limits and stats

Deliverables:

- cgroup subtree setup and controller verification;
- memory, swap, CPU, PIDs, and I/O statistics;
- strict value parsers and readback verification;
- `stats` human/JSON output;
- OOM, throttling, PID exhaustion, signal, and cleanup behavior;
- resource defaults embedded in `create/run` state.

Live proof: memory allocation exceeds `memory.max` and records OOM events; a CPU workload is throttled according to `cpu.max`; fork pressure stops at `pids.max`; stats match cgroup files; cgroup disappears after removal.

### Stage 3 — v0.3: complete lifecycle and crash recovery

Deliverables:

- versioned atomic state store and state-machine enforcement;
- `create`, `start`, `ps`, `inspect`, `exec`, `stop`, `kill`, `logs`, `rm`, and `gc`;
- name and abbreviated-ID lookup;
- pidfd signaling and PID-start-time validation;
- namespace joining for exec with the same cgroup, user, capability, seccomp, and rootfs boundary;
- graceful timeout followed by cgroup kill;
- concurrent command locking, stale state reconciliation, shim/CLI failure injection, and idempotent cleanup.

Live proof: full lifecycle sequence, exec namespace/cgroup identity, graceful and forced stop, exit-code persistence, concurrent conflict handling, deliberate CLI/shim kills, reconciliation, and orphan-free recovery.

### Stage 4 — v0.4: bridge networking and port publishing

Deliverables:

- rtnetlink bridge, veth, address, route, and loopback operations;
- locked IPAM store and stale lease reconciliation;
- nftables forwarding, masquerade, TCP/UDP publishing, conflict detection, and per-container cleanup;
- generated hostname/hosts/resolver files;
- bridge and none network modes;
- IAP-reachable live HTTP demo workload.

Live proof: distinct network namespace, outbound DNS/HTTP while temporary Terraform NAT is enabled, network-none denial, container-to-host routing, workstation TCP HTTP call through IAP, UDP mapping, undeclared-port denial, nft rule ownership, IP/port reuse after removal, complete interface/rule cleanup, and destruction of temporary Cloud NAT.

### Stage 5 — v0.5: security hardening

Deliverables:

- `no_new_privs`, securebits, supplementary-group clearing, and full capability-set handling;
- default-empty capabilities and explicit namespaced capability additions;
- generated default-deny seccomp allowlist and custom-profile validation;
- secure bind/tmpfs/read-only mount policy with `openat2` resolution;
- malicious archive and filesystem race defenses;
- FD closure and sensitive-state redaction;
- `SECURITY.md`, threat model, trust boundaries, and disclosure instructions.

Live proof: capabilities are empty by default; declared namespaced capability behaves as documented; blocked syscalls return the defined result; allowed workload remains functional; forbidden mounts and malicious archives fail safely; read-only root is enforced; host sentinel files remain unreachable.

### Stage 6 — v0.6: reliability and operations

Deliverables:

- log rotation and follow semantics;
- boot-time reconciliation service and idempotent bridge/cgroup recovery;
- deterministic cleanup stack and expanded failure injection;
- package install, upgrade, rollback, and uninstall behavior;
- operational runbooks for deploy, rollback, recovery, debugging, resource leaks, and GCP teardown;
- 100-container sequential stress run, bounded concurrency run, exec/stop races, OOM storms, log pressure, and reboot recovery;
- zero-leak assertions across processes, mounts, namespaces, cgroups, veths, nftables, files, sockets, and leases.

Live proof: restart the VM, recover truthful stopped/running state, restore the demo workload through systemd, complete stress/failure suites, inspect kernel and runtime logs, and show no leaked resources.

### Stage 7 — v1.0: release, benchmark, demo, and final cleanup

Deliverables:

- close all sanitizer, Valgrind, static-analysis, test, documentation, and CI findings;
- deterministic release package, debug symbols, SBOM, checksums, and GitHub v1.0.0 release;
- benchmark cold start, steady-state memory, run/remove throughput, exec latency, CPU-limit accuracy, and HTTP overhead;
- stop `minicontainer-vm`, resize that same VM in place from `e2-micro` to `n2-standard-16`, run the exact release artifact through the benchmark and heavy stress gates, then stop and resize the same VM back to `e2-micro` before the stage can pass;
- run the 24-hour `e2-micro` soak and inspect kernel/runtime logs afterward;
- produce architecture diagrams, syscall walkthrough, comparison table, terminal recording, demo GIF/video, resume bullet, and polished README;
- audit public Git history and artifacts for secrets/private data;
- verify the final GCP inventory contains exactly one running `e2-micro` workload VM and no other development/benchmark compute;
- tag and publish the final proof commit.

Exit gate: all completion-contract items pass, live demo works, CI and release are green, proof is self-contained and sanitized, cost/resource inventory is clean, and the repository is ready to show without verbal explanation.

## 7. Per-stage execution protocol

For every stage, execute this fixed order:

1. Re-read the stage boundary and record exclusions from the next stage.
2. Confirm clean/understood Git status and record the starting commit.
3. Implement in small reviewable changes with tests written alongside behavior.
4. Run format, unit, sanitizer, static, integration, failure, and stage-specific tests locally.
5. Review the diff for security, cleanup, error paths, scope creep, private data, and generated artifacts.
6. Build the release `.deb`, SBOM, manifest, and SHA-256 once.
7. Commit and push the stage candidate to GitHub.
8. Wait for required CI and fix every failure; never waive a failed required check.
9. Run the GCP account/project guard and save a redacted Terraform plan.
10. Apply Terraform, upload the exact local artifact, install through SSH, and read back version/commit/digest.
11. Run live behavioral calls, then SSH into the VM and inspect process, log, namespace, mount, cgroup, network, security, and cleanup evidence.
12. Run an orphan/resource/cost scan and redact the transcripts.
13. Commit the stage proof and stage retrospective to `docs/proofs` and append lessons that affect future stages to this plan.
14. Push the proof commit, confirm CI again, tag the stage version, and mark the stage `PASS`.

No stage combines unverified changes from the next stage. A discovered defect is fixed in the earliest owning stage and the affected local/live gates are rerun.

## 8. Research-closed decisions

The 2026-07-12 research pass closed every architecture question before implementation:

1. **Canonical OS:** Ubuntu 24.04 on both WSL2 and GCE. The workstation is confirmed at kernel `6.6.87.2-microsoft-standard-WSL2`, systemd 255, cgroup2, and overlayfs. GCE family `ubuntu-2404-lts-amd64` is GA and currently resolves to a ready July 2026 image.
2. **Cloud allowance:** one non-preemptible `e2-micro` in `us-west1`, `us-central1`, or `us-east1`, 30 GB-month `pd-standard`, and 1 GB North America outbound traffic are Always Free. Running external IPv4 addresses are billed, so the final VM has none.
3. **Location/access:** `us-west1-a`; private VM; IAP for SSH, SCP, and TCP 8080; transient Terraform Cloud NAT only for controlled outbound windows.
4. **Local toolchain:** Linux runtime work occurs in the confirmed Ubuntu 24.04 WSL2 distro. Windows remains the orchestration surface for GitHub, gcloud, Terraform, and PowerShell deploy/proof scripts. Exact installed versions and paths are appended after the Stage 0 installation audit.
5. **Process/cgroup API:** raw `clone3` provides pidfd and direct cgroup placement. systemd transient scopes provide the delegated subtree required by the cgroup-v2 single-writer model. Direct writes beneath the root hierarchy are forbidden.
6. **Filesystem/path API:** shifted subordinate IDs plus overlayfs, private propagation, and `pivot_root`; `openat2` with `RESOLVE_BENEATH`, `RESOLVE_NO_MAGICLINKS`, and operation-specific symlink rules prevents host-path escape.
7. **CI boundary:** GitHub's full `ubuntu-24.04` runner is a VM with passwordless sudo and runs namespace/mount/network/seccomp smoke tests. Full systemd/cgroup lifecycle proof remains mandatory on WSL2 and GCP.
8. **Image fixture:** Alpine 3.24.1 x86-64 minirootfs with pinned SHA-256 and detached-signature verification.
9. **Security/network libraries:** libseccomp generates a default-deny policy; rtnetlink owns L2/L3 objects; `nft -f -` receives validated atomic batches without a shell.
10. **Packaging/infrastructure:** reproducible Debian packaging through `dpkg-buildpackage`; Terraform 1.15 series with exact provider lock `hashicorp/google` 7.39.0; GCS versioned remote state after guarded project bootstrap.

The evidence and source links are preserved in `docs/research.md`. Changing one of these decisions requires an explicit plan amendment, rationale, affected-stage analysis, and rerun of every invalidated gate.

## 9. Risk register and fixed responses

| Risk | Prevention and response |
|---|---|
| WSL2 diverges from GCE kernel behavior | Treat GCE as the canonical privileged-integration environment; keep syscall/unit coverage local and prove every privileged feature live. |
| Tiny `e2-micro` memory pressure causes flaky builds | Build the artifact locally, deploy the exact package, limit parallel tests, and separate VM capacity failures from runtime correctness with host metrics. |
| Container setup fails mid-sequence | Register inverse cleanup before each mutation, inject failure at every checkpoint, reconcile on every mutating command and boot. |
| PID reuse targets the wrong process | Require pidfd plus saved process start time and namespace inode validation before signal/join operations. |
| Path or archive traversal reaches host files | Use dirfd-relative operations, `openat2` resolution restrictions, libarchive secure extraction flags, and hostile fixtures. |
| nftables or veth resources leak | Tag every owned object with full ID, perform atomic updates, and run an ownership-scoped orphan scan after every live test. |
| C memory/FD error corrupts cleanup | Enforce single ownership, cleanup stacks, sanitizers, Valgrind, FD inventory tests, and failure injection. |
| Seccomp breaks legitimate workloads | Generate and version the allowlist, test a representative Alpine workload suite, and surface denied syscall diagnostics in debug mode. |
| GCP mutation hits the wrong account/project | Fail-closed account/project/commit guard before every mutation and read back active identity in proof. |
| Proof leaks sensitive data | Capture through a redaction pipeline, scan staged files and Git history, and keep raw transcripts outside the repository. |
| N2 benchmark capacity or lingering cost | Resize the sole VM in place only for the bounded release gate, use `us-west1-a`, then prove machine type `e2-micro` and absence of all other instances before completion. |
| systemd and MiniContainer contend for cgroup ownership | Use a transient scope with `Delegate=yes`, read its D-Bus `ControlGroup`, manipulate only child cgroups, and test the single-writer contract. |
| Scope expands into an OCI engine | Enforce the explicit exclusions and stage boundaries; amend this plan before any boundary change. |

## 10. README and demo contract

The final README will contain:

1. one-sentence product definition and an immediate demo;
2. feature list and why the project is technically distinctive;
3. architecture diagram showing CLI, shim, namespaces, overlayfs, cgroups, veth/bridge/nftables, state, and logs;
4. exact local prerequisites and launch commands;
5. a syscall-oriented “How it works” walkthrough;
6. command reference with copyable examples;
7. security model, threat boundary, and responsible-use statement;
8. live GCP topology and linked proof, with version/commit/digest;
9. benchmark results and methodology;
10. comparison against Docker, runc, bubblewrap, and systemd-nspawn without overstating equivalence;
11. test/CI/coverage status;
12. repository layout and contributor workflow;
13. limitations expressed as present boundaries, not deferred promises;
14. the resume bullet from the product objective, updated with measured results.

The demo sequence is fixed: import pinned Alpine rootfs; run an isolated shell command; inspect namespace and cgroup state; demonstrate memory/CPU enforcement; start a detached HTTP container with a published port; call it from the workstation through IAP; exec into it; show blocked capability/seccomp probes; display stats/logs; stop/remove it; prove all resources are gone.

## 11. Planning log

- 2026-07-12: Initial authoritative plan created before any implementation, as required by the objective.
- 2026-07-12: Current web research and live environment discovery completed. Locked systemd delegated cgroups, raw `clone3`, shifted user mapping, overlay/pivot-root filesystem, seccomp/capability model, rtnetlink+nftables networking, Alpine 3.24.1 fixture, Ubuntu 24.04 parity, `us-west1-a`, IAP-only final access, transient Cloud NAT, in-place N2 benchmarking, reproducible Debian packaging, and exact Terraform/provider families. Planning gate closed; Stage 0 implementation authorized.
- 2026-07-12: Operator explicitly authorized automatic in-place escalation from `e2-micro` through the smallest sufficient size up to `n2-standard-16` whenever the free-tier VM constrains development, plus deliberate N2 stress testing. Final cleanup remains non-negotiable: exactly one running `e2-micro`, with all other development/load resources deleted.
- 2026-07-12: Live billing-account discovery showed the account currency is TRY, so the API cannot accept a USD-denominated budget. Locked the budget at TRY 470, approximately USD 10 at the observed 46.98 USD/TRY rate; threshold percentages remain unchanged.
- 2026-07-12: Stage 2 passed locally and live with systemd-delegated cgroups v2, exact limit readback, JSON statistics, CPU throttling, memory OOM accounting, PID exhaustion, signal cleanup, and a zero-orphan GCP inspection. The e2-micro remained healthy, so no temporary resize was needed.
- 2026-07-12: Stage 3 passed locally and live with durable lifecycle state, name/prefix resolution, namespace-preserving exec, root-only shim control, pidfd identity checks, graceful and forced stop, persistent overlays, concurrent conflict handling, shim/CLI crash recovery, gc/restart, and zero-orphan GCP inspection. The e2-micro remained healthy.

## 12. Stage status

| Stage | Version | Status | Local | GitHub/CI | GCP deploy | Live proof |
|---|---:|---|---|---|---|---|
| 0. Blueprint/foundation | bootstrap | Complete | GCC+Clang+Terraform PASS | CI+CodeQL PASS | Private e2-micro exact artifact PASS | `docs/proofs/stage-00-foundation/` |
| 1. Isolation/rootfs | v0.1 | Complete | GCC+Clang+privileged runtime PASS | CI+CodeQL PASS | Exact artifact on private e2-micro PASS | `docs/proofs/stage-01-isolation-rootfs/` |
| 2. Resources/stats | v0.2 | Complete | GCC+Clang+cgroup integration PASS | CI+CodeQL PASS | Exact artifact on private e2-micro PASS | `docs/proofs/stage-02-resources-stats/` |
| 3. Lifecycle/recovery | v0.3 | Complete | GCC+Clang+lifecycle/recovery PASS | CI+CodeQL PASS | Exact artifact on private e2-micro PASS | `docs/proofs/stage-03-lifecycle-recovery/` |
| 4. Networking | v0.4 | Complete | PASS | PASS | PASS | `docs/proofs/stage-04-networking/` |
| 5. Security | v0.5 | Not started | — | — | — | — |
| 6. Reliability/operations | v0.6 | Not started | — | — | — | — |
| 7. Release/demo | v1.0 | Not started | — | — | — | — |
