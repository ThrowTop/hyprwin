#pragma once
#include <stdexcept>
#include <string>
#include <strsafe.h>
#include <windows.h>
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace tray {
enum class OwnershipPolicy {
    Copy,   // default: clone handle => wrapper owns clone
    Borrow, // non-owning view (wrapper never frees)
    Adopt   // take ownership of passed handle
};

class Icon {
    HICON m_hIcon = nullptr;
    bool m_owns = false;

    static HICON cloneIcon(HICON src) {
        if (!src)
            return nullptr;
        return CopyIcon(src);
    }

  public:
    Icon() = default;

    explicit Icon(HICON h, OwnershipPolicy p = OwnershipPolicy::Copy) {
        switch (p) {
            case OwnershipPolicy::Copy:
                m_hIcon = cloneIcon(h);
                m_owns = (m_hIcon != nullptr);
                break;
            case OwnershipPolicy::Borrow:
                m_hIcon = h;
                m_owns = false;
                break;
            case OwnershipPolicy::Adopt:
                m_hIcon = h;
                m_owns = true;
                break;
        }
    }

    explicit Icon(const std::wstring& path) {
        m_hIcon = reinterpret_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
        if (!m_hIcon)
            throw std::runtime_error("LoadImageW icon failed");
        m_owns = true;
    }
    explicit Icon(const wchar_t* path) : Icon(std::wstring(path)) {}

    // From resource (classic LoadIconW returns shared) => clone so we own
    explicit Icon(WORD resid) {
        HICON shared = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resid));
        if (!shared)
            throw std::runtime_error("LoadIconW resource failed");
        m_hIcon = cloneIcon(shared);
        if (!m_hIcon)
            throw std::runtime_error("CopyIcon failed");
        m_owns = true;
    }

    Icon(const Icon& rhs) {
        if (rhs.m_hIcon) {
            m_hIcon = cloneIcon(rhs.m_hIcon);
            m_owns = (m_hIcon != nullptr);
        }
    }
    Icon& operator=(const Icon& rhs) {
        if (this == &rhs)
            return *this;
        reset();
        if (rhs.m_hIcon) {
            m_hIcon = cloneIcon(rhs.m_hIcon);
            m_owns = (m_hIcon != nullptr);
        }
        return *this;
    }

    Icon(Icon&& rhs) noexcept : m_hIcon(rhs.m_hIcon), m_owns(rhs.m_owns) {
        rhs.m_hIcon = nullptr;
        rhs.m_owns = false;
    }
    Icon& operator=(Icon&& rhs) noexcept {
        if (this == &rhs)
            return *this;
        reset();
        m_hIcon = rhs.m_hIcon;
        m_owns = rhs.m_owns;
        rhs.m_hIcon = nullptr;
        rhs.m_owns = false;
        return *this;
    }

    ~Icon() {
        reset();
    }

    void reset() {
        if (m_owns && m_hIcon)
            DestroyIcon(m_hIcon);
        m_hIcon = nullptr;
        m_owns = false;
    }

    operator HICON() const {
        return m_hIcon;
    }
    HICON get() const {
        return m_hIcon;
    }

    static Icon FromStock(SHSTOCKICONID id, bool small_icon = true) {
        HICON h = nullptr;

        SHSTOCKICONINFO sii{};
        sii.cbSize = sizeof(sii);
        const UINT tryFlags[] = {(UINT)(SHGSI_ICON | (small_icon ? SHGSI_SMALLICON : SHGSI_LARGEICON)), (UINT)(SHGSI_ICON | SHGSI_SHELLICONSIZE), (UINT)(SHGSI_ICON)};
        for (UINT f : tryFlags) {
            ZeroMemory(&sii, sizeof(sii));
            sii.cbSize = sizeof(sii);
            if (SUCCEEDED(SHGetStockIconInfo(id, f, &sii)) && sii.hIcon) {
                h = sii.hIcon;
                break;
            }
        }

        if (!h) {
            ZeroMemory(&sii, sizeof(sii));
            sii.cbSize = sizeof(sii);
            if (SUCCEEDED(SHGetStockIconInfo(id, SHGSI_ICONLOCATION, &sii)) && sii.szPath[0]) {
                HICON hLarge = nullptr, hSmall = nullptr;
                if (ExtractIconExW(sii.szPath, sii.iIcon, &hLarge, &hSmall, 1) > 0) {
                    h = small_icon ? (hSmall ? hSmall : hLarge) : (hLarge ? hLarge : hSmall);
                    if (hSmall && hSmall != h)
                        DestroyIcon(hSmall);
                    if (hLarge && hLarge != h)
                        DestroyIcon(hLarge);
                }
            }
        }

        if (h)
            return Icon(h, OwnershipPolicy::Adopt);
        return Icon();
    }

    // Convert to 32bpp top-down DIB for MIIM_BITMAP glyphs
    HBITMAP toBitmap(int cx, int cy) const {
        if (!m_hIcon)
            return nullptr;

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = cx;
        bi.bmiHeader.biHeight = -cy; // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HDC screen = GetDC(nullptr);
        if (!screen)
            return nullptr;

        HBITMAP hbmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!hbmp) {
            ReleaseDC(nullptr, screen);
            return nullptr;
        }

        HDC mem = CreateCompatibleDC(screen);
        HGDIOBJ old = SelectObject(mem, hbmp);

        RECT rc{0, 0, cx, cy};
        HBRUSH clear = (HBRUSH)GetStockObject(BLACK_BRUSH);
        FillRect(mem, &rc, clear);

        DrawIconEx(mem, 0, 0, m_hIcon, cx, cy, 0, nullptr, DI_NORMAL);

        SelectObject(mem, old);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);

        return hbmp;
    }
};

class Image {
    HBITMAP m_hbmp = nullptr;
    bool m_owns = false;

    static HBITMAP cloneBitmap(HBITMAP src) {
        if (!src)
            return nullptr;
        // Copy with same size; 0,0 lets GDI pick original size; LR_CREATEDIBSECTION gives an ownable DIB
        return reinterpret_cast<HBITMAP>(CopyImage(src, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    }

  public:
    Image() = default;

    explicit Image(HBITMAP h, OwnershipPolicy p = OwnershipPolicy::Copy) {
        switch (p) {
            case OwnershipPolicy::Copy:
                m_hbmp = cloneBitmap(h);
                m_owns = (m_hbmp != nullptr);
                break;
            case OwnershipPolicy::Borrow:
                m_hbmp = h;
                m_owns = false;
                break;
            case OwnershipPolicy::Adopt:
                m_hbmp = h;
                m_owns = true;
                break;
        }
    }

    explicit Image(const std::wstring& path) {
        m_hbmp = reinterpret_cast<HBITMAP>(LoadImageW(nullptr, path.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION));
        if (!m_hbmp)
            throw std::runtime_error("LoadImageW bitmap failed");
        m_owns = true;
    }
    explicit Image(const wchar_t* path) : Image(std::wstring(path)) {}

    Image(const Image& rhs) {
        if (rhs.m_hbmp) {
            m_hbmp = cloneBitmap(rhs.m_hbmp);
            m_owns = (m_hbmp != nullptr);
        }
    }
    Image& operator=(const Image& rhs) {
        if (this == &rhs)
            return *this;
        reset();
        if (rhs.m_hbmp) {
            m_hbmp = cloneBitmap(rhs.m_hbmp);
            m_owns = (m_hbmp != nullptr);
        }
        return *this;
    }

    Image(Image&& rhs) noexcept : m_hbmp(rhs.m_hbmp), m_owns(rhs.m_owns) {
        rhs.m_hbmp = nullptr;
        rhs.m_owns = false;
    }
    Image& operator=(Image&& rhs) noexcept {
        if (this == &rhs)
            return *this;
        reset();
        m_hbmp = rhs.m_hbmp;
        m_owns = rhs.m_owns;
        rhs.m_hbmp = nullptr;
        rhs.m_owns = false;
        return *this;
    }

    ~Image() {
        reset();
    }

    void reset() {
        if (m_owns && m_hbmp)
            DeleteObject(m_hbmp);
        m_hbmp = nullptr;
        m_owns = false;
    }

    operator HBITMAP() const {
        return m_hbmp;
    }
    HBITMAP get() const {
        return m_hbmp;
    }
};
} // namespace tray
