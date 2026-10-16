# Outpostia Fork Changelog

This changelog records the changes carried by the Outpostia Godot fork and why
they exist. Official Godot release changes remain in [`CHANGELOG.md`](CHANGELOG.md).

## 4.7.1-outpostia.4

Date: 2026-10-15

Base: Godot `4.7.1-stable` (`a13da4feb8d8aefc283c3763d33a2f170a18d541`)

- Added shared canvas masks through `RenderingServer`. Rectangles inherit masks
  through their canvas item parents, including Y-sort. Owners follow canvas
  transforms without per-frame calls from the application.
- Added eight-mask sets, exclusions, resource cleanup and hidden invalid or
  overflowing sets. A ninth mask hides the affected subtree and reports one error.
- Applied masks after custom fragment code on the main renderer and OpenGL.
  Masked rectangles retain their material. Premultiplied alpha and
  multiply blending preserve their coverage behavior.
  Zero coverage preserves background alpha with OpenGL multiply blending.
- Added one mask texture array and packed mask rows. The main renderer uses a
  storage buffer. OpenGL uses an integer texture and reserves two texture units.
  Every canvas shader, masked or not, may use at most the available units minus ten.
- Packed the page layer, maximum mip level and mask set into `item_data` at
  byte 96 of the 128-byte canvas instance record. Byte 100 stays reserved and
  zero. Drawable arrays report at most 2048 layers.
- Added the `sampler_domain` name for page coordinates shared with nine-patch
  storage. Existing light data and the light texture atlas remain unchanged.
- Set native and managed distribution identity to `4.7.1-outpostia.4`.
- Calculate coordinates for one and two masks at sprite corners and omit mask code
  from batches without masks. Forward+ and Mobile select plain, one-mask, two-mask or
  general code from the highest mask count in each batch. Each rectangle skips absent
  masks. Small plain and masked rectangles share batches; plain and masked rectangles
  that cover at least 2000 screen pixels, measured with the item and draw-command
  transforms, start separate batches. Three or more masks use per-pixel entry reads.
  Rectangles hidden by an invalid mask set are not drawn.
- Share up to two mask sampling descriptors through 128 bytes of push constants.
  A rectangle that would add a third descriptor starts a new batch without
  changing draw order, so per-rectangle values remain only for three or more masks. Fixed descriptor
  slots and matching valid mask counts remove redundant fragment checks. Descriptors
  ignore the mip level when the batch filter has no mipmaps. Per-rectangle descriptor
  slots select between constant indexes, except on NVIDIA devices, which index push
  constants per pixel correctly.
- Compile new mask pipeline states in the background. Until one is ready, a general
  mask pipeline of the same material and format draws the same picture. Masks are in
  use once the first canvas mask is created or a mask page is set. From then on, each
  new plain rectangle pipeline queues its general pipelines at low priority after
  itself; plain pipelines built earlier queue theirs when masks come into use. If the
  general pipeline a batch needs is still queued, the render thread builds that one
  pipeline itself instead of waiting for the compiles queued before it.
- In every RenderingDevice pipeline cache, a thread that waits for a pipeline whose
  background compile has not started builds it itself, and a pipeline needed at once
  is built on the calling thread. Clearing a cache drops compiles that have not
  started and waits only for running ones. A pipeline that fails to build keeps an
  empty entry, so it is not built again, and its compile record is released like a
  finished one. Finished canvas compile records are released once per frame.
- Forward+ and Mobile shader changes stop the shader's background pipeline compiles
  before changing its data. Mobile no longer holds its shader lock while waiting for
  a running compile that needs the same lock.
- `RenderingServer.get_rendering_info()` reports canvas pipeline waits
  (`RENDERING_INFO_PIPELINE_WAITS_CANVAS`) and batches drawn with a general mask
  pipeline (`RENDERING_INFO_GENERAL_MASK_BATCHES_CANVAS`). Both counters are atomic,
  so counting takes no lock.
- Plain rectangles skip mask work with one check. Mask containers live in a record
  that only items with masks allocate. Freed mask entries are reused; mask sets are
  rebuilt when many entries are unused and every 256 canvas passes.
- Store the first mask transform in six unused ordinary-rectangle floats. Page
  rectangles and nine-patches keep their existing field meanings and row reads.
  The canvas instance record remains 128 bytes.
- Share matching two-mask linear transforms through push constants and keep their
  translations in unused ordinary-rectangle fields. Prefer smaller clipped mask
  bounds and discard zero coverage before the second sample and lighting.
- Preserve pixel snapping, mask coordinates after fragment vertex writes,
  level-zero filter margins, and blend coverage after modulation and lighting.
- Validate page types and layers, clamp mip levels and remove expired uniform sets.
- Cache inherited mask sets between canvas passes. Upload only changed rows and
  skip entries that no drawn rectangle uses.
- Reserve five extra varying locations for the two-mask path. One smooth `vec4`
  holds both UV pairs, and four flat `vec4` values hold sampling metadata. Forward+
  and Mobile start custom varyings at location 15 in every canvas shader, masked or
  not, leaving one within 16 locations.
  The largest OpenGL rectangle path uses 14 locations. Its 3.3 minimum of 60
  varying components leaves one custom vec4 without relying on component packing.

Masks affect rectangle draws only. Lines, polygons and meshes are unaffected.
Canvas groups and `clip_children` below a masked item are unsupported.
LCD text is unaffected, also below an overflowing mask chain. Repeated canvas items keep the mask at the first copy.
Descendant UI rectangles receive masks unless explicitly excluded.

## 4.7.1-outpostia.3

Date: 2026-08-14

Base: Godot `4.7.1-stable` (`a13da4feb8d8aefc283c3763d33a2f170a18d541`)

Source: `713fbf4a6fc63161179c0c172681ce38124293ba`

### Build and distribution

- Isolated persistent build outputs by engine SHA, platform, architecture,
  target, toolchain, and build flags so incompatible artifacts cannot overwrite
  one another (`b08f8c4595`).
- Set the native and managed artifact identity to `4.7.1-outpostia.3`
  (`713fbf4a6f`).

### Upstream correctness backports

Five Godot 4.7.2 fixes were carried without updating the fork's 4.7.1 base:

- High-polling-rate mouse performance: upstream `364304d372`, local
  `c6d4b65185`.
- Simultaneous Shift release: upstream `dcabf2e4db`, local `cf0e0a2a15`.
- Empty `DirAccess::create_temp` prefixes: upstream `8c6e403752`, local
  `564dbafe1d`.
- Logger crash after failing to open its output: upstream `2906aa0e6d`, local
  `7260c40315`.
- Incorrect instance-shader-parameter erasure: upstream `31e13d9f6f`, local
  `1b14db268f`.

### Runtime behavior

- Made known teardown-only leak diagnostics verbose while preserving their
  original severity outside teardown (`885ff05b0a`).
- Added a real create/write/close probe and interactive confirmation for
  unwritable portable user-data directories (`df303bce30`).

### Rendering

- Added validated batched drawable uploads across images, regions, mip levels,
  and array layers to reduce per-transfer overhead (`c136bdcca8`).
- Allowed compatible canvas materials on texture pages while retaining the
  existing page-path safety guards (`1554d1e697`).

### Artifact provenance

All artifacts below identify source `713fbf4a6fc63161179c0c172681ce38124293ba`.

| Package | SHA-256 |
| --- | --- |
| `Godot.NET.Sdk.4.7.1-outpostia.3.nupkg` | `5d700d6447e660642668c0ee7dd196e3b92b6931bce14bf4ca92294f0fa03fc4` |
| `Godot.SourceGenerators.4.7.1-outpostia.3.nupkg` | `5cdbb94653cd3398b2547e8f4d17182bdc110f3b512e05a996585dd3de5ea738` |
| `GodotSharp.4.7.1-outpostia.3.nupkg` | `a878fa24461f07474148221aad5b4e19a76e7676e8b69ebbe3b6a2735190375a` |
| `GodotSharp.4.7.1-outpostia.3.snupkg` | `112cbed55602f5588c0e8c845776c1fa9c8fb0f98db73d753402b3bdc33a9c24` |
| `GodotSharpEditor.4.7.1-outpostia.3.nupkg` | `4c54fe6b45d51681623d475641783189468b4f07952a6c5595e9bc91ea1ba441` |
| `GodotSharpEditor.4.7.1-outpostia.3.snupkg` | `22068678bbb757bfc1812381a2baf6e8d40348d3fed39de97a1f628041b4cafe` |

| Archive | SHA-256 |
| --- | --- |
| `Godot_v4.7.1-outpostia.3-win64_editor.zip` | `ab0151bfce9e24bfbc6d75dd1400fd8d80e28234078dab0bd483b26d3127622e` |
| `Godot_v4.7.1-outpostia.3-linux64_editor.zip` | `ee72b1a4b0c1e7b17c4000d65c36bf7255f5a8bc9812299417804dbab5dbf0ce` |
| `Godot_v4.7.1-outpostia.3-templates.zip` | `43127c826b327bb248e19b34b3a8f32c216d56dfeaa9d95c3e33a041eaad214b` |

## 4.7.1-outpostia.2

Date: 2026-08-01

Base: Godot `4.7.1-stable` (`a13da4feb8d8aefc283c3763d33a2f170a18d541`)

Tag and source: `4.7.1-outpostia.2` (`0d31e0905586c4b704a5a96a223e5d73ccf97112`)

- Added checked exact subresource transfers for drawable textures
  (`65d8cd34e8`).
- Added mip-isolated sampling for canvas texture pages (`bd93548884`).
- Added checked layered storage and transfers for drawable arrays
  (`0e8f1fb4b0`).
- Added per-instance array-layer sampling for canvas texture pages
  (`90431573f9`).
- Set the artifact identity to `4.7.1-outpostia.2` (`0d31e09055`).

These changes support Outpostia's shared texture-page renderer without leaking
between mip levels or array layers.

## 4.7.1-outpostia.1

Date: 2026-07-25

Base: Godot `4.7.1-stable` (`a13da4feb8d8aefc283c3763d33a2f170a18d541`)

Tag and source: `4.7.1-outpostia.1` (`e1e0cb6cde907006cc4c19ad2401ca7599237fda`)

- Configured the reduced Outpostia module profile and coherent fork identity,
  then added the canonical distribution pipeline (`e2c1f83f84`,
  `2079824f44`, `d9fd1d29af`).
- Reimplemented portable mode and the `--user-data-dir` override so packaged
  builds and parallel workspaces can isolate user data (`5d1fbabb93`,
  `323f080251`).
- Reimplemented coverage-friendly managed assembly loading and configurable
  Godot analyzers (`a4783bf40c`, `4c70fd258a`).
- Reimplemented the fallback-locale, Y-sort-offset, and Tree viewport/scrollbar
  APIs required by Outpostia (`90278a4f4b`, `1ec0dd0ab4`, `8ecf936c74`).
- Carried the custom-canvas RID double-free and synchronous shader-property-list
  corrections (`d7c5c28911`, `d8e343fcb5`).
- Backported two maximum-size propagation corrections without adopting the
  broader later-upstream desired-size chain (`1e9792bd86`, `ed1bc0878c`).
- Applied command-line window modes before display creation and made rejected
  empty texture updates verbose-only (`e1241d7793`, `12899d7b8a`).
- Reused cached drawable mip views to stop persistent view allocation during
  mipmap generation (`e1e0cb6cde`).

## Earlier fork lines

### 4.6.0-outpostia

Base: Godot `4.6-stable` (`89cea14398`)

Final branch head: `24d72218e863d065e91db0d57372d29204b2ac09`

This line accumulated changes under one unrevisioned fork version. It introduced
the reduced module profile (`8509262050`), portable mode (`eefe3ea413`),
`--user-data-dir` (`52e1e35317`), coverage-friendly assembly loading
(`087bb887a7`), configurable analyzers (`24d72218e8`), fallback-locale bindings
(`0833d457aa`), Y-sort offset (`dc292a9ff7`), `--minimized` (`d9c96ce2ad`),
Tree scrollbar access (`68beeb896e`), and the fork identity (`56b7dfc34f`).

### 4.5.1-outpostia

Base: Godot `4.5.1-stable` (`f62fdbde15`)

Final branch head: `f65e1d8128cc2b0e989190219673c658e01fda03`

This first custom line added Y-sort offset (`c149df2bfa`), the Outpostia version
suffix (`e4e56c82f4`), and the corresponding `CanvasItem` documentation
(`f65e1d8128`).

## Reimplementation ledger

These identifiers remain stable when the fork is rebuilt on a new upstream
release and the implementation commits change.

| Change | 4.5.1 | 4.6 | 4.7.1 | Status |
| --- | --- | --- | --- | --- |
| `GODOT-CANVAS-001` Y-sort offset | `c149df2bfa` | `dc292a9ff7` | `1ec0dd0ab4` | Active |
| `GODOT-DATA-001` Portable mode | — | `eefe3ea413` | `5d1fbabb93` | Active |
| `GODOT-DATA-002` User-data override | — | `52e1e35317` | `323f080251` | Active |
| `GODOT-CS-001` Coverage assembly loading | — | `087bb887a7` | `a4783bf40c` | Active |
| `GODOT-CS-002` Configurable analyzers | — | `24d72218e8` | `4c70fd258a` | Active |
| `GODOT-I18N-001` Fallback locale | — | `0833d457aa` | `90278a4f4b` | Active |
| `GODOT-TREE-001` Internal Tree access | — | `68beeb896e` | `8ecf936c74` | Expanded |
| `GODOT-WINDOW-001` Creation-time window mode | — | `d9c96ce2ad` | `e1241d7793` | Expanded |
