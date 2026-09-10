# uicoopa

A header-only, Unity-style `RectTransform` UI system for C++20, rendered through
[gfxcoopa](../gfxcoopa) (Vulkan) and built on [libcoopa](../libcoopa)'s
`coopa::scene::SceneObject`/`Component` scene graph. Anchored panels, layout groups,
word-wrapped text, interactive widgets, a fluent builder API (dialogs, tabs, split
layouts), declarative signal reactors, and UI sound — composited on top of an existing
Vulkan render pass rather than owning one of its own.

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
  rotation/scale about the pivot, `z_order` (elevates a subtree above later siblings and
  lets it escape an ancestor `Mask` — see `ComboBox`'s popup and `UIBuilder::dialog()`'s
  scrim/frame for why that matters), and the resolved `Rect` other components read.
- **`canvas.h`** — `CanvasComponent`, the root driver. Runs an explicit three-pass rebuild
  each frame: bottom-up **measure**, top-down **arrange** (`RectTransform::resolve()` +
  `on_rect_changed()`), then **emit** (draw list population). This explicit pipeline — not
  `SceneObject`'s plain pre-order `update()` walk — is what lets layout groups compute an
  aggregate child size before their own rect is resolved. `rebuild_layout()`/`rebuild_emit()`
  are public and independently callable — safe to use headlessly with no display, which is
  how the test suite (`test.cpp`) exercises resolved layout without a window.
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
- **`texture_factory.h`** — builds a device-local Vulkan image from host pixel data
  (`Texture`/`TextureView` themselves come from gfxcoopa).
- **`sprite.h`** — `Sprite`, a named sub-region of a texture, with optional 9-slice border
  insets.
- **`sprite_sheet.h`** / **`sprite_sheet_loader.h`** — `SpriteSheet`, a named multi-sprite
  atlas parsed from a YAML descriptor, with a `TypedAssetLoader` integration and hot-reload.
- **`icon_library.h`** — `IconLibrary`, a process-wide singleton (`IconLibrary::instance()`)
  resolving icon names to sprites from one or more loaded sheets; every builder factory that
  can draw a real icon (see `builder/` below) degrades gracefully to its pre-icon look when
  nothing is published.

### Text (`uicoopa/text/`)
- **`font_atlas.h`** — `FontAtlas`, an R8 coverage atlas baked from a TrueType font at one
  fixed pixel size via stb_truetype.
- **`font.h`** — `Font`, owning one `FontAtlas` per size actually requested, plus the
  word-wrap layout logic (`layout()`) shared between `Text::emit()`'s per-glyph placement and
  `ContentSizeFitter`'s aggregate-size-only queries.
- **`font_defaults.h`** — `FontDefaults`, the process-wide default-font holder every widget
  factory falls back to when a `UITheme` doesn't specify its own `Font*`.

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
- **`color_transition.h`** — `ColorTransition`, the shared four-state (normal/highlighted/
  pressed/disabled) tint-and-fade model used by `Button` and every other interactive widget
  below, including `TabView`'s tab buttons.
- **`toggle.h`** — `Toggle`, a checkbox/switch with a driven checkmark graphic and
  `Signal<bool> on_value_changed`.
- **`slider.h`** — `Slider`, a draggable handle over a track/fill, with min/max/step and
  `Signal<float> on_value_changed`.
- **`scrollbar.h`** — `Scrollbar`, a draggable handle representing `ScrollRect`'s scroll
  position; usually auto-managed by a `ScrollRect` rather than built directly.
- **`spinbox.h`** — `SpinBox`, a numeric stepper (`-`/readout/`+`) with click-to-edit text
  entry, built on `TextEditBase`.
- **`text_field.h`** / **`text_edit_base.h`** — `TextField`, a free-form editable text box
  (double-click to enter edit mode, blinking caret, Shift-select, Escape to revert);
  `TextEditBase` is the shared caret/selection/commit machinery `TextField` and `SpinBox`
  both build on.
- **`combobox.h`** — `ComboBox`, a dropdown button with a `z_order`-elevated, self-masked
  popup list of items; `Signal<int, const std::string&> on_selection_changed`.
- **`inventory_grid.h`** — `InventoryItem` (data model), `InventorySlot` (a drag source *and*
  drop target, with a hover tooltip), `InventoryGrid` (an R×C grid of slots with move/swap/
  stack-merge logic already wired).
- **`tab_view.h`** — `TabView`, owns N (tab button, page node) pairs and keeps exactly one
  page active; built by `UIBuilder::tab_view()` — see **Builder**, below, for the full
  contract (in particular, why it must not self-start the way `ComboBox`/`SpinBox` do).
- **`dialog.h`** — `Dialog`, a one-field component (`starts_open`) that applies a
  `UIBuilder::dialog()`'s initial open/closed state exactly once, at `start()` — see
  **Builder** for why that can't happen any earlier.
- **`mask.h`** — `Mask`, clips its children's geometry to its own resolved rect via a
  push/pop scissor bracket around the emit pass.
- **`cursor_overlay.h`** — `CursorOverlay`, the opt-in software cursor that replaces the OS
  pointer once `UIBuilder::enable_cursor()` installs it — see **Builder**'s `UITheme::cursor`
  section for the full picture (theme schema, hotspot convention, position/role lag trade-off).
- **`fill_direction.h`** — `SliderDirection` and `apply_fill_rect()`, the fill-bar anchor math
  shared verbatim by `Slider` and `ProgressBar` — extracted so the two can never drift apart.
- **`progress_bar.h`** — `ProgressBar`, a display-only fill bar (not a disabled `Slider` — see
  its own doc for why that would flip the software cursor to its disabled glyph on hover) with
  an optional delayed "chip damage" ghost trail and a formatted value label; `bind()`s directly
  to a `coopa::stat::Resource`.
- **`message_log.h`** — `MessageLog`, a capped, timed, fading line log (a pickup/kill feed, or
  — with `hold_seconds <= 0` — a never-expiring console scrollback) backed by a pool of reused
  `Text` children rather than rebuilt ones, so `LayoutGroupBase`'s inactive-child skip collapses
  expired lines for free.
- **`inventory_binding.h`** — `InventoryBinding`, the seam that makes an `InventoryGrid` a pure
  visualization of a `coopa::item::Inventory`: installs `InventoryGrid::transfer_override` to
  route every drag-drop/pick-place through `Inventory::move_or_merge()`, and mirrors the
  model's `on_slot_changed` back into the grid. See **Builder**'s hotbar section below.
- **`console.h`** — `ConsoleInput` (a `TextField` whose Enter submits-and-stays-editing and
  whose Escape/Up/Down are repurposed for cancel/history instead of TextEditBase's own
  revert-and-blur) and `Console` (toggle/focus/`ModalContext` lifecycle, scrollback, and a
  small named-command registry) — the backtick dev console; see `UIBuilder::add_console()`.

### Groups (`uicoopa/groups/`)
- **`layout_group.h`** — `HorizontalLayoutGroup`/`VerticalLayoutGroup`: automatic child
  arrangement along one axis, with spacing, padding, per-axis child-control flags, and
  force-expand. `LayoutGroupBase::child_distribute_by_weight` is a second, opt-in
  distribution mode used by `UIBuilder::split_rows()`/`split_columns()`: main-axis sizing
  ignores measured content entirely, giving a fixed child exactly its
  `LayoutElement::preferred_size` and dividing the remainder strictly by
  `LayoutElement::flexible_size` among the rest — a plain layout group can't express this,
  since its ordinary distribution both reports `min == preferred` (a content-bearing child
  can never shrink below its own content) and falls back to the *previous frame's* resolved
  size when nothing reports a preferred size (so a weight-only child's share silently
  latches after the first frame). Off by default; existing layouts are unaffected.
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
  (`on_pointer_enter/exit/down/up/click/double_click/drag/scroll`). `Button` is the primary
  consumer. Also handles click-outside blur into `FocusContext` and keyboard routing to the
  focused object — both gated by `ModalContext`, below, when a blocking dialog is open.
  `IPointerHandler::cursor_role()` (default `CursorRole::Default`) is a smaller, orthogonal
  virtual on the same interface — see **Builder**'s `UITheme::cursor` section for what
  consumes it.
- **`focus.h`** — `ITextInputHandler` and `FocusContext::instance()`, the process-wide
  keyboard-focus singleton (`focused()`, `clear_focus()`) `TextField`/`SpinBox` register with.
- **`modal_context.h`** — `ModalContext::instance()`, the process-wide stack of "blocking"
  subtree roots a `UIBuilder::dialog()`'s `Dialog` component pushes/pops itself onto (see
  **Dialogs**, below). `EventSystem::process()` reads it each frame: a pointer hit, an
  in-flight press/drag, or the currently focused object outside the topmost pushed root is
  treated as if nothing were there — a subtree-membership *policy*, not a z_order/geometry
  check, so it doesn't depend on the modal's scrim winning the raycast or geometrically
  covering the point in question.
- **`drag_drop.h`** — `DragPayload`, `IDragSource`, `IDropTarget`, and
  `DragDropContext::instance()`, the generalized drag-and-drop framework `InventorySlot`
  implements against.

### Builder (`uicoopa/builder/`)

A fluent, imperative, theme-driven construction API — the fastest way to stand up a real UI
without touching `SceneObject`/component wiring by hand. `test_settings_builder.cpp` and
`test_dialog_builder.cpp` (below) are worked examples; `test.cpp`'s `test_builder_*` tests
are the API contract in assertion form. `ui_builder.h` (the `UIBuilder` facade) and
`ui_theme.h`/`ui_theme_yaml.h` (`UITheme`/`ThemeLibrary`) are the only headers meant to be
included directly; each concern behind them — containers, standard widgets, labeled rows,
inventory grids, value get/set, splits, dialogs, tabs — lives in its own
`builder/detail/*.h`, so a consumer only pays to compile what they actually use.

`UIBuilder` (`ui_builder.h`) wraps a `SceneObject*` plus a `const UITheme*` and is a small,
freely-copyable value type. **Container** methods (`panel`, `vertical_layout`,
`horizontal_layout`, `grid_layout`, `scroll_view`, `card`, `split_rows`, `split_columns`,
`tab_view`, `dialog`) return a **new** builder/handle over the child they just created;
**modifier** methods (`with_theme`, `with_size`, `with_padding`, `with_background`, `at`,
`fit_content_height`, `fit_content_width`) return `*this`; **`add_*`** methods build one
widget and return its raw component pointer, taking `std::function` callbacks
(`add_label`, `add_paragraph`, `add_button`, `add_slider`, `add_toggle`, `add_spinbox`,
`add_dropdown`, `add_text_field`, `add_image`, `add_icon`, `add_icon_button`,
`add_inventory_grid`, `add_progress_bar`, `add_stat_bar`, `add_message_log`, `add_spacer`,
`add_separator`, `add_action_bar`, `add_icon_row`, plus `add_*_row` label-and-control variants
for settings forms); `add_hotbar`/`add_console` return small handle types for the same reason
`dialog()`/`tab_view()` do (see **Gameplay HUD**, below). A name-keyed
`get_value<T>(name)` / `set_value(name, val)` pair reads/writes any descendant widget's value
by the `SceneObject` name it was built with — this is why every widget factory names its
node after the caller's identifier.

```cpp
UIBuilder root(canvas_obj, &theme);
root.panel("Background")->get_component<Image>()->color = theme.panel.background;

UIBuilder settings = root.card("Settings", "Preferences", AnchorPreset::TopLeft, {20,-20}, {400,300});
settings.add_toggle_row("vsync", true);
settings.add_slider_row("volume", 0.0f, 1.0f, 0.8f);

bool vsync = settings.get_value<bool>("vsync");  // read back by name from anywhere
```

#### Splitting: `split_rows()` / `split_columns()`

Splits a node into a top-to-bottom (`split_rows`) or left-to-right (`split_columns`) row of
sections, replacing hand-computed `at(preset, {x,y}, {w,h})` offsets and magic-number widths:

```cpp
struct Section {
    std::string name;
    float weight = 1.0f;                      // share of remaining space when size <= 0
    float size   = 0.0f;                       // > 0: fixed extent along the split axis
    SectionFlow flow = SectionFlow::Vertical;   // layout group inside the section itself
    bool boxed = false;                         // paint theme.panel.panel_alt behind it
};

SectionSet cols = body.split_columns({
    {"Nav", 1.0f, 220.0f},   // fixed 220px
    {"Main", 1.0f},          // fills the rest
});
cols["Nav"].add_button("Settings");
cols["Main"].add_label("Content");
```
Returns a `SectionSet`: `operator[](index_or_name)` gives a `UIBuilder` over that section,
`container()` gives the split's own container, and it's range-for iterable.

#### Dialogs: `dialog()`

```cpp
enum class DialogMode { Embedded, Window, Modal };
```
- `Embedded` — a `StretchAll` child of the calling node; no scrim, no floating frame. The
  "the dialog is just the display canvas" case.
- `Window` — a centered titled frame, no scrim.
- `Modal` — full-screen click-swallowing scrim + centered frame.

```cpp
DialogHandle confirm = root.dialog("Confirm", "Discard changes?", {360,170}, DialogMode::Modal);
confirm.body().add_paragraph("This can't be undone.");
confirm.add_action("Cancel", ButtonRole::Neutral, [confirm]() { confirm.hide(); });
confirm.add_action("Discard", ButtonRole::Primary, [confirm]() { /* ...; */ confirm.hide(); });
```
`DialogHandle` is a small copyable value type (`body()`, `header()`, `footer()`,
`close_button()`, `show()`/`hide()`/`toggle()`/`is_open()`, `add_action()`,
`split_rows()`/`split_columns()` forwarded to `body()`). **Important:** a dialog is built
(and left) *active* in every mode, including `Modal` — its `Dialog` component (see
`widgets/dialog.h`) applies the real initial state at `start()`, not at construction. This
is deliberate: the body/footer are meant to be populated by the caller *after* `dialog()`
returns, and hiding the subtree before that population would mean any widget added
afterward never gets its own `start()` run (`SceneObject::start()` early-returns on an
inactive object — see its doc). Consequence: `is_open()` reports `true` for a fresh Modal
dialog until `start()` has run at least once, which normally "just happens" — the app calls
`Scene::start()` a single time, after the whole UI (every dialog's content included) is
built. A dialog built and populated *after* that one `Scene::start()` call already ran (e.g.
spawned at runtime) needs the same call made manually once population is complete —
the same requirement any other widget has in that situation.

**Blocking.** `dialog()` takes a trailing `bool blocks_input = true`, meaningful only for
`DialogMode::Modal` (`Window`/`Embedded` never block, regardless of this argument). While a
blocking dialog is open, `ModalContext` (see **Input**, above) makes pointer hover/press/
click/drag/scroll *and* keyboard focus outside its subtree behave as if nothing were there —
not just clicks: a `TextField` focused before the dialog opened stops receiving keystrokes
the instant it's blocked, and opening the dialog proactively blurs it (so it also stops
rendering a caret). A drag already in progress when the dialog opens is cleanly released
rather than left driving the widget behind it. This is global to wherever the dialog's
`EventSystem`/canvas is — independent of z_order (so it can't lose a tie to, say, an open
`ComboBox` popup elsewhere) and independent of which node `dialog()` was called on — but the
scrim's own *visual* darkening still only spans the node `dialog()` was called on; build on
the canvas root for full-screen dimming. `DialogHandle::show()/hide()/toggle()`, the close
button, and an optional scrim click (`close_on_scrim_click`) all route through the same
`Dialog` component (`widgets/dialog.h`) choke point, so `ModalContext` never falls out of
sync with however the dialog was actually opened or closed.

#### Tabs: `tab_view()`

```cpp
TabSet tabs = root.tab_view("Settings", {"Display", "Audio", "Controls"});
tabs["Display"].add_toggle_row("vsync", true);
tabs["Audio"].add_slider_row("volume", 0.0f, 1.0f, 0.8f);
tabs.select(1);  // switch pages programmatically; also wired to each tab button's click
```
Builds a `TabBar` of buttons above a `Pages` node holding one always-populatable
`VerticalLayoutGroup` page per label (`TabSet::operator[]`, `bar()`, `component()`,
`select()`, range-for iterable). The selected tab is told apart by swapping its **whole**
`ColorTransition` (`theme.tab.normal*` ↔ `theme.tab.selected*`) plus an underline indicator
child — never by writing `Image::color` directly, since `Button::update()` recomputes it from
`Button::colors` every frame and would overwrite a direct write. **Same start()-timing
contract as `dialog()`, for the same reason**: pages are built empty and returned for the
caller to populate, so `tab_view()` deliberately does *not* self-start the way
`add_dropdown()`/`add_spinbox()` do — `TabView::start()` (which starts every page's subtree
before hiding all but the selected one) must run only after every page's content already
exists, which is exactly when the app's one `Scene::start()` call reaches it.

#### Gameplay HUD: `hud_layer()` / `hud_corner()` / stat bars / hotbar / console

The pieces every gameplay HUD needs, built the same UIBuilder way as everything else —
see `test_hud_builder.cpp` (below) for a complete worked example, and
`widgets/inventory_binding.h`'s file doc for the "the UI visualizes a `coopa::item`/
`coopa::stat` model, it doesn't own one" contract this whole family follows.

```cpp
UIBuilder hud = root.hud_layer();  // full-canvas, non-interactive root (children still hit-test)

hud.hud_corner(HudAnchor::TopLeft, {320, 24}).add_status_line("Objective: survive the night");

UIBuilder top_right = hud.hud_corner(HudAnchor::TopRight, {260, 70});
top_right.add_stat_bar("Health", "heart", &stats.resource("health"), ProgressBarRole::Health);
top_right.add_stat_bar("Stamina", "bolt", &stats.resource("stamina"), ProgressBarRole::Stamina);

MessageLog* log = hud.hud_corner(HudAnchor::BottomLeft, {420, 160}).add_message_log("Log", 8);

HotbarHandle hb = hud.hud_corner(HudAnchor::BottomCenter, {520, 60}, -1, SectionFlow::None)
                      .add_hotbar("Hotbar", &hotbar, &item_db);

ConsoleHandle console = root.add_console("Console", app_input);
console.register_command("give", "give <id> [n]", [&](const std::vector<std::string>& args) { ... });
```

- **`hud_layer(name)`** — a `StretchAll`, `hittable = false` node. Its own `hittable = false`
  only silences *itself* for raycasting; `Raycaster::hit_test_all_()` still recurses into (and
  hit-tests) its children regardless — the mechanism that lets a fully interactive hotbar live
  under a HUD layer that never itself steals a click meant for gameplay underneath it.
- **`hud_corner(anchor, size, margin = -1, flow = SectionFlow::Vertical, name = "")`** — a
  fixed-size region docked to one of eight non-centered `HudAnchor` points
  (`TopLeft/TopCenter/TopRight/MiddleLeft/MiddleRight/BottomLeft/BottomCenter/BottomRight`),
  inset by `margin` (`< 0` → `theme.hud.corner_margin`). `flow` adds a matching layout group
  packed toward that same corner point, or `SectionFlow::None` for a single pre-sized child
  (e.g. a hotbar grid that already sizes itself).
- **`add_progress_bar(...)`** — a display-only fill bar (see `ProgressBar`'s own doc for why
  it isn't just a disabled `Slider`); **`add_stat_bar(name, icon, resource, role, width)`** —
  the standard "icon + labeled bar" row, `bind()`ing the bar to a `coopa::stat::Resource`
  immediately if one is given.
- **`add_message_log(name, max_lines, size, boxed)`** — a timed, fading line log; set the
  returned `MessageLog::hold_seconds <= 0` for a never-expiring scrollback instead (which is
  exactly what `add_console()` does internally for its own scrollback).
- **`add_hotbar(name, hotbar, database, slot_size, slot_spacing, key_labels)`** — a 1×N
  `InventoryGrid` with "1".."9" key labels, bound to `hotbar`'s backing `coopa::item::Inventory`
  via `InventoryBinding`. Returns a `HotbarHandle` (`grid()`, `binding()`, `model()`, `node()`,
  `select(i)`, `selected()`). **Selection is one-way, model → view**: every input path (number
  keys, the scroll wheel, a slot click) calls `coopa::item::Hotbar::select()`/`next()`/`prev()`
  and lets `on_selection_changed` drive the grid's highlight back around — the grid never owns
  selection itself.
- **`add_console(name, app_input, height = -1)`** — a backtick-toggled dev console: an
  always-active layer (polls `app_input.key_pressed(toggle_key)` in `late_update()`, since
  keyboard events are only ever dispatched to `FocusContext::focused()` — a *closed* console,
  focused by nothing, could never otherwise learn the key was pressed) over a panel that
  pushes/pops itself on `ModalContext` while open, exactly like `Dialog` — except, unlike
  `Dialog`, `Console`'s destructor actually removes itself. Returns a `ConsoleHandle`
  (`component()`, `input()`, `scrollback()`, `open/close/toggle/is_open`, `register_command`,
  `echo`). `ConsoleInput` (a `TextField` subclass) makes Enter submit-and-stay-editing instead
  of committing-and-blurring, repurposes Escape/Up/Down for cancel/history, and refuses to
  ever type the backtick that opened it.

#### `UITheme` / `ThemeLibrary` (`ui_theme.h`, `ui_theme_yaml.h`)

`UITheme` is grouped into per-widget-family style structs (`PanelStyle`, `TypographyStyle`,
`ButtonStyle` ×3 roles, `SliderStyle`, `ToggleStyle`, `SpinBoxStyle`, `ComboBoxStyle`,
`SlotStyle`, `IconStyle`, `CursorStyle`, `MetricsStyle`, `TabStyle`, `HudStyle`) rather than one flat bag of fields, so
the shape matches the YAML schema `ui_theme_yaml.h` parses field-for-field. `UITheme::
builtin_dark()`/`builtin_light()` are the two built-in palettes; `ThemeLibrary::instance()`
loads/caches theme files and tracks the single process-wide active theme, falling back to
`builtin_dark()` (after one warning) if nothing else is configured — so headless code always
gets a usable theme. **Any new theme field must be added in three places, kept in lockstep**:
`ui_theme.h` (the field + both builtins), `ui_theme_yaml.h` (`parse_theme()`'s matching
`contains()` line), and `assets/themes/{dark,light}.yaml` (for parity — every YAML key is
optional and omitted keys inherit `builtin_dark()`'s value, so a *color* left out of
`light.yaml` specifically would render dark in the light theme).

`TypographyStyle` additionally carries six `FontRoleStyle` fields — `title`, `heading`,
`body`, `label`, `caption`, `numeric` — each an optional `{path, size}` pair letting a theme
assign a different font *and* size per typographic category (e.g. a display face for titles,
a tabular-figures mono for digit readouts), instead of every widget sharing one `font_path`
at one of `size_title`/`size_label`/`size_small`. An omitted `path` inherits `font_path`; an
omitted (or `<= 0`) `size` inherits that category's `size_title`/`size_heading`/`size_body`/
`size_label`/`size_small`/`size_label` scalar respectively (see `FontRole`'s doc in
`detail/build_context.h` for the exact table) — so existing theme files with only the legacy
`text:` keys parse unchanged. In YAML:

```yaml
text:
  font_path: ../fonts/DejaVuSans.ttf   # fallback for any role without its own path
  fonts:
    title:   { path: ../fonts/Inter-SemiBold.ttf }        # size omitted -> inherits size_title
    numeric: { path: ../fonts/JetBrainsMono-Regular.ttf, size: 13.0 }  # explicit size wins
```

Builder factories resolve a role via `detail::apply_role_font(txt, theme, FontRole::X)` /
`detail::measure_role_text(theme, FontRole::X, text)` (`detail/text_style.h`) rather than
calling `apply_font()` with a raw theme size — see `detail/widgets.h`'s label/button/spinbox
factories for the pattern. A role's `FontRoleStyle::font` is populated from its `path` when a
theme loads through `ThemeLibrary` (`ui_theme_yaml.h`'s `resolve_theme_fonts()`, via the
`FontDefaults::resolve_font` hook an application wires to its GPU font loader — see
`register_ui_components()`'s GPU overload, or `test_settings_builder.cpp`'s manual wiring for
an app that never calls `register_ui_components()`); a null role font falls back to
`UITheme::font`, then `FontDefaults::font`, exactly like `apply_font()` always has.

#### Themed cursor: `CursorStyle`, `CursorRole`, `enable_cursor()`

`UITheme::cursor` (`CursorStyle`) is unlike every other theme field in one respect: it drives
nothing by default. It only takes effect once an application calls
`UIBuilder::enable_cursor(input)` (or the free-function form, `coopa::ui::enable_cursor(node,
theme, input)`, for scenes with no `UIBuilder` in scope — see `test_window.cpp`) — that
installs a `CursorOverlay` (`widgets/cursor_overlay.h`) at the root of the calling canvas,
hides the OS pointer (`coopa::input::Input::set_cursor_mode(Hidden)`), and from then on draws
a themed, IconLibrary-backed sprite that tracks the mouse and swaps icon automatically based
on what's hovered:

```yaml
cursor:
  size: 24.0
  color: { r: 1.0, g: 1.0, b: 1.0, a: 1.0 }   # tint; light.yaml darkens this for contrast
  default:  { icon: cursor_default,  hotspot: { x: 0.19, y: 0.16 } }  # CursorRole::Default
  pointer:  { icon: cursor_pointer,  hotspot: { x: 0.50, y: 0.19 } }  # clickable widgets
  text:     { icon: cursor_text,     hotspot: { x: 0.50, y: 0.50 } }  # editable text fields
  disabled: { icon: cursor_disabled, hotspot: { x: 0.50, y: 0.50 } }  # non-interactable widgets
```

`hotspot` is a `[0,1]` fraction, in image convention (top-left origin, +Y down, same as every
other generated icon), marking the point that should land exactly on the real cursor position
(an arrow's tip, an I-beam's center, ...). The four default icons come from
`tools/gen_default_cursors.py`, a sibling of `gen_default_icons.py` writing a separate sheet
(`assets/icons/cursors.png`/`cursors.yaml`) so cursor changes never touch the main icon sheet's
byte-for-byte reproducibility.

Which icon shows is driven by `CursorRole` (`input/event_system.h`) and a new
`IPointerHandler::cursor_role()` virtual (default `CursorRole::Default`) that `Button`,
`Toggle`, `Slider`, `Scrollbar`, and `TextEditBase` (so `TextField`/`SpinBox`) override,
returning `Disabled` instead of their usual `Pointer`/`Text` when `interactable` is false.
`CursorOverlay::update()` reads whichever handler is on `EventSystem::hovered_object()` each
frame. Position tracking is zero-lag (written from `update()`, which the whole scene finishes
before any canvas's `late_update()` — the same layout/emit pass — runs this same frame); role
selection lags by exactly one frame for the same reason `hovered_object()` itself does (it's
only updated inside that `late_update()`) — an imperceptible trade-off documented on
`CursorOverlay` itself.

### Reactors (`uicoopa/reactors/`)

The declarative counterpart to wiring signals by hand in C++: a `SignalReactor` is an
ordinary `coopa::scene::Component` (not a `UIComponent`) that connects to the owning Scene's
name-addressed `coopa::event::EventBus` in its own `start()` and performs one predefined
action whenever `(listen_object, listen_signal)` fires — attach one to an object in scene
YAML and it reacts entirely on its own, no C++ code holding a pointer to either the emitter
or the listener. `listen_object` empty means "any object" (wildcard); `target` empty means
"self" (the reactor's own owner) — the common case of a modal dialog listening for a button's
click and toggling its own active state.

- **`signal_reactor.h`** — `SignalReactor`, the shared base above.
- **`set_active_on_signal.h`** — `SetActiveOnSignal`, flips `resolve_target()->active()`.
- **`color_on_signal.h`** — `ColorOnSignal`, fades a `Graphic`'s color toward a target.
- **`text_on_signal.h`** — `TextOnSignal`, sets a `Text`'s string.
- **`log_on_signal.h`** — `LogOnSignal`, prints the signal to stderr — useful while wiring a
  new scene up.

`uicoopa/builder/`'s `DialogHandle`/`TabSet` are the imperative equivalent of what
`SetActiveOnSignal` does declaratively; the two are not currently interchangeable —
`Dialog`/`TabView` have no `!TypeName` YAML tag (`TabView` is a plausible future one; `Dialog`
would need a way to express its whole composite subtree, which `ui_yaml.h` has no precedent
for).

### Audio (`uicoopa/audio/`)

Optional (`UICOOPA_HAS_AUDIO`, see [Building](#building), below) UI sound wired the same
declarative way as reactors. Every audio call site is guarded by `#ifdef UICOOPA_HAS_AUDIO`,
so both configurations build cleanly.

- **`ui_audio.h`** — `UiAudio`, the device wrapper (built on miniaudio), with a process-wide
  `UiAudio::active()`/`set_active()` static pointer — deliberately mirroring
  `FontDefaults::font`'s pattern, so components can reach "the" audio device without a
  constructor parameter.
- **`sound_library.h`** — `SoundLibrary::instance()` + `SoundDef`, mapping sound names to
  clip files, loaded from a manifest.
- **`ui_sound_scheme.h`** — `SoundCue`/`UiSoundScheme`, mapping UI event names ("hover_enter",
  "click", ...) to sound names.
- **`ui_sound_player.h`** — `UiSoundPlayer`, a `coopa::scene::Component` that auto-connects to
  every widget's signals it finds under its own subtree via `ScopedConnection`s — one
  instance on a Canvas gives every `Button` in the whole UI hover/click sounds with zero
  per-button wiring (see `test_settings_builder.cpp`'s/`test_dialog_builder.cpp`'s `main()`).
- **`play_sound_on_signal.h`** — `PlaySoundOnSignal : SignalReactor`, the reactor form.
- **`audio_yaml.h`** — registers `!UiSoundPlayer`/`!PlaySoundOnSignal` YAML parsers with
  `SceneLoader`; kept separate from `ui_yaml.h::register_ui_components()` (rather than folded
  in) so that function — included unconditionally by `test.cpp` — keeps building with audio
  disabled. An application opts into audio-scene-loading by calling this alongside
  `register_ui_components()`.

### YAML loading (`uicoopa/ui_yaml.h`)
`register_ui_components()` registers a `!TypeName` parser for every widget/group/reactor
above with libcoopa's `coopa::scene::SceneLoader`, so a scene `.yaml` file can declare UI
components directly (`test_window.cpp`'s `assets/scenes/test_window/scene.yaml` is the
reference example). One-directional bridge only — libcoopa has no dependency on uicoopa. The
`builder/` API above is the *imperative* path and has no YAML counterpart for its composite
containers (`panel`/`card`/`scroll_view`/`split_rows`/`dialog`/`tab_view`) — only the leaf
widgets it assembles are individually YAML-expressible.

`register_ui_animated_properties()` — called automatically by `register_ui_components()`,
and directly by apps (like `test_settings_builder.cpp`/`test_dialog_builder.cpp`) that build
their UI imperatively and never call `register_ui_components()` at all — registers every
field below with libcoopa's `coopa::anim::AnimatedPropertyRegistry`, so a `coopa::anim::
Animator` can drive them by name from a clip, exactly as if libcoopa itself defined
`RectTransform`:

| Component | Property (registry key) | Type | Notes |
|---|---|---|---|
| `RectTransform` | `anchor_min`, `anchor_max`, `pivot`, `anchored_position`, `size_delta`, `scale` | `glm::vec2` | Overwritten every frame on any child of a layout group ([`layout_group.h`](uicoopa/groups/layout_group.h)'s `place_child()`) — unsafe there without `LayoutElement{ignore_layout = true}`. |
| `RectTransform` | `offset_min`, `offset_max` | `glm::vec2` | Derived from `anchored_position`/`size_delta`; same layout-group caveat. |
| `RectTransform` | `rotation` | `float` | Never touched by layout — always safe. |
| `Image`, `Text` | `color` | `glm::vec4` | Inherited from `Graphic`; never touched by layout. Contested by a `Button`'s own color chase or a `ColorOnSignal` if the same object has one — see `coopa/scene/README.md`'s "Update Phases" for why an `Animator` track wins that race deterministically. |

#### `inherit_from` — prefabs and scene variants

Any object node, or the top-level `scene:` block, can carry `inherit_from: <path.yaml>` to
merge in another file's object/scene as a base before its own fields override it — see
[`libcoopa/coopa/scene/scene_inherit.h`](../libcoopa/coopa/scene/scene_inherit.h) for the full
merge rules (component/child matching, `id:`/`remove:` keys, asset-path provenance). This repo's
own `assets/scenes/test_window/scene.yaml` is the reference example: the four corner panels
pull from `assets/prefabs/corner_panel.yaml`, the three bar boxes from `bar_box.yaml`, and the
entire modal dialog subtree from `dialog.yaml`, each overriding only what actually differs (a
color, a position, a label). `assets/scenes/test_window_variant/scene.yaml` shows the
scene-level form — it inherits the whole of `test_window/scene.yaml`, recolors one panel, and
removes another with `remove: true`.

A relative path inside a prefab (e.g. `font: ../fonts/DejaVuSans.ttf` in `corner_panel.yaml`)
resolves against the prefab's own directory, not the including scene's — `ParseContext::resolve()`
handles this via per-node provenance, so prefabs stay relocatable.

### Events: `coopa::event::Signal`
`Button`'s signals (and any other event-driven component — `Toggle`, `Slider`, `SpinBox`,
`TextField`, `ComboBox`, `TabView`, `InventoryGrid`) are built on [libcoopa](../libcoopa)'s
`coopa::event::Signal<Args...>` (`libcoopa/coopa/event/signal.h`) — a generic, non-UI-specific
multicast signal/slot type: `connect()` returns a `Connection` token that stays safe to
query/disconnect even after the `Signal` is destroyed, `ScopedConnection` wraps that in
move-only RAII, and `emit()` is safe to re-enter (a slot may connect, disconnect itself, or
even destroy its own `Signal` mid-emit). `uicoopa` doesn't define its own event primitive —
it's the reference consumer of libcoopa's. Each such widget *also* publishes the same
transition on the owning Scene's named `EventBus` (`scene.events().on(object_name,
signal_name, handler)`), which is what makes `reactors/` possible with zero pointers to a
specific widget instance.

### Base types
- **`ui_component.h`** — `UIComponent`, the base every widget above derives from. Extends
  `coopa::scene::Component` with the three hooks `CanvasComponent` drives explicitly:
  `measure()`, `on_rect_changed()`, and `emit()`/`on_children_emitted()`.

## The demos

Three windowed, interactive demos, each its own build target — see [Building](#building) and
the table below. None of them are part of the headless test suite (`test.cpp`'s `uicoopa`
target) — that's deliberate, so `ctest`/CI never needs a display.

### `test_window.cpp` — the YAML-declarative demo

Loads `assets/scenes/test_window/scene.yaml` and drives it through nothing but
`Scene::update()`/`late_update()` — there is no separate UI wrapper class:
`CanvasComponent` is a normal scene-graph component that does its own layout/emit/input
work from `late_update()`. Every response to the button — a halo bloom, the button label's
hover tint, a status-line readout, a click console log, opening/closing a modal dialog — is
declared as a `SignalReactor` component directly in `scene.yaml`, with zero signal-wiring
code in the C++ file. Proves: anchored layout tracking window resizes, a
`HorizontalLayoutGroup` bar, a `Button` wired entirely through `coopa::event::Signal` (with
two unrelated listeners on the same signal), and a hand-authored modal dialog subtree
(`assets/prefabs/dialog.yaml`, `inherit_from`'d in) toggled by `SetActiveOnSignal`.

### `test_settings_builder.cpp` — the UIBuilder settings-panel demo

The same settings-inspector shape, but built entirely through `UIBuilder` — zero scene YAML
except the theme file. A tabbed settings inspector (`tab_view()`, seven pages: Display,
Graphics, Audio, Gameplay, Controls, Accessibility, Network), an info banner and an action
panel each `split_rows()` into labeled sections instead of hand-positioned children, a live
status readout that re-reads the settings panel's values every frame via
`UIBuilder::get_value()`, a drag-and-drop inventory grid, and a `DialogMode::Modal`
confirmation the Reset button opens instead of resetting immediately.

### `test_dialog_builder.cpp` — the split/dialog/tab demo

The focused proof for the newest `builder/` additions, composed three levels deep: an
`Embedded` dialog spans the whole canvas, `split_columns()` makes a fixed-width Nav column
and a weighted Main column, `tab_view()` switches three pages inside Main (one of which
nests its own `split_rows()`), and Nav's buttons open a `DialogMode::Window` ("About") and a
`DialogMode::Modal` ("Discard Changes?") — both stacking correctly over the tabbed content
beneath them.

### `test_hud_builder.cpp` — the gameplay HUD demo

The worked example for **Gameplay HUD** (above): a hotbar (9 slots, keys 1-9, bound to a
`coopa::item::Inventory`/`Hotbar` via `InventoryBinding`), health/stamina `ProgressBar`s
bound to `coopa::stat::Resource`s, a pickup/kill `MessageLog`, and a backtick `Console` with
`give`/`take`/`damage`/`heal`/`stamina`/`kill`/`say`/`items`/`clearlog` commands. The model
objects (`Inventory`, `Hotbar`, `StatBlock`, the loaded `ItemDatabase`) are declared *before*
the `Scene` in `main()`, so they outlive it during teardown — see
`widgets/inventory_binding.h`'s ownership contract. Item definitions load from
`assets/items/items.yaml` via `coopa::item::ItemDatabaseLoader`.

### Running a demo

```bash
cbuild --vulkan   # or: cmake -B build && cmake --build build
./build/uicoopa_test_window
./build/uicoopa_settings_builder
./build/uicoopa_dialog_builder
./build/uicoopa_hud_builder
# or, via the local cplay/cbuild wrappers:
cplay
```
Press `ESC` to quit any of them — a screenshot of the final frame is always written to
`output/<name>.png` on exit (`SCREENSHOT_NAME=` overrides the file name).

Environment variables for scripted, headless-friendly runs (no human required at the mouse):

| Variable | Demo(s) | Effect |
|---|---|---|
| `MAX_FRAMES=<n>` | all | Exit automatically after `n` frames. |
| `ONESHOT=1` | all | Exit after the first frame. |
| `SCREENSHOT_NAME=<name>` | all | Screenshot file name (default: the demo's own name). |
| `THEME=light` | `settings_builder`, `dialog_builder` | Load `assets/themes/light.yaml` instead of `dark.yaml`. |
| `UI_AUDIO=0` | `settings_builder`, `dialog_builder` (audio builds) | Skip audio entirely — no device opened, no sounds loaded. |
| `SFX_DEVICE=null` | all (audio builds) | Exercise the audio path with miniaudio's silent null backend. |
| `OPEN_DIALOG=1` | `test_window` | Start with the modal dialog already visible. |
| `OPEN_DIALOG=<name>` | `settings_builder`, `dialog_builder` | Force-open a built dialog by node name (e.g. `ResetConfirm`, `AboutWindow`, `DiscardConfirm`) regardless of its `start()`-applied state. |
| `SELECT_TAB=<index>` | `settings_builder`, `dialog_builder` | Select a page of the settings/main tab bar before the render loop starts. |
| `FORCE_HOVER=1` | `test_window` | Emit the button's `on_hover_enter` signal once before the loop starts, so hover effects appear in a screenshot with no pointer anywhere near the button. |
| `OPEN_COMBO=<name>` | `settings_builder`, `test_window` | Force a named `ComboBox`'s popup open. |
| `SLIDER_MAX=<name>` | `settings_builder`, `test_window` | Snap a named `Slider` to its max value. |
| `SCROLL_Y=<f>` | `test_window` | Set a named `ScrollRect`'s `Content` to a given scroll offset. |
| `EDIT_FIELD=<name>` | `settings_builder` | Open keyboard editing on a named `TextField` (e.g. `player_name`) so a screenshot can capture its blinking caret. `SELECT_LEFT=<n>` additionally simulates `n` Shift+Left presses, for the selection-highlight look. |
| `HOVER_SLOT=<name>` | `settings_builder`, `test_window` | Synthesize a pointer-enter on a named `InventorySlot`, for its hover tooltip. |
| `ANIM_TIME=<seconds>` | `settings_builder` | Freeze the info banner's idle animation at an exact time before the loop starts, for a reproducible screenshot. |
| `CURSOR_POS=<x>,<y>` | `settings_builder` | Pin the (now OS-hidden, see `UITheme::cursor`) pointer to a fixed window-pixel position every frame, so a screenshot can show the themed cursor overlay reacting to a known hover target. |
| `SCENE=<name>` | `test_window` | Load `assets/scenes/<name>/scene.yaml` instead of `test_window` (e.g. `test_window_variant`). |
| `HUD_HP=<0..1>` / `HUD_STAMINA=<0..1>` | `hud_builder` | Pre-set health's/stamina's normalized value before the loop starts. |
| `HUD_HOTBAR=<0..8>` | `hud_builder` | Pre-select a hotbar slot. |
| `HUD_CONSOLE=1` | `hud_builder` | Open the console before the first frame. |
| `HUD_SCRIPT="give potion_health 5,kill Grunt,say hello"` | `hud_builder` | Comma-separated console commands replayed pre-loop — what makes a message-log/hotbar screenshot deterministic. |
| `HUD_LOG_HOLD=<seconds>` | `hud_builder` | Override the message log's `hold_seconds` so a scripted pickup line can't fade before a screenshot is taken. |

### Targets

`CMakeLists.txt` builds five executables from this repo:

| Target | Source | Purpose |
|---|---|---|
| `uicoopa` | `test.cpp` | Headless assertion-based test suite (`RUN_TEST`/`ASSERT_TRUE`). Registered with `ctest` via `add_test(NAME uicoopa_tests COMMAND uicoopa)` — no display required. |
| `uicoopa_test_window` | `test_window.cpp` | The YAML-declarative interactive demo. |
| `uicoopa_settings_builder` | `test_settings_builder.cpp` | The `UIBuilder` settings-panel demo. |
| `uicoopa_dialog_builder` | `test_dialog_builder.cpp` | The `dialog()`/`split_rows()`/`split_columns()`/`tab_view()` demo. |
| `uicoopa_hud_builder` | `test_hud_builder.cpp` | The gameplay HUD demo — hotbar/stat-bars/message-log/console bound to `coopa::item`/`coopa::stat` models. |

Run the test suite with `./build/uicoopa` (or `ctest --test-dir build`, which also runs the
Vulkan/GLFW leak gate below); run a demo with `./build/uicoopa_<name>` (or `cplay`, which keys
off this `CMakeLists.txt`'s `project()` name to pick a demo target).

### Vulkan/GLFW leak gate

`GFX_LEAK_CHECK` (ON by default) fails the build if any consumer source names a raw
`Vk*`/`VK_*`/`vk*`/`GLFW*`/`glfw*` symbol outside a line explicitly marked
`// gfx-allow-vulkan` — every `uicoopa/` header stays Vulkan-free by construction; the only
sanctioned exceptions are each demo's own `save_screenshot()` (gfxcoopa has no `Framebuffer`
wrapper, so an ad-hoc capture target needs one raw `vkCreateFramebuffer`/`vkDestroyFramebuffer`
pair).

## Building

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

Then `#include <uicoopa/...>` as needed — see `test_dialog_builder.cpp` for the shortest
full-application include list (layout, render, widgets, text, `builder/ui_builder.h`), or
`test_window.cpp` for the equivalent YAML-driven set.

`UICOOPA_WITH_AUDIO` (CMake option, ON by default) links `sfxcoopa` and defines
`UICOOPA_HAS_AUDIO` when the sibling `sfxcoopa` repo is present, degrading to a no-audio
build (rather than a configure failure) if it simply isn't checked out — see
`uicoopa/audio/ui_audio.h`'s file doc.

## Documentation

Doxygen-style comments throughout `uicoopa/` are formatted for `coopadocs`
(`.coopadocs` includes this repo's `uicoopa/` directory). Generate with:
```bash
coopadocs build
coopadocs show
```
