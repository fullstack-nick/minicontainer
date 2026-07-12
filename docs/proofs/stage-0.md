# Stage 0 Proof

Status: in progress. This file records verified evidence only; remaining GitHub and GCP gates are not yet complete.

## Recovered planning baseline

- `PLAN.md` SHA-256: `8E99CEAB96901E3466DDE19E9A39B361AD834CFDA9BE453DE32AA6D842AC6B6C`
- `docs/research.md` SHA-256: `F48272BB6F5E5090ADBB633671FF0FC3614BECC9F184B3D6759F6468CA3CB986`

## Local build proof

On Ubuntu 24.04 WSL2, both GCC 13.3.0 and Clang 18.1.3 configured and built the strict-warning C17 skeleton. Each compiler passed the `version` and `info` CTest cases.

Observed output:

```json
{"name":"minicontainer","version":"0.0.1"}
{"linux":true,"cgroup_version":2,"ready":true}
```

## Release and infrastructure validation

- A Debian package was built twice from commit `1a141f03e19aa7c9eaaef3b1e81c0f2f060e8fda`; both builds passed CTest and produced the same SHA-256.
- Reproducible artifact SHA-256: `12A344369E57EB50E0D21A851A9C4A7C6C0A10FBD4E0861882318C872FF6CAA0`.
- SPDX JSON SBOM generation completed with Syft 1.46.0.
- Terraform 1.15 initialized with locked Google provider 7.39.0 and `terraform validate` passed.
- The GCP mutation guard passed with the expected active account/project and rejected a deliberately wrong account.
- A privileged WSL2 capability audit passed for systemd PID 1, cgroups v2, user/mount/network namespaces, overlayfs, libseccomp, libsystemd, transient systemd delegation, and nftables transaction parsing.

## Current GCP gate

Project `minicontainer-r7m5o9ld` was created successfully on 2026-07-12. Billing linkage was rejected with `Cloud billing quota exceeded`; consequently Compute Engine API activation, Terraform apply, deployment, and live host proof remain incomplete. No VM or other billable project resource was created.

Read-only billing inventory showed five existing linked projects, which accounts for the quota. MiniContainer does not detach or modify billing for those unrelated systems.

## GitHub verification

- Public repository: `https://github.com/fullstack-nick/minicontainer`
- CI on `1a141f03e19aa7c9eaaef3b1e81c0f2f060e8fda`: `https://github.com/fullstack-nick/minicontainer/actions/runs/29190779453` — PASS
- CodeQL on `1a141f03e19aa7c9eaaef3b1e81c0f2f060e8fda`: `https://github.com/fullstack-nick/minicontainer/actions/runs/29190779452` — PASS

Stage 0 verdict: **OPEN** pending billing linkage, Terraform apply/idempotence, exact-artifact deployment, IAP live calls, host inspection, and final inventory proof.
