# ADR-020. Error catalog and localized failure messages

[Русская версия](../ru/adr/020-error-catalog.md)

**Status** - Accepted.

**Context.** The HTTP API returned a Problem code chosen at each place of the server, with the HTTP status written next to it, so one code went with different statuses. The service gRPC described failures in English text. The web client mapped codes to messages separately on each screen. Some texts were produced by the server in Russian: the page of a failed corporate sign-in and the descriptions of recording policy changes in the event journal. A cause had no class, no recommended handling and no place in a reference.

**Decision.** One catalog [errors.yaml](../../contracts/v1/errors.yaml) names every cause of a failure. A code has a category, one HTTP status, one gRPC status code, a retry class, an audience and an English description. Everything else is derived from the catalog.

| Consumer | What it receives |
| --- | --- |
| Server | The `ErrorCode` type with the properties of each code, generated at build time. A failure carries a code and its parameters, the HTTP status and the gRPC status follow from the code |
| HTTP API | The `code` field of a Problem response. The Problem schema lists the public HTTP codes from the catalog |
| Service gRPC | The status code of the catalog and the code as the status message |
| Web client | The properties of each code and the message `error.<code>` in each interface language. A screen may show its own wording for a cause it explains in its context |
| Reference | [The error reference](../errors.md) in English and Russian, generated from the catalog and the client messages |

The catalog identifies where a cause can appear. HTTP handlers forward a public gRPC cause when its name and numeric status agree. Internal service causes and transport failures use the public fallback for the operation. Map source and installation job states have their own list of causes in OpenAPI. The client uses the retry class when repeating reads.

The HTTPS server also returns Problem for request size limits, unsupported expectations and malformed HTTP. Its request identifier appears in both the body and the response header. Loss of the database connection produces `dependency_unavailable` (503). Unexpected handler failures produce `internal_error` (500).

The server never sends text meant for people. A page or a journal entry that needs words carries a code and parameters, and the client renders them in the language of the interface.

| Retry class | Meaning for the caller |
| --- | --- |
| `never` | The request has to change |
| `after_change` | The state changed: read it again and decide again |
| `later` | A transient cause: the client may repeat after a pause |
| `user_action` | A person has to sign in, confirm or complete a step |

**Rationale and consequences.** A new cause is added in one place, and the check names every consumer that still lacks it. The categories and the retry classes give default handling for causes a screen does not know. The catalog is the base for help pages and for later diagnostics of a site, such as counting failures by category. Changing the HTTP status of a code changes the API contract and follows its compatibility rules.

**Verification.** `task api:check` compares the catalog with the Problem and map state codes of the contract, the codes in the server sources, the messages of both client languages and the error reference. The type check of the client rejects a code without a message.
