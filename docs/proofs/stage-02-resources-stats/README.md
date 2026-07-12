# Stage 2 — cgroups v2 limits and stats

Result: **PASS**

Candidate `bec0516f87f9070a0454771b1c9e86bafeaa398b` implements delegated cgroups v2,
strict resource parsing, direct payload placement with `CLONE_INTO_CGROUP`, resource state,
and human/JSON statistics. Proof-support commit `dfc9c40fbf2986b7915e9193ceb9a445c90302f4`
adds the compiler-free live fixture path and CodeQL dependency.

The exact candidate Debian package was deployed to the private GCP VM. Live tests proved
limit readback, CPU throttling, memory OOM accounting, PID exhaustion, statistics, signal
handling, and complete scope/cgroup cleanup. No Stage 3 lifecycle behavior is included.

Proof files are sanitized: no billing identifiers, credentials, operator paths, public IPs,
or sensitive environment values are retained.
