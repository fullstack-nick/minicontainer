# ADR 0006: Empty capabilities and allowlist seccomp

Status: accepted. Workloads default to an empty capability set, `no_new_privs`, and architecture-checked seccomp allowlists. The runtime remains an educational same-kernel isolation tool, not a hostile multi-tenant sandbox.

