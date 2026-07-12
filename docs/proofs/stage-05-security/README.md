# Stage 5 — security hardening

Result: **PASS**

Candidate `ca48b3c9af6620a3d2fdd534058e0d995e04083e` implements the complete Stage 5
boundary: empty-by-default capabilities, explicit user-namespace-scoped capability grants,
locked securebits, `no_new_privs`, default-deny seccomp, restrictive custom profiles,
read-only root, allowlisted bind mounts, bounded tmpfs, symlink-safe target resolution,
workload FD closure, root-only stored config, and a documented threat model.

The exact Debian package was deployed to the private GCP VM and exercised with the same
adversarial suite used locally. The suite verified negative and positive syscall behavior,
capability state, hostile mount targets, source allowlisting, host-sentinel isolation,
secret-safe inspection, and zero residual runtime resources.

Stage 6 reliability/operations behavior is not included. Proof is sanitized and contains
no billing identifiers, credentials, external IPs, host secrets, or operator paths.
