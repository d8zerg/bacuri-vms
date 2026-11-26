# ADR-001. API contracts, sessions and access of Bacuri VMS

[Русская версия](../ru/adr/001-api-contracts.md)

**Status** - Accepted.

**Context.** An administrator needs a configuration to be applied once when a request is repeated. An operator must open a permitted recording and lose access to delivery when the permission is revoked, including an already open connection. The site works in an isolated network.

**Options considered.** A local server session or a self-contained token in the browser. For media - checking the permission only at opening, at every frame, or a short renewable permit. For browser notifications - periodic polling, or a consistent snapshot and a subscription with a cursor.

## Decision

The browser talks to one HTTPS origin. REST uses the `/api/v1` prefix, the feed uses WSS, media delivery uses WebRTC or HTTPS depending on the mode. Control calls between processes use the `bacuri.vms.v1` package. On one node gRPC listens on protected Unix sockets with separate system identities of the processes. Access to a socket is limited to the calling service. Transport between nodes requires mTLS with identity and destination checks. [Architecture, security and actors](../architecture.md#10-security-and-actors)

A local server session revokes permissions and changes roles at the next check. A self-contained token valid until its expiry would need a separate revocation mechanism. For active delivery a permit lasts up to 5 seconds. Checking every frame would depend on the latency of the core and the availability of the permission store. A short permit bounds the revocation delay and keeps a separate media data path.

**Rationale and consequences.** One HTTPS origin ties sign-in, audit, search and delivery to one subject. A snapshot with a cursor lets the feed recover and requires storing a bounded history. When the core is unavailable, new viewing is unavailable and viewing that is already open is limited by the rest of its permit. Repeating a write and updating after a revision conflict require the owner of the operation to store its result atomically.

**Verification and review.** The "Data and access" requirement is checked by the role matrix, request repetition, feed recovery and a network measurement of active delivery stopping within 5 s after revocation. Review when an external IdP, delivery between nodes or a new permission model is introduced, or when the revocation deadline cannot be held under mixed load. [Release requirements](../quality.md#acceptance-requirements)

## REST and camera configuration

OpenAPI fixes the requests, responses, conditional updates and error formats. UUIDs are passed as lowercase strings. Time is passed in UTC with the `Z` suffix, intervals are half-open. Revisions in the signed int64 range are encoded as decimal strings. An error contains the HTTP status, an application `code` and a `request_id` for diagnostics. Responses with restricted data get `Cache-Control: no-store`. [OpenAPI](../../contracts/v1/openapi.yaml) · [HTTP If-Match](https://www.rfc-editor.org/rfc/rfc9110.html#name-if-match) · [Problem Details](https://www.rfc-editor.org/rfc/rfc9457.html)

Registering a camera creates an observation point or assigns an explicitly chosen point of the site. An update keeps the identity of the point. After saving, the desired revision is visible immediately. The applied revision and the rejection reason come from the result of the recording node. `ETag` covers the whole camera representation. A change requires an exact `If-Match`. A stale value returns `412` with the current representation and its `ETag`, a missing header returns `428`.

The `Idempotency-Key` is registered atomically with the camera creation and the audit. The key scope includes the site, the actor and the operation. Within the 24-hour replay window an equal normalized request returns the original result. A changed request gets `409 idempotency_conflict`. A protected fingerprint of the request is stored for the comparison. The current permissions are checked before the stored result is replayed. Creating a media session uses a separate key scope bound to the browser session with a replay period of up to 5 minutes. A replay keeps the original delivery term. A closed session needs a new request with a new key.

Search is limited to a one-hour window and 1000 items per page. The cursor is opaque and bound to the permission scope, the parameters and the search position. The timeline distinguishes `recorded` with a confirmed or estimated timestamp, a confirmed `gap`, a known lag `pending_catalog` and `unavailable` with a reason. The absence of a recording in the catalog by itself gives unknown coverage. A catalog or authorization dependency that the response needs gives `503` when it fails.

## Local session and access rules

The optional corporate sign-in uses the site server as an OIDC client of Keycloak. The browser gets a Bacuri session cookie. The server checks the ID Token signature, the issuer, the audience, the expiry and the nonce. The pair of issuer and subject is bound to a local account, the administrator role requires a configured acr value. Renewing the session requires a positive answer from Keycloak. Local roles, privacy and permissions are checked at every video request. [Access management](../ui.md#access-management) · [Client contract](../release-register.md#sign-in-through-the-site)

**Target sign-in contract for administrators.** For an autonomous site, a local sign-in with mandatory MFA and WebAuthn with user verification, available without the internet, is chosen. The built-in method allows the site to be maintained without a corporate IdP. It requires an initial trusted key enrollment and a recovery procedure. A password alone does not give an administrator session. The rule also applies to raising the role within an active session. The server checks both factors before granting the permissions.

At installation, provide the enrollment of the first authenticator during local setup and automatic checks of registration and sign-in. Check the one-time challenge, the origin, the RP ID and the user verification flag. The loss of a key is checked together with access recovery, revocation of the lost key and the previous sessions, and the audit of the actions. At release, repeat both series on the signed kit. Review the enrollment and recovery methods when an external IdP is connected. [Security criteria](../quality.md#release-candidate-security)

A successful sign-in creates an opaque random identifier, replaces the previous browser identifier and stores a server record. Passwords of local users are hashed with Argon2id. The sign-in error is the same for an unknown user and a wrong password. Sign-in attempts are rate limited. The `__Host-bacuri_session` cookie is sent with `Secure`, `HttpOnly`, `SameSite=Strict` and `Path=/`. The maximum session lifetime is 8 hours, the idle limit is 30 minutes. Background delivery checks and WebSocket pings leave the user activity time unchanged. [Argon2](https://www.rfc-editor.org/rfc/rfc9106.html)

Sign-in checks an exact `Origin` match and accepts JSON. For the other changing HTTP operations, `Origin` and the session-bound `X-CSRF-Token` are checked. The client keeps the token in memory. Changing or ending a session closes its media sessions. Access is denied by default. Before an action the server checks the current role, the site, the point and, for the archive, the interval.

The local site defines the `administrator` and `operator` roles. The administrator adds cameras. For a separate auditor role, the access rights to the log, the read API and the work screen are not defined in the current plan. The server keeps the audit of configuration changes.

| Operation and resource | Administrator of the own site | Operator with a permission for the point | Operator outside the point scope |
| --- | --- | --- | --- |
| Point list and alarm snapshot | Visible resources of the site | Permitted resources only | The resource is missing from the list |
| Reading the registry and changing a camera | Allowed on the site | `403 forbidden` | `403 forbidden` |
| Search and media session creation for a point | Allowed on the site | Allowed for the point | `404 not_found` |
| WSS feed | Events of the site | Events of the permitted points | Events of the point are missing |
| Live video and archive parts | Only the own active media session and its scope | Only the own active media session and its scope | `404 not_found` |

Reading another user's resource or using another user's media session gets `404`. A missing or expired browser session gets `401`. A closed media session gets `410` when its owner requests it while the browser session is active. `DELETE` revokes only the own media session and returns the same `204` for a missing, closed or foreign session. When the permission check is unavailable, metadata requests and requests for new delivery get `503 authorization_unavailable`. [Response examples](../../tests/contracts/rest.yaml)

## Media session lifecycle

A media session binds the browser session, the site, the point and the mode. Live video opens the current secondary stream, the archive is limited to the requested interval. The term is up to 5 minutes within the remaining term of the browser session. The HTTPS entry passes the internal identity of the browser session over a trusted channel. A media session identifier in a URL gives no permission by itself.

Live video uses a complete SDP offer and answer with the gathered ICE candidates. Media delivery checks the scope when live video opens, when the manifest is requested and when each part is read. For the archive, the manifest contains the permitted initialization and media parts. The server chooses the decoding point and delivers only parts within the permitted interval. Going beyond its bounds requires a separate media session.

One live session allows one successful negotiation. To reconnect, the client creates a new session. The delivery process serves up to 16 WebRTC connections at a time, including those still being established. 5 seconds are allowed from the start of negotiation to the established connection. The SDP limit is 64 KiB. The browser receives one H.264 video track over DTLS-SRTP.

The recording node publishes the secondary profile through `PreviewSource.Subscribe` on a separate Unix socket. At most 16 subscriptions. For each viewer, the queue on the recording node and the input queue of the delivery process are limited to 8 frames and 1 MiB each. After an overflow, delivery resumes from a keyframe. The main profile uses its own recording queue.

- Before delivery starts, the process calls `AuthorizeDelivery` with the actual point, mode and interval. The permit is measured on the monotonic clock from the start of the call. Its duration is limited to 5 seconds and the remaining terms of both sessions.
- During delivery the check is repeated at least once every 2 seconds with a call deadline of up to 1 second. An explicit refusal or the expiry of the permit ends delivery and drops the data waiting to be sent. A dependency failure keeps only the rest of the permit already granted.
- Revocation of new access takes effect when the permission change is committed. Active delivery stops when the revocation is detected or no later than 5 seconds after the commit. When the WebRTC connection ends, the process calls `CloseLive` to release the session. When the core is unavailable, the stored negotiation prevents reuse, and the session term limits how long the quota is held.

In the compact configuration the delivery process reads the archive through a read-only volume and gRPC `ArchiveDelivery.ReadPart`. The core maps the part identifier to a block of the media session. The delivery process checks the point, the interval and the SHA-256 against the local manifest. Writes to the HTTPS socket are limited to 32 KiB portions. Even when the client buffer is full, the write deadline comes no later than the next permit check.

The interval of one request is limited to one hour. A browser can hold up to 16 active media sessions at a time. Part identifiers belong to the media session and survive a restart. Search reconciles the catalog with the intervals the recording node has yet to register. When the node is unavailable, the registered recordings stay visible and the coverage without confirmation gets `unknown_coverage`. [Search and playback API](../../contracts/v1/openapi.yaml)

## Alarm feed

`GET /sites/{siteId}/alerts` returns the last 100 visible alarms and a consistent cursor from one snapshot of the state. Alarms follow the publication order and include the quality of the observation time. WSS uses the cookie of the active session and an exact `Origin`. The schema fixes the path, the subprotocol and the frame formats. As the first message within 5 seconds the client sends `subscribe` with the cursor of the snapshot or of the last processed frame. [Feed schema](../../contracts/v1/alerts.schema.yaml) · [Frame examples](../../tests/contracts/alerts.yaml) · [WebSocket](https://www.rfc-editor.org/rfc/rfc6455.html)

The server sends `upsert` and `remove` in publication order. A removal also reflects eviction from the window of the last 100 alarms. The client applies updates by alarm identifier and moves the cursor after a successful application. Resumption starts strictly after the accepted cursor. The cursor is bound to the site, the subject and the revision of the access scope. When the history expires, the scope changes or the queue overflows, the server ends the subscription with `resync_required`. When the queue cannot hold a control frame, the connection closes with code `1013`. In both cases the client gets a fresh snapshot. When the session is revoked, the server closes the connection with code `1008`, when the permission check is unavailable - with `1011`. The permit for an active subscription has the same maximum revocation delay as active media delivery.

## Interprocess operations

| Call | Caller | Acknowledgement condition |
| --- | --- | --- |
| `RecorderControl.ApplyConfiguration` | Core -> assigned recording node | The node stores the decision for the revision of the full snapshot. A repeat gets the stored result, a stale revision keeps the active configuration |
| `ArchiveCatalog.RegisterBlock` | Assigned recording node -> core | The catalog transaction is committed. A repeat of an equal manifest gets `REGISTRATION_STATE_REPLAYED`, another manifest with the same key gets `ALREADY_EXISTS` |
| `ObservationIngest.SubmitObservation` | Permitted source -> core | The observation record is committed by site, source and ID. An equal repeat keeps the first acceptance |
| `MediaAuthorization.AuthorizeDelivery` | Media delivery process -> core | Owner, term, current permissions, actual resource, mode and interval are checked. When the check is unavailable, there is no fresh permit |

The owning side checks the destination of the calling service and the scope before processing. The call sets a deadline. A transport repeat of a write operation uses its original key or revision. An invalid request returns `INVALID_ARGUMENT`, an unauthenticated call - `UNAUTHENTICATED`, insufficient permissions - `PERMISSION_DENIED`. A missing available resource returns `NOT_FOUND`, a violated state - `FAILED_PRECONDITION`. A successful response of a write operation follows the state commit. [Protobuf](../../contracts/v1/control.proto) · [gRPC examples](../../tests/contracts/grpc.yaml) · [gRPC codes](https://grpc.io/docs/guides/status-codes/)

The camera configuration snapshot includes `channel_id`, `main_profile_id` and `binding_revision` for the origin of an archive block. In `RegisterBlock`, the `availability` field passes the result of the local reconciliation, `verification_generation` gives the generation of the owner that performed it. An empty generation field uses the value of the manifest. The catalog checks the site and the node and, on a repeat, compares the immutable manifest before updating the availability.

## Observation intake and alarm archive

The acknowledgement to the source guarantees that the observation and its publication are stored. Sending a notification separately after the write leaves an interval in which a stopped process loses the notification. A transactional outbox lets the feed recover from the last acknowledged position. Its cost is storing a bounded history and switching to a new snapshot when a client lags behind. Distributed subscribers or measured PostgreSQL saturation require a review of the delivery method. The alarm delivery tests check the behavior on repeats and disconnects.

In the compact configuration the core accepts `SubmitObservation` through a dedicated Unix socket. The socket mount and the system permissions limit the calling service. The core checks the site and the source against the destination of the socket. The `motion` and `heartbeat` types accept an attribute object of up to 16 KiB. Motion requires an existing binding revision. One motion forms one alarm, and a status signal is stored in the observation log.

The replay key consists of the site, the source and the observation ID. An equal repeat keeps the first acceptance. A repeat with different content gets `ALREADY_EXISTS`. The observation, the alarm and the outbox position are inserted in one transaction. The counter row of the site orders the publications by commit. The last 1024 positions are kept for repeated delivery. The snapshot and its cursor are read in one PostgreSQL transaction with `REPEATABLE READ` isolation. [PostgreSQL transaction isolation](https://www.postgresql.org/docs/17/transaction-iso.html)

The cursor is bound to the site, the user, the browser session and the permission revision. On eviction from the window the client gets `remove`, then `upsert`. A disconnect between these frames keeps the resumption point. Up to 16 frames are sent per database query. When an active client lags by more than 64 visible publications, it needs a new snapshot. Writing a batch to the socket is limited to two seconds from the start of the permission check.

`GET /sites/{siteId}/alerts/{alertId}/archive` checks access to the point at every request. The event carries the original timestamp and, when a confirmed binding to UTC exists, the epoch and the uncertainty. Without a binding the core keeps the original timestamp and uses the receive time for the feed. The context interval is 15 seconds before and after the alarm. A ready link returns the block bounds and the frame position when the binding revision matches and the uncertainty interval of the observation and the recording fits into one block. The other states describe a gap, a pending catalog, unknown coverage or uncertain time. [REST API](../../contracts/v1/openapi.yaml)

## Secrets and audit

An RTSP URL is checked against the permitted protocols and the addresses of the stand. The URL contains the scheme, the address and an optional path. Credentials are accepted in a separate field. The registry stores them as AEAD ciphertext with a unique nonce and a key version. The encryption context binds the site, the camera and the secret revision. The key is stored separately from PostgreSQL, readable only by the permitted service. Connection parameters are passed to the assigned recording node over a protected interprocess channel. The local applied configuration keeps the secrets encrypted with a separate node key.

API responses contain only a flag that a secret exists. The audit and the log exclude passwords, RTSP URL values with secrets, cookies, tokens and SDP. A configuration change and its audit record are committed in one transaction. The audit includes the actor, the action, the site, the resource, the revisions before and after, the result and the request ID. When the mandatory audit is unavailable, the configuration change is not acknowledged.

## Client generation and verification

`task api:types` generates the request and response types in `build/contracts/api.d.ts` and checks them with TypeScript. The current live, archive and alarm pages call `fetch` through the common session module. For the single web client, `openapi-fetch` with the `paths` type from the generated file and the `/api/v1` base is provided. The session wrapper adds the CSRF header to changing operations. The test responses fit the interface skeleton before the server is connected. [TypeScript generator](https://openapi-ts.dev/cli) · [Typed client](https://openapi-ts.dev/openapi-fetch/) · [Test responses](../../tests/contracts/rest.yaml)

`task api:check` checks the OpenAPI description, the WSS schema and the Protobuf compilation. REST examples are compared with the declared operations and HTTP statuses, WSS frames with their direction, gRPC examples with the message types of the call. The tests reject incompatible media request modes, misdeclared time precision and secrets in a camera response. Compatible changes add optional fields and keep the meaning of the existing ones. Numbers of removed Protobuf fields are reserved. Changes of required fields, codes and semantics are published under a new version number. [Contract checks](../../tests/contracts/check.py)

Background requests that check the session, the list of permitted points, the feed state and the manifest check the lifetime and idle limits without extending `last_active`. This lets the client hide revoked access while the window is open. An end-to-end test leaves an idle session at the timeout boundary and checks the return to the sign-in form while polling continues. [Client verification](../test-profile.md#single-web-client-acceptance)

## Alarm handling

The card `GET /sites/{siteId}/alerts/{alertId}/work` is available to a person with the right to view the point. The server returns the state, the revision, the name of the owner, the result, the available actions and up to 25 last successful commands. Taking an alarm moves it from `new` to `in_progress` and makes the initiator its owner. Only the owner can record the result or move the alarm to `closed` after the check. A command contains the action, the current revision of the card and a non-empty reason of up to 1024 characters.

`POST` to the card address accepts a UUID in `Idempotency-Key`. The server changes the state and adds a history record in one transaction. An authorized repeat with the same key and content returns the original event identifier and revision. Changed content or a stale revision gives HTTP 409. Going outside the visibility scope of the point gives HTTP 404, an attempt to change an alarm taken by someone else gives HTTP 403. The feed reports handling changes of visible points with `resync_required` with the reason `work_changed`.

A user with a permission for the point issues a delegation through `POST /sites/{siteId}/alert-delegations`. The token is limited to the site, the point, one action and a term from 1 to 86400 seconds. The plain value is returned at issue. The service passes it in `Authorization: Bearer` on the command route. The service gets the alarm identifier and the expected revision from the initiator. Every command checks the current permissions of the initiator, the owner of the alarm, the term and the revocation of the delegation. The user who issued a delegation or the site administrator can revoke it with `DELETE`.

`POST /sites/{siteId}/alert-audit/search` accepts an exact alarm identifier, a literal case-insensitive substring of the result or the reason and a `before` cursor. The response contains up to 50 records from newest to oldest. An operator sees the actions performed on the available points and the own refusals on these points. An administrator sees the log of the site. A record contains the server time in UTC, the initiator, the actor type, the action and the outcome. For delegations the identifier, the service name, the permitted action and the term are kept, for a successful command the state transition, the revision and the owner. The log is stored in PostgreSQL. A trigger rejects changes, deletions and truncation of the records.
