/**
 * @file test_settings_builder.cpp
 * @brief Windowed demo: the settings_demo-shaped UI (a tabbed settings
 *        inspector, an info banner, a live status readout, an inventory
 *        grid, and an action bar with a modal confirm dialog), built
 *        entirely through UIBuilder -- zero scene YAML except the theme
 *        file loaded via ThemeLibrary.
 *
 * Proves the builder split (uicoopa/builder/ui_builder.h + detail/*.h) is
 * expressive enough to stand up a real, multi-panel UI without hand-rolling
 * a single SceneObject outside the Canvas root, and that a theme YAML file
 * (assets/themes/dark.yaml or light.yaml, chosen via THEME=) drives every
 * color in the demo -- nothing here hardcodes a color literal. It also
 * exercises the split/dialog/tab additions end to end: build_settings_panel()
 * groups its seven sections into a UIBuilder::tab_view() instead of a flat
 * scroll list, build_action_panel()/build_info_banner()/build_inventory_card()
 * replace hand-computed anchored-position offsets with UIBuilder::split_rows()/
 * split_columns(), and the Reset button now opens a UIBuilder::dialog()
 * (DialogMode::Modal) confirmation instead of resetting immediately.
 *
 * Unlike a YAML scene, this demo is genuinely interactive: the StatusCard's
 * nine readouts are recomputed every frame from the settings panel's live
 * values (via UIBuilder::get_value()), and the action buttons write back
 * into it (via UIBuilder::set_value()) and update a footer line.
 *
 * Set THEME=light to load assets/themes/light.yaml instead of the default
 * dark.yaml. Same MAX_FRAMES/SCREENSHOT_NAME/OPEN_COMBO/SLIDER_MAX/
 * HOVER_SLOT env hooks as test_window.cpp, for scripted screenshots, plus
 * SELECT_TAB=<index> (selects a page of the settings tab bar) and
 * OPEN_DIALOG=<name> (opens the "ResetConfirm" modal) below. EDIT_FIELD=<name>
 * additionally opens keyboard editing on a named TextField (e.g. "player_name") before
 * the render loop starts, so a screenshot can capture its blinking text caret.
 *
 * When built with audio support (UICOOPA_HAS_AUDIO, ON by default -- see
 * CMakeLists.txt's UICOOPA_WITH_AUDIO option), every Button in this demo gets
 * hover and click sounds via a single UiSoundPlayer on the Canvas object (see
 * uicoopa/audio/ui_sound_player.h) -- no per-button wiring. Set UI_AUDIO=0 to
 * skip audio entirely (no device opened, no sounds loaded), or SFX_DEVICE=null
 * to still exercise the audio path with miniaudio's silent null backend.
 */

// The one raw Vulkan include in this file, for save_screenshot()'s framebuffer
// create/destroy pair -- see that function's doc for why (gfxcoopa has no
// Framebuffer wrapper). Nothing else in this file names a Vk*/VK_*/GLFW_*
// symbol. Mirrors test_window.cpp's identical escape.
#include <volk/volk.h>

#include <root_directory.h>

#include <gfxcoopa/app/context.h>
#include <gfxcoopa/memory/image.h>
#include <gfxcoopa/memory/buffer.h>
#include <gfxcoopa/command/command_buffer.h>
#include <gfxcoopa/util/image_readback.h>

#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/canvas.h>
#include <uicoopa/layout/canvas_scaler.h>
#include <uicoopa/render/draw_list.h>
#include <uicoopa/render/ui_pass.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/text/font.h>
#include <uicoopa/text/font_defaults.h>
#include <uicoopa/input/ui_input.h>
#include <uicoopa/input/event_system.h>
#include <uicoopa/builder/ui_builder.h>
#include <uicoopa/ui_yaml.h>  // UIResourceCache::note_text_atlas_use()/mark_text_atlases()
#include <uicoopa/render/icon_library.h>

#ifdef UICOOPA_HAS_AUDIO
#include <uicoopa/audio/sound_library.h>
#include <uicoopa/audio/ui_audio.h>
#include <uicoopa/audio/ui_sound_player.h>
#endif

#include <coopa/asset/asset_manager.h>
#include <coopa/job/engine.h>
#include <coopa/scene/scene_object.h>
#include <coopa/scene/scene.h>
#include <coopa/animation/animator.h>
#include <coopa/animation/animation_clip.h>
#include <coopa/animation/animation_system.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace coopa::ui;
using coopa::scene::Scene;
using coopa::scene::SceneObject;
using coopa::anim::Animator;
using coopa::anim::AnimationClip;
using coopa::anim::AnimationTrack;
using coopa::anim::Interpolation;
using coopa::anim::WrapMode;

namespace {

/**
 * @brief Re-renders draw_list into a throwaway offscreen image and writes it to a PNG.
 *
 * Identical to test_window.cpp's save_screenshot() -- see that file's doc for
 * why the framebuffer create/destroy pair is the one raw-Vulkan block gfxcoopa
 * can't yet cover (it has no Framebuffer wrapper).
 */
void save_screenshot(coopa::gfx::app::Context& ctx,
                     UiPass& ui_pass,
                     const DrawList& draw_list,
                     uint32_t width, uint32_t height,
                     float scale_factor,
                     const std::string& out_path) {
    using namespace coopa::gfx;

    if (width == 0 || height == 0) {
        std::cerr << "[settings_builder] save_screenshot: zero-sized framebuffer, skipping.\n";
        return;
    }

    ui_pass.register_textures(draw_list);
    ui_pass.begin_frame(/*frame_index=*/0, draw_list.vertices().size(),
                        draw_list.indices().size());

    memory::Image capture_image(ctx.device(), ctx.allocator(), width, height, ctx.color_format(),
                                ImageUsage::ColorAttachment | ImageUsage::TransferSrc);

    VkImageView attachment = capture_image.view(); // gfx-allow-vulkan
    VkFramebufferCreateInfo fb_info{}; // gfx-allow-vulkan
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; // gfx-allow-vulkan
    fb_info.renderPass = ctx.render_pass().handle();
    fb_info.attachmentCount = 1;
    fb_info.pAttachments = &attachment;
    fb_info.width = width;
    fb_info.height = height;
    fb_info.layers = 1;

    VkFramebuffer framebuffer = VK_NULL_HANDLE; // gfx-allow-vulkan
    if (vkCreateFramebuffer(ctx.device().handle(), &fb_info, nullptr, &framebuffer) != VK_SUCCESS) { // gfx-allow-vulkan
        std::cerr << "[settings_builder] save_screenshot: vkCreateFramebuffer failed.\n"; // gfx-allow-vulkan
        return;
    }

    ctx.command_pool().submit_once([&](command::CommandBuffer& cmd) {
        cmd.begin_render_pass(ctx.render_pass().handle(), framebuffer, { width, height },
                              VkClearColorValue{{ 0.05f, 0.05f, 0.07f, 1.0f }}); // gfx-allow-vulkan
        ui_pass.draw(cmd, /*frame_index=*/0, width, height, scale_factor, draw_list);
        cmd.end_render_pass();
        capture_image.mark_transitioned(TextureUsage::Present);
    });

    vkDestroyFramebuffer(ctx.device().handle(), framebuffer, nullptr); // gfx-allow-vulkan

    util::ImageData data = util::read_image(ctx.device(), ctx.allocator(), ctx.command_pool(), capture_image);
    for (size_t i = 3; i < data.pixels.size(); i += 4) {
        data.pixels[i] = 255;
    }

    util::save_image_png(data, out_path);
    std::cout << "[settings_builder] Saved screenshot to " << out_path << "\n";
}

/** @brief Formats `v` to `decimals` places without pulling in <sstream>/<iomanip>. */
std::string format_float(float v, int decimals = 2) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

/**
 * @struct StatusReadouts
 * @brief The live status lines shown on StatusCard -- see build_status_card().
 */
struct StatusReadouts {
    Text* player_name   = nullptr;
    Text* resolution    = nullptr;
    Text* vsync         = nullptr;
    Text* quality       = nullptr;
    Text* render_scale  = nullptr;
    Text* shadows       = nullptr;
    Text* ao_strength   = nullptr;
    Text* volume        = nullptr;
    Text* difficulty    = nullptr;
    Text* region        = nullptr;
    Text* accessibility = nullptr;
    Text* autosave      = nullptr;
};

/**
 * @brief Builds the settings inspector (SettingsWindow): a titled card whose
 *        body is a UIBuilder::tab_view() with seven pages -- Display,
 *        Graphics, Audio, Gameplay, Controls, Accessibility, Network --
 *        each populated entirely via UIBuilder's row helpers. Replaces the
 *        old single 52-row scroll list (add_section_header() strips with no
 *        real grouping) with a real tabbed layout; every row still fits its
 *        page without scrolling once split this way.
 * @return The card's Body builder, so StatusReadouts and the env-var hooks
 *         below can keep reading back by name -- UIBuilder::get_value()
 *         searches the whole subtree regardless of which tab is selected
 *         (SceneObject::find_descendant() doesn't skip inactive nodes), so
 *         hidden pages' values stay live even while their tab isn't shown.
 */
UIBuilder build_settings_panel(UIBuilder root) {
    UIBuilder frame = root.card("SettingsWindow", "uicoopa Settings Inspector",
                                AnchorPreset::TopLeft, {20.0f, -20.0f}, {460.0f, 680.0f});

    TabSet tabs = frame.tab_view("SettingsTabs", {
        "Display", "Graphics", "Audio", "Gameplay", "Controls", "Accessibility", "Network"
    });

    UIBuilder display = tabs["Display"];
    display.add_dropdown_row("resolution", {"1280x720", "1600x900", "1920x1080", "2560x1440"}, 2);
    display.add_spinbox_row("width", 640.0, 3840.0, 1920.0, 1.0);
    display.add_spinbox_row("height", 360.0, 2160.0, 1080.0, 1.0);
    display.add_dropdown_row("window_mode", {"Windowed", "Borderless", "Fullscreen"}, 2);
    display.add_toggle_row("vsync", true);
    display.add_toggle_row("fullscreen", false);
    display.add_slider_row("brightness", 0.0f, 2.0f, 1.0f, 0.05f);
    display.add_slider_row("ui_scale", 0.75f, 1.5f, 1.0f, 0.05f);

    UIBuilder graphics = tabs["Graphics"];
    graphics.add_dropdown_row("quality", {"Low", "Medium", "High", "Ultra"}, 2);
    graphics.add_dropdown_row("texture_quality", {"Low", "Medium", "High"}, 2);
    graphics.add_dropdown_row("anisotropic_filtering", {"Off", "2x", "4x", "8x", "16x"}, 3);
    graphics.add_dropdown_row("anti_aliasing", {"Off", "FXAA", "TAA", "MSAA 4x"}, 2);
    graphics.add_slider_row("render_scale", 0.5f, 2.0f, 1.0f, 0.05f);
    graphics.add_slider_row("shadow_distance", 10.0f, 500.0f, 150.0f, 5.0f);
    graphics.add_slider_row("ao_strength", 0.0f, 1.0f, 0.6f, 0.05f);
    graphics.add_toggle_row("bloom_enabled", true);
    graphics.add_toggle_row("ssao_enabled", true);
    graphics.add_toggle_row("ssr_enabled", false);
    graphics.add_toggle_row("motion_blur", false);
    graphics.add_toggle_row("vignette", true);
    graphics.add_toggle_row("film_grain", false);

    UIBuilder audio = tabs["Audio"];
    audio.add_slider_row("master_volume", 0.0f, 1.0f, 0.8f);
    audio.add_slider_row("music_volume", 0.0f, 1.0f, 0.6f);
    audio.add_slider_row("sfx_volume", 0.0f, 1.0f, 0.9f);
    audio.add_slider_row("voice_volume", 0.0f, 1.0f, 0.75f);
    audio.add_slider_row("ambient_volume", 0.0f, 1.0f, 0.5f);
    audio.add_dropdown_row("audio_output", {"Default", "Speakers", "Headphones"}, 0);
    audio.add_toggle_row("mute_on_focus_loss", true);

    UIBuilder gameplay = tabs["Gameplay"];
    gameplay.add_text_field_row("player_name", "Player One");
    gameplay.add_dropdown_row("difficulty", {"Easy", "Normal", "Hard", "Nightmare"}, 1);
    gameplay.add_dropdown_row("language", {"English", "Spanish", "French", "German", "Japanese"}, 0);
    gameplay.add_slider_row("mouse_sensitivity", 0.1f, 5.0f, 1.0f, 0.1f);
    gameplay.add_toggle_row("camera_shake", true);
    gameplay.add_toggle_row("aim_assist", false);
    gameplay.add_toggle_row("subtitles", true);
    gameplay.add_toggle_row("autosave", true);
    gameplay.add_spinbox_row("autosave_interval_min", 1.0, 30.0, 5.0, 1.0);
    gameplay.add_toggle_row("hardcore_mode", false);

    UIBuilder controls = tabs["Controls"];
    controls.add_toggle_row("invert_y", false);
    controls.add_toggle_row("mouse_acceleration", false);
    controls.add_toggle_row("controller_vibration", true);
    controls.add_slider_row("gamepad_deadzone", 0.0f, 0.5f, 0.15f, 0.01f);
    controls.add_spinbox_row("key_repeat_delay_ms", 100.0, 1000.0, 300.0, 25.0);

    UIBuilder accessibility = tabs["Accessibility"];
    accessibility.add_dropdown_row("colorblind_mode", {"Off", "Protanopia", "Deuteranopia", "Tritanopia"}, 0);
    accessibility.add_slider_row("text_size", 0.75f, 2.0f, 1.0f, 0.05f);
    accessibility.add_toggle_row("high_contrast_ui", false);
    accessibility.add_toggle_row("reduce_motion", false);
    accessibility.add_toggle_row("screen_reader_support", false);

    UIBuilder network = tabs["Network"];
    network.add_text_field_row("server_address", "127.0.0.1");
    network.add_dropdown_row("region", {"NA-East", "NA-West", "EU", "Asia", "Oceania"}, 0);
    network.add_toggle_row("voice_chat_enabled", true);
    network.add_toggle_row("upnp_enabled", true);
    network.add_spinbox_row("max_ping_ms", 20.0, 300.0, 120.0, 5.0);
    network.add_spinbox_row("bandwidth_limit_mbps", 1.0, 1000.0, 100.0, 1.0);

    return frame;
}

/**
 * @brief Builds the InfoBanner: an accent title over a wrapped subtitle paragraph.
 *
 * Split into two split_rows() sections (title, subtitle) instead of two free-
 * positioned add_paragraph() calls each followed by a manual
 * `->owner->get_component<RectTransform>()->set_anchored_position(...)` reach-through --
 * each paragraph now just sits at its own section's default TopLeft origin.
 */
void build_info_banner(UIBuilder root) {
    const UITheme& theme = root.theme();
    constexpr float kBannerWidth = 760.0f;
    constexpr float kPad = 16.0f;

    UIBuilder banner = root.panel("InfoBanner").at(AnchorPreset::TopLeft, {500.0f, -20.0f}, {kBannerWidth, 100.0f});
    banner.with_background(theme.panel.panel_alt);

    SectionSet rows = banner.split_rows({
        {"Title", 1.0f, 32.0f, SectionFlow::None},
        {"Subtitle", 1.0f, 46.0f, SectionFlow::None},
    }, 2.0f, LayoutPadding{kPad, kPad, 8.0f, 8.0f});

    rows["Title"].add_paragraph("uicoopa Settings Builder Demo", theme.text.size_title,
                                theme.text.accent, {kBannerWidth - 2.0f * kPad, 28.0f}, "BannerTitle");
    rows["Subtitle"].add_paragraph(
        "Every panel on this screen is built by UIBuilder calls in test_settings_builder.cpp --\n"
        "no scene YAML. The theme (colors, sizes, metrics) loads from assets/themes/*.yaml.",
        theme.text.size_small, theme.text.secondary, {kBannerWidth - 2.0f * kPad, 42.0f}, "BannerSubtitle");
}

/** @brief Builds the StatusCard shell and its nine (initially blank) readout lines. */
StatusReadouts build_status_card(UIBuilder root) {
    UIBuilder body = root.card("StatusCard", "Live Settings Readout",
                              AnchorPreset::TopLeft, {500.0f, -135.0f}, {420.0f, 395.0f});
    UIBuilder list = body.vertical_layout("StatusContent", 8.0f, LayoutPadding{10.0f, 10.0f, 10.0f, 10.0f});

    StatusReadouts s;
    s.player_name   = list.add_status_line("Player: ...", TextRole::Accent, "Status_PlayerName");
    s.resolution    = list.add_status_line("Resolution: ...", TextRole::Secondary, "Status_Resolution");
    s.vsync         = list.add_status_line("VSync: ...", TextRole::Success, "Status_VSync");
    s.quality       = list.add_status_line("Quality: ...", TextRole::Accent, "Status_Quality");
    s.render_scale  = list.add_status_line("Render Scale: ...", TextRole::Secondary, "Status_RenderScale");
    s.shadows       = list.add_status_line("Shadow Distance: ...", TextRole::Secondary, "Status_Shadows");
    s.ao_strength   = list.add_status_line("AO Strength: ...", TextRole::Secondary, "Status_AO");
    s.volume        = list.add_status_line("Master Volume: ...", TextRole::Secondary, "Status_Volume");
    s.difficulty    = list.add_status_line("Difficulty: ...", TextRole::Accent, "Status_Difficulty");
    s.region        = list.add_status_line("Region: ...", TextRole::Secondary, "Status_Region");
    s.accessibility = list.add_status_line("Accessibility: ...", TextRole::Info, "Status_Accessibility");
    s.autosave      = list.add_status_line("Autosave: ...", TextRole::Success, "Status_Autosave");
    return s;
}

/** @brief Recomputes every StatusCard line from the settings panel's current values. */
void refresh_status_card(UIBuilder settings, const UITheme& theme, StatusReadouts& s) {
    // get_value<std::string> on a TextField returns the COMMITTED value (TextField::text()),
    // never a live in-progress edit buffer -- see values.h -- so this never echoes
    // half-typed input even while the field is mid-edit on any given frame.
    s.player_name->text = "Player: " + settings.get_value<std::string>("player_name");

    int w = settings.get_value<int>("width");
    int h = settings.get_value<int>("height");
    s.resolution->text = "Resolution: " + std::to_string(w) + " x " + std::to_string(h);

    bool vsync = settings.get_value<bool>("vsync");
    s.vsync->text = std::string("VSync: ") + (vsync ? "ON" : "OFF");
    s.vsync->color = vsync ? theme.text.success : theme.text.warning;

    s.quality->text = "Quality: " + settings.get_value<std::string>("quality");
    s.render_scale->text = "Render Scale: " + format_float(settings.get_value<float>("render_scale")) + "x";

    // Flagged Warning near the top of its range (attention-worthy, not necessarily bad),
    // Secondary otherwise -- mirrors the old demo's habit of color-coding notable values.
    float shadow_distance = settings.get_value<float>("shadow_distance");
    s.shadows->text = "Shadow Distance: " + format_float(shadow_distance, 0);
    s.shadows->color = shadow_distance >= 400.0f ? theme.text.warning : theme.text.secondary;

    s.ao_strength->text = "AO Strength: " + format_float(settings.get_value<float>("ao_strength"));
    s.volume->text = "Master Volume: " + format_float(settings.get_value<float>("master_volume"));
    s.difficulty->text = "Difficulty: " + settings.get_value<std::string>("difficulty");
    s.region->text = "Region: " + settings.get_value<std::string>("region");

    bool high_contrast = settings.get_value<bool>("high_contrast_ui");
    bool reduce_motion = settings.get_value<bool>("reduce_motion");
    bool screen_reader = settings.get_value<bool>("screen_reader_support");
    int accessibility_count = (high_contrast ? 1 : 0) + (reduce_motion ? 1 : 0) + (screen_reader ? 1 : 0);
    s.accessibility->text = "Accessibility: " + std::to_string(accessibility_count) + "/3 features on";

    bool autosave = settings.get_value<bool>("autosave");
    s.autosave->text = std::string("Autosave: ") + (autosave ? "ON" : "OFF");
    s.autosave->color = autosave ? theme.text.success : theme.text.warning;
}

/**
 * @brief Builds the InventoryCard: description, a 4x5 grid with sample items, and a footer.
 *
 * Split into three split_rows() sections instead of three free-positioned children each
 * followed by a manual RectTransform reach-through -- see build_info_banner()'s doc for
 * the same pattern.
 */
void build_inventory_card(UIBuilder root) {
    const UITheme& theme = root.theme();
    UIBuilder body = root.card("InventoryCard", "Inventory Grid System",
                              AnchorPreset::TopLeft, {940.0f, -135.0f}, {320.0f, 395.0f});

    SectionSet rows = body.split_rows({
        {"Desc", 1.0f, 36.0f, SectionFlow::None},
        {"Grid", 1.0f, 222.0f, SectionFlow::None},
        {"Footer", 1.0f, 0.0f, SectionFlow::None},  // size 0 -> weighted, fills what's left.
    }, 6.0f, LayoutPadding{14.0f, 14.0f, 10.0f, 10.0f});

    rows["Desc"].add_paragraph("Interactive 4x5 grid built with UIBuilder.\nSupports slot selection & drag-and-drop.",
                               theme.text.size_small, theme.text.secondary, {292.0f, 36.0f}, "InvDesc");

    InventoryGrid* grid = rows["Grid"].add_inventory_grid("GridArea", 4, 5, {48.0f, 48.0f}, {6.0f, 6.0f});
    grid->owner->get_component<RectTransform>()->anchor_preset(AnchorPreset::TopLeft);

    std::vector<InventoryItem> items;
    // Each item sets its own icon tint (InventoryItem::color) rather than relying on
    // InventorySlot::update_visuals()'s built-in id fallback, which only recognizes a
    // handful of hardcoded ids -- see uicoopa/widgets/inventory_grid.h.
    InventoryItem key; key.id = "iron_key"; key.name = "Iron Key"; key.count = 1; key.max_stack = 1;
    key.tooltip = "Opens the old storage vault."; key.color = {0.80f, 0.84f, 0.92f, 0.95f}; items.push_back(key);
    InventoryItem potion; potion.id = "health_potion"; potion.name = "Health Potion"; potion.count = 12;
    potion.max_stack = 64; potion.tooltip = "Restores 50 HP."; potion.color = {0.92f, 0.28f, 0.32f, 0.95f}; items.push_back(potion);
    InventoryItem crystal; crystal.id = "mana_crystal"; crystal.name = "Mana Crystal"; crystal.count = 6;
    crystal.max_stack = 64; crystal.tooltip = "Restores 50 MP."; crystal.color = {0.25f, 0.55f, 0.98f, 0.95f}; items.push_back(crystal);
    InventoryItem gold; gold.id = "gold_pouch"; gold.name = "Gold Pouch"; gold.count = 128;
    gold.max_stack = 999; gold.color = {0.98f, 0.82f, 0.22f, 0.95f}; items.push_back(gold);
    InventoryItem map; map.id = "ancient_map"; map.name = "Ancient Map"; map.count = 1;
    map.max_stack = 1; map.tooltip = "Marks a distant, unexplored region."; map.color = {0.90f, 0.20f, 0.55f, 0.95f}; items.push_back(map);
    body.set_items(grid, items);  // UIBuilder facade for detail::set_items() -- no detail:: reach-through needed.

    // Demonstrates theme.slot.selected: tint slot 0's border to show it "selected".
    if (auto* slot_obj = grid->owner->find_descendant("Slot_0")) {
        if (auto* slot = slot_obj->get_component<InventorySlot>()) {
            if (slot->border_image) slot->border_image->color = theme.slot.selected;
        }
    }

    rows["Footer"].add_paragraph("Slots: 20  |  Capacity: 5/20", theme.text.size_small,
                                 theme.text.secondary, {292.0f, 20.0f}, "InvFooter");
}

/** @brief Every default icon name from uicoopa/tools/gen_default_icons.py, for the
 *         ActionPanel's icon strip below -- visual proof that IconLibrary/SpriteSheet
 *         actually resolved and rendered the generated sheet (assets/icons/icons.png)
 *         correctly (orientation, tinting, no bleed between cells). */
constexpr const char* kDefaultIconNames[] = {
    "arrow_left", "arrow_right", "arrow_up", "arrow_down",
    "chevron_left", "chevron_right", "chevron_up", "chevron_down",
    "caret_up", "caret_down", "check", "cross", "plus", "minus", "dot", "circle",
    "gear", "search", "menu", "star", "warning", "info", "lock", "folder",
};

/**
 * @brief Attaches an Animator to InfoBanner: a looping idle float/breathe/sway,
 *        and a one-shot alert flash the action buttons crossfade into.
 *
 * Targets are chosen to be layout-group-proof: InfoBanner and InventoryCard
 * are both direct Canvas children placed via explicit at(AnchorPreset, ...),
 * never inside a HorizontalLayoutGroup/VerticalLayoutGroup/GridLayoutGroup,
 * so even anchored_position is safe here (see
 * uicoopa/ui_yaml.h's register_ui_animated_properties() doc for the
 * layout-group caveat that makes this choice matter for other objects).
 * InfoBanner's Image carries neither a Button nor a ColorOnSignal, so its
 * color is uncontested — animating it here can't race any other per-frame
 * writer.
 *
 * @param canvas_obj The root Canvas object, searched for InfoBanner/InventoryCard by name.
 * @param theme      Supplies the alert flash's warning/panel colors, so it matches whichever
 *                    theme (THEME=dark|light) is active.
 * @return The attached Animator, or nullptr if InfoBanner wasn't found.
 */
Animator* build_animations(SceneObject* canvas_obj, const UITheme& theme) {
    auto* banner = canvas_obj->find_descendant("InfoBanner");
    if (!banner) return nullptr;
    bool has_inventory_card = canvas_obj->find_descendant("InventoryCard") != nullptr;

    auto idle = std::make_shared<AnimationClip>();
    idle->name = "banner_idle";
    idle->wrap = WrapMode::Loop;
    idle->set_explicit_length(2.4f);

    // InfoBanner's own anchored_position.y: floats up 14px and back.
    AnimationTrack float_track;
    float_track.component_type = "RectTransform";
    float_track.property = "anchored_position.y";
    float_track.curve.add_key({0.0f, {-20.0f, 0.0f, 0.0f, 1.0f}, Interpolation::EaseInOut});
    float_track.curve.add_key({1.2f, {-34.0f, 0.0f, 0.0f, 1.0f}, Interpolation::EaseInOut});
    float_track.curve.add_key({2.4f, {-20.0f, 0.0f, 0.0f, 1.0f}, Interpolation::EaseInOut});
    float_track.curve.sort_keys();
    idle->tracks.push_back(float_track);

    // InfoBanner's own Image.color alpha: a subtle breathe.
    AnimationTrack breathe_track;
    breathe_track.component_type = "Image";
    breathe_track.property = "color.a";
    breathe_track.curve.add_key({0.0f, {1.00f, 0.0f, 0.0f, 1.0f}, Interpolation::EaseInOut});
    breathe_track.curve.add_key({1.2f, {0.85f, 0.0f, 0.0f, 1.0f}, Interpolation::EaseInOut});
    breathe_track.curve.add_key({2.4f, {1.00f, 0.0f, 0.0f, 1.0f}, Interpolation::EaseInOut});
    breathe_track.curve.sort_keys();
    idle->tracks.push_back(breathe_track);

    // InventoryCard is a SIBLING of InfoBanner (both direct Canvas children), not a
    // descendant of it — this track exercises Animator's scene-wide fallback
    // resolution (owner->find_descendant() fails since it's not a descendant of
    // InfoBanner, so it falls through to Scene::find_object()).
    if (has_inventory_card) {
        AnimationTrack sway_track;
        sway_track.object_path = "InventoryCard";
        sway_track.component_type = "RectTransform";
        sway_track.property = "rotation";
        sway_track.curve.add_key({0.0f, {-1.5f, 0.0f, 0.0f, 1.0f}, Interpolation::EaseInOut});
        sway_track.curve.add_key({1.2f, {1.5f, 0.0f, 0.0f, 1.0f}, Interpolation::EaseInOut});
        sway_track.curve.add_key({2.4f, {-1.5f, 0.0f, 0.0f, 1.0f}, Interpolation::EaseInOut});
        sway_track.curve.sort_keys();
        idle->tracks.push_back(sway_track);
    }

    auto alert = std::make_shared<AnimationClip>();
    alert->name = "banner_alert";
    alert->wrap = WrapMode::Once;
    alert->set_explicit_length(0.8f);

    AnimationTrack scale_track;
    scale_track.component_type = "RectTransform";
    scale_track.property = "scale";
    scale_track.curve.add_key({0.00f, {1.00f, 1.00f, 0.0f, 1.0f}, Interpolation::EaseOut});
    scale_track.curve.add_key({0.18f, {1.05f, 1.05f, 0.0f, 1.0f}, Interpolation::EaseOut});
    scale_track.curve.add_key({0.80f, {1.00f, 1.00f, 0.0f, 1.0f}, Interpolation::EaseOut});
    scale_track.curve.sort_keys();
    alert->tracks.push_back(scale_track);

    AnimationTrack flash_track;
    flash_track.component_type = "Image";
    flash_track.property = "color";
    const glm::vec4& base = theme.panel.panel_alt;
    const glm::vec4& warn = theme.text.warning;
    flash_track.curve.add_key({0.0f, {warn.r, warn.g, warn.b, 1.0f}, Interpolation::EaseOut});
    flash_track.curve.add_key({0.8f, {base.r, base.g, base.b, 1.0f}, Interpolation::EaseOut});
    flash_track.curve.sort_keys();
    alert->tracks.push_back(flash_track);

    auto* animator = banner->add_component<Animator>();
    animator->add_state("idle", idle);
    animator->add_state("alert", alert);
    animator->auto_play = "idle";
    animator->default_crossfade = 0.12f;
    // Drift back into the idle loop once the alert flash finishes playing.
    animator->on_state_finished.connect([animator](const std::string& finished_state) {
        if (finished_state == "alert") animator->crossfade("idle", 0.25f);
    });
    return animator;
}

/**
 * @brief Builds the ActionPanel: an icon strip, three role-styled buttons that mutate
 *        the settings panel live, and a footer status line -- plus a Modal confirm
 *        dialog the Reset button opens instead of resetting immediately.
 *
 * The icon strip / button row / footer used to be two hand-positioned
 * horizontal_layout() children (`at(TopLeft, {20,-6}, {720,32})` then
 * `{20,-46},{720,40}`, with widths restated as `760 - 2*20` literals) plus a free-
 * positioned footer paragraph. split_rows() replaces all three with one call, and
 * add_icon_row()/add_action_bar() replace their respective hand-rolled loops.
 */
void build_action_panel(UIBuilder root, UIBuilder settings, Animator* banner_anim) {
    const UITheme& theme = root.theme();
    UIBuilder body = root.card("ActionPanel", "Inspector Actions & Config Management",
                              AnchorPreset::TopLeft, {500.0f, -550.0f}, {760.0f, 150.0f});

    SectionSet rows = body.split_rows({
        {"IconStrip", 1.0f, 32.0f, SectionFlow::None},
        {"Buttons",   1.0f, 40.0f, SectionFlow::None},
        {"Footer",    1.0f, 0.0f,  SectionFlow::None},  // size 0 -> weighted, fills what's left.
    }, 8.0f, LayoutPadding{20.0f, 20.0f, 10.0f, 10.0f});

    // add_icon_row() needs no IconLibrary::has_icons() guard -- add_icon() already
    // draws nothing (and creates nothing) for an unresolved name, see its own doc.
    std::vector<std::string> icon_names(kDefaultIconNames,
                                        kDefaultIconNames + sizeof(kDefaultIconNames) / sizeof(kDefaultIconNames[0]));
    rows["IconStrip"].add_icon_row(icon_names, 22.0f, theme.text.accent, 6.0f, "Icons");

    Text* footer = rows["Footer"].add_paragraph("Ready.", theme.text.size_small, theme.text.secondary,
                                                {700.0f, 20.0f}, "FooterStatus");

    // A Modal confirm dialog for the destructive Reset action. Built (and left) active
    // -- see DialogMode::Modal's doc -- until root's ancestor Scene::start() call
    // (in main(), below) applies its initial closed state via its Dialog component.
    DialogHandle confirm = root.dialog("ResetConfirm", "Reset to Defaults?",
                                       {360.0f, 170.0f}, DialogMode::Modal);
    confirm.body().add_paragraph(
        "This resets vsync, master volume, difficulty, and autosave\nback to their default values.",
        theme.text.size_small, theme.text.secondary, {320.0f, 40.0f}, "ResetConfirmBody");
    confirm.add_action("Cancel", ButtonRole::Neutral, [confirm]() { confirm.hide(); });
    confirm.add_action("Reset", ButtonRole::Primary, [confirm, settings, footer]() {
        UIBuilder s = settings;  // a fresh, non-const copy -- set_value() isn't a const method.
        s.set_value("vsync", true);
        s.set_value("master_volume", 0.8f);
        s.set_value<int>("difficulty", 1);
        s.set_value("autosave", true);
        footer->text = "Reset to defaults.";
        confirm.hide();
    });

    rows["Buttons"].add_action_bar({
        {"Apply Settings", ButtonRole::Primary, [footer, banner_anim]() {
            footer->text = "Settings applied.";
            if (banner_anim) banner_anim->crossfade("alert", banner_anim->default_crossfade);
        }},
        {"Reset to Defaults", ButtonRole::Neutral, [confirm]() { confirm.show(); }},
        {"Save Profile", ButtonRole::Success, [footer]() { footer->text = "Profile saved."; }},
    }, 16.0f, "ButtonsRow");
}

}  // namespace

int main() {
    std::cout << "==========================================================\n";
    std::cout << "  uicoopa_settings_builder\n";
    std::cout << "  The former settings_demo scene, built entirely through\n";
    std::cout << "  UIBuilder -- no scene YAML except the theme file. Switch\n";
    std::cout << "  settings tabs, drag inventory items, click the action\n";
    std::cout << "  buttons -- Reset opens a confirm dialog. Set THEME=light\n";
    std::cout << "  to try the light theme.\n";
    std::cout << "  Press ESC to quit.\n";
    std::cout << "==========================================================\n\n";

    coopa::gfx::app::ContextConfig config = coopa::gfx::app::ContextConfig::from_env(
        coopa::gfx::app::ContextConfig{
            .title = "uicoopa settings_builder", .width = 1280, .height = 720, .resizable = true,
        });
#ifdef NDEBUG
    config.validation = false;
#endif
    coopa::gfx::app::Context ctx(config);

    std::string shader_dir = std::string(ROOT_DIR) + "/assets/shaders";
    UiPass ui_pass(ctx.device(), ctx.allocator(), ctx.command_pool(), ctx.render_pass(),
                  shader_dir + "/ui.vert.spv", shader_dir + "/ui_quad.frag.spv",
                  shader_dir + "/ui_text.frag.spv");

    // Declared after ctx so it (and every AssetHandle/Texture it owns) is destroyed
    // before ctx's Device/Allocator -- see IconLibrary::clear()'s doc for the
    // explicit teardown call this pairs with, near the end of main().
    //
    // jobs is shared with AssetManager below so icon/sprite decode runs on a JobEngine
    // the app actually owns instead of AssetManager's private 2-thread fallback pool (see
    // AssetManager's ctor doc) -- separate from the "no set_job_engine() call" decision
    // further down for this demo's AnimationSystem, which is about per-frame animator
    // dispatch, not asset decode.
    coopa::job::JobEngine jobs;
    coopa::asset::AssetManager assets(&jobs);
    assets.add_search_root(std::string(ROOT_DIR) + "/assets");
    IconLibrary::instance().configure(assets, ctx.device(), ctx.allocator(), ctx.command_pool());
    IconLibrary::instance().add_sheet(assets, "icons/icons.yaml");
    IconLibrary::instance().add_sheet(assets, "icons/cursors.yaml");

    // Loads the UI font once and installs it as the process-wide fallback, so
    // every apply_font() call in the builder (which every widget factory
    // funnels through) picks it up with no per-call plumbing -- see
    // uicoopa/text/font_defaults.h.
    Font ui_font(ctx.device(), ctx.allocator(), ctx.command_pool(),
                std::string(ROOT_DIR) + "/assets/fonts/DejaVuSans.ttf");
    FontDefaults::font = &ui_font;
    FontDefaults::note_text_size = [](Font* f, uint32_t sz) {
        UIResourceCache::instance().note_text_atlas_use(f, sz);
    };
    // Lets ThemeLibrary's per-role fonts (title/heading/body/label/caption/numeric,
    // and the legacy font_path) actually load -- see ui_theme_yaml.h's
    // resolve_theme_fonts(), called from load() below. configure() is needed even
    // though this demo never calls register_ui_components() (it builds UI
    // imperatively): font_for_path() no-ops without a Device/Allocator/CommandPool.
    UIResourceCache::instance().configure(ctx.device(), ctx.allocator(), ctx.command_pool());
    FontDefaults::resolve_font = [](const std::string& path) {
        return UIResourceCache::instance().font_for_path(path);
    };

    // The UI always has at least one theme loaded (see ThemeLibrary::active()'s
    // doc): even if assets/themes/ were missing, this call would fall back to
    // UITheme::builtin_dark() and just warn once.
    ThemeLibrary::instance().set_search_dir(std::string(ROOT_DIR) + "/assets/themes");
    const char* theme_env = std::getenv("THEME");
    const UITheme& theme = ThemeLibrary::instance().load(theme_env && theme_env[0] ? theme_env : "dark");
    ThemeLibrary::instance().set_active(theme);

#ifdef UICOOPA_HAS_AUDIO
    // Declared before `scene` (and set active before scene.start() runs the
    // UiSoundPlayer attached to the Canvas below) so it outlives every
    // component that might touch it -- same lifetime discipline this file
    // already applies to `assets` vs `ctx`. UI_AUDIO=0 skips this whole
    // block: no device opened, no sounds loaded, UiSoundPlayer's signal
    // bindings become harmless no-ops (UiAudio::active() stays nullptr).
    const char* ui_audio_env = std::getenv("UI_AUDIO");
    const bool audio_enabled = !(ui_audio_env && std::string(ui_audio_env) == "0");
    std::unique_ptr<UiAudio> audio;
    if (audio_enabled) {
        SoundLibrary::instance().set_search_dir(std::string(ROOT_DIR) + "/assets/sounds");
        SoundLibrary::instance().load_manifest();
        audio = std::make_unique<UiAudio>();
        UiAudio::set_active(audio.get());
    }
#endif

    // --- Build the entire UI tree via UIBuilder -- no scene YAML. ---
    Scene scene("settings_builder_demo");
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ScaleWithScreenSize;
    canvas->scaler.reference_resolution = {1280.0f, 720.0f};
    canvas->scaler.match_width_or_height = 0.5f;

#ifdef UICOOPA_HAS_AUDIO
    // One UiSoundPlayer on the Canvas gives every Button in this demo (all 24
    // icon-strip buttons, the 3 action buttons, every combobox header, ...)
    // hover and click sounds -- it listens on the scene EventBus's wildcard
    // tier (see button.h's "hover_enter"/"click" emissions), so no individual
    // widget needs to know audio exists at all.
    if (audio_enabled) canvas_obj->add_component<UiSoundPlayer>();
#endif

    UIBuilder root(canvas_obj.get(), &theme);
    root.panel("Background")->get_component<Image>()->color = theme.panel.background;

    UIBuilder settings = build_settings_panel(root);
    build_info_banner(root);
    StatusReadouts status = build_status_card(root);
    build_inventory_card(root);
    Animator* banner_anim = build_animations(canvas_obj.get(), theme);
    build_action_panel(root, settings, banner_anim);

    // Replaces the OS pointer with a themed, auto-switching cursor sprite --
    // must run before canvas_obj is moved into the scene below (add_child()
    // above attaches "Cursor" directly to it while it's still ours to reach).
    root.enable_cursor(ctx.input());

    // Makes RectTransform/Graphic fields animatable by name (see
    // coopa::anim::AnimatedPropertyRegistry) -- this demo builds its UI
    // imperatively via UIBuilder and never calls register_ui_components(),
    // so it must call this directly rather than getting it automatically.
    coopa::ui::register_ui_animated_properties();

    SceneObject* canvas_raw = scene.add_root_object(std::move(canvas_obj));

    // Drives banner_anim (and any other Animator added to this scene) at
    // coopa::scene::UpdatePhase::Animation, which runs inside scene.update()
    // below, before scene.late_update()'s CanvasComponent layout pass — so
    // Canvas always measures/arranges/emits the current frame's already-
    // animated pose. No set_job_engine() call: this demo's handful of tracks
    // isn't remotely enough to make job dispatch worth it (see
    // AnimationSystem::set_parallel_threshold()'s doc), so this stays inline.
    scene.add_system(std::make_unique<coopa::anim::AnimationSystem>(), coopa::scene::UpdatePhase::Animation);

    // Runs every widget's start() (SpinBox/ComboBox already start()ed during
    // construction above) -- also the single point where SettingsTabs' TabView and
    // ResetConfirm's Dialog apply their initial visibility (page 0 selected, dialog
    // closed): both are built (and left) active so this one call reaches every row/
    // action added to them above, however deeply nested or currently hidden -- see
    // widgets/tab_view.h's TabView::start() and widgets/dialog.h's Dialog::start().
    scene.start();
    canvas->set_default_texture(ui_pass.white_view());
    UIResourceCache::instance().mark_text_atlases(ui_pass);

    // ANIM_TIME=<seconds> freezes banner_anim's "idle" pose at an exact time
    // via sample_at() (seek + apply, no signals fired) before the render loop
    // starts, so a screenshot is reproducible regardless of real frame timing.
    // stop() immediately after is what makes it STAY frozen: MAX_FRAMES>1 still
    // runs several real frames (with real, run-to-run-varying wall-clock dt)
    // before the screenshot is saved, and a still-playing Animator would drift
    // by a small, nondeterministic amount over those frames. A stopped
    // Animator's AnimationSystem pass still re-applies its last sample_time
    // every frame (advance_() is a no-op when not playing_, but evaluate_()/
    // apply_() always run), so the seeded pose holds exactly.
    if (banner_anim) {
        if (const char* anim_time_env = std::getenv("ANIM_TIME")) {
            banner_anim->sample_at(std::stof(anim_time_env));
            banner_anim->stop();
        }
    }

    // CURSOR_POS=<x>,<y> pins the (now OS-hidden, see enable_cursor() above) real
    // pointer to a fixed window-pixel position, so a screenshot can show the themed
    // cursor overlay reacting to a known, reproducible hover target (e.g. a button)
    // instead of wherever the test happened to launch. Re-applied every frame (see
    // the render loop below, right after ctx.poll()) rather than once here -- some
    // windowing backends re-report a real (unmoving) pointer position on every
    // poll(), which would otherwise silently undo a one-shot warp before the first
    // frame even renders.
    bool has_cursor_pos = false;
    float cursor_pos_x = 0.0f, cursor_pos_y = 0.0f;
    if (const char* cursor_pos_env = std::getenv("CURSOR_POS")) {
        has_cursor_pos = std::sscanf(cursor_pos_env, "%f,%f", &cursor_pos_x, &cursor_pos_y) == 2;
    }

    // SELECT_TAB=<index> picks a page of the settings tab bar for the screenshot --
    // the tab-bar counterpart to test_window.cpp's OPEN_DIALOG=1.
    if (const char* select_tab_env = std::getenv("SELECT_TAB")) {
        if (auto* tabs_obj = canvas_raw->find_descendant("SettingsTabs")) {
            if (auto* tabs = tabs_obj->get_component<TabView>()) tabs->select(std::atoi(select_tab_env));
        }
    }
    // OPEN_DIALOG=<name> force-opens a built dialog (e.g. "ResetConfirm") regardless
    // of the closed state scene.start() just applied to it -- test_window.cpp's
    // OPEN_DIALOG=1 does the same for its single hand-authored YAML dialog; this one
    // is name-addressed since a UIBuilder demo may build more than one.
    if (const char* open_dialog_env = std::getenv("OPEN_DIALOG")) {
        if (auto* dialog_obj = canvas_raw->find_descendant(open_dialog_env)) {
            dialog_obj->set_active(true);
        }
    }
    if (const char* combo_env = std::getenv("OPEN_COMBO")) {
        if (auto* combo_obj = canvas_raw->find_descendant(combo_env)) {
            if (auto* combo = combo_obj->get_component<ComboBox>()) combo->show_popup();
        }
    }
    if (const char* slider_env = std::getenv("SLIDER_MAX")) {
        if (auto* slider_obj = canvas_raw->find_descendant(slider_env)) {
            if (auto* slider = slider_obj->get_component<Slider>()) slider->set_value(slider->max_value, false);
        }
    }
    if (const char* edit_field_env = std::getenv("EDIT_FIELD")) {
        if (auto* field_obj = canvas_raw->find_descendant(edit_field_env)) {
            if (auto* field = field_obj->get_component<TextField>()) {
                auto [init_w, init_h] = ctx.window().framebuffer_size();
                canvas->set_viewport(init_w, init_h);
                canvas->rebuild_layout(init_w, init_h);
                if (auto* value_rt = field->label_text->owner->get_component<RectTransform>()) {
                    PointerEventData synth;
                    synth.position = value_rt->rect().center();
                    field->on_pointer_double_click(synth);
                }
                // SELECT_LEFT=<n> additionally simulates n Shift+Left presses after
                // opening the field, for screenshotting the selection highlight.
                if (const char* select_env = std::getenv("SELECT_LEFT")) {
                    int n = std::atoi(select_env);
                    coopa::input::KeyEvent shift_left{
                        coopa::input::Key::Left, 0, coopa::input::KeyAction::Press,
                        coopa::input::Mods::Shift};
                    for (int i = 0; i < n; ++i) field->on_key(shift_left);
                }
            }
        }
    }
    if (const char* hover_slot_env = std::getenv("HOVER_SLOT")) {
        if (auto* slot_obj = canvas_raw->find_descendant(hover_slot_env)) {
            if (auto* slot = slot_obj->get_component<InventorySlot>()) {
                auto [init_w, init_h] = ctx.window().framebuffer_size();
                canvas->set_viewport(init_w, init_h);
                canvas->rebuild_layout(init_w, init_h);
                if (auto* rt = slot_obj->get_component<RectTransform>()) {
                    PointerEventData synth;
                    synth.position = rt->rect().center();
                    slot->on_pointer_enter(synth);
                }
            }
        }
    }

    while (!ctx.should_close()) {
        ctx.poll();
        if (has_cursor_pos) ctx.input().set_cursor_position(cursor_pos_x, cursor_pos_y);

        if (ctx.input().key_down(coopa::input::Key::Escape)) {
            ctx.window().set_should_close(true);
        }

        float dt = ctx.delta_time();
        auto [sw, sh] = ctx.window().framebuffer_size();
        if (sw == 0 || sh == 0) continue;  // minimized

        canvas->set_viewport(sw, sh);
        canvas->set_input(ctx.input());

        // Before scene.update()/register_textures() -- finalize_typed() (the only GPU
        // touch a SpriteSheet load makes) runs inside this call, outside any render
        // pass, satisfying register_textures()'s "before begin_frame()" contract by
        // construction. Only matters for a sheet added later via load_async(); the
        // startup add_sheet() call above already completed synchronously.
        assets.update(dt);

#ifdef UICOOPA_HAS_AUDIO
        if (audio) audio->update(dt);
#endif

        refresh_status_card(settings, theme, status);

        scene.update(dt);
        scene.late_update(dt);

        // Re-marks every (font, size) atlas noted so far as text (R8 coverage), not just
        // the ones seen before the initial build -- needed because InventoryGrid's hover
        // tooltip (uicoopa/widgets/inventory_grid.h) creates its Text lazily on first
        // hover, at a font size (12px) nothing else in this demo uses, so its atlas
        // wouldn't otherwise get marked before mark_text_atlases()'s one call in main()
        // ran. Idempotent (a std::set insert) and cheap for this demo's handful of sizes.
        UIResourceCache::instance().mark_text_atlases(ui_pass);

        ui_pass.register_textures(canvas->draw_list());
        ui_pass.begin_frame(ctx.current_frame(), canvas->draw_list().vertices().size(),
                            canvas->draw_list().indices().size());

        coopa::gfx::app::FrameCallbacks cb;
        cb.record = [&](coopa::gfx::command::CommandBuffer& cmd) {
            ui_pass.draw(cmd, ctx.current_frame(), sw, sh, canvas->scale_factor(), canvas->draw_list());
        };
        cb.clear = coopa::gfx::ClearColor{ 0.05f, 0.05f, 0.07f, 1.0f };
        ctx.frame(cb);

        if (config.headless_oneshot) break;
        if (ctx.max_frames() > 0 && ctx.frame_index() >= ctx.max_frames()) break;
    }

    ctx.wait_idle();

    auto [final_w, final_h] = ctx.window().framebuffer_size();
    const char* out_env = std::getenv("SCREENSHOT_NAME");
    std::string shot_name = (out_env && out_env[0] != '\0') ? out_env : "settings_builder";
    std::string screenshot_path = std::string(ROOT_DIR) + "/output/" + shot_name + ".png";
    save_screenshot(ctx, ui_pass, canvas->draw_list(), final_w, final_h, canvas->scale_factor(), screenshot_path);

    std::cout << "[settings_builder] Exiting cleanly.\n";

#ifdef UICOOPA_HAS_AUDIO
    // UiAudio::active() must stop pointing at `audio` before it's destroyed below --
    // `scene`'s SceneObjects (specifically the Canvas's UiSoundPlayer) still exist at
    // this point but nothing signals after this line, so clearing the pointer here
    // rather than after `scene`'s destruction is enough to avoid a dangling static.
    UiAudio::set_active(nullptr);
    audio.reset();
    SoundLibrary::instance().clear();
#endif

    // Must precede assets going out of scope below (whose destructor calls
    // shutdown(), destroying every SpriteSheet's Texture) and the Device/Allocator
    // ctx owns -- see IconLibrary::clear()'s doc.
    IconLibrary::instance().clear();
    // ThemeLibrary first: its cached UITheme::font/FontRoleStyle::font pointers are
    // non-owning references into UIResourceCache's fonts_by_path_ -- clearing that
    // cache first would leave them dangling for whatever instant separates the two
    // calls (harmless today since nothing renders between them, but this ordering
    // is the one that's never wrong).
    ThemeLibrary::instance().clear();
    UIResourceCache::instance().clear();
    // FontDefaults::font points at ui_font, a local about to go out of scope --
    // drop the reference now rather than leave a dangling static past this
    // function's end (mirrors the explicit clear() calls above).
    FontDefaults::font = nullptr;
    FontDefaults::note_text_size = nullptr;
    FontDefaults::resolve_font = nullptr;

    // Restores the OS pointer enable_cursor() hid -- courtesy cleanup, not
    // load-bearing (the process is exiting either way).
    ctx.input().set_cursor_mode(coopa::input::CursorMode::Normal);

    return 0;
}
