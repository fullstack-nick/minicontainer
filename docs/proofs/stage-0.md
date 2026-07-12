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

