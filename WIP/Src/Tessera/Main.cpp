// Tessera: hold a key (or right-click) while dragging a window to see zones, drop to snap, 1 to 9 to change preset.
//
// The keyboard and mouse hooks exist only between the start and the end of a window move. Outside a drag
// Tessera sees no keys at all, so a number key can never be taken from another program by a drag that
// "stuck": a timer also ends the drag if the window goes away or stops moving without saying so.
#include "App.h"

#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

namespace tessera {

HINSTANCE g_instance = nullptr;
Config g_config;

namespace {

const wchar_t kMainClass[] = L"TesseraMain";
const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t kExplorerKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_OPEN_EDITOR = WM_APP + 2; // from Tessera.exe run again: wparam 1 opens the editor (--edit), 0 only sees to the tray icon
constexpr UINT WM_TEST_SNAP = WM_APP + 10; // tests only (--isolated): snap the window in wparam to zone lparam
constexpr UINT WM_TEST_OVERLAY = WM_APP + 11; // tests only: wparam 1 shows the zones, 2 with two lit, 0 hides them
bool s_isolated = false;
constexpr UINT_PTR kWatchdog = 1;
enum { IDM_SETTINGS = 100, IDM_EDIT, IDM_EXIT, IDM_ADMIN };
const wchar_t kInstanceName[] = L"Local\\Tessera.SingleInstance";
HANDLE s_instance = nullptr; // held for as long as this is the Tessera that is running

HWND s_main = nullptr;
std::wstring s_exePath, s_configPath;
UINT s_taskbarCreated = 0;
NOTIFYICONDATAW s_tray{};

struct Drag {
    HWND window = nullptr;
    bool keySide[2] = {};       // left and right variant of the activation key
    bool toggled = false;       // by a right-click
    bool cancelled = false;     // Esc: the window goes back where it was, so no snap
    bool dirty = false;         // a preset was picked; save when the drag ends
    bool swallowRightUp = false;
    // Spanning: with the span key held, the window will cover everything from the zone the pointer was in
    // when the key went down to the zone it is in now.
    bool spanning = false;
    bool anchored = false;
    Zone anchor{};
    HMONITOR anchorMonitor = nullptr;
    int anchorPreset = -1;
    bool swallowed[256] = {};   // number keys whose press was taken, so their release is taken too
    // The mouse button has come up: where, and with what held, is settled there and then. Windows says the
    // move is over a moment later, and a key let go or a pointer still moving in between changes nothing.
    bool dropped = false;
    POINT dropAt{};
    HHOOK keyboard = nullptr, mouse = nullptr;
};
Drag s_drag;

// ---------------------------------------------------------------- the tray icon, drawn here
HICON MakeIcon(int size)
{
    BITMAPINFO info{};
    info.bmiHeader = { sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB };
    DWORD* pixels = nullptr;
    const HBITMAP colour = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
    if (!colour) return nullptr;

    const int margin = size / 8, gap = std::max(1, size / 16), split = size * 58 / 100;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            DWORD pixel = 0;
            if (y >= margin && y < size - margin) {
                if (x >= margin && x < split - gap) pixel = 0xFF0078D7;                 // the wide zone, accent blue
                else if (x >= split + gap && x < size - margin) pixel = 0xFFE6ECF5;     // the narrow one
            }
            pixels[y * size + x] = pixel;
        }
    }
    const HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO icon{ TRUE, 0, 0, mask, colour };
    const HICON result = CreateIconIndirect(&icon);
    DeleteObject(mask);
    DeleteObject(colour);
    return result;
}

void AddTrayIcon()
{
    if (s_tray.hIcon) DestroyIcon(s_tray.hIcon); // Explorer restarted: the old one is no use to anyone
    s_tray = { sizeof(s_tray) };
    s_tray.hWnd = s_main;
    s_tray.uID = 1;
    s_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    s_tray.uCallbackMessage = WM_TRAY;
    s_tray.hIcon = static_cast<HICON>(LoadImageW(g_instance, MAKEINTRESOURCEW(AppIconId()), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                                 GetSystemMetrics(SM_CYSMICON), 0));
    if (!s_tray.hIcon) s_tray.hIcon = MakeIcon(GetSystemMetrics(SM_CXSMICON));
    wcscpy_s(s_tray.szTip, IsElevated() ? L"Tessera (Administrator)" : L"Tessera");
    Shell_NotifyIconW(NIM_ADD, &s_tray);
}

// ---------------------------------------------------------------- the tray menu
// Drawn here, the way Helios draws its own: a flat dark surface, roomy items, a rounded highlight under
// the pointer, a small line icon beside each item and a quiet line before Close.
enum class MenuIcon { None, Gear, Zones, Shield, Quit };
struct MenuEntry {
    UINT id; // 0: a separator
    const wchar_t* label;
    MenuIcon icon;
    bool quiet = false; // says something, but can't be clicked
};
// The administrator item reads "Restart as Administrator", or once that is so, "Running as Administrator".
MenuEntry kMenu[] = {
    { IDM_SETTINGS, L"Settings", MenuIcon::Gear },
    { IDM_EDIT, L"Edit Zones", MenuIcon::Zones },
    { IDM_ADMIN, L"Restart as Administrator", MenuIcon::Shield },
    { 0, nullptr, MenuIcon::None },
    { IDM_EXIT, L"Close", MenuIcon::Quit },
};
constexpr COLORREF kMenuSurface = RGB(44, 44, 44), kMenuHover = RGB(61, 61, 61), kMenuText = RGB(242, 242, 242), kMenuLine = RGB(70, 70, 70), kMenuQuiet = RGB(140, 140, 140);
int s_menuDpi = 96;

int M(int v) { return MulDiv(v, s_menuDpi, 96); }
SIZE MenuItemSize(const MenuEntry& entry) { return { M(236), entry.id ? M(36) : M(9) }; }

// Drawn four times too large and shrunk, which smooths the lines: plain GDI has no anti-aliasing.
void PaintMenuIcon(HDC dc, int x, int y, int size, MenuIcon icon, COLORREF background, COLORREF colour)
{
    constexpr int s = 64;
    const HDC memory = CreateCompatibleDC(dc);
    const HBITMAP bitmap = CreateCompatibleBitmap(dc, s, s);
    const HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    Fill(memory, { 0, 0, s, s }, background);

    const LOGBRUSH ink{ BS_SOLID, colour, 0 };
    const HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND, 5, &ink, 0, nullptr);
    const HGDIOBJ oldPen = SelectObject(memory, pen);
    const HGDIOBJ oldBrush = SelectObject(memory, GetStockObject(NULL_BRUSH));
    const auto at = [](double f) { return static_cast<int>(f * s + 0.5); };
    const auto line = [&](double x1, double y1, double x2, double y2) {
        MoveToEx(memory, at(x1), at(y1), nullptr);
        LineTo(memory, at(x2), at(y2));
    };

    switch (icon) {
    case MenuIcon::Gear: // a ring with eight short teeth
        for (int i = 0; i < 8; ++i) {
            const double a = 6.283185307 * i / 8;
            line(0.5 + std::cos(a) * 0.30, 0.5 + std::sin(a) * 0.30, 0.5 + std::cos(a) * 0.43, 0.5 + std::sin(a) * 0.43);
        }
        Ellipse(memory, at(0.24), at(0.24), at(0.76), at(0.76));
        Ellipse(memory, at(0.41), at(0.41), at(0.59), at(0.59));
        break;
    case MenuIcon::Zones: // a screen split in two, the right half split again
        Rectangle(memory, at(0.10), at(0.18), at(0.90), at(0.82));
        line(0.56, 0.18, 0.56, 0.82);
        line(0.56, 0.50, 0.90, 0.50);
        break;
    case MenuIcon::Shield: { // what Windows marks administrator things with
        const POINT outline[] = { { at(0.50), at(0.10) }, { at(0.82), at(0.22) }, { at(0.78), at(0.58) },
                                  { at(0.50), at(0.90) }, { at(0.22), at(0.58) }, { at(0.18), at(0.22) } };
        Polygon(memory, outline, 6);
        break;
    }
    case MenuIcon::Quit: // the power mark: a ring open at the top, and a stroke through the gap
        Arc(memory, at(0.18), at(0.20), at(0.82), at(0.84), at(0.5 + 0.32 * std::cos(-2.2689)), at(0.52 + 0.32 * std::sin(-2.2689)),
            at(0.5 + 0.32 * std::cos(-0.8727)), at(0.52 + 0.32 * std::sin(-0.8727)));
        line(0.5, 0.10, 0.5, 0.48);
        break;
    default:
        break;
    }

    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchBlt(dc, x, y, size, size, memory, 0, 0, s, s, SRCCOPY);
    SelectObject(memory, oldBrush);
    SelectObject(memory, oldPen);
    SelectObject(memory, oldBitmap);
    DeleteObject(pen);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

void PaintMenuItem(HDC dc, const RECT& r, const MenuEntry& entry, bool selected)
{
    Fill(dc, r, kMenuSurface);
    if (!entry.id) {
        const int y = (r.top + r.bottom) / 2;
        Fill(dc, { r.left + M(12), y, r.right - M(12), y + 1 }, kMenuLine);
        return;
    }
    if (entry.quiet) selected = false;
    if (selected) {
        const HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        const HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(DC_BRUSH));
        SetDCBrushColor(dc, kMenuHover);
        RoundFill(dc, { r.left + M(4), r.top + M(2), r.right - M(4), r.bottom - M(2) }, M(6), kMenuHover);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
    }
    const int icon = M(16);
    const COLORREF colour = entry.quiet ? kMenuQuiet : kMenuText;
    PaintMenuIcon(dc, r.left + M(16), (r.top + r.bottom - icon) / 2, icon, entry.icon, selected ? kMenuHover : kMenuSurface, colour);
    const HFONT font = MakeFont(M(15));
    Text(dc, entry.label, { r.left + M(44), r.top, r.right - M(12), r.bottom }, font, colour, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(font);
}

void ShowTrayMenu()
{
    s_menuDpi = static_cast<int>(GetDpiForSystem());
    const HMENU menu = CreatePopupMenu();
    for (MenuEntry& entry : kMenu) {
        if (entry.id == IDM_ADMIN) {
            entry.quiet = IsElevated();
            entry.label = entry.quiet ? L"Running as Administrator" : L"Restart as Administrator";
        }
        AppendMenuW(menu, MF_OWNERDRAW | (entry.id && !entry.quiet ? 0 : MF_DISABLED), entry.id, reinterpret_cast<LPCWSTR>(&entry));
    }
    static const HBRUSH surface = CreateSolidBrush(kMenuSurface);
    MENUINFO look{ sizeof(look) };
    look.fMask = MIM_BACKGROUND | MIM_STYLE;
    look.hbrBack = surface;
    look.dwStyle = MNS_NOCHECK; // no empty column for tick marks
    SetMenuInfo(menu, &look);

    POINT at{};
    GetCursorPos(&at);
    SetForegroundWindow(s_main); // or the menu won't close on a click elsewhere
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, at.x, at.y, 0, s_main, nullptr);
    PostMessageW(s_main, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

// ---------------------------------------------------------------- as administrator
// Windows doesn't let a program move the windows of one running as administrator unless it is one itself.
// So Tessera can start itself again that way: Windows asks first, and saying no leaves everything as it was.
// True, with that window brought forward, when there is work in the editor or in settings that leaving
// now would lose. Closing or restarting Tessera then waits until it has been saved or given up.
bool UnsavedWorkKeepsTesseraOpen()
{
    if (EditorHasUnsaved()) {
        EditorOpen();
        return true;
    }
    if (SettingsHasUnsaved()) {
        SettingsOpen();
        return true;
    }
    return false;
}

void RestartAsAdministrator()
{
    if (IsElevated()) return;
    if (UnsavedWorkKeepsTesseraOpen()) return;
    SHELLEXECUTEINFOW run{ sizeof(run) };
    run.fMask = SEE_MASK_NOASYNC;
    run.lpVerb = L"runas";
    run.lpFile = s_exePath.c_str();
    run.lpParameters = L"--relaunched";
    run.nShow = SW_SHOWNORMAL;
    // The name is kept while Windows asks: let go of before, a Tessera started meanwhile would have taken
    // it, and after a "no" there would be two. The new one waits a few seconds for this one to leave.
    if (!ShellExecuteExW(&run)) return; // declined: carry on as before
    if (s_instance) {
        CloseHandle(s_instance);
        s_instance = nullptr;
    }
    DestroyWindow(s_main);
}

// ---------------------------------------------------------------- snapping
RECT FrameBounds(HWND window)
{
    RECT frame{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) GetWindowRect(window, &frame);
    return frame;
}

// Puts the visible edges of a window on target. Windows 10 and 11 windows have invisible resize borders
// outside what is seen, so the window's own rectangle has to be that much larger.
void SnapWindow(HWND window, const RECT& target)
{
    if (IsZoomed(window)) ShowWindow(window, SW_RESTORE);
    for (int pass = 0; pass < 2; ++pass) { // twice: the borders change size when the move crosses to another DPI
        RECT outer{};
        GetWindowRect(window, &outer);
        const RECT seen = FrameBounds(window);
        if (pass == 1 && EqualRect(&seen, &target)) break;
        const RECT place = { target.left - (seen.left - outer.left), target.top - (seen.top - outer.top),
                             target.right + (outer.right - seen.right), target.bottom + (outer.bottom - seen.bottom) };
        SetWindowPos(window, nullptr, place.left, place.top, place.right - place.left, place.bottom - place.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

// Where a window dropped on a zone goes.
RECT PlaceFor(const Zone& zone, const RECT& area)
{
    RECT place = SnapRect(zone, area, g_config.gap);
    // A small zone on a small screen with a large gap: the gap gives way, not the window.
    if (place.right - place.left < 120 || place.bottom - place.top < 60) place = ZoneRect(zone, area);
    return place;
}

// ---------------------------------------------------------------- a drag in progress
bool ZonesActive()
{
    const bool held = s_drag.keySide[0] || s_drag.keySide[1];
    return held ^ s_drag.toggled;
}

struct Target {
    HMONITOR monitor = nullptr;
    int preset = 0;
    int zone = -1; // under the pointer
    Zone span{};   // what a drop would fill: that zone, or with the span key held, the box around it and the anchor
    RECT area{};
};

// The key that spans zones, as chosen in Settings. One key can't both show the zones and span them:
// showing wins, and Settings says so.
bool IsSpanKey(DWORD vk)
{
    if (g_config.spanKey == g_config.key) return false;
    switch (g_config.spanKey) {
    case ActivationKey::Shift: return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT;
    case ActivationKey::Ctrl: return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL;
    case ActivationKey::Alt: return vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU;
    default: return false;
    }
}

Zone Around(const Zone& a, const Zone& b)
{
    return { std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right), std::max(a.bottom, b.bottom) };
}

Target TargetAt(POINT cursor)
{
    Target target;
    target.monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    target.area = WorkArea(target.monitor);
    target.preset = PresetForMonitor(target.monitor);
    const std::vector<Zone>& zones = g_config.presets[target.preset].zones;
    // By the zones' own pixels, as they are drawn and as the highlight is kept: worked out in units, the
    // pixel on a boundary could belong to one zone here and to its neighbour there, and a window dropped on
    // it would land beside the zone that was lit.
    for (size_t i = 0; i < zones.size() && target.zone < 0; ++i) {
        const RECT box = ZoneRect(zones[i], target.area);
        if (PtInRect(&box, cursor)) target.zone = static_cast<int>(i);
    }
    if (target.zone < 0) { // off the work area (over the taskbar): the nearest zone
        const POINT units = ToUnits(cursor, target.area);
        target.zone = ZoneAt(zones, units.x, units.y);
    }
    if (target.zone >= 0) {
        target.span = zones[target.zone];
        if (s_drag.spanning && s_drag.anchored && s_drag.anchorMonitor == target.monitor && s_drag.anchorPreset == target.preset) {
            target.span = Around(s_drag.anchor, target.span);
        }
    }
    return target;
}

// Windows has snapping of its own (drag to an edge or the top, and the bar of layouts): with the zones
// showing, the two would both act on the drop. So it is switched off for exactly as long as the zones show,
// and back on the moment they don't. The change is never written to the user's profile, and a mark that
// outlives a crash lets the next start put it right.
const wchar_t kSnapPausedMark[] = L"Tessera.WindowsSnapPaused";
bool s_snapPaused = false;

void ForgetSnapMark()
{
    while (const ATOM mark = GlobalFindAtomW(kSnapPausedMark)) GlobalDeleteAtom(mark);
}

void PauseWindowsSnap(bool pause)
{
    if (pause == s_snapPaused) return;
    if (pause) {
        if (!g_config.pauseWindowsSnap || s_isolated) return;
        BOOL on = FALSE;
        if (!SystemParametersInfoW(SPI_GETWINARRANGING, 0, &on, 0) || !on) return; // off already: not Tessera's to turn on later
        if (!SystemParametersInfoW(SPI_SETWINARRANGING, FALSE, nullptr, 0)) return;
        GlobalAddAtomW(kSnapPausedMark);
        s_snapPaused = true;
    } else {
        SystemParametersInfoW(SPI_SETWINARRANGING, TRUE, nullptr, 0);
        ForgetSnapMark();
        s_snapPaused = false;
    }
}

// The zone the pointer was last found in. While it stays inside, a mouse move changes nothing and costs nothing.
RECT s_sameZone{};
bool s_sameZoneKnown = false;

// movedTo: the pointer's new place, from a mouse move. Without it everything is worked out afresh.
void UpdateOverlay(const POINT* movedTo = nullptr)
{
    if (!s_drag.window || !ZonesActive()) {
        s_sameZoneKnown = false;
        PauseWindowsSnap(false);
        OverlayHide();
        return;
    }
    POINT cursor{};
    if (movedTo) cursor = *movedTo;
    else GetCursorPos(&cursor);
    if (movedTo && s_sameZoneKnown && PtInRect(&s_sameZone, cursor)) return;

    PauseWindowsSnap(true);
    const Target target = TargetAt(cursor);
    s_sameZoneKnown = target.zone >= 0;
    if (s_sameZoneKnown) s_sameZone = ZoneRect(g_config.presets[target.preset].zones[target.zone], target.area);
    OverlayShow(target.monitor, target.preset, target.zone >= 0 ? &target.span : nullptr);
}

void EndDrag(bool snap)
{
    if (!s_drag.window) return;
    const HWND window = s_drag.window;
    const bool active = ZonesActive() && !s_drag.cancelled;
    const bool dirty = s_drag.dirty;
    // Where the drop goes is worked out now, while what the drag knows (the span) is still known.
    POINT dropAt = s_drag.dropAt;
    if (!s_drag.dropped) GetCursorPos(&dropAt);
    const Target target = TargetAt(dropAt);
    if (s_drag.keyboard) UnhookWindowsHookEx(s_drag.keyboard);
    if (s_drag.mouse) UnhookWindowsHookEx(s_drag.mouse);
    KillTimer(s_main, kWatchdog);
    s_drag = Drag{};
    s_sameZoneKnown = false;
    PauseWindowsSnap(false);
    OverlayHide();

    if (snap && active && IsWindow(window)) {
        if (target.zone >= 0) SnapWindow(window, PlaceFor(target.span, target.area));
    }
    if (dirty) SaveSettings();
}

int ActivationSide(DWORD vk)
{
    switch (g_config.key) {
    case ActivationKey::Shift: return vk == VK_LSHIFT || vk == VK_SHIFT ? 0 : vk == VK_RSHIFT ? 1 : -1;
    case ActivationKey::Ctrl: return vk == VK_LCONTROL || vk == VK_CONTROL ? 0 : vk == VK_RCONTROL ? 1 : -1;
    case ActivationKey::Alt: return vk == VK_LMENU || vk == VK_MENU ? 0 : vk == VK_RMENU ? 1 : -1;
    default: return -1;
    }
}

LRESULT CALLBACK KeyboardHook(int code, WPARAM wparam, LPARAM lparam)
{
    if (code == HC_ACTION && s_drag.window) {
        const DWORD vk = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam)->vkCode;
        const bool down = wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;

        if (s_drag.dropped) { // let go of already: only the release of a key taken earlier is still Tessera's
            if (!down && vk < 256 && s_drag.swallowed[vk]) {
                s_drag.swallowed[vk] = false;
                return 1;
            }
            return CallNextHookEx(nullptr, code, wparam, lparam);
        }
        const int side = ActivationSide(vk);
        if (side >= 0) {
            s_drag.keySide[side] = down;
            UpdateOverlay();
        } else if (IsSpanKey(vk)) {
            if (down && !s_drag.spanning && ZonesActive()) { // the first press, not its repeats: anchor here
                POINT cursor{};
                GetCursorPos(&cursor);
                s_drag.spanning = false;
                const Target here = TargetAt(cursor);
                s_drag.anchored = here.zone >= 0;
                s_drag.anchor = here.span;
                s_drag.anchorMonitor = here.monitor;
                s_drag.anchorPreset = here.preset;
                s_drag.spanning = true;
                UpdateOverlay();
            } else if (!down && s_drag.spanning) {
                s_drag.spanning = s_drag.anchored = false;
                UpdateOverlay();
            }
        } else if (vk == VK_ESCAPE && down) {
            s_drag.cancelled = true;
        } else if (vk < 256) {
            const int preset = vk >= '1' && vk <= '9' ? static_cast<int>(vk - '1') :
                               vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9 ? static_cast<int>(vk - VK_NUMPAD1) : -1;
            if (!down && s_drag.swallowed[vk]) {
                s_drag.swallowed[vk] = false;
                return 1;
            }
            // Only while the zones are showing: with them off, a number is none of Tessera's business.
            if (down && preset >= 0 && ZonesActive()) {
                if (!g_config.presets[preset].Empty()) {
                    POINT cursor{};
                    GetCursorPos(&cursor);
                    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
                    if (PresetForMonitor(monitor) != preset) {
                        SetPresetForMonitor(monitor, preset);
                        s_drag.dirty = true;
                        if (s_drag.spanning) { // the span starts again here, in the preset just picked
                            s_drag.spanning = false;
                            const Target here = TargetAt(cursor);
                            s_drag.anchored = here.zone >= 0;
                            s_drag.anchor = here.span;
                            s_drag.anchorMonitor = here.monitor;
                            s_drag.anchorPreset = here.preset;
                            s_drag.spanning = true;
                        }
                    }
                    UpdateOverlay();
                }
                s_drag.swallowed[vk] = true;
                return 1;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

LRESULT CALLBACK MouseHook(int code, WPARAM wparam, LPARAM lparam)
{
    if (code == HC_ACTION && s_drag.window) {
        if (wparam == WM_RBUTTONUP && s_drag.swallowRightUp) {
            s_drag.swallowRightUp = false;
            return 1;
        }
        if (s_drag.dropped) {
            // Let go of already: nothing more changes until Windows says the move is over.
        } else if (wparam == WM_LBUTTONUP) {
            s_drag.dropped = true;
            s_drag.dropAt = reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam)->pt;
        } else if (wparam == WM_MOUSEMOVE) {
            // The position in the message, not GetCursorPos: inside this hook that still gives the old one.
            UpdateOverlay(&reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam)->pt);
        } else if (wparam == WM_RBUTTONDOWN && g_config.rightClickToggles) {
            s_drag.toggled = !s_drag.toggled;
            s_drag.swallowRightUp = true;
            UpdateOverlay();
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

void BeginDrag(HWND window)
{
    if (s_drag.window || EditorIsOpen()) return;
    // A window inside another (a document in an old-style program) moves within its parent: not for zones,
    // and the program's own window must not be snapped in its place.
    if (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) return;
    window = GetAncestor(window, GA_ROOT);
    if (!window || !IsWindowVisible(window)) return;
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    if ((style & WS_CHILD) || !(style & WS_THICKFRAME)) return; // only windows that can be resized
    // A keyboard move has no button down. Either button: these are the buttons as they are on the mouse, and
    // with them swapped for the left hand it is the right one that drags.
    if (!KeyDown(VK_LBUTTON) && !KeyDown(VK_RBUTTON)) return;

    // A drag by an edge or corner is a resize, not a move.
    POINT cursor{};
    GetCursorPos(&cursor);
    DWORD_PTR hit = 0;
    SetLastError(0);
    if (SendMessageTimeoutW(window, WM_NCHITTEST, 0, MAKELPARAM(cursor.x, cursor.y), SMTO_ABORTIFHUNG, 100, &hit)) {
        if (hit >= HTLEFT && hit <= HTBOTTOMRIGHT) return;
    } else if (GetLastError() == ERROR_ACCESS_DENIED) {
        return; // a program running as administrator: Windows won't let it be moved from here, so offer no zones
    }

    s_drag = Drag{};
    s_drag.window = window;
    switch (g_config.key) {
    case ActivationKey::Shift: s_drag.keySide[0] = KeyDown(VK_LSHIFT); s_drag.keySide[1] = KeyDown(VK_RSHIFT); break;
    case ActivationKey::Ctrl: s_drag.keySide[0] = KeyDown(VK_LCONTROL); s_drag.keySide[1] = KeyDown(VK_RCONTROL); break;
    case ActivationKey::Alt: s_drag.keySide[0] = KeyDown(VK_LMENU); s_drag.keySide[1] = KeyDown(VK_RMENU); break;
    default: break;
    }
    s_drag.keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook, g_instance, 0);
    s_drag.mouse = SetWindowsHookExW(WH_MOUSE_LL, MouseHook, g_instance, 0);
    SetTimer(s_main, kWatchdog, 200, nullptr);
    UpdateOverlay();
}

void CALLBACK WinEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG, DWORD, DWORD)
{
    if (object != OBJID_WINDOW || !window) return;
    if (event == EVENT_SYSTEM_MOVESIZESTART) BeginDrag(window);
    else if (event == EVENT_SYSTEM_MOVESIZEEND && s_drag.window && GetAncestor(window, GA_ROOT) == s_drag.window) EndDrag(true);
}

// The drag is over if the window is gone or its thread has left the move loop, whether or not Windows said so.
void Watchdog()
{
    if (!s_drag.window) return;
    GUITHREADINFO info{ sizeof(info) };
    const bool moving = IsWindow(s_drag.window) && GetGUIThreadInfo(GetWindowThreadProcessId(s_drag.window, nullptr), &info) &&
                        (info.flags & GUI_INMOVESIZE);
    if (!moving) EndDrag(false);
}

LRESULT CALLBACK MainProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == s_taskbarCreated && s_taskbarCreated) {
        AddTrayIcon(); // Explorer restarted
        return 0;
    }
    switch (message) {
    case WM_TRAY:
        if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_CONTEXTMENU) ShowTrayMenu();
        else if (LOWORD(lparam) == WM_LBUTTONUP) EditorOpen();
        return 0;
    case WM_TEST_SNAP:
        if (s_isolated && IsWindow(reinterpret_cast<HWND>(wparam))) {
            const HWND target = reinterpret_cast<HWND>(wparam);
            const HMONITOR monitor = MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
            const std::vector<Zone>& zones = g_config.presets[PresetForMonitor(monitor)].zones;
            // The low half of lparam is the zone; the high half, when not 0, one more than a second zone to span to.
            const size_t first = LOWORD(lparam), second = HIWORD(lparam);
            if (first < zones.size() && second <= zones.size()) {
                SnapWindow(target, PlaceFor(second ? Around(zones[first], zones[second - 1]) : zones[first], WorkArea(monitor)));
            }
        }
        return 0;
    case WM_TEST_OVERLAY:
        if (s_isolated) {
            const HMONITOR monitor = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
            const int preset = PresetForMonitor(monitor);
            const std::vector<Zone>& zones = g_config.presets[preset].zones;
            if (wparam == 0 || zones.empty()) {
                OverlayHide();
            } else {
                const Zone lit = wparam == 2 && zones.size() > 1 ? Around(zones[0], zones[1]) : zones[0];
                OverlayShow(monitor, preset, &lit);
            }
        }
        return 0;
    case WM_OPEN_EDITOR:
        // Tessera.exe run while it is already running: the tray icon is put back in case it was lost, and
        // nothing opens unless the editor was asked for.
        AddTrayIcon();
        if (wparam == 1) EditorOpen();
        return 0;
    case WM_TIMER:
        if (wparam == kWatchdog) Watchdog();
        return 0;
    case WM_MEASUREITEM: {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
        if (measure->CtlType != ODT_MENU || !measure->itemData) break;
        const SIZE size = MenuItemSize(*reinterpret_cast<const MenuEntry*>(measure->itemData));
        measure->itemWidth = size.cx;
        measure->itemHeight = size.cy;
        return TRUE;
    }
    case WM_DRAWITEM: {
        const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
        if (draw->CtlType != ODT_MENU || !draw->itemData) break;
        // Into a picture of Tessera's own, then across: the rounded corners are written straight into the
        // pixels, and what Windows hands a menu to draw on isn't promised to be laid out as those are.
        static Canvas item;
        const RECT& r = draw->rcItem;
        const int w = r.right - r.left, h = r.bottom - r.top;
        const MenuEntry& entry = *reinterpret_cast<const MenuEntry*>(draw->itemData);
        const bool selected = (draw->itemState & ODS_SELECTED) != 0;
        if (item.Resize(w, h)) {
            PaintMenuItem(item.dc, { 0, 0, w, h }, entry, selected);
            BitBlt(draw->hDC, r.left, r.top, w, h, item.dc, 0, 0, SRCCOPY);
        } else {
            PaintMenuItem(draw->hDC, r, entry, selected);
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDM_SETTINGS:
            // Settings saves as it goes, which would also write zone edits not yet saved: finish those first.
            if (EditorIsOpen()) EditorOpen();
            else SettingsOpen();
            break;
        case IDM_EDIT: EditorOpen(); break;
        case IDM_EXIT:
            if (!UnsavedWorkKeepsTesseraOpen()) DestroyWindow(window);
            break;
        case IDM_ADMIN: RestartAsAdministrator(); break;
        }
        return 0;
    case WM_DESTROY:
        // Tessera is leaving. Its other windows are closed properly first, or they would simply vanish with
        // it, and the size the settings window was dragged to would never be written down.
        if (const HWND settings = SettingsWindow()) DestroyWindow(settings);
        if (const HWND editor = EditorWindow()) DestroyWindow(editor);
        EndDrag(false);
        Shell_NotifyIconW(NIM_DELETE, &s_tray);
        if (s_tray.hIcon) DestroyIcon(s_tray.hIcon);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

// ---------------------------------------------------------------- pictures of the interface, for checking it unseen
bool RenderToFile(const std::wstring& path, int width, int height, const std::function<void(HDC)>& paint)
{
    BITMAPINFO info{};
    info.bmiHeader = { sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB };
    void* bits = nullptr;
    const HDC dc = CreateCompatibleDC(nullptr);
    const HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) return false;
    const HGDIOBJ old = SelectObject(dc, bitmap);
    paint(dc);
    GdiFlush();

    BITMAPFILEHEADER header{};
    const DWORD size = static_cast<DWORD>(width) * height * 4;
    header.bfType = 0x4D42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + size;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool ok = file != INVALID_HANDLE_VALUE;
    DWORD written = 0;
    ok = ok && WriteFile(file, &header, sizeof(header), &written, nullptr);
    ok = ok && WriteFile(file, &info.bmiHeader, sizeof(BITMAPINFOHEADER), &written, nullptr);
    ok = ok && WriteFile(file, bits, size, &written, nullptr);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return ok;
}

int Snapshot(const std::wstring& folder)
{
    g_config = DefaultConfig();
    g_config.presets[3] = { L"Three", { { 0, 0, 5000, 10000 }, { 5000, 0, 10000, 4000 }, { 5000, 4000, 10000, 10000 } } };
    EditorOpen();
    if (!EditorWindow()) return 2;
    RECT client{};
    GetClientRect(EditorWindow(), &client);
    bool ok = RenderToFile(folder + L"\\editor.bmp", client.right, client.bottom, [&](HDC dc) {
        EditorPointAt({ client.right / 4, client.bottom * 2 / 3 });
        EditorPaintTo(dc);
    });
    PostMessageW(EditorWindow(), WM_KEYDOWN, '4', 0);
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
    ok = ok && RenderToFile(folder + L"\\editor-preset4.bmp", client.right, client.bottom, [&](HDC dc) {
        EditorPointAt({ client.right / 2, client.bottom / 2 });
        EditorPaintTo(dc);
    });
    // The same with the mouse on a button, and the zones as a drag shows them over a made-up desktop.
    const int editorDpi = static_cast<int>(GetDpiForWindow(EditorWindow()));
    ok = ok && RenderToFile(folder + L"\\editor-hover.bmp", client.right, client.bottom, [&](HDC dc) {
        EditorPointAt({ (client.right - MulDiv(780, editorDpi, 96)) / 2 + MulDiv(16 + 46 + 20, editorDpi, 96), MulDiv(24 + 12 + 18, editorDpi, 96) });
        EditorPaintTo(dc);
    });
    const RECT screen = { 0, 0, 1600, 900 };
    for (const int opacity : { 25, 70 }) {
        g_config.opacity = opacity;
        ok = ok && RenderToFile(folder + (opacity == 25 ? L"\\overlay.bmp" : L"\\overlay-70.bmp"), screen.right, screen.bottom, [&](HDC dc) {
            Fill(dc, screen, RGB(58, 96, 140));
            Fill(dc, { 120, 90, 980, 760 }, RGB(238, 240, 244));
            Fill(dc, { 120, 90, 980, 130 }, RGB(206, 211, 221));
            Fill(dc, { 1040, 300, 1500, 820 }, RGB(38, 40, 48));
            OverlayPaintOver(PixelsOf(dc).bits, screen.right, screen.bottom, 3, 2, 96, 0, g_config.padding);
        });
    }
    g_config.opacity = Config{}.opacity;

    // The tray menu's items as they are drawn, the second one under the pointer.
    s_menuDpi = 144;
    int menuHeight = 0;
    for (const MenuEntry& entry : kMenu) menuHeight += MenuItemSize(entry).cy;
    ok = ok && RenderToFile(folder + L"\\traymenu.bmp", MenuItemSize(kMenu[0]).cx, menuHeight, [&](HDC dc) {
        int y = 0;
        for (const MenuEntry& entry : kMenu) {
            const SIZE size = MenuItemSize(entry);
            PaintMenuItem(dc, { 0, y, size.cx, y + size.cy }, entry, &entry == &kMenu[1]);
            y += size.cy;
        }
    });

    // How long drawing takes, at the size of this screen: a whole editor picture, and a whole set of zones.
    {
        Canvas canvas;
        LARGE_INTEGER rate{}, from{}, to{};
        QueryPerformanceFrequency(&rate);
        const auto timed = [&](int times, const std::function<void()>& work) {
            QueryPerformanceCounter(&from);
            for (int i = 0; i < times; ++i) work();
            GdiFlush();
            QueryPerformanceCounter(&to);
            return 1000.0 * static_cast<double>(to.QuadPart - from.QuadPart) / static_cast<double>(rate.QuadPart) / times;
        };
        if (canvas.Resize(client.right, client.bottom)) {
            const double editor = timed(20, [&] { EditorPaintTo(canvas.dc); });
            double zones = 0, oneZone = 0;
            OverlayMeasure(canvas.w, canvas.h, 3, editorDpi, zones, oneZone);
            FILE* file = nullptr;
            if (_wfopen_s(&file, (folder + L"\\timings.txt").c_str(), L"w") == 0 && file) {
                fprintf(file, "%d x %d pixels\neditor, whole picture: %.1f ms\nzones appearing (all drawn): %.1f ms\nhighlight moving (one zone redrawn, half the screen): %.1f ms\n", canvas.w, canvas.h, editor, zones, oneZone);
                fclose(file);
            }
        }
    }

    g_config.gap = 16;
    SettingsOpen();
    if (!SettingsWindow()) return 4;
    GetClientRect(SettingsWindow(), &client);
    ok = ok && RenderToFile(folder + L"\\settings.bmp", client.right, client.bottom, [&](HDC dc) { SettingsPaintTo(dc); });
    {
        // And how long the settings window takes to draw, which is what every step of a slider costs.
        Canvas canvas;
        LARGE_INTEGER rate{}, from{}, to{};
        QueryPerformanceFrequency(&rate);
        FILE* file = nullptr;
        if (canvas.Resize(client.right, client.bottom) && _wfopen_s(&file, (folder + L"\\timings.txt").c_str(), L"a") == 0 && file) {
            QueryPerformanceCounter(&from);
            for (int i = 0; i < 20; ++i) SettingsPaintTo(canvas.dc);
            GdiFlush();
            QueryPerformanceCounter(&to);
            fprintf(file, "settings, whole picture (%d x %d): %.1f ms\n", canvas.w, canvas.h, 1000.0 * static_cast<double>(to.QuadPart - from.QuadPart) / static_cast<double>(rate.QuadPart) / 20);
            fclose(file);
        }
    }
    return ok ? 0 : 3;
}

} // namespace

// ---------------------------------------------------------------- shared
bool StartsWithWindows()
{
    wchar_t value[MAX_PATH * 2]{};
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, L"Tessera", RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS &&
           std::wstring(value).find(s_exePath) != std::wstring::npos;
}

void SetStartsWithWindows(bool on)
{
    if (s_isolated) return; // a test's private copy never touches Windows' own settings
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (on) {
        const std::wstring command = L"\"" + s_exePath + L"\"";
        RegSetValueExW(key, L"Tessera", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, L"Tessera");
    }
    RegCloseKey(key);
}

bool WindowsSnapBarHidden()
{
    DWORD value = 1, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, kExplorerKey, L"EnableSnapBar", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0; // no value at all means Windows' default, which is to show it
}

void SetWindowsSnapBarHidden(bool hidden)
{
    if (s_isolated) return;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kExplorerKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    const DWORD value = hidden ? 0 : 1;
    RegSetValueExW(key, L"EnableSnapBar", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    // Tell Explorer its settings changed, as the Settings app does.
    SendNotifyMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"WindowsSnapBar"));
}

bool IsElevated()
{
    static int known = -1;
    if (known < 0) {
        known = 0;
        HANDLE token = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            TOKEN_ELEVATION elevation{};
            DWORD size = 0;
            if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated) known = 1;
            CloseHandle(token);
        }
    }
    return known == 1;
}

int AppIconId() { return IsElevated() ? 2 : 1; }

void QuitTessera()
{
    if (s_main) DestroyWindow(s_main);
}

void SaveSettings()
{
    if (s_configPath.empty()) return;
    Config out = g_config;
    EditorMaskUnsaved(out);
    SettingsMaskUnsaved(out);
    SaveConfig(s_configPath, out);
}

std::wstring MonitorName(HMONITOR monitor)
{
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    return GetMonitorInfoW(monitor, &info) ? info.szDevice : L"";
}

RECT WorkArea(HMONITOR monitor)
{
    MONITORINFO info{ sizeof(info) };
    if (!GetMonitorInfoW(monitor, &info)) return { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    return info.rcWork;
}

int PresetForMonitor(HMONITOR monitor)
{
    const auto found = g_config.monitorPreset.find(MonitorName(monitor));
    if (found != g_config.monitorPreset.end() && found->second >= 0 && found->second < kPresetCount &&
        !g_config.presets[found->second].Empty()) return found->second;
    for (int i = 0; i < kPresetCount; ++i) {
        if (!g_config.presets[i].Empty()) return i;
    }
    return 0;
}

void SetPresetForMonitor(HMONITOR monitor, int preset)
{
    const std::wstring name = MonitorName(monitor);
    if (!name.empty() && preset >= 0 && preset < kPresetCount) g_config.monitorPreset[name] = preset;
}

std::wstring PresetTitle(int preset)
{
    if (preset < 0 || preset >= kPresetCount) return L"";
    std::wstring name = g_config.presets[preset].name;
    if (name.empty()) name = AutoName(g_config.presets[preset].zones);
    return name.empty() ? L"Preset " + std::to_wstring(preset + 1) : std::to_wstring(preset + 1) + L"  \x00B7  " + name;
}

} // namespace tessera

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    using namespace tessera;
    g_instance = instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    bool edit = false, isolated = false, relaunched = false;
    std::wstring snapshot;
    int count = 0;
    if (LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 1; i < count; ++i) {
            const std::wstring argument = arguments[i];
            if (argument == L"--edit") edit = true;
            else if (argument == L"--isolated") isolated = true;
            else if (argument == L"--relaunched") relaunched = true;
            else if (argument == L"--snapshot" && i + 1 < count) snapshot = arguments[++i];
        }
        LocalFree(arguments);
    }

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    s_exePath = path;

    if (!EditorRegister() || !OverlayRegister() || !SettingsRegister()) return 1;
    if (!snapshot.empty()) return Snapshot(snapshot);

    // One Tessera at a time. Starting it again opens the editor of the one already running.
    s_isolated = isolated;
    if (!isolated && GlobalFindAtomW(kSnapPausedMark)) { // an earlier Tessera stopped mid-drag
        SystemParametersInfoW(SPI_SETWINARRANGING, TRUE, nullptr, 0);
        ForgetSnapMark();
    }
    if (!isolated) {
        // A Tessera started by "Restart as Administrator" waits a few seconds for the one it replaces to leave.
        for (int tries = 0; !s_instance; ++tries) {
            s_instance = CreateMutexW(nullptr, TRUE, kInstanceName);
            // Taken already; or it can't even be looked at, which is a Tessera running as administrator.
            const bool taken = s_instance ? GetLastError() == ERROR_ALREADY_EXISTS : GetLastError() == ERROR_ACCESS_DENIED;
            if (!taken) break;
            if (s_instance) CloseHandle(s_instance);
            s_instance = nullptr;
            if (!relaunched || tries >= 50) {
                if (const HWND running = FindWindowW(kMainClass, nullptr)) PostMessageW(running, WM_OPEN_EDITOR, edit ? 1 : 0, 0);
                return 0;
            }
            Sleep(100);
        }
    }

    s_configPath = s_exePath.substr(0, s_exePath.find_last_of(L'\\') + 1) + L"Tessera.cfg";
    // No file: the defaults. A file that is there but can't be read just now (held by a sync or backup
    // program as Windows starts) is waited for; if it never opens, Tessera runs on the defaults and saves
    // nothing, so the real file is never replaced by them.
    bool loaded = LoadConfig(s_configPath, g_config);
    const auto there = [] { return GetFileAttributesW(s_configPath.c_str()) != INVALID_FILE_ATTRIBUTES; };
    for (int tries = 0; !loaded && tries < 30 && there(); ++tries) {
        Sleep(100);
        loaded = LoadConfig(s_configPath, g_config);
    }
    if (!loaded) {
        g_config = DefaultConfig();
        if (there()) s_configPath.clear();
    }

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = MainProc;
    wc.hInstance = instance;
    wc.lpszClassName = kMainClass;
    if (!RegisterClassExW(&wc)) return 1;
    s_main = CreateWindowExW(0, kMainClass, L"Tessera", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!s_main) return 1;

    s_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (IsElevated()) { // let through what Windows would otherwise keep from an administrator's window
        ChangeWindowMessageFilterEx(s_main, s_taskbarCreated, MSGFLT_ALLOW, nullptr);
        ChangeWindowMessageFilterEx(s_main, WM_OPEN_EDITOR, MSGFLT_ALLOW, nullptr); // Tessera.exe run again, unelevated
    }
    AddTrayIcon();
    const HWINEVENTHOOK hook = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, nullptr, WinEvent, 0, 0,
                                               WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (edit) EditorOpen();

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (hook) UnhookWinEvent(hook);
    return 0;
}
