# Debug

- `minicontainer info --json`: kernel and runtime prerequisites.
- `minicontainer inspect ID`: durable runtime identity and exit state.
- `minicontainer logs --tail 100 ID`: current log; rotated files live beside it as
  `container.log.1` through `.3`.
- `minicontainer stats --no-stream ID`: cgroup CPU, memory, PID, and OOM counters.
- `journalctl -u minicontainer-reconcile.service`: boot reconciliation.
- `systemctl status minicontainer-ID.scope`: live resource scope.
- `nft -a list table inet minicontainer` and `ip -details link`: owned network state.

Set `MC_DEBUG=1` only for a controlled reproduction; its diagnostics may expose host-level
metadata and must not be copied into public proof files without review.
