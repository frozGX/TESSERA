// What the parts of Tessera (Main, Overlay, Editor, Settings) share.
#pragma once
#include <windows.h>

#include "Config.h"

#include <cmath>
#include <map>
#include <utility>

namespace tessera {

extern HINSTANCE g_instance;
extern Config g_config;

constexpr COLORREF kAccent = RGB(0, 120, 215);

// Writes Tessera.cfg. Zone edits and settings changes that are still unsaved in an open window are left
// out (the two Mask functions), so one window's Save never writes the other's work in progress.
// False when nothing reached the file (it could not be written just now, or was never readable).
bool SaveSettings();
void EditorMaskUnsaved(Config& config);
void SettingsMaskUnsaved(Config& config);

// Whether a key is held, for the editor and settings windows (Shift, Ctrl). A test's private copy
// (--isolated) always says no: its windows are worked by messages on a desktop of their own, and a key
// pressed on the real keyboard meanwhile must not change what a click there does.
bool KeyHeld(int vk);

std::wstring MonitorName(HMONITOR monitor);
RECT WorkArea(HMONITOR monitor);
int PresetForMonitor(HMONITOR monitor);
void SetPresetForMonitor(HMONITOR monitor, int preset);

// "2 - 61% / 39%", or "Preset 4" for an empty one.
std::wstring PresetTitle(int preset);

// ---------------------------------------------------------------- units <-> pixels
inline RECT ZoneRect(const Zone& z, const RECT& area)
{
    const int w = area.right - area.left, h = area.bottom - area.top;
    return { area.left + MulDiv(z.left, w, kUnits), area.top + MulDiv(z.top, h, kUnits),
             area.left + MulDiv(z.right, w, kUnits), area.top + MulDiv(z.bottom, h, kUnits) };
}

// Where a window snapped to a zone goes: the zone less the gap, which is whole at the screen's edge and
// shared between two neighbours.
inline RECT SnapRect(const Zone& z, const RECT& area, int gap)
{
    RECT r = ZoneRect(z, area);
    const int half = gap / 2;
    r.left += z.left <= 0 ? gap : half;
    r.top += z.top <= 0 ? gap : half;
    r.right -= z.right >= kUnits ? gap : gap - half;
    r.bottom -= z.bottom >= kUnits ? gap : gap - half;
    return r;
}

inline COLORREF Rgb(unsigned colour) { return RGB((colour >> 16) & 255, (colour >> 8) & 255, colour & 255); }
inline unsigned FromRgb(COLORREF colour) { return (GetRValue(colour) << 16) | (GetGValue(colour) << 8) | GetBValue(colour); }

inline POINT ToUnits(POINT px, const RECT& area)
{
    const int w = area.right - area.left, h = area.bottom - area.top;
    if (w <= 0 || h <= 0) return { 0, 0 };
    return { MulDiv(px.x - area.left, kUnits, w), MulDiv(px.y - area.top, kUnits, h) };
}

// ---------------------------------------------------------------- drawing
// A picture to draw into and keep: 32 bits a pixel, top row first, reachable both through GDI (dc) and
// directly (pixels). Windows keep one each and repaint it only when what they show has changed.
struct Canvas {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    DWORD* pixels = nullptr;
    int w = 0, h = 0;

    Canvas() = default;
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;
    ~Canvas() { Free(); }

    void Free()
    {
        if (dc) {
            SelectObject(dc, old);
            DeleteObject(bitmap);
            DeleteDC(dc);
        }
        dc = nullptr;
        bitmap = nullptr;
        pixels = nullptr;
        w = h = 0;
    }

    // True when there is a picture of that size to draw into. A new one starts blank.
    bool Resize(int width, int height)
    {
        if (pixels && width == w && height == h) return true;
        Free();
        if (width <= 0 || height <= 0) return false;
        BITMAPINFO info{};
        info.bmiHeader = { sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB };
        dc = CreateCompatibleDC(nullptr);
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
        if (!bitmap) {
            DeleteDC(dc);
            dc = nullptr;
            return false;
        }
        old = SelectObject(dc, bitmap);
        w = width;
        h = height;
        return true;
    }
};

inline void Fill(HDC dc, const RECT& r, COLORREF colour)
{
    SetDCBrushColor(dc, colour);
    FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

// The pixels behind a DC, when it draws into a 32-bit picture such as a Canvas. Rounded corners are written
// straight into them, which is far faster than asking GDI for one pixel at a time.
struct Pixels {
    DWORD* bits = nullptr;
    int w = 0, h = 0;
    bool topDown = true;
    DWORD* At(int x, int y) const
    {
        if (x < 0 || y < 0 || x >= w || y >= h) return nullptr;
        return bits + static_cast<size_t>(topDown ? y : h - 1 - y) * w + x;
    }
};

inline Pixels PixelsOf(HDC dc)
{
    Pixels px;
    DIBSECTION section{};
    const HGDIOBJ bitmap = GetCurrentObject(dc, OBJ_BITMAP);
    if (!bitmap || GetObjectW(bitmap, sizeof(section), &section) != sizeof(section)) return px;
    if (!section.dsBm.bmBits || section.dsBm.bmBitsPixel != 32) return px;
    GdiFlush(); // what GDI has been asked to draw is there before the pixels are touched
    px.bits = static_cast<DWORD*>(section.dsBm.bmBits);
    px.w = section.dsBm.bmWidth;
    px.h = section.dsBm.bmHeight;
    // Every picture Tessera makes has its top row first (Canvas, and the ones written to files). Windows
    // doesn't say which way round one is here: it reports the height as positive either way.
    px.topDown = true;
    return px;
}

// How much of the pixel at (x, y) inside a radius x radius corner square lies within the rounded corner,
// from 0 to 1. (0, 0) is the square's outer point.
inline double CornerCover(int x, int y, int radius)
{
    const double dx = radius - x - 0.5, dy = radius - y - 0.5;
    const double distance = radius - std::sqrt(dx * dx + dy * dy) + 0.5;
    return distance <= 0 ? 0 : distance >= 1 ? 1 : distance;
}

inline COLORREF Mix(COLORREF over, COLORREF under, double share)
{
    const auto channel = [share](int a, int b) { return static_cast<int>(a * share + b * (1 - share) + 0.5); };
    return RGB(channel(GetRValue(over), GetRValue(under)), channel(GetGValue(over), GetGValue(under)), channel(GetBValue(over), GetBValue(under)));
}

// A COLORREF is 0x00BBGGRR; a picture's pixel is 0x00RRGGBB.
inline DWORD ToPixel(COLORREF c) { return (static_cast<DWORD>(GetRValue(c)) << 16) | (static_cast<DWORD>(GetGValue(c)) << 8) | GetBValue(c); }
inline COLORREF FromPixel(DWORD p) { return RGB((p >> 16) & 255, (p >> 8) & 255, p & 255); }

// Sets one pixel to colour, or to a blend of it and what is there (cover below 1).
inline void CornerPixel(HDC dc, const Pixels& px, int x, int y, COLORREF colour, double cover)
{
    if (px.bits) {
        if (DWORD* at = px.At(x, y)) *at = cover >= 1 ? ToPixel(colour) : ToPixel(Mix(colour, FromPixel(*at), cover));
    } else {
        SetPixelV(dc, x, y, cover >= 1 ? colour : Mix(colour, GetPixel(dc, x, y), cover));
    }
}

// A filled rectangle with rounded corners, their edges blended into what is already there.
inline void RoundFill(HDC dc, const RECT& r, int radius, COLORREF colour)
{
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;
    radius = radius < 0 ? 0 : radius;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    Fill(dc, { r.left, r.top + radius, r.right, r.bottom - radius }, colour);
    Fill(dc, { r.left + radius, r.top, r.right - radius, r.top + radius }, colour);
    Fill(dc, { r.left + radius, r.bottom - radius, r.right - radius, r.bottom }, colour);
    if (radius == 0) return;

    const Pixels px = PixelsOf(dc);
    for (int y = 0; y < radius; ++y) {
        for (int x = 0; x < radius; ++x) {
            const double cover = CornerCover(x, y, radius);
            if (cover <= 0) continue;
            CornerPixel(dc, px, r.left + x, r.top + y, colour, cover);
            CornerPixel(dc, px, r.right - 1 - x, r.top + y, colour, cover);
            CornerPixel(dc, px, r.left + x, r.bottom - 1 - y, colour, cover);
            CornerPixel(dc, px, r.right - 1 - x, r.bottom - 1 - y, colour, cover);
        }
    }
}

// The same with a border: the border's colour first, the fill inside it.
inline void RoundBox(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF border, int thickness)
{
    RoundFill(dc, r, radius, border);
    RECT inner = r;
    InflateRect(&inner, -thickness, -thickness);
    RoundFill(dc, inner, radius - thickness, fill);
}

// Rounds off something already drawn square (a picture), by painting outside its corners.
inline void RoundOutside(HDC dc, const RECT& r, int radius, COLORREF outside)
{
    const Pixels px = PixelsOf(dc);
    for (int y = 0; y < radius; ++y) {
        for (int x = 0; x < radius; ++x) {
            const double cover = 1 - CornerCover(x, y, radius);
            if (cover <= 0) continue;
            CornerPixel(dc, px, r.left + x, r.top + y, outside, cover);
            CornerPixel(dc, px, r.right - 1 - x, r.top + y, outside, cover);
            CornerPixel(dc, px, r.left + x, r.bottom - 1 - y, outside, cover);
            CornerPixel(dc, px, r.right - 1 - x, r.bottom - 1 - y, outside, cover);
        }
    }
}

inline void Text(HDC dc, const std::wstring& text, RECT r, HFONT font, COLORREF colour, UINT format)
{
    const HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, colour);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &r, format | DT_NOPREFIX);
    SelectObject(dc, old);
}

inline HFONT MakeFont(int height, int weight = FW_NORMAL)
{
    return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

// A font to use straight away and not keep: made once per size, not once per paint. (Zone numbers come in
// whatever size the zone allows, so the store is emptied when it has grown large.)
inline HFONT CachedFont(int height, int weight)
{
    static std::map<std::pair<int, int>, HFONT> cache;
    const auto key = std::make_pair(height, weight);
    const auto found = cache.find(key);
    if (found != cache.end()) return found->second;
    if (cache.size() > 64) {
        for (const auto& entry : cache) DeleteObject(entry.second);
        cache.clear();
    }
    return cache[key] = MakeFont(height, weight);
}

// ---------------------------------------------------------------- the zones shown during a drag
bool OverlayRegister();
// lit: what a drop would fill, in the preset's own units. Every zone lying inside it is lit: one zone as a
// rule, several when the window is being stretched across them. Null for none.
void OverlayShow(HMONITOR monitor, int preset, const Zone* lit);
void OverlayHide();
// Draws the zones as a drag shows them over an opaque picture of w x h pixels (the settings preview, and
// pictures for checking). highlight: the zone under the pointer, or -1.
void OverlayPaintOver(DWORD* pixels, int w, int h, int preset, int highlight, int dpi, int gap, int padding);
// For measuring: draws the zones as a drag does (first: all of them, as when they appear; then: one zone
// again, as when the highlight moves) into a picture of w x h, and says how many milliseconds each took.
void OverlayMeasure(int w, int h, int preset, int dpi, double& all, double& one);

// ---------------------------------------------------------------- the settings window
bool SettingsRegister();
void SettingsOpen();
HWND SettingsWindow();
bool SettingsHasUnsaved(); // likewise for settings changed and not yet saved
void SettingsPaintTo(HDC dc);

bool StartsWithWindows();
void SetStartsWithWindows(bool on);
void QuitTessera();

// Whether Tessera is running as administrator, and the icon that goes with it: 1 the frog, 2 the toad.
bool IsElevated();
int AppIconId();

// Windows 11's own bar of snap layouts that drops from the top of the screen during a drag. This is the
// switch in Windows Settings > System > Multitasking, "Show snap layouts when I drag a window to the top
// of my screen": Tessera only flips it, and only when asked to in its settings.
bool WindowsSnapBarHidden();
void SetWindowsSnapBarHidden(bool hidden);

// ---------------------------------------------------------------- the layout editor
bool EditorRegister();
void EditorOpen();
bool EditorIsOpen();
bool EditorHasUnsaved();   // zone edits that closing Tessera now would lose
HWND EditorWindow();
void EditorPaintTo(HDC dc);
void EditorPointAt(POINT client); // moves the editor's idea of the mouse; for snapshots

} // namespace tessera
