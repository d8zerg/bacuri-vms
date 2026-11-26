# Security policy

Bacuri VMS protects video archives, camera credentials and access to live video. Reports of vulnerabilities are welcome and handled privately.

## Reporting a vulnerability

1. Open an [issue](https://github.com/d8zerg/bacuri-vms/issues/new) titled "Security report" that asks for a private contact. Do not describe the vulnerability in the issue, a pull request or a discussion.
2. A maintainer replies in the issue and agrees on a private channel.
3. Send the details through that channel:
   - the affected version or commit;
   - the component, for example the HTTPS API, the recorder, the media delivery or the offline installer;
   - steps to reproduce and the observed impact;
   - whether the issue is already known to others.

## What to expect

A report receives a response within 30 days.

A published advisory credits the reporter unless the reporter asks otherwise.

## Scope

In scope: the services in `src/`, the web client in `web/`, the contracts in `contracts/`, the Debian packages and the offline installer built from this repository.

Out of scope: vulnerabilities of cameras and third-party systems connected to a site, and deployments that disable the documented protections, such as TLS or the camera network allow list.

## Supported versions

Security fixes are released for the latest release. A site upgrades with the signed offline bundle as described in the [maintenance guide](docs/maintenance.md).
