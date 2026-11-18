// The settings window: how Tessera behaves and how the zones look. A change shows at once, and is kept only
// by Save, which leaves the window open; closing it with changes not saved puts them back as they were.
//
// Everything can be worked with the mouse or the keyboard: Tab and Shift+Tab move between the controls,
// Space or Enter presses the one in focus, and the left and right arrows move a slider.
#include "App.h"

#include <commdlg.h>
#include <dwmapi.h>

#include <algorithm>

// The version comes from CMakeLists.txt, as a narrow string; this makes it a wide one.
#define TESSERA_WIDE2(text) L##text
#define TESSERA_WIDE(text) TESSERA_WIDE2(text)
#define TESSERA_VERSION_TEXT TESSERA_WIDE(TESSERA_VERSION)

namespace tessera {

namespace {

const wchar_t kClass[] = L"TesseraSettings";
// The window can be dragged to any size and shape, and is laid out one of two ways, whichever lets the
// text be larger: tall, with the four sections in one column (at least kMinWidth x kHeight at 100 %
// scaling), or wide, with Behaviour beside Look and Theme and the preview right across below them (at
// least kWideMinWidth x kWideHeight). Everything grows with the window until it would be smaller than
// that; room beyond widens the controls and deepens the preview.
constexpr int kMinWidth = 560, kHeight = 964, kWideMinWidth = 1120, kWideHeight = 700;

enum Id {
    None, KeyNone, KeyShift, KeyCtrl, KeyAlt, SpanOff, SpanShift, SpanCtrl, SpanAlt, RightClick, Startup, PauseSnap, Opacity, Gap, Padding, Fade,
    ShowNumbers, ShowLabel, ThemeSlate, ThemeFrog, ThemeGold, ThemeMono, ColourZone, ColourHighlight, ColourBorder, ColourText, ResetAll, SaveButton
};
struct Item {
    Id id;
    RECT r;
};

constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
constexpr int kMinScale = 70; // the smallest the window can be dragged, in percent of kMinWidth x kWideHeight
constexpr COLORREF kBackground = RGB(24, 26, 32);
// Each section sits on a card a shade lighter than the window, and the buttons that act on the whole
// window (Reset, Save) on a darker bar along the bottom.
constexpr COLORREF kCard = RGB(32, 35, 43), kCardEdge = RGB(46, 50, 61), kBar = RGB(16, 17, 21), kBarEdge = RGB(46, 50, 61);

// Where a section's card is, in the layout's own units.
struct Section {
    int x, y, w, h;
};

HWND s_window = nullptr;
int s_baseDpi = 96; // the monitor's
int s_dpi = 96;     // what everything is drawn at: the monitor's, times how far the window has been resized
int s_w = kMinWidth, s_h = kHeight; // the layout's own width and height, in the units everything is placed in
bool s_wide = false;                // laid out wide, in two columns
Section s_behaviour{}, s_look{}, s_view{}, s_theme{};
int s_bar = 0;    // where the bottom bar starts
int s_pitch = 32; // from one of Behaviour's ticks to the next
int s_openedWidth = 0, s_openedHeight = 0;
bool s_userSized = false;
HFONT s_font = nullptr, s_bold = nullptr, s_head = nullptr, s_small = nullptr;
std::vector<Item> s_items; // in the order Tab visits them
RECT s_preview{};
Canvas s_canvas, s_desktop; // the picture last drawn, and the preview's mock desktop
bool s_stale = true;
Id s_dragging = None;        // the slider being dragged
Id s_hot = None;             // the control under the mouse
Id s_pressed = None;         // the control the mouse went down on; it acts when the mouse comes up on it
Id s_focus = None;           // the control the keyboard is on
bool s_focusShown = false;   // its ring is drawn once the keyboard has been used, and hidden again by a click
bool s_tracking = false;     // asked to be told when the mouse leaves the window
bool s_startup = false;      // "Start with Windows" as ticked; the registry is only written on Save
Config s_saved;              // the settings as last saved, which closing without Save goes back to

// Everything this window edits. Presets and which preset each monitor uses are not its business.
void CopySettings(Config& to, const Config& from)
{
    to.key = from.key;
    to.rightClickToggles = from.rightClickToggles;
    to.pauseWindowsSnap = from.pauseWindowsSnap;
    to.spanKey = from.spanKey;
    to.fade = from.fade;
    to.opacity = from.opacity;
    to.gap = from.gap;
    to.padding = from.padding;
    to.showNumbers = from.showNumbers;
    to.showLabel = from.showLabel;
    to.zoneColour = from.zoneColour;
    to.highlightColour = from.highlightColour;
    to.borderColour = from.borderColour;
    to.textColour = from.textColour;
}

bool s_startupWas = false; // "Start with Windows" as it really is

// Whether Save has anything to do.
bool Dirty()
{
    const Config& a = g_config;
    const Config& b = s_saved;
    return a.key != b.key || a.rightClickToggles != b.rightClickToggles ||
           a.pauseWindowsSnap != b.pauseWindowsSnap || a.spanKey != b.spanKey || a.fade != b.fade || a.opacity != b.opacity || a.gap != b.gap ||
           a.padding != b.padding || a.showNumbers != b.showNumbers || a.showLabel != b.showLabel || a.zoneColour != b.zoneColour ||
           a.highlightColour != b.highlightColour || a.borderColour != b.borderColour || a.textColour != b.textColour ||
           s_startup != s_startupWas;
}

// Whether Save is lit. Settled when a slider is let go of, not while it is being dragged: mid-drag the
// answer can flip back and forth as the handle passes where it started.
bool s_saveLit = false;
bool SaveLit()
{
    if (s_dragging == None) s_saveLit = Dirty();
    return s_saveLit;
}

int S(int v) { return MulDiv(v, s_dpi, 96); }
RECT R(int x, int y, int w, int h) { return { S(x), S(y), S(x + w), S(y + h) }; }

void Redraw()
{
    s_stale = true;
    if (s_window) InvalidateRect(s_window, nullptr, FALSE);
}

void Arrange()
{
    s_items.clear();
    const int extra = s_h - (s_wide ? kWideHeight : kHeight); // height beyond the least: the preview takes it
    if (s_wide) {
        const int column = (s_w - 36) / 2;
        s_behaviour = { 12, 10, column, 394 };
        s_look = { 24 + column, 10, s_w - 36 - column, 256 };
        s_theme = { s_look.x, 278, s_look.w, 126 };
        s_view = { 12, 416, s_w - 24, 208 + extra };
        s_bar = 636 + extra;
        s_pitch = 40; // Behaviour has the height of the two cards beside it, so its ticks sit a little further apart
    } else {
        s_behaviour = { 12, 10, s_w - 24, 252 };
        s_look = { 12, 274, s_w - 24, 256 };
        s_view = { 12, 542, s_w - 24, 208 + extra };
        s_theme = { 12, 762 + extra, s_w - 24, 126 };
        s_bar = 900 + extra;
        s_pitch = 32;
    }
    const Section& b = s_behaviour;
    const Section& l = s_look;
    const Section& t = s_theme;
    const Id keys[] = { KeyNone, KeyShift, KeyCtrl, KeyAlt };
    for (int i = 0; i < 4; ++i) s_items.push_back({ keys[i], R(b.x + b.w - 328 + i * 80, b.y + 48, 76, 30) });
    const Id spans[] = { SpanOff, SpanShift, SpanCtrl, SpanAlt };
    for (int i = 0; i < 4; ++i) s_items.push_back({ spans[i], R(b.x + b.w - 328 + i * 80, b.y + 86, 76, 30) });
    const Id ticks[] = { RightClick, Startup, PauseSnap };
    for (int i = 0; i < 3; ++i) s_items.push_back({ ticks[i], R(b.x + 12, b.y + 128 + i * s_pitch, b.w - 24, 28) });

    const Id sliders[] = { Opacity, Gap, Padding, Fade };
    for (int i = 0; i < 4; ++i) s_items.push_back({ sliders[i], R(l.x + 188, l.y + 46 + i * 34, l.w - 286, 24) });
    s_items.push_back({ ShowNumbers, R(l.x + 12, l.y + 184, l.w - 24, 28) });
    s_items.push_back({ ShowLabel, R(l.x + 12, l.y + 216, l.w - 24, 28) });
    s_preview = R(s_view.x + 12, s_view.y + 46, s_view.w - 24, s_view.h - 58);
    const Id themes[] = { ThemeSlate, ThemeFrog, ThemeGold, ThemeMono };
    for (int i = 0; i < 4; ++i) s_items.push_back({ themes[i], R(t.x + 12 + i * 128, t.y + 48, 120, 28) });
    const Id colours[] = { ColourZone, ColourHighlight, ColourBorder, ColourText };
    const int quarter = (t.w - 24) / 4;
    for (int i = 0; i < 4; ++i) s_items.push_back({ colours[i], R(t.x + 12 + i * quarter, t.y + 86, quarter - 4, 28) });

    s_items.push_back({ ResetAll, R(24, s_bar + 16, 150, 32) });
    s_items.push_back({ SaveButton, R(s_w - 124, s_bar + 16, 100, 32) });
}

// Fits the layout to an inside of w x h pixels: tall or wide, whichever comes out larger, as large as
// both directions allow, then stretched. So a window made wider has larger text, not just longer sliders.
void Layout(int w, int h)
{
    const int tall = std::min(MulDiv(96, w, kMinWidth), MulDiv(96, h, kHeight));
    const int wide = std::min(MulDiv(96, w, kWideMinWidth), MulDiv(96, h, kWideHeight));
    s_wide = wide > tall;
    const int dpi = std::max(24, std::max(tall, wide));
    if (dpi != s_dpi || !s_font) {
        s_dpi = dpi;
        for (HFONT font : { s_font, s_bold, s_head, s_small }) {
            if (font) DeleteObject(font);
        }
        s_font = MakeFont(S(15));
        s_bold = MakeFont(S(15), FW_SEMIBOLD);
        s_head = MakeFont(S(20), FW_SEMIBOLD);
        s_small = MakeFont(S(13));
    }
    s_w = std::max(s_wide ? kWideMinWidth : kMinWidth, MulDiv(w, 96, dpi));
    s_h = std::max(s_wide ? kWideHeight : kHeight, MulDiv(h, 96, dpi));
    Arrange();
    Redraw();
}

SIZE FrameExtra()
{
    RECT frame{};
    AdjustWindowRectExForDpi(&frame, kStyle, FALSE, 0, s_baseDpi);
    return { frame.right - frame.left, frame.bottom - frame.top };
}

const RECT* Find(Id id)
{
    for (const Item& item : s_items) {
        if (item.id == id) return &item.r;
    }
    return nullptr;
}

Id ItemAt(POINT at)
{
    for (const Item& item : s_items) {
        if (PtInRect(&item.r, at)) return item.id;
    }
    return None;
}

// ---------------------------------------------------------------- what each control stands for
bool IsKey(Id id) { return id >= KeyNone && id <= KeyAlt; }
bool IsSpan(Id id) { return id >= SpanOff && id <= SpanAlt; }
bool IsSlider(Id id) { return id == Opacity || id == Gap || id == Padding || id == Fade; }
bool IsTheme(Id id) { return id >= ThemeSlate && id <= ThemeMono; }

// Ready-made colours: the dark slate Tessera starts with, the frog's sky blue and navy, the gold of the
// administrator's toad, and plain greys.
struct Theme {
    const wchar_t* name;
    unsigned zone, highlight, border, text;
};
const Theme kThemes[] = {
    { L"Slate", 0x22262E, 0x0078D7, 0x96A0AF, 0xFFFFFF },
    { L"Frog", 0x1C4896, 0x4FA9E8, 0xFFFFFF, 0xFFFFFF },
    { L"Gold", 0x4A3508, 0xF4C448, 0xFFE07A, 0xFFFFFF },
    { L"Mono", 0x2A2A2A, 0x8C8C8C, 0xE0E0E0, 0xFFFFFF },
};
bool IsColour(Id id) { return id >= ColourZone && id <= ColourText; }
bool IsButton(Id id) { return id == ResetAll || id == SaveButton; }

// The ticks. Start with Windows stands for a switch of Windows' own, which is only written on Save.
bool* Flag(Id id)
{
    switch (id) {
    case RightClick: return &g_config.rightClickToggles;
    case PauseSnap: return &g_config.pauseWindowsSnap;
    case ShowNumbers: return &g_config.showNumbers;
    case ShowLabel: return &g_config.showLabel;
    case Startup: return &s_startup;
    default: return nullptr;
    }
}

const wchar_t* Label(Id id)
{
    switch (id) {
    case KeyNone: case SpanOff: return L"Off";
    case KeyShift: case SpanShift: return L"Shift";
    case KeyCtrl: case SpanCtrl: return L"Ctrl";
    case KeyAlt: case SpanAlt: return L"Alt";
    case RightClick: return L"Right-click while dragging shows the zones";
    case Startup: return L"Start with Windows";
    case PauseSnap: return L"Keep Windows' own snapping and snap bar out of the way";
    case ThemeSlate: case ThemeFrog: case ThemeGold: case ThemeMono: return kThemes[id - ThemeSlate].name;
    case ShowNumbers: return L"Show a number in each zone";
    case ShowLabel: return L"Show the preset's name";
    case ColourZone: return L"Zone";
    case ColourHighlight: return L"Highlight";
    case ColourBorder: return L"Border";
    case ColourText: return L"Number";
    case ResetAll: return L"Reset to default";
    case SaveButton: return L"Save";
    default: return L"";
    }
}

unsigned* Colour(Id id)
{
    switch (id) {
    case ColourZone: return &g_config.zoneColour;
    case ColourHighlight: return &g_config.highlightColour;
    case ColourBorder: return &g_config.borderColour;
    case ColourText: return &g_config.textColour;
    default: return nullptr;
    }
}

ActivationKey KeyOf(Id id)
{
    return id == KeyShift || id == SpanShift ? ActivationKey::Shift : id == KeyCtrl || id == SpanCtrl ? ActivationKey::Ctrl :
           id == KeyAlt || id == SpanAlt ? ActivationKey::Alt : ActivationKey::None;
}

struct Range {
    int* value;
    int low, high;
};
Range Slider(Id id)
{
    if (id == Opacity) return { &g_config.opacity, 5, 95 };
    if (id == Padding) return { &g_config.padding, 0, 24 };
    if (id == Fade) return { &g_config.fade, 0, 200 };
    return { &g_config.gap, 0, 40 };
}

void SlideTo(Id id, int x)
{
    const RECT* r = Find(id);
    if (!r) return;
    const Range range = Slider(id);
    const int knob = S(7), span = static_cast<int>(r->right - r->left) - 2 * knob;
    const int at = std::clamp(x - static_cast<int>(r->left) - knob, 0, span);
    int value = range.low + MulDiv(at, range.high - range.low, std::max(1, span));
    if (id == Fade) value = (value + 5) / 10 * 10; // in tens of milliseconds
    if (value == *range.value) return;
    *range.value = value;
    Redraw();
    // Drawn now, not when the mouse next pauses: Windows puts mouse moves ahead of repainting, so on a
    // fast mouse the handle, its figure and the preview would otherwise trail behind the hand.
    UpdateWindow(s_window);
}

// A control that would do nothing is not lit by the mouse.
bool Enabled(Id id) { return id != None && (id != SaveButton || SaveLit()); }

// ---------------------------------------------------------------- drawing
// Round the keyboard's control: drawn first, with the control itself on top.
void Ring(HDC dc, Id id, const RECT& r, int radius)
{
    if (!s_focusShown || id != s_focus) return;
    RECT ring = r;
    InflateRect(&ring, S(3), S(3));
    RoundBox(dc, ring, radius + S(3), IsButton(id) ? kBar : kCard, RGB(140, 200, 255), std::max(2, S(2)));
}

// A small desktop with the zones over it, exactly as a drag draws them, and in the proportions of the
// screen the window is on: as large as fits the room it has, and centred there.
void PaintPreview(HDC dc)
{
    const HMONITOR monitor = MonitorFromWindow(s_window, MONITOR_DEFAULTTONEAREST);
    const RECT area = WorkArea(monitor);
    const int screen = std::max(1L, area.right - area.left), screenHeight = std::max(1L, area.bottom - area.top);
    const int roomW = s_preview.right - s_preview.left, roomH = s_preview.bottom - s_preview.top;
    const int w = std::min(roomW, MulDiv(roomH, screen, screenHeight)), h = std::min(roomH, MulDiv(w, screenHeight, screen));
    if (w <= 0 || h <= 0 || !s_desktop.Resize(w, h)) return;
    const int left = s_preview.left + (roomW - w) / 2, top = s_preview.top + (roomH - h) / 2;
    const RECT box = { 0, 0, w, h };
    Fill(s_desktop.dc, box, RGB(46, 84, 128));
    const RECT light = { w * 10 / 100, h * 16 / 100, w * 52 / 100, h * 82 / 100 };
    Fill(s_desktop.dc, light, RGB(236, 238, 242));
    Fill(s_desktop.dc, { light.left, light.top, light.right, light.top + h / 10 }, RGB(205, 210, 220));
    Fill(s_desktop.dc, { w * 60 / 100, h * 30 / 100, w * 92 / 100, h * 92 / 100 }, RGB(40, 42, 50));
    GdiFlush();

    const int gap = g_config.gap > 0 ? std::max(1, MulDiv(g_config.gap, w, screen)) : 0;
    // Shrunk with the rest, but kept visible: any padding at all shows as at least a line.
    const int padding = g_config.padding > 0 ? std::max(2, MulDiv(g_config.padding, w, screen)) : 0;
    OverlayPaintOver(s_desktop.pixels, w, h, PresetForMonitor(monitor), 0, std::max(30, MulDiv(96 * 3, w, screen)), gap, padding);

    BitBlt(dc, left, top, w, h, s_desktop.dc, 0, 0, SRCCOPY);
    RoundOutside(dc, { left, top, left + w, top + h }, S(8), kCard);
}

// strong: the blue one. A button under the mouse is a shade lighter, one being pressed a shade darker.
void PaintButton(HDC dc, Id id, bool strong, bool dim = false)
{
    const RECT r = *Find(id);
    const bool lit = id == s_hot && Enabled(id), down = lit && id == s_pressed;
    const COLORREF fill = strong ? (down ? RGB(0, 96, 178) : lit ? RGB(26, 140, 232) : kAccent)
                                 : (down ? RGB(40, 45, 55) : lit ? RGB(68, 76, 92) : RGB(52, 58, 70));
    const COLORREF edge = strong ? RGB(120, 190, 255) : lit ? RGB(128, 138, 158) : RGB(86, 94, 110);
    Ring(dc, id, r, S(6));
    RoundBox(dc, r, S(6), fill, edge, 1);
    Text(dc, Label(id), r, strong ? s_bold : s_font, dim ? RGB(130, 138, 152) : RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

RECT CheckBox(const RECT& row)
{
    const int size = S(18), top = (row.top + row.bottom - size) / 2;
    return { row.left, top, row.left + size, top + size };
}

void PaintCheck(HDC dc, Id id)
{
    const RECT r = *Find(id);
    const RECT box = CheckBox(r);
    const bool on = *Flag(id), lit = id == s_hot, down = lit && id == s_pressed;
    const int size = box.right - box.left;
    Ring(dc, id, box, S(4));
    RoundBox(dc, box, S(4), on ? (down ? RGB(0, 96, 178) : lit ? RGB(26, 140, 232) : kAccent) : (down ? RGB(14, 15, 19) : lit ? RGB(34, 38, 47) : RGB(20, 22, 27)),
             on ? RGB(120, 190, 255) : lit ? RGB(170, 182, 204) : RGB(110, 120, 140), 1);
    if (on) {
        const HPEN pen = CreatePen(PS_SOLID, std::max(2, S(2)), RGB(255, 255, 255));
        const HGDIOBJ old = SelectObject(dc, pen);
        MoveToEx(dc, box.left + size * 22 / 100, box.top + size * 52 / 100, nullptr);
        LineTo(dc, box.left + size * 42 / 100, box.top + size * 72 / 100);
        LineTo(dc, box.left + size * 80 / 100, box.top + size * 28 / 100);
        SelectObject(dc, old);
        DeleteObject(pen);
    }
    RECT text = r;
    text.left = box.right + S(10);
    Text(dc, Label(id), text, s_font, lit ? RGB(255, 255, 255) : RGB(232, 236, 244), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void PaintSlider(HDC dc, Id id, const wchar_t* label, const std::wstring& value)
{
    const RECT* r = Find(id);
    if (!r) return;
    const Range range = Slider(id);
    const bool lit = id == s_hot || id == s_dragging;
    Text(dc, label, { S(s_look.x + 12), r->top, r->left - S(8), r->bottom }, s_font, RGB(232, 236, 244), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Text(dc, value, { r->right + S(12), r->top, S(s_look.x + s_look.w - 12), r->bottom }, s_font, lit ? RGB(232, 236, 244) : RGB(170, 180, 198), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const int knob = S(7), span = r->right - r->left - 2 * knob, middle = (r->top + r->bottom) / 2;
    const int at = r->left + knob + MulDiv(*range.value - range.low, span, std::max(1, range.high - range.low));
    RoundFill(dc, { r->left, middle - S(2), r->right, middle + S(2) }, S(2), lit ? RGB(74, 82, 98) : RGB(60, 66, 80));
    RoundFill(dc, { r->left, middle - S(2), at, middle + S(2) }, S(2), lit ? RGB(26, 140, 232) : kAccent);
    const RECT handle = { at - knob, middle - S(10), at + knob, middle + S(10) };
    Ring(dc, id, handle, S(5));
    RoundBox(dc, handle, S(5), lit ? RGB(255, 255, 255) : RGB(235, 240, 250), RGB(120, 190, 255), id == s_dragging ? std::max(2, S(2)) : 1);
}

void Paint(HDC dc)
{
    RECT client{};
    GetClientRect(s_window, &client);
    Fill(dc, client, kBackground);
    const COLORREF text = RGB(232, 236, 244);

    // The four sections' cards, then the bar the bottom buttons stand on, right across the window.
    for (const Section* card : { &s_behaviour, &s_look, &s_view, &s_theme }) {
        RoundBox(dc, R(card->x, card->y, card->w, card->h), S(10), kCard, kCardEdge, 1);
    }
    Fill(dc, { 0, S(s_bar), client.right, client.bottom }, kBar);
    Fill(dc, { 0, S(s_bar), client.right, S(s_bar) + 1 }, kBarEdge);
    const UINT left = DT_LEFT | DT_VCENTER | DT_SINGLELINE;
    const auto head = [&](const wchar_t* title, const Section& in, int down) {
        Text(dc, title, R(in.x + 12, in.y + down, 300, 28), s_head, RGB(255, 255, 255), left);
    };

    const Section& b = s_behaviour;
    head(L"Behaviour", b, 8);
    Text(dc, L"Hold a key to show zones", R(b.x + 12, b.y + 48, b.w - 348, 30), s_font, text, left);
    for (Id id : { KeyNone, KeyShift, KeyCtrl, KeyAlt }) PaintButton(dc, id, g_config.key == KeyOf(id));
    // With the zones showing, this key stretches the window across several of them.
    Text(dc, L"Hold a key to span zones", R(b.x + 12, b.y + 86, b.w - 348, 30), s_font, text, left);
    for (Id id : { SpanOff, SpanShift, SpanCtrl, SpanAlt }) PaintButton(dc, id, g_config.spanKey == KeyOf(id));
    for (Id id : { RightClick, Startup, PauseSnap, ShowNumbers, ShowLabel }) PaintCheck(dc, id);
    if (g_config.key != ActivationKey::None && g_config.key == g_config.spanKey) {
        Text(dc, L"The same key shows the zones and spans them, so spanning is off. Pick another key for one of the two.",
             R(b.x + 12, b.y + 128 + 3 * s_pitch, b.w - 24, s_wide ? 36 : 18), s_small, RGB(240, 180, 80), DT_LEFT | DT_WORDBREAK);
    } else if (g_config.key == ActivationKey::None && !g_config.rightClickToggles) {
        // On two lines where the column is too narrow for one.
        Text(dc, L"With these settings the zones can never appear. Turn on right-click or pick a key.",
             R(b.x + 12, b.y + 128 + 3 * s_pitch, b.w - 24, s_wide ? 36 : 18), s_small, RGB(240, 180, 80), DT_LEFT | DT_WORDBREAK);
    }

    head(L"Look", s_look, 10);
    PaintSlider(dc, Opacity, L"Opacity of the zones", std::to_wstring(g_config.opacity) + L" %");
    PaintSlider(dc, Gap, L"Space between windows", g_config.gap ? std::to_wstring(g_config.gap) + L" px" : L"None");
    PaintSlider(dc, Padding, L"Padding between zones", g_config.padding ? std::to_wstring(g_config.padding) + L" px" : L"None");
    PaintSlider(dc, Fade, L"Fade in and out", g_config.fade ? std::to_wstring(g_config.fade) + L" ms" : L"Off");

    head(L"Preview", s_view, 8);
    PaintPreview(dc);

    head(L"Theme", s_theme, 8);
    // Ready-made sets of the four colours below, each with a dot of its highlight colour. The one in use is lit.
    for (Id id : { ThemeSlate, ThemeFrog, ThemeGold, ThemeMono }) {
        const Theme& theme = kThemes[id - ThemeSlate];
        const bool inUse = g_config.zoneColour == theme.zone && g_config.highlightColour == theme.highlight &&
                           g_config.borderColour == theme.border && g_config.textColour == theme.text;
        PaintButton(dc, id, inUse);
        const RECT r = *Find(id);
        const int dot = S(10), middle = (r.top + r.bottom) / 2;
        RoundBox(dc, { r.left + S(10), middle - dot / 2, r.left + S(10) + dot, middle - dot / 2 + dot }, dot / 2, Rgb(theme.highlight), RGB(255, 255, 255), 1);
    }
    for (Id id : { ColourZone, ColourHighlight, ColourBorder, ColourText }) {
        const RECT r = *Find(id);
        const RECT swatch = { r.left, r.top, r.left + S(28), r.bottom };
        const bool lit = id == s_hot;
        Ring(dc, id, swatch, S(5));
        RoundBox(dc, swatch, S(5), Rgb(*Colour(id)), lit ? RGB(255, 255, 255) : RGB(150, 160, 175), lit ? std::max(2, S(2)) : 1);
        Text(dc, Label(id), { swatch.right + S(8), r.top, r.right, r.bottom }, s_font, lit ? RGB(255, 255, 255) : text, left);
    }

    PaintButton(dc, ResetAll, false);
    PaintButton(dc, SaveButton, SaveLit(), !SaveLit()); // dim until something has changed

    // Which Tessera this is, quietly, in the corner beside Save.
    const RECT save = *Find(SaveButton);
    Text(dc, L"Tessera " TESSERA_VERSION_TEXT, { S(190), save.top, save.left - S(16), save.bottom }, s_small, RGB(112, 120, 136), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

// ---------------------------------------------------------------- input
void PickColour(Id id)
{
    static COLORREF custom[16]{};
    unsigned* target = Colour(id);
    CHOOSECOLORW choose{ sizeof(choose) };
    choose.hwndOwner = s_window;
    choose.rgbResult = Rgb(*target);
    choose.lpCustColors = custom;
    choose.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (ChooseColorW(&choose)) *target = FromRgb(choose.rgbResult);
}

void Keep()
{
    const bool keptAway = s_saved.pauseWindowsSnap;
    CopySettings(s_saved, g_config);
    SaveSettings();
    if (s_startup != StartsWithWindows()) SetStartsWithWindows(s_startup);
    // One tick for both halves of keeping Windows out of the way: its snapping is paused while the zones
    // show (Main.cpp), and its bar of layouts at the top of the screen is switched off in Windows itself.
    // The bar is only switched back on by unticking: left unticked, a bar the user turned off in Windows'
    // own settings is theirs, and saving something else here leaves it off.
    if (g_config.pauseWindowsSnap) {
        if (!WindowsSnapBarHidden()) SetWindowsSnapBarHidden(true);
    } else if (keptAway && WindowsSnapBarHidden()) {
        SetWindowsSnapBarHidden(false);
    }
    s_startupWas = s_startup;
}

// What a control does when clicked, or pressed with Space or Enter. Shown at once, here and on the next drag, but only written to disk by Save.
void Activate(Id id)
{
    if (IsKey(id)) {
        g_config.key = KeyOf(id);
    } else if (IsSpan(id)) {
        g_config.spanKey = KeyOf(id);
    } else if (Flag(id)) {
        *Flag(id) = !*Flag(id);
    } else if (IsTheme(id)) {
        const Theme& theme = kThemes[id - ThemeSlate];
        g_config.zoneColour = theme.zone;
        g_config.highlightColour = theme.highlight;
        g_config.borderColour = theme.border;
        g_config.textColour = theme.text;
    } else if (IsColour(id)) {
        PickColour(id);
    } else if (id == ResetAll) {
        CopySettings(g_config, Config{});
        s_startup = false;
    } else if (id == SaveButton) {
        if (!Dirty()) return;
        Keep(); // the window stays open, with Save dim again to show there is nothing left to keep
    }
    Redraw();
}

void OnDown(POINT at)
{
    if (s_focusShown) {
        s_focusShown = false;
        Redraw();
    }
    const Id hit = ItemAt(at);
    if (hit == None) return;
    s_focus = hit;
    SetCapture(s_window);
    if (IsSlider(hit)) {
        s_dragging = hit;
        SlideTo(hit, at.x);
    } else {
        s_hot = s_pressed = hit;
    }
    Redraw();
}

void OnUp(POINT at)
{
    if (s_dragging != None) {
        s_dragging = None;
        ReleaseCapture();
        Redraw();
        return;
    }
    if (s_pressed == None) return;
    const Id id = s_pressed;
    s_pressed = None;
    ReleaseCapture();
    if (ItemAt(at) == id) Activate(id); // may close the window
    else Redraw();
}

void OnMove(POINT at)
{
    if (s_dragging != None) {
        SlideTo(s_dragging, at.x);
        return;
    }
    if (!s_tracking) {
        TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, s_window, 0 };
        s_tracking = TrackMouseEvent(&track) != 0;
    }
    const Id hot = ItemAt(at);
    if (hot == s_hot) return;
    s_hot = hot;
    Redraw();
}

void OnKey(WPARAM key)
{
    if (key == VK_ESCAPE) {
        DestroyWindow(s_window);
        return;
    }
    if (s_items.empty()) return;
    if (key == VK_TAB) {
        int index = -1;
        for (size_t i = 0; i < s_items.size(); ++i) {
            if (s_items[i].id == s_focus) index = static_cast<int>(i);
        }
        const int count = static_cast<int>(s_items.size());
        const bool back = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        index = index < 0 ? (back ? count - 1 : 0) : (index + (back ? count - 1 : 1)) % count;
        s_focus = s_items[index].id;
        s_focusShown = true;
        Redraw();
        return;
    }
    if (!s_focusShown || s_focus == None) return;
    if (IsSlider(s_focus) && (key == VK_LEFT || key == VK_RIGHT)) {
        const Range range = Slider(s_focus);
        const int step = (key == VK_LEFT ? -1 : 1) * ((GetKeyState(VK_SHIFT) & 0x8000) ? 5 : 1) * (s_focus == Fade ? 10 : 1);
        *range.value = std::clamp(*range.value + step, range.low, range.high);
        Redraw();
    } else if (!IsSlider(s_focus) && (key == VK_SPACE || key == VK_RETURN)) {
        Activate(s_focus); // may close the window
    }
}

LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    const POINT at = { static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam)) };
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        const HDC dc = BeginPaint(window, &ps);
        RECT client{};
        GetClientRect(window, &client);
        if (s_canvas.w != client.right || s_canvas.h != client.bottom) s_stale = true;
        if (s_canvas.Resize(client.right, client.bottom)) {
            if (s_stale) {
                Paint(s_canvas.dc);
                s_stale = false;
            }
            BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
                   s_canvas.dc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        }
        EndPaint(window, &ps);
        return 0;
    }
    case WM_SIZING: // dragged by an edge or corner, in any direction
        s_userSized = true;
        break;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        const SIZE extra = FrameExtra();
        const auto side = [](int length, int scale) { return MulDiv(MulDiv(length, s_baseDpi, 96), scale, 100); };
        limits->ptMinTrackSize = { side(kMinWidth, kMinScale) + extra.cx, side(kWideHeight, kMinScale) + extra.cy };
        return 0;
    }
    case WM_DPICHANGED: { // moved to a monitor with different scaling: take the size Windows suggests
        s_baseDpi = HIWORD(wparam);
        const RECT* to = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, to->left, to->top, to->right - to->left, to->bottom - to->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SIZE:
        if (s_window && wparam != SIZE_MINIMIZED) Layout(LOWORD(lparam), HIWORD(lparam));
        return 0;
    case WM_EXITSIZEMOVE:
        // Only a resize by hand changes the remembered size. Working it out on every open would round it
        // a little smaller each time.
        if (s_userSized) {
            s_userSized = false;
            RECT client{};
            GetClientRect(window, &client);
            g_config.settingsWidth = MulDiv(client.right, 96, s_baseDpi);
            g_config.settingsHeight = MulDiv(client.bottom, 96, s_baseDpi);
        }
        return 0;
    case WM_LBUTTONDOWN:
        OnDown(at);
        return 0;
    case WM_MOUSEMOVE:
        OnMove(at);
        return 0;
    case WM_LBUTTONUP:
        OnUp(at);
        return 0;
    case WM_MOUSELEAVE:
        s_tracking = false;
        if (s_hot != None) {
            s_hot = None;
            Redraw();
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (s_dragging != None || s_pressed != None) Redraw();
        s_dragging = None;
        s_pressed = None;
        return 0;
    case WM_KEYDOWN:
        OnKey(wparam);
        return 0;
    case WM_APP + 12: { // for the tests: where a control is, so they need not know the layout
        const RECT* r = Find(static_cast<Id>(wparam));
        if (!r) return 0;
        return lparam ? MAKELONG(r->right, r->bottom) : MAKELONG(r->left, r->top);
    }
    case WM_DESTROY:
        // The X, Esc or anything else that closes the window without Save: back to what was saved. Only this
        // window goes; Tessera stays in the tray. (After Save the two are the same, so nothing changes.)
        CopySettings(g_config, s_saved);
        if (g_config.settingsWidth != s_openedWidth || g_config.settingsHeight != s_openedHeight) SaveSettings(); // the window's size is remembered either way
        DeleteObject(s_font);
        DeleteObject(s_bold);
        DeleteObject(s_head);
        DeleteObject(s_small);
        s_font = s_bold = s_head = s_small = nullptr;
        s_canvas.Free();
        s_desktop.Free();
        s_hot = s_pressed = s_dragging = s_focus = None;
        s_focusShown = s_tracking = false;
        s_window = nullptr;
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

bool SettingsRegister()
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = Proc;
    wc.hInstance = g_instance;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(g_instance, MAKEINTRESOURCEW(AppIconId()));
    return RegisterClassExW(&wc) != 0;
}

HWND SettingsWindow() { return s_window; }
bool SettingsHasUnsaved() { return s_window != nullptr && Dirty(); }

void SettingsMaskUnsaved(Config& config)
{
    if (s_window) CopySettings(config, s_saved);
}

void SettingsOpen()
{
    if (s_window) {
        if (IsIconic(s_window)) ShowWindow(s_window, SW_RESTORE);
        SetForegroundWindow(s_window);
        return;
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    const RECT area = WorkArea(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST));
    s_startup = StartsWithWindows();
    s_startupWas = s_startup;
    s_saved = g_config;
    s_openedWidth = g_config.settingsWidth;
    s_openedHeight = g_config.settingsHeight;

    const HWND window = CreateWindowExW(0, kClass, L"Tessera settings", kStyle, area.left, area.top, 400, 300, nullptr, nullptr, g_instance, nullptr);
    if (!window) return;
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(window, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));

    // At the size it was left at, but never larger than the screen it opens on.
    s_baseDpi = static_cast<int>(GetDpiForWindow(window));
    const SIZE extra = FrameExtra();
    const int inW = std::min(MulDiv(s_openedWidth, s_baseDpi, 96), static_cast<int>(area.right - area.left - extra.cx));
    const int inH = std::min(MulDiv(s_openedHeight, s_baseDpi, 96), static_cast<int>(area.bottom - area.top - extra.cy));
    const int w = inW + extra.cx, h = inH + extra.cy;
    s_window = window;
    Layout(inW, inH);
    SetWindowPos(s_window, nullptr, area.left + (area.right - area.left - w) / 2, area.top + (area.bottom - area.top - h) / 2, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(s_window, SW_SHOW);
    SetForegroundWindow(s_window);
}

void SettingsPaintTo(HDC dc)
{
    if (s_window) Paint(dc);
}

} // namespace tessera
