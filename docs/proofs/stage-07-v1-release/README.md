# v1.0 release proof

Candidate commit: `c2d811e815dff90e3b6c076cc6a703d4c23a15fc`

Runtime package SHA-256: `bc224044d9ddba657c205fe8e15f6b4d73e96357ca03c5152a20a4fc80acf8b9`

Debug package SHA-256: `3f36ab61456470806549c2a33a471246218a656ed5ae545d2c157b5ce7f7216a`

## Local release gates

- GCC build and CTest: PASS.
- ASan and UBSan: PASS.
- Valgrind owned-descriptor and heap checks: PASS.
- clang-tidy analyzer checks: PASS.
- cppcheck warning, performance, and portability checks: PASS.
- Two clean release builds produced the same runtime package digest: PASS.
- Runtime package, detached debug symbols, SHA-256 files, SPDX JSON SBOM, and build
  manifest generated: PASS.
- Detached debug files are unstripped ELF objects and their two GNU build IDs exactly
  match the stripped runtime and shim binaries: PASS.
- The checksum-verified debug-symbol package was installed beside runtime version `1.0.0`
  on the live `e2-micro`: PASS.
- Gitleaks 8.30.1 scanned all 43 Git commits and the release artifact directory: no
  leaks found.

## GitHub gates

- CI run `29200214887`: PASS.
- CodeQL run `29200214903`: PASS.

Both runs tested candidate commit `c2d811e815dff90e3b6c076cc6a703d4c23a15fc`.

## N2 benchmark and stress gate

The sole private VM was stopped, resized in place from `e2-micro` to
`n2-standard-16`, tested, stopped, and restored to `e2-micro`. No external IP, NAT, or
second instance was created.

The installed runtime reported version `1.0.0`, candidate commit `c2d811e...`, and the
runtime package matched SHA-256 `bc224044...acf8b9` before execution.

- benchmark suite: PASS; machine-readable results are in `benchmark-n2.json`;
- 200 sequential run/remove cycles: PASS;
- eight-container concurrent start: PASS;
- 20-way exec/stop race: PASS;
- three memory-OOM storms: PASS;
- 3 MiB log rotation and tail continuity: PASS;
- final reconciliation count: zero;
- final container inventory: empty.

## e2-micro soak and final inventory

The exact v1.0 candidate completed a 1,800-second soak with 30 probes, zero failures,
2,113,536 bytes maximum workload memory, zero kernel findings, and systemd exit status 0.
The machine-readable result is in `soak-e2-micro.json`.

A final HTTP container exposed its declared port only on the private VM. A request from the
Windows workstation traversed an authenticated IAP TCP tunnel and returned HTTP 200 with
body `MINICONTAINER_HTTP`. Live inspection showed the candidate commit, seven distinct
namespace inodes, configured cgroup limits, healthy statistics, and `NETWORK_READY` logs.

After removal and garbage collection:

- container inventory: empty;
- container-owned veth interfaces: none;
- container-owned nftables rules: none (only the base runtime table remained);
- container mounts: none;
- container systemd cgroups: none;
- Terraform plan: no changes;
- Compute Engine inventory: exactly one running private `e2-micro` named
  `minicontainer-vm`;
- other instances, external IPs, and Cloud Routers/NAT: none.

Raw transcripts containing operator or cloud-account data remain outside Git; committed
proof contains no credentials, billing identifiers, personal IP addresses, or operator
filesystem paths.
