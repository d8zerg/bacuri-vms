# ADR-011. Offline installation and upgrade of Bacuri VMS

[Русская версия](../ru/adr/011-offline-delivery.md)

**Status** - Accepted.

**Context.** On an isolated site, the administrator needs to install and recover a working system with access to the previous archive. The installation kit and its dependencies must be checked before the node is changed.

**Options.** Download packages from a network repository, deliver a set of containers with a runtime, or deliver a signed offline Debian kit with local dependencies and systemd services.

**Compatibility.** A native kit is built separately for each supported Debian release starting with 12 on amd64. All kits of one Bacuri release deliver the same PostgreSQL version, including fixes. The distribution package revisions and the system libraries match the target operating system. The build compares the versions of the server, the client and libpq before signing. The installer compares the target operating system and architecture with the signed metadata before running APT. Supporting the next Debian release requires building a kit and checking it on a clean node.

**Decision.** For a single-node installation, deliver a Debian x86-64 kit with the packages, local interface resources, a list of components and versions (SBOM), and installation and recovery guides. Check the kit signature against a key trusted in advance and the integrity of the packages before applying them. An upgrade checks the available space and the schema compatibility and keeps a backup. For each supported source schema, a way back to a consistent version of the application and the data is defined.

**Rationale.** An offline kit lets the same binaries be installed again on a clean node and the result be checked with the regular tools of the target operating system. Docker Compose remains the development and test environment.

**Consequences.** The team maintains a matrix of operating system versions, keys and upgrade paths. Recovering earlier frames depends on the integrity of the volume named in the profile. Passing an incompatible migration requires a verified copy.

**Verification and review.** Automatically check the installation without the internet, the refusal on tampering, a reboot, an upgrade with data, an interruption, a rollback and a recovery that reads an old and a new frame. In manual acceptance, the administrator repeats maintenance by the guide on the same kit. Review the format when the target operating system, the upgrade model or the durability requirements change. [Operation requirement](../quality.md#acceptance-requirements)
