# Roll back

1. Identify the last proven package by tag, embedded commit, and SHA-256.
2. Upload that exact `.deb` through IAP and run `sudo dpkg -i PACKAGE`.
3. Verify `minicontainer version --json` and the uploaded package digest.
4. Run `sudo minicontainer gc`, then a create/start/exec/stop/remove smoke test.
5. Preserve current `/var/lib/minicontainer` state. If a future schema is incompatible,
   stop and restore a state backup made before upgrade; never edit JSON records by hand.

Roll forward by reinstalling the newer exact package and repeating the same checks.
