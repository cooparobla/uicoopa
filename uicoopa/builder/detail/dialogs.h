/**
 * @file dialogs.h
 * @brief UIBuilder's dialog factory: a titled frame, optionally a scrim, optionally a footer.
 *
 * Builds a plain SceneObject subtree (plus one Dialog component -- see widgets/dialog.h
 * -- for Window/Modal) and hands back a DialogParts of raw pointers, which
 * UIBuilder::dialog() wraps as the small copyable DialogHandle value type (see
 * ui_builder.h), mirroring UIBuilder's own two-pointer style. The subtree is built (and
 * left) ACTIVE regardless of mode -- see Dialog's doc for why a Modal dialog can't
 * simply be hidden here, at construction time, before the caller has populated body()/
 * footer() with content of their own.
 */

#ifndef UICOOPA_BUILDER_DETAIL_DIALOGS_H
#define UICOOPA_BUILDER_DETAIL_DIALOGS_H

#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/builder/detail/text_style.h>
#include <uicoopa/builder/detail/widgets.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/groups/layout_group.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/widgets/button.h>
#include <uicoopa/widgets/dialog.h>
#include <memory>
#include <string>

namespace coopa {
namespace ui {

/**
 * @enum DialogMode
 * @brief How a UIBuilder::dialog() presents itself. Named DialogMode, not "DialogStyle",
 *        to avoid colliding with a future coopa::ui::DialogStyle theme struct.
 */
enum class DialogMode {
    Embedded, /**< A StretchAll child of this node -- no scrim, no floating frame. The
                   "the dialog is just the display canvas (or some other container)"
                   case. Built active; show()/hide() toggle it like any other mode. Never
                   blocks input -- has no Dialog component at all. */
    Window,   /**< A centered titled frame, no scrim. Built active. Never blocks input,
                   regardless of dialog()'s blocks_input parameter -- only Modal can. */
    Modal,    /**< Full-screen click-swallowing scrim + centered frame. Built active, then
                   closed by its Dialog component's start() (see widgets/dialog.h) once
                   population is complete -- so is_open() reports true until start() has
                   run at least once (typically via the app's one Scene::start() call).
                   Blocks input (pointer AND keyboard focus) to everything outside this
                   subtree while open, by default -- see Dialog::blocks_input and
                   ModalContext (uicoopa/input/modal_context.h). That blocking is global
                   to wherever this dialog's EventSystem/canvas is, independent of which
                   node dialog() was called on -- but the scrim's own visual darkening
                   still only spans ctx.parent's rect (a known limitation: build on the
                   canvas root, or reparent the scrim yourself, for full-screen dimming). */
};

namespace detail {

using coopa::scene::SceneObject;

/** @brief Raw pointers into a just-built dialog subtree -- see dialogs.h's file comment. */
struct DialogParts {
    SceneObject* node   = nullptr; /**< Root: is_open()/show()/hide() toggle this node's active(). */
    SceneObject* scrim  = nullptr; /**< Null for Embedded and Window. */
    SceneObject* frame  = nullptr; /**< Null for Embedded. */
    SceneObject* header = nullptr; /**< Null for Embedded. */
    SceneObject* body   = nullptr; /**< Always present -- where callers add content. */
    SceneObject* footer = nullptr; /**< Null unless with_footer and not Embedded. */
    Button*      close_button = nullptr;
};

/**
 * @brief Builds a dialog subtree per `mode` -- see DialogMode's doc for the three shapes.
 *
 * Built (and left) active in every mode, including Modal -- see widgets/dialog.h's
 * Dialog::start(), added below for Window/Modal, for why the actual initial hide is
 * deferred to that component's start() rather than applied here at construction time.
 * `close_button`'s click always closes the dialog (via its Dialog component, so
 * ModalContext stays in sync); `close_on_scrim_click` additionally wires the scrim
 * itself the same way. `blocks_input` only ever takes effect for DialogMode::Modal --
 * see DialogMode's doc.
 */
inline DialogParts make_dialog(BuildContext ctx, const std::string& name, const std::string& title,
                               glm::vec2 size, DialogMode mode,
                               bool with_close_button, bool with_footer, bool close_on_scrim_click,
                               bool blocks_input) {
    const UITheme& theme = *ctx.theme;
    DialogParts parts;

    auto root_obj = std::make_unique<SceneObject>(name);
    auto* root_rt = root_obj->add_component<RectTransform>();
    root_rt->anchor_preset(AnchorPreset::StretchAll);
    root_rt->set_size_delta({0.0f, 0.0f});  // see make_vertical_layout()'s comment on this line.
    SceneObject* root_raw = root_obj.get();

    Dialog* dialog_comp = nullptr;
    if (mode != DialogMode::Embedded) {
        dialog_comp = root_obj->add_component<Dialog>();
        dialog_comp->starts_open = (mode != DialogMode::Modal);
        dialog_comp->blocks_input = (mode == DialogMode::Modal) && blocks_input;
    }

    if (mode == DialogMode::Embedded) {
        auto body_obj = std::make_unique<SceneObject>("Body");
        auto* body_rt = body_obj->add_component<RectTransform>();
        body_rt->anchor_preset(AnchorPreset::StretchAll);
        body_rt->set_size_delta({0.0f, 0.0f});
        parts.body = body_obj.get();
        root_obj->add_child(std::move(body_obj));

        parts.node = root_raw;
        ctx.parent->add_child(std::move(root_obj));
        return parts;
    }

    SceneObject* scrim_raw = nullptr;
    if (mode == DialogMode::Modal) {
        auto scrim_obj = std::make_unique<SceneObject>("Scrim");
        auto* scrim_rt = scrim_obj->add_component<RectTransform>();
        scrim_rt->anchor_preset(AnchorPreset::StretchAll);
        scrim_rt->set_size_delta({0.0f, 0.0f});
        // Elevates the scrim (and the frame built below) above every later sibling in
        // whatever this dialog was built into, and lets both escape an ancestor Mask --
        // see RectTransform::z_order's doc, and make_dropdown's popup for precedent.
        scrim_rt->z_order = 1;
        glm::vec4 scrim_color = theme.panel.background;
        scrim_color.a = theme.metrics.scrim_alpha;
        auto* scrim_img = scrim_obj->add_component<Image>();
        scrim_img->color = scrim_color;
        scrim_img->raycast_target = true;  // Full-rect and opaque to hit-testing -- swallows clicks to
                                            // whatever is behind the dialog even with no Button attached.
        if (close_on_scrim_click) {
            auto* scrim_btn = scrim_obj->add_component<Button>();
            scrim_btn->colors.fade_duration = 0.0f;  // No hover/press tint -- the scrim shouldn't visibly react.
            scrim_btn->colors.normal = scrim_btn->colors.highlighted = scrim_btn->colors.pressed = scrim_color;
            scrim_btn->on_click.connect([dialog_comp]() { dialog_comp->close(); });
        }
        scrim_raw = scrim_obj.get();
        parts.scrim = scrim_raw;
        root_obj->add_child(std::move(scrim_obj));
    }

    auto frame_obj = std::make_unique<SceneObject>("Frame");
    auto* frame_rt = frame_obj->add_component<RectTransform>();
    frame_rt->anchor_preset(AnchorPreset::MiddleCenter);
    frame_rt->set_size_delta(size);
    frame_rt->z_order = 1;  // Same reasoning as the scrim above.
    frame_obj->add_component<Image>()->color = theme.panel.panel;

    const float header_h = theme.metrics.card_header_height;
    auto header_obj = std::make_unique<SceneObject>("Header");
    auto* header_rt = header_obj->add_component<RectTransform>();
    header_rt->anchor_preset(AnchorPreset::StretchTop);
    header_rt->set_size_delta({0.0f, header_h});
    header_obj->add_component<Image>()->color = theme.panel.header_bar;

    auto title_obj = std::make_unique<SceneObject>("Title");
    auto* title_rt = title_obj->add_component<RectTransform>();
    title_rt->anchor_preset(AnchorPreset::StretchAll);
    title_rt->set_offset_min({14.0f, 0.0f});
    title_rt->set_offset_max({with_close_button ? -header_h : -12.0f, 0.0f});
    title_rt->hittable = false;
    auto* title_txt = title_obj->add_component<Text>();
    apply_role_font(title_txt, theme, FontRole::Title);
    title_txt->text = title;
    title_txt->color = theme.text.primary;
    title_txt->horizontal_align = HorizontalAlign::Left;
    title_txt->vertical_align = VerticalAlign::Middle;
    header_obj->add_child(std::move(title_obj));

    if (with_close_button) {
        auto close_obj = std::make_unique<SceneObject>("CloseButton");
        auto* close_rt = close_obj->add_component<RectTransform>();
        close_rt->anchor_preset(AnchorPreset::MiddleRight);
        close_rt->set_size_delta({header_h - 8.0f, header_h - 8.0f});
        close_rt->set_anchored_position({-6.0f, 0.0f});
        close_obj->add_component<Image>()->color = theme.button.normal;
        auto* close_btn = close_obj->add_component<Button>();
        close_btn->colors.normal = theme.button.normal;
        close_btn->colors.highlighted = theme.button.hover;
        close_btn->colors.pressed = theme.button.press;
        close_btn->colors.disabled = theme.button.disabled;
        close_btn->on_click.connect([dialog_comp]() { dialog_comp->close(); });

        // Falls back to an "x" glyph if no IconLibrary sheet is loaded -- same pattern
        // as make_spinbox_step_button_'s icon/glyph fallback.
        if (!make_icon(ctx.into(close_obj.get()), "cross", theme.text.size_label, theme.text.primary, "Icon")) {
            auto x_obj = std::make_unique<SceneObject>("Txt");
            auto* x_rt = x_obj->add_component<RectTransform>();
            x_rt->anchor_preset(AnchorPreset::MiddleCenter);
            x_rt->hittable = false;
            auto* x_txt = x_obj->add_component<Text>();
            apply_role_font(x_txt, theme, FontRole::Label);
            x_txt->text = "x";
            x_txt->color = theme.text.primary;
            x_txt->horizontal_align = HorizontalAlign::Center;
            x_txt->vertical_align = VerticalAlign::Middle;
            close_obj->add_child(std::move(x_obj));
        } else {
            close_obj->children().back()->get_component<RectTransform>()->hittable = false;
        }

        parts.close_button = close_btn;
        header_obj->add_child(std::move(close_obj));
    }
    parts.header = header_obj.get();

    const float footer_h = theme.metrics.row_height + theme.metrics.dialog_padding;
    const float body_bottom_inset = with_footer ? footer_h : theme.metrics.dialog_padding;

    auto body_obj = std::make_unique<SceneObject>("Body");
    auto* body_rt = body_obj->add_component<RectTransform>();
    body_rt->anchor_preset(AnchorPreset::StretchAll);
    body_rt->set_offset_min({theme.metrics.dialog_padding, body_bottom_inset});
    body_rt->set_offset_max({-theme.metrics.dialog_padding, -(header_h + theme.metrics.dialog_padding)});
    parts.body = body_obj.get();

    std::unique_ptr<SceneObject> footer_obj;
    if (with_footer) {
        footer_obj = std::make_unique<SceneObject>("Footer");
        auto* footer_rt = footer_obj->add_component<RectTransform>();
        footer_rt->anchor_preset(AnchorPreset::StretchBottom);
        footer_rt->set_size_delta({0.0f, footer_h});
        auto* fgroup = footer_obj->add_component<HorizontalLayoutGroup>();
        fgroup->spacing = theme.metrics.row_spacing;
        fgroup->padding = LayoutPadding{theme.metrics.dialog_padding, theme.metrics.dialog_padding, 8.0f, 8.0f};
        fgroup->child_alignment = ChildAlignment::MiddleRight;
        fgroup->child_force_expand_width  = false;
        fgroup->child_force_expand_height = false;
        fgroup->child_control_height = false;
        parts.footer = footer_obj.get();
    }

    frame_obj->add_child(std::move(header_obj));
    frame_obj->add_child(std::move(body_obj));
    if (footer_obj) frame_obj->add_child(std::move(footer_obj));
    parts.frame = frame_obj.get();
    root_obj->add_child(std::move(frame_obj));

    // Deliberately no start()/set_active(false) here for Modal -- see this function's
    // doc comment and Dialog::start() (added above) for why that's deferred.

    parts.node = root_raw;
    ctx.parent->add_child(std::move(root_obj));
    return parts;
}

}  // namespace detail
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_DIALOGS_H
