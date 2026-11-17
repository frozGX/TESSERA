// The layout editor: a window over the whole work area, so zones are drawn at the size they will have.
//   click a zone            split it left | right at the mouse
//   Shift+click             split it top | bottom
//   drag a line             move it (Shift: pixel by pixel)
//   arrow keys              nudge the line last touched by one pixel (Shift: ten)
//   double-click a line     type where it should stand: 60%, or 1200px for the zone before it
//   right-click a line      remove it
//   right-click elsewhere   close without saving, like Esc (while dragging a line: put the line back)
//   1 to 9                  pick the preset to edit
// Save (or Ctrl+S) keeps the changes; Esc closes and drops everything since the last save. Ctrl+Z takes one back.
//
// The window keeps the picture it last drew and draws a new one only when something it shows has changed,
// so a mouse moving across a zone costs nothing.
#include "App.h"

#include <algorithm>
#include <cwchar>

namespace tessera {

namespace {

const wchar_t kClass[] = L"TesseraEditor";
constexpr int kNameId = 1;

constexpr int kSizeId = 2;

// The buttons in the panel: 0 to 8 are the presets.
enum { kNoButton = -1, kClearButton = kPresetCount, kUndoButton, kSaveButton, kSaveCloseButton, kMonitorButton };

HWND s_window = nullptr;
HWND s_name = nullptr;
WNDPROC s_nameProc = nullptr;
HWND s_size = nullptr; // the box for typing where a line should stand; there only while it is in use
WNDPROC s_sizeProc = nullptr;
RECT s_sizePill{};
bool s_sizeUpright = true;
HMONITOR s_monitor = nullptr;
int s_dpi = 96;
int s_current = 0;

std::vector<Zone> s_work;  // the zones on screen; one whole-screen zone stands in for an empty preset
Divider s_hover;           // the line under the mouse
int s_hoverZone = -1;
POINT s_mouse = { -1, -1 };
bool s_dragging = false;
bool s_settingName = false;
int s_hot = kNoButton;     // the button under the mouse
int s_pressed = kNoButton; // the button the mouse went down on; it acts when the mouse comes up on it

// The line last hovered or dragged, which the arrow keys move. Kept by where it is, not by which zones it
// touches, since those change with every edit.
struct Line {
    bool valid = false;
    Axis axis = Axis::Vertical;
    int position = 0, along = 0;
};
Line s_line;
bool s_nudged = false;   // the arrows just moved it: its sizes show until the mouse moves
bool s_nudgeRun = false; // further presses belong to the same undo step

struct Undo {
    int preset;
    std::vector<Zone> zones;
};
std::vector<Undo> s_undo;
Preset s_saved[kPresetCount]; // the presets as last saved, which Esc goes back to

Canvas s_canvas;     // the picture last drawn
bool s_stale = true; // it no longer shows the current state
HFONT s_fontUi = nullptr, s_fontBold = nullptr, s_fontSmall = nullptr, s_fontKey = nullptr;
HBRUSH s_editBrush = nullptr;
// s_save keeps the changes and stays; s_done closes, dropping whatever was not saved, as Esc does.
RECT s_panel{}, s_presetButton[kPresetCount]{}, s_nameBox{}, s_clear{}, s_undoButton{}, s_monitorButton{}, s_save{}, s_done{}, s_hint{}, s_hintBox{};

// Every monitor, in the order Windows lists them. With more than one, a button in the panel moves the
// editor on to the next.
std::vector<HMONITOR> Monitors()
{
    std::vector<HMONITOR> all;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM list) -> BOOL {
        reinterpret_cast<std::vector<HMONITOR>*>(list)->push_back(monitor);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&all));
    return all;
}

int S(int v) { return MulDiv(v, s_dpi, 96); }

RECT Client()
{
    RECT r{};
    if (s_window) GetClientRect(s_window, &r);
    return r;
}

bool Held(int key) { return (GetKeyState(key) & 0x8000) != 0; }

// Whole percents, or exactly where the mouse is (pixel by pixel) when fine.
int SnapUnits(int v, bool fine) { return fine ? v : ((v + 50) / 100) * 100; }

void Redraw()
{
    s_stale = true;
    if (s_window) InvalidateRect(s_window, nullptr, FALSE);
}

// ---------------------------------------------------------------- the preset being edited
// Whether Save has anything to do: a preset differs from how it was saved, or the one being edited is not
// yet the one this monitor uses.
// Work that would be lost if the editor went away now: a preset no longer as it was saved.
bool PresetsChanged()
{
    for (int i = 0; i < kPresetCount; ++i) {
        if (g_config.presets[i].name != s_saved[i].name || g_config.presets[i].zones != s_saved[i].zones) return true;
    }
    return false;
}

bool Dirty()
{
    return PresetsChanged() || (!g_config.presets[s_current].Empty() && PresetForMonitor(s_monitor) != s_current);
}

// Whether Save is lit. Settled when a line is let go of, not while it is being dragged: mid-drag the answer
// can flip back and forth as the line passes where it started.
bool s_saveLit = false;
bool SaveLit()
{
    if (!s_dragging) s_saveLit = Dirty();
    return s_saveLit;
}

// The name box: the preset's name, or for one without a name its shape ("41% / 59%"), kept up to date.
void ShowName()
{
    if (!s_name) return;
    const Preset& preset = g_config.presets[s_current];
    const std::wstring text = preset.name.empty() ? AutoName(preset.zones) : preset.name;
    wchar_t shown[64]{};
    GetWindowTextW(s_name, shown, 64);
    if (text == shown) return;
    s_settingName = true;
    SetWindowTextW(s_name, text.c_str());
    s_settingName = false;
}

void Commit()
{
    g_config.presets[s_current].zones = s_work;
    ShowName();
}

void PushUndo()
{
    s_undo.push_back({ s_current, g_config.presets[s_current].zones });
    if (s_undo.size() > 200) s_undo.erase(s_undo.begin());
}

void Load()
{
    s_work = g_config.presets[s_current].zones;
    if (s_work.empty()) s_work = { { 0, 0, kUnits, kUnits } };
    s_hover = Divider{};
    s_hoverZone = -1;
    s_line = Line{};
    s_nudged = s_nudgeRun = false;
    if (s_dragging) { // a key pressed mid-drag (a preset number, Ctrl+Z): let go of the mouse, or it stays captured
        s_dragging = false;
        ReleaseCapture();
    }
    ShowName();
    Redraw();
}

void Select(int preset)
{
    if (preset < 0 || preset >= kPresetCount) return;
    s_current = preset;
    Load();
}

void UndoLast()
{
    if (s_undo.empty()) return;
    const Undo last = s_undo.back();
    s_undo.pop_back();
    s_current = last.preset;
    g_config.presets[s_current].zones = last.zones;
    Load();
}

// ---------------------------------------------------------------- where things are
void Arrange()
{
    const RECT client = Client();
    const int width = S(780), height = S(98);
    s_panel.left = (client.right - width) / 2;
    s_panel.top = S(24);
    s_panel.right = s_panel.left + width;
    s_panel.bottom = s_panel.top + height;

    int x = s_panel.left + S(16);
    const int row1 = s_panel.top + S(12);
    for (RECT& button : s_presetButton) {
        button = { x, row1, x + S(40), row1 + S(36) };
        x += S(46);
    }
    s_nameBox = { x + S(8), row1, s_panel.right - S(16), row1 + S(36) };

    const int row2 = s_panel.top + S(60);
    s_clear = { s_panel.left + S(16), row2, s_panel.left + S(16) + S(130), row2 + S(26) };
    s_undoButton = { s_clear.right + S(8), row2, s_clear.right + S(8) + S(90), row2 + S(26) };
    s_done = { s_panel.right - S(16) - S(80), row2, s_panel.right - S(16), row2 + S(26) };
    s_save = { s_done.left - S(8) - S(80), row2, s_done.left - S(8), row2 + S(26) };
    s_monitorButton = {};
    if (Monitors().size() > 1) s_monitorButton = { s_undoButton.right + S(8), row2, s_undoButton.right + S(8) + S(150), row2 + S(26) };
    // The guide has a box of its own, just under the panel.
    s_hintBox = { s_panel.left, s_panel.bottom + S(8), s_panel.right, s_panel.bottom + S(8) + S(58) };
    s_hint = { s_hintBox.left + S(16), s_hintBox.top + S(8), s_hintBox.right - S(16), s_hintBox.bottom - S(8) };

    if (s_name) {
        MoveWindow(s_name, s_nameBox.left + S(8), s_nameBox.top + S(7), s_nameBox.right - s_nameBox.left - S(16),
                   s_nameBox.bottom - s_nameBox.top - S(14), TRUE);
    }
}

int ButtonAt(POINT at)
{
    for (int i = 0; i < kPresetCount; ++i) {
        if (PtInRect(&s_presetButton[i], at)) return i;
    }
    if (PtInRect(&s_clear, at)) return kClearButton;
    if (PtInRect(&s_undoButton, at)) return kUndoButton;
    if (PtInRect(&s_save, at)) return kSaveButton;
    if (PtInRect(&s_done, at)) return kSaveCloseButton;
    if (PtInRect(&s_monitorButton, at)) return kMonitorButton;
    return kNoButton;
}

// A button that would do nothing is neither lit by the mouse nor pressed.
bool Enabled(int button)
{
    if (button == kClearButton) return !g_config.presets[s_current].Empty();
    if (button == kUndoButton) return !s_undo.empty();
    if (button == kSaveButton) return SaveLit();
    return button != kNoButton;
}

// The split a click would make right now. False when the zone is too small to cut there.
bool SplitPreview(Axis& axis, int& position)
{
    if (s_dragging || s_hover.Valid() || s_hoverZone < 0 || s_hoverZone >= static_cast<int>(s_work.size())) return false;
    const POINT units = ToUnits(s_mouse, Client());
    axis = Held(VK_SHIFT) ? Axis::Horizontal : Axis::Vertical;
    position = SnapUnits(axis == Axis::Vertical ? units.x : units.y, Held(VK_CONTROL)); // Shift is taken: it turns the cut
    const Zone& z = s_work[s_hoverZone];
    const int low = (axis == Axis::Vertical ? z.left : z.top) + kMinZone;
    const int high = (axis == Axis::Vertical ? z.right : z.bottom) - kMinZone;
    return position >= low && position <= high;
}

void Remember(const Divider& line)
{
    if (line.Valid()) s_line = { true, line.axis, line.position, (line.from + line.to) / 2 };
}

// The remembered line as it is now, or nothing if an edit has done away with it.
Divider FindLine()
{
    if (!s_line.valid) return {};
    const bool upright = s_line.axis == Axis::Vertical;
    const Divider found = FindDivider(s_work, upright ? s_line.position : s_line.along, upright ? s_line.along : s_line.position, 1, 1);
    return found.Valid() && found.axis == s_line.axis ? found : Divider{};
}

void UpdateHover()
{
    const RECT client = Client();
    if (client.right <= 0 || client.bottom <= 0) return;
    if (PtInRect(&s_panel, s_mouse) || PtInRect(&s_hintBox, s_mouse)) {
        s_hover = Divider{};
        s_hoverZone = -1;
        return;
    }
    const POINT units = ToUnits(s_mouse, client);
    s_hover = FindDivider(s_work, units.x, units.y, std::max(1, MulDiv(S(6), kUnits, client.right)),
                          std::max(1, MulDiv(S(6), kUnits, client.bottom)));
    s_hoverZone = ZoneAt(s_work, units.x, units.y);
    Remember(s_hover);
}

// Everything about the mouse that changes the picture. Moving it only redraws when this differs.
struct Shown {
    int zone = -1, hot = kNoButton, pressed = kNoButton;
    bool line = false, cut = false;
    Axis lineAxis = Axis::Vertical, cutAxis = Axis::Vertical;
    int linePosition = 0, lineFrom = 0, lineTo = 0, cutPosition = 0;
    bool operator==(const Shown&) const = default;
};
Shown s_shown;

Shown Showing()
{
    Shown now;
    now.hot = Enabled(s_hot) ? s_hot : kNoButton;
    now.pressed = s_pressed;
    now.line = s_hover.Valid();
    if (now.line) {
        now.lineAxis = s_hover.axis;
        now.linePosition = s_hover.position;
        now.lineFrom = s_hover.from;
        now.lineTo = s_hover.to;
    } else {
        now.zone = s_hoverZone;
        now.cut = SplitPreview(now.cutAxis, now.cutPosition);
        if (!now.cut) {
            now.cutAxis = Axis::Vertical;
            now.cutPosition = 0;
        }
    }
    return now;
}

// ---------------------------------------------------------------- drawing
std::wstring Percent(int units)
{
    wchar_t text[32];
    if (units % 100 == 0) swprintf_s(text, L"%d%%", units / 100);
    else swprintf_s(text, L"%.2f%%", units / 100.0);
    return text;
}

// The undo mark: an arrow to the left whose tail bends round underneath. Drawn four times too large and
// shrunk, which smooths the lines.
void PaintUndoIcon(HDC dc, int x, int y, int size, COLORREF ink, COLORREF background)
{
    constexpr int s = 64;
    const HDC memory = CreateCompatibleDC(dc);
    const HBITMAP bitmap = CreateCompatibleBitmap(dc, s, s);
    const HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    Fill(memory, { 0, 0, s, s }, background);
    const LOGBRUSH brush{ BS_SOLID, ink, 0 };
    const HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND, 6, &brush, 0, nullptr);
    const HGDIOBJ oldPen = SelectObject(memory, pen);
    const auto at = [](double f) { return static_cast<int>(f * s + 0.5); };

    MoveToEx(memory, at(0.32), at(0.16), nullptr); // the head
    LineTo(memory, at(0.12), at(0.36));
    LineTo(memory, at(0.32), at(0.56));
    MoveToEx(memory, at(0.12), at(0.36), nullptr); // the shaft
    LineTo(memory, at(0.62), at(0.36));
    Arc(memory, at(0.40), at(0.36), at(0.84), at(0.80), at(0.62), at(0.80), at(0.62), at(0.36)); // round the right
    MoveToEx(memory, at(0.62), at(0.80), nullptr); // and back along the bottom
    LineTo(memory, at(0.34), at(0.80));

    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchBlt(dc, x, y, size, size, memory, 0, 0, s, s, SRCCOPY);
    SelectObject(memory, oldPen);
    SelectObject(memory, oldBitmap);
    DeleteObject(pen);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

// strong: the blue one. dim: its words are greyed. A button under the mouse is a shade lighter, and one
// being pressed a shade darker.
void Button(HDC dc, int id, const RECT& r, const std::wstring& label, bool strong, bool dim, bool undoIcon = false)
{
    const bool lit = id == s_hot && Enabled(id), down = lit && id == s_pressed;
    const COLORREF fill = strong ? (down ? RGB(0, 96, 178) : lit ? RGB(26, 140, 232) : kAccent)
                                 : (down ? RGB(40, 45, 55) : lit ? RGB(68, 76, 92) : RGB(52, 58, 70));
    const COLORREF edge = strong ? RGB(120, 190, 255) : lit ? RGB(128, 138, 158) : RGB(86, 94, 110);
    const COLORREF ink = dim ? RGB(130, 138, 152) : RGB(255, 255, 255);
    const HFONT font = strong ? s_fontBold : s_fontUi;
    RoundBox(dc, r, S(6), fill, edge, 1);
    if (!undoIcon) {
        Text(dc, label, r, font, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    // The icon and the word, centred together.
    const HGDIOBJ old = SelectObject(dc, font);
    SIZE text{};
    GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &text);
    SelectObject(dc, old);
    const int icon = S(14), between = S(6);
    const int left = (r.left + r.right - (icon + between + text.cx)) / 2;
    PaintUndoIcon(dc, left, (r.top + r.bottom - icon) / 2, icon, ink, fill);
    Text(dc, label, { left + icon + between, r.top, r.right, r.bottom }, font, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

// The guide: each key in a bright colour, then what it does in a few quiet words. Wraps between pairs.
void PaintHints(HDC dc)
{
    struct Hint {
        const wchar_t* key;
        const wchar_t* what;
    };
    static const Hint hints[] = {
        { L"Click", L"split" },         { L"Shift+Click", L"split across" }, { L"Drag", L"move line" },
        { L"Shift+Drag", L"by pixel" }, { L"Arrows", L"nudge" },             { L"Double-click line", L"type size" },
        { L"Right-click line", L"remove" },
        { L"1\x2013" L"9", L"preset" }, { L"Ctrl+Z", L"undo" },              { L"Ctrl+S", L"save" },
        { L"Esc / Right-click", L"cancel" },
    };
    const auto width = [dc](HFONT font, const wchar_t* text) {
        const HGDIOBJ old = SelectObject(dc, font);
        SIZE size{};
        GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
        SelectObject(dc, old);
        return static_cast<int>(size.cx);
    };

    const int line = S(21), between = S(18), inside = S(6);
    int x = s_hint.left, y = s_hint.top;
    for (const Hint& hint : hints) {
        const int keyWidth = width(s_fontKey, hint.key), whatWidth = width(s_fontSmall, hint.what);
        if (x > s_hint.left && x + keyWidth + inside + whatWidth > s_hint.right) {
            x = s_hint.left;
            y += line;
        }
        Text(dc, hint.key, { x, y, x + keyWidth, y + line }, s_fontKey, RGB(125, 200, 255), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        x += keyWidth + inside;
        Text(dc, hint.what, { x, y, x + whatWidth, y + line }, s_fontSmall, RGB(150, 160, 178), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        x += whatWidth + between;
    }
}

RECT LineRect(const Divider& line, const RECT& client, int half)
{
    const int w = client.right, h = client.bottom;
    return line.axis == Axis::Vertical
        ? RECT{ MulDiv(line.position, w, kUnits) - half, MulDiv(line.from, h, kUnits), MulDiv(line.position, w, kUnits) + half, MulDiv(line.to, h, kUnits) }
        : RECT{ MulDiv(line.from, w, kUnits), MulDiv(line.position, h, kUnits) - half, MulDiv(line.to, w, kUnits), MulDiv(line.position, h, kUnits) + half };
}

// While a line is being moved, by the mouse or the arrows: the size in pixels of what lies either side of
// it, and where it stands as a percentage.
void PaintMeasure(HDC dc, const RECT& client)
{
    if (!s_dragging && !s_nudged) return;
    const Divider line = s_dragging ? s_hover : FindLine();
    if (!line.Valid()) return;
    const bool upright = line.axis == Axis::Vertical;
    const int extent = upright ? client.right : client.bottom;
    const Zone& before = s_work[line.before[0]];
    const Zone& after = s_work[line.after[0]];
    const int at = MulDiv(line.position, extent, kUnits);
    const int first = at - MulDiv(upright ? before.left : before.top, extent, kUnits);
    const int second = MulDiv(upright ? after.right : after.bottom, extent, kUnits) - at;
    const std::wstring text = std::to_wstring(first) + L" px   |   " + std::to_wstring(second) + L" px      " + Percent(line.position);

    const HGDIOBJ old = SelectObject(dc, s_fontKey);
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(dc, old);
    const int w = size.cx + S(28), h = S(30);
    // Beside the mouse while dragging; beside the middle of the line after a nudge.
    POINT anchor = s_mouse;
    if (!s_dragging) {
        const RECT r = LineRect(line, client, 0);
        anchor = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
    }
    RECT pill = { anchor.x + S(18), anchor.y + S(18), anchor.x + S(18) + w, anchor.y + S(18) + h };
    if (pill.right > client.right - S(8)) OffsetRect(&pill, -(w + S(36)), 0);
    if (pill.bottom > client.bottom - S(8)) OffsetRect(&pill, 0, -(h + S(36)));
    RoundBox(dc, pill, S(8), RGB(16, 18, 22), RGB(120, 190, 255), 1);
    Text(dc, text, pill, s_fontKey, RGB(235, 240, 250), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void Paint(HDC dc)
{
    const RECT client = Client();
    const Shown shown = Showing();
    Fill(dc, client, RGB(20, 22, 27));
    const bool empty = g_config.presets[s_current].Empty();

    for (size_t i = 0; i < s_work.size(); ++i) {
        RECT r = ZoneRect(s_work[i], client);
        InflateRect(&r, -S(4), -S(4));
        RoundBox(dc, r, S(8), static_cast<int>(i) == shown.zone ? RGB(52, 60, 76) : RGB(40, 46, 58), RGB(110, 120, 140), S(2));

        const int size = std::max(8, std::clamp(static_cast<int>(std::min(r.right - r.left, r.bottom - r.top)) / 4, S(24), S(120)) & ~3);
        RECT number = r;
        number.bottom -= size / 2;
        Text(dc, empty ? L"\x2013" : std::to_wstring(i + 1), number, CachedFont(size, FW_SEMIBOLD), RGB(235, 240, 250), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        const Zone& z = s_work[i];
        std::wstring detail = Percent(z.right - z.left) + L" \x00D7 " + Percent(z.bottom - z.top) + L"     " +
                              std::to_wstring(r.right - r.left + 2 * S(4)) + L" \x00D7 " + std::to_wstring(r.bottom - r.top + 2 * S(4)) + L" px";
        if (empty) detail = L"This preset is empty. Click to split it into zones.";
        RECT caption = r;
        caption.top = (r.top + r.bottom) / 2 + size / 3;
        caption.bottom = caption.top + S(28);
        if (caption.bottom < r.bottom) Text(dc, detail, caption, s_fontSmall, RGB(170, 180, 198), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // The line under the mouse, the cut a click would make, or the line the arrows have just moved.
    if (shown.line) {
        Fill(dc, LineRect(s_hover, client, S(3)), kAccent);
    } else if (shown.cut) {
        const RECT r = ZoneRect(s_work[s_hoverZone], client);
        const RECT line = shown.cutAxis == Axis::Vertical
            ? RECT{ MulDiv(shown.cutPosition, client.right, kUnits) - 1, r.top + S(6), MulDiv(shown.cutPosition, client.right, kUnits) + 1, r.bottom - S(6) }
            : RECT{ r.left + S(6), MulDiv(shown.cutPosition, client.bottom, kUnits) - 1, r.right - S(6), MulDiv(shown.cutPosition, client.bottom, kUnits) + 1 };
        Fill(dc, line, RGB(120, 190, 255));
    }
    if (!shown.line && s_nudged) {
        const Divider line = FindLine();
        if (line.Valid()) Fill(dc, LineRect(line, client, S(3)), kAccent);
    }

    // The panel.
    RoundBox(dc, s_panel, S(10), RGB(30, 33, 40), RGB(86, 94, 110), 1);
    for (int i = 0; i < kPresetCount; ++i) {
        Button(dc, i, s_presetButton[i], std::to_wstring(i + 1), i == s_current, g_config.presets[i].Empty() && i != s_current);
    }
    RoundBox(dc, s_nameBox, S(6), RGB(20, 22, 27), RGB(86, 94, 110), 1);
    Button(dc, kClearButton, s_clear, L"Clear preset", false, empty);
    Button(dc, kUndoButton, s_undoButton, L"Undo", false, s_undo.empty(), true);
    if (!IsRectEmpty(&s_monitorButton)) {
        const std::vector<HMONITOR> all = Monitors();
        size_t at = 0;
        for (size_t i = 0; i < all.size(); ++i) {
            if (all[i] == s_monitor) at = i;
        }
        Button(dc, kMonitorButton, s_monitorButton, L"Monitor " + std::to_wstring(at + 1) + L" of " + std::to_wstring(all.size()), false, false);
    }

    // The box for typing a line's place: a pill round it, and a word on what may be typed.
    if (s_size) {
        RoundBox(dc, s_sizePill, S(8), RGB(16, 18, 22), RGB(120, 190, 255), 1);
        RoundBox(dc, { s_sizePill.left + S(6), s_sizePill.top + S(5), s_sizePill.left + S(6) + S(92), s_sizePill.bottom - S(5) }, S(5), RGB(20, 22, 27), RGB(86, 94, 110), 1);
        Text(dc, s_sizeUpright ? L"% across, or px for the zone to its left" : L"% down, or px for the zone above it",
             { s_sizePill.left + S(108), s_sizePill.top, s_sizePill.right - S(10), s_sizePill.bottom }, s_fontSmall, RGB(170, 180, 198), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    // Save is dim, and does nothing, until there is something to save; it keeps the editor open, as Ctrl+S
    // does. Close never saves: keeping changes takes Save first, then Close.
    Button(dc, kSaveButton, s_save, L"Save", SaveLit(), !SaveLit());
    Button(dc, kSaveCloseButton, s_done, L"Close", false, false);
    RoundBox(dc, s_hintBox, S(10), RGB(30, 33, 40), RGB(86, 94, 110), 1);
    PaintHints(dc);
    PaintMeasure(dc, client);

    s_shown = shown;
}

// ---------------------------------------------------------------- input
void Save()
{
    if (!g_config.presets[s_current].Empty()) SetPresetForMonitor(s_monitor, s_current);
    for (int i = 0; i < kPresetCount; ++i) s_saved[i] = g_config.presets[i];
    SaveSettings();
    Redraw(); // Save goes dim
}

// keep: write the presets to disk. Otherwise every preset goes back to how it was when last saved.
void Close(bool keep)
{
    if (keep) {
        Save();
    } else {
        for (int i = 0; i < kPresetCount; ++i) g_config.presets[i] = s_saved[i];
    }
    DestroyWindow(s_window);
}

// Sizes everything to the monitor the window is on.
void Rescale()
{
    s_dpi = static_cast<int>(GetDpiForWindow(s_window));
    for (HFONT font : { s_fontUi, s_fontBold, s_fontSmall, s_fontKey }) {
        if (font) DeleteObject(font);
    }
    s_fontUi = MakeFont(S(15));
    s_fontBold = MakeFont(S(15), FW_SEMIBOLD);
    s_fontSmall = MakeFont(S(13));
    s_fontKey = MakeFont(S(13), FW_SEMIBOLD);
    if (s_name) SendMessageW(s_name, WM_SETFONT, reinterpret_cast<WPARAM>(s_fontUi), TRUE);
    Arrange();
    Redraw();
}

// On to the next monitor: the editor moves there and shows the preset that monitor uses. What has been
// changed and not yet saved stays changed; presets belong to every monitor alike.
void NextMonitor()
{
    const std::vector<HMONITOR> all = Monitors();
    if (all.size() < 2) return;
    size_t at = 0;
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i] == s_monitor) at = i;
    }
    s_monitor = all[(at + 1) % all.size()];
    const RECT area = WorkArea(s_monitor);
    SetWindowPos(s_window, nullptr, area.left, area.top, area.right - area.left, area.bottom - area.top, SWP_NOZORDER);
    Rescale();
    Select(PresetForMonitor(s_monitor));
}

// ---------------------------------------------------------------- typing where a line should stand
// A double-click on a line opens a small box beside it. A number with % is where the line stands across (or
// down) the screen; a number with px is the size of the zone to its left (or above it). Enter moves the
// line, as far as the 5 % rule allows; Esc, or clicking anywhere else, leaves it be.
void CloseSizeBox(bool takeFocus)
{
    if (!s_size) return;
    const HWND box = s_size;
    s_size = nullptr; // first: destroying it takes its focus away, which comes back here
    DestroyWindow(box);
    if (takeFocus && s_window) SetFocus(s_window);
    Redraw();
}

void ApplySize()
{
    if (!s_size) return;
    wchar_t text[32]{};
    GetWindowTextW(s_size, text, 32);
    for (wchar_t& c : text) {
        if (c == L',') c = L'.'; // 61,5 as well as 61.5
    }
    wchar_t* end = nullptr;
    const double value = std::wcstod(text, &end);
    const bool typed = end != text;
    // "1200px" is pixels and "60%" a percentage. A bare number is a percentage up to 100; beyond that it
    // can only have been meant as pixels.
    const bool saysPixels = end && (std::wcschr(end, L'p') || std::wcschr(end, L'P'));
    const bool saysPercent = end && std::wcschr(end, L'%');
    const bool pixels = saysPixels || (!saysPercent && value > 100);
    CloseSizeBox(true);

    const Divider line = FindLine();
    if (!typed || !line.Valid()) return;
    const bool upright = line.axis == Axis::Vertical;
    const RECT client = Client();
    const int extent = upright ? client.right : client.bottom;
    if (extent <= 0) return;

    int target;
    if (pixels) {
        const Zone& before = s_work[line.before[0]];
        const int start = MulDiv(upright ? before.left : before.top, extent, kUnits);
        target = MulDiv(start + static_cast<int>(value + 0.5), kUnits, extent);
    } else {
        target = static_cast<int>(value * 100 + 0.5);
    }
    target = std::clamp(target, 0, kUnits);
    if (target == line.position) return;

    PushUndo();
    const int moved = MoveDivider(s_work, line, target);
    if (moved == line.position) {
        s_undo.pop_back();
        MessageBeep(MB_ICONWARNING); // against the 5 % limit already
        return;
    }
    s_line.position = moved;
    s_nudged = true; // its sizes show beside it until the mouse moves
    s_nudgeRun = false;
    Commit();
    UpdateHover();
    Redraw();
}

LRESULT CALLBACK SizeProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if ((message == WM_KEYDOWN || message == WM_CHAR) && (wparam == VK_RETURN || wparam == VK_ESCAPE)) {
        if (message == WM_KEYDOWN) {
            if (wparam == VK_RETURN) ApplySize();
            else CloseSizeBox(true);
        }
        return 0;
    }
    if (message == WM_KILLFOCUS) {
        const LRESULT result = CallWindowProcW(s_sizeProc, window, message, wparam, lparam);
        CloseSizeBox(false); // focus has gone where the user sent it: don't take it back
        return result;
    }
    return CallWindowProcW(s_sizeProc, window, message, wparam, lparam);
}

void OpenSizeBox(POINT at)
{
    if (!s_hover.Valid()) return;
    CloseSizeBox(false);
    Remember(s_hover);
    s_sizeUpright = s_hover.axis == Axis::Vertical;
    s_nudged = s_nudgeRun = false;

    const RECT client = Client();
    const int w = S(370), h = S(34);
    s_sizePill = { at.x + S(14), at.y + S(14), at.x + S(14) + w, at.y + S(14) + h };
    if (s_sizePill.right > client.right - S(8)) OffsetRect(&s_sizePill, -(w + S(28)), 0);
    if (s_sizePill.bottom > client.bottom - S(8)) OffsetRect(&s_sizePill, 0, -(h + S(28)));

    s_size = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_CENTER, s_sizePill.left + S(12), s_sizePill.top + S(8),
                             S(80), h - S(16), s_window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSizeId)), g_instance, nullptr);
    if (!s_size) return;
    SendMessageW(s_size, WM_SETFONT, reinterpret_cast<WPARAM>(s_fontUi), TRUE);
    SendMessageW(s_size, EM_SETLIMITTEXT, 12, 0);
    s_sizeProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(s_size, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SizeProc)));
    wchar_t now[32];
    if (s_hover.position % 100 == 0) swprintf_s(now, L"%d%%", s_hover.position / 100);
    else swprintf_s(now, L"%.2f%%", s_hover.position / 100.0);
    SetWindowTextW(s_size, now);
    SendMessageW(s_size, EM_SETSEL, 0, -1);
    SetFocus(s_size);
    Redraw();
}

// What a button does. The window may be gone when this returns (Close).
void Press(int button)
{
    if (!Enabled(button)) {
        Redraw();
        return;
    }
    if (button == kMonitorButton) {
        NextMonitor();
        return;
    }
    if (button >= 0 && button < kPresetCount) {
        Select(button);
    } else if (button == kClearButton) {
        PushUndo();
        g_config.presets[s_current].zones.clear();
        Load();
    } else if (button == kUndoButton) {
        UndoLast();
    } else if (button == kSaveButton) {
        Save();
    } else if (button == kSaveCloseButton) {
        Close(false);
    }
}

void OnLeftDown(POINT at)
{
    SetFocus(s_window);
    s_mouse = at;
    s_nudged = s_nudgeRun = false;
    if (PtInRect(&s_panel, at)) {
        const int button = ButtonAt(at);
        if (button != kNoButton) {
            s_hot = s_pressed = button;
            SetCapture(s_window);
            Redraw();
        }
        return;
    }
    if (PtInRect(&s_hintBox, at)) return; // the guide is only to be read

    UpdateHover();
    if (s_hover.Valid()) {
        PushUndo();
        s_dragging = true;
        SetCapture(s_window);
        Redraw();
        return;
    }
    Axis axis;
    int position;
    if (SplitPreview(axis, position)) {
        PushUndo();
        if (SplitZone(s_work, s_hoverZone, axis, position)) Commit();
        else s_undo.pop_back();
        UpdateHover();
        Redraw();
    }
}

void OnLeftUp(POINT at)
{
    if (s_pressed != kNoButton) {
        const int button = s_pressed;
        s_pressed = kNoButton;
        ReleaseCapture();
        if (ButtonAt(at) == button) Press(button); // may close the window
        else Redraw();
        return;
    }
    if (s_dragging) {
        s_dragging = false;
        ReleaseCapture();
        // A click on a line that didn't move it leaves nothing to undo.
        if (!s_undo.empty() && s_undo.back().preset == s_current && s_undo.back().zones == g_config.presets[s_current].zones) s_undo.pop_back();
        UpdateHover();
        Redraw();
    }
}

// The second click of a double-click. On a line it opens the box for typing where the line should stand;
// anywhere else it is just another click.
void OnDoubleClick(POINT at)
{
    s_mouse = at;
    if (!PtInRect(&s_panel, at) && !PtInRect(&s_hintBox, at)) {
        UpdateHover();
        if (s_hover.Valid()) {
            OpenSizeBox(at);
            return;
        }
    }
    OnLeftDown(at);
}

void OnMouseMove(POINT at)
{
    const bool moved = at.x != s_mouse.x || at.y != s_mouse.y; // Windows also sends this when nothing moved
    s_mouse = at;
    if (moved) s_nudgeRun = false;
    if (moved && s_nudged) {
        s_nudged = false;
        Redraw();
    }
    if (s_dragging) {
        const POINT units = ToUnits(at, Client());
        // Shift (or Ctrl) while dragging a line: it follows the mouse pixel by pixel instead of in whole percents.
        const bool fine = Held(VK_SHIFT) || Held(VK_CONTROL);
        s_hover.position = MoveDivider(s_work, s_hover, SnapUnits(s_hover.axis == Axis::Vertical ? units.x : units.y, fine));
        s_line.position = s_hover.position;
        Commit();
        Redraw();
        // Drawn now, not when the mouse next pauses: Windows puts mouse moves ahead of repainting, so on a
        // fast mouse the line and its figures would otherwise trail behind the hand.
        UpdateWindow(s_window);
        return;
    }
    s_hot = ButtonAt(at);
    if (s_pressed == kNoButton) UpdateHover();
    if (!(Showing() == s_shown)) Redraw();
}

// A right-click takes back: on a line it removes the line, in the middle of dragging one it puts the line
// back where it was, and anywhere else on the zones it closes the editor without saving, as Esc does.
void OnRightDown(POINT at)
{
    s_mouse = at;
    if (s_dragging) {
        UndoLast(); // the step saved when the drag began; this also lets go of the mouse
        return;
    }
    if (PtInRect(&s_panel, at) || PtInRect(&s_hintBox, at)) return;
    UpdateHover();
    if (!s_hover.Valid()) {
        Close(false);
        return;
    }
    PushUndo();
    if (RemoveDivider(s_work, s_hover)) {
        Commit();
        s_line = Line{};
    } else {
        s_undo.pop_back();
        MessageBeep(MB_ICONWARNING); // the line has more than two zones on it: remove the shorter lines first
    }
    UpdateHover();
    Redraw();
}

// An arrow key: the line last touched moves a pixel that way (ten with Shift). Left and right move upright
// lines, up and down level ones.
void Nudge(WPARAM key)
{
    if (s_dragging) return;
    const Divider line = FindLine();
    if (!line.Valid()) return;
    const bool upright = line.axis == Axis::Vertical;
    const int direction = upright ? (key == VK_LEFT ? -1 : key == VK_RIGHT ? 1 : 0) : (key == VK_UP ? -1 : key == VK_DOWN ? 1 : 0);
    if (!direction) return;
    const RECT client = Client();
    const int extent = upright ? client.right : client.bottom;
    if (extent <= 0) return;

    const int pixel = MulDiv(line.position, extent, kUnits) + direction * (Held(VK_SHIFT) ? 10 : 1);
    int target = MulDiv(pixel, kUnits, extent);
    if (target == line.position) target += direction;
    if (!s_nudgeRun) PushUndo();
    const int moved = MoveDivider(s_work, line, target);
    if (moved == line.position) { // against the 5 % limit
        if (!s_nudgeRun) s_undo.pop_back();
        return;
    }
    s_nudgeRun = s_nudged = true;
    s_line.position = moved;
    Commit();
    UpdateHover();
    Redraw();
    UpdateWindow(s_window); // a held arrow key repeats faster than Windows would otherwise repaint
}

// Enter and Esc in the name box give the keyboard back to the editor.
LRESULT CALLBACK NameProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if ((message == WM_KEYDOWN || message == WM_CHAR) && (wparam == VK_RETURN || wparam == VK_ESCAPE)) {
        if (message == WM_KEYDOWN) SetFocus(s_window);
        return 0;
    }
    const LRESULT result = CallWindowProcW(s_nameProc, window, message, wparam, lparam);
    if (message == WM_KILLFOCUS) ShowName(); // left empty, it goes back to showing the shape
    return result;
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
        const RECT client = Client();
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
    case WM_SIZE:
        Arrange();
        Redraw();
        return 0;
    case WM_LBUTTONDOWN:
        OnLeftDown(at);
        return 0;
    case WM_LBUTTONDBLCLK:
        OnDoubleClick(at);
        return 0;
    case WM_DPICHANGED: // moved to a monitor with different scaling
        if (s_window) Rescale();
        return 0;
    case WM_MOUSEMOVE:
        OnMouseMove(at);
        return 0;
    case WM_LBUTTONUP:
        OnLeftUp(at);
        return 0;
    case WM_CAPTURECHANGED:
        if (s_dragging || s_pressed != kNoButton) Redraw();
        s_dragging = false;
        s_pressed = kNoButton;
        return 0;
    case WM_RBUTTONDOWN:
        OnRightDown(at);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT && reinterpret_cast<HWND>(wparam) == window) {
            LPCWSTR cursor = IDC_ARROW;
            if (s_hover.Valid()) cursor = s_hover.axis == Axis::Vertical ? IDC_SIZEWE : IDC_SIZENS;
            else if (s_hoverZone >= 0) cursor = IDC_CROSS;
            else if (Enabled(s_hot)) cursor = IDC_HAND;
            SetCursor(LoadCursorW(nullptr, cursor));
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
        if (wparam >= '1' && wparam <= '9') Select(static_cast<int>(wparam - '1'));
        else if (wparam >= VK_NUMPAD1 && wparam <= VK_NUMPAD9) Select(static_cast<int>(wparam - VK_NUMPAD1));
        else if (wparam == VK_ESCAPE) Close(false);
        else if (wparam == 'S' && Held(VK_CONTROL)) Save();
        else if (wparam == 'Z' && Held(VK_CONTROL)) UndoLast();
        else if (wparam == 'M') NextMonitor();
        else if (wparam == VK_LEFT || wparam == VK_RIGHT || wparam == VK_UP || wparam == VK_DOWN) Nudge(wparam);
        else if (wparam == VK_SHIFT || wparam == VK_CONTROL) Redraw();
        return 0;
    case WM_KEYUP:
        if (wparam == VK_SHIFT || wparam == VK_CONTROL) Redraw();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == kNameId && HIWORD(wparam) == EN_CHANGE && !s_settingName) {
            wchar_t text[64]{};
            GetWindowTextW(s_name, text, 64);
            g_config.presets[s_current].name = text;
            Redraw(); // Save lights up
        }
        return 0;
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wparam), RGB(255, 255, 255));
        SetBkColor(reinterpret_cast<HDC>(wparam), RGB(20, 22, 27));
        return reinterpret_cast<LRESULT>(s_editBrush);
    case WM_DISPLAYCHANGE: // a monitor came or went: nobody asked to throw the work away
    case WM_CLOSE:
        Close(true);
        return 0;
    case WM_DESTROY:
        DeleteObject(s_fontUi);
        DeleteObject(s_fontBold);
        DeleteObject(s_fontSmall);
        DeleteObject(s_fontKey);
        DeleteObject(s_editBrush);
        s_canvas.Free();
        s_fontUi = s_fontBold = s_fontSmall = s_fontKey = nullptr;
        s_window = nullptr;
        s_name = nullptr;
        s_size = nullptr;
        s_hot = s_pressed = kNoButton;
        s_dragging = false;
        s_undo.clear();
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

bool EditorRegister()
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = Proc;
    wc.hInstance = g_instance;
    wc.lpszClassName = kClass;
    wc.style = CS_DBLCLKS; // a double-click on a line is told apart from two clicks
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(g_instance, MAKEINTRESOURCEW(AppIconId()));
    return RegisterClassExW(&wc) != 0;
}

void EditorMaskUnsaved(Config& config)
{
    if (!s_window) return;
    for (int i = 0; i < kPresetCount; ++i) config.presets[i] = s_saved[i];
}

bool EditorIsOpen() { return s_window != nullptr; }
bool EditorHasUnsaved() { return s_window != nullptr && PresetsChanged(); }
HWND EditorWindow() { return s_window; }

void EditorOpen()
{
    if (s_window) {
        SetForegroundWindow(s_window);
        return;
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    s_monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    const RECT area = WorkArea(s_monitor);

    // WS_CLIPCHILDREN: or each repaint wipes the name box, which then flickers back.
    s_window = CreateWindowExW(WS_EX_TOPMOST, kClass, L"Tessera layouts", WS_POPUP | WS_CLIPCHILDREN, area.left, area.top,
                               area.right - area.left, area.bottom - area.top, nullptr, nullptr, g_instance, nullptr);
    if (!s_window) return;

    s_editBrush = CreateSolidBrush(RGB(20, 22, 27));
    s_name = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 10, 10, s_window,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(kNameId)), g_instance, nullptr);
    Rescale(); // the fonts, at this monitor's scaling
    SendMessageW(s_name, EM_SETLIMITTEXT, 40, 0);
    s_nameProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(s_name, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(NameProc)));

    s_undo.clear();
    s_hot = s_pressed = kNoButton;
    s_mouse = { -1, -1 };
    for (int i = 0; i < kPresetCount; ++i) s_saved[i] = g_config.presets[i];
    s_current = PresetForMonitor(s_monitor);
    Arrange();
    Load();
    ShowWindow(s_window, SW_SHOW);
    SetForegroundWindow(s_window);
    SetFocus(s_window);
}

void EditorPaintTo(HDC dc)
{
    if (s_window) Paint(dc);
}

void EditorPointAt(POINT client)
{
    if (!s_window) return;
    s_mouse = client;
    s_hot = ButtonAt(client);
    UpdateHover();
}

} // namespace tessera
