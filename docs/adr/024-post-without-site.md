# ADR-024. Desktop post when the site is unreachable

[Русская версия](../ru/adr/024-post-without-site.md)

**Status** - Accepted. Emergency live video directly from cameras is an option that is off by default.

**Context.** The site is the single authority for the Bacuri VMS desktop application: rights, sessions, recording, archive, events, alarms, layouts and audit live on the site, and the client holds no data of its own that counts as the truth ([ADR-023](023-client-architecture.md)). After a network break or a restart of the site the client restores the post and checks the rights again. During a longer outage the operator has no live video, which is the moment a security post needs it most. A post that keeps working on its own copy of rights or recordings would create a second truth and keep revoked access working.

**Options.**

| Option | Assessment |
| --- | --- |
| Wait for the site | Simple and safe. The operator has no video until the site returns |
| Reconnect to another node of the installation | Keeps the full function and the rights checks of the site. Needs an installation with more than one node |
| Emergency live video directly from cameras | Live video during an outage of a single-node site. Needs camera access on the post and rights that hold without the site |
| Local copy of rights, recording and archive on the post | Rejected: a second truth, revoked access keeps working, recording on the post is outside the guarantees of the archive |
| Wall composed on the server and streamed to thin posts | Rejected: higher latency for PTZ and seeks, a server GPU per post, and an overloaded server degrades every post at once |

**Decision.** The primary path is reconnection to another node. Emergency live video is an option that an administrator enables explicitly.

| Part | Decision |
| --- | --- |
| Addresses of the installation | The session response lists the client addresses of every node of the installation, and a session is valid on each of them. After a lost connection the client tries the current node, then the other nodes, and restores the post the same way as after a restart: layouts, alarms, archive position and focus, with the rights checked again. The post shows the outage and the node in use |
| Enabling the emergency mode | Off by default. An administrator enables it for the site, for named posts and for camera groups per role |
| Emergency grant | While connected, the site issues a grant to the post for the signed-in user. The grant is signed by the site and bound to a post key held in the key store of the operating system. Its validity is set by the administrator up to 12 hours, and the client renews it while connected. It lists the cameras and profiles the user may watch live, their privacy masks and the sound right |
| Camera access | For the emergency mode the site creates a separate view-only account on each camera and passes its credentials in the grant, encrypted to the post key. Camera administrator credentials never reach the post. A camera without a separate view-only account is not included in the grant |
| Entering the mode | The mode is offered when the site and every node have been unreachable for 30 s, and it starts after the operator confirms it. A new sign-in without the site is not possible |
| Work in the mode | Live video of the granted cameras only. The client applies the privacy masks before the first frame is drawn, and a camera whose mask the client cannot apply is not shown. Archive, recording, PTZ, export and alarm handling are not available. A persistent banner states that the site is unreachable and that Bacuri VMS is not recording |
| Journal | Every view is written to a local append-only journal, chained by hashes and signed with the post key. When the site returns, the client sends the journal to the site audit and the site verifies the chain |
| Leaving the mode | When the site or a node returns, the client closes the direct streams and returns to the site path with the rights checked again. The grant ends at its expiry, at sign-out or with a revocation the site delivers when it returns. Revoking a post or a user rotates the view-only accounts on the cameras |

**Rationale and consequences.** Reconnection to another node keeps one authority and the full function and relies on the redundancy of the installation. The emergency mode covers a single-node site without making the post a second authority: it shows live video only, under the rights the site issued in advance, and its use is accounted for in the site audit. A revocation made during the outage cannot reach the post, so the validity of the grant bounds how long a revoked user can still watch, and the administrator chooses it with that in mind. The view-only accounts add a camera configuration task for the site and a check of the camera capability. Video of the outage is in the archive only if the camera records to its own storage and the site fetches it later by backfill. [Client architecture](023-client-architecture.md) · [Camera capabilities](012-camera-capabilities.md) · [API contracts and sessions](001-api-contracts.md)

**Verification.** The parity register scenarios `workstation-site-failover` and `workstation-emergency-live`. Stopping the active node moves the post to another node within the failover budget of the installation plus 5 s with the rights checked again. Stopping every node with the mode off shows the outage only. With the mode on, the granted cameras show live video with masks and no other function is available, and the site accepts the journal after it returns. An expired grant, a revoked post, a camera without a view-only account and a mask the client cannot apply each leave the camera hidden.
