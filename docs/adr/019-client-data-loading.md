# ADR-019. Data loading in the web client

[Русская версия](../ru/adr/019-client-data-loading.md)

**Status** - Accepted.

**Context.** The screens of the single client read site data through the `Services` interface. The same interface is implemented by the server adapters and by the Storybook fixture. Each screen loaded its data in its own `useEffect` with an `AbortController`, its own failure flag and its own refresh counter. The map screen also polled the scene. The rules for a stale response, a transient failure and a refresh differed between screens. [ADR-014. Single web client](014-web-client.md)

**Options.** React Router loaders read data per route. Most reads of the client belong to a tab, a dialog or a selected object inside one route, and the map scene is polled, so loaders cover a part of the reads. TanStack Query provides a cache, cancellation, retries, polling and invalidation, and adds a dependency with its own release cycle to the offline kit. A small layer of the client covers the needs of the current screens: a cached value per key, cancellation, retries of transient failures, polling and showing a saved value.

**Decision.** The client uses its own hook `useResource` in [resource.ts](../../web/client/src/adapters/resource.ts).

| Rule | Behavior |
| --- | --- |
| Key and scope | A read is named by a key with its parameters. Values are cached per scope, such as the services of a session or a fixture, so a new sign-in never shows the values of another session |
| Cancellation | A change of the key or the closing of the screen cancels the read. A late response of a cancelled read does not change the screen |
| Retries | Network failures and the statuses 502, 503 and 504 are repeated twice with a growing pause. A refusal of the request is shown at once |
| Refresh and polling | A refresh keeps the shown value until the new one arrives. Polling skips a tick while a read is running. An unchanged answer keeps the shown object |
| Changes | A screen shows the value the server returned for a saved change through `set`. Commands keep the idempotency contract of the API |

A screen that needs workflow state, such as a form with several steps, keeps it in a reducer next to the screen. Loading stays in `useResource`.

**Rationale and consequences.** The layer has no runtime dependency and works with the existing `Services` interface, so the fixture and the server keep one path. Invalidation between screens, paginated reads and request deduplication are not provided. When a screen needs them, the choice of TanStack Query is reviewed again.

**Verification.** The screen tests of the UI Kit cover loading, failure and refresh states on the fixture. The end-to-end scenarios check the same screens against the server.
