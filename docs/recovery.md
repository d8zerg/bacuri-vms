# Recovery of the Bacuri VMS configuration and archive

[Русская версия](ru/recovery.md)

The procedure is designed for one site and one recording node. It runs local C++ commands in the environment of the corresponding service. It needs the identifiers of the original site and node, core access to PostgreSQL, the core configuration key and the preserved archive volume. Before starting, determine where the original data is and where the site will be restored. `task recovery:check` checks the procedure in a separate environment. [Test protocol](test-profile.md#failure-and-recovery-tests)

For an installed Debian package, a full backup also includes the history, the catalog and the site keys. [Maintenance of an installed node](maintenance.md)

## Backup

Create a snapshot on media independent of PostgreSQL and the recording volume. The example uses a backup directory accessible only to the administrator. The `backup_dir` variable must contain the path to this directory.

```sh
set -eu
umask 077
partial=$(mktemp "$backup_dir/.config-XXXXXXXX")
name=${partial##*/}
backup="$backup_dir/${name#.}.backup"
if docker compose exec -T core /opt/bacuri/bacuri-vms-core backup-config > "$partial"; then
    sync "$partial"
    ln "$partial" "$backup"
    rm "$partial"
    sync -f "$backup_dir"
    echo "$backup"
else
    rm -f "$partial"
    exit 1
fi
```

The snapshot is read in one PostgreSQL transaction and encrypted with authentication by the core key. It contains the accounts with password hashes, the audit of WebAuthn enrollments, roles, access scopes, the registry, bindings, settings revisions, encrypted camera credentials, the configuration audit and the registration replay keys. Recovery ends browser sessions and resets WebAuthn enrollments, including keys from old copies. The administrator registers the key again with a one-time code from the local console. Recovery is bound to the same site and node. The ciphertext limit is 128 MiB. Keep a protected copy of the core key separately. To keep signing exports on behalf of the site, also keep `export.key` from the `core-identity` volume. The archive of trusted public keys lets previously released packages be verified after a key change. To restore the existing TLS identities and the local destination log of the recorder, also keep the corresponding keys. The snapshot frequency determines the possible rollback of configuration and permissions.

The archive, events and alarms need separate backups. For video, keep the blocks with manifests and the volume journal on independent media. SHA-256 checks the integrity of the preserved bytes.

## Site recovery

1. Stop `core`, `recorder` and `media` on the site being restored. Keep the original databases and volumes for diagnostics. Prepare a separate clean PostgreSQL database, the core key and the volume at the original mount point. All following commands must address the target database. To restore an old configuration, use a fresh `recorder-data` volume: the recorder gets its assignment again. The `archive-data` volume contains a separate archive journal.
2. Apply the migrations, load the backup and check the state. A repeat with the same snapshot is allowed on a database with an identical configuration. A mismatch of content, key or assignment aborts the recovery. The operation revokes browser sessions and requires the recorder to apply the settings again.

   ```sh
   docker compose run --rm --no-deps -T core /opt/bacuri/bacuri-vms-core migrate
   docker compose run --rm --no-deps -T core /opt/bacuri/bacuri-vms-core restore-config < "$backup"
   docker compose run --rm --no-deps -T core /opt/bacuri/bacuri-vms-core recovery-status
   ```

3. Compare the roles, bindings and granted permissions with those in force at the time of the return to operation. Changes after the snapshot, including permission revocations, have to be applied again. At this stage the archive is in the `awaiting_verification` state. If the working database is preserved and only the media needs reconciliation, go directly to the next operation. With the core stopped, the following query on the target database lists the roles and the explicit point assignments. For an administrator the scope covers the whole site.

   ```sql
   SELECT u.username, r.site_id, r.role, g.point_id
   FROM bacuri_access.site_roles r
   JOIN bacuri_access.local_users u USING (user_id)
   LEFT JOIN bacuri_access.point_grants g USING (user_id, site_id)
   ORDER BY u.username, r.site_id, g.point_id;
   ```

4. With the recorder stopped, run `docker compose run --rm --no-deps -T archive-recovery`. The command itself closes delivery until the check, holds exclusive ownership of the volume and alone updates the catalog of the site. The check log names the UUIDs of damaged files. In the final JSON, `verified` is the number of verified blocks, `corrupt` the number of damaged ones, `journal_missing` and `catalog_missing` the numbers of missing blocks known to the journal and to the catalog respectively. These two counters can include the same block.
5. `recovery-status` shows the archive admission state in the core. `available` means that the administrative ban is lifted. `volume_unavailable` requires checking the mount and the path. `verifying` after the process ended with an error means an unfinished reconciliation. Check the reason in stderr, the volume owner, the file permissions and the presence of historical bindings in the restored configuration. If needed, restore a suitable snapshot into a clean database. Repeat the same command. Without SQLite, healthy manifests and files let the journal and the catalog be rebuilt. Information about earlier gaps needs a preserved journal or catalog. When an old SQLite journal is migrated, the intervals waiting for transfer are kept. Information about gaps confirmed before the move stays in the original PostgreSQL.
6. After a successful reconciliation, run `docker compose up -d --wait core recorder media`. Check `task archive:status`, the applied state of the camera and the appearance of a new completed block. Automatic acceptance additionally signs in as an operator, decodes old and new material over HTTPS and checks the refusal on a neighboring point. [Matrix and measurements](test-profile.md#failure-and-recovery-tests)

## Failures during operation

| Observation | Action and expected result |
| --- | --- |
| PostgreSQL is temporarily unavailable, the volume is healthy | Recording continues. After the database returns, the recorder passes the accumulated manifests until acknowledged. A repeated registration of a matching block keeps one catalog row. |
| The camera is unavailable | Restore the connection with both RTSP profiles. After a new keyframe, check that recording continues and the `source_offline` interval. |
| Media delivery failed | Restart `media`. The recorder keeps writing. A repeated read checks the permissions and the checksum. |
| The volume is full | Free space according to the accepted retention policy. The recorder repeats committing the failure information when space appears. Diagnostics contain the point and the bounds of `storage_failure`; when the journal and the process are lost at the same time, only the external diagnostic logs remain. |
| The volume is disconnected | Stop the recorder and media delivery. Return the original media, run the reconciliation and start the services. When the assignment does not match, delivery stays closed. |
| A known block is damaged | Keep the damaged material for diagnostics. The reconciliation excludes it from delivery and marks the known interval as `storage_failure`. For a directory without a readable manifest and without earlier metadata, the UUID of the block directory and the damage message are available. |
| The only volume is lost | Without an independent copy, the earlier material is lost. Keep the original catalog with the known intervals. On new healthy media, initialize the directories with the regular `control-init`, start the recorder to create the journal of the new volume and stop it. Run the reconciliation and return the services. The known earlier intervals get `storage_failure`, the new recording is available separately. |
