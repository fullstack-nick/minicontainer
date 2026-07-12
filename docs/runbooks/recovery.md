# Recover after a crash or reboot

`minicontainer-reconcile.service` runs once at boot. Inspect it with
`systemctl status minicontainer-reconcile.service` and
`journalctl -u minicontainer-reconcile.service`.

For manual recovery:

1. Run `sudo minicontainer gc` twice; both runs must succeed and the second must report no
   repairs.
2. Compare `minicontainer ps --all` with shim PIDs, systemd scopes, cgroups, veths, nftables
   comments, runtime directories, and overlay mounts.
3. A stale running record must become stopped; its owned cgroup, interface, rules, socket,
   and transient mounts must disappear.
4. Restart only a container whose stored config is understood and whose bind allowlist is
   still valid.
