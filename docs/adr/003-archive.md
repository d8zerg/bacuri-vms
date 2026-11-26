# ADR-003. Archive commit and catalog recovery

[Русская версия](../ru/adr/003-archive.md)

**Status** - Accepted.

**Context.** A video block and its catalog row are stored on different state media. A process can stop between publishing the file, committing the journal and the database response.

**Options.** Store media in PostgreSQL, treat the list of files as the catalog, or publish a verifiable block on the volume and register it with retries.

**Decision.** The only owner of the volume syncs the files and the manifest, publishes the block directory by an atomic rename, commits it in SQLite and passes the manifest to PostgreSQL. The key and the content define a repeat. When the volume opens, the owner reconciles the completed blocks, their origin and checksums.

**Rationale.** Writing to a healthy volume continues while the database is temporarily unavailable. The catalog can be rebuilt from the preserved verified manifests.

**Consequences.** A delay between recording and search is possible. Reconciliation reads data from disk. Visible states for pending, damaged and gap intervals are needed. The loss of the only medium loses the video on it.

**Recovery after the loss of the database.** The configuration and the access scopes are restored from an authenticated snapshot. Delivering video as the import proceeds was rejected: some blocks still wait for reconciliation, and an earlier session could have gained access under a different catalog state. The core first commits a ban on archive delivery for the site in PostgreSQL. After checking the fMP4 structure, the origin, the size and the SHA-256 under the volume lock, it publishes the reconciliation results and lifts the ban in one transaction. An interruption leaves the ban in force. The local check at a regular start of the recorder also excludes concurrent reads of the volume under check. Confirmed gap information is kept in SQLite to rebuild the timeline again. The time to open the archive depends on the amount of stored files and the throughput of the medium. [Procedure](../recovery.md)

**Verification and review.** For the "Data and access" and "Operation" requirements, interrupt every commit boundary, lose an acknowledgement, damage a block and recover the catalog. A healthy completed block has exactly one record, the correct frame opens, and the damage is explained. Review for other file system semantics, mandatory redundancy or an unacceptable reconciliation time. [Commit protocol](../domain-model.md#recording-and-reconciliation-on-the-volume) · [Requirements](../quality.md#acceptance-requirements)
