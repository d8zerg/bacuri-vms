# Upgrade and recovery of Bacuri VMS

[Русская версия](ru/maintenance.md)

This guide describes upgrade, rollback and recovery on Debian Trixie for amd64. The maintenance tool compares the installed package with the database schema and rejects an unsupported kit. Qualification packages have the `~qualification` suffix. Keep both signed kits and independently trusted keys. The commands run in the local root console. The test stand checks the return to an independently prepared Debian with the packages installed and the recovery tools available. To use `bacuri-maintain` on a clean node, first install the dependencies with the regular installer of the kit.

During an upgrade the services stop for the time of the migration. Its duration depends on the size of the map and archive tables whose indexes are rebuilt. Use the maintenance tool of the new signed kit and keep the previous kit and the snapshot for a rollback. After the migration, check the camera and plan bindings, the regions, the PTZ calibration and the effective permissions for viewing, control and export. `bacuri-vms --version` shows the product version. The expected PostgreSQL schema number is stored in the `Bacuri-Schema` field of the Debian package. The applied number is returned by `SELECT max(version) FROM bacuri_meta.schema_migrations`.

## Backup and upgrade

Provide a new snapshot path on independent media that only root can access. Create a separate 32-byte recovery key with mode 0600 and keep a backup of this key separately. One snapshot includes a logical PostgreSQL dump with permissions, history and catalog, the service settings, the identifiers, the site keys and the certificates. The file is encrypted and authenticated. Keep the video blocks and the volume journal on separate archive media. The index of the local recorder destinations is restored from PostgreSQL. During maintenance `/var/tmp` holds temporary files with plain data that other users cannot read. Use a protected system disk.

```sh
set -eu
umask 077
# Choose a new file for a new key store.
( set -C; openssl rand 32 > "$key" )
gpgv --keyring "$new_trust" "$new_kit/bacuri-maintain.sig" "$new_kit/bacuri-maintain"
"$new_kit/bacuri-maintain" backup "$online_snapshot" "$key"
"$new_kit/bacuri-maintain" upgrade "$new_trust" "$new_kit" "$upgrade_snapshot" "$key"
```

Periodic backups can be taken on a running node. For the target RPO of permissions and configuration, the interval between successful snapshots must stay within five minutes. The upgrade checks the kit signature and the space for the snapshot, stops the services, keeps a separate copy before the migration and starts the services after it. If it is interrupted, repeat the command with the same paths. The `/var/lib/bacuri-maintenance/pending` marker keeps the services from starting after a reboot.

## Rollback and recovery

For a rollback, use the maintenance tool of the new kit, the trusted key and the package of the version recorded in the snapshot.

```sh
"$new_kit/bacuri-maintain" rollback "$old_trust" "$old_kit" "$upgrade_snapshot" "$key"
```

The command returns the configuration, the permissions and the history to the moment of the backup. Check the permission changes made after the snapshot. The preserved completed blocks of the same node are reconciled with the catalog.

To recover on a clean node, install the package of the snapshot version without the initial setup, mount the preserved ext4 volume at `/srv/bacuri-archive` and restore its fstab entry. Make sure the target PostgreSQL has no active site. Pass a kit of the same version as the backup.

```sh
"$new_kit/bacuri-maintain" restore "$trust" "$kit" "$snapshot" "$key"
```

Recovery closes the previous browser sessions and resets the WebAuthn enrollments. The administrator signs in with the original password and registers the key again with a one-time code from the local console. For a lost key on an active site, run `bacuri-vms admin recover-admin NAME`. The command revokes the enrollment and the active sessions of this account and issues a code valid for ten minutes. After the recovery use `bacuri-vms admin enroll-admin NAME`. The action log is in `bacuri_access.credential_audit`. On installations with the old schema, check `bacuri_inventory.credential_audit`.

## Diagnostics

`bacuri-maintain diagnose NEW_DIRECTORY` collects the state of the services, the camera connections, the registration queue and the free space. The bundle contains `metrics.prom`, the `events.jsonl` events and `traces.json` in OTLP JSON. Trace and span identifiers link an event in the system journal to the probe call. The fields are limited to identifiers, states and measurements. Prometheus can scrape `http://127.0.0.1:9464/metrics` on the node. The regular timer refreshes the metrics every ten seconds.

Every ten seconds the core, recording and media delivery services write their own metrics to their directory `/run/bacuri-vms/<service>/`, and the timer adds the fresh files to the probes. A file older than one minute is not published. The service metrics measure the thresholds of the [product principles](product-principles.md#2-operator-speed):

| Metric | Service | Meaning |
| --- | --- | --- |
| `bacuri_http_request_duration_seconds{workload}` | core | HTTPS request processing time for the workspace (`interactive`), password checks (`credential`) and long operations (`long_running`) |
| `bacuri_http_responses_total{workload,status}` | core | Responses by workload and status class |
| `bacuri_live_first_frame_seconds` | media delivery | Time from a live request to the first frame sent to the viewer |
| `bacuri_archive_publication_lag_seconds` | recording | Time from receiving the last frame of a block to its durable write |
| `bacuri_archive_gaps_total{reason}` | recording | Recording gaps by reason |
| `bacuri_archive_verified_bytes_total`, `bacuri_archive_verified_blocks_total{outcome}` | recording | Volume and results of the background integrity verification |

[Prometheus exposition format](https://prometheus.io/docs/instrumenting/exposition_formats/) · [OTLP trace format](https://opentelemetry.io/docs/specs/otlp/)

To configure trust from the local console, run `bacuri-vms admin configure-oidc ISSUER CLIENT_ID ADMIN_ACR` and pass the client secret on standard input. `bacuri-vms admin bind-identity USER_ID ISSUER SUBJECT` binds an external identifier to the UUID of a site user. Both operations are written to the audit. Changing the settings ends the corporate sessions of the site, changing a binding ends the corporate sessions of the affected user. Recovery resets the open sessions and the unfinished sign-in attempts. [Access management](ui.md#access-management)

## Second archive copy

Prepare separate media and a directory on it. For Compose, set the absolute path in `BACURI_REPLICA_MOUNT` and add `-f compose.yaml -f compose.replica.yaml` to the commands that start the services. The recorder and the recovery service get write access to the directory, the media service gets read access. Check the owner and group permissions against the service settings. In a native installation, mount the media before the services start and set `BACURI_ARCHIVE_REPLICA_DIR` to the same path for the recorder, the media service and the recovery command. The primary volume and the replica volume must be in different directories. Tolerating the loss of a disk requires independent physical media.

The recorder gives each pair of site and node a separate directory. After a block is completed, it checks the manifest and the SHA-256 of video and sound and publishes the copy atomically. The "Recording and storage" page and `bacuri-vms-recorder archive-status` show the queue, verification errors, the reserve of the primary volume and the depth forecast at the current recording rate. Check the queue when the number of cameras or the bitrate changes.

When one copy is damaged, reads use the intact one and the background reconciliation restores the damaged one. When both are lost, the interval becomes unavailable. Catalog recovery checks the files against the manifests and returns the active holds before the retention cleanup. When the replica volume is disconnected for a while, return the same volume so that the accumulated deletions are carried out. [Catalog recovery](recovery.md)
