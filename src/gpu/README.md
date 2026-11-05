# Portable GPU internals

Private GPU transport, allocation accounting, color conversion, payload limits,
and WGSL validation live here under `stana::gpu::detail`. Consumer interfaces and
GPU class implementations remain in rendering; consumers still `import stana;`.

## Architecture

Application adapters convert public GPU values into owning transport records.
WGSL validation retains its parsed program and source together. The macOS backend
consumes that validated artifact without reparsing and owns Metal lowering,
binding slots, immediate buffers, pipelines, resource handles, and presentation.
No native framework or Metal writer dependency belongs in this directory.

## Internal example

```cpp
const auto validated = stana::gpu::detail::shader::validate(request);
if (!validated) {
  return std::unexpected(validated.error());
}
// Only the macOS adapter passes this owning artifact to its Metal lowerer.
```
