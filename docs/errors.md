# Error reference

[Русская версия](ru/errors.md)

HTTP failures carry a catalog code in the `code` field of the Problem response. Its `request_id` matches the `X-Request-Id` header. Service gRPC calls carry the cause in the status message. Map sources and installation jobs report their cause in the `error` field. The web client displays each cause in the selected language.

## Request

The request is malformed, names unknown fields or values outside the contract.

| Code | HTTP status or source | Handling | Who | Meaning |
| --- | --- | --- | --- | --- |
| `invalid_request` | 400 | Change the request | Administrator | The request body, a parameter or a header does not match the API contract. |
| `payload_too_large` | 413 | Change the request | Service | The request body exceeds the size limit for the operation. |
| `headers_too_large` | 431 | Change the request | Service | The request headers exceed the site limit. |
| `expectation_failed` | 417 | Change the request | Service | The server does not support the requested HTTP expectation. |
| `method_not_allowed` | 405 | Change the request | Service | The resource does not accept this HTTP method. |
| `precondition_required` | 428 | Change the request | Service | A change of a revisioned object needs the If-Match header with the read revision. |
| `invalid_georeference` | 400 | Change the request | Administrator | The reference points of a floor plan do not define a valid placement on the map. |
| `camera_selection_expired` | 400 | Complete the named step | Administrator | The selection of a discovered camera is too old. Discover or inspect the camera again. |
| `invalid_catalog` | map state | Change the request | Administrator | The map catalog has an invalid format or invalid region metadata. |
| `invalid_package` | map state | Change the request | Administrator | The uploaded map package could not be read as ZIP or MBTiles. |

## Sign-in

The caller is not signed in or the presented identity is not valid.

| Code | HTTP status or source | Handling | Who | Meaning |
| --- | --- | --- | --- | --- |
| `unauthenticated` | 401 | Complete the named step | Operator | The browser session is missing or ended. Sign in again. |
| `invalid_credentials` | 401 | Complete the named step | Operator | The user name or the password is not correct. |
| `desktop_code_invalid` | 400 | Complete the named step | Operator | The one-time sign-in code of the desktop client is unknown, expired, already used or issued for another verifier or address, or the browser session that issued it ended. Start the sign-in from the desktop client again. |
| `invalid_identity` | 401 | Complete the named step | Operator | The corporate identity provider did not confirm the identity or the required authentication level. |
| `invalid_factor` | 403 | Complete the named step | Operator | The security key did not confirm the sign-in. |
| `enrollment_required` | 403 | Complete the named step | Operator | The account needs a security key before the first sign-in. Use the enrollment code from the administrator. |

## Permissions

The caller is signed in but may not perform the operation.

| Code | HTTP status or source | Handling | Who | Meaning |
| --- | --- | --- | --- | --- |
| `forbidden` | 403 | Change the request | Operator | The role or the grants of the user do not allow the operation. |
| `last_administrator` | 409 | Change the request | Administrator | The change would leave the site without an active administrator. |
| `assignment_mismatch` | gRPC | Change the request | Service | The request names a site, a volume or a source assignment that the service does not serve. |
| `media_scope_denied` | gRPC | Change the request | Service | No authorized media session covers the requested block, interval or export. |
| `privacy_policy_changed` | gRPC | Read the current state and decide again | Service | The privacy policy of the point changed after the media session was authorized. |
| `representation_unavailable` | gRPC | Change the request | Service | The session needs a masked view, or the original of the recording is not available to it. |

## Not found

The named object does not exist or is not visible to the caller.

| Code | HTTP status or source | Handling | Who | Meaning |
| --- | --- | --- | --- | --- |
| `not_found` | 404 | Change the request | Operator | The object does not exist or the user may not see it. |
| `camera_audio_missing` | 404 | Change the request | Operator | The camera has no audio channel. |

## State conflict

The current state of the object prevents the operation.

| Code | HTTP status or source | Handling | Who | Meaning |
| --- | --- | --- | --- | --- |
| `revision_conflict` | 409 | Read the current state and decide again | Administrator | The object changed since it was read. Read it again and repeat the change. |
| `precondition_failed` | 412 | Read the current state and decide again | Administrator | The object changed since it was read, its version differs from the If-Match header. Read it again and repeat the change. |
| `idempotency_conflict` | 409 | Change the request | Service | The idempotency key was already used for a different request. |
| `conflict` | 409 | Read the current state and decide again | Administrator | The state of the object prevents the operation. |
| `account_conflict` | 409 | Change the request | Administrator | An account with this user name already exists. |
| `group_conflict` | 409 | Change the request | Administrator | A resource group with this name already exists. |
| `grant_conflict` | 409 | Change the request | Administrator | The grant already exists. |
| `identity_conflict` | 409 | Change the request | Administrator | The corporate identity is already linked to another account. |
| `camera_already_registered` | 409 | Change the request | Administrator | The camera is already registered on the site. |
| `camera_binding_conflict` | 409 | Read the current state and decide again | Administrator | The observation point or the camera is bound differently than the request expects. |
| `camera_ptz_busy` | 409 | Repeat later | Operator | Another operator controls the PTZ camera now. |
| `camera_position_changed` | 409 | Read the current state and decide again | Administrator | The camera moved after its view was calibrated. Calibrate the view again. |
| `camera_clock_uncertain` | 409 | Repeat later | Administrator | The camera clock cannot be related to the site time well enough for the operation. |
| `position_from_plan` | 409 | Change the request | Administrator | The position of the point follows its floor plan and cannot be changed on the map. |
| `georeference_changed` | 409 | Read the current state and decide again | Administrator | The placement of the floor plan changed since it was read. |
| `region_busy` | 409 | Repeat later | Administrator | A map package of the region is being installed or removed. |
| `alert_state_conflict` | 409 | Read the current state and decide again | Operator | The alert was taken or closed by someone else. |
| `alert_result_required` | 409 | Complete the named step | Operator | Closing the alert needs the result of its processing. |
| `recording_policy_managed` | 409 | Change the request | Administrator | The recording of the point follows a group policy and is changed in the group. |
| `manual_recording_disabled` | 409 | Change the request | Operator | The recording policy of the point does not allow manual recording. |
| `binding_changed` | gRPC | Read the current state and decide again | Service | The binding of the camera to the point changed or is unknown to the service. |

## Limits

A capacity or rate limit of the site is reached for now.

| Code | HTTP status or source | Handling | Who | Meaning |
| --- | --- | --- | --- | --- |
| `rate_limited` | 429 | Repeat later | Operator | Too many requests in a short time. |
| `video_session_limit` | 429 | Repeat later | Operator | The site serves the largest number of video views it allows. |
| `video_bandwidth_limit` | 429 | Repeat later | Operator | The video bandwidth of the site is used up. |
| `video_transcoder_limit` | 429 | Repeat later | Operator | All video conversion slots are busy. Close another view or use a client with HEVC support. |
| `export_too_large` | 413 | Change the request | Operator | The export exceeds the size limit. Select a shorter interval. |
| `camera_batch_limit` | 422 | Change the request | Administrator | The batch contains more cameras than one batch allows. |
| `replay_busy` | gRPC | Repeat later | Service | The recording node already replays the recording of a camera. |

## Cameras

The camera or the camera network refused or failed the operation.

| Code | HTTP status or source | Handling | Who | Meaning |
| --- | --- | --- | --- | --- |
| `camera_unavailable` | 503 | Repeat later | Administrator | The camera does not answer on the camera network. |
| `camera_tls_error` | 502 | Change the request | Administrator | The TLS connection to the camera failed or its certificate is not trusted. |
| `camera_transport_denied` | 422 | Change the request | Administrator | The transport policy of the site does not allow this connection to the camera. |
| `camera_authentication_failed` | 422 | Change the request | Administrator | The camera rejected the stored credentials. |
| `camera_protocol_error` | 502 | Repeat later | Administrator | The camera answered with a response that does not follow its protocol. |
| `camera_incompatible_profiles` | 422 | Change the request | Administrator | The stream profiles of the camera do not meet the recording requirements. |
| `camera_discovery_unavailable` | 503 | Repeat later | Administrator | Discovery cannot run on the camera networks now. |
| `camera_command_unsent` | 502 | Repeat later | Operator | The command did not reach the camera. |
| `camera_command_rejected` | 422 | Change the request | Operator | The camera rejected the command. |
| `camera_ptz_unknown` | 502 | Read the current state and decide again | Operator | The camera did not confirm the PTZ position. |
| `camera_ptz_unsupported` | 422 | Change the request | Operator | The camera does not support this PTZ command. |
| `camera_audio_unavailable` | 503 | Repeat later | Operator | The audio channel of the camera does not answer. |
| `camera_audio_unsupported` | 422 | Change the request | Operator | The camera does not support the requested audio direction or format. |
| `camera_image_unsupported` | 422 | Change the request | Administrator | The camera does not support this image setting. |
| `camera_image_unconfirmed` | 502 | Read the current state and decide again | Administrator | The camera did not confirm the image setting. |
| `edge_recording_unavailable` | 409 | Repeat later | Administrator | The recording on the camera is not available for the interval. |

## Recording and video

The recording or the media delivery cannot serve the requested interval or view.

| Code | HTTP status or source | Handling | Who | Meaning |
| --- | --- | --- | --- | --- |
| `media_unavailable` | 409 | Change the request | Operator | The archive has no recording that can be shown for the interval. |
| `recording_gap` | 409 | Change the request | Operator | The interval contains only a recording gap. |
| `pending_catalog` | 409 | Repeat later | Operator | The recording is still being added to the archive catalog. |
| `media_session_closed` | 410 | Complete the named step | Operator | The view ended. Open the video again. |
| `video_source_unavailable` | 503 | Repeat later | Operator | The recording node has not received the video stream of the camera yet. |
| `live_setup_timeout` | gRPC | Repeat later | Service | The live connection was not established in time. |

## Site services

A service of the site that the operation needs is unavailable.

| Code | HTTP status or source | Handling | Who | Meaning |
| --- | --- | --- | --- | --- |
| `media_service_unavailable` | 503 | Repeat later | Operator | The media service of the site does not answer. |
| `authorization_unavailable` | 503 | Repeat later | Operator | The permission check cannot complete because the database does not answer. |
| `dependency_unavailable` | 503 | Repeat later | Administrator | A service of the site that the operation needs does not answer. |
| `identity_unavailable` | 503 | Repeat later | Operator | The corporate identity provider does not answer. |
| `source_unavailable` | 409 | Repeat later | Administrator | The source of the map package is not reachable. |
| `download_unavailable` | 409 | Repeat later | Administrator | The map package cannot be downloaded from its source now. |
| `network_unavailable` | map state | Repeat later | Administrator | The connection to the map catalog or download server is unavailable. |
| `checksum_mismatch` | map state | Repeat later | Administrator | The downloaded map file does not match the checksum of the catalog. |
| `invalid_range` | map state | Change the request | Administrator | The catalog server does not support the byte ranges needed to resume a download. |
| `catalog_unavailable` | map state | Repeat later | Administrator | The map file is not available in the catalog. |
| `storage_unavailable` | map state | Read the current state and decide again | Administrator | The map file cannot be stored on the disk of the site. |
| `connection_lost` | browser | Repeat later | Operator | The browser lost the connection to the site before the answer arrived. |
| `recorder_feature_unavailable` | gRPC | Change the request | Service | The recording node does not support the operation, such as archive protection. |

## Internal failure

The service failed unexpectedly. The request identifier leads to the journal entry.

| Code | HTTP status or source | Handling | Who | Meaning |
| --- | --- | --- | --- | --- |
| `internal_error` | 500 | Repeat later | Administrator | The service failed unexpectedly. The journal entry has the request identifier. |
| `installation_failed` | map state | Read the current state and decide again | Administrator | The map region could not be installed from the selected package. |
