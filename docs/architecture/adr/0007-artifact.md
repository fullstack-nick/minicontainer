# ADR 0007: Exact Debian deployment artifact

Status: accepted. CI/local verification produces one Debian package with embedded Git commit, checksum, build manifest, and SBOM. GCP receives that exact artifact over IAP; the VM never rebuilds source.

