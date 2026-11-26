# ADR-016. Maintenance of system packages

[Русская версия](../ru/adr/016-system-backports.md)

**Status** - Accepted.

**Context.** A pinned Debian Trixie snapshot provides a repeatable build. Between the publication of a fix by the supplier and its inclusion in the stable distribution, a critical match can remain. The regression needs a verifiable fix of the problem, a preserved ABI and the ability to repeat the decision for a specific kit offline.

**Options.** Moving to another distribution branch requires qualifying the ABI of all services again. A temporary exception requires a proven compensating measure. Backporting a published fix into the Debian source package keeps the set of interfaces and lets the update be checked separately.

**Decision.** The platform team maintains a local revision only for the used packages that need a fix. Binary packages are built from an exactly specified source version in a signed Debian archive. The changes come from the supplier's release with a SHA-256 check. The build checks that the fix applies and reverts cleanly and keeps the list of build dependencies. When a fixed package exists in the same stable branch, the supplier's package is installed with a signature check. For static Go utilities, the sources, the modules and the compiler image are pinned. In the WebRTC development SDK, the regular headers, the pkg-config metadata and the linker reference from a package of the same version as the runtime library are enough.

**Rationale.** Installing through dpkg keeps the file owner, the version and the regular accounting mechanisms. For each image the automation compares the versions and SHA-256 of the installed files with the build result. A separate scan of the original Debian version keeps all applicable advisories in the report. The status of a fixed advisory is bound to the package, the local version, the specific patch and the verified bytes. An advisory about a missing function needs a check of the installed package content and a check of the runtime. A mismatch fails the check.

**Consequences and review.** The platform team is responsible for receiving new advisories, checking compatibility and carrying the local fix over at every source update. The packages and tools can be exported with the build materials. When a fixed package appears in the pinned branch, move to it, remove the local rule and repeat the scan of the original content and the full regression. Each new CVE is assessed separately. The content of system packages is part of the delivery and the SBOM, and in qualification the evidence is compared with the hash of the signed kit. [Technology maintenance](015-technology-lifecycle.md) · [Build guide](../local-development.md#maintenance-of-system-packages) · [Quality requirements](../quality.md#acceptance-requirements)
