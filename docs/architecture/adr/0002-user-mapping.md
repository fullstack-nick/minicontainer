# ADR 0002: Subordinate user mapping

Status: accepted. Every container receives a user namespace backed by the dedicated `minicontainer` subordinate UID/GID range; container root never maps to host root.

