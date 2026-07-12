# ADR 0006: Empty capabilities and allowlist seccomp

Status: implemented. Workloads default to empty capability sets, locked securebits,
`no_new_privs`, and an architecture-checked default-deny seccomp allowlist. Explicit
capabilities remain user-namespace scoped. Custom profiles may only narrow the built-in
allowlist. Bind sources require an operator allowlist and mount targets are resolved beneath
the container root with `openat2`; read-only root and bounded tmpfs mounts are supported.
The runtime remains a same-kernel isolation tool, not a hostile multi-tenant sandbox.
