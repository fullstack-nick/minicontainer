# Resume bullet

Built a daemonless Linux container runtime in C using `clone3`/pidfds, seven namespaces,
cgroup v2, overlayfs/`pivot_root`, rtnetlink, nftables, capabilities, and default-deny
seccomp; added crash-safe lifecycle state, package/reboot recovery, reproducible Debian
artifacts, and GCP/IAP proof through 100-container stress, OOM/race/security tests, and
zero-leak audits.
