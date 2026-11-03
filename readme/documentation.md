## Codebase Documentation Guide

This guide establishes clear, consistent conventions for documenting code across the codebase. Good documentation is concise, accurate, and tightly coupled with the code it describes. We use **Doxygen**-style comments for C++ and standard documentation practices across other languages.

---

### 1. General Principles

* **Document the *Why* and *Contracts*, Not Just the *What*:** The code itself explains *how* it works. Documentation should explain *why* a design choice was made, what invariants must hold true, and any subtle side effects or preconditions.
* **Keep Comments Up-to-Date:** Out-of-date documentation is worse than no documentation. When you change behavior, update the docs.
* **Prefer Inline and Concise Briefs:** Use brief, one-sentence summaries for classes, structs, functions, and enum values.

---

### 2. C++ Documentation Standards

We use standard Doxygen format (`///` or `//!`) for public APIs, complex subsystems, and performance-sensitive modules.

#### Classes and Structs

Provide a clear overview of the class's responsibility, thread-safety guarantees (if any), and lifetime expectations.

```cpp
/// @brief Manages platform-independent display list recording and execution.
/// 
/// The `DisplayList` records portable paint commands in logical coordinates 
/// for deferred rendering. It is thread-safe for recording but must be 
/// flushed on the main render thread.
class DisplayList {
public:
    /// @brief Appends a rendering command to the internal buffer.
    /// @param command The portable paint command to record.
    void push(PaintCommand command);
};

```

#### Functions and Methods

Document parameters, return values, preconditions, and failure modes.

```cpp
/// @brief Allocates a fixed-size block within the local memory arena.
/// @param size The number of bytes to allocate.
/// @param alignment Required memory alignment boundary.
/// @return A raw pointer to the allocated memory block, or `nullptr` if exhausted.
/// @note The returned memory does not need to be manually freed; it is released 
///       when the parent arena is reset.
[[nodiscard]] void* allocate(std::size_t size, std::size_t alignment);

```

#### Enums and Enum Classes

Use inline documentation for enum constants to clearly document what each state represents.

```cpp
/// @brief Defines the scheduling priority for background worker tasks.
enum class TaskPriority : std::uint8_t {
    background, ///< Low priority; runs only when idle.
    normal,     ///< Standard priority for regular application work.
    interactive ///< High priority; prioritized for user responsiveness.
};

```

---

### 3. README and Subsystem Documentation

Every major module, package, or directory containing a distinct component should include a short `README.md` following this structure:

1. **Overview:** A 2–3 sentence summary of what the component does.
2. **Architecture / Design:** A brief note on core data structures, memory ownership models (e.g., document-owned vs. arena-allocated), and key constraints.
3. **Quick Example:** A minimal code snippet showing how to consume the API.

---

### 4. Review Checklist for Documentation

Before submitting a pull request, verify that:

* [ ] All public classes, structs, enums, and functions have proper detailed documentation for the next person.
* [ ] Template parameters (`@tparam`), parameters (`@param`), and return values (`@return`) are explicitly documented where their purpose isn't self-evident.
* [ ] Complex algorithms, thread-safety assumptions, or non-obvious performance trade-offs are explained in comments.
* [ ] Typographical and grammatical errors are cleaned up.

---
