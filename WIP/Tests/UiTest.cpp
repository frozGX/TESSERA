// Drives a private copy of Tessera through its editor and settings windows by sending them mouse and key
// messages, and checks what ends up in Tessera.cfg. Meant to be started by HiddenRun, so no window of it
// ever reaches the screen in use.
//   UiTest <Tessera.exe> <log file> [seed for the random sessions]
//
// Not covered: dragging a real window (needs real input), and anything that reads the live keyboard
// (Shift and Ctrl held), since sent messages don't change it. It never ticks Start with Windows or the
// snap bar option, and never saves after Reset to default, so it can't touch the registry.
#include <windows.h>

#include <dwmapi.h>

#include "../Src/Tessera/Config.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace {

FILE* s_log = nullptr;
int s_failed = 0, s_checks = 0;
PROCESS_INFORMATION s_tessera{};
std::wstring s_cfgPath;

void Log(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vfprintf(s_log, format, arguments);
    va_end(arguments);
    fputc('\n', s_log);
    fflush(s_log);
}

void Check(bool ok, const char* what)
{
    ++s_checks;
    if (!ok) ++s_failed;
    Log("%s  %s", ok ? "ok    " : "FAILED", what);
}

bool Alive() { return WaitForSingleObject(s_tessera.hProcess, 0) == WAIT_TIMEOUT; }

HWND Find(const wchar_t* windowClass)
{
    for (HWND window = nullptr; (window = FindWindowExW(nullptr, window, windowClass, nullptr)) != nullptr;) {
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (process == s_tessera.dwProcessId) return window;
    }
    return nullptr;
}

HWND WaitFor(const wchar_t* windowClass, bool present)
{
    for (int i = 0; i < 100; ++i) {
        const HWND window = Find(windowClass);
        if ((window != nullptr) == present) return window;
        Sleep(30);
    }
    return Find(windowClass);
}

void Send(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    DWORD_PTR result = 0;
    SendMessageTimeoutW(window, message, wparam, lparam, SMTO_ABORTIFHUNG, 3000, &result);
}

void Move(HWND window, int x, int y, WPARAM buttons = 0) { Send(window, WM_MOUSEMOVE, buttons, MAKELPARAM(x, y)); }
void Down(HWND window, int x, int y) { Send(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y)); }
void Up(HWND window, int x, int y) { Send(window, WM_LBUTTONUP, 0, MAKELPARAM(x, y)); }
void Click(HWND window, int x, int y)
{
    Move(window, x, y);
    Down(window, x, y);
    Up(window, x, y);
}
void Key(HWND window, WPARAM key) { Send(window, WM_KEYDOWN, key, 0); }

std::string Cfg()
{
    std::string text;
    FILE* file = nullptr;
    if (_wfopen_s(&file, s_cfgPath.c_str(), L"rb") != 0 || !file) return text;
    char buffer[4096];
    for (size_t read; (read = fread(buffer, 1, sizeof(buffer), file)) > 0;) text.append(buffer, read);
    fclose(file);
    return text;
}

bool Has(const std::string& line) { return ("\n" + Cfg()).find("\n" + line + "\n") != std::string::npos; }

// The zone lines of one preset, or "" when the preset isn't in the file.
std::string Zones(int preset)
{
    const std::string text = "\n" + Cfg();
    const size_t start = text.find("\npreset=" + std::to_string(preset) + "|");
    if (start == std::string::npos) return "";
    const size_t body = text.find('\n', start + 1);
    const size_t end = text.find("\npreset=", start + 1);
    return text.substr(body, end == std::string::npos ? std::string::npos : end - body);
}

int Count(const std::string& text, const std::string& what)
{
    int count = 0;
    for (size_t at = 0; (at = text.find(what, at)) != std::string::npos; at += what.size()) ++count;
    return count;
}

// ---------------------------------------------------------------- snapping a real window
// Tessera is asked (a message it only answers with --isolated) to put a window of ours into a zone, by the
// same code a drop uses. Run first, while the presets are still the defaults: 50 % | 50 %.
void TestSnap()
{
    Log("-- snapping");
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TesseraUiTestTarget";
    RegisterClassW(&wc);
    const HWND target = CreateWindowExW(0, wc.lpszClassName, L"target", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 200, 200, 500, 400, nullptr, nullptr, wc.hInstance, nullptr);
    Check(target != nullptr, "a window to snap is made");
    if (!target) return;

    RECT area{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    const int middle = area.left + MulDiv(5000, area.right - area.left, 10000);
    const auto seen = [target] { // the window's visible edges, as Tessera measures them
        RECT r{};
        if (FAILED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(target, &r);
        return r;
    };
    const auto is = [&](LONG left, LONG right, const char* what) {
        const RECT r = seen();
        const bool ok = r.left == left && r.right == right && r.top == area.top && r.bottom == area.bottom;
        Check(ok, what);
        if (!ok) Log("        got %ld,%ld - %ld,%ld  wanted %ld,%ld - %ld,%ld", r.left, r.top, r.right, r.bottom, left, area.top, right, area.bottom);
    };
    const HWND main = Find(L"TesseraMain");

    Send(main, WM_APP + 10, reinterpret_cast<WPARAM>(target), 0);
    is(area.left, middle, "a window snaps exactly onto the left zone");
    Send(main, WM_APP + 10, reinterpret_cast<WPARAM>(target), 1);
    is(middle, area.right, "and onto the right zone");
    Send(main, WM_APP + 10, reinterpret_cast<WPARAM>(target), 1);
    is(middle, area.right, "snapping again doesn't move it");

    ShowWindow(target, SW_MAXIMIZE);
    Send(main, WM_APP + 10, reinterpret_cast<WPARAM>(target), 0);
    Check(!IsZoomed(target), "a maximized window is restored first");
    is(area.left, middle, "and then snapped");

    Send(main, WM_APP + 10, reinterpret_cast<WPARAM>(target), MAKELPARAM(0, 2)); // zone 1 through to zone 2
    is(area.left, area.right, "stretched across both zones it fills the screen");
    Send(main, WM_APP + 10, reinterpret_cast<WPARAM>(target), 0);

    Send(main, WM_APP + 10, reinterpret_cast<WPARAM>(target), 7);
    is(area.left, middle, "a zone that doesn't exist is ignored");
    DestroyWindow(target);
    Send(main, WM_APP + 10, reinterpret_cast<WPARAM>(target), 0);
    Check(Alive(), "a window that has gone is ignored");
}

// ---------------------------------------------------------------- the zones themselves
// The window a drag shows: made to appear, to light two zones, and to go again, by a message Tessera only
// answers with --isolated. It can't be seen here; what is checked is that it comes, goes, and nothing breaks.
void TestOverlay()
{
    Log("-- the zones a drag shows");
    const HWND main = Find(L"TesseraMain");
    const HWND overlay = Find(L"TesseraOverlay");
    Check(overlay != nullptr && !IsWindowVisible(overlay), "hidden until a drag wants them");
    Send(main, WM_APP + 11, 1, 0);
    Sleep(400);
    RECT shown{}, area{};
    GetWindowRect(overlay, &shown);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    Check(IsWindowVisible(overlay) && EqualRect(&shown, &area), "they appear, over the whole work area");
    Send(main, WM_APP + 11, 2, 0);
    Sleep(300);
    Send(main, WM_APP + 11, 1, 0);
    Sleep(300);
    Check(Alive() && IsWindowVisible(overlay), "the highlight moves between one zone and two");
    Send(main, WM_APP + 11, 0, 0);
    Send(main, WM_APP + 11, 1, 0); // back before they have finished going
    Sleep(400);
    Check(Alive() && IsWindowVisible(overlay), "called back in the middle of fading out, they stay");
    Send(main, WM_APP + 11, 0, 0);
    Sleep(700);
    Check(Alive() && !IsWindowVisible(overlay), "and they go again");
    for (int i = 0; i < 30; ++i) Send(main, WM_APP + 11, i % 3, 0); // faster than any fade
    Send(main, WM_APP + 11, 0, 0);
    Sleep(700);
    Check(Alive() && !IsWindowVisible(overlay), "shown and hidden faster than they can fade, nothing breaks");
}

// ---------------------------------------------------------------- the editor
struct Editor {
    HWND window = nullptr;
    int w = 0, h = 0, dpi = 96;
    int S(int v) const { return MulDiv(v, dpi, 96); }
    int PanelLeft() const { return (w - S(780)) / 2; }
    int Row2() const { return S(24) + S(60) + S(13); }
    void SaveOnly() const { Click(window, PanelLeft() + S(780) - S(16) - S(80) - S(8) - S(40), Row2()); } // Save, staying open
    void CloseButton() const { Click(window, PanelLeft() + S(780) - S(16) - S(40), Row2()); }             // Close, saving nothing
    void Save() const { SaveOnly(); CloseButton(); }
    void Clear() const { Click(window, PanelLeft() + S(16) + S(65), Row2()); }
    void Undo() const { Click(window, PanelLeft() + S(16) + S(130) + S(8) + S(45), Row2()); }
    int Y() const { return h * 3 / 4; } // well below the panel
    bool Closed() const { return WaitFor(L"TesseraEditor", false) == nullptr; }
    std::wstring Name() const // what the name box shows
    {
        wchar_t text[64]{};
        DWORD_PTR length = 0;
        SendMessageTimeoutW(GetDlgItem(window, 1), WM_GETTEXT, 64, reinterpret_cast<LPARAM>(text), SMTO_ABORTIFHUNG, 3000, &length);
        return text;
    }
};

Editor OpenEditor()
{
    Editor editor;
    PostMessageW(Find(L"TesseraMain"), WM_APP + 2, 1, 0);
    editor.window = WaitFor(L"TesseraEditor", true);
    if (!editor.window) return editor;
    // The window can be found a moment before Tessera has finished setting it up (its name box, the preset
    // loaded). It is shown last, and the message after that is only answered once it is back at its loop.
    for (int i = 0; i < 100 && !IsWindowVisible(editor.window); ++i) Sleep(30);
    Send(editor.window, WM_NULL, 0, 0);
    RECT client{};
    GetClientRect(editor.window, &client);
    editor.w = client.right;
    editor.h = client.bottom;
    editor.dpi = static_cast<int>(GetDpiForWindow(editor.window));
    return editor;
}

void TestEditor()
{
    Log("-- editor");
    Editor e = OpenEditor();
    Check(e.window != nullptr && e.w > 400, "editor opens");
    if (!e.window) return;
    Check(e.Name() == L"50% / 50%", "an unnamed preset shows its shape in the name box");

    Click(e.window, e.w / 4, e.Y());
    Check(e.Name() == L"25% / 25% / 50%", "and the name follows a split");
    Key(e.window, VK_ESCAPE);
    Check(e.Closed(), "Esc closes the editor");
    Check(Zones(1).empty() || Count(Zones(1), "zone=") == 2, "Esc threw the split away");

    // A right-click on a zone is Esc: the editor closes and the split is dropped.
    e = OpenEditor();
    Click(e.window, e.w / 4, e.Y());
    Send(e.window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(e.w * 3 / 4, e.Y()));
    Check(e.Closed(), "a right-click on a zone closes the editor");
    Check(Zones(1).empty() || Count(Zones(1), "zone=") == 2, "and throws the split away");

    // A right-click in the middle of dragging a line puts the line back and leaves the editor open.
    e = OpenEditor();
    Move(e.window, e.w / 2, e.Y());
    Down(e.window, e.w / 2, e.Y());
    Move(e.window, e.w * 70 / 100, e.Y(), MK_LBUTTON);
    Send(e.window, WM_RBUTTONDOWN, MK_RBUTTON | MK_LBUTTON, MAKELPARAM(e.w * 70 / 100, e.Y()));
    Up(e.window, e.w * 70 / 100, e.Y());
    Check(Find(L"TesseraEditor") != nullptr && e.Name() == L"50% / 50%", "a right-click while dragging a line puts it back");
    Key(e.window, VK_ESCAPE);
    e.Closed();

    e = OpenEditor();
    Click(e.window, e.w / 4, e.Y());
    e.Save();
    Check(e.Closed(), "Save closes the editor");
    Check(Count(Zones(1), "zone=") == 3 && Has("zone=0 0 2500 10000"), "Save kept the split at 25 %");

    e = OpenEditor();
    Move(e.window, e.w / 4, e.Y());
    Send(e.window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(e.w / 4, e.Y()));
    e.Save();
    Check(e.Closed() && Count(Zones(1), "zone=") == 2 && Has("zone=0 0 5000 10000"), "right-click removed the line");

    const auto drag = [&](int fromPercent, int toPercent) {
        Move(e.window, e.w * fromPercent / 100, e.Y());
        Down(e.window, e.w * fromPercent / 100, e.Y());
        Move(e.window, e.w * toPercent / 100, e.Y(), MK_LBUTTON);
        Up(e.window, e.w * toPercent / 100, e.Y());
    };
    e = OpenEditor();
    drag(50, 70);
    Key(e.window, VK_ESCAPE);
    Check(e.Closed() && Has("zone=0 0 5000 10000"), "Esc threw the moved line away");

    e = OpenEditor();
    drag(50, 70);
    Check(e.Name() == L"70% / 30%", "and a moved line");
    e.Save();
    Check(e.Closed() && Has("zone=0 0 7000 10000") && Has("zone=7000 0 10000 10000"), "Save kept the line at 70 %");

    e = OpenEditor();
    drag(70, 99);
    e.Save();
    Check(e.Closed() && Has("zone=0 0 9500 10000"), "a line stops 5 % from the edge");

    // A preset key pressed in the middle of dragging a line must not leave the mouse captured.
    e = OpenEditor();
    Move(e.window, e.w * 95 / 100, e.Y());
    Down(e.window, e.w * 95 / 100, e.Y());
    Key(e.window, '2');
    GUITHREADINFO info{ sizeof(info) };
    GetGUIThreadInfo(GetWindowThreadProcessId(e.window, nullptr), &info);
    Check(info.hwndCapture == nullptr, "mouse let go when a preset key interrupts a drag");
    Up(e.window, e.w * 95 / 100, e.Y());
    Key(e.window, VK_ESCAPE);
    Check(e.Closed(), "editor closes after that");

    e = OpenEditor();
    Key(e.window, '5');
    Click(e.window, e.w / 2, e.Y());
    e.Save();
    Check(e.Closed() && Count(Zones(5), "zone=") == 2, "an empty preset can be filled and saved");
    Check(Cfg().find("|5\n") != std::string::npos, "the preset just saved becomes the monitor's");

    e = OpenEditor();
    Key(e.window, '5');
    e.Clear();
    e.Save();
    Check(e.Closed() && Count(Zones(5), "zone=") == 0, "Clear preset empties it");
    Check(Cfg().find("|5\n") == std::string::npos, "and the monitor lets go of the emptied preset");

    // A display changing under the editor: nobody asked to throw the work away, so it is kept.
    e = OpenEditor();
    Key(e.window, '7');
    Click(e.window, e.w / 4, e.Y());
    Send(e.window, WM_DISPLAYCHANGE, 32, MAKELPARAM(e.w, e.h));
    Check(e.Closed() && Count(Zones(7), "zone=") == 2, "a display change keeps unsaved zones and closes the editor");
    e = OpenEditor(); // and preset 7 empty again, as later checks expect to find it
    Key(e.window, '7');
    e.Clear();
    e.Save();
    e.Closed();

    e = OpenEditor();
    Key(e.window, '1');
    Click(e.window, e.w / 4, e.Y());
    Click(e.window, e.w / 8, e.Y());
    e.Undo();
    e.Save();
    Check(e.Closed() && Count(Zones(1), "zone=") == 3, "Undo took the second split back");

    // Save keeps the changes and leaves the editor open; Close closes it and saves nothing.
    e = OpenEditor();
    Key(e.window, '6');
    Click(e.window, e.w / 2, e.Y());
    e.SaveOnly();
    Sleep(300);
    Check(Find(L"TesseraEditor") != nullptr && Count(Zones(6), "zone=") == 2, "Save writes the changes and stays open");
    e.SaveOnly();
    Sleep(200);
    Check(Find(L"TesseraEditor") != nullptr, "and with nothing more to save it does nothing");
    Key(e.window, VK_ESCAPE);
    Check(e.Closed() && Count(Zones(6), "zone=") == 2, "Esc after Save loses nothing");

    // Close on its own drops what was not saved.
    e = OpenEditor();
    Key(e.window, '6');
    Click(e.window, e.w / 4, e.Y());
    e.CloseButton();
    Check(e.Closed() && Count(Zones(6), "zone=") == 2, "Close without Save keeps nothing");

    // With nothing changed, Save is dim and does nothing.
    e = OpenEditor();
    e.SaveOnly();
    Sleep(300);
    Check(Find(L"TesseraEditor") != nullptr, "a dim Save leaves the editor open");
    Key(e.window, VK_ESCAPE);
    Check(e.Closed(), "and Esc still closes it");

    // The arrow keys move the line last touched, a pixel a press.
    e = OpenEditor();
    Key(e.window, '8');
    Click(e.window, e.w / 2, e.Y());
    int expected = 5000;
    for (int i = 0; i < 3; ++i) {
        Key(e.window, VK_RIGHT);
        const int target = MulDiv(MulDiv(expected, e.w, 10000) + 1, 10000, e.w);
        expected = target == expected ? expected + 1 : target;
    }
    Key(e.window, VK_UP); // not this line's direction: nothing
    e.Save();
    Check(e.Closed() && Has("zone=0 0 " + std::to_string(expected) + " 10000"), "the arrow keys nudge a line a pixel at a time");
    Log("        (the line went from 5000 to %d)", expected);

    // A double-click on a line opens a box; what is typed there moves the line: a percentage of the screen,
    // or the size in pixels of the zone before it.
    e = OpenEditor();
    Key(e.window, '9');
    Click(e.window, e.w / 2, e.Y());
    // typeAt: double-click the line at that many pixels across, and type. type: the same, by percentage.
    const auto typeAt = [&](int x, const wchar_t* text) {
        Move(e.window, x, e.Y());
        Send(e.window, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(x, e.Y()));
        Up(e.window, x, e.Y());
        const HWND box = GetDlgItem(e.window, 2);
        if (!box) return false;
        DWORD_PTR result = 0;
        SendMessageTimeoutW(box, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text), SMTO_ABORTIFHUNG, 3000, &result);
        Send(box, WM_KEYDOWN, VK_RETURN, 0);
        return GetDlgItem(e.window, 2) == nullptr;
    };
    const auto type = [&](int atPercent, const wchar_t* text) { return typeAt(e.w * atPercent / 100, text); };
    Check(type(50, L"30%") && e.Name() == L"30% / 70%", "a percentage typed for a line moves it there");
    Check(type(30, L"1000px"), "a size in pixels is taken too");
    e.Save();
    Check(e.Closed() && Has("zone=0 0 " + std::to_string(MulDiv(1000, 10000, e.w)) + " 10000"), "and the zone before it is 1000 pixels wide");

    // A bare number above 100 can only be pixels; and a right-click on a zone with the box open closes the
    // editor without a fuss.
    e = OpenEditor();
    Key(e.window, '9');
    Check(typeAt(1000, L"1500"), "a bare number is taken");
    e.SaveOnly();
    Sleep(200);
    Check(Has("zone=0 0 " + std::to_string(MulDiv(1500, 10000, e.w)) + " 10000"), "and above 100 it is read as pixels");
    {
        const int x = 1500;
        Move(e.window, x, e.Y());
        Send(e.window, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(x, e.Y()));
        Up(e.window, x, e.Y());
        const bool open = GetDlgItem(e.window, 2) != nullptr;
        Send(e.window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(e.w * 3 / 4, e.Y()));
        Check(open && e.Closed() && Alive(), "a right-click with the size box open closes the editor cleanly");
    }

    // Less than the 5 % rule allows: the line goes as far as it may, and no further.
    e = OpenEditor();
    Key(e.window, '9');
    Check(typeAt(1500, L"2%") && e.Name() == L"5% / 95%", "a size too small stops at the 5 % limit");
    Check(type(5, L"nonsense") && e.Name() == L"5% / 95%" && Alive(), "and nonsense typed there changes nothing");
    Key(e.window, VK_ESCAPE);
    e.Closed();

    // Splitting until nothing more fits must stop, not crash: click the same spot many times.
    e = OpenEditor();
    Key(e.window, '6');
    for (int i = 0; i < 40; ++i) Click(e.window, e.w / 2 + i * 7, e.Y());
    for (int i = 0; i < 40; ++i) Click(e.window, 30 + i * e.w / 41, e.h - 40);
    e.Save();
    const int many = Count(Zones(6), "zone=");
    Check(e.Closed() && Alive() && many >= 2 && many <= 20, "splitting over and over stays within the 5 % rule");
    Log("        (preset 6 ended with %d zones)", many);
}

// ---------------------------------------------------------------- settings
// The controls, numbered as Settings.cpp numbers them. The window is asked where each one is, so nothing
// here depends on how it happens to be laid out.
enum SettingsId {
    sKeyNone = 1, sKeyShift, sKeyCtrl, sKeyAlt, sSpanOff, sSpanShift, sSpanCtrl, sSpanAlt, sRightClick, sStartup, sPauseSnap,
    sOpacity, sGap, sPadding, sFade, sShowNumbers, sShowLabel, sThemeSlate, sThemeFrog, sThemeGold, sThemeMono,
    sColourZone, sColourHighlight, sColourBorder, sColourText, sResetAll, sSave,
    sSubOff, sSubShift, sSubCtrl, sSubAlt
};

struct Settings {
    HWND window = nullptr;
    RECT Of(int id) const
    {
        const LRESULT from = SendMessageW(window, WM_APP + 12, id, 0), to = SendMessageW(window, WM_APP + 12, id, 1);
        return { static_cast<short>(LOWORD(from)), static_cast<short>(HIWORD(from)), static_cast<short>(LOWORD(to)), static_cast<short>(HIWORD(to)) };
    }
    void Press(int id) const
    {
        const RECT r = Of(id);
        Click(window, (r.left + r.right) / 2, (r.top + r.bottom) / 2);
    }
    void Save() const { Press(sSave); }
    void Reset() const { Press(sResetAll); }
    void Key(int which) const { Press(sKeyNone + which); } // 0 none, 1 Shift, 2 Ctrl, 3 Alt
    // Clicks a slider at one end or the other.
    void Slide(int id, bool toEnd) const
    {
        const RECT r = Of(id);
        Click(window, toEnd ? r.right - 2 : r.left + 2, (r.top + r.bottom) / 2);
    }
    void Resize(int width, int height) const { SetWindowPos(window, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER); }
    bool Closed() const { return WaitFor(L"TesseraSettings", false) == nullptr; }
    bool CloseByX() const
    {
        PostMessageW(window, WM_CLOSE, 0, 0);
        return Closed();
    }
};

Settings OpenSettings()
{
    Settings settings;
    PostMessageW(Find(L"TesseraMain"), WM_COMMAND, 100, 0); // the tray menu's Settings
    settings.window = WaitFor(L"TesseraSettings", true);
    if (!settings.window) return settings;
    // As with the editor: found a moment before it is ready. Shown last, then back at its loop.
    for (int i = 0; i < 100 && !IsWindowVisible(settings.window); ++i) Sleep(30);
    Send(settings.window, WM_NULL, 0, 0);
    return settings;
}

void TestSettings()
{
    Log("-- settings");
    Settings s = OpenSettings();
    Check(s.window != nullptr, "settings opens");
    if (!s.window) return;

    s.Key(1); // Shift
    Check(s.CloseByX() && Alive(), "the X closes the window and Tessera keeps running");
    Check(Has("key=none"), "the X threw the key change away");

    s = OpenSettings();
    s.Save();
    Sleep(300);
    Check(Find(L"TesseraSettings") != nullptr, "a dim Save leaves settings open");
    s.Key(1);
    s.Save();
    Sleep(300);
    Check(Find(L"TesseraSettings") != nullptr && Has("key=shift"), "Save kept the key and left the window open");
    s.Save(); // dim again: nothing happens
    Check(s.CloseByX() && Has("key=shift"), "and closing it afterwards loses nothing");

    s = OpenSettings();
    s.Slide(sOpacity, true);
    s.Save();
    Check(s.CloseByX() && Has("opacity=95"), "the opacity slider reaches 95 % and saves");
    Check(Cfg().find("padding=") == std::string::npos, "and no padding is written: the zones are always drawn 2 px apart");

    s = OpenSettings();
    s.Press(sSpanOff);
    s.Reset();
    Check(s.CloseByX() && Has("key=shift") && Has("opacity=95") && Has("span=ctrl"), "Reset without Save changes nothing");

    for (int i = 0; i < 4; ++i) {
        s = OpenSettings();
        s.CloseByX();
    }
    Check(Has("settingswidth=1370") && Has("settingsheight=870"), "the remembered size doesn't drift when opened and closed");

    // An unsaved settings change must not ride along when the editor saves.
    s = OpenSettings();
    s.Key(2); // Ctrl, not saved
    Editor e = OpenEditor();
    if (e.window) {
        Key(e.window, '7');
        Click(e.window, e.w / 2, e.Y());
        e.Save();
        Check(e.Closed() && Count(Zones(7), "zone=") == 2 && Has("key=shift"), "the editor's Save left the unsaved key change out");
    } else {
        Check(false, "editor opens while settings is open");
    }
    s.CloseByX();
    Check(Has("key=shift"), "and closing settings still dropped it");

    // Resized, everything is still where it is drawn.
    s = OpenSettings();
    RECT outer{};
    GetWindowRect(s.window, &outer);
    s.Resize((outer.right - outer.left) * 3 / 2, (outer.bottom - outer.top) * 3 / 2);
    s.Key(3); // Alt
    s.Save();
    Check(s.CloseByX() && Has("key=alt"), "buttons still work after the window is made larger");

    // Made wider only, and then taller only: the controls move, and are still where they are drawn.
    s = OpenSettings();
    GetWindowRect(s.window, &outer);
    const int saveWas = s.Of(sSave).left;
    s.Resize((outer.right - outer.left) * 3 / 2, outer.bottom - outer.top);
    const bool saveMoved = s.Of(sSave).left > saveWas;
    s.Key(1);
    s.Save();
    Check(s.CloseByX() && Has("key=shift") && saveMoved, "and after it is made wider only");
    s = OpenSettings();
    GetWindowRect(s.window, &outer);
    s.Resize(outer.right - outer.left, (outer.bottom - outer.top) * 5 / 4);
    s.Key(2);
    s.Save();
    Check(s.CloseByX() && Has("key=ctrl"), "and taller only");

    // Wider than tall, Look stands beside Behaviour; taller than wide, below it. In a wider window the
    // text is larger too, which shows in the height of a row. Either way the controls work.
    s = OpenSettings();
    GetWindowRect(s.window, &outer);
    const int side = outer.bottom - outer.top;
    s.Resize(side * 5 / 4, side);
    RECT ticks = s.Of(sPauseSnap), slider = s.Of(sOpacity);
    const int rowWide = ticks.bottom - ticks.top;
    Check(slider.left > ticks.right && slider.top < ticks.bottom, "wider than tall, the sections stand side by side");
    s.Resize(side * 7 / 4, side);
    Check(s.Of(sPauseSnap).bottom - s.Of(sPauseSnap).top > rowWide, "and wider still, the text is larger");
    s.Resize(side * 3 / 4, side);
    ticks = s.Of(sPauseSnap), slider = s.Of(sOpacity);
    Check(slider.top > ticks.bottom && slider.left < ticks.right, "taller than wide, they are one below the other");
    s.Slide(sGap, true);
    s.Resize(side * 5 / 4, side);
    s.Slide(sOpacity, false);
    s.Save();
    Check(s.CloseByX() && Has("gap=40") && Has("opacity=5"), "and the sliders work in both");
    s = OpenSettings();
    s.Slide(sGap, false);
    s.Slide(sOpacity, true);
    s.Save();
    s.CloseByX();

    // A theme sets all four colours at once.
    s = OpenSettings();
    s.Press(sThemeGold);
    s.Save();
    Check(s.CloseByX() && Has("colour.zone=4a3508") && Has("colour.highlight=f4c448") && Has("colour.border=ffe07a"), "a theme sets the colours");

    // Maximized, it fills the screen and still works; the size it opens at next time is not changed by that.
    s = OpenSettings();
    ShowWindow(s.window, SW_MAXIMIZE);
    Check(IsZoomed(s.window) != 0, "the window can be maximized");
    s.Key(3);
    s.Save();
    Check(s.CloseByX() && Has("key=alt") && Has("settingswidth=1370") && Has("settingsheight=870"), "and works there, without changing the remembered size");
    s = OpenSettings();
    s.Key(1);
    s.Save();
    s.CloseByX();

    // The key that spans zones is chosen like the one that shows them.
    s = OpenSettings();
    s.Press(sSpanAlt);
    s.Save();
    Check(s.CloseByX() && Has("span=alt"), "the span key can be chosen");
    s = OpenSettings();
    s.Press(sSpanCtrl);
    s.Save();
    Check(s.CloseByX() && Has("span=ctrl"), "and put back");

    // By keyboard alone. Tab reaches the controls in order: the four keys to show zones, the four to span them, the four for sub-zones, three
    // ticks, three sliders, three ticks, four themes, four colours, two buttons.
    s = OpenSettings();
    const auto tab = [&](int times) { for (int i = 0; i < times; ++i) Key(s.window, VK_TAB); };
    tab(4); // Alt
    Key(s.window, VK_SPACE);
    tab(13); // Space between windows
    for (int i = 0; i < 3; ++i) Key(s.window, VK_RIGHT);
    tab(14); // Save
    Key(s.window, VK_RETURN);
    Check(s.CloseByX() && Has("key=alt") && Has("gap=3"), "Tab, Space, the arrows and Enter work the window without a mouse");
}

// ---------------------------------------------------------------- long random sessions
// Hundreds of clicks, drags, keys and typed sizes in no sensible order, the way no person would but a
// tired one might. Afterwards Tessera must still be running, every preset on disk must still cover the
// screen exactly with no zone too small, and it must not have kept hold of drawing resources it was done with.
unsigned s_seed = 20261116;
int Random(int below)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return static_cast<int>((s_seed >> 8) % static_cast<unsigned>(below));
}

DWORD Gdi() { return GetGuiResources(s_tessera.hProcess, GR_GDIOBJECTS); }
DWORD User() { return GetGuiResources(s_tessera.hProcess, GR_USEROBJECTS); }

// Every preset in the file: as many zones read back as were written (a preset that doesn't cover the screen
// is dropped on reading), and none smaller than the rule.
bool PresetsSound()
{
    tessera::Config config;
    if (!tessera::LoadConfig(s_cfgPath, config)) return Cfg().empty(); // nothing saved yet is sound; a file that won't read is not
    bool sound = true;
    for (int i = 0; i < tessera::kPresetCount; ++i) {
        const int written = Count(Zones(i + 1), "zone=");
        int subsRead = 0;
        for (const tessera::Zone& z : config.presets[i].zones) subsRead += static_cast<int>(z.subs.size());
        if (Count(Zones(i + 1), "sub=") != subsRead) { // sub-zones that don't cover their zone are dropped on reading
            Log("        preset %d: %d sub-zones written, %d sound", i + 1, Count(Zones(i + 1), "sub="), subsRead);
            sound = false;
        }
        if (written != static_cast<int>(config.presets[i].zones.size())) {
            Log("        preset %d: %d zones written, %d sound", i + 1, written, static_cast<int>(config.presets[i].zones.size()));
            sound = false;
        }
        for (const tessera::Zone& z : config.presets[i].zones) {
            if (z.right - z.left < tessera::kMinZone || z.bottom - z.top < tessera::kMinZone) {
                Log("        preset %d has a zone of %d x %d", i + 1, z.right - z.left, z.bottom - z.top);
                sound = false;
            }
        }
    }
    return sound;
}

// ---------------------------------------------------------------- the harder cases
// Things that only go wrong when something else does: a gap round a snapped window, a settings file that
// can't be written, Windows closing with zones unsaved.
void TestHardCases()
{
    Log("-- harder cases");
    const HWND main = Find(L"TesseraMain");

    // Preset 8 as two halves, and this monitor's: something known to snap to.
    Editor e = OpenEditor();
    Key(e.window, '8');
    e.Clear();
    Click(e.window, e.w / 2, e.Y());
    e.Save();
    Check(e.Closed() && Count(Zones(8), "zone=") == 2 && Has("zone=0 0 5000 10000"), "preset 8 is two halves");

    Settings s = OpenSettings();
    s.Slide(sGap, true);
    s.Save();
    Check(s.CloseByX() && Has("gap=40"), "the space between windows reaches 40 px and saves");
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"TesseraUiTestGapTarget";
        RegisterClassW(&wc);
        const HWND target = CreateWindowExW(0, wc.lpszClassName, L"target", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 200, 200, 500, 400, nullptr, nullptr, wc.hInstance, nullptr);
        RECT area{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
        const int middle = area.left + MulDiv(5000, area.right - area.left, 10000);
        const auto is = [&](LONG left, LONG right, const char* what) {
            RECT r{};
            if (FAILED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(target, &r);
            const bool ok = r.left == left && r.right == right && r.top == area.top + 40 && r.bottom == area.bottom - 40;
            Check(ok, what);
            if (!ok) Log("        got %ld,%ld - %ld,%ld  wanted %ld,%ld - %ld,%ld", r.left, r.top, r.right, r.bottom, left, area.top + 40, right, area.bottom - 40);
        };
        Send(main, WM_APP + 10, reinterpret_cast<WPARAM>(target), 0);
        is(area.left + 40, middle - 20, "with a gap, the left window stands 40 px from the screen's edge and 20 from the middle");
        Send(main, WM_APP + 10, reinterpret_cast<WPARAM>(target), 1);
        is(middle + 20, area.right - 40, "and the right one likewise, so 40 px lie between the two");
        if (target) DestroyWindow(target);
    }
    s = OpenSettings();
    s.Slide(sGap, false);
    s.Save();
    Check(s.CloseByX() && Has("gap=0"), "and the gap goes back to none");

    // The settings file held by another program: Save can't write it, says so by staying lit, and nothing
    // is lost; let go of, the same Save goes through.
    s = OpenSettings();
    const std::string before = Cfg();
    const HANDLE held = CreateFileW(s_cfgPath.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    s.Key(3); // Alt
    s.Save();
    Sleep(1500); // it tries for a second before giving up
    const bool alive = Alive() && Find(L"TesseraSettings") != nullptr;
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    Check(held != INVALID_HANDLE_VALUE && alive && Cfg() == before, "a Save that can't write the file changes nothing and breaks nothing");
    Check(GetFileAttributesW((s_cfgPath + L".tmp").c_str()) == INVALID_FILE_ATTRIBUTES, "and leaves no half-written file behind");
    s.Save(); // still lit: the change is still unsaved
    Sleep(300);
    Check(Has("key=alt"), "pressed again once the file is free, the same Save goes through");
    s.Key(1); // back to Shift, as the later checks expect
    s.Save();
    Check(s.CloseByX() && Has("key=shift"), "and the key goes back");

    // Windows closing with zones not yet saved: they are kept.
    e = OpenEditor();
    Key(e.window, '7');
    e.Clear(); // whatever an earlier check left there
    Click(e.window, e.w / 4, e.Y());
    Send(main, WM_ENDSESSION, TRUE, 0);
    Check(e.Closed() && Alive() && Count(Zones(7), "zone=") == 2, "signing out keeps unsaved zones");
    e = OpenEditor(); // and preset 7 empty again
    Key(e.window, '7');
    e.Clear();
    e.Save();
    e.Closed();
}

// ---------------------------------------------------------------- a whole drag
// Tessera is told, step by step, what its mouse and keyboard hooks would have been told during a drag (a
// message it only answers with --isolated, which hands each step to those same functions), and what comes
// of it is checked: whether the zones are on, what was taken from the program it was meant for, and where
// the window ends up. Run with preset 8 (two halves) as the monitor's, Shift as the key that shows the
// zones, Ctrl as the one that spans, and right-click on.
void TestDrag()
{
    Log("-- a whole drag");
    {
        Editor e = OpenEditor(); // preset 8, the two halves, as this monitor's: earlier checks moved it on
        Key(e.window, '8');
        e.Save();
        Check(e.Closed() && Cfg().find("|8\n") != std::string::npos && Count(Zones(8), "zone=") == 2, "preset 8, two halves, is the monitor's");
    }
    const HWND main = Find(L"TesseraMain");
    const HWND overlay = Find(L"TesseraOverlay");
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TesseraUiTestDragTarget";
    RegisterClassW(&wc);
    const HWND target = CreateWindowExW(0, wc.lpszClassName, L"target", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 300, 250, 500, 400, nullptr, nullptr, wc.hInstance, nullptr);
    Check(target != nullptr, "a window to drag is made");
    if (!target) return;

    RECT area{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    const int middle = area.left + MulDiv(5000, area.right - area.left, 10000);
    const int y = (area.top + area.bottom) / 2;
    const LPARAM onLeft = MAKELPARAM((area.left + middle) / 2, y), onRight = MAKELPARAM((middle + area.right) / 2, y);
    const RECT leftHalf = { area.left, area.top, middle, area.bottom }, rightHalf = { middle, area.top, area.right, area.bottom };
    const RECT whole = area;

    const auto tell = [main](int what, LPARAM with = 0) {
        DWORD_PTR answer = 0;
        SendMessageTimeoutW(main, WM_APP + 13, what, with, SMTO_ABORTIFHUNG, 3000, &answer);
        return static_cast<int>(answer);
    };
    enum { Begin = 1, MoveTo, RightDown, RightUp, LeftUpAt, KeyDown, KeyUp, MoveOver, State, Watchdog };
    enum { Dragging = 1, ZonesOn = 2, Spanning = 4, Tail = 8, Dropped = 16 };
    const auto seen = [target] {
        RECT r{};
        if (FAILED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(target, &r);
        return r;
    };
    const auto at = [&](const RECT& want, const char* what) {
        const RECT r = seen();
        const bool ok = EqualRect(&r, &want) != 0;
        Check(ok, what);
        if (!ok) Log("        got %ld,%ld - %ld,%ld  wanted %ld,%ld - %ld,%ld", r.left, r.top, r.right, r.bottom, want.left, want.top, want.right, want.bottom);
    };
    const auto rightClick = [&] { return tell(RightDown) == 1 && tell(RightUp) == 1; };
    const auto park = [&] { // somewhere that is no zone's shape, so a snap that should not happen shows
        SetWindowPos(target, nullptr, area.left + 120, area.top + 90, 520, 410, SWP_NOZORDER | SWP_NOACTIVATE);
        return seen();
    };

    // A right-click shows the zones and is taken; the drop fills the zone under the pointer.
    Check(tell(Begin, reinterpret_cast<LPARAM>(target)) == 1 && tell(State) == Dragging, "a drag begins with the zones off");
    Check(!IsWindowVisible(overlay), "and nothing is shown yet");
    tell(MoveTo, onLeft);
    Check(tell(RightDown) == 1 && (tell(State) & ZonesOn), "a right-click turns the zones on, and is kept from the window");
    Check(IsWindowVisible(overlay), "they are on screen");
    Check(tell(RightUp) == 1, "its release is kept from the window too");
    Check(tell(LeftUpAt, onLeft) == 0 && (tell(State) & Dropped), "letting go of the window is not taken from it");
    tell(MoveOver);
    Check(tell(State) == 0, "the drag is over, with nothing left waiting");
    at(leftHalf, "dropped on the left zone, the window fills it");

    Check(tell(Begin, reinterpret_cast<LPARAM>(target)) == 1 && rightClick(), "another drag, zones on");
    tell(MoveTo, onRight);
    tell(LeftUpAt, onRight);
    tell(MoveOver);
    at(rightHalf, "dropped on the right zone, it fills that");

    // A second right-click turns them off again; with them off nothing snaps.
    RECT parked = park();
    tell(Begin, reinterpret_cast<LPARAM>(target));
    Check(rightClick() && rightClick() && !(tell(State) & ZonesOn), "a second right-click turns the zones off again");
    tell(MoveTo, onLeft);
    tell(LeftUpAt, onLeft);
    tell(MoveOver);
    at(parked, "dropped with the zones off, the window stays where it was put");

    // Esc cancels: no snap, and Esc itself is left to the window.
    tell(Begin, reinterpret_cast<LPARAM>(target));
    rightClick();
    tell(MoveTo, onLeft);
    Check(tell(KeyDown, VK_ESCAPE) == 0, "Esc is not taken from the window");
    tell(MoveOver);
    at(parked, "and after Esc nothing snaps");

    // The key that shows the zones, held: on while it is down. Let go of just after the mouse, it still snaps.
    tell(Begin, reinterpret_cast<LPARAM>(target));
    Check(tell(KeyDown, VK_LSHIFT) == 0 && (tell(State) & ZonesOn), "holding Shift turns the zones on, and Shift is not taken");
    Check(tell(KeyUp, VK_LSHIFT) == 0 && !(tell(State) & ZonesOn), "letting go turns them off");
    tell(KeyDown, VK_RSHIFT);
    tell(MoveTo, onLeft);
    tell(LeftUpAt, onLeft);
    tell(KeyUp, VK_RSHIFT); // a moment after the mouse, before Windows has said the move is over
    tell(MoveTo, onRight);  // and the pointer wandering on
    tell(MoveOver);
    at(leftHalf, "the key let go of just after the mouse, the drop still counts, and where the mouse came up");

    // Spanning: from the zone the pointer was in when Ctrl went down to the one it is dropped on.
    park();
    tell(Begin, reinterpret_cast<LPARAM>(target));
    rightClick();
    tell(MoveTo, onLeft);
    Check(tell(KeyDown, VK_LCONTROL) == 0 && (tell(State) & Spanning), "Ctrl with the zones on starts a span, and is not taken");
    tell(MoveTo, onRight);
    tell(LeftUpAt, onRight);
    tell(MoveOver);
    at(whole, "dropped a zone further on, the window covers both");
    tell(KeyUp, VK_LCONTROL);

    tell(Begin, reinterpret_cast<LPARAM>(target));
    rightClick();
    tell(MoveTo, onLeft);
    tell(KeyDown, VK_LCONTROL);
    tell(MoveTo, onRight);
    Check(tell(KeyUp, VK_LCONTROL) == 0 && !(tell(State) & Spanning), "Ctrl let go of ends the span");
    tell(LeftUpAt, onRight);
    tell(MoveOver);
    at(rightHalf, "and the drop fills one zone again");

    // A number picks a preset, only with the zones on, and is taken; the choice is saved when the drag ends.
    tessera::Config config;
    tessera::LoadConfig(s_cfgPath, config);
    int other = -1;
    for (int i = 0; i < tessera::kPresetCount && other < 0; ++i) {
        if (i != 7 && !config.presets[i].Empty()) other = i;
    }
    Check(other >= 0, "there is another preset to switch to");
    if (other >= 0) {
        const WPARAM digit = '1' + other;
        const std::string theirs = "|" + std::to_string(other + 1) + "\n";
        tell(Begin, reinterpret_cast<LPARAM>(target));
        tell(MoveTo, onLeft);
        Check(tell(KeyDown, digit) == 0 && tell(KeyUp, digit) == 0, "with the zones off a number is left to the window");
        rightClick();
        Check(tell(KeyDown, digit) == 1 && tell(KeyDown, digit) == 1 && tell(KeyUp, digit) == 1, "with them on it is taken: press, repeat and release");
        tell(LeftUpAt, onLeft);
        tell(MoveOver);
        Check(Cfg().find(theirs) != std::string::npos, "and the preset it picked is the monitor's, saved when the drag ends");

        // Back to preset 8, this time with the key still down at the drop: its repeats and release are still
        // taken afterwards, the save waits for that, and then Tessera stops listening.
        tell(Begin, reinterpret_cast<LPARAM>(target));
        rightClick();
        Check(tell(KeyDown, '8') == 1, "a number held down");
        tell(LeftUpAt, onLeft);
        Check(tell(KeyDown, '8') == 1, "its repeat between the drop and the end of the move is taken");
        tell(MoveOver);
        Check(tell(State) == Tail, "after the drag Tessera still waits for that key");
        Check(Cfg().find(theirs) != std::string::npos, "and has not written the file while it waits");
        Check(tell(KeyDown, '8') == 1 && tell(KeyDown, '7') == 0, "its repeats are still taken, and no other key is");
        Check(tell(KeyUp, '8') == 1, "and so is its release");
        Sleep(300);
        Check(tell(State) == 0 && tell(KeyDown, '8') == 0, "then it stops listening");
        Check(Cfg().find("|8\n") != std::string::npos, "and the preset picked is saved");
    }

    // The right button still down at the drop: its release is taken afterwards, so the window gets no menu.
    tell(Begin, reinterpret_cast<LPARAM>(target));
    tell(MoveTo, onRight);
    Check(tell(RightDown) == 1, "the right button goes down");
    tell(LeftUpAt, onRight);
    tell(MoveOver);
    at(rightHalf, "dropped with it still down, the window snaps");
    Check(tell(State) == Tail && tell(RightDown) == 0, "Tessera waits for the release, and takes no new press");
    Check(tell(RightUp) == 1, "the release is taken");
    Sleep(300);
    Check(tell(State) == 0 && tell(RightUp) == 0, "then it stops listening");

    // Never let go of: it gives up by itself after a few seconds.
    tell(Begin, reinterpret_cast<LPARAM>(target));
    tell(RightDown);
    tell(LeftUpAt, onRight);
    tell(MoveOver);
    Check(tell(State) == Tail, "a right button never let go of");
    Sleep(3600);
    Check(tell(State) == 0, "is not waited for beyond three seconds");

    // A new drag while still waiting starts clean.
    tell(Begin, reinterpret_cast<LPARAM>(target));
    tell(RightDown);
    tell(LeftUpAt, onRight);
    tell(MoveOver);
    Check(tell(Begin, reinterpret_cast<LPARAM>(target)) == 1 && tell(State) == Dragging, "a new drag while waiting starts clean");
    tell(MoveOver);

    // Windows never says the move is over: a drop already made still counts.
    park();
    tell(Begin, reinterpret_cast<LPARAM>(target));
    rightClick();
    tell(MoveTo, onLeft);
    tell(LeftUpAt, onLeft);
    tell(Watchdog);
    at(leftHalf, "a drop counts even when Windows never says the move is over");
    parked = park();
    tell(Begin, reinterpret_cast<LPARAM>(target));
    rightClick();
    tell(MoveTo, onLeft);
    tell(Watchdog);
    at(parked, "but a move that just stops, with no drop, snaps nothing");

    Sleep(400);
    Check(Alive() && !IsWindowVisible(overlay) && tell(State) == 0, "and at the end nothing is showing or waiting");
    DestroyWindow(target);
}

// ---------------------------------------------------------------- sub-zones
// Zones within a zone: made in the editor with Alt held, and dropped on with the sub-zone key held. The
// editor is told which keys to take as held, since sent messages don't press any. Run after the drags, with
// preset 8 (two halves) as the monitor's; it leaves preset 8 as it found it.
void TestSubZones()
{
    Log("-- sub-zones");
    const HWND main = Find(L"TesseraMain");
    enum { Shift = 1, Ctrl = 2, Alt = 4 };
    const auto held = [main](int keys) { Send(main, WM_APP + 14, keys, 0); };
    const auto subs = [] { return Count(Zones(8), "sub="); };

    Editor e = OpenEditor();
    Key(e.window, '8');
    const int mid = e.h / 2; // the height sub-zone lines are taken hold of at: clear of the panel, and of the line made at Y()
    held(Alt);
    Click(e.window, e.w / 4, e.Y());
    held(0);
    e.SaveOnly();
    Check(Count(Zones(8), "zone=") == 2 && subs() == 1 && Has("sub=0 0 5000 10000"), "Alt+click adds a sub-zone from the zone's left edge to the mouse, and leaves the zones as they were");
    held(Alt | Shift);
    Click(e.window, e.w * 3 / 8, e.Y());
    held(0);
    e.SaveOnly();
    Check(subs() == 2 && Has("sub=0 0 5000 10000") && Has("sub=0 0 10000 7500"), "Alt+Shift+click adds one from its top down to the mouse, over the first");
    held(Alt);
    Click(e.window, e.w / 4, e.h * 6 / 10); // on the first one's own edge
    held(0);
    e.SaveOnly();
    Check(subs() == 2, "an Alt+click on a sub-zone's own edge takes hold of the edge, and adds nothing");
    Click(e.window, e.w / 8, mid);
    Click(e.window, e.w * 3 / 4, mid); // the other zone, which has none, is cut as ever
    e.SaveOnly();
    Check(Count(Zones(8), "zone=") == 3 && subs() == 2, "a plain click leaves a zone that holds sub-zones alone");
    e.Undo();
    e.SaveOnly();
    Check(Count(Zones(8), "zone=") == 2 && subs() == 2, "and Undo keeps the sub-zones");

    // A sub-zone's edge is dragged like any line, in percents of its zone.
    Move(e.window, e.w / 4, mid);
    Down(e.window, e.w / 4, mid);
    Move(e.window, e.w * 15 / 100, mid, MK_LBUTTON);
    Up(e.window, e.w * 15 / 100, mid);
    e.SaveOnly();
    Check(Has("sub=0 0 3000 10000") && Has("sub=0 0 10000 7500"), "a sub-zone's edge is dragged to 30 % of its zone, and the other sub-zone stays");
    e.Undo();
    e.SaveOnly();
    Check(Has("sub=0 0 5000 10000"), "and Undo puts it back");

    // The line between the two zones moves, and the sub-zones keep their share.
    Move(e.window, e.w / 2, mid);
    Down(e.window, e.w / 2, mid);
    Move(e.window, e.w * 60 / 100, mid, MK_LBUTTON);
    Up(e.window, e.w * 60 / 100, mid);
    e.SaveOnly();
    Check(Has("zone=0 0 6000 10000") && Has("sub=0 0 5000 10000") && subs() == 2, "when the zone's own line moves, its sub-zones keep their share of it");
    e.Undo();
    Send(e.window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(e.w / 2, mid));
    e.SaveOnly();
    Check(Find(L"TesseraEditor") != nullptr && Count(Zones(8), "zone=") == 2 && subs() == 2, "a zone holding sub-zones is not joined to its neighbour");
    e.CloseButton();
    Check(e.Closed() && Has("zone=0 0 5000 10000") && subs() == 2, "the editor closes with the sub-zones saved");

    // Dropping on them. Alt is the sub-zone key here: Shift shows the zones in these tests, Ctrl spans.
    Settings s = OpenSettings();
    s.Press(sSubAlt);
    s.Save();
    Check(s.CloseByX() && Has("subkey=alt"), "the sub-zone key can be chosen");

    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TesseraUiTestSubTarget";
    RegisterClassW(&wc);
    const HWND target = CreateWindowExW(0, wc.lpszClassName, L"target", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 300, 250, 500, 400, nullptr, nullptr, wc.hInstance, nullptr);
    Check(target != nullptr, "a window to drag is made");
    if (!target) return;
    RECT area{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    const int w = area.right - area.left, h = area.bottom - area.top;
    const auto px = [&](int units) { return static_cast<LONG>(area.left + MulDiv(units, w, 10000)); };
    const auto py = [&](int units) { return static_cast<LONG>(area.top + MulDiv(units, h, 10000)); };
    const auto point = [&](int x, int y) { return MAKELPARAM(px(x), py(y)); };
    const auto tell = [main](int what, LPARAM with = 0) {
        DWORD_PTR answer = 0;
        SendMessageTimeoutW(main, WM_APP + 13, what, with, SMTO_ABORTIFHUNG, 3000, &answer);
        return static_cast<int>(answer);
    };
    enum { Begin = 1, MoveTo, RightDown, RightUp, LeftUpAt, KeyDown, KeyUp, MoveOver, State };
    enum { SubHeld = 32 };
    const auto at = [&](const RECT& want, const char* what) {
        RECT r{};
        if (FAILED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(target, &r);
        const bool ok = EqualRect(&r, &want) != 0;
        Check(ok, what);
        if (!ok) Log("        got %ld,%ld - %ld,%ld  wanted %ld,%ld - %ld,%ld", r.left, r.top, r.right, r.bottom, want.left, want.top, want.right, want.bottom);
    };
    const auto begin = [&](LPARAM where) {
        SetWindowPos(target, nullptr, area.left + 120, area.top + 90, 520, 410, SWP_NOZORDER | SWP_NOACTIVATE);
        tell(Begin, reinterpret_cast<LPARAM>(target));
        tell(RightDown);
        tell(RightUp);
        tell(MoveTo, where);
    };
    const auto drop = [&](LPARAM where) {
        tell(LeftUpAt, where);
        tell(MoveOver);
    };
    const RECT leftHalf = { px(0), py(0), px(5000), py(10000) }, rightHalf = { px(5000), py(0), px(10000), py(10000) };

    begin(point(1250, 5000));
    Check(tell(KeyDown, VK_LMENU) == 0 && (tell(State) & SubHeld), "the sub-zone key is noticed, and not taken from the window");
    drop(point(1250, 5000));
    tell(KeyUp, VK_LMENU);
    at({ px(0), py(0), px(2500), py(10000) }, "dropped with the key held, the window fills the sub-zone under the pointer");

    begin(point(1250, 5000));
    tell(KeyDown, VK_LMENU);
    tell(MoveTo, point(3750, 5000)); // past the narrow one: only the wide one reaches here
    drop(point(3750, 5000));
    tell(KeyUp, VK_LMENU);
    at({ px(0), py(0), px(5000), py(7500) }, "moved on to where only another sub-zone reaches, it fills that one");

    begin(point(1250, 5000));
    tell(KeyDown, VK_LMENU);
    tell(KeyUp, VK_LMENU);
    Check(!(tell(State) & SubHeld), "letting go of the key closes the zone again");
    drop(point(1250, 5000));
    at(leftHalf, "and the drop fills the whole zone");

    begin(point(7500, 5000));
    tell(KeyDown, VK_LMENU);
    drop(point(7500, 5000));
    tell(KeyUp, VK_LMENU);
    at(rightHalf, "a zone without sub-zones is filled whole, key or no key");

    begin(point(1250, 5000));
    tell(KeyDown, VK_LMENU);
    tell(KeyDown, VK_RMENU);
    tell(KeyUp, VK_LMENU);
    Check((tell(State) & SubHeld) != 0, "with both Alt keys down, letting go of one leaves the zone open");
    drop(point(1250, 5000));
    tell(KeyUp, VK_RMENU);
    at({ px(0), py(0), px(2500), py(10000) }, "and the drop still fills the sub-zone");

    // Spanning from a sub-zone to the next zone: the box round both.
    begin(point(1250, 2000));
    tell(KeyDown, VK_LMENU);
    tell(KeyDown, VK_LCONTROL);
    tell(MoveTo, point(7500, 9000));
    drop(point(7500, 9000));
    tell(KeyUp, VK_LCONTROL);
    tell(KeyUp, VK_LMENU);
    at({ px(0), py(0), px(10000), py(10000) }, "spanning from a sub-zone to the next zone covers the box round both");

    // One key can't have two jobs: set to the key that shows the zones, sub-zones are off.
    s = OpenSettings();
    s.Press(sSubShift);
    s.Save();
    Check(s.CloseByX() && Has("subkey=shift"), "the sub-zone key set to the one that shows the zones");
    SetWindowPos(target, nullptr, area.left + 120, area.top + 90, 520, 410, SWP_NOZORDER | SWP_NOACTIVATE);
    tell(Begin, reinterpret_cast<LPARAM>(target));
    tell(KeyDown, VK_LSHIFT);
    tell(MoveTo, point(1250, 5000));
    Check(!(tell(State) & SubHeld), "leaves that key showing the zones");
    drop(point(1250, 5000));
    tell(KeyUp, VK_LSHIFT);
    at(leftHalf, "and a drop fills the zone, not a sub-zone");
    Sleep(300);
    Check(Alive() && tell(State) == 0, "nothing is left waiting");

    // A sub-zone's own edge joins nothing, so a right-click on it removes nothing; Alt+right-click removes
    // the sub-zone under the mouse, and with the last one gone the zone is whole again.
    e = OpenEditor();
    Key(e.window, '8');
    Send(e.window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(e.w / 4, mid));
    e.SaveOnly();
    Check(Find(L"TesseraEditor") != nullptr && subs() == 2, "a right-click on a sub-zone's edge removes nothing, and closes nothing");
    held(Alt);
    Send(e.window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(e.w / 8, mid));
    held(0);
    e.SaveOnly();
    Check(subs() == 1 && Has("sub=0 0 10000 7500"), "Alt+right-click removes the sub-zone under the mouse: of two there, the smaller");
    held(Alt);
    Send(e.window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(e.w / 8, mid));
    held(0);
    e.SaveOnly();
    Check(subs() == 0 && Count(Zones(8), "zone=") == 2 && Has("zone=0 0 5000 10000"), "and with the last one gone the zone is whole again");

    // Two widths in the same corner, then one sub-zone's own edges moved with Alt.
    held(Alt);
    Click(e.window, e.w / 4, mid);
    Click(e.window, e.w * 40 / 100, mid);
    held(0);
    e.SaveOnly();
    Check(subs() == 2 && Has("sub=0 0 5000 10000") && Has("sub=0 0 8000 10000"), "two Alt+clicks stack two widths in the same corner");
    // Alt+double-click: a size typed for one sub-zone's own edge moves that edge alone, on the zone's border too.
    const auto typeAt = [&](int x, const wchar_t* text) {
        held(Alt);
        Move(e.window, x, mid);
        Send(e.window, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(x, mid));
        Up(e.window, x, mid);
        held(0);
        const HWND box = GetDlgItem(e.window, 2);
        if (!box) return false;
        DWORD_PTR result = 0;
        SendMessageTimeoutW(box, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text), SMTO_ABORTIFHUNG, 3000, &result);
        Send(box, WM_KEYDOWN, VK_RETURN, 0);
        e.SaveOnly();
        return true;
    };
    Check(typeAt(e.S(3), L"20%") && Has("sub=2000 0 5000 10000") && Has("sub=0 0 8000 10000"), "a size typed for one sub-zone's edge on the border moves that edge alone");
    e.Undo();
    Check(typeAt(e.w / 4 - e.S(3), L"60%") && Has("sub=0 0 6000 10000") && Has("sub=0 0 8000 10000"), "and for an edge inside the zone");
    e.Undo();
    e.SaveOnly();
    Check(subs() == 2 && Has("sub=0 0 5000 10000") && Has("sub=0 0 8000 10000"), "Undo puts both back");

    held(Alt);
    Move(e.window, e.w * 40 / 100 - e.S(3), mid); // the wide one's right edge
    Down(e.window, e.w * 40 / 100 - e.S(3), mid);
    Move(e.window, e.w * 45 / 100, mid, MK_LBUTTON);
    Up(e.window, e.w * 45 / 100, mid);
    held(0);
    e.SaveOnly();
    Check(subs() == 2 && Has("sub=0 0 5000 10000") && Has("sub=0 0 9000 10000"), "Alt+drag moves one sub-zone's edge by itself");
    held(Alt);
    Move(e.window, e.S(3), mid); // on the zone's own left border, where both begin: the smaller one's edge
    Down(e.window, e.S(3), mid);
    Move(e.window, e.w / 10, mid, MK_LBUTTON);
    Up(e.window, e.w / 10, mid);
    held(0);
    e.SaveOnly();
    Check(Has("sub=2000 0 5000 10000") && Has("sub=0 0 9000 10000") && Has("zone=0 0 5000 10000") && Has("zone=5000 0 10000 10000"),
          "an edge lying on the zone's own border is drawn in, and the zones stay");
    e.CloseButton();
    Check(e.Closed(), "the editor closes");

    s = OpenSettings();
    s.Press(sSubAlt);
    s.Save();
    s.CloseByX();
    begin(point(1500, 5000)); // 30 % across the zone: in both, and the narrow one is the smaller
    tell(KeyDown, VK_LMENU);
    drop(point(1500, 5000));
    tell(KeyUp, VK_LMENU);
    at({ px(1000), py(0), px(2500), py(10000) }, "where two sub-zones lie over one another, a drop fills the smaller");
    begin(point(3250, 5000)); // 65 %: only the other
    tell(KeyDown, VK_LMENU);
    drop(point(3250, 5000));
    tell(KeyUp, VK_LMENU);
    at({ px(0), py(0), px(4500), py(10000) }, "beside it, the one that reaches there");
    begin(point(4750, 5000)); // 95 %: neither
    tell(KeyDown, VK_LMENU);
    drop(point(4750, 5000));
    tell(KeyUp, VK_LMENU);
    at(leftHalf, "and where no sub-zone reaches, the zone itself");
    Sleep(300);
    s = OpenSettings();
    s.Press(sSubShift);
    s.Save();
    s.CloseByX();
    DestroyWindow(target);

    // Alt+right-click removes the sub-zone under the mouse, and never closes the editor.
    e = OpenEditor();
    Key(e.window, '8');
    held(Alt);
    Send(e.window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(e.w * 325 / 1000, mid));
    held(0);
    e.SaveOnly();
    Check(subs() == 1 && Has("sub=2000 0 5000 10000"), "Alt+right-click removes the sub-zone under the mouse; one may stand alone");
    held(Alt);
    Send(e.window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(e.w / 8, mid));
    Send(e.window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(e.w * 3 / 4, mid)); // nothing there to remove
    held(0);
    e.SaveOnly();
    Check(Find(L"TesseraEditor") != nullptr && subs() == 0 && Count(Zones(8), "zone=") == 2, "the last one too, and with none under the mouse it does nothing");
    e.CloseButton();
    Check(e.Closed(), "the editor closes");
}

// Started again on a settings file that has something wrong with it. Run last, with Tessera closed.
void TestStartup(const std::wstring& exe)
{
    Log("-- starting on a settings file with something wrong");
    const auto start = [&exe] {
        std::wstring command = L"\"" + exe + L"\" --isolated";
        STARTUPINFOW startup{ sizeof(startup) };
        CloseHandle(s_tessera.hThread);
        CloseHandle(s_tessera.hProcess);
        s_tessera = {};
        return CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &s_tessera) &&
               WaitFor(L"TesseraMain", true) != nullptr;
    };
    const auto stop = [] {
        PostMessageW(Find(L"TesseraMain"), WM_COMMAND, 102, 0);
        const bool exited = WaitForSingleObject(s_tessera.hProcess, 5000) == WAIT_OBJECT_0;
        if (!exited) TerminateProcess(s_tessera.hProcess, 1);
        Sleep(200);
        return exited;
    };
    const auto write = [](const std::wstring& path, const std::string& text) {
        FILE* file = nullptr;
        if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || !file) return;
        fwrite(text.data(), 1, text.size(), file);
        fclose(file);
    };
    const auto read = [](const std::wstring& path) {
        std::string text;
        FILE* file = nullptr;
        if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) return text;
        char buffer[4096];
        for (size_t got; (got = fread(buffer, 1, sizeof(buffer), file)) > 0;) text.append(buffer, got);
        fclose(file);
        return text;
    };
    const std::wstring kept = s_cfgPath + L".damaged";

    // A preset that no longer covers the screen: dropped, and the file as it was is kept beside it.
    const std::string broken = Cfg() + "preset=9|Broken\nzone=0 0 9999 10000\n";
    write(s_cfgPath, broken);
    Check(start(), "Tessera starts on a file holding a preset that doesn't cover the screen");
    Check(read(kept) == broken, "and keeps a copy of the file as it was");
    Check(Has("key=shift"), "the rest of the file is still in use");
    Check(stop(), "it closes again");

    // A file with nothing in it: the defaults, not nine empty presets.
    write(s_cfgPath, "");
    Check(start(), "Tessera starts on an emptied file");
    Editor e = OpenEditor();
    Check(e.window != nullptr && e.Name() == L"50% / 50%", "and has the default presets, not nine empty ones");
    if (e.window) {
        Key(e.window, VK_ESCAPE);
        e.Closed();
    }
    Check(stop(), "it closes again");

    // Random drops on random sub-zones lying over one another: where each window lands is worked out here as
    // well, and must be the same. The smallest sub-zone under the pointer, the later of two the same size;
    // the zone where none reaches; and with the span key, the box round where it began and where it ended.
    {
        tessera::Config config;
        config.key = tessera::ActivationKey::None;
        config.spanKey = tessera::ActivationKey::Ctrl;
        config.subKey = tessera::ActivationKey::Alt;
        std::vector<tessera::Zone>& zones = config.presets[0].zones;
        zones = { { 0, 0, 4700, 10000, {} }, { 4700, 0, 10000, 5300, {} }, { 4700, 5300, 10000, 10000, {} } };
        for (size_t z = 0; z < 2; ++z) { // the third has none
            for (int i = 0; i < 7; ++i) {
                const int left = Random(60) * 100, top = Random(60) * 100;
                zones[z].subs.push_back({ left, top, left + 3500 + Random((10000 - left - 3500) / 100 + 1) * 100, top + 3500 + Random((10000 - top - 3500) / 100 + 1) * 100, {} });
            }
        }
        tessera::SortZones(zones);
        Check(tessera::SaveConfig(s_cfgPath, config) && start(), "Tessera starts on random overlapping sub-zones");
        const HWND main = Find(L"TesseraMain");
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"TesseraUiTestRandomTarget";
        RegisterClassW(&wc);
        const HWND target = CreateWindowExW(0, wc.lpszClassName, L"target", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 300, 250, 500, 400, nullptr, nullptr, wc.hInstance, nullptr);
        RECT area{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
        const int w = area.right - area.left, h = area.bottom - area.top;
        const auto rect = [&](const tessera::Zone& z) {
            return RECT{ area.left + MulDiv(z.left, w, 10000), area.top + MulDiv(z.top, h, 10000), area.left + MulDiv(z.right, w, 10000), area.top + MulDiv(z.bottom, h, 10000) };
        };
        const auto tell = [main](int what, LPARAM with = 0) {
            DWORD_PTR answer = 0;
            SendMessageTimeoutW(main, WM_APP + 13, what, with, SMTO_ABORTIFHUNG, 3000, &answer);
            return static_cast<int>(answer);
        };
        const auto under = [&](POINT at, bool sub) {
            for (const tessera::Zone& zone : zones) {
                const RECT box = rect(zone);
                if (!PtInRect(&box, at)) continue;
                tessera::Zone best = { zone.left, zone.top, zone.right, zone.bottom, {} };
                long long least = -1;
                for (const tessera::Zone& s : zone.subs) {
                    const tessera::Zone whole = tessera::Inside(zone, s);
                    const RECT own = rect(whole);
                    const long long size = static_cast<long long>(s.right - s.left) * (s.bottom - s.top);
                    if (!sub || !PtInRect(&own, at) || (least >= 0 && size > least)) continue;
                    least = size;
                    best = whole;
                }
                return best;
            }
            return tessera::Zone{};
        };
        int wrong = 0, drops = 0;
        for (; target && drops < 120 && Alive(); ++drops) {
            SetWindowPos(target, nullptr, area.left + 60, area.top + 60, 520, 410, SWP_NOZORDER | SWP_NOACTIVATE);
            const POINT from = { area.left + Random(w), area.top + Random(h) }, to = { area.left + Random(w), area.top + Random(h) };
            const bool sub = Random(4) != 0, span = Random(3) == 0;
            tell(1, reinterpret_cast<LPARAM>(target));
            tell(3);
            tell(4);
            tell(2, MAKELPARAM(from.x, from.y));
            if (sub) tell(6, VK_LMENU);
            if (span) tell(6, VK_LCONTROL);
            tell(2, MAKELPARAM(to.x, to.y));
            tell(5, MAKELPARAM(to.x, to.y));
            tell(8);
            if (span) tell(7, VK_LCONTROL);
            if (sub) tell(7, VK_LMENU);
            tessera::Zone want = under(to, sub);
            if (span) {
                const tessera::Zone began = under(from, sub);
                want = { (std::min)(want.left, began.left), (std::min)(want.top, began.top), (std::max)(want.right, began.right), (std::max)(want.bottom, began.bottom), {} };
            }
            const RECT box = rect(want);
            RECT seen{};
            if (FAILED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &seen, sizeof(seen)))) GetWindowRect(target, &seen);
            if (EqualRect(&seen, &box)) continue;
            if (++wrong <= 3) {
                Log("        dropped at %ld,%ld (sub-zones %d, span %d from %ld,%ld): got %ld,%ld - %ld,%ld  wanted %ld,%ld - %ld,%ld", to.x, to.y, sub, span, from.x, from.y,
                    seen.left, seen.top, seen.right, seen.bottom, box.left, box.top, box.right, box.bottom);
            }
        }
        Check(drops == 120 && wrong == 0, "a hundred and twenty random drops on them all land where they should");
        if (target) DestroyWindow(target);
        Check(stop(), "it closes again");
    }
    DeleteFileW(kept.c_str());
}

void StressEditor()
{
    Log("-- a long random session in the editor");
    Editor e = OpenEditor(); // once first, so what it only ever makes once is already made
    Key(e.window, VK_ESCAPE);
    e.Closed();
    const DWORD gdiBefore = Gdi(), userBefore = User();

    const wchar_t* typed[] = { L"37%", L"812px", L"abc", L"0", L"99999", L"-5", L"50", L"12,5%", L"", L"1e9", L"%%%", L"3000 px" };
    int reopened = 0, steps = 0;
    for (; steps < 1500 && Alive(); ++steps) {
        if (!Find(L"TesseraEditor")) {
            e = OpenEditor();
            ++reopened;
            if (!e.window) break;
        }
        const int x = Random(e.w), y = Random(e.h);
        // Now and then with Shift, Ctrl or Alt taken as held, so sub-zones are made, moved and removed as well.
        Send(Find(L"TesseraMain"), WM_APP + 14, Random(3) == 0 ? Random(8) : 0, 0);
        switch (Random(11)) {
        case 0: case 1: case 2:
            Click(e.window, x, y); // splits a zone, or presses whatever button is there
            break;
        case 3: case 4: { // drags, a line if there is one under it
            Move(e.window, x, y);
            Down(e.window, x, y);
            const int tx = Random(e.w), ty = Random(e.h);
            Move(e.window, tx, ty, MK_LBUTTON);
            if (Random(8) == 0) Key(e.window, '1' + Random(9)); // a preset key in the middle of it
            Up(e.window, tx, ty);
            break;
        }
        case 5:
            Key(e.window, '1' + Random(9));
            break;
        case 6:
            for (int i = Random(6); i >= 0; --i) Key(e.window, VK_LEFT + Random(4));
            break;
        case 7:
            e.Undo();
            break;
        case 8: { // a double-click, and if that opened the size box, something typed into it
            Move(e.window, x, y);
            Send(e.window, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(x, y));
            Up(e.window, x, y);
            if (const HWND box = GetDlgItem(e.window, 2)) {
                DWORD_PTR result = 0;
                SendMessageTimeoutW(box, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(typed[Random(12)]), SMTO_ABORTIFHUNG, 3000, &result);
                Send(box, WM_KEYDOWN, Random(4) ? VK_RETURN : VK_ESCAPE, 0);
            }
            break;
        }
        case 9:
            Send(e.window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(x, y)); // removes a line, or closes without saving
            break;
        case 10:
            e.SaveOnly();
            break;
        }
    }
    Send(Find(L"TesseraMain"), WM_APP + 14, 0, 0);
    if (Find(L"TesseraEditor")) {
        e.SaveOnly();
        Key(e.window, VK_ESCAPE);
        e.Closed();
    }
    Log("        (%d actions; the editor closed and was reopened %d times)", steps, reopened);
    Check(Alive(), "still running after it");
    Check(PresetsSound(), "every preset on disk still covers the screen, with no zone too small");
    Sleep(200);
    const int gdi = static_cast<int>(Gdi()) - static_cast<int>(gdiBefore), user = static_cast<int>(User()) - static_cast<int>(userBefore);
    Log("        (drawing objects held: %+d; windows and menus: %+d)", gdi, user);
    Check(gdi < 120 && user < 20, "and it kept hold of nothing it was done with");
}

void StressSettings()
{
    Log("-- a long random session in settings");
    Settings s = OpenSettings();
    s.CloseByX();
    const DWORD gdiBefore = Gdi(), userBefore = User();
    const std::string before = Cfg();

    s = OpenSettings();
    if (!s.window) {
        Check(false, "settings opens");
        return;
    }
    // Everything that can be clicked without touching Windows itself or opening another window: not Start
    // with Windows, the snap bar, the colour pickers or Save. Nothing here is saved.
    const int ticks[] = { sRightClick, sPauseSnap, sSpanOff, sSpanShift, sSpanCtrl, sSpanAlt, sShowNumbers, sShowLabel };
    RECT outer{};
    GetWindowRect(s.window, &outer);
    const int width = outer.right - outer.left, height = outer.bottom - outer.top;
    int steps = 0;
    for (; steps < 1200 && Alive() && Find(L"TesseraSettings"); ++steps) {
        switch (Random(8)) {
        case 0: s.Key(Random(4)); break;
        case 1: s.Press(ticks[Random(8)]); break;
        case 2: case 3: { // a slider, clicked or dragged along
            const RECT r = s.Of(sOpacity + Random(4));
            const int y = (r.top + r.bottom) / 2, span = std::max(1, static_cast<int>(r.right - r.left));
            const int x = r.left + Random(span);
            Move(s.window, x, y);
            Down(s.window, x, y);
            Move(s.window, r.left + Random(span), y + Random(40) - 20, MK_LBUTTON);
            Up(s.window, r.left + Random(span), y);
            break;
        }
        case 4: s.Press(sThemeSlate + Random(4)); break; // a theme
        case 5: s.Reset(); break;
        case 6: // the keyboard, but never Space or Enter, which could land on what must not be pressed
            for (int i = Random(8); i >= 0; --i) ::Key(s.window, Random(3) ? VK_TAB : (Random(2) ? VK_LEFT : VK_RIGHT));
            break;
        case 7: // another size and shape
            s.Resize(width * (40 + Random(140)) / 100, height * (60 + Random(80)) / 100);
            break;
        }
    }
    Log("        (%d actions)", steps);
    Check(Alive() && Find(L"TesseraSettings") != nullptr, "still running, and the window still open");
    Check(s.CloseByX(), "it closes");
    Check(Cfg() == before, "nothing of it reached the settings file, since nothing was saved");
    Sleep(200);
    const int gdi = static_cast<int>(Gdi()) - static_cast<int>(gdiBefore), user = static_cast<int>(User()) - static_cast<int>(userBefore);
    Log("        (drawing objects held: %+d; windows and menus: %+d)", gdi, user);
    Check(gdi < 60 && user < 20, "and it kept hold of nothing it was done with");
}

void StressOverlay()
{
    Log("-- the zones shown and hidden two hundred times");
    const HWND main = Find(L"TesseraMain");
    Send(main, WM_APP + 11, 1, 0);
    Send(main, WM_APP + 11, 0, 0);
    Sleep(600);
    const DWORD gdiBefore = Gdi();
    for (int i = 0; i < 200 && Alive(); ++i) {
        Send(main, WM_APP + 11, 1 + Random(2), 0);
        if (Random(3) == 0) Sleep(Random(40));
        Send(main, WM_APP + 11, 0, 0);
        if (Random(3) == 0) Sleep(Random(160));
    }
    Sleep(700);
    const int gdi = static_cast<int>(Gdi()) - static_cast<int>(gdiBefore);
    Log("        (drawing objects held: %+d)", gdi);
    Check(Alive() && !IsWindowVisible(Find(L"TesseraOverlay")), "still running, and they are gone at the end");
    Check(gdi < 40, "and nothing was kept hold of");
}

} // namespace

int wmain(int count, wchar_t** arguments)
{
    if (count < 3) return 2;
    if (count > 3) s_seed = static_cast<unsigned>(_wtoi(arguments[3])); // another run of random sessions
    // Real pixels, as Tessera sees them; otherwise window sizes read here are scaled and the clicks miss.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    _wfopen_s(&s_log, arguments[2], L"w");
    if (!s_log) return 2;

    // A copy of its own, so its Tessera.cfg is not the user's.
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const std::wstring folder = std::wstring(temp) + L"Tessera-uitest-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(folder.c_str(), nullptr);
    const std::wstring exe = folder + L"\\Tessera.exe";
    s_cfgPath = folder + L"\\Tessera.cfg";
    if (!CopyFileW(arguments[1], exe.c_str(), FALSE)) {
        Log("FAILED  could not copy Tessera.exe");
        return 2;
    }

    std::wstring command = L"\"" + exe + L"\" --isolated";
    STARTUPINFOW startup{ sizeof(startup) };
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &s_tessera)) {
        Log("FAILED  could not start Tessera");
        return 2;
    }
    Check(WaitFor(L"TesseraMain", true) != nullptr, "Tessera starts");

    TestSnap();
    TestOverlay();
    TestEditor();
    Check(Alive(), "still running after the editor tests");
    TestSettings();
    Check(Alive(), "still running after the settings tests");
    TestHardCases();
    Check(Alive(), "still running after the harder cases");
    TestDrag();
    Check(Alive(), "still running after the drags");
    TestSubZones();
    Check(Alive(), "still running after the sub-zones");
    // Close in the tray menu doesn't throw away zone edits: with some unsaved, it brings the editor forward
    // and Tessera stays.
    {
        Editor e = OpenEditor();
        Key(e.window, '8');
        Click(e.window, e.w / 4, e.Y());
        PostMessageW(Find(L"TesseraMain"), WM_COMMAND, 102, 0);
        Sleep(500);
        Check(Alive() && Find(L"TesseraEditor") != nullptr, "Close with zone edits unsaved keeps Tessera and the editor open");
        Key(e.window, VK_ESCAPE);
        e.Closed();
    }

    StressOverlay();
    StressEditor();
    StressSettings();

    PostMessageW(Find(L"TesseraMain"), WM_COMMAND, 102, 0); // the tray menu's Close
    DWORD code = 1;
    const bool exited = WaitForSingleObject(s_tessera.hProcess, 5000) == WAIT_OBJECT_0;
    if (exited) GetExitCodeProcess(s_tessera.hProcess, &code);
    else TerminateProcess(s_tessera.hProcess, 1);
    Check(exited && code == 0, "Close in the tray menu exits cleanly");
    Sleep(200);
    TestStartup(exe);

    Sleep(200);
    DeleteFileW(s_cfgPath.c_str());
    DeleteFileW(exe.c_str());
    RemoveDirectoryW(folder.c_str());

    Log("%d of %d checks failed", s_failed, s_checks);
    fclose(s_log);
    return s_failed ? 1 : 0;
}
