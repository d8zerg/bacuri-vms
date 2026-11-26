# ADR-009. Domain types for identity and intervals

[Русская версия](../ru/adr/009-domain-types.md)

**Status** - Accepted.

**Context.** The signature of archive search and registration contains several identifiers with the same UUID representation. A time interval can arrive with swapped bounds. An error in these values opens another episode or gives an unreliable answer about the recording.

**Options.** Pass strings and check them in every handler, introduce a separate class with its own UUID implementation for each resource, or use a common type with a purpose parameter and checked factories for related values.

**Decision.** `Identifier<Tag>` distinguishes the site, the device and other resources at the type level. For related values the domain checks the invariant when the object is created. Independent fields can remain an aggregate. Network and SQL adapters convert external representations to domain types at the boundary.

**Rationale.** The signature expresses the purpose of the argument, and one UUID parser removes discrepancies. An invalid interval can be rejected before search or a catalog change.

**Consequences.** The boundary needs explicit parsing and the conversion of an error into a contract response. A new type or template parameter needs an example of a real call and a check of the property it protects.

**Verification and review.** For the "Code" requirement, check at compile time that `SiteId` does not convert implicitly to `DeviceId`, feed an inverted interval and follow the error handling through the regular entry point. Review the representation when the identity of a resource changes or the type starts to allow an invalid operation. [Types and tests](../../tests/domain/model.cpp) · [Review rules](../quality.md#technical-code-review)
