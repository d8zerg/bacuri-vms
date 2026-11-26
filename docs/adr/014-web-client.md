# ADR-014. Architecture of the single web client

[Русская версия](../ru/adr/014-web-client.md)

**Status** - Accepted.

**Context.** The administrator and the operator work on one site without external internet. Sessions, permissions, the catalog and the feed belong to the C++ services. The browser needs to combine navigation with long-running playback and an alarm subscription. The UI Kit must show the same states on deterministic data and when working with the server.

**Options.** Separate pages with common CSS are simpler to deliver but already require coordinating the point selection and the media session on navigation. A React framework with server rendering is useful when the server delivers finished pages. For two authorized interactive routes, a static application with client routing also fits. [Ways to build a React application](https://react.dev/learn/build-a-react-app-from-scratch) · [React Router modes](https://reactrouter.com/start/modes)

**Decision.** Use React, strict TypeScript, Vite and React Router in Data Mode. Vite builds the static resources, the router manages navigation and the cancellation of loading. The site serves the resources from the same HTTPS origin as the API. Node.js is used for the build. Direct opening, page reload and browser back must work on the supported URLs, including the transition from an alarm to the archive. Pin the dependency and Node.js versions when the skeleton is created, in the lock file and the build image respectively. [Vite static deployment](https://vite.dev/guide/static-deploy) · [Load cancellation in React Router](https://reactrouter.com/explanation/race-conditions)

| Boundary | Responsibility | Verification |
| --- | --- | --- |
| UI Kit | Basic elements use shared tokens. The camera form, the timeline, the alarm card and the player view receive a typed state and report actions. Basic elements do not depend on scenarios, transport or the session | Import rules and isolated examples. The same component shows the empty state, waiting, the result and the applicable failures |
| Administrator and operator scenarios | Own the selected point, the interval, navigation and focus. The result of a request or of decoding belongs to a specific point and media session. Events of an old session are dropped | Delay a response, change the point, then deliver the old response. Update the feed with the archive open and check the return to the original alarm |
| Adapters and startup | The common session adapter and the typed REST client use [ADR-001](001-api-contracts.md). The WebSocket, WebRTC and MSE adapters own the subscriptions, connections and player resources. At startup the application connects them with the scenarios | In integration, check resubscription by cursor, the release of the old player, the end of delivery on revocation and the correctness of the next permitted session |

**Rationale and consequences.** One set of components serves the catalog and the work routes. Static delivery uses the existing site server. The team is responsible for handling direct links and cleaning up subscriptions and media resources. Cancelling a client request by itself does not cancel a command the server has already applied. A repeat of a changing operation follows the idempotency contract.

**Verification and review.** Run a clean production build, the type and import checks, and go through both routes through the regular HTTPS sign-in with internet access disabled. Check a direct link and a page reload with a valid and an expired session. Record the size of the initial JavaScript and CSS and the time from navigation to the availability of the main action on the stand. Review the boundaries when the two routes duplicate the session state or do not fit the justified limits of the profile. [Work screens](../ui.md#work-screens) · [Code checks](../quality.md#automatic-code-checks)

**Delivery and route state.** Vite builds the static resources. The site server returns HTML for the three addresses `/`, `/monitor` and `/cameras`, and the hashed files from the resource directory. Source maps stay in the build report. Both consumers of the existing MSE module use the archive player. The subscription, the WebRTC connection and the media session are released when the point is left. The archive checks the permit again even on a paused frame. The client keeps the navigation context in the URL and the parameters of an open form in memory. Losing the session stops showing the data.

**Cost control.** A local comparison run uses the same server, browsers, routes and measurement protocol for the build with deterministic adapters and the delivered build. When the budget is exceeded, investigate the initial imports and the number of requests before the work action appears. [Conditions and thresholds](../test-profile.md#single-web-client-acceptance)
