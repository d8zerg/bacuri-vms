# Offline installation of Bacuri VMS

[Русская версия](ru/install.md)

Separate offline kits with PostgreSQL are delivered for Debian Bookworm and Trixie on amd64. The node needs systemd, APT and gpgv. The first kit for installation on one local site is free of charge. The project code is distributed under the MIT terms of the LICENSE file. Debian and web client components keep their own licenses. The names and versions of the delivered packages, media plugins, libraries and client resources are listed in sbom.json. The supplier keeps the versions of the build tools in build-tools.json, separately from the node components.

## Preparation

Check `VERSION_CODENAME` in `/etc/os-release` and the architecture with `dpkg --print-architecture`. Choose the kit of your operating system. The PostgreSQL server, client and libpq are included in the kit. Bookworm uses the packages of the PostgreSQL Global Development Group, Trixie uses the Debian packages. The node does not need a network repository for the installation.

Prepare the local DNS name of the site, a correct time and a TLS certificate with this DNS name in its SAN. The operator browser must trust the certificate authority. The HTTPS port is 8443. Mount a dedicated ext4 volume at `/srv/bacuri-archive` and pin it by UUID in `/etc/fstab`. The installer checks the mount point. Formatting and the choice of the disk happen before the installation.

Obtain the binary public signing key through a trusted channel and compare its fingerprint with the data of the supplier. Bring the kit to the node on read-only media. The `keyring` variable points to the trusted keyring file, `kit` to the kit directory.

```sh
gpgv --keyring "$keyring" "$kit/bacuri-install.sig" "$kit/bacuri-install" &&
    sudo "$kit/bacuri-install" "$keyring" "$kit"
```

The installer checks the signed SHA-256 list in the local copy, then gives APT the signed repository with the dependencies. On a clean node the local network of the site is enough for the installation and for operation.

## Initial setup

Run the following command in the local console. Pass one line with the password of the first administrator on standard input. In the example the password is read without echo. Give the camera networks in CIDR notation.

```bash
read -rs -p 'Administrator password: ' password
printf '%s\n' "$password" | sudo /usr/sbin/bacuri-vms setup "$dns_name" "$admin" "$certificate_chain" "$private_key" "$camera_cidrs"
unset password
```

Keep the one-time code from the console output. It is valid for ten minutes. Open the site address in a browser, enter the name and the password, then the initial enrollment code. Confirm the registration of the WebAuthn key and the sign-in with the same key and user verification. At later sign-ins the administrator enters the password and confirms the request with the key. Until the enrollment, `sudo bacuri-vms admin enroll-admin "$admin"` issues a new code.

```bash
systemctl is-active bacuri-core bacuri-recorder bacuri-media
read -rs -p 'Operator password: ' password
printf '%s\n' "$password" | sudo bacuri-vms admin create-user "$operator" operator
unset password
sudo bacuri-vms admin grant-point "$operator" "$point"
sudo bacuri-vms admin revoke-point "$operator" "$point"
```

The user creation command reads the password from standard input. The point identifier comes from the registry after the camera is connected. The service settings and the separate keys of the core and the recorder are in `/etc/bacuri-vms`. The signing key of the installation kit stays with the supplier.

## Maintenance of an installed node

A full backup includes the configuration, the history, the catalog, TLS and the site keys. Use it for upgrades, rollbacks and the replacement of the system disk. [Maintenance and recovery commands](maintenance.md)

## Saving the installed configuration

Keep a protected copy of `/etc/bacuri-vms` separately from the archive. `sudo bacuri-vms admin backup-config` writes an encrypted snapshot to standard output. Write it to a new file on independent media with mode `0600` and publish the copy only after the command succeeds. Restoring the configuration on the same site needs the original identifiers, the core key and a clean or identical database. With the services stopped, run `sudo bacuri-vms admin migrate`, then `sudo bacuri-vms admin restore-config < "$backup"`. `sudo bacuri-vms admin recovery-status` shows the archive state after the snapshot is loaded. Returning video needs a separate reconciliation of the preserved archive volume.
