// The zones drawn over a monitor while a window is being dragged. Clicks pass straight through it.
//
// The picture has its own transparency, pixel by pixel: the fill of a zone is as faint as the opacity
// setting says, while its border and number stay crisp, and round corners are smooth against whatever is
// behind. It is drawn once when the zones appear; after that only the zones whose highlight is changing
// are redrawn, and only that part of the screen is refreshed. Appearing, disappearing and the highlight
// moving from zone to zone are short fades.
#include "App.h"

#include <shellscalingapi.h>

#include <algorithm>
#include <vector>

namespace tessera {

namespace {

const wchar_t kClass[] = L"TesseraOverlay";
constexpr UINT_PTR kTick = 1;
// How long the zones take to appear and disappear is the user's (the fade setting; 0 for at once). The
// highlight arriving at or leaving a zone takes four fifths of that.
constexpr int kBorderAlpha = 235;

// ---------------------------------------------------------------- pixels with transparency
// 0xAARRGGBB, with the colour already multiplied by the alpha: what UpdateLayeredWindow expects.
struct Picture {
    DWORD* px;
    int w, h;
};

DWORD Argb(COLORREF c, int alpha)
{
    const auto part = [alpha](int v) { return static_cast<DWORD>(v * alpha / 255); };
    return (static_cast<DWORD>(alpha) << 24) | (part(GetRValue(c)) << 16) | (part(GetGValue(c)) << 8) | part(GetBValue(c));
}

// Part of the way from one pixel to another, every channel alike.
DWORD Lerp(DWORD from, DWORD to, double t)
{
    DWORD out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const int a = (from >> shift) & 255, b = (to >> shift) & 255;
        out |= static_cast<DWORD>(a + (b - a) * t + 0.5) << shift;
    }
    return out;
}

// src laid over dst.
DWORD Over(DWORD src, DWORD dst)
{
    const int keep = 255 - static_cast<int>(src >> 24);
    DWORD out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const int s = (src >> shift) & 255, d = (dst >> shift) & 255;
        out |= static_cast<DWORD>(std::min(255, s + d * keep / 255)) << shift;
    }
    return out;
}

void ARect(const Picture& p, RECT r, DWORD value)
{
    r.left = std::max<LONG>(r.left, 0);
    r.top = std::max<LONG>(r.top, 0);
    r.right = std::min<LONG>(r.right, p.w);
    r.bottom = std::min<LONG>(r.bottom, p.h);
    if (r.right <= r.left) return;
    for (LONG y = r.top; y < r.bottom; ++y) std::fill_n(p.px + static_cast<size_t>(y) * p.w + r.left, r.right - r.left, value);
}

// A rounded rectangle that replaces what was there, its corner edges blended.
void ARoundFill(const Picture& p, const RECT& r, int radius, DWORD value)
{
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;
    radius = std::clamp(radius, 0, std::min(w, h) / 2);
    ARect(p, { r.left, r.top + radius, r.right, r.bottom - radius }, value);
    ARect(p, { r.left + radius, r.top, r.right - radius, r.top + radius }, value);
    ARect(p, { r.left + radius, r.bottom - radius, r.right - radius, r.bottom }, value);

    const auto put = [&](int x, int y, double cover) {
        if (x < 0 || y < 0 || x >= p.w || y >= p.h) return;
        DWORD& at = p.px[static_cast<size_t>(y) * p.w + x];
        at = cover >= 1 ? value : Lerp(at, value, cover);
    };
    for (int y = 0; y < radius; ++y) {
        for (int x = 0; x < radius; ++x) {
            const double cover = CornerCover(x, y, radius);
            if (cover <= 0) continue;
            put(r.left + x, r.top + y, cover);
            put(r.right - 1 - x, r.top + y, cover);
            put(r.left + x, r.bottom - 1 - y, cover);
            put(r.right - 1 - x, r.bottom - 1 - y, cover);
        }
    }
}

// Text, centred in r, laid over what is there. GDI can't write transparency, so the words are drawn white
// on black on the side and how white each pixel came out is how much of the colour it gets.
void AText(const Picture& p, const std::wstring& text, const RECT& r, HFONT font, COLORREF colour, int alpha)
{
    static Canvas scratch;
    if (!scratch.pixels && !scratch.Resize(256, 64)) return;
    HGDIOBJ old = SelectObject(scratch.dc, font);
    SIZE size{};
    GetTextExtentPoint32W(scratch.dc, text.c_str(), static_cast<int>(text.size()), &size);
    const int tw = size.cx + 2, th = size.cy;
    if (tw > scratch.w || th > scratch.h) {
        SelectObject(scratch.dc, old);
        if (!scratch.Resize(std::max(scratch.w, tw), std::max(scratch.h, th))) return;
        old = SelectObject(scratch.dc, font);
    }
    Fill(scratch.dc, { 0, 0, tw, th }, RGB(0, 0, 0));
    SetBkMode(scratch.dc, TRANSPARENT);
    SetTextColor(scratch.dc, RGB(255, 255, 255));
    TextOutW(scratch.dc, 1, 0, text.c_str(), static_cast<int>(text.size()));
    SelectObject(scratch.dc, old);
    GdiFlush();

    const int left = (r.left + r.right - tw) / 2, top = (r.top + r.bottom - th) / 2;
    for (int y = 0; y < th; ++y) {
        const int py = top + y;
        if (py < 0 || py >= p.h) continue;
        for (int x = 0; x < tw; ++x) {
            const int px = left + x;
            if (px < 0 || px >= p.w) continue;
            const int cover = (scratch.pixels[static_cast<size_t>(y) * scratch.w + x] >> 8) & 255;
            if (!cover) continue;
            DWORD& at = p.px[static_cast<size_t>(py) * p.w + px];
            at = Over(Argb(colour, cover * alpha / 255), at);
        }
    }
}

// ---------------------------------------------------------------- what the zones look like
struct Look {
    int dpi, gap, padding;
    int Scale(int v) const { return std::max(1, MulDiv(v, dpi, 96)); }
};

// Where a zone is drawn: where the window will land, less the padding, half of it on each side, so two
// neighbours stand that far apart. An odd padding's spare
// pixel goes to the right and the bottom, so the two halves always add up to the whole.
RECT ZoneBox(const Picture& p, const Zone& zone, const Look& look)
{
    RECT r = SnapRect(zone, { 0, 0, p.w, p.h }, look.gap);
    const int first = look.padding / 2, rest = look.padding - first;
    r.left += first;
    r.top += first;
    r.right -= rest;
    r.bottom -= rest;
    return r;
}

// What is drawn: a preset's zones, and for one that is open its sub-zones too, numbered "3.1", "3.2".
// Sub-zones may lie over one another: the larger are drawn first, the smaller on top of them.
struct Tile {
    Zone zone;  // in the units of the screen
    std::wstring number;
    Zone clear; // the part of it nothing drawn later lies over, where its number goes; of no size when there is none
    std::wstring badge; // the number of the zone it is a sub-zone of, shown small in its corner: on the one sub-zone in the zone's bottom left corner
};

// A zone showing its sub-zones still says which zone it is: its own number, small, in its bottom left
// corner (the top of the screen is where the preset's name is). On top of the sub-zone that lies there,
// and drawn after every zone, so none drawn later takes a bite out of it. Left out where the sub-zone is
// too small for it and its own number both.
void RenderBadge(const Picture& p, const Tile& tile, const Look& look)
{
    if (!g_config.showNumbers || tile.badge.empty()) return;
    const RECT r = ZoneBox(p, tile.zone, look);
    const int w = look.Scale(22) + look.Scale(12) * static_cast<int>(tile.badge.size()), h = look.Scale(30), in = look.Scale(8);
    const RECT pill = { r.left + in, r.bottom - in - h, r.left + in + w, r.bottom - in };
    if (r.right - r.left < 3 * w || r.bottom - r.top < 3 * h) return;
    ARoundFill(p, pill, look.Scale(7), Argb(RGB(255, 255, 255), 255));
    RECT inside = pill;
    InflateRect(&inside, -look.Scale(1), -look.Scale(1));
    ARoundFill(p, inside, look.Scale(7) - look.Scale(1), Argb(RGB(16, 18, 22), 240));
    AText(p, tile.badge, pill, CachedFont(look.Scale(18), FW_SEMIBOLD), RGB(255, 255, 255), 255);
}

// One zone. lit runs from 0 (as any zone) to 1 (the one a drop would fill).
void RenderZone(const Picture& p, const Tile& tile, double lit, const Look& look)
{
    const Zone& zone = tile.zone;
    const std::wstring& number = tile.number;
    const RECT r = ZoneBox(p, zone, look);
    if (r.right - r.left < 4 || r.bottom - r.top < 4) return; // squeezed out by the gap
    // Rounded if the user likes them so.
    const int radius = g_config.roundCorners && look.padding > 0 ? look.Scale(8) : 0, border = look.Scale(2);
    const COLORREF colour = Mix(Rgb(g_config.highlightColour), Rgb(g_config.zoneColour), lit);
    // The lit zone is a little more solid than the rest, so it tells even when the fill is very faint.
    const int percent = std::min(95, g_config.opacity + static_cast<int>(20 * lit + 0.5));

    ARoundFill(p, r, radius, Argb(Rgb(g_config.borderColour), kBorderAlpha));
    RECT inner = r;
    InflateRect(&inner, -border, -border);
    ARoundFill(p, inner, radius - border, Argb(colour, 255 * percent / 100));

    if (g_config.showNumbers) {
        // In the part of it that shows: all of it as a rule, less where a smaller sub-zone lies on top.
        RECT room = r;
        if (!(tile.clear == zone)) {
            const RECT clear = ZoneRect(tile.clear, { 0, 0, p.w, p.h });
            if (!IntersectRect(&room, &r, &clear)) return;
        }
        const int least = static_cast<int>(std::min(room.right - room.left, room.bottom - room.top));
        const int whole = std::clamp(static_cast<int>(std::min(r.right - r.left, r.bottom - r.top)) / 4, look.Scale(24), look.Scale(140));
        // Nor wider than its room: "3.2" in a narrow strip is three figures across.
        const int fits = static_cast<int>((room.right - room.left) * 10 / (6 * static_cast<long>(number.size()) + 4));
        const int size = std::min({ whole, least * 8 / 10, fits }) & ~3;
        if (size >= 8) AText(p, number, room, CachedFont(size, FW_SEMIBOLD), Rgb(g_config.textColour), 255);
    }
}

bool Within(const Zone& z, const Zone& box)
{
    return z.left >= box.left && z.top >= box.top && z.right <= box.right && z.bottom <= box.bottom;
}

// Which zones are drawn as their sub-zones: the one the sub-zone key has opened, and any that what a drop
// would fill covers only a part of (a span begun in a sub-zone and carried on into the next zone), so
// that what is lit is always what the window will cover.
std::vector<char> Opened(int preset, int open, const Zone* span)
{
    std::vector<char> opened;
    if (preset < 0 || preset >= kPresetCount) return opened;
    const std::vector<Zone>& zones = g_config.presets[preset].zones;
    opened.assign(zones.size(), 0);
    for (size_t i = 0; i < zones.size(); ++i) {
        const Zone& z = zones[i];
        if (z.subs.empty()) continue;
        const bool partly = span && !Within(z, *span) && z.left < span->right && span->left < z.right && z.top < span->bottom && span->top < z.bottom;
        opened[i] = static_cast<int>(i) == open || partly;
    }
    return opened;
}

std::vector<Tile> Tiles(int preset, const std::vector<char>& opened)
{
    std::vector<Tile> tiles;
    if (preset < 0 || preset >= kPresetCount) return tiles;
    const std::vector<Zone>& zones = g_config.presets[preset].zones;
    for (size_t i = 0; i < zones.size(); ++i) {
        const Zone& z = zones[i];
        const Zone plain = { z.left, z.top, z.right, z.bottom, {} };
        if (i < opened.size() && opened[i] && !z.subs.empty()) {
            // The zone itself as well, where its sub-zones leave some of it showing: a drop there fills it.
            const Zone bare = FreePart({ 0, 0, kUnits, kUnits, {} }, z.subs);
            const bool own = bare.right > bare.left && bare.bottom > bare.top;
            // Its number stands in that part where there is room for it (3 % of the screen each way); in a
            // sliver it would be lost, and goes in the corner instead.
            const Zone room = Inside(z, bare); // in the screen's units: a share of a narrow zone is very little
            const bool roomy = room.right - room.left >= 300 && room.bottom - room.top >= 300;
            if (own) tiles.push_back({ plain, roomy ? std::to_wstring(i + 1) : std::wstring(), plain });
            std::vector<size_t> order(z.subs.size());
            for (size_t j = 0; j < order.size(); ++j) order[j] = j;
            const auto size = [&](size_t j) { return static_cast<long long>(z.subs[j].right - z.subs[j].left) * (z.subs[j].bottom - z.subs[j].top); };
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return size(a) > size(b); });
            const size_t first = tiles.size();
            for (const size_t j : order) {
                const Zone whole = Inside(z, z.subs[j]);
                tiles.push_back({ whole, std::to_wstring(i + 1) + L"." + std::to_wstring(j + 1), whole });
            }
            // Otherwise its number goes on the sub-zone in its bottom left corner: the last of them drawn
            // there, which is the one on top. (None there: then that corner is the zone's own.)
            bool badged = roomy;
            for (size_t t = tiles.size(); !badged && t > first; --t) {
                if (tiles[t - 1].zone.left == plain.left && tiles[t - 1].zone.bottom == plain.bottom) {
                    tiles[t - 1].badge = std::to_wstring(i + 1);
                    badged = true;
                }
            }
            if (!badged && own) tiles[first - 1].badge = std::to_wstring(i + 1);
        } else {
            tiles.push_back({ plain, std::to_wstring(i + 1), plain });
        }
    }
    // Where each one's number goes: clear of whatever is drawn after it.
    for (size_t i = 0; i < tiles.size(); ++i) {
        std::vector<Zone> over;
        for (size_t j = i + 1; j < tiles.size(); ++j) {
            const Zone& a = tiles[i].zone;
            const Zone& b = tiles[j].zone;
            if (a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom) over.push_back(b);
        }
        if (!over.empty()) tiles[i].clear = FreePart(tiles[i].zone, over);
    }
    return tiles;
}

// Whether any of them lies over another. Then one can't be drawn again by itself.
bool Overlapping(const std::vector<Tile>& tiles)
{
    for (const Tile& tile : tiles) {
        if (!(tile.clear == tile.zone)) return true;
    }
    return false;
}

// Which preset this is, top centre, so a press of 1 to 9 shows what it picked. Gives back where it is.
RECT RenderLabel(const Picture& p, int preset, const Look& look)
{
    if (!g_config.showLabel) return {};
    std::wstring label = PresetTitle(preset);
    if (g_config.presets[preset].Empty()) label += L"  (empty)";
    const HFONT font = CachedFont(look.Scale(20), FW_SEMIBOLD);
    const HDC screen = GetDC(nullptr);
    const HGDIOBJ old = SelectObject(screen, font);
    SIZE extent{};
    GetTextExtentPoint32W(screen, label.c_str(), static_cast<int>(label.size()), &extent);
    SelectObject(screen, old);
    ReleaseDC(nullptr, screen);

    const int centre = p.w / 2;
    const RECT pill = { centre - extent.cx / 2 - look.Scale(18), look.Scale(22), centre + extent.cx / 2 + look.Scale(18),
                        look.Scale(22) + extent.cy + look.Scale(14) };
    ARoundFill(p, pill, look.Scale(8), Argb(RGB(255, 255, 255), 255));
    RECT inner = pill;
    InflateRect(&inner, -look.Scale(1), -look.Scale(1));
    ARoundFill(p, inner, look.Scale(8) - look.Scale(1), Argb(RGB(16, 18, 22), 240));
    AText(p, label, pill, font, RGB(255, 255, 255), 255);
    return pill;
}

void RenderAll(const Picture& p, int preset, const std::vector<Tile>& tiles, const std::vector<float>& lit, const Look& look, RECT* pill = nullptr)
{
    std::fill_n(p.px, static_cast<size_t>(p.w) * p.h, 0u);
    if (preset < 0 || preset >= kPresetCount) return;
    for (size_t i = 0; i < tiles.size(); ++i) RenderZone(p, tiles[i], i < lit.size() ? lit[i] : 0, look);
    for (const Tile& tile : tiles) RenderBadge(p, tile, look);
    const RECT where = RenderLabel(p, preset, look);
    if (pill) *pill = where;
}

// ---------------------------------------------------------------- the window
HWND s_window = nullptr;
HMONITOR s_monitor = nullptr;
RECT s_area{};
int s_preset = -1;
std::vector<char> s_opened; // the zones showing their sub-zones
std::vector<Tile> s_tiles;  // what is on screen
Zone s_span{};          // what a drop would fill; the zones inside it are lit
bool s_spanning = false;
bool s_across = true;   // it is a stretch across several: all inside it are lit. Otherwise just the one it is.
bool s_overlap = false; // some of what is on screen lies over the rest

bool s_shown = false;
Canvas s_frame;
Look s_look{};
std::vector<float> s_lit; // how far each zone is lit, moving towards 1 for the highlighted one and 0 for the rest
RECT s_pill{};
double s_alpha = 0; // of the whole picture, 0 to 255: the fade in and out
int s_alphaTarget = 0;
ULONGLONG s_lastTick = 0;
bool s_ticking = false;

int MonitorDpi(HMONITOR monitor)
{
    UINT x = 96, y = 96;
    return SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y)) ? static_cast<int>(x) : 96;
}

bool Same(const Zone& a, const Zone& b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }

// Whether a drop would fill it.
bool LitNow(const Tile& tile) { return s_spanning && (s_across ? Within(tile.zone, s_span) : Same(tile.zone, s_span)); }

// Puts the picture on screen: all of it, or only the part that changed.
void Present(const RECT* changed)
{
    if (!s_frame.pixels) return;
    POINT source = { 0, 0 }, at = { s_area.left, s_area.top };
    SIZE size = { s_frame.w, s_frame.h };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, static_cast<BYTE>(std::clamp(s_alpha, 0.0, 255.0) + 0.5), AC_SRC_ALPHA };
    UPDATELAYEREDWINDOWINFO info{ sizeof(info) };
    info.pptDst = &at;
    info.psize = &size;
    info.hdcSrc = s_frame.dc;
    info.pptSrc = &source;
    info.pblend = &blend;
    info.dwFlags = ULW_ALPHA;
    info.prcDirty = changed;
    UpdateLayeredWindowIndirect(s_window, &info);
}

void StartTicking()
{
    if (s_ticking) return;
    s_ticking = true;
    s_lastTick = GetTickCount64();
    SetTimer(s_window, kTick, 10, nullptr);
}

// One step of whatever is fading.
void Tick()
{
    const ULONGLONG now = GetTickCount64();
    const double elapsed = static_cast<double>(std::min<ULONGLONG>(now - s_lastTick, 100));
    s_lastTick = now;
    bool more = false, faded = false, redrawn = false, whole = false;
    RECT changed{};

    if (s_alpha != s_alphaTarget) {
        const double step = g_config.fade > 0 ? 255.0 * elapsed / g_config.fade : 255.0;
        s_alpha = s_alpha < s_alphaTarget ? std::min<double>(s_alphaTarget, s_alpha + step) : std::max<double>(s_alphaTarget, s_alpha - step);
        faded = true;
        more = more || s_alpha != s_alphaTarget;
    }

    if (s_frame.pixels && s_preset >= 0 && s_preset < kPresetCount && s_overlap) {
        // Sub-zones lying over one another can't be drawn again one at a time, so the lights change at once
        // and everything is drawn afresh.
        bool any = false;
        for (size_t i = 0; i < std::min(s_tiles.size(), s_lit.size()); ++i) {
            const float target = LitNow(s_tiles[i]) ? 1.0f : 0.0f;
            any = any || s_lit[i] != target;
            s_lit[i] = target;
        }
        if (any) {
            RenderAll({ s_frame.pixels, s_frame.w, s_frame.h }, s_preset, s_tiles, s_lit, s_look, &s_pill);
            whole = true;
        }
    } else if (s_frame.pixels && s_preset >= 0 && s_preset < kPresetCount) {
        const Picture p = { s_frame.pixels, s_frame.w, s_frame.h };
        const std::vector<Tile>& zones = s_tiles;
        const float step = g_config.fade > 0 ? static_cast<float>(elapsed / (g_config.fade * 0.8)) : 1.0f;
        const size_t count = std::min(zones.size(), s_lit.size());
        std::vector<char> redo(count, 0);
        bool any = false;
        for (size_t i = 0; i < count; ++i) {
            const float target = LitNow(zones[i]) ? 1.0f : 0.0f;
            if (s_lit[i] == target) continue;
            s_lit[i] = s_lit[i] < target ? std::min(target, s_lit[i] + step) : std::max(target, s_lit[i] - step);
            more = more || s_lit[i] != target;
            redo[i] = 1;
            any = true;
        }
        // The label is drawn again on top, and its round corners are blended into what lies under them:
        // drawn over its own last picture they would harden a little each time. So what lies under it is
        // drawn afresh first.
        if (any && g_config.showLabel && !IsRectEmpty(&s_pill)) {
            ARect(p, s_pill, 0);
            for (size_t i = 0; i < count; ++i) {
                const RECT box = ZoneBox(p, zones[i].zone, s_look);
                RECT shared{};
                if (IntersectRect(&shared, &box, &s_pill)) redo[i] = 1;
            }
        }
        for (size_t i = 0; i < count; ++i) {
            if (!redo[i]) continue;
            RECT box = ZoneBox(p, zones[i].zone, s_look);
            ARect(p, box, 0);
            RenderZone(p, zones[i], s_lit[i], s_look);
            RenderBadge(p, zones[i], s_look);
            UnionRect(&changed, &changed, &box);
            redrawn = true;
        }
        if (redrawn && g_config.showLabel) { // the label sits on top of the zones just redrawn
            s_pill = RenderLabel(p, s_preset, s_look);
            UnionRect(&changed, &changed, &s_pill);
        }
    }

    if (faded || whole) Present(nullptr);
    else if (redrawn) Present(&changed);

    if (s_alphaTarget == 0 && s_alpha <= 0) { // faded right out
        ShowWindow(s_window, SW_HIDE);
        s_shown = false;
        s_frame.Free(); // a screenful of memory, not kept between drags
        more = false;
    }
    if (!more) {
        KillTimer(s_window, kTick);
        s_ticking = false;
    }
}

LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_TIMER && wparam == kTick) {
        Tick();
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

void OverlayPaintOver(DWORD* pixels, int w, int h, int preset, int highlight, int dpi, int gap, int padding, int open)
{
    if (!pixels || w <= 0 || h <= 0) return;
    static std::vector<DWORD> scratch;
    scratch.assign(static_cast<size_t>(w) * h, 0);
    const Picture p = { scratch.data(), w, h };
    const std::vector<Tile> tiles = Tiles(preset, Opened(preset, open, nullptr));
    std::vector<float> lit(tiles.size(), 0.0f);
    if (highlight >= 0 && highlight < static_cast<int>(lit.size())) lit[highlight] = 1.0f;
    RenderAll(p, preset, tiles, lit, { dpi, gap, padding });
    for (size_t i = 0; i < scratch.size(); ++i) {
        if (scratch[i]) pixels[i] = Over(scratch[i], pixels[i] | 0xFF000000) & 0x00FFFFFF;
    }
}

void OverlayMeasure(int w, int h, int preset, int dpi, double& all, double& one)
{
    all = one = 0;
    if (preset < 0 || preset >= kPresetCount || g_config.presets[preset].Empty()) return;
    std::vector<DWORD> pixels(static_cast<size_t>(w) * h);
    const Picture p = { pixels.data(), w, h };
    const Look look = { dpi, g_config.gap, kZonePadding };
    const std::vector<Tile> zones = Tiles(preset, {});
    std::vector<float> lit(zones.size(), 0.0f);
    LARGE_INTEGER rate{}, from{}, to{};
    QueryPerformanceFrequency(&rate);
    const auto since = [&] { return 1000.0 * static_cast<double>(to.QuadPart - from.QuadPart) / static_cast<double>(rate.QuadPart); };

    constexpr int times = 10;
    QueryPerformanceCounter(&from);
    for (int i = 0; i < times; ++i) RenderAll(p, preset, zones, lit, look);
    QueryPerformanceCounter(&to);
    all = since() / times;

    QueryPerformanceCounter(&from);
    for (int i = 0; i < times; ++i) {
        ARect(p, ZoneBox(p, zones[0].zone, look), 0);
        RenderZone(p, zones[0], 0.5, look);
        RenderLabel(p, preset, look);
    }
    QueryPerformanceCounter(&to);
    one = since() / times;
}

bool OverlayRegister()
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = Proc;
    wc.hInstance = g_instance;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc)) return false;

    s_window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                               kClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, g_instance, nullptr);
    return s_window != nullptr;
}

void OverlayShow(HMONITOR monitor, int preset, const Zone* lit, int open, bool across)
{
    if (!s_window || preset < 0 || preset >= kPresetCount) return;

    std::vector<char> opened = Opened(preset, open, lit);
    if (!s_shown || monitor != s_monitor || preset != s_preset || opened != s_opened || !s_frame.pixels) {
        // New to the screen, another monitor or preset, or a zone opening into its sub-zones (or closing
        // again): drawn afresh, the zone under the pointer lit already.
        s_monitor = monitor;
        s_preset = preset;
        s_opened = std::move(opened);
        s_spanning = lit != nullptr;
        s_across = across;
        if (lit) s_span = *lit;
        s_area = WorkArea(monitor);
        if (!s_frame.Resize(s_area.right - s_area.left, s_area.bottom - s_area.top)) return;
        s_look = { MonitorDpi(monitor), g_config.gap, kZonePadding };
        s_tiles = Tiles(preset, s_opened);
        s_overlap = Overlapping(s_tiles);
        s_lit.assign(s_tiles.size(), 0.0f);
        for (size_t i = 0; i < s_tiles.size(); ++i) {
            if (LitNow(s_tiles[i])) s_lit[i] = 1.0f;
        }
        RenderAll({ s_frame.pixels, s_frame.w, s_frame.h }, preset, s_tiles, s_lit, s_look, &s_pill);
        if (!s_shown) s_alpha = 0;
        Present(nullptr);
        if (!s_shown) {
            SetWindowPos(s_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            s_shown = true;
        }
    } else if ((lit != nullptr) != s_spanning || across != s_across || (lit && !(*lit == s_span))) {
        s_spanning = lit != nullptr; // the lights follow, a step at a time
        s_across = across;
        if (lit) s_span = *lit;
        StartTicking();
    }

    s_alphaTarget = 255;
    if (s_alpha < 255) StartTicking();
    if (s_ticking && g_config.fade <= 0) Tick(); // no fades: there now, not at the next tick
}

void OverlayHide()
{
    if (!s_window || !s_shown) return;
    s_alphaTarget = 0;
    StartTicking();
    if (g_config.fade <= 0) Tick();
}

} // namespace tessera
