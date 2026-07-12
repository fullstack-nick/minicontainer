# MiniContainer

MiniContainer is a daemonless, Docker-like Linux container runtime written in C. Development is underway under the locked architecture and live-verification contract in [PLAN.md](PLAN.md).

The Stage 0 executable currently provides `minicontainer version` and host prerequisite inspection through `minicontainer info`. Namespace, filesystem, cgroup, lifecycle, networking, and security features are delivered stage-by-stage and are not claimed before live GCP proof exists.

## Local build

```bash
cmake --preset dev-gcc
cmake --build --preset dev-gcc
ctest --preset dev-gcc
./build/dev-gcc/minicontainer version
./build/dev-gcc/minicontainer info
```

