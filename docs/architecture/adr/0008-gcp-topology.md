# ADR 0008: Private free-tier GCE topology

Status: accepted. The final deployment is one private Ubuntu 24.04 `e2-micro` in `us-west1-a`, accessed only through IAP and managed with Terraform plus explicit SSH deployment. Temporary paid resources must be destroyed before a stage passes.

