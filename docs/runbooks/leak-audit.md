# Runtime leak audit

After stopping/removing every test container, require:

- no `minicontainer-shim` process associated with the test state path;
- no `minicontainer-*.scope`, payload cgroup, namespace handle, control socket, or runtime
  directory;
- no `mch*` or `mcc*` veth; `mcbr0` may remain as base runtime state;
- no nftables comment containing a full container ID; base chains/rules may remain;
- no overlay mount or writable upper/work directory for a removed container;
- no unexpected listening socket, log directory, lease, or test workspace.

Run `minicontainer gc` twice before declaring a leak. If the second pass changes anything,
reconciliation is not idempotent and the stage fails.
