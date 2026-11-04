# Canvases

`CPUCanvas` replaces `Canvas` with no compatibility alias. `Drawing` retains
`fill`/`stroke` and adds `fillRect`/`strokeRect`. Its value, signal and computed
properties update retained paint without remounting or requesting layout.

Both canvas coordinates start at the direction-resolved padded content origin.
Clipping defaults to the rounded outer bounds; `.appearance = {.clip = false}`
explicitly disables that clip. Drawn geometry does not determine intrinsic size
or create accessible children. Supply an explicit frame and accessible equivalent.

CPU drawings have structural command equality and validate their entire stream
before replacing retained state. Builder edits do not affect a mounted copy.
Invalid geometry, color or unbalanced clips throw `std::invalid_argument` and
leave the previous retained drawing intact. Signed line deltas remain supported.
Semantic colors resolve against the document theme and contrast preferences.
