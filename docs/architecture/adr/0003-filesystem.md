# ADR 0003: Immutable images and overlay root

Status: accepted. Securely imported rootfs archives become immutable digest-addressed lower directories. Containers use private overlay upper/work directories and `pivot_root`, never plain `chroot`.

