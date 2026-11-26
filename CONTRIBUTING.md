# Contributing to Bacuri VMS

Bacuri VMS is free software under the MIT license. Every change is judged against the [product principles](docs/product-principles.md): openness first, then operator speed, then archive truth.

## Prerequisites

- Linux x86-64 with the native Docker Engine and the Docker Compose plugin.
- [Go Task](https://taskfile.dev/docs/installation/).

Builds, tests and the local stand run in containers. No compiler, database or browser is needed on the host.

## First run

```sh
task stand:up                      # PostgreSQL and a synthetic RTSP camera
task control:up                    # core, recorder and media delivery with migrations applied
printf '%s\n' 'a-long-password' | task control:create-user USERNAME=admin ROLE=administrator
task control:enroll-admin USERNAME=admin
```

Open <https://localhost:8443/cameras>, sign in with the password and the enrollment code, and register a WebAuthn key. Add the synthetic camera with the stream addresses `rtsp://rtsp-camera:8554/main` and `rtsp://rtsp-camera:8554/sub`. The [local development guide](docs/local-development.md) describes the stand, the test camera and every task.

## Checks before a change

| Command | Purpose |
| --- | --- |
| `task test` | Build with the `gcc-debug` preset and run the unit tests |
| `task test PRESET=clang-asan-ubsan` | The same under AddressSanitizer and UndefinedBehaviorSanitizer |
| `task format:check` and `task lint` | clang-format and clang-tidy |
| `task ui-kit:check` | Types, lint, formatting, accessibility and screen references of the web client |
| `task check` | All quality gates, including end-to-end scenarios on the stand |

Run the scenario of the area you changed, for example `task archive:check` for recording and the archive or `task control:check` for the core API. A change of a contract in `contracts/v1` regenerates the client types with `task api:types` and passes `task api:check`.

## Engineering rules

- Services, domain logic, administration and recovery are written in C++23. Python is used for build, generation and test tooling only.
- Each Core module owns its data and is reached through its public header, as [ADR-018](docs/adr/018-core-module-boundary.md) describes. `tests/architecture/core_boundaries.py` checks the boundary.
- Behaviour that a user can observe is covered by a test that fails without the change.
- Code, comments, contracts and program messages do not refer to documents, plan steps or document sections. ADR numbers are allowed.
- The primary language of the README and documentation is English. The Russian version lives in `README.ru.md` and `docs/ru/` and is updated in the same change.
- Web client text lives in the message catalogs `web/client/src/i18n/ru.ts` and `en.ts`. A new key is added to both catalogs: the type check rejects a key missing from either of them, and the linter rejects Russian text in components.

## Commit messages

One line in English: `<type>(<scope>): <summary>`. The type is one of `feat`, `fix`, `refactor`, `docs`, `test`, `build`, `ci` or `chore`. The summary starts with an imperative verb and has no final period.

```text
fix(archive): report frames discarded at the keyframe interval limit as recording gaps
```

## Security issues

Do not describe a vulnerability in a public issue. Follow the [security policy](SECURITY.md).
