/**
 * @file file_dialog.h
 * @brief UIBuilder's file-dialog factory: a modal browser built on make_dialog().
 *
 * Layered on top of make_dialog(DialogMode::Modal) rather than built from scratch, because
 * everything a file picker needs around its listing already exists there: the scrim, the
 * ModalContext input blocking (pointer AND keyboard, so the filename field cannot be
 * blurred by a stray click behind the dialog), the titled header with its close button,
 * and the role-styled footer. This file adds the listing, the path readout and the
 * filename row.
 *
 * The listing is a fixed pool of row widgets, built once here and never destroyed --
 * see FileBrowser's class doc (widgets/file_browser.h) for why rebuilding it per directory
 * would be a use-after-free.
 */

#ifndef UICOOPA_BUILDER_DETAIL_FILE_DIALOG_H
#define UICOOPA_BUILDER_DETAIL_FILE_DIALOG_H

#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/builder/detail/containers.h>
#include <uicoopa/builder/detail/dialogs.h>
#include <uicoopa/builder/detail/text_style.h>
#include <uicoopa/builder/detail/widgets.h>
#include <uicoopa/layout/layout_element.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/groups/layout_group.h>
#include <uicoopa/render/icon_library.h>
#include <uicoopa/widgets/file_browser.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/mask.h>
#include <uicoopa/widgets/scrollbar.h>
#include <uicoopa/widgets/text.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace coopa {
namespace ui {
namespace detail {

using coopa::scene::SceneObject;

/**
 * @struct FileDialogParts
 * @brief The nodes make_file_dialog() built, for UIBuilder::file_dialog() to wrap.
 */
struct FileDialogParts {
    DialogParts  dialog;                 /**< @brief The modal frame this is built on. */
    FileBrowser* component = nullptr;    /**< @brief The behaviour, on dialog.node. */
};

/**
 * @brief A modal file browser: path readout, virtualized listing, filename field, actions.
 *
 * @param ctx          Parent node + theme to build into.
 * @param name         Root SceneObject name.
 * @param title        Header title, e.g. "Import map".
 * @param mode         Open (must exist) or Save (may be a new name). See FileDialogMode.
 * @param extensions   Lowercase, dot-included filters ({".yaml"}); empty shows every file.
 * @param size         Frame size in canvas pixels.
 * @param visible_rows Size of the row pool the listing is virtualized over.
 * @return The built nodes; see FileDialogParts.
 */
inline FileDialogParts make_file_dialog(BuildContext ctx, const std::string& name,
                                        const std::string& title, FileDialogMode mode,
                                        std::vector<std::string> extensions,
                                        glm::vec2 size, int visible_rows) {
    const UITheme& theme = *ctx.theme;
    const float row_h = theme.metrics.row_height;
    const float icon_sz = theme.icons.size;
    const float bar_w = theme.metrics.scrollbar_thickness;

    FileDialogParts parts;
    parts.dialog = make_dialog(ctx, name, title, size, DialogMode::Modal,
                               /*with_close_button=*/true, /*with_footer=*/true,
                               /*close_on_scrim_click=*/false, /*blocks_input=*/true);

    auto* browser = parts.dialog.node->add_component<FileBrowser>();
    browser->mode = mode;
    browser->extensions = std::move(extensions);
    parts.component = browser;

    BuildContext body = ctx.into(parts.dialog.body);

    // Two fixed strips with a weighted remainder between them -- exactly the case
    // child_distribute_by_weight exists for, so the listing takes whatever is left.
    std::vector<SceneObject*> sections;
    make_split(body, "FileRows", /*horizontal=*/false,
               { Section{"PathRow", 0.0f, row_h, SectionFlow::Horizontal},
                 Section{"List",    1.0f, 0.0f,  SectionFlow::None},
                 Section{"NameRow", 0.0f, row_h, SectionFlow::Horizontal} },
               theme.metrics.row_spacing, LayoutPadding{}, sections);
    SceneObject* path_row = sections[0];
    SceneObject* list_node = sections[1];
    SceneObject* name_row = sections[2];

    // --- Path row: an up button and the current directory. ---
    make_icon_button(ctx.into(path_row), theme.icons.scroll_up, icon_sz,
                     ButtonRole::Neutral, [browser]() { browser->go_up(); });
    {
        auto path_obj = std::make_unique<SceneObject>("Path");
        auto* prt = path_obj->add_component<RectTransform>();
        prt->set_size_delta({0.0f, row_h});
        auto* ple = path_obj->add_component<LayoutElement>();
        ple->preferred_size = {-1.0f, row_h};
        ple->flexible_size = {1.0f, -1.0f};
        auto* ptxt = path_obj->add_component<Text>();
        apply_role_font(ptxt, theme, FontRole::Caption);
        ptxt->color = theme.text.secondary;
        ptxt->horizontal_align = HorizontalAlign::Left;
        ptxt->vertical_align = VerticalAlign::Middle;
        browser->path_label = ptxt;
        path_row->add_child(std::move(path_obj));
    }

    // --- Listing: the fixed row pool plus the scrollbar that pages it. ---
    list_node->add_component<Image>()->color = theme.panel.panel_alt;
    // The rows live in their own node inset from the scrollbar, so a row's full width is
    // clickable without reaching under the bar.
    auto rows_obj = std::make_unique<SceneObject>("Rows");
    auto* rows_rt = rows_obj->add_component<RectTransform>();
    rows_rt->anchor_preset(AnchorPreset::StretchAll);
    rows_rt->set_offset_min({2.0f, 2.0f});
    rows_rt->set_offset_max({-(bar_w + 4.0f), -2.0f});
    auto* rows_group = rows_obj->add_component<VerticalLayoutGroup>();
    rows_group->spacing = 1.0f;
    rows_group->child_force_expand_width = true;
    rows_group->child_force_expand_height = false;
    rows_obj->add_component<Mask>();  // Clips a row whose name is wider than the column.

    Sprite* folder_sprite = IconLibrary::instance().icon(theme.icons.file_folder);
    browser->rows.reserve(static_cast<std::size_t>(visible_rows));
    for (int i = 0; i < visible_rows; ++i) {
        auto row_obj = std::make_unique<SceneObject>("Row_" + std::to_string(i));
        row_obj->add_component<RectTransform>()->set_size_delta({0.0f, row_h});
        row_obj->add_component<LayoutElement>()->preferred_size = {-1.0f, row_h};
        row_obj->add_component<Image>()->color = theme.menu.item_normal;
        auto* row_btn = row_obj->add_component<Button>();
        row_btn->colors.normal      = theme.menu.item_normal;
        row_btn->colors.highlighted = theme.menu.item_hover;
        row_btn->colors.pressed     = theme.menu.item_press;
        row_btn->colors.disabled    = theme.menu.item_normal;
        // Wired ONCE, to the pool SLOT -- never to an entry index. apply_page_() slides
        // which entry a slot shows; this connection never has to change.
        row_btn->on_click.connect([browser, i]() { browser->activate_row(i); });

        FileRow row;
        row.node = row_obj.get();
        row.button = row_btn;

        if (folder_sprite) {
            auto icon_obj = std::make_unique<SceneObject>("Icon");
            auto* irt = icon_obj->add_component<RectTransform>();
            irt->anchor_preset(AnchorPreset::MiddleLeft);
            irt->set_size_delta({icon_sz, icon_sz});
            irt->set_anchored_position({6.0f, 0.0f});
            irt->hittable = false;
            auto* iimg = icon_obj->add_component<Image>();
            iimg->sprite = folder_sprite;
            iimg->color = theme.text.accent;
            row.icon = iimg;
            row_obj->add_child(std::move(icon_obj));
        }

        auto lbl_obj = std::make_unique<SceneObject>("Label");
        auto* lrt = lbl_obj->add_component<RectTransform>();
        lrt->anchor_preset(AnchorPreset::StretchAll);
        // A fixed inset whether or not the folder glyph shows, so names stay aligned.
        lrt->set_offset_min({6.0f + icon_sz + 6.0f, 0.0f});
        lrt->set_offset_max({-8.0f, 0.0f});
        lrt->hittable = false;
        auto* ltxt = lbl_obj->add_component<Text>();
        apply_role_font(ltxt, theme, FontRole::Label);
        ltxt->color = theme.text.primary;
        ltxt->horizontal_align = HorizontalAlign::Left;
        ltxt->vertical_align = VerticalAlign::Middle;
        ltxt->overflow = TextOverflow::Truncate;
        row.label = ltxt;
        row_obj->add_child(std::move(lbl_obj));

        browser->rows.push_back(row);
        rows_obj->add_child(std::move(row_obj));
    }
    list_node->add_child(std::move(rows_obj));

    {
        auto bar_obj = std::make_unique<SceneObject>("Scrollbar");
        auto* brt = bar_obj->add_component<RectTransform>();
        brt->set_anchor_min({1.0f, 0.0f});
        brt->set_anchor_max({1.0f, 1.0f});
        brt->set_pivot({1.0f, 0.5f});
        brt->set_size_delta({bar_w, 0.0f});
        bar_obj->add_component<Image>()->color = theme.slider.track;
        auto* bar = bar_obj->add_component<Scrollbar>();
        bar->direction = ScrollbarDirection::Vertical;

        auto handle_obj = std::make_unique<SceneObject>("Handle");
        auto* hrt = handle_obj->add_component<RectTransform>();
        hrt->set_anchor_min({0.0f, 1.0f});
        hrt->set_anchor_max({1.0f, 1.0f});
        hrt->set_pivot({0.5f, 1.0f});
        hrt->set_size_delta({0.0f, 40.0f});
        hrt->hittable = false;
        handle_obj->add_component<Image>()->color = theme.slider.handle;
        bar->handle_rect = hrt;
        bar_obj->add_child(std::move(handle_obj));

        browser->scrollbar = bar;
        list_node->add_child(std::move(bar_obj));
    }

    // --- Name row: the label and the field the confirmed path is read from. ---
    //
    // Both need an explicit LayoutElement. make_label() anchors StretchAll and reports no
    // preferred width, and make_text_field() only sets a size_delta -- inside a
    // HorizontalLayoutGroup that leaves the label collapsed to nothing and the field at a
    // fixed 160px instead of filling the row. The labelled *_row() helpers in rows.h solve
    // the same problem the same way.
    Text* name_label = make_label(ctx.into(name_row), "File name", 0.0f, theme.text.secondary,
                                  "NameLabel");
    auto* name_label_le = name_label->owner->add_component<LayoutElement>();
    name_label_le->preferred_size = {theme.metrics.label_width * 0.5f, row_h};

    browser->name_field = make_text_field(ctx.into(name_row), "NameField", "", nullptr);
    auto* field_le = browser->name_field->owner->get_component<LayoutElement>();
    if (!field_le) field_le = browser->name_field->owner->add_component<LayoutElement>();
    field_le->preferred_size = {-1.0f, row_h};
    field_le->flexible_size = {1.0f, -1.0f};

    // --- Footer actions. Cancel first, so Confirm sits at the trailing edge. ---
    BuildContext footer = ctx.into(parts.dialog.footer);
    make_button(footer, "Cancel", ButtonRole::Neutral, [browser]() { browser->cancel(); });
    browser->confirm_button = make_button(
        footer, mode == FileDialogMode::Save ? "Save" : "Open",
        ButtonRole::Primary, [browser]() { browser->confirm(); });

    return parts;
}

}  // namespace detail
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_FILE_DIALOG_H
