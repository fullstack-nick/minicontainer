# ADR 0005: Rtnetlink and nftables networking

Status: accepted. MiniContainer configures bridge/veth/routes with rtnetlink and owns one isolated nftables table for NAT and published ports. Generated rules never pass through a shell.

