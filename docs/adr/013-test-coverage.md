# ADR-013. Coverage threshold of product code

[Русская версия](../ru/adr/013-test-coverage.md)

**Status** - Accepted.

**Context.** The C++ services run in separate processes, and JavaScript runs in the browser. The main scenarios cover both layers. Acceptance needs a measurable lower limit of automatic test coverage.

**Options.** One percentage for the repository, independent thresholds for the services and the client, or a threshold for changed lines only.

**Decision.** Require at least 75% of lines separately for product C++ in `src/`, working JavaScript in `web/` and TypeScript in `web/client/src/`. GCC collects data in a separate preset over CTest and the integration runs. Chrome collects V8 data while the scenarios of the working client run. Test files, mockups, generated sources and external libraries are outside the measurement.

**Rationale.** Independent thresholds reveal gaps in the smaller client. A common run counts code that runs only against real services. Line coverage shows that code ran. The expected result and the failures of each scenario are checked by separate test assertions.

**Consequences.** The run requires a separate instrumented build and a browser. Reports by file and uncovered line are kept to analyze a coverage drop. Code of a forcibly stopped process may not manage to write the GCC counters.

**Verification and review.** `task coverage:check` includes independent thresholds for product C++, browser JavaScript and the TypeScript of the single client. The UI Kit gate checks the views and scenarios on test data. The single client gate combines their V8 data with the execution of the delivered JS over HTTPS in Chrome. Source maps restore the TS and TSX lines. The denominator includes all executable files of `web/client/src`, including adapters and the entry point. A file that was not loaded gets zero. Declaration files, dependencies, stories and fixtures are excluded. The gate compares the full set of files with the disk and checks the integer counters without rounding. When the language or the tool changes, check again the restoration of paths and the counting of unloaded files. [Acceptance criteria](../quality.md#automatic-code-checks)
