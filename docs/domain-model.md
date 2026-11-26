# Domain model and storage boundaries of Bacuri VMS

[Русская версия](ru/domain-model.md)

The site owns the resources, the configuration and the working catalog. A recording node owns the completed blocks on its volume and the local commit journal. [Architecture](architecture.md#2-components-and-data-owners)

## Resources and revisions

The compiler detects substituting the identifier of another resource. At the API or storage boundary an adapter parses the external value, and the domain checks the related fields before the operation starts. For review, take a call with several identifiers and an example of an invalid interval. [ADR-009. Domain types](adr/009-domain-types.md)

| Entity | Identity and relation |
| --- | --- |
| Site | Site UUID. The time zone applies to schedules and display |
| Observation point | UUID within the site. The identifier is kept when the camera is replaced |
| Device and channel | A device has its own UUID. A channel belongs to the device and has an index within it |
| Stream profile | Profile identifier and channel. On a local site one main and one secondary profile are chosen per channel |
| Point binding | Site, point and a positive revision. The revision stores the channel and the start of validity in UTC |
| Device configuration | A snapshot has a key of site, device and revision. The current state separately stores the desired and the last applied revisions, the application result and the error code |

Replacing a channel creates the next revision with a later start of validity. A selection by time uses the last revision that started by that moment. Recordings and observations related to a point keep the specific binding revision. This lets the history before and after a replacement be opened on one point and shows the actual source of each block.

Publishing a new binding and an operation on the desired configuration run in a short transaction within the site. For concurrent changes the registry compares the expected revision and stores the next snapshot together with the application state. A failed application leaves the previous confirmed revision available for recording. [C++ resource types](../src/domain/resources.hpp) · [Initial PostgreSQL schema](../storage/postgresql/migrations/0001_initial.sql)

### Model extensions for the first delivery

| Planned entity | Owner and invariant |
| --- | --- |
| Camera capabilities and plan placement | The registry binds an observed capability to the device and its firmware. A position on a plan belongs to a stable point, and replacing the camera keeps the position and access to the history |
| Alarm state and history | The core stores the transitions, the owner and the revision. Two operators on duty cannot both take ownership of one alarm |
| Retention policy | The core stores the term, the volume owner applies the cleanup. A repeat after an interruption keeps the active blocks and reconciles the available intervals with the catalog |
| Export | The operation binds the permissions of the subject, the point, the interval, the origin and the verifiable files. The audit links the obtained material to the original action |

The schemas and contracts of these entities are introduced together with the corresponding operations and migration tests.

## Time and intervals

Normalized time and receive time are stored in UTC as signed integer microseconds since the Unix epoch. The original timestamp is stored separately in microseconds of the source clock. It can be missing for a source that does not send it. Two models apply to source time.

| Time basis | Rule and data |
| --- | --- |
| `source_clock` | UTC is computed from a pair of reference timestamps as the reference UTC plus the difference of the original timestamps. The original timestamp, the UUID of the clock epoch and a non-negative uncertainty estimate in microseconds are stored |
| `reception` | The receive time is taken as the normalized UTC. A transmitted original timestamp is kept. There is no uncertainty estimate and no epoch UUID |

A new clock binding gets another epoch after a clock jump or a reconnection that broke the binding. The model checks for overflow in the projection. Recording and gap intervals are half-open `[begin, end)`, which lets neighboring intervals share a boundary. A known gap carries a reason and a binding revision. A recording with estimated time remains a recording with its own reliability mark. [C++ time model](../src/domain/time.hpp) · [Test vectors](../tests/domain/model.cpp)

## Archive block and observation

The block key includes the site, the block UUID, the recording epoch and the assignment generation. The recording epoch separates assignments after a recovery. The clock epoch describes the time scale of the camera. The commit follows this order.

1. The node publishes the completed block directory with the media and the manifest on the local volume.
2. A local SQLite transaction stores the identifier, the manifest, the relative directory and the commit time. The registration field stays empty until the catalog acknowledges.
3. Registration in PostgreSQL inserts the manifest only for a new key. When the write result is empty, the next SQL statement reads the manifest by this key. An equal manifest confirms a repeat, a changed one gives a conflict. In Read Committed mode a read by a separate statement sees a row committed by a concurrent transaction after the insert started.
4. After the catalog transaction is acknowledged, the node marks the local registration. On a failure between the acknowledgement and the mark, a repeat uses the same key.

The catalog keeps the origin of a block through composite foreign keys. They link the site, the point, the binding revision, the device, the channel and the profile. Queries by point use derived columns of the manifest. The node journal is stored in SQLite with WAL. The writing process sets the FULL synchronous mode for each connection. [Block insert](../storage/postgresql/insert-block.sql) · [Manifest read](../storage/postgresql/read-block-manifest.sql) · [SQLite schema](../storage/sqlite/migrations/0001_initial.sql) · [PostgreSQL isolation](https://www.postgresql.org/docs/17/transaction-iso.html)

The observation key includes the site, the source and a stable observation UUID. A repeat keeps the original timestamps and the first acceptance. A new observation gets its own type revision, origin, attributes and, when present, a reference to the binding revision. The acknowledgement to the source follows the durable insert. The publication of related events uses the same transaction that changes the application state. [Events and commands](architecture.md#6-events-reactions-and-commands)

## Recording and reconciliation on the volume

The node gets the UUIDs of the channel and the main profile and the point binding revision from the applied snapshot. The catalog migration queues the existing assignments for delivery again. Until these fields arrive, the node can restore intake from the old snapshot. Archive recording starts at the first keyframe for which the binding is known.

A block starts with an H.264 keyframe. The next keyframe opens a new block. The node's own writer packs the original access units without re-encoding into `media.mp4` with the initialization, fragments and a random access index. The start of a block uses the UTC estimate of the first frame, the end takes into account the relative PTS and the durations of the recorded frames. The time estimate basis and the uncertainty of the first frame are part of the manifest. One group of pictures is limited to 32 MiB, 900 frames and 15 s. When a limit is reached, the node completes the accumulated block and waits for a keyframe. [Archive format](archive-format.md)

The owner holds an exclusive `flock` lock on the volume directory. The journal stores the site, the node and the volume epoch. Each successful acquisition increases the owner generation. On a local Linux file system the node syncs the media and the manifest, then the temporary directory, publishes it by a rename within the volume and syncs the parent directory. After that SQLite in WAL and FULL mode commits the record. [Volume implementation](../src/archive/volume.cpp)

At startup the node checks the sizes, the fMP4 structure, the SHA-256, the identifiers and the stored manifest. A published verified block without a local row is restored in the journal. `.partial-*` directories stay outside the recording catalog and are reported in diagnostics. For a previously known block the node reports the reason `missing` or `corrupt`. After the core acknowledges, the timeline view shows a `storage_failure` gap for its interval. A healthy block put back passes the same reconciliation at the next start.

The node sends one manifest at a time from SQLite through a separate Unix socket of the core. The call deadline is 3 s, the retry delay grows from 1 to 30 s. When the node lags behind, the queue stays on disk. The acknowledgement is recorded after the PostgreSQL transaction. A repeat compares the whole original manifest. A conflict gets the local status `catalog_rejected`, after which the node continues with the next blocks. The verification generation protects the availability state from a late message of the previous owner.

## Manifest format and schema changes

The manifest describes the block identity, the source, a half-open UTC interval, the time binding and the metadata of `media.mp4`. The media contains the initialization data and the fMP4 block index. The manifest gives the size, the SHA-256 and the track parameters. Identifiers are written as lowercase UUIDs, integer values as decimal integers. The JSON schema defines the structure and the ranges of the fields. The domain model and the catalog check the order of the bounds and the consistency of the receive time.

The manifest version defines the file format independently of the database schema version. When a new format is added, reading stored blocks uses their manifest version. A new database migration gets the next number and is checked on the previous supported schema with data. [Manifest JSON Schema](../storage/archive/block-manifest-v1.schema.json) · [Example](../tests/schema/block-manifest-v1.json)

`task schema:check` checks the manifest and the initial schemas, then applies the PostgreSQL and SQLite migrations. The check confirms the repeated delivery of an existing assignment and that the acknowledgement of a previously registered block is kept. It is part of `task check`.
