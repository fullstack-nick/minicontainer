# ADR 0004: Durable file-backed state

Status: accepted. Versioned JSON state uses registry and per-container locks plus temporary-write, fsync, atomic-rename, and directory-fsync durability. Runtime reconciliation verifies kernel resources before mutation.

