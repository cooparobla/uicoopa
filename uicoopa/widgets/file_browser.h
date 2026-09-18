/**
 * @file file_browser.h
 * @brief FileBrowser: the directory listing, navigation and selection behind a file dialog.
 */

#ifndef UICOOPA_WIDGETS_FILE_BROWSER_H
#define UICOOPA_WIDGETS_FILE_BROWSER_H

#include <uicoopa/ui_component.h>
#include <uicoopa/widgets/button.h>
#include <uicoopa/widgets/dialog.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/scrollbar.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/widgets/text_field.h>
#include <coopa/event/signal.h>
#include <coopa/scene/scene_object.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace coopa {
namespace ui {

/**
 * @enum FileDialogMode
 * @brief Whether a FileBrowser is picking something that exists or naming something new.
 */
enum class FileDialogMode {
    Open, /**< @brief Confirm requires an existing file. */
    Save  /**< @brief Confirm accepts a typed name that need not exist yet. */
};

/**
 * @struct FileEntry
 * @brief One row of a listed directory.
 */
struct FileEntry {
    std::string name;             /**< @brief Leaf name, or ".." for the parent link. */
    bool        is_directory = false;
};

/**
 * @struct FileRow
 * @brief One reusable row widget in FileBrowser's fixed pool. See that class's doc.
 */
struct FileRow {
    coopa::scene::SceneObject* node = nullptr;  /**< @brief Non-owning; toggled, never destroyed. */
    Button* button = nullptr;                   /**< @brief Non-owning; wired once, at build time. */
    Text*   label = nullptr;                    /**< @brief Non-owning; re-labelled per page. */
    Image*  icon = nullptr;                     /**< @brief Non-owning; folder glyph, hidden for files. */
};

/**
 * @class FileBrowser
 * @brief Lists a directory, navigates it, and reports the confirmed path.
 *
 * Built by UIBuilder::file_dialog() (builder/detail/file_dialog.h) onto the same node as a
 * Dialog, which supplies the scrim, the ModalContext input blocking and the open/close
 * choke point. This component is only the file half.
 *
 * ## Why the rows are a fixed pool rather than rebuilt per directory
 *
 * The obvious implementation -- destroy the old row nodes, build one per entry -- is a
 * use-after-free in this scene graph, and not a theoretical one. Navigation is triggered
 * from a row Button's on_click, which runs inside EventSystem::dispatch_chain_(), which is
 * iterating `obj->components()` **by reference** over the objects in the live hit chain --
 * and that chain contains the row node. Destroying it there frees the Button under its own
 * running lambda, frees the vector being iterated, and leaves EventSystem's hovered_object_
 * and press_chain_ dangling.
 *
 * (Adding children mid-dispatch is fine -- EventSystem never walks children() and the
 * raycast has already run. Only destruction is forbidden.)
 *
 * So the pool is built once, never grown and never destroyed, and a page of entries is
 * painted onto it: apply_page_() writes label text, an icon's active flag, and each row's
 * own active flag. Those are pure string and bool writes, safe to perform from inside a
 * click handler. A Scrollbar slides the window over a directory of any size, so the
 * SceneObject count is bounded by `rows.size()` no matter how many files are listed.
 *
 * Every filesystem call uses the std::error_code overloads: an unreadable directory
 * produces an empty listing, never an exception thrown out of a click handler.
 */
class FileBrowser : public UIComponent {
public:
    /** @brief Fires with the absolute confirmed path. */
    using PathSignal = coopa::event::Signal<const std::string&>;

    std::string type_name() const override { return "FileBrowser"; }

    FileDialogMode mode = FileDialogMode::Open;

    /**
     * @brief Lowercase, dot-included suffixes ({".yaml", ".yml"}). Empty shows every file.
     *
     * Applies to files only -- directories are never filtered, or the browser could not be
     * navigated to where the matching files are.
     */
    std::vector<std::string> extensions;

    bool show_hidden = false;  /**< @brief Whether to list dot-files. Never hides "..". */

    std::vector<FileRow> rows;           /**< @brief The fixed pool; see the class doc. */
    Text*      path_label = nullptr;     /**< @brief Non-owning; shows the current directory. */
    TextField* name_field = nullptr;     /**< @brief Non-owning; the filename being confirmed. */
    Scrollbar* scrollbar = nullptr;      /**< @brief Non-owning; slides the page window. */
    Dialog*    dialog = nullptr;         /**< @brief Non-owning; resolved in start() from the same node. */
    Button*    confirm_button = nullptr; /**< @brief Non-owning; disabled while the selection is invalid. */

    PathSignal             on_confirm;
    coopa::event::Signal<> on_cancel;

    /**
     * @brief Shows the dialog listing `directory`.
     * @param directory Where to browse; empty keeps the last directory, or the process
     *                  working directory on the first open. A path that does not exist
     *                  falls back the same way.
     * @param suggested_name Prefills the filename field; ignored when empty.
     */
    void open(const std::string& directory = "", const std::string& suggested_name = "") {
        std::error_code ec;
        std::filesystem::path target = directory.empty()
            ? current_dir_
            : std::filesystem::path(directory);
        if (target.empty() || !std::filesystem::is_directory(target, ec)) {
            target = std::filesystem::current_path(ec);
        }
        current_dir_ = target;
        if (name_field && !suggested_name.empty()) name_field->set_text(suggested_name, false);
        selected_ = -1;
        scroll_offset_ = 0;
        refresh();
        if (dialog) dialog->open();
    }

    /** @brief Hides the dialog without emitting anything. */
    void close() { if (dialog) dialog->close(); }

    /** @brief Whether the dialog is showing. */
    bool is_open() const { return dialog && dialog->is_open(); }

    /**
     * @brief Re-reads the current directory from disk and repaints the row pool.
     *
     * Safe to call from inside a row Button's on_click: it only writes strings and active
     * flags, and never creates or destroys a SceneObject. See the class doc.
     */
    void refresh() {
        entries_.clear();

        std::error_code ec;
        if (current_dir_.has_parent_path() && current_dir_.parent_path() != current_dir_) {
            entries_.push_back(FileEntry{"..", true});
        }

        std::vector<FileEntry> dirs;
        std::vector<FileEntry> files;
        for (std::filesystem::directory_iterator it(current_dir_, ec), end; !ec && it != end;
             it.increment(ec)) {
            const std::string leaf = it->path().filename().string();
            if (leaf.empty()) continue;
            if (!show_hidden && leaf[0] == '.') continue;

            std::error_code dir_ec;
            if (std::filesystem::is_directory(it->path(), dir_ec)) {
                dirs.push_back(FileEntry{leaf, true});
            } else if (matches_filter_(leaf)) {
                files.push_back(FileEntry{leaf, false});
            }
        }

        auto by_name = [](const FileEntry& a, const FileEntry& b) { return a.name < b.name; };
        std::sort(dirs.begin(), dirs.end(), by_name);
        std::sort(files.begin(), files.end(), by_name);
        entries_.insert(entries_.end(), dirs.begin(), dirs.end());
        entries_.insert(entries_.end(), files.begin(), files.end());

        scroll_offset_ = std::clamp(scroll_offset_, 0, max_offset_());
        apply_page_();
    }

    /** @brief The directory currently listed. */
    std::string current_directory() const { return current_dir_.string(); }

    /**
     * @brief Lists `path`, which may be absolute or relative to the current directory.
     * @param path Where to go; a non-directory is ignored.
     */
    void navigate(const std::string& path) {
        std::filesystem::path target(path);
        if (target.is_relative()) target = current_dir_ / target;

        std::error_code ec;
        std::filesystem::path canonical = std::filesystem::weakly_canonical(target, ec);
        if (!ec && !canonical.empty()) target = canonical;
        if (!std::filesystem::is_directory(target, ec)) return;

        current_dir_ = target;
        selected_ = -1;
        scroll_offset_ = 0;
        refresh();
    }

    /** @brief Lists the parent directory, if there is one. */
    void go_up() { navigate(".."); }

    /** @brief The path confirm() would emit: the filename field resolved against the directory. */
    std::string current_path() const {
        std::string name = name_field ? name_field->text() : std::string{};
        if (name.empty()) return std::string{};
        std::filesystem::path p(name);
        if (p.is_relative()) p = current_dir_ / p;
        return p.string();
    }

    /**
     * @brief Emits on_confirm with the chosen path and closes, if the choice is valid.
     *
     * In Save mode a typed name with no extension gets `extensions.front()` appended, so
     * "world" becomes "world.yaml". In Open mode a path that does not exist is rejected:
     * nothing is emitted and the dialog stays open, which is the whole point of the mode.
     */
    void confirm() {
        std::string name = name_field ? name_field->text() : std::string{};
        if (name.empty()) return;

        std::filesystem::path p(name);
        if (p.is_relative()) p = current_dir_ / p;

        if (mode == FileDialogMode::Save && !p.has_extension() && !extensions.empty()) {
            p += extensions.front();
        }

        std::error_code ec;
        if (mode == FileDialogMode::Open && !std::filesystem::exists(p, ec)) return;

        close();
        on_confirm.emit(p.string());
    }

    /** @brief Emits on_cancel and closes. */
    void cancel() {
        close();
        on_cancel.emit();
    }

    /**
     * @brief Resolves the sibling Dialog, wires the scrollbar, and lists the first directory.
     *
     * Resolving `dialog` here rather than relying on component order makes this
     * order-independent: Dialog::start() may run before or after this one.
     */
    void start() override {
        if (owner && !dialog) dialog = owner->get_component<Dialog>();
        if (scrollbar) {
            scroll_conn_ = scrollbar->on_value_changed.connect([this](float v) {
                set_scroll_offset(static_cast<int>(v * static_cast<float>(max_offset_()) + 0.5f));
            });
        }
        std::error_code ec;
        if (current_dir_.empty()) current_dir_ = std::filesystem::current_path(ec);
        refresh();
    }

    /**
     * @brief Slides the visible window over the entry list.
     * @param offset Index of the first listed entry; clamped into range.
     */
    void set_scroll_offset(int offset) {
        int clamped = std::clamp(offset, 0, max_offset_());
        if (clamped == scroll_offset_) return;
        scroll_offset_ = clamped;
        apply_page_();
    }

    /** @brief How many entries the current directory listing holds. */
    std::size_t entry_count() const { return entries_.size(); }

    /**
     * @brief Acts on one pool slot, as its row Button's click does.
     *
     * A directory navigates into it; a file selects it and fills the filename field.
     * Public so a test can drive a row without synthesising a pointer event.
     *
     * @param slot Index into `rows`, not into the entry list.
     */
    void activate_row(int slot) {
        int index = scroll_offset_ + slot;
        if (slot < 0 || index < 0 || index >= static_cast<int>(entries_.size())) return;

        const FileEntry entry = entries_[static_cast<std::size_t>(index)];
        if (entry.is_directory) {
            navigate(entry.name);
            return;
        }
        selected_ = index;
        if (name_field) name_field->set_text(entry.name, false);
    }

private:
    /** @brief Whether `leaf` passes the extension filter. An empty filter passes everything. */
    bool matches_filter_(const std::string& leaf) const {
        if (extensions.empty()) return true;
        std::string lower = leaf;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (const std::string& ext : extensions) {
            if (lower.size() >= ext.size() &&
                lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0) {
                return true;
            }
        }
        return false;
    }

    /** @brief Largest first-entry index that still fills the pool. */
    int max_offset_() const {
        int extra = static_cast<int>(entries_.size()) - static_cast<int>(rows.size());
        return extra > 0 ? extra : 0;
    }

    /** @brief Paints the window [scroll_offset_, +rows.size()) onto the pool. */
    void apply_page_() {
        for (std::size_t slot = 0; slot < rows.size(); ++slot) {
            const std::size_t index = static_cast<std::size_t>(scroll_offset_) + slot;
            FileRow& row = rows[slot];
            const bool used = index < entries_.size();
            if (row.node) row.node->set_active(used);
            if (!used) continue;

            const FileEntry& entry = entries_[index];
            if (row.label) row.label->text = entry.name;
            // The icon's own node is toggled, not the Image, so the label's fixed left
            // inset stays put whether or not a folder glyph is showing.
            if (row.icon && row.icon->owner) row.icon->owner->set_active(entry.is_directory);
        }

        if (path_label) path_label->text = elide_path_(current_dir_.string());
    }

    /**
     * @brief Shortens a path to fit the readout, keeping the tail.
     *
     * Text has no ellipsis overflow mode (only Overflow/Wrap/Truncate), so the elision is
     * done on the string. Keeping the tail is right for a path anyway -- the directory you
     * are in matters more than the root it descends from.
     */
    static std::string elide_path_(const std::string& path) {
        constexpr std::size_t k_max = 56;
        if (path.size() <= k_max) return path;
        return ".../" + path.substr(path.size() - (k_max - 4));
    }

    std::vector<FileEntry> entries_;
    std::filesystem::path  current_dir_;
    int scroll_offset_ = 0;
    int selected_ = -1;
    coopa::event::ScopedConnection scroll_conn_;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_FILE_BROWSER_H
