# Stage 4 — bridge networking and port publishing

Result: **PASS**

Candidate `c63747eb4704ab7bd227aee800034be50fe74d68` implements the complete Stage 4
boundary: rtnetlink bridge/veth lifecycle, serialized IPAM, bridge and none modes,
generated guest identity and resolver files, nftables default-deny forwarding,
masquerade, TCP/UDP publication, collision checks, and ownership-scoped cleanup.

The exact Debian package was deployed to the private GCP VM. Live proof covered outbound
DNS/HTTP through temporary Terraform Cloud NAT, TCP and UDP publishing, network-none
isolation, stale-shim reconciliation, lease reuse, and an authenticated workstation HTTP
request through IAP to a container-published port. Cloud NAT was then destroyed and the
final inventory returned to one private running e2-micro with no per-container resources.

Stage 5 security-hardening behavior is not included. Proof is sanitized and contains no
billing identifiers, credentials, external IPs, or operator filesystem paths.
