# Verified development toolchain

Verified on 2026-07-12. Canonical runtime development occurs in Ubuntu 24.04 under WSL2; Windows orchestrates GitHub, GCP, and Terraform.

| Environment | Tool | Version |
|---|---|---|
| Ubuntu | GCC | 13.3.0 |
| Ubuntu | Clang | 18.1.3 |
| Ubuntu | CMake | 3.28.3 |
| Ubuntu | Ninja | 1.11.1 |
| Ubuntu | GDB | 15.1 |
| Ubuntu | Valgrind | 3.22.0 |
| Ubuntu | libseccomp | 2.5.5 |
| Ubuntu | libsystemd | 255 |
| Ubuntu | cmocka | 1.1.7 |
| Ubuntu | Syft | 1.46.0 |
| Windows | Terraform | 1.15.8 |
| Windows | TFLint | 0.63.1 |
| Windows | Git | 2.53.0.windows.2 |
| Windows | GitHub CLI | 2.88.1 |
| Windows | Google Cloud SDK | 574.0.0 |
| Windows | Syft | 1.46.0 |
| Windows | Gitleaks | 8.30.1 |

Exact executable paths are discovered at execution time rather than embedded with workstation-specific home paths in the public repository.
