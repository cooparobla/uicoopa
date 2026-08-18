# uicoopa

A header-only, Unity-style `RectTransform` UI system for C++20, rendered through
[gfxcoopa](../gfxcoopa) (Vulkan) and built on [libcoopa](../libcoopa)'s
`coopa::scene::SceneObject`/`Component` scene graph. Anchored panels, layout groups,
word-wrapped text, and interactive widgets — composited on top of an existing Vulkan
render pass rather than owning one of its own.

## What `uicoopa/` offers

Everything under `uicoopa/` is header-only (`namespace coopa::ui`), with no `.cpp` files.
Composition follows a `coopa::scene::SceneObject` tree: every UI node carries a
`RectTransform` plus zero or more of the components below.

### Layout (`uicoopa/layout/`)
- **`rect.h`** — the pure math core: `Rect`, `RectParams`, and `resolve_rect()`. Anchor-based
  resolution modeled directly on Unity's `RectTransform` (`anchor_min`/`anchor_max`, `pivot`,
  `anchored_position`, `size_delta`). Independent of Vulkan and `coopa::scene`, so it's
  exercised directly by headless tests.
- **`rect_transform.h`** — the `RectTransform` component every UI object has exactly one of:
  named anchor presets (`TopLeft`, `StretchAll`, `MiddleCenter`, ...), optional local
  rotation/scale about the pivot, and the resolved `Rect` other components read.
- **`canvas.h`** — `CanvasComponent`, the root driver. Runs an explicit three-pass rebuild
  each frame: bottom-up **measure**, top-down **arrange** (`RectTransform::resolve()` +
  `on_rect_changed()`), then **emit** (draw list population). This explicit pipeline — not
  `SceneObject`'s plain pre-order `update()` walk — is what lets layout groups compute an
  aggregate child size before their own rect is resolved.
- **`canvas_scaler.h`** — `CanvasScaler`, screen-size-independent scaling (Unity's
  `CanvasScaler` equivalent): a reference resolution plus a width/height blend factor.
- **`layout_element.h`** — `LayoutElement`, explicit min/preferred/flexible size overrides
  for a child inside a layout group, and an `ignore_layout` escape hatch.

### Rendering (`uicoopa/render/`)
- **`ui_vertex.h`** — the single 2D vertex format (`UiVertex`) every draw call uses, plus
  `pack_color()` for the packed RGBA8 vertex color.
- **`draw_list.h`** — `DrawList`, accumulated per frame and batched by `(VkImageView, clip
  Rect)` into indexed draw calls. Carries raw `VkImageView`s rather than resolved descriptor
  sets, so it has no notion of Vulkan descriptors at all.
- **`ui_pass.h`** — `UiPass`, the actual Vulkan pipeline: owns the per-frame streaming
  vertex/index buffers and an image-view-to-descriptor-set cache, and **composites into the
  same render pass an existing 3D pass already opened** rather than clearing and owning its
  own — the render pass it's built against always clears on load, so a second pass would
  erase whatever was drawn before it.
- **`texture.h`**, **`sprite.h`** — `Texture` (host-pixel-data upload to a device-local
  Vulkan image) and `Sprite` (a named sub-region of a `Texture`, with optional 9-slice
  border insets).

### Text (`uicoopa/text/`)
- **`font_atlas.h`** — `FontAtlas`, an R8 coverage atlas baked from a TrueType font at one
  fixed pixel size via stb_truetype.
- **`font.h`** — `Font`, owning one `FontAtlas` per size actually requested, plus the
  word-wrap layout logic (`layout()`) shared between `Text::emit()`'s per-glyph placement and
  `ContentSizeFitter`'s aggregate-size-only queries.

### Widgets (`uicoopa/widgets/`)
- **`graphic.h`** — `Graphic`, the abstract base for anything visible and raycastable: owns
  `color` (`glm::vec4` RGBA — opacity is just `color.a`, there is no separate alpha concept)
  and `raycast_target`.
- **`image.h`** — `Image`, a solid-color or sprite-textured quad, with optional
  `ImageType::Sliced` nine-slicing.
- **`text.h`** — `Text`, glyph quads laid out from a `Font`, with horizontal/vertical
  alignment and `TextOverflow::{Overflow,Wrap,Truncate}`.
- **`button.h`** — `Button`: a hover/press color-and-opacity `ColorTransition` applied to a
  target `Graphic`, plus five `coopa::event::Signal`s (`on_click`, `on_hover_enter`,
  `on_hover_exit`, `on_press`, `on_release`) any number of independent listeners can
  `connect()` to. See **Events**, below.
- **`mask.h`** — `Mask`, clips its children's geometry to its own resolved rect via a
  push/pop scissor bracket around the emit pass.

### Groups (`uicoopa/groups/`)
- **`layout_group.h`** — `HorizontalLayoutGroup`/`VerticalLayoutGroup`: automatic child
  arrangement along one axis, with spacing, padding, per-axis child-control flags, and
  force-expand.
- **`grid_layout_group.h`** — `GridLayoutGroup`, a uniform grid of fixed-size cells.
- **`content_size_fitter.h`** — `ContentSizeFitter`, sizes its own `RectTransform` to fit its
  siblings' aggregate measured content.
- **`scroll_rect.h`** — `ScrollRect`, a scrollable viewport over an oversized `content` rect
  (drag and mouse-wheel), with `MovementType::{Unrestricted,Clamped,Elastic}`.

### Input (`uicoopa/input/`)
- **`ui_input.h`** — `UiInput`, per-frame mouse/keyboard state in canvas pixel space, built
  on top of gfxcoopa's `Window`. Adds edge detection (pressed/released *this* frame) on top
  of the window's level-triggered polling.
- **`raycaster.h`** — `Raycaster::hit_test()`, stateless point-in-UI hit testing. Visits
  children before parents and siblings in reverse array order — the exact reverse of draw
  order — so the topmost element always wins.
- **`event_system.h`** — `EventSystem::process()`, the hover/press/click state machine run
  once per frame: raycasts via `Raycaster`, tracks the hovered/pressed object across frames,
  and dispatches `PointerEventData` to every `IPointerHandler` on the hit object
  (`on_pointer_enter/exit/down/up/click/drag/scroll`). `Button` is the primary consumer.

### YAML loading (`uicoopa/ui_yaml.h`)
`register_ui_components()` registers a `!TypeName` parser for every widget/group above with
libcoopa's `coopa::scene::SceneLoader`, so a scene `.yaml` file can declare UI components
directly. One-directional bridge only — libcoopa has no dependency on uicoopa.

### Events: `coopa::event::Signal`
`Button`'s signals (and any future event-driven component) are built on
[libcoopa](../libcoopa)'s `coopa::event::Signal<Args...>` (`libcoopa/coopa/event/signal.h`) —
a generic, non-UI-specific multicast signal/slot type: `connect()` returns a `Connection`
token that stays safe to query/disconnect even after the `Signal` is destroyed,
`ScopedConnection` wraps that in move-only RAII, and `emit()` is safe to re-enter (a slot may
connect, disconnect itself, or even destroy its own `Signal` mid-emit). `uicoopa` doesn't
define its own event primitive — it's the reference consumer of libcoopa's.

### Base types
- **`ui_component.h`** — `UIComponent`, the base every widget above derives from. Extends
  `coopa::scene::Component` with the three hooks `CanvasComponent` drives explicitly:
  `measure()`, `on_rect_changed()`, and `emit()`/`on_children_emitted()`.

## `test_window.cpp` — the interactive demo

`test_window.cpp` opens a real GLFW/Vulkan window and exercises the library end to end. It is
**not** part of the headless test suite — see [Two targets](#two-targets) below — it's a
live, visual demo you run and look at / click on.

What it builds and what it's meant to show:

- **Anchored layout**: four corner panels (`TopLeft`/`TopRight`/`BottomLeft`/`BottomRight`)
  that stay pinned to their corner as the window resizes, a centered title + word-wrapped
  paragraph (`TextOverflow::Wrap`), and a `HorizontalLayoutGroup` bar of three
  equally force-expanded boxes along the bottom.
- **A `Button` wired entirely through `coopa::event::Signal`**, at the center of the screen:
  - Hovering it fades its own color *and* opacity via `ColorTransition`
    (`colors.normal.a = 0.55` → `colors.highlighted.a = 1.0`) — the button is deliberately
    translucent until you're pointing at it.
  - The **same** hover/press transitions independently drive a soft glow (`HoverHalo`) on a
    separate `SceneObject` behind the button, and a one-line status readout below it that
    reports the live cursor position and `on_hover_enter.slot_count()` — proof that multiple,
    mutually-unaware listeners can observe one `Signal`.
  - **Clicking it opens a modal dialog**: a full-screen semi-transparent scrim (which
    swallows clicks so they never reach the button underneath) plus a centered panel with
    its own title, body text, and hover-tinting **Close** button. The dialog is an ordinary
    `SceneObject` subtree, built active once (so its `Button::start()` runs) and then hidden
    with `set_active(false)` — shown again by `set_active(true)` from the main button's click.
  - A one-shot slot demonstrates a `Signal` slot **disconnecting itself from inside its own
    invocation** — safe by construction, not by convention (see `signal.h`'s re-entrancy
    notes).

### Running it

```bash
cbuild --vulkan   # or: cmake -B build && cmake --build build
./build/uicoopa_test_window
# or, via the local cplay/cbuild wrappers:
cplay
```
Resize the window to see the anchored panels track their corners. Hover and click the center
button. Press `ESC` to quit — a screenshot of the final frame is always written to
`output/test_window.png` on exit.

Environment variables for scripted, headless-friendly runs (no human required at the mouse):

| Variable | Effect |
|---|---|
| `MAX_FRAMES=<n>` | Exit automatically after `n` frames. |
| `ONESHOT=1` | Exit after the first frame. |
| `OPEN_DIALOG=1` | Start with the modal dialog already visible. |
| `FORCE_HOVER=1` | Emit the button's `on_hover_enter` signal once before the loop starts (with the layout resolved first), so the halo/label/status effects appear in a screenshot with no pointer anywhere near the button. |

```bash
MAX_FRAMES=5                 ./build/uicoopa_test_window   # base layout, dialog hidden
OPEN_DIALOG=1 MAX_FRAMES=5   ./build/uicoopa_test_window   # scrim + dialog + Close button
FORCE_HOVER=1 MAX_FRAMES=90  ./build/uicoopa_test_window   # signal-driven halo/label/status
```

### Two targets

`CMakeLists.txt` builds two executables from this repo:

| Target | Source | Purpose |
|---|---|---|
| `uicoopa` | `test.cpp` | Headless assertion-based test suite (`RUN_TEST`/`ASSERT_TRUE`). Registered with `ctest` via `add_test(NAME uicoopa_tests COMMAND uicoopa)` — no display required. |
| `uicoopa_test_window` | `test_window.cpp` | The interactive demo described above. |

Run the test suite with `./build/uicoopa` (or `ctest --test-dir build`); run the demo with
`./build/uicoopa_test_window` (or `cplay`, which keys off this `CMakeLists.txt`'s `project()`
name to pick the demo target).

## Consuming uicoopa

`uicoopa`, [libcoopa](../libcoopa), and [gfxcoopa](../gfxcoopa) are sibling, header-only
repositories with no install step — a downstream project (see this repo's own
`CMakeLists.txt`, or [blendy](../blendy)'s) just adds them as `include_directories()`:

```cmake
get_filename_component(ROOT_DIR_PARENT "${CMAKE_SOURCE_DIR}" DIRECTORY)
include_directories("${ROOT_DIR_PARENT}/libcoopa/includes/")   # fkYAML, glm, phmap
include_directories("${ROOT_DIR_PARENT}/libcoopa")             # coopa/
include_directories("${ROOT_DIR_PARENT}/gfxcoopa/includes/")   # volk, vma
include_directories("${ROOT_DIR_PARENT}/gfxcoopa")             # gfxcoopa/
include_directories("${CMAKE_SOURCE_DIR}/")                    # uicoopa/
```

Then `#include <uicoopa/...>` as needed — see `test_window.cpp` for the include list a full
application typically needs (layout, render, widgets, text, groups, input).

## Documentation

Doxygen-style comments throughout `uicoopa/` are formatted for `coopadocs`
(`.coopadocs` includes this repo's `uicoopa/` directory). Generate with:
```bash
coopadocs build
coopadocs show
```
