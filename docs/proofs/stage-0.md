# Stage 0 Proof

Status: in progress. This file records verified evidence only; remaining GitHub and GCP gates are not yet complete.

## Recovered planning baseline

- `PLAN.md` SHA-256: `8E99CEAB96901E3466DDE19E9A39B361AD834CFDA9BE453DE32AA6D842AC6B6C`
- `docs/research.md` SHA-256: `F48272BB6F5E5090ADBB633671FF0FC3614BECC9F184B3D6759F6468CA3CB986`

## Local build proof

On Ubuntu 24.04 WSL2, both GCC 13.3.0 and Clang 18.1.3 configured and built the strict-warning C17 skeleton. Each compiler passed the `version` and `info` CTest cases.

Observed output:

```json
{"name":"minicontainer","version":"0.0.1"}
{"linux":true,"cgroup_version":2,"ready":true}
```

## Release and infrastructure validation

- A Debian package was built from commit `5526755dc1a96b634843c9a4e1cf13f325dfc901` and passed the same CTest suite before packaging.
- Artifact SHA-256: `25DB3B62BBFC96D7968F98A230C5BD87A41CCDFE071A6611277F09EBC4EA90BA`.
- Terraform 1.15 initialized with locked Google provider 7.39.0 and `terraform validate` passed.
- The GCP mutation guard passed with the expected active account/project and rejected a deliberately wrong account.

## Current GCP gate

Project `minicontainer-r7m5o9ld` was created successfully on 2026-07-12. Billing linkage was rejected with `Cloud billing quota exceeded`; consequently Compute Engine API activation, Terraform apply, deployment, and live host proof remain incomplete. No VM or other billable project resource was created.
