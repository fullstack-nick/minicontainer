# GCP teardown

1. Stop and remove workloads; run the leak audit.
2. Confirm the active account with `scripts/gcp-guard.ps1`.
3. Apply Terraform with `enable_temporary_nat=false` and require no router/NAT afterward.
4. Use `terraform destroy` only when the entire MiniContainer environment is intentionally
   being retired. Review the plan for the VM, disk, firewall, IAM, service account, network,
   subnet, APIs, and budget resources.
5. Verify with `gcloud compute instances list`, router/NAT lists, disks, addresses, and
   firewall rules. The normal completed-project state is one private running e2-micro; a
   full teardown is zero project resources.
