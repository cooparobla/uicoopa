/**
 * @file imm.h
 * @brief uicoopa's immediate-mode widget layer: tool UIs (the toyengine editor) drawn
 *        through the same DrawList, fonts and UiPass as every retained widget.
 *
 * The retained widgets (Button, Slider, TabView, ...) suit game UI: a fixed layout built
 * once and bound to a model. A tool UI is the opposite -- the hierarchy panel's rows, the
 * inspector's fields and every menu change whenever the document does. Rebuilding
 * SceneObject subtrees for that means destroying nodes mid-dispatch (a known
 * use-after-free shape in this library) and re-wiring signals on every edit. Here the UI
 * is instead re-declared every frame from the model:
 *
 * @code
 * ctx.begin_frame(draw_list, input, canvas_size);
 * if (ctx.begin_menubar({0, 0, w, 24})) {
 *     if (ctx.begin_menu("File")) {
 *         if (ctx.menu_item("Save", "Ctrl+S")) save();
 *         ctx.end_menu();
 *     }
 *     ctx.end_menubar();
 * }
 * ctx.begin_panel("inspector", rect);
 * ctx.drag_float("Roughness", &material.roughness, 0.01f, 0.0f, 1.0f);
 * ctx.end_panel();
 * ctx.end_frame();
 * @endcode
 *
 * Everything is in canvas pixels with a TOP-LEFT origin, +Y down (screen convention) --
 * converted to the canvas's +Y-up space only when geometry reaches the DrawList. Layers
 * (popups, menus, modals, tooltips) are DrawList z-orders, so a menu opened mid-panel still
 * draws over everything after DrawList::finalize_z_order(); hover and clicks resolve against
 * the previous frame's layer rects so a widget under an open popup never reacts.
 *
 * Text goes through TextRenderer, which wraps a uicoopa Font -- or, with no font (headless
 * logic tests), measures with a fixed advance and draws nothing.
 *
 * Host it with ImmediateCanvas (immediate/imm_canvas.h) under a screen-space
 * CanvasComponent, or drive a Context directly with any DrawList.
 */

#ifndef UICOOPA_IMMEDIATE_IMM_H
#define UICOOPA_IMMEDIATE_IMM_H

#include <uicoopa/render/draw_list.h>
#include <uicoopa/render/ui_vertex.h>
#include <uicoopa/text/font.h>
#include <uicoopa/input/ui_input.h>
#include <uicoopa/builder/ui_theme.h>

#include <coopa/input/keys.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace coopa {
namespace ui {
namespace imm {

using Id = uint64_t;

// =====================================================================================
// Small value types
// =====================================================================================

/** @brief An axis-aligned rect in imm space (top-left origin, +Y down). */
struct Box {
    float x = 0, y = 0, w = 0, h = 0;

    float right()  const { return x + w; }
    float bottom() const { return y + h; }
    glm::vec2 pos()  const { return {x, y}; }
    glm::vec2 size() const { return {w, h}; }
    glm::vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
    bool contains(glm::vec2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
    bool empty() const { return w <= 0 || h <= 0; }
    Box shrink(float d) const { return {x + d, y + d, std::max(0.0f, w - 2 * d), std::max(0.0f, h - 2 * d)}; }
    Box intersect(const Box& o) const {
        float x0 = std::max(x, o.x), y0 = std::max(y, o.y);
        float x1 = std::min(right(), o.right()), y1 = std::min(bottom(), o.bottom());
        return {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
    }
};

inline uint32_t pack(const glm::vec4& c) { return UiVertex::pack_color(c.r, c.g, c.b, c.a); }
inline glm::vec4 with_alpha(glm::vec4 c, float a) { c.a = a; return c; }
inline glm::vec4 mix(const glm::vec4& a, const glm::vec4& b, float t) { return a + (b - a) * t; }

/** @brief The display part of a label: everything before "##" (the rest only feeds the id). */
inline std::string_view label_text(std::string_view label) {
    const size_t p = label.find("##");
    return p == std::string_view::npos ? label : label.substr(0, p);
}

/** @brief 64-bit FNV-1a, seeded so nested ids differ from top-level ones. */
inline Id hash_str(std::string_view s, Id seed = 0xcbf29ce484222325ull) {
    Id h = seed;
    for (unsigned char c : s) { h ^= c; h *= 0x100000001b3ull; }
    return h ? h : 1;
}
inline Id hash_int(uint64_t v, Id seed) {
    Id h = seed;
    for (int i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 0xFF; h *= 0x100000001b3ull; }
    return h ? h : 1;
}

/** @brief HSV (all 0..1) to RGB. */
inline glm::vec3 hsv_to_rgb(float h, float s, float v) {
    h = h - std::floor(h);
    const float i = std::floor(h * 6.0f), f = h * 6.0f - i;
    const float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch (static_cast<int>(i) % 6) {
        case 0: return {v, t, p};
        case 1: return {q, v, p};
        case 2: return {p, v, t};
        case 3: return {p, q, v};
        case 4: return {t, p, v};
        default: return {v, p, q};
    }
}

/** @brief RGB (0..1) to HSV (0..1). */
inline glm::vec3 rgb_to_hsv(const glm::vec3& c) {
    const float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b});
    const float d = mx - mn;
    float h = 0.0f;
    if (d > 1e-6f) {
        if (mx == c.r)      h = std::fmod((c.g - c.b) / d, 6.0f);
        else if (mx == c.g) h = (c.b - c.r) / d + 2.0f;
        else                h = (c.r - c.g) / d + 4.0f;
        h /= 6.0f;
        if (h < 0) h += 1.0f;
    }
    return {h, mx > 1e-6f ? d / mx : 0.0f, mx};
}

// =====================================================================================
// Style
// =====================================================================================

/** @brief Sizes and colours for every imm widget. */
struct Style {
    float font_size   = 13.0f;
    float row_height  = 22.0f;
    float padding     = 6.0f;
    float spacing     = 4.0f;
    float indent      = 14.0f;
    float label_ratio = 0.40f;   ///< Property rows: fraction of the row width given to the label.
    float scrollbar   = 8.0f;

    glm::vec4 window_bg     {0.11f, 0.12f, 0.14f, 1.0f};
    glm::vec4 panel_bg      {0.15f, 0.16f, 0.19f, 1.0f};
    glm::vec4 panel_alt     {0.13f, 0.14f, 0.17f, 1.0f};
    glm::vec4 header        {0.19f, 0.21f, 0.26f, 1.0f};
    glm::vec4 header_hover  {0.24f, 0.27f, 0.33f, 1.0f};
    glm::vec4 border        {0.07f, 0.08f, 0.09f, 1.0f};
    glm::vec4 text          {0.90f, 0.91f, 0.93f, 1.0f};
    glm::vec4 text_dim      {0.58f, 0.61f, 0.67f, 1.0f};
    glm::vec4 text_disabled {0.40f, 0.42f, 0.46f, 1.0f};
    glm::vec4 accent        {0.96f, 0.56f, 0.20f, 1.0f};
    glm::vec4 button        {0.23f, 0.25f, 0.30f, 1.0f};
    glm::vec4 button_hover  {0.30f, 0.33f, 0.40f, 1.0f};
    glm::vec4 button_active {0.18f, 0.19f, 0.23f, 1.0f};
    glm::vec4 field         {0.09f, 0.10f, 0.12f, 1.0f};
    glm::vec4 field_hover   {0.12f, 0.13f, 0.16f, 1.0f};
    glm::vec4 selection     {0.85f, 0.45f, 0.12f, 0.55f};
    glm::vec4 row_hover     {1.0f, 1.0f, 1.0f, 0.05f};
    glm::vec4 popup_bg      {0.12f, 0.13f, 0.16f, 0.98f};
    glm::vec4 scroll_grab   {0.32f, 0.34f, 0.40f, 1.0f};
    glm::vec4 axis_x        {0.86f, 0.30f, 0.30f, 1.0f};
    glm::vec4 axis_y        {0.45f, 0.78f, 0.30f, 1.0f};
    glm::vec4 axis_z        {0.32f, 0.52f, 0.92f, 1.0f};
    glm::vec4 warning       {0.98f, 0.80f, 0.30f, 1.0f};
    glm::vec4 error         {0.95f, 0.38f, 0.35f, 1.0f};

    /** @brief The defaults, derived from uicoopa's own dark theme where the roles overlap. */
    static Style from_theme(const UITheme& t) {
        Style s;
        s.panel_bg   = glm::vec4(glm::vec3(t.panel.panel), 1.0f);
        s.panel_alt  = glm::vec4(glm::vec3(t.panel.panel_alt), 1.0f);
        s.header     = glm::vec4(glm::vec3(t.panel.header_bar), 1.0f);
        s.text       = t.text.primary;
        s.text_dim   = t.text.secondary;
        s.accent     = t.text.accent;
        s.selection  = with_alpha(t.text.selection, 0.55f);
        return s;
    }
};

// =====================================================================================
// Text
// =====================================================================================

/**
 * @brief Measures and draws text through a uicoopa Font, at 1:1 canvas pixels.
 *
 * Without a font (headless tests) every glyph is 0.55 em wide and nothing is drawn, so
 * layout logic stays testable with no GPU.
 */
class TextRenderer {
public:
    explicit TextRenderer(Font* font = nullptr) : font_(font) {}
    void set_font(Font* font) { font_ = font; }
    Font* font() const { return font_; }

    float width(std::string_view s, float size) const {
        if (!font_) return static_cast<float>(s.size()) * size * 0.55f;
        FontAtlas& atlas = font_->atlas_for_size(px_(size), 2);
        float w = 0.0f;
        for (unsigned char c : s) {
            if (const GlyphInfo* g = atlas.glyph(c)) w += g->advance;
        }
        return w;
    }

    float line_height(float size) const {
        if (!font_) return size * 1.25f;
        return font_->atlas_for_size(px_(size), 2).line_height();
    }

    /** @brief Index of the gap closest to x (0..s.size()), for caret placement. */
    size_t hit_index(std::string_view s, float size, float x) const {
        float pen = 0.0f;
        for (size_t i = 0; i < s.size(); ++i) {
            const float adv = width(s.substr(i, 1), size);
            if (x < pen + adv * 0.5f) return i;
            pen += adv;
        }
        return s.size();
    }

    /**
     * @brief Draws one line of text with its top-left at `pos` (imm space).
     * @param canvas_h The canvas height, to flip into DrawList space.
     */
    void draw(DrawList& dl, float canvas_h, glm::vec2 pos, std::string_view s, float size, uint32_t color) const {
        if (!font_ || s.empty()) return;
        // Bake at the size the glyphs are actually DRAWN at (canvas pixels times the canvas's
        // text scale -- 2 on a Retina display) and scale the metrics back down, the same
        // supersampling Text::emit() does, so tool UI text is as crisp as the framebuffer.
        const float scale = std::clamp(dl.text_scale(), 1.0f, 4.0f);
        const uint32_t px = px_(size * scale);
        const float inv = size / static_cast<float>(px);
        FontAtlas& atlas = font_->atlas_for_size(px, scale > 1.0f ? 1u : 2u);
        dl.set_texture(atlas.texture().view_typed());
        // Baseline in imm space (y down): top + ascent. Canvas y = canvas_h - imm y.
        const float snap = 1.0f / scale;
        const float baseline = std::round((pos.y + atlas.ascent() * inv) / snap) * snap;
        float pen = std::round(pos.x / snap) * snap;
        for (unsigned char c : s) {
            const GlyphInfo* g = atlas.glyph(c);
            if (!g) continue;
            Rect r;
            r.min = glm::vec2(pen + g->quad_min.x * inv, canvas_h - (baseline + g->quad_max.y * inv));
            r.max = glm::vec2(pen + g->quad_max.x * inv, canvas_h - (baseline + g->quad_min.y * inv));
            dl.add_quad(r, g->uv, color);
            pen += g->advance * inv;
        }
        dl.set_texture(dl.default_texture());
    }

private:
    static uint32_t px_(float size) { return static_cast<uint32_t>(std::max(1.0f, std::round(size))); }
    Font* font_ = nullptr;
};

// =====================================================================================
// Input
// =====================================================================================

/** @brief One frame of input, in imm space. Build with from_ui_input() or by hand (tests). */
struct FrameInput {
    glm::vec2 mouse{-1e6f};
    glm::vec2 mouse_delta{0.0f};
    bool down[3]     = {};
    bool pressed[3]  = {};
    bool released[3] = {};
    glm::vec2 scroll{0.0f};
    std::vector<uint32_t> chars;
    std::vector<coopa::input::KeyEvent> keys;
    coopa::input::Mods mods = coopa::input::Mods::None;
    float dt = 1.0f / 60.0f;
    std::function<bool(coopa::input::Key)> key_down;           ///< Optional level-triggered query.
    std::function<std::string()> get_clipboard;                ///< Optional.
    std::function<void(const std::string&)> set_clipboard;     ///< Optional.

    /** @brief Converts a canvas's UiInput (+Y up) into imm space. */
    static FrameInput from_ui_input(const UiInput& in, float canvas_h, float dt) {
        FrameInput f;
        f.mouse = glm::vec2(in.position().x, canvas_h - in.position().y);
        f.mouse_delta = glm::vec2(in.delta().x, -in.delta().y);
        for (int b = 0; b < 3; ++b) {
            f.down[b] = in.is_button_down(b);
            f.pressed[b] = in.is_button_pressed(b);
            f.released[b] = in.is_button_released(b);
        }
        f.scroll = in.scroll_delta();
        f.chars.assign(in.char_input().begin(), in.char_input().end());
        f.keys = in.key_events();
        f.mods = in.mods();
        f.dt = dt;
        const UiInput* src = &in;
        f.key_down = [src](coopa::input::Key k) { return src->is_key_down(k); };
        if (const coopa::input::Input* raw = in.source()) {
            auto* mutable_raw = const_cast<coopa::input::Input*>(raw);
            f.get_clipboard = [raw] { return raw->clipboard_text(); };
            f.set_clipboard = [mutable_raw](const std::string& s) { mutable_raw->set_clipboard_text(s); };
        }
        return f;
    }
};

enum class Mouse { Left = 0, Right = 1, Middle = 2 };

// =====================================================================================
// Context
// =====================================================================================

/** @brief Result of tree_node(). */
struct TreeNodeResult {
    bool open = false;            ///< Children should be emitted (then call tree_pop()).
    bool clicked = false;         ///< Row clicked (not the expander) -- select.
    bool double_clicked = false;
    bool right_clicked = false;
    Box  rect;
};

/** @brief Result of a splitter drag. */
struct DragState {
    bool active = false;
    bool changed = false;
};

/**
 * @class Context
 * @brief All immediate-mode state: ids, hover/active tracking, layout, popups, text editing.
 *
 * One Context per window. Call begin_frame()/end_frame() around each frame's declarations.
 */
class Context {
public:
    Style style;
    TextRenderer text;

    // -------------------------------------------------------------------------------
    // Frame
    // -------------------------------------------------------------------------------

    /**
     * @param dl          Destination; must already be begun (DrawList::begin()) with its
     *                    default texture set.
     * @param in          This frame's input (imm space).
     * @param canvas_size Canvas size in canvas pixels.
     */
    void begin_frame(DrawList& dl, const FrameInput& in, glm::vec2 canvas_size) {
        dl_ = &dl;
        in_ = in;
        size_ = canvas_size;
        time_ += in.dt;
        ++frame_;

        // Resolve which layer the mouse is over from LAST frame's layer rects: the highest z
        // containing the mouse, or the open modal (which owns all input while it is up).
        hovered_layer_ = 0;
        int best_z = -1;
        for (const auto& l : layers_prev_) {
            if (l.modal) { hovered_layer_ = l.id; best_z = 1 << 30; continue; }
            if (l.rect.contains(in_.mouse) && l.z > best_z) { best_z = l.z; hovered_layer_ = l.id; }
        }
        // The modal stays the hover layer even off its rect (so nothing below reacts); a
        // popup ABOVE the modal (e.g. a combo inside it) still wins when under the mouse.
        for (const auto& l : layers_prev_) {
            if (!l.modal && l.z > modal_z_prev_ && l.rect.contains(in_.mouse) && modal_z_prev_ >= 0) hovered_layer_ = l.id;
        }
        popup_hovered_ = hovered_layer_ != 0;

        // Click outside every open (non-modal) popup closes the whole popup stack.
        closed_by_click_.clear();
        if (any_pressed_() && !open_popups_.empty()) {
            bool inside = false;
            for (const auto& l : layers_prev_) if (l.rect.contains(in_.mouse)) inside = true;
            if (!inside) {
                while (!open_popups_.empty() && !open_popups_.back().modal) {
                    closed_by_click_.push_back(open_popups_.back().id);
                    open_popups_.pop_back();
                }
            }
        }

        layers_.clear();
        layer_stack_.clear();
        layer_stack_.push_back({0, 0});
        id_stack_.clear();
        id_stack_.push_back(0x9e3779b97f4a7c15ull);
        clip_stack_.clear();
        clip_stack_.push_back(Box{0, 0, size_.x, size_.y});
        layout_stack_.clear();
        layout_stack_.push_back(Layout{Box{0, 0, size_.x, size_.y}});
        layout_stack_.back().cursor = {0, 0};
        apply_clip_();
        dl_->set_z_order(0);
        hot_id_ = 0;
        last_item_ = {};
        keyboard_consumed_ = false;
        wheel_consumed_ = false;
        tooltip_.clear();
        dl_->set_texture(dl_->default_texture());

        // A text field that was not redeclared this frame loses focus without committing.
        if (text_edit_.active && text_edit_.seen_frame + 1 < frame_) text_edit_ = {};
    }

    void end_frame() {
        // Tooltip, top-most.
        if (!tooltip_.empty() && !drag_.active) {
            const float pad = style.padding;
            const float w = text.width(tooltip_, style.font_size) + pad * 2;
            const float h = text.line_height(style.font_size) + pad * 1.5f;
            Box b{in_.mouse.x + 14, in_.mouse.y + 18, w, h};
            if (b.right() > size_.x) b.x = size_.x - b.w - 2;
            if (b.bottom() > size_.y) b.y = in_.mouse.y - h - 4;
            dl_->set_z_order(kTooltipZ);
            push_clip_raw_(Box{0, 0, size_.x, size_.y});
            fill(b, style.popup_bg);
            outline(b, style.border);
            draw_text({b.x + pad, b.y + pad * 0.75f}, tooltip_, style.text);
            pop_clip_();
        }
        // Drag payload preview, top-most.
        if (drag_.active && !drag_.label.empty()) {
            dl_->set_z_order(kTooltipZ + 1);
            push_clip_raw_(Box{0, 0, size_.x, size_.y});
            const float w = text.width(drag_.label, style.font_size) + style.padding * 2;
            Box b{in_.mouse.x + 12, in_.mouse.y + 6, w, style.row_height};
            fill(b, with_alpha(style.header, 0.92f));
            draw_text({b.x + style.padding, b.y + (b.h - text.line_height(style.font_size)) * 0.5f}, drag_.label, style.text);
            pop_clip_();
        }
        // A widget that held the mouse but was not redeclared this frame (or never saw its
        // release) lets go once every button is up.
        if (active_id_ && !any_down_() && !text_edit_.active) active_id_ = 0;
        if (drag_.active && in_.released[0]) drag_ = {};
        if (in_.released[0]) drag_candidate_ = 0;

        layers_prev_ = layers_;
        modal_z_prev_ = -1;
        for (const auto& l : layers_) if (l.modal) modal_z_prev_ = std::max(modal_z_prev_, l.z);
        // Popups not re-declared this frame are closed (their owner stopped drawing them).
        open_popups_.erase(std::remove_if(open_popups_.begin(), open_popups_.end(),
            [this](const PopupState& p) { return p.seen_frame + 1 < frame_ && p.opened_frame + 1 < frame_; }),
            open_popups_.end());
        dl_->set_z_order(0);
        dl_->pop_clip();   // the clip begin_frame()/apply_clip_() left on the shared DrawList
    }

    // -------------------------------------------------------------------------------
    // Ids
    // -------------------------------------------------------------------------------

    Id get_id(std::string_view label) const { return hash_str(label, id_stack_.back()); }
    Id get_id(const void* ptr) const { return hash_int(reinterpret_cast<uintptr_t>(ptr), id_stack_.back()); }
    Id get_id(int64_t v) const { return hash_int(static_cast<uint64_t>(v) ^ 0x5bd1e995ull, id_stack_.back()); }
    void push_id(std::string_view s) { id_stack_.push_back(get_id(s)); }
    void push_id(int64_t v) { id_stack_.push_back(get_id(v)); }
    void push_id(const void* p) { id_stack_.push_back(get_id(p)); }
    void pop_id() { if (id_stack_.size() > 1) id_stack_.pop_back(); }

    // -------------------------------------------------------------------------------
    // Queries
    // -------------------------------------------------------------------------------

    const FrameInput& input() const { return in_; }
    glm::vec2 mouse() const { return in_.mouse; }
    glm::vec2 canvas_size() const { return size_; }
    double time() const { return time_; }
    DrawList& draw_list() { return *dl_; }

    /** @brief True while any widget holds the mouse (a drag, a held button) or a popup is under it. */
    bool wants_mouse() const { return active_id_ != 0 || popup_hovered_ || drag_.active; }
    /** @brief True while a text field has keyboard focus. */
    bool wants_keyboard() const { return text_edit_.active; }
    /** @brief True if the mouse is over a popup/menu/modal layer (last frame's rects). */
    bool popup_hovered() const { return popup_hovered_; }
    /** @brief True if any popup or modal is open. */
    bool any_popup_open() const { return !open_popups_.empty(); }

    bool mouse_pressed(Mouse b = Mouse::Left) const { return in_.pressed[static_cast<int>(b)]; }
    bool mouse_down(Mouse b = Mouse::Left) const { return in_.down[static_cast<int>(b)]; }
    bool mouse_released(Mouse b = Mouse::Left) const { return in_.released[static_cast<int>(b)]; }

    /** @brief True if `key` was pressed (or repeated, with `repeat`) this frame with exactly `mods` held. */
    bool key_pressed(coopa::input::Key key, coopa::input::Mods mods = coopa::input::Mods::None, bool repeat = false) const {
        using coopa::input::KeyAction;
        using coopa::input::Mods;
        const auto mask = [](Mods m) {
            return static_cast<uint8_t>(m) & static_cast<uint8_t>(Mods::Shift | Mods::Control | Mods::Alt | Mods::Super);
        };
        for (const auto& e : in_.keys) {
            if (e.key != key) continue;
            if (!(e.action == KeyAction::Press || (repeat && e.action == KeyAction::Repeat))) continue;
            if (mask(e.mods) == mask(mods)) return true;
        }
        return false;
    }

    /** @brief The platform's command modifier: Super on macOS, Control elsewhere. */
    static coopa::input::Mods command_mod() {
#ifdef __APPLE__
        return coopa::input::Mods::Super;
#else
        return coopa::input::Mods::Control;
#endif
    }

    /**
     * @brief A keyboard shortcut: key_pressed() unless a text field owns the keyboard. Also
     *        accepts Control in place of Super on macOS so "Ctrl+S" works on every keyboard.
     */
    bool shortcut(coopa::input::Key key, coopa::input::Mods mods = coopa::input::Mods::None) const {
        if (text_edit_.active || keyboard_consumed_) return false;
        if (key_pressed(key, mods)) return true;
        using coopa::input::Mods;
        if (has(mods, Mods::Super)) {
            const Mods alt = static_cast<Mods>((static_cast<uint8_t>(mods) & ~static_cast<uint8_t>(Mods::Super)) |
                                               static_cast<uint8_t>(Mods::Control));
            return key_pressed(key, alt);
        }
        return false;
    }

    /** @brief Gives the text field `id_str` (declared later this frame) keyboard focus with `value`. */
    void begin_text_edit(std::string_view id_str, const std::string& value) { begin_text_edit_(get_id(id_str), value, false); }

    /** @brief Marks the keyboard as used this frame, so later shortcut() calls return false. */
    void consume_keyboard() { keyboard_consumed_ = true; }

    // -------------------------------------------------------------------------------
    // Last-item queries (the widget most recently declared)
    // -------------------------------------------------------------------------------

    Box  last_rect() const { return last_item_.rect; }
    Id   last_id() const { return last_item_.id; }
    bool last_hovered() const { return last_item_.hovered; }
    bool last_active() const { return last_item_.id != 0 && last_item_.id == active_id_; }
    /** @brief The last item stopped being active this frame (end of a drag / edit). */
    bool last_deactivated() const { return last_item_.deactivated; }
    /** @brief The last item was clicked with `b` this frame. */
    bool last_clicked(Mouse b = Mouse::Left) const {
        return last_item_.hovered && in_.released[static_cast<int>(b)] && press_origin_hit_(b, last_item_.rect);
    }

    /** @brief Shows `text` as a tooltip if the last item has been hovered for a moment. */
    void tooltip(std::string_view tip) {
        if (!last_item_.hovered) return;
        if (tooltip_target_ != last_item_.id) { tooltip_target_ = last_item_.id; tooltip_start_ = time_; }
        if (time_ - tooltip_start_ > 0.45) tooltip_ = std::string(tip);
    }

    // -------------------------------------------------------------------------------
    // Drawing primitives (imm space)
    // -------------------------------------------------------------------------------

    void fill(const Box& b, const glm::vec4& c) {
        if (b.empty() || c.a <= 0.0f) return;
        dl_->set_texture(dl_->default_texture());
        dl_->add_quad(to_canvas_(b), Rect{{0.5f, 0.5f}, {0.5f, 0.5f}}, pack(c));
    }
    void outline(const Box& b, const glm::vec4& c, float t = 1.0f) {
        fill({b.x, b.y, b.w, t}, c);
        fill({b.x, b.bottom() - t, b.w, t}, c);
        fill({b.x, b.y + t, t, b.h - 2 * t}, c);
        fill({b.right() - t, b.y + t, t, b.h - 2 * t}, c);
    }
    void line(glm::vec2 a, glm::vec2 b, const glm::vec4& c, float t = 1.0f) {
        dl_->set_texture(dl_->default_texture());
        dl_->add_line(flip_(a), flip_(b), t, pack(c));
    }
    void triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4& col) {
        dl_->set_texture(dl_->default_texture());
        dl_->add_triangle(flip_(a), flip_(b), flip_(c), pack(col));
    }
    /** @brief Expander arrow centred in `b`: pointing right (closed) or down (open). */
    void arrow(const Box& b, bool open, const glm::vec4& c) {
        const glm::vec2 m = b.center();
        const float r = std::min(b.w, b.h) * 0.28f;
        if (open) triangle({m.x - r, m.y - r * 0.6f}, {m.x + r, m.y - r * 0.6f}, {m.x, m.y + r * 0.7f}, c);
        else      triangle({m.x - r * 0.6f, m.y - r}, {m.x - r * 0.6f, m.y + r}, {m.x + r * 0.7f, m.y}, c);
    }
    void gradient(const Box& b, const glm::vec4& tl, const glm::vec4& tr, const glm::vec4& br, const glm::vec4& bl) {
        dl_->set_texture(dl_->default_texture());
        dl_->add_quad_gradient(flip_(b.pos()), flip_({b.right(), b.y}), flip_({b.right(), b.bottom()}),
                               flip_({b.x, b.bottom()}), pack(tl), pack(tr), pack(br), pack(bl));
    }
    void draw_text(glm::vec2 pos, std::string_view s, const glm::vec4& c, float size = 0.0f) {
        text.draw(*dl_, size_.y, pos, s, size > 0 ? size : style.font_size, pack(c));
    }
    /** @brief Text vertically centred in `b`, left-aligned with `pad`, clipped to b. */
    void text_in(const Box& b, std::string_view s, const glm::vec4& c, float pad = -1.0f, bool center = false) {
        if (pad < 0) pad = style.padding;
        const float lh = text.line_height(style.font_size);
        float x = b.x + pad;
        if (center) x = b.x + (b.w - text.width(s, style.font_size)) * 0.5f;
        push_clip(b);
        draw_text({x, b.y + (b.h - lh) * 0.5f}, s, c);
        pop_clip();
    }
    float text_width(std::string_view s) const { return text.width(s, style.font_size); }

    void push_clip(const Box& b) { clip_stack_.push_back(clip_stack_.back().intersect(b)); apply_clip_(); }
    void pop_clip() { pop_clip_(); }
    Box  current_clip() const { return clip_stack_.back(); }

    // -------------------------------------------------------------------------------
    // Layout
    // -------------------------------------------------------------------------------

    /** @brief The current layout region's content box. */
    Box content_region() const { return layout_().region; }
    /** @brief Width left on the current row from the cursor to the region's right edge. */
    float available_width() const {
        const Layout& l = layout_();
        return std::max(0.0f, l.region.right() - cursor_x_());
    }
    glm::vec2 cursor() const { return {cursor_x_(), layout_().cursor.y}; }
    void set_cursor_y(float y) { layout_().cursor.y = y; layout_().same_line = false; }

    /**
     * @brief Reserves the next item's box: on the current row after same_line(), else at the
     *        start of a new row (after `indent`).
     * @param w Width; <= 0 means "the rest of the row" (minus -w).
     */
    Box next_box(float h, float w = 0.0f) {
        Layout& l = layout_();
        const float x = cursor_x_();
        if (w <= 0) w = std::max(1.0f, l.region.right() - x + w);
        Box b{x, l.cursor.y, w, h};
        l.row_h = l.same_line ? std::max(l.row_h, h) : h;
        l.last_x_end = b.right();
        l.line_start_y = l.cursor.y;
        l.same_line = false;
        l.cursor.y = l.line_start_y + l.row_h + style.spacing;
        l.content_bottom = std::max(l.content_bottom, b.bottom());
        return b;
    }
    /** @brief Places the next item to the right of the previous one, on the same row. */
    void same_line(float gap = -1.0f) {
        Layout& l = layout_();
        l.same_line = true;
        l.same_line_x = l.last_x_end + (gap < 0 ? style.spacing : gap);
        l.cursor.y = l.line_start_y;
    }
    void indent(float d = -1.0f) { layout_().indent += d < 0 ? style.indent : d; }
    void unindent(float d = -1.0f) { layout_().indent = std::max(0.0f, layout_().indent - (d < 0 ? style.indent : d)); }
    void spacing(float h = -1.0f) { layout_().cursor.y += h < 0 ? style.spacing * 2 : h; layout_().same_line = false; }
    void separator() {
        Box b = next_box(style.spacing * 2 + 1);
        fill({b.x, b.y + style.spacing, b.w, 1}, style.border);
    }

    /**
     * @brief A fixed-rect layout region, optionally scrollable and filled.
     * @param id     Scroll state key.
     * @param rect   The region on screen.
     * @param scroll Vertical scrolling + scrollbar when the content overflows.
     */
    void begin_region(std::string_view id, const Box& rect, bool scroll = true, const glm::vec4* bg = nullptr) {
        const Id rid = get_id(id);
        if (bg) fill(rect, *bg);
        push_clip(rect);
        Layout l{rect.shrink(style.padding)};
        l.id = rid;
        l.outer = rect;
        l.scroll = scroll;
        if (scroll) {
            l.region.w = std::max(0.0f, l.region.w - style.scrollbar);
            l.scroll_y = &state_(rid).f[0];
        }
        l.cursor = {l.region.x, l.region.y - (l.scroll_y ? *l.scroll_y : 0.0f)};
        l.content_top = l.cursor.y;
        l.content_bottom = l.cursor.y;
        layout_stack_.push_back(l);
        push_id(id);
    }

    void end_region() {
        pop_id();
        Layout l = layout_stack_.back();
        layout_stack_.pop_back();
        if (l.scroll && l.scroll_y) {
            const float content_h = (l.content_bottom - l.content_top) + style.padding;
            const float view_h = l.outer.h - style.padding;
            const float max_scroll = std::max(0.0f, content_h - view_h);
            if (!wheel_consumed_ && in_.scroll.y != 0.0f && l.outer.contains(in_.mouse) && layer_ok_() &&
                current_clip().contains(in_.mouse)) {
                *l.scroll_y -= in_.scroll.y * style.row_height * 2.0f;
                wheel_consumed_ = true;
            }
            // Scrollbar.
            if (max_scroll > 0.0f) {
                const Box track{l.outer.right() - style.scrollbar, l.outer.y, style.scrollbar, l.outer.h};
                const float grab_h = std::max(20.0f, track.h * view_h / content_h);
                const float t = *l.scroll_y / max_scroll;
                Box grab{track.x + 1, track.y + (track.h - grab_h) * t, track.w - 2, grab_h};
                const Id sid = hash_int(l.id, 77);
                bool hovered = hoverable_(track);
                if (hovered && in_.pressed[0]) { active_id_ = sid; scroll_grab_offset_ = in_.mouse.y - grab.y; }
                if (active_id_ == sid && in_.down[0] && track.h > grab_h) {
                    const float ny = std::clamp((in_.mouse.y - scroll_grab_offset_ - track.y) / (track.h - grab_h), 0.0f, 1.0f);
                    *l.scroll_y = ny * max_scroll;
                }
                fill(grab, active_id_ == sid || hovered ? style.header_hover : style.scroll_grab);
            }
            *l.scroll_y = std::clamp(*l.scroll_y, 0.0f, max_scroll);
        }
        pop_clip();
    }

    /** @brief A panel: a filled region (scrollable) with an optional title bar. */
    void begin_panel(std::string_view id, const Box& rect, std::string_view title = {}) {
        fill(rect, style.panel_bg);
        Box body = rect;
        if (!title.empty()) {
            Box bar{rect.x, rect.y, rect.w, style.row_height};
            fill(bar, style.header);
            text_in(bar, title, style.text_dim);
            body = {rect.x, rect.y + bar.h, rect.w, rect.h - bar.h};
        }
        begin_region(id, body, true);
    }
    void end_panel() { end_region(); }

    // -------------------------------------------------------------------------------
    // Basic widgets
    // -------------------------------------------------------------------------------

    void label(std::string_view s, const glm::vec4* color = nullptr) {
        Box b = next_box(style.row_height, text_width(label_text(s)) + style.padding);
        text_in(b, label_text(s), color ? *color : style.text, 0.0f);
        set_last_(0, b, false);
    }
    void label_dim(std::string_view s) { label(s, &style.text_dim); }
    /** @brief A full-row heading in the accent colour. */
    void heading(std::string_view s) {
        Box b = next_box(style.row_height);
        text_in(b, label_text(s), style.accent, 0.0f);
        set_last_(0, b, false);
    }
    /** @brief Wrapped paragraph text (word wrap at the region width). */
    void paragraph(std::string_view s, const glm::vec4* color = nullptr) {
        const float w = available_width();
        std::string line, word;
        std::vector<std::string> lines;
        auto flush_word = [&] {
            if (word.empty()) return;
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && text_width(candidate) > w) { lines.push_back(line); line = word; }
            else line = candidate;
            word.clear();
        };
        for (char c : s) {
            if (c == ' ') flush_word();
            else if (c == '\n') { flush_word(); lines.push_back(line); line.clear(); }
            else word += c;
        }
        flush_word();
        if (!line.empty()) lines.push_back(line);
        const float lh = text.line_height(style.font_size);
        for (const auto& l : lines) {
            Box b = next_box(lh);
            draw_text(b.pos(), l, color ? *color : style.text);
        }
    }

    /** @brief A push button. @return True when clicked. */
    bool button(std::string_view lbl, float w = -1.0f, bool enabled = true) {
        const Id id = get_id(lbl);
        const std::string_view shown = label_text(lbl);
        if (w < 0) w = text_width(shown) + style.padding * 3;
        Box b = next_box(style.row_height, w);
        bool hovered = false, held = false;
        const bool clicked = enabled && behavior_(id, b, hovered, held);
        fill(b, !enabled ? style.button_active : held ? style.button_active : hovered ? style.button_hover : style.button);
        text_in(b, shown, enabled ? style.text : style.text_disabled, 0.0f, true);
        set_last_(id, b, hovered);
        return clicked;
    }

    /** @brief A toggle-able button (toolbar). @return True when clicked. */
    bool toggle_button(std::string_view lbl, bool on, float w = -1.0f) {
        const Id id = get_id(lbl);
        const std::string_view shown = label_text(lbl);
        if (w < 0) w = text_width(shown) + style.padding * 3;
        Box b = next_box(style.row_height, w);
        bool hovered = false, held = false;
        const bool clicked = behavior_(id, b, hovered, held);
        fill(b, on ? with_alpha(style.accent, held ? 0.6f : 0.8f) : held ? style.button_active : hovered ? style.button_hover : style.button);
        text_in(b, shown, on ? glm::vec4(0.08f, 0.08f, 0.09f, 1.0f) : style.text, 0.0f, true);
        set_last_(id, b, hovered);
        return clicked;
    }

    /** @brief A checkbox with a label to its right. @return True when toggled. */
    bool checkbox(std::string_view lbl, bool* value) {
        const Id id = get_id(lbl);
        const std::string_view shown = label_text(lbl);
        const float box = style.row_height - 8;
        Box b = next_box(style.row_height, box + (shown.empty() ? 0 : style.spacing * 2 + text_width(shown)));
        bool hovered = false, held = false;
        const bool clicked = behavior_(id, b, hovered, held);
        if (clicked) *value = !*value;
        Box cb{b.x, b.y + 4, box, box};
        fill(cb, hovered ? style.field_hover : style.field);
        outline(cb, style.border);
        if (*value) fill(cb.shrink(3), style.accent);
        if (!shown.empty()) text_in({cb.right() + style.spacing * 2, b.y, b.w, b.h}, shown, style.text, 0.0f);
        set_last_(id, b, hovered);
        return clicked;
    }

    /** @brief A full-width row that can be selected. @return True when clicked. */
    bool selectable(std::string_view lbl, bool selected, float w = 0.0f) {
        const Id id = get_id(lbl);
        Box b = next_box(style.row_height, w);
        bool hovered = false, held = false;
        const bool clicked = behavior_(id, b, hovered, held);
        if (selected) fill(b, style.selection);
        else if (hovered) fill(b, style.row_hover);
        text_in(b, label_text(lbl), style.text);
        set_last_(id, b, hovered);
        return clicked;
    }

    /** @brief A horizontal progress/value bar. */
    void progress(float t, std::string_view overlay = {}) {
        Box b = next_box(style.row_height - 4);
        fill(b, style.field);
        fill({b.x, b.y, b.w * std::clamp(t, 0.0f, 1.0f), b.h}, with_alpha(style.accent, 0.8f));
        if (!overlay.empty()) text_in(b, overlay, style.text, 0.0f, true);
    }

    // -------------------------------------------------------------------------------
    // Property rows: "Label | widget" with a shared label column
    // -------------------------------------------------------------------------------

    /**
     * @brief Draws a property label in the left column and returns the widget box on the
     *        right. Everything in the inspector goes through this so columns line up.
     */
    Box property_row(std::string_view lbl, float h = 0.0f) {
        if (h <= 0) h = style.row_height;
        Box row = next_box(h);
        const float lw = std::floor(row.w * style.label_ratio);
        const std::string_view shown = label_text(lbl);
        if (!shown.empty()) text_in({row.x, row.y, lw - 4, style.row_height}, shown, style.text_dim, 0.0f);
        last_label_box_ = {row.x, row.y, lw, h};
        return shown.empty() && lbl.find("##") == 0 ? row : Box{row.x + lw, row.y, row.w - lw, h};
    }

    /**
     * @brief A draggable number: drag horizontally to scrub, click (no drag) to type.
     * @return True on any change (every drag step, and a committed typed value).
     */
    bool drag_float(std::string_view lbl, float* v, float speed = 0.01f, float lo = -1e30f, float hi = 1e30f,
                    const char* fmt = "%.3f") {
        Box b = property_row(lbl);
        return drag_float_in_(get_id(lbl), b, v, speed, lo, hi, fmt, nullptr);
    }
    bool drag_int(std::string_view lbl, int* v, float speed = 0.2f, int lo = -1000000000, int hi = 1000000000) {
        float f = static_cast<float>(*v);
        Box b = property_row(lbl);
        const bool changed = drag_float_in_(get_id(lbl), b, &f, speed, static_cast<float>(lo), static_cast<float>(hi), "%.0f", nullptr);
        if (changed) *v = static_cast<int>(std::lround(f));
        return changed;
    }
    /** @brief N floats in one row (vectors), each with an axis-coloured tick. */
    bool drag_floatn(std::string_view lbl, float* v, int n, float speed = 0.01f, const char* fmt = "%.3f",
                     float lo = -1e30f, float hi = 1e30f) {
        Box b = property_row(lbl);
        push_id(lbl);
        bool changed = false;
        const float gap = 3.0f;
        const float w = (b.w - gap * (n - 1)) / n;
        const glm::vec4* axis[4] = {&style.axis_x, &style.axis_y, &style.axis_z, &style.text_dim};
        bool any_active = false, any_deact = false;
        for (int i = 0; i < n; ++i) {
            Box fb{b.x + i * (w + gap), b.y, w, b.h};
            changed |= drag_float_in_(get_id(static_cast<int64_t>(i)), fb, &v[i], speed, lo, hi, fmt, axis[i]);
            any_active |= last_active();
            any_deact |= last_item_.deactivated;
        }
        pop_id();
        set_last_(get_id(lbl), b, b.contains(in_.mouse));
        last_item_.deactivated = any_deact;
        group_active_ = any_active;
        return changed;
    }
    /** @brief True while any sub-field of the last drag_floatn() is being dragged/edited. */
    bool last_group_active() const { return group_active_; }

    bool slider_float(std::string_view lbl, float* v, float lo, float hi, const char* fmt = "%.3f") {
        Box b = property_row(lbl);
        const Id id = get_id(lbl);
        bool hovered = false, held = false;
        behavior_(id, b, hovered, held);
        bool changed = false;
        if (held && b.w > 0) {
            const float t = std::clamp((in_.mouse.x - b.x) / b.w, 0.0f, 1.0f);
            const float nv = lo + (hi - lo) * t;
            if (nv != *v) { *v = nv; changed = true; }
        }
        fill(b, hovered || held ? style.field_hover : style.field);
        const float t = hi > lo ? std::clamp((*v - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
        fill({b.x, b.y, b.w * t, b.h}, with_alpha(style.accent, held ? 0.75f : 0.55f));
        char buf[64];
        std::snprintf(buf, sizeof(buf), fmt, *v);
        text_in(b, buf, style.text, 0.0f, true);
        set_last_(id, b, hovered);
        return changed;
    }

    /** @brief A checkbox in the property column. */
    bool property_bool(std::string_view lbl, bool* v) {
        Box b = property_row(lbl);
        const Id id = get_id(lbl);
        bool hovered = false, held = false;
        const bool clicked = behavior_(id, {b.x, b.y, b.h, b.h}, hovered, held);
        if (clicked) *v = !*v;
        Box cb{b.x, b.y + 4, b.h - 8, b.h - 8};
        fill(cb, hovered ? style.field_hover : style.field);
        outline(cb, style.border);
        if (*v) fill(cb.shrink(3), style.accent);
        set_last_(id, b, hovered);
        return clicked;
    }

    /**
     * @brief A single-line text field. Edits a private buffer while focused; the value only
     *        changes on commit (Enter, Tab, or clicking away). Escape cancels.
     * @return True when a changed value was committed.
     */
    bool input_text(std::string_view lbl, std::string* value, bool property = true) {
        Box b = property ? property_row(lbl) : next_box(style.row_height);
        return input_text_in_(get_id(lbl), b, value, false);
    }

    /** @brief A text field in an explicit box (toolbars, search fields). */
    bool input_text_box(std::string_view id_str, const Box& b, std::string* value, std::string_view placeholder = {}) {
        const bool r = input_text_in_(get_id(id_str), b, value, true);
        if (value->empty() && !(text_edit_.active && text_edit_.id == get_id(id_str)) && !placeholder.empty()) {
            text_in(b, placeholder, style.text_disabled);
        }
        return r;
    }

    /**
     * @brief A dropdown. @return True when the selection changed.
     */
    bool combo(std::string_view lbl, int* index, const std::vector<std::string>& items) {
        Box b = property_row(lbl);
        return combo_in_(get_id(lbl), b, index, items);
    }
    /** @brief combo() in an explicit box. */
    bool combo_box(std::string_view id_str, const Box& b, int* index, const std::vector<std::string>& items) {
        return combo_in_(get_id(id_str), b, index, items);
    }

    /**
     * @brief An RGB(A) colour: a swatch (click for an HSV picker popup) plus numeric fields.
     * @return True on any change.
     */
    bool color_edit(std::string_view lbl, float* rgba, bool alpha = false) {
        Box b = property_row(lbl);
        const Id id = get_id(lbl);
        push_id(lbl);
        bool changed = false;
        const float sw = std::min(44.0f, b.w * 0.3f);
        Box swatch{b.x, b.y + 2, sw, b.h - 4};
        bool hovered = false, held = false;
        const Id sid = get_id("swatch");
        const bool clicked = behavior_(sid, swatch, hovered, held);
        fill(swatch, glm::vec4(glm::clamp(glm::vec3(rgba[0], rgba[1], rgba[2]), 0.0f, 1.0f), 1.0f));
        outline(swatch, hovered ? style.text_dim : style.border);
        if (clicked) open_popup("picker", glm::vec2(swatch.x, swatch.bottom() + 2));
        const int n = alpha ? 4 : 3;
        const float gap = 3.0f;
        const float fx = swatch.right() + gap;
        const float w = (b.right() - fx - gap * (n - 1)) / n;
        const glm::vec4* axis[4] = {&style.axis_x, &style.axis_y, &style.axis_z, &style.text_dim};
        bool any_active = false, any_deact = false;
        for (int i = 0; i < n; ++i) {
            Box fb{fx + i * (w + gap), b.y, w, b.h};
            changed |= drag_float_in_(get_id(static_cast<int64_t>(i)), fb, &rgba[i], 0.005f, 0.0f, 64.0f, "%.3f", axis[i]);
            any_active |= last_active();
            any_deact |= last_item_.deactivated;
        }
        if (begin_popup("picker")) {
            changed |= color_picker_(rgba, any_active, any_deact);
            end_popup();
        }
        pop_id();
        set_last_(id, b, b.contains(in_.mouse));
        last_item_.deactivated = any_deact;
        group_active_ = any_active;
        return changed;
    }

    /**
     * @brief A full-width foldout header.
     * @return True while open (emit the section's contents).
     */
    bool collapsing_header(std::string_view lbl, bool default_open = true, bool* remove_clicked = nullptr) {
        const Id id = get_id(lbl);
        State& st = state_(id);
        if (!st.init) { st.init = true; st.b[0] = default_open; }
        Box b = next_box(style.row_height);
        bool hovered = false, held = false;
        Box toggle = b;
        if (remove_clicked) toggle.w -= style.row_height;
        if (behavior_(id, toggle, hovered, held)) st.b[0] = !st.b[0];
        fill(b, hovered ? style.header_hover : style.header);
        arrow({b.x + 2, b.y, style.row_height - 4, b.h}, st.b[0], style.text_dim);
        text_in({b.x + style.row_height, b.y, b.w - style.row_height, b.h}, label_text(lbl), style.text, 0.0f);
        set_last_(id, b, hovered);
        if (remove_clicked) {
            Box x{b.right() - style.row_height, b.y, style.row_height, b.h};
            bool xh = false, xheld = false;
            *remove_clicked = behavior_(hash_int(id, 3), x, xh, xheld);
            text_in(x, "x", xh ? style.error : style.text_dim, 0.0f, true);
        }
        return st.b[0];
    }

    /**
     * @brief A tree row: expander arrow (unless `leaf`), label, selection highlight.
     *        When the result is open, emit children then call tree_pop().
     */
    TreeNodeResult tree_node(Id id, std::string_view lbl, bool leaf, bool selected, bool default_open = false,
                             const glm::vec4* color = nullptr) {
        State& st = state_(id);
        if (!st.init) { st.init = true; st.b[0] = default_open; }
        Box b = next_box(style.row_height);
        const Layout& l = layout_();
        Box full{l.region.x, b.y, l.region.w, b.h};   // highlight spans the whole row
        const float ax = b.x;
        Box arrow_box{ax, b.y, style.row_height - 6, b.h};
        TreeNodeResult r;
        r.rect = full;
        const bool hovered = hoverable_(full, id);
        if (hovered) hot_id_ = id;
        if (selected) fill(full, style.selection);
        else if (hovered) fill(full, style.row_hover);
        if (!leaf) {
            arrow(arrow_box, st.b[0], style.text_dim);
            if (hovered && in_.pressed[0] && arrow_box.contains(in_.mouse)) st.b[0] = !st.b[0];
        }
        const bool on_arrow = !leaf && arrow_box.contains(in_.mouse);
        if (hovered && in_.pressed[0] && !on_arrow) {
            active_id_ = id;
            press_pos_[0] = in_.mouse;
        }
        if (hovered && in_.released[0] && !on_arrow && press_origin_hit_(Mouse::Left, full) && !drag_.active) {
            r.clicked = true;
            if (last_click_id_ == id && time_ - last_click_time_ < 0.35) { r.double_clicked = true; last_click_id_ = 0; }
            else { last_click_id_ = id; last_click_time_ = time_; }
        }
        if (hovered && in_.released[1]) r.right_clicked = true;
        text_in({arrow_box.right(), b.y, full.right() - arrow_box.right(), b.h}, lbl, color ? *color : style.text, 2.0f);
        set_last_(id, full, hovered);
        r.open = !leaf && st.b[0];
        if (r.open) indent();
        return r;
    }
    void tree_pop() { unindent(); }
    /** @brief Forces a tree node's (or collapsing header's) open state. */
    void set_open(Id id, bool open) { State& st = state_(id); st.init = true; st.b[0] = open; }

    /**
     * @brief A row of tabs across `b`. @return True when the active tab changed.
     */
    bool tab_bar(std::string_view id_str, const Box& b, const std::vector<std::string>& tabs, int* active) {
        push_id(id_str);
        fill(b, style.panel_alt);
        float x = b.x + 4;
        bool changed = false;
        for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
            const float w = text_width(tabs[i]) + style.padding * 4;
            Box t{x, b.y + 3, w, b.h - 3};
            const Id id = get_id(static_cast<int64_t>(i));
            bool hovered = false, held = false;
            if (behavior_(id, t, hovered, held) && *active != i) { *active = i; changed = true; }
            const bool on = *active == i;
            fill(t, on ? style.panel_bg : hovered ? style.header_hover : style.header);
            if (on) fill({t.x, t.y, t.w, 2}, style.accent);
            text_in(t, tabs[i], on ? style.text : style.text_dim, 0.0f, true);
            x += w + 2;
        }
        pop_id();
        return changed;
    }

    /**
     * @brief A draggable divider. Drag changes `*value` by the mouse delta (pixels along the
     *        divider's normal), clamped to [lo, hi].
     * @param vertical True for a vertical bar (drag horizontally).
     */
    DragState splitter(std::string_view id_str, const Box& bar, bool vertical, float* value, float lo, float hi) {
        const Id id = get_id(id_str);
        bool hovered = false, held = false;
        behavior_(id, bar, hovered, held);
        DragState r;
        r.active = held;
        if (held) {
            const float d = vertical ? in_.mouse_delta.x : in_.mouse_delta.y;
            const float nv = std::clamp(*value + d, lo, hi);
            if (nv != *value) { *value = nv; r.changed = true; }
        }
        fill(bar, held ? style.accent : hovered ? style.header_hover : style.border);
        return r;
    }

    // -------------------------------------------------------------------------------
    // Popups, menus, modals
    // -------------------------------------------------------------------------------

    /** @brief Opens the popup `name` (scoped to the current id stack) at `pos` (default: mouse). */
    void open_popup(std::string_view name, std::optional<glm::vec2> pos = std::nullopt) {
        const Id id = get_id(name);
        open_popup_id_(id, pos.value_or(in_.mouse), false, false);
    }
    /** @brief Opens a popup when the last item is right-clicked. @return True if it opened. */
    bool open_context_popup_on_last(std::string_view name) {
        if (last_item_.hovered && in_.released[1]) { open_popup(name); return true; }
        return false;
    }
    /** @brief Opens a popup when `area` is right-clicked over nothing else. */
    bool open_context_popup_in(std::string_view name, const Box& area) {
        if (hoverable_(area) && in_.released[1] && hot_id_ == 0) { open_popup(name); return true; }
        return false;
    }
    bool is_popup_open(std::string_view name) const { return find_popup_(get_id(name)) >= 0; }

    /** @brief Begins drawing popup `name` if open. Pair with end_popup() only when it returns true. */
    bool begin_popup(std::string_view name, float min_w = 160.0f) {
        return begin_popup_id_(get_id(name), min_w);
    }
    void end_popup() { end_popup_(); }
    /** @brief Closes the innermost popup currently being drawn (and its children). */
    void close_current_popup() {
        if (popup_draw_stack_.empty()) return;
        const int idx = find_popup_(popup_draw_stack_.back());
        if (idx >= 0) open_popups_.resize(static_cast<size_t>(idx));
    }
    void close_all_popups() {
        while (!open_popups_.empty() && !open_popups_.back().modal) open_popups_.pop_back();
    }

    /** @brief Opens a modal dialog (centred, blocks everything else) by name. */
    void open_modal(std::string_view name) {
        open_popup_id_(get_id(name), size_ * 0.5f, true, false);
    }
    /**
     * @brief Draws modal `name` if open: dims the screen, centres a titled box of `size`.
     * @return True while open; call end_modal() then.
     */
    bool begin_modal(std::string_view name, glm::vec2 box_size) {
        const Id id = get_id(name);
        const int idx = find_popup_(id);
        if (idx < 0) return false;
        PopupState& p = open_popups_[static_cast<size_t>(idx)];
        p.seen_frame = frame_;
        const int z = kPopupZ + idx * 10;
        dl_->set_z_order(z);
        push_clip_raw_(Box{0, 0, size_.x, size_.y});
        fill(Box{0, 0, size_.x, size_.y}, glm::vec4(0, 0, 0, 0.45f));
        Box b{std::floor((size_.x - box_size.x) * 0.5f), std::floor((size_.y - box_size.y) * 0.5f), box_size.x, box_size.y};
        fill(b, style.panel_bg);
        outline(b, style.border);
        Box bar{b.x, b.y, b.w, style.row_height};
        fill(bar, style.header);
        text_in(bar, label_text(name), style.text);
        layer_stack_.push_back({id, z});
        layers_.push_back({id, b, z, true});
        popup_draw_stack_.push_back(id);
        Layout l{Box{b.x, bar.bottom(), b.w, b.h - bar.h}.shrink(style.padding)};
        l.cursor = l.region.pos();
        l.content_top = l.cursor.y;
        layout_stack_.push_back(l);
        clip_stack_.push_back(b);
        apply_clip_();
        push_id(name);
        return true;
    }
    void end_modal() {
        pop_id();
        layout_stack_.pop_back();
        pop_clip_();
        pop_clip_();
        popup_draw_stack_.pop_back();
        layer_stack_.pop_back();
        dl_->set_z_order(layer_stack_.back().z);
    }
    /** @brief Closes the modal currently being drawn. */
    void close_modal() { close_current_popup(); }

    /** @brief A menu bar across `b`. Always returns true; pair with end_menubar(). */
    bool begin_menubar(const Box& b) {
        fill(b, style.panel_alt);
        fill({b.x, b.bottom() - 1, b.w, 1}, style.border);
        menubar_ = MenuBarState{};
        menubar_.active = true;
        menubar_.rect = b;
        menubar_.x = b.x + 4;
        return true;
    }
    void end_menubar() { menubar_.active = false; }

    /**
     * @brief A menu: a header in a menu bar, or a submenu row inside another menu.
     * @return True while its popup is open; pair with end_menu() then.
     */
    bool begin_menu(std::string_view lbl, bool enabled = true) {
        const Id id = get_id(lbl);
        const std::string_view shown = label_text(lbl);
        if (menubar_.active && popup_draw_stack_.empty()) {
            const float w = text_width(shown) + style.padding * 3;
            Box h{menubar_.x, menubar_.rect.y, w, menubar_.rect.h - 1};
            menubar_.x += w;
            const bool hovered = enabled && hoverable_(h);
            const bool open = find_popup_(id) == 0;
            const bool any_menubar_open = !open_popups_.empty() && open_popups_.front().from_menubar;
            if (hovered && in_.pressed[0]) {
                const bool just_closed = std::find(closed_by_click_.begin(), closed_by_click_.end(), id) != closed_by_click_.end();
                if (open) open_popups_.clear();
                else if (!just_closed) { open_popups_.clear(); open_popup_id_(id, {h.x, h.bottom()}, false, true); }
            } else if (hovered && any_menubar_open && !open) {
                open_popups_.clear();
                open_popup_id_(id, {h.x, h.bottom()}, false, true);
            }
            const bool now_open = find_popup_(id) == 0;
            fill(h, now_open ? style.header_hover : hovered ? style.header : glm::vec4(0));
            text_in(h, shown, enabled ? style.text : style.text_disabled, 0.0f, true);
            set_last_(id, h, hovered);
            if (!now_open) return false;
            return begin_popup_id_(id, 200.0f);
        }
        // Submenu row inside a menu.
        Box row = next_box(style.row_height);
        const bool hovered = enabled && hoverable_(row);
        const int depth = static_cast<int>(popup_draw_stack_.size());
        if (hovered) {
            const int idx = find_popup_(id);
            if (idx < 0) {
                open_popups_.resize(static_cast<size_t>(std::min<int>(depth, static_cast<int>(open_popups_.size()))));
                open_popup_id_(id, {row.right() + 2, row.y - style.padding}, false, open_popups_.front().from_menubar);
            }
        }
        const bool open = find_popup_(id) >= 0;
        if (open || hovered) fill(row, style.header_hover);
        text_in(row, shown, enabled ? style.text : style.text_disabled);
        arrow({row.right() - row.h, row.y, row.h, row.h}, false, style.text_dim);
        set_last_(id, row, hovered);
        if (!open) return false;
        return begin_popup_id_(id, 180.0f);
    }
    void end_menu() { end_popup_(); }

    /**
     * @brief A menu entry. @return True when chosen (the menus then close).
     * @param shortcut Right-aligned hint text, e.g. "Ctrl+S".
     * @param checked  Draws a check mark when non-null and true.
     */
    bool menu_item(std::string_view lbl, std::string_view shortcut_text = {}, const bool* checked = nullptr,
                   bool enabled = true) {
        const Id id = get_id(lbl);
        Box row = next_box(style.row_height);
        const bool hovered = enabled && hoverable_(row);
        if (hovered) {
            // Hovering a plain item closes any sibling submenu.
            const int depth = static_cast<int>(popup_draw_stack_.size());
            if (static_cast<int>(open_popups_.size()) > depth) open_popups_.resize(static_cast<size_t>(depth));
            fill(row, style.header_hover);
        }
        const float check_w = style.row_height;
        if (checked && *checked) fill(Box{row.x + 7, row.y + 7, row.h - 14, row.h - 14}, style.accent);
        text_in({row.x + check_w, row.y, row.w - check_w, row.h}, label_text(lbl), enabled ? style.text : style.text_disabled, 0.0f);
        if (!shortcut_text.empty()) {
            const float sw = text_width(shortcut_text);
            text_in({row.right() - sw - style.padding * 2, row.y, sw + style.padding, row.h}, shortcut_text, style.text_dim, 0.0f);
        }
        set_last_(id, row, hovered);
        const bool chosen = hovered && in_.released[0];
        if (chosen) close_all_popups();
        return chosen;
    }
    void menu_separator() {
        Box b = next_box(5);
        fill({b.x, b.y + 2, b.w, 1}, style.border);
    }

    // -------------------------------------------------------------------------------
    // Drag and drop
    // -------------------------------------------------------------------------------

    /**
     * @brief Makes the last item a drag source: once it is pressed and the mouse moves a few
     *        pixels, `payload` (of `type`) follows the cursor labelled `label`.
     * @return True while this item's payload is being dragged.
     */
    bool drag_source(std::string_view type, const std::string& payload, std::string_view label = {}) {
        const Id id = last_item_.id;
        if (!id) return false;
        if (drag_.active) return drag_.source == id;
        if (last_item_.hovered && in_.pressed[0]) { drag_candidate_ = id; press_pos_[0] = in_.mouse; }
        if (drag_candidate_ == id && in_.down[0] && glm::distance(in_.mouse, press_pos_[0]) > 5.0f) {
            drag_ = DragPayload{true, id, std::string(type), payload, std::string(label.empty() ? payload : label)};
            drag_candidate_ = 0;
            return true;
        }
        return false;
    }
    /** @brief The payload type being dragged, or empty. */
    std::string_view dragging_type() const { return drag_.active ? std::string_view(drag_.type) : std::string_view(); }

    /**
     * @brief Makes `area` (default: the last item) a drop target for payloads of `type`.
     * @return The payload when dropped here this frame.
     */
    std::optional<std::string> drop_target(std::string_view type, std::optional<Box> area = std::nullopt) {
        if (!drag_.active || drag_.type != type) return std::nullopt;
        const Box b = area.value_or(last_item_.rect);
        if (!b.contains(in_.mouse) || !current_clip().contains(in_.mouse) || !layer_ok_()) return std::nullopt;
        outline(b, style.accent, 2.0f);
        if (in_.released[0]) {
            std::string payload = drag_.payload;
            drag_ = {};
            return payload;
        }
        return std::nullopt;
    }

    // -------------------------------------------------------------------------------
    // Free-form interaction (custom widgets, the editor viewport)
    // -------------------------------------------------------------------------------

    /**
     * @brief Registers `b` as an interactive area with button behaviour.
     * @return True when clicked (pressed and released inside with the left button).
     */
    bool invisible_button(std::string_view id_str, const Box& b, bool* hovered_out = nullptr, bool* held_out = nullptr) {
        const Id id = get_id(id_str);
        bool hovered = false, held = false;
        const bool clicked = behavior_(id, b, hovered, held);
        if (hovered_out) *hovered_out = hovered;
        if (held_out) *held_out = held;
        set_last_(id, b, hovered);
        return clicked;
    }
    /** @brief True if `b` is under the mouse and nothing (popup, active widget) blocks it. */
    bool is_hovered(const Box& b) const { return hoverable_(b); }

private:
    // --- constants ---
    static constexpr int kPopupZ = 1000;
    static constexpr int kTooltipZ = 100000;

    // --- per-id persistent state ---
    struct State {
        bool  init = false;
        bool  b[4] = {};
        float f[4] = {};
    };
    State& state_(Id id) { return states_[id]; }

    struct Layout {
        Box   region;
        Box   outer;
        glm::vec2 cursor{0.0f};
        float indent = 0.0f;
        float row_h = 0.0f;
        float line_start_y = 0.0f;
        float last_x_end = 0.0f;
        float same_line_x = 0.0f;
        bool  same_line = false;
        bool  scroll = false;
        float* scroll_y = nullptr;
        float content_top = 0.0f;
        float content_bottom = 0.0f;
        float popup_max_w = 0.0f;
        Id    id = 0;

        Layout() = default;
        explicit Layout(const Box& r) : region(r), outer(r), cursor(r.pos()), line_start_y(r.y),
                                        content_top(r.y), content_bottom(r.y) {}
    };
    Layout& layout_() { return layout_stack_.back(); }
    const Layout& layout_() const { return layout_stack_.back(); }
    float cursor_x_() const {
        const Layout& l = layout_();
        return l.same_line ? l.same_line_x : l.region.x + l.indent;
    }

    struct LayerRect { Id id; Box rect; int z; bool modal = false; };
    struct LayerFrame { Id id; int z; };
    struct PopupState {
        Id id = 0;
        glm::vec2 pos{0.0f};
        glm::vec2 size{0.0f};
        bool modal = false;
        bool from_menubar = false;
        uint64_t seen_frame = 0;
        uint64_t opened_frame = 0;
    };
    struct MenuBarState { bool active = false; Box rect; float x = 0.0f; };
    struct LastItem { Id id = 0; Box rect; bool hovered = false; bool deactivated = false; };
    struct DragPayload { bool active = false; Id source = 0; std::string type, payload, label; };
    struct TextEdit {
        bool active = false;
        Id id = 0;
        std::string buffer;
        std::string original;
        size_t caret = 0;
        bool select_all = false;
        float scroll_x = 0.0f;
        uint64_t seen_frame = 0;
        bool numeric = false;
    };

    // --- helpers ---
    Rect to_canvas_(const Box& b) const {
        return Rect{{b.x, size_.y - b.bottom()}, {b.right(), size_.y - b.y}};
    }
    glm::vec2 flip_(glm::vec2 p) const { return {p.x, size_.y - p.y}; }
    void apply_clip_() { if (dl_) { dl_->pop_clip(); dl_->push_clip(to_canvas_(clip_stack_.back())); } }
    void push_clip_raw_(const Box& b) { clip_stack_.push_back(b); apply_clip_(); }
    void pop_clip_() { if (clip_stack_.size() > 1) clip_stack_.pop_back(); apply_clip_(); }

    bool any_pressed_() const { return in_.pressed[0] || in_.pressed[1] || in_.pressed[2]; }
    bool any_down_() const { return in_.down[0] || in_.down[1] || in_.down[2]; }
    bool layer_ok_() const { return layer_stack_.back().id == hovered_layer_; }
    bool hoverable_(const Box& b, Id self = 0) const {
        return layer_ok_() && b.contains(in_.mouse) && clip_stack_.back().contains(in_.mouse) &&
               (active_id_ == 0 || (self != 0 && active_id_ == self));
    }
    bool press_origin_hit_(Mouse b, const Box& r) const { return r.contains(press_pos_[static_cast<int>(b)]); }

    /** @brief Button behaviour: returns true on click (press + release inside). */
    bool behavior_(Id id, const Box& b, bool& hovered, bool& held) {
        hovered = layer_ok_() && b.contains(in_.mouse) && clip_stack_.back().contains(in_.mouse) &&
                  (active_id_ == 0 || active_id_ == id);
        if (hovered) hot_id_ = id;
        bool clicked = false;
        if (hovered && in_.pressed[0]) {
            active_id_ = id;
            press_pos_[0] = in_.mouse;
            activated_id_ = id;
        }
        held = active_id_ == id && in_.down[0];
        if (active_id_ == id && in_.released[0]) {
            clicked = hovered && !drag_.active;
            active_id_ = 0;
            deactivated_id_ = id;
        }
        return clicked;
    }

    void set_last_(Id id, const Box& b, bool hovered) {
        last_item_.id = id;
        last_item_.rect = b;
        last_item_.hovered = hovered;
        last_item_.deactivated = id != 0 && deactivated_id_ == id;
        if (last_item_.deactivated) deactivated_id_ = 0;
    }

    int find_popup_(Id id) const {
        for (size_t i = 0; i < open_popups_.size(); ++i) if (open_popups_[i].id == id) return static_cast<int>(i);
        return -1;
    }

    void open_popup_id_(Id id, glm::vec2 pos, bool modal, bool from_menubar) {
        const int existing = find_popup_(id);
        if (existing >= 0) { open_popups_[static_cast<size_t>(existing)].pos = pos; return; }
        // Opened from inside popup N: nests at depth N+1; opened from the base layer: replaces
        // every non-modal popup.
        const size_t depth = popup_draw_stack_.size();
        if (open_popups_.size() > depth) open_popups_.resize(depth);
        PopupState p;
        p.id = id;
        p.pos = pos;
        p.modal = modal;
        p.from_menubar = from_menubar;
        p.seen_frame = frame_;
        p.opened_frame = frame_;
        open_popups_.push_back(p);
    }

    bool begin_popup_id_(Id id, float min_w) {
        const int idx = find_popup_(id);
        if (idx < 0) return false;
        PopupState& p = open_popups_[static_cast<size_t>(idx)];
        p.seen_frame = frame_;
        const int z = kPopupZ + (idx + 1) * 10;
        glm::vec2 sz = glm::max(p.size, glm::vec2(min_w, style.row_height));
        glm::vec2 pos = p.pos;
        if (pos.x + sz.x > size_.x) pos.x = std::max(0.0f, size_.x - sz.x - 2);
        if (pos.y + sz.y > size_.y) pos.y = std::max(0.0f, size_.y - sz.y - 2);
        Box frame{pos.x, pos.y, sz.x, sz.y};
        dl_->set_z_order(z + 1);
        layer_stack_.push_back({id, z});
        popup_draw_stack_.push_back(id);
        clip_stack_.push_back(Box{0, 0, size_.x, size_.y}.intersect(frame));
        apply_clip_();
        Layout l{Box{frame.x + 4, frame.y + 4, std::max(min_w, p.size.x) - 8, 1e6f}};
        l.cursor = l.region.pos();
        l.content_top = l.cursor.y;
        l.content_bottom = l.cursor.y;
        l.id = id;
        layout_stack_.push_back(l);
        push_id(static_cast<int64_t>(id));
        return true;
    }

    void end_popup_() {
        if (popup_draw_stack_.empty()) return;
        pop_id();
        const Layout l = layout_stack_.back();
        layout_stack_.pop_back();
        const Id id = popup_draw_stack_.back();
        popup_draw_stack_.pop_back();
        const int idx = find_popup_(id);
        const int z = layer_stack_.back().z;
        layer_stack_.pop_back();
        pop_clip_();
        if (idx >= 0) {
            PopupState& p = open_popups_[static_cast<size_t>(idx)];
            const glm::vec2 measured{std::max(l.region.w + 8, p.size.x), (l.content_bottom - l.content_top) + 8};
            p.size = glm::vec2(std::max(l.region.w + 8, 0.0f), measured.y);
            glm::vec2 pos = p.pos;
            if (pos.x + p.size.x > size_.x) pos.x = std::max(0.0f, size_.x - p.size.x - 2);
            if (pos.y + p.size.y > size_.y) pos.y = std::max(0.0f, size_.y - p.size.y - 2);
            Box frame{pos.x, pos.y, p.size.x, p.size.y};
            dl_->set_z_order(z);
            push_clip_raw_(Box{0, 0, size_.x, size_.y});
            fill(frame, style.popup_bg);
            outline(frame, style.border);
            pop_clip_();
            layers_.push_back({id, frame, z, false});
        }
        dl_->set_z_order(layer_stack_.back().z == 0 ? 0 : layer_stack_.back().z + 1);
    }

    bool combo_in_(Id id, const Box& b, int* index, const std::vector<std::string>& items) {
        bool hovered = false, held = false;
        const bool clicked = behavior_(id, b, hovered, held);
        fill(b, hovered ? style.field_hover : style.field);
        const std::string cur = (*index >= 0 && *index < static_cast<int>(items.size())) ? items[*index] : std::string("-");
        text_in({b.x, b.y, b.w - b.h, b.h}, cur, style.text);
        arrow({b.right() - b.h, b.y, b.h, b.h}, true, style.text_dim);
        const Id pid = hash_int(id, 11);
        if (clicked) {
            if (find_popup_(pid) >= 0) open_popups_.resize(static_cast<size_t>(find_popup_(pid)));
            else open_popup_id_(pid, {b.x, b.bottom() + 1}, false, false);
        }
        set_last_(id, b, hovered);
        bool changed = false;
        if (begin_popup_id_(pid, b.w)) {
            const size_t visible = std::min<size_t>(items.size(), 18);
            Box list = next_box(visible * (style.row_height + style.spacing));
            begin_region("combo_list", list, items.size() > visible);
            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                push_id(static_cast<int64_t>(i));
                if (selectable(items[i], i == *index)) {
                    if (*index != i) { *index = i; changed = true; }
                    close_all_popups();
                }
                pop_id();
            }
            end_region();
            end_popup_();
        }
        return changed;
    }

    bool drag_float_in_(Id id, const Box& b, float* v, float speed, float lo, float hi, const char* fmt,
                        const glm::vec4* tick) {
        bool pending_changed = false;
        if (pending_commit_.id == id) {
            char* end = nullptr;
            const float parsed = std::strtof(pending_commit_.value.c_str(), &end);
            if (end != pending_commit_.value.c_str()) {
                const float nv = std::clamp(parsed, lo, hi);
                pending_changed = nv != *v;
                *v = nv;
            }
            pending_commit_ = {};
        }
        // Typed entry mode.
        if (text_edit_.active && text_edit_.id == id) {
            std::string s;
            char buf[64];
            std::snprintf(buf, sizeof(buf), fmt, *v);
            s = buf;
            const float before = *v;
            if (input_text_in_(id, b, &s, true)) {
                char* end = nullptr;
                const float parsed = std::strtof(s.c_str(), &end);
                if (end != s.c_str()) *v = std::clamp(parsed, lo, hi);
            }
            set_last_(id, b, b.contains(in_.mouse));
            if (!text_edit_.active || text_edit_.id != id) last_item_.deactivated = true;
            return *v != before;
        }
        bool hovered = layer_ok_() && b.contains(in_.mouse) && clip_stack_.back().contains(in_.mouse) &&
                       (active_id_ == 0 || active_id_ == id);
        if (hovered) hot_id_ = id;
        bool changed = false;
        if (hovered && in_.pressed[0]) {
            active_id_ = id;
            press_pos_[0] = in_.mouse;
            drag_moved_ = false;
            drag_accum_ = 0.0f;
        }
        const bool held = active_id_ == id && in_.down[0];
        if (held && in_.mouse_delta.x != 0.0f) {
            drag_accum_ += std::abs(in_.mouse_delta.x);
            if (drag_accum_ > 2.0f) drag_moved_ = true;
            if (drag_moved_) {
                float step = speed;
                if (has(in_.mods, coopa::input::Mods::Shift)) step *= 10.0f;
                if (has(in_.mods, coopa::input::Mods::Alt)) step *= 0.1f;
                const float nv = std::clamp(*v + in_.mouse_delta.x * step, lo, hi);
                if (nv != *v) { *v = nv; changed = true; }
            }
        }
        bool deactivated = false;
        if (active_id_ == id && in_.released[0]) {
            active_id_ = 0;
            deactivated = drag_moved_;
            if (!drag_moved_ && hovered) {
                // A click without a drag: type a value.
                char buf[64];
                std::snprintf(buf, sizeof(buf), fmt, *v);
                begin_text_edit_(id, buf, true);
            }
        }
        fill(b, held ? style.field_hover : hovered ? style.field_hover : style.field);
        if (tick) fill({b.x, b.y + 2, 2, b.h - 4}, *tick);
        char buf[64];
        std::snprintf(buf, sizeof(buf), fmt, *v);
        text_in(b, buf, style.text, tick ? 6.0f : style.padding);
        set_last_(id, b, hovered);
        last_item_.deactivated = deactivated || pending_changed;
        return changed || pending_changed;
    }

    void begin_text_edit_(Id id, const std::string& value, bool numeric) {
        // Focus jumping straight from one field to another: the old field may already have
        // been declared this frame, so park its buffer for it to commit when next declared.
        if (text_edit_.active && text_edit_.id != id && text_edit_.buffer != text_edit_.original) {
            pending_commit_ = {text_edit_.id, text_edit_.buffer};
        }
        text_edit_ = TextEdit{};
        text_edit_.active = true;
        text_edit_.id = id;
        text_edit_.buffer = value;
        text_edit_.original = value;
        text_edit_.caret = value.size();
        text_edit_.select_all = true;
        text_edit_.seen_frame = frame_;
        text_edit_.numeric = numeric;
        active_id_ = 0;
    }

    bool input_text_in_(Id id, const Box& b, std::string* value, bool /*boxed*/) {
        using coopa::input::Key;
        using coopa::input::KeyAction;
        using coopa::input::Mods;
        const bool editing = text_edit_.active && text_edit_.id == id;
        const bool hovered = layer_ok_() && b.contains(in_.mouse) && clip_stack_.back().contains(in_.mouse);
        if (hovered) hot_id_ = id;
        bool committed = false;

        if (!editing) {
            if (pending_commit_.id == id) {
                committed = *value != pending_commit_.value;
                *value = pending_commit_.value;
                pending_commit_ = {};
            }
            if (hovered && in_.pressed[0]) begin_text_edit_(id, *value, false);
            fill(b, hovered ? style.field_hover : style.field);
            text_in(b, *value, style.text);
            set_last_(id, b, hovered);
            if (committed) last_item_.deactivated = true;
            return committed;
        }

        TextEdit& te = text_edit_;
        te.seen_frame = frame_;
        bool finish = false, cancel = false;
        // Click outside commits.
        if (any_pressed_() && !hovered) finish = true;
        else if (hovered && in_.pressed[0]) {
            te.select_all = false;
            te.caret = text.hit_index(te.buffer, style.font_size, in_.mouse.x - (b.x + style.padding) + te.scroll_x);
        }
        auto erase_selection = [&] {
            if (te.select_all) { te.buffer.clear(); te.caret = 0; te.select_all = false; return true; }
            return false;
        };
        const Mods cmd = command_mod();
        for (const auto& e : in_.keys) {
            if (e.action == KeyAction::Release) continue;
            const bool command = has(e.mods, cmd) || has(e.mods, Mods::Control);
            switch (e.key) {
                case Key::Enter: case Key::KpEnter: case Key::Tab: finish = true; break;
                case Key::Escape: cancel = true; break;
                case Key::Backspace:
                    if (!erase_selection() && te.caret > 0) { te.buffer.erase(te.caret - 1, 1); --te.caret; }
                    break;
                case Key::Delete:
                    if (!erase_selection() && te.caret < te.buffer.size()) te.buffer.erase(te.caret, 1);
                    break;
                case Key::Left:  te.select_all = false; if (te.caret > 0) --te.caret; break;
                case Key::Right: te.select_all = false; if (te.caret < te.buffer.size()) ++te.caret; break;
                case Key::Home:  te.select_all = false; te.caret = 0; break;
                case Key::End:   te.select_all = false; te.caret = te.buffer.size(); break;
                case Key::A: if (command) te.select_all = true; break;
                case Key::C: if (command && in_.set_clipboard) in_.set_clipboard(te.buffer); break;
                case Key::X:
                    if (command && in_.set_clipboard) { in_.set_clipboard(te.buffer); te.buffer.clear(); te.caret = 0; te.select_all = false; }
                    break;
                case Key::V:
                    if (command && in_.get_clipboard) {
                        erase_selection();
                        std::string clip = in_.get_clipboard();
                        clip.erase(std::remove_if(clip.begin(), clip.end(), [](char c) { return c == '\n' || c == '\r'; }), clip.end());
                        te.buffer.insert(te.caret, clip);
                        te.caret += clip.size();
                    }
                    break;
                default: break;
            }
        }
        const bool command_held = has(in_.mods, cmd) || has(in_.mods, Mods::Control);
        if (!command_held) {
            for (uint32_t c : in_.chars) {
                if (c < 32 || c > 126) continue;
                erase_selection();
                te.buffer.insert(te.buffer.begin() + static_cast<long>(te.caret), static_cast<char>(c));
                ++te.caret;
            }
        }
        keyboard_consumed_ = true;

        // Draw.
        fill(b, style.field);
        outline(b, style.accent);
        const float pad = style.padding;
        const float caret_x = text.width(std::string_view(te.buffer).substr(0, te.caret), style.font_size);
        const float inner_w = b.w - pad * 2;
        if (caret_x - te.scroll_x > inner_w) te.scroll_x = caret_x - inner_w;
        if (caret_x - te.scroll_x < 0) te.scroll_x = caret_x;
        push_clip(b);
        const float lh = text.line_height(style.font_size);
        if (te.select_all && !te.buffer.empty()) {
            fill({b.x + pad - te.scroll_x, b.y + (b.h - lh) * 0.5f, text.width(te.buffer, style.font_size), lh}, style.selection);
        }
        draw_text({b.x + pad - te.scroll_x, b.y + (b.h - lh) * 0.5f}, te.buffer, style.text);
        if (std::fmod(time_, 1.0) < 0.6) fill({b.x + pad + caret_x - te.scroll_x, b.y + 4, 1, b.h - 8}, style.text);
        pop_clip();

        if (cancel) {
            text_edit_ = {};
        } else if (finish) {
            if (te.buffer != *value) { *value = te.buffer; committed = true; }
            text_edit_ = {};
        }
        set_last_(id, b, hovered);
        if (committed || cancel) last_item_.deactivated = true;
        return committed;
    }

    bool color_picker_(float* rgba, bool& any_active, bool& any_deact) {
        bool changed = false;
        glm::vec3 rgb = glm::clamp(glm::vec3(rgba[0], rgba[1], rgba[2]), 0.0f, 1.0f);
        State& st = state_(get_id("hsv"));
        // Keep hue/saturation stable while the colour is grey/black (they are undefined there).
        glm::vec3 hsv = rgb_to_hsv(rgb);
        if (st.init && (hsv.y < 1e-4f || hsv.z < 1e-4f)) { hsv.x = st.f[0]; if (hsv.z < 1e-4f) hsv.y = st.f[1]; }
        st.init = true;
        Box sv = next_box(150, 150);
        same_line();
        Box hue = next_box(150, 18);
        const glm::vec3 pure = hsv_to_rgb(hsv.x, 1, 1);
        gradient(sv, glm::vec4(1), glm::vec4(pure, 1), glm::vec4(pure, 1), glm::vec4(1));
        gradient(sv, glm::vec4(0, 0, 0, 0), glm::vec4(0, 0, 0, 0), glm::vec4(0, 0, 0, 1), glm::vec4(0, 0, 0, 1));
        for (int i = 0; i < 6; ++i) {
            const float y0 = hue.y + hue.h * i / 6.0f, y1 = hue.y + hue.h * (i + 1) / 6.0f;
            gradient({hue.x, y0, hue.w, y1 - y0}, glm::vec4(hsv_to_rgb(i / 6.0f, 1, 1), 1), glm::vec4(hsv_to_rgb(i / 6.0f, 1, 1), 1),
                     glm::vec4(hsv_to_rgb((i + 1) / 6.0f, 1, 1), 1), glm::vec4(hsv_to_rgb((i + 1) / 6.0f, 1, 1), 1));
        }
        bool h1 = false, held1 = false, h2 = false, held2 = false;
        behavior_(get_id("sv"), sv, h1, held1);
        behavior_(get_id("hue"), hue, h2, held2);
        if (held1) {
            hsv.y = std::clamp((in_.mouse.x - sv.x) / sv.w, 0.0f, 1.0f);
            hsv.z = 1.0f - std::clamp((in_.mouse.y - sv.y) / sv.h, 0.0f, 1.0f);
            changed = true;
        }
        if (held2) { hsv.x = std::clamp((in_.mouse.y - hue.y) / hue.h, 0.0f, 0.9999f); changed = true; }
        any_active |= held1 || held2;
        if (deactivated_id_ == get_id("sv") || deactivated_id_ == get_id("hue")) { any_deact = true; deactivated_id_ = 0; }
        st.f[0] = hsv.x;
        st.f[1] = hsv.y;
        const glm::vec2 m{sv.x + hsv.y * sv.w, sv.y + (1 - hsv.z) * sv.h};
        outline({m.x - 4, m.y - 4, 8, 8}, glm::vec4(1));
        fill({hue.x - 2, hue.y + hsv.x * hue.h - 1, hue.w + 4, 2}, glm::vec4(1));
        if (changed) {
            const glm::vec3 c = hsv_to_rgb(hsv.x, hsv.y, hsv.z);
            rgba[0] = c.r; rgba[1] = c.g; rgba[2] = c.b;
        }
        // Hex entry.
        char hex[16];
        const glm::vec3 cur = glm::clamp(glm::vec3(rgba[0], rgba[1], rgba[2]), 0.0f, 1.0f);
        std::snprintf(hex, sizeof(hex), "%02X%02X%02X", int(cur.r * 255 + 0.5f), int(cur.g * 255 + 0.5f), int(cur.b * 255 + 0.5f));
        std::string hs = hex;
        if (input_text("Hex", &hs)) {
            unsigned int v = 0;
            if (std::sscanf(hs.c_str(), "%x", &v) == 1) {
                rgba[0] = ((v >> 16) & 0xFF) / 255.0f;
                rgba[1] = ((v >> 8) & 0xFF) / 255.0f;
                rgba[2] = (v & 0xFF) / 255.0f;
                changed = true;
                any_deact = true;
            }
        }
        return changed;
    }

    // --- state ---
    DrawList* dl_ = nullptr;
    FrameInput in_;
    glm::vec2 size_{0.0f};
    double time_ = 0.0;
    uint64_t frame_ = 0;

    std::vector<Id> id_stack_;
    std::vector<Box> clip_stack_;
    std::vector<Layout> layout_stack_;
    std::vector<LayerFrame> layer_stack_;
    std::vector<LayerRect> layers_, layers_prev_;
    int modal_z_prev_ = -1;
    Id hovered_layer_ = 0;
    bool popup_hovered_ = false;

    std::vector<PopupState> open_popups_;
    std::vector<Id> popup_draw_stack_;
    std::vector<Id> closed_by_click_;
    MenuBarState menubar_;

    Id hot_id_ = 0;
    Id active_id_ = 0;
    Id activated_id_ = 0;
    Id deactivated_id_ = 0;
    glm::vec2 press_pos_[3] = {};
    bool drag_moved_ = false;
    float drag_accum_ = 0.0f;
    float scroll_grab_offset_ = 0.0f;
    bool group_active_ = false;
    Box last_label_box_;
    LastItem last_item_;
    bool keyboard_consumed_ = false;
    bool wheel_consumed_ = false;

    Id last_click_id_ = 0;
    double last_click_time_ = 0.0;

    std::string tooltip_;
    Id tooltip_target_ = 0;
    double tooltip_start_ = 0.0;

    DragPayload drag_;
    Id drag_candidate_ = 0;

    TextEdit text_edit_;
    struct PendingCommit { Id id = 0; std::string value; } pending_commit_;
    std::unordered_map<Id, State> states_;
};

}  // namespace imm
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_IMMEDIATE_IMM_H
