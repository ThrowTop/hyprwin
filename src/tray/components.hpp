#pragma once
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <windows.h>

#include "assets.hpp"
namespace tray {
class BaseTray;

class TrayEntry {
  protected:
    std::wstring text;
    bool disabled = false;
    BaseTray* parent = nullptr;

    enum class GlyphKind { None, Bitmap, Icon };
    GlyphKind glyphKind = GlyphKind::None;

    Image glyphBitmapOwned;
    Icon glyphIcon;
    mutable Image glyphCache; // ...we lazily convert to a bitmap for menus
    mutable int cacheCx = 0;
    mutable int cacheCy = 0;

    bool defaultItem = false;

  public:
    explicit TrayEntry(std::wstring text) : text(std::move(text)) {}
    virtual ~TrayEntry() = default;

    BaseTray* getParent() {
        return parent;
    }
    virtual void setParent(BaseTray* newParent);
    const std::wstring& getText() const {
        return text;
    }
    void setText(std::wstring newText);
    void setDisabled(bool state);
    bool isDisabled() const {
        return disabled;
    }

    void setGlyphBitmap(Image hbmp);
    void setGlyphIcon(Icon ico);
    void setDefault(bool v);

    HBITMAP getOrBuildGlyphBitmap(int cx, int cy) const;
    bool isDefault() const {
        return defaultItem;
    }
};

template <typename T>
concept tray_entry = std::is_base_of_v<TrayEntry, T>;

class BaseTray {
  protected:
    Icon icon;
    std::wstring identifier;
    std::vector<std::shared_ptr<TrayEntry>> entries;

  public:
    BaseTray(std::wstring identifierIn, Icon iconIn) : icon(std::move(iconIn)), identifier(std::move(identifierIn)) {}
    virtual ~BaseTray() = default;

    template <tray_entry... Ts>
    void addEntries(const Ts&... es) {
        (addEntry(es), ...);
    }

    template <tray_entry T>
    std::shared_ptr<T> addEntry(const T& entry) {
        auto sp = std::make_shared<T>(entry);
        sp->setParent(this);
        entries.emplace_back(sp);
        update();
        return sp;
    }

    template <tray_entry T>
    std::shared_ptr<T> addEntry(T&& entry) {
        auto sp = std::make_shared<T>(std::move(entry));
        sp->setParent(this);
        entries.emplace_back(sp);
        update();
        return sp;
    }

    template <tray_entry T, typename... Args>
    std::shared_ptr<T> emplaceEntry(Args&&... args) {
        auto sp = std::make_shared<T>(std::forward<Args>(args)...);
        sp->setParent(this);
        entries.emplace_back(sp);
        update();
        return sp;
    }

    virtual void run() = 0;
    virtual void exit() = 0;
    virtual void update() = 0;

    const std::vector<std::shared_ptr<TrayEntry>>& getEntries() const {
        return entries;
    }
};

// deferred: requires complete BaseTray
inline void TrayEntry::setParent(BaseTray* newParent) {
    parent = newParent;
}
inline void TrayEntry::setText(std::wstring newText) {
    text = std::move(newText);
    if (parent)
        parent->update();
}
inline void TrayEntry::setDisabled(bool state) {
    disabled = state;
    if (parent)
        parent->update();
}
inline void TrayEntry::setGlyphBitmap(Image hbmp) {
    glyphKind = GlyphKind::Bitmap;
    glyphBitmapOwned = std::move(hbmp);
    glyphCache = Image();
    if (parent)
        parent->update();
}

inline void TrayEntry::setGlyphIcon(Icon ico) {
    glyphKind = GlyphKind::Icon;
    glyphIcon = std::move(ico);
    glyphCache = Image();
    if (parent)
        parent->update();
}

inline void TrayEntry::setDefault(bool v) {
    defaultItem = v;
    if (parent)
        parent->update();
}

inline HBITMAP TrayEntry::getOrBuildGlyphBitmap(int cx, int cy) const {
    if (glyphKind == GlyphKind::Bitmap)
        return glyphBitmapOwned.get();
    if (glyphKind == GlyphKind::Icon) {
        if (!glyphCache.get() || cacheCx != cx || cacheCy != cy) {
            glyphCache = Image(glyphIcon.toBitmap(cx, cy), OwnershipPolicy::Adopt);
            cacheCx = cx;
            cacheCy = cy;
        }
        return glyphCache.get();
    }
    return nullptr;
}

class Button : public TrayEntry {
    std::function<void()> callback;

  public:
    ~Button() override = default;
    Button(std::wstring text, std::function<void()> cb) : TrayEntry(std::move(text)), callback(std::move(cb)) {}
    void clicked() {
        if (callback)
            callback();
    }
    void setCallback(std::function<void()> cb) {
        callback = std::move(cb);
    }
};

class ImageButton : public Button {
    Image image;

  public:
    ~ImageButton() override = default;
    ImageButton(std::wstring text, Image img, std::function<void()> cb) : Button(std::move(text), std::move(cb)), image(std::move(img)) {
        // also expose as glyph so menu builder shows it
        this->setGlyphBitmap(image);
    }
    Image getImage() {
        return image;
    }
    void setImage(Image newImage) {
        image = std::move(newImage);
        this->setGlyphBitmap(image);
    }
};

class Label : public TrayEntry {
  public:
    explicit Label(std::wstring text) : TrayEntry(std::move(text)) {}
    ~Label() override = default;
};

class Separator : public TrayEntry {
  public:
    Separator() : TrayEntry(L"") {}
    ~Separator() override = default;
};
class Toggle : public TrayEntry {
    bool* toggled = nullptr; // synced, non-owning
    // Forced return: always return a string; empty string => don't change label
    std::function<std::wstring(bool&)> onToggle;

    HBITMAP hbmpChecked = nullptr;
    HBITMAP hbmpUnchecked = nullptr;

  public:
    ~Toggle() override = default;

    Toggle(std::wstring text, bool* state, std::function<std::wstring(bool&)> cb = {}) : TrayEntry(std::move(text)), toggled(state), onToggle(std::move(cb)) {}

    void onToggled() {
        if (!toggled)
            return;
        *toggled = !*toggled;

        if (onToggle) {
            std::wstring newText = onToggle(*toggled);
            if (!newText.empty()) {
                setText(std::move(newText));
            } else if (parent) {
                parent->update(); // still refresh check state
            }
        } else if (parent) {
            parent->update();
        }
    }

    bool isToggled() const {
        return toggled ? *toggled : false;
    }

    void setCheckBitmaps(HBITMAP checked, HBITMAP unchecked) {
        hbmpChecked = checked;
        hbmpUnchecked = unchecked;
        if (parent)
            parent->update();
    }
    HBITMAP getCheckedBitmap() const {
        return hbmpChecked;
    }
    HBITMAP getUncheckedBitmap() const {
        return hbmpUnchecked;
    }

    void setCallback(std::function<std::wstring(bool&)> cb) {
        onToggle = std::move(cb);
    }
};
class Submenu : public TrayEntry {
    std::vector<std::shared_ptr<TrayEntry>> children;

  public:
    explicit Submenu(std::wstring textIn) : TrayEntry(std::move(textIn)) {}
    ~Submenu() override = default;

    void setParent(BaseTray* newParent) override {
        TrayEntry::setParent(newParent);
        for (auto& child : children)
            child->setParent(newParent);
    }

    template <tray_entry... Ts>
    void addEntries(const Ts&... es) {
        (addEntry(es), ...);
    }

    template <tray_entry T>
    std::shared_ptr<T> addEntry(const T& entry) {
        auto sp = std::make_shared<T>(entry);
        sp->setParent(parent); // owning tray, not this Submenu
        children.emplace_back(sp);
        if (parent)
            parent->update();
        return sp;
    }

    template <tray_entry T>
    std::shared_ptr<T> addEntry(T&& entry) {
        auto sp = std::make_shared<T>(std::move(entry));
        sp->setParent(parent);
        children.emplace_back(sp);
        if (parent)
            parent->update();
        return sp;
    }

    template <tray_entry T, typename... Args>
    std::shared_ptr<T> emplaceEntry(Args&&... args) {
        auto sp = std::make_shared<T>(std::forward<Args>(args)...);
        sp->setParent(parent);
        children.emplace_back(sp);
        if (parent)
            parent->update();
        return sp;
    }

    void update() {
        if (parent)
            parent->update();
    }

    const std::vector<std::shared_ptr<TrayEntry>>& getEntries() const {
        return children;
    }
};
} // namespace tray
