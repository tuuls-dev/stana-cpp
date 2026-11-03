# C++ naming and documentation

These conventions apply to first-party modules, implementations, tests and examples.
They follow the intent/consistency principles in the [C++ Core Guidelines,
NL.1–NL.8](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-naming),
and use [Doxygen documentation comments](https://www.doxygen.nl/manual/docblocks.html).
The chosen casing follows the existing Stana design notes; consistency matters
more than imposing a different style on the framework halfway through the port.

## Names

- Types and concepts use `PascalCase`; functions, variables, fields and constants
  use `lowerCamelCase`. Private data members end in `_`. File/directory names are
  lowercase and describe a subsystem or responsibility.
- Name operations with verbs (`publishGeometry`, `cancelSubscription`) and values
  with domain nouns (`pendingObservations`, `documentRevision`). Boolean names
  answer a question (`isAlive`, `hasPendingFrame`, `canActivate`).
- Prefer domain vocabulary over implementation encodings: `nodeId`, not `u64Id`;
  `subscription`, not `subscriptionPtr`. Avoid vague `data`, `temp`, `manager`,
  `process` and `handle` when the actual purpose can be named.
- Short names belong in tiny local scopes or established mathematical notation
  (`x`, `y`, affine coefficients); broader scopes deserve more descriptive names.
- Use named constants for non-obvious units, limits, dimensions and tolerances.
  Do not invent synonyms for the same concept in neighboring subsystems.

## Comments

- Document exported and important internal declarations beside their definitions:
  `/// @brief` for types/operations, followed by usage/consumer/ownership details;
  `///<` for fields and enum values. IDE hover should answer what the entity
  represents, who consumes it, and how to use or interpret it. Describe purpose, ownership, executor, lifetime and failure rules.
  Use `@param`, `@return`, `@pre` or `@throws` when those contracts need explanation.
- Document invariants at the code that enforces them: transaction publication,
  weak graph edges, scope disposal, callback invalidation, UTF conversions and
  native presentation identity. Explain *why*, not a paraphrase of each line.
- Keep comments concise and accurate; change them with the implementation. Avoid
  boilerplate for obvious getters and narrative comments that obscure the code.
- Tests should state the regression or externally observable contract they prove.
  Architecture documents explain subsystem boundaries; comments explain local
  decisions. Neither substitutes for descriptive names and focused functions.
