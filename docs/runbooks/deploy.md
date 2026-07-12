# Deploy

1. Confirm a clean commit and green GCC, Clang, privileged integration, Terraform, and
   CodeQL checks.
2. Run `scripts/build-release.sh`; retain the generated manifest, SHA-256, and SBOM.
3. Run `scripts/gcp-guard.ps1` and require the expected account and project.
4. Run `scripts/deploy.ps1`. It uploads through IAP, installs the exact Debian package,
   and reads back the embedded commit and remote digest.
5. Run `sudo minicontainer gc`, the stage integration suite, and the host leak inventory.

Never rebuild on the VM or attach an external IP. Enable Terraform’s temporary NAT toggle
only for a proof that explicitly requires outbound traffic, and destroy it immediately.
