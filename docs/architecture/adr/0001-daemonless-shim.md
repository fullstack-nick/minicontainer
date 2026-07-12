# ADR 0001: Daemonless per-container shim

Status: accepted. The CLI starts one small shim per container; no global privileged daemon or setuid binary is used. The shim owns PID lifecycle, control socket, logs, and idempotent cleanup.

