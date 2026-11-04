# Incremental shared rendering — qualification record

**Status: qualification in progress; not yet full-scope completion of task 01.** The old bitmap-upload production implementation
has been removed, as requested. The draft composition root uses the new tile host;
that switch is **not qualified for release**. No fallback or runtime toggle remains.

## Environment

- Apple M1 Pro, 16 GiB unified memory; macOS 27.0.1 (26A434).
- Repository toolchain: upstream LLVM/Clang/clang-tidy/clang-format 22.1.8,
  Ninja, CMake presets; deployment target macOS 26.0, system libc++.
- CPU raster references: sRGB, BGRA8 premultiplied first, little endian,
  default CoreGraphics antialiasing; 1× and 2×; 1025×769 logical points.
- Native GPU/window tests need execution outside the restricted sandbox. An
  unlocked display does not imply the test window is unoccluded; initial native
  runs reported `occlusionState=8192` (visible bit absent).

## Pixel-reference amendment and evidence

`stana.raster` compares every visible byte of the assembled physical tile image
with a single full-viewport replay using the same prepared commands/resources.
It checks row payload, not padding. The initial byte-exact gate failed as recorded
below; no expected-failure annotation or skipped test was substituted.

The original fixture exposed a one-channel difference of one byte at a diagonal
line crossing a tile boundary (1×, tile 3, row 15, byte 2045: 85 versus 86 with
underlying content). A standalone CoreGraphics-only experiment reproduced the
same difference on white with just the line `(530.25,510.25) → (510.25,530.25)`,
width 2. Pre-stroking into a filled path did not remove it. Turning antialiasing
off did, but that changes the reference pixels and was not retained.

The user explicitly authorized testing a bounded raster-gutter amendment:

| Device-pixel gutter | Observed result |
| --- | --- |
| 2 | Original fixture still fails at 1× and 2×. |
| 32 | Original fixture passes 1× but fails 2×. |
| 64 | A longer diagonal crossing multiple tiles still fails both scales. |

The long line is `(5.25,10.25) → (1005.5,710.5)`, width 1, translucent red. With
64-pixel gutters, observed first differences included 221 versus 215 at 1× and
248 versus 251 at 2×. This rules out treating a small fixed sampling margin as a
proven solution for arbitrary supported line geometry.

Unsuccessful gutter implementations were removed; the expanded crossing-line
regression remains. No authoring pixels, antialiasing, or assertion were weakened.
The user then explicitly approved a bounded antialiasing gate: exact alpha and
uniform reference regions; differences only in a nonuniform 3×3 reference coverage
neighborhood; RGB error ≤32/255; ≤0.25% differing pixels per fixture. No gutter or
antialiasing mode change is retained. Incremental versus fresh tile replay remains
byte-exact, so the amendment cannot hide stale or incompletely reconstructed tiles.

The amended gate passes at both scales:

| Scale | Visible pixels | Differing pixels | Maximum RGB error | Alpha error |
| --- | ---: | ---: | ---: | ---: |
| 1× | 788,225 | 1,397 (0.177%) | 9/255 | 0 |
| 2× | 3,152,900 | 3,672 (0.116%) | 31/255 | 0 |

## Approved native qualification amendments

The user explicitly approved:

1. Onscreen real-window evidence/lifecycle gates plus full-size offscreen 4K/8K
   GPU transfer/resource gates on this machine. Physical scale switching and large
   display confirmation are **not** claimed qualified.
2. Native transaction-plus-anchor acknowledgement: GPU success for every copied
   tile, an issued batch transaction, and a real positive timestamp for the first
   submitted tile. Other tile timestamps are logged, not fabricated or counted as
   per-tile display proof. Portable explicit expected-tile sets remain strict.

The raw native runs showed why the second amendment was needed: a replacement
surface can return zero times for some/all drawables while the command buffer
succeeds. Zero never became positive evidence in the implementation.

## Implemented and separately testable


- Classified retained assignments and dirty-child routes; proposal-keyed layout
  caches; same-size text, paint-only updates, focus and scroll work separation.
- Immutable scene branches/local paint segments; lazy flat inspection; staged
  projection promotion; failed-stamp retry bounds and reentrant provider handling.
- Persistent AVL geometry roots, keyed semantic diffs, admitted-attempt pins,
  retirement-before-cleanup, composite changed-region and surface gates.
- Owning native tree conversion; prepared Core Text/path resources; conservative
  ink and complete-snapshot damage comparisons.
- Checked tile/budget arithmetic, shared Metal-backed CGContext storage, direct
  buffer-to-texture copies, independent GPU owners and allocation accounting.
- Production single-batch scheduler, typed tile evidence, transaction presentation,
  bounded acquisition retry, recovery/draining, generation checks and teardown.

Implementation is not evidence that every acceptance scenario has passed. In
particular, full R01–R18/P01–P24/native lifecycle qualification, 4K/8K layer resource
measurements, native sparse-work timings, and backing-scale/window stress remain
unqualified. The current native smoke is not a substitute for those gates.

## Commands and results

Final Debug/Release/sanitizer/distribution results are recorded below after runs.
No skipped, unregistered, or occluded test is counted as native success.
