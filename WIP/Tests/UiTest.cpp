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
    sColourZone, sColourHighlight, sColourBorder, sColourText, sResetAll, sSave
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
    s.Slide(sPadding, false);
    s.Save();
    Check(s.CloseByX() && Has("opacity=95"), "the opacity slider reaches 95 % and saves");
    Check(Has("padding=0"), "the padding slider reaches none and saves");

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

    // By keyboard alone. Tab reaches the controls in order: the four keys to show zones, the four to span them, three ticks, four sliders, two
    // ticks, four themes, four colours, two buttons.
    s = OpenSettings();
    const auto tab = [&](int times) { for (int i = 0; i < times; ++i) Key(s.window, VK_TAB); };
    tab(4); // Alt
    Key(s.window, VK_SPACE);
    tab(9); // Space between windows
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
    DeleteFileW(s_cfgPath.c_str());
    DeleteFileW(exe.c_str());
    RemoveDirectoryW(folder.c_str());

    Log("%d of %d checks failed", s_failed, s_checks);
    fclose(s_log);
    return s_failed ? 1 : 0;
}
