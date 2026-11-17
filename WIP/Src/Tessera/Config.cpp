#include "Config.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <sstream>

namespace tessera {

namespace {

int ToInt(const std::wstring& text, int fallback)
{
    wchar_t* end = nullptr;
    const long value = std::wcstol(text.c_str(), &end, 10);
    return end == text.c_str() ? fallback : static_cast<int>(value);
}

std::wstring FromUtf8(const std::string& bytes)
{
    if (bytes.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring text(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), length);
    return text;
}

std::string ToUtf8(const std::wstring& text)
{
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string bytes(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), bytes.data(), length, nullptr, nullptr);
    return bytes;
}

} // namespace

Config DefaultConfig()
{
    Config config;
    auto twoColumns = [](int split) {
        return std::vector<Zone>{ { 0, 0, split, kUnits }, { split, 0, kUnits, kUnits } };
    };
    config.presets[0] = { L"", twoColumns(5000) }; // unnamed: shown by their shape, "50% / 50%"
    config.presets[1] = { L"", twoColumns(6103) };
    config.presets[2] = { L"", twoColumns(4113) };
    return config;
}

std::wstring Serialize(const Config& config)
{
    std::wostringstream out;
    out << L"# Tessera settings. Written by Tessera; edit with Tessera closed.\n";
    const wchar_t* key = config.key == ActivationKey::Shift ? L"shift" : config.key == ActivationKey::Ctrl ? L"ctrl" :
                         config.key == ActivationKey::Alt ? L"alt" : L"none";
    out << L"key=" << key << L"\n";
    out << L"rightclick=" << (config.rightClickToggles ? 1 : 0) << L"\n";
    out << L"opacity=" << config.opacity << L"\n";
    out << L"span=" << (config.spanKey == ActivationKey::Shift ? L"shift" : config.spanKey == ActivationKey::Ctrl ? L"ctrl" :
                        config.spanKey == ActivationKey::Alt ? L"alt" : L"off") << L"\n";
    out << L"pausesnap=" << (config.pauseWindowsSnap ? 1 : 0) << L"\n";
    out << L"gap=" << config.gap << L"\n";
    out << L"padding=" << config.padding << L"\n";
    out << L"fade=" << config.fade << L"\n";
    out << L"settingswidth=" << config.settingsWidth << L"\nsettingsheight=" << config.settingsHeight << L"\n";
    out << L"shownumbers=" << (config.showNumbers ? 1 : 0) << L"\n";
    out << L"showlabel=" << (config.showLabel ? 1 : 0) << L"\n";
    out << std::hex << L"colour.zone=" << config.zoneColour << L"\ncolour.highlight=" << config.highlightColour
        << L"\ncolour.border=" << config.borderColour << L"\ncolour.text=" << config.textColour << L"\n" << std::dec;
    for (const auto& [monitor, preset] : config.monitorPreset) out << L"monitor=" << monitor << L"|" << (preset + 1) << L"\n";
    for (int i = 0; i < kPresetCount; ++i) {
        const Preset& preset = config.presets[i];
        if (preset.Empty() && preset.name.empty()) continue;
        out << L"preset=" << (i + 1) << L"|" << preset.name << L"\n";
        for (const Zone& z : preset.zones) out << L"zone=" << z.left << L" " << z.top << L" " << z.right << L" " << z.bottom << L"\n";
    }
    return out.str();
}

Config Parse(const std::wstring& text)
{
    Config config;
    int current = -1;
    std::wistringstream in(text);
    std::wstring line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty() || line[0] == L'#') continue;
        const size_t equals = line.find(L'=');
        if (equals == std::wstring::npos) continue;
        const std::wstring name = line.substr(0, equals);
        const std::wstring value = line.substr(equals + 1);

        if (name == L"key") {
            config.key = value == L"ctrl" ? ActivationKey::Ctrl : value == L"alt" ? ActivationKey::Alt :
                         value == L"shift" ? ActivationKey::Shift : ActivationKey::None;
        } else if (name == L"rightclick") {
            config.rightClickToggles = value == L"1";
        } else if (name == L"opacity") {
            config.opacity = std::clamp(ToInt(value, 25), 5, 95);
        } else if (name == L"span") {
            // Before 0.5.1 this was 1 or 0, and the key was Ctrl.
            config.spanKey = value == L"ctrl" || value == L"1" ? ActivationKey::Ctrl : value == L"alt" ? ActivationKey::Alt :
                             value == L"shift" ? ActivationKey::Shift : ActivationKey::None;
        } else if (name == L"pausesnap") {
            config.pauseWindowsSnap = value == L"1";
        } else if (name == L"gap") {
            config.gap = std::clamp(ToInt(value, 0), 0, 40);
        } else if (name == L"fade") {
            config.fade = std::clamp(ToInt(value, 80), 0, 200);
            if (config.fade == 110) config.fade = 80; // the default before 0.5.1
        } else if (name == L"padding") {
            config.padding = std::clamp(ToInt(value, 2), 0, 24);
        } else if (name == L"settingswidth") {
            config.settingsWidth = std::clamp(ToInt(value, 874), 300, 6000);
            if (config.settingsWidth == 672) config.settingsWidth = 874; // the default of 0.2.3, never chosen by hand
        } else if (name == L"settingsheight") {
            config.settingsHeight = std::clamp(ToInt(value, 1052), 400, 6000);
            for (const int earlier : { 810, 856, 888, 958, 1018 }) { // defaults of earlier versions, never chosen by hand
                if (config.settingsHeight == earlier) config.settingsHeight = 1052;
            }
        } else if (name == L"shownumbers") {
            config.showNumbers = value == L"1";
        } else if (name == L"showlabel") {
            config.showLabel = value == L"1";
        } else if (name.rfind(L"colour.", 0) == 0) {
            const unsigned colour = static_cast<unsigned>(std::wcstoul(value.c_str(), nullptr, 16)) & 0xFFFFFF;
            if (name == L"colour.zone") config.zoneColour = colour;
            else if (name == L"colour.highlight") config.highlightColour = colour;
            else if (name == L"colour.border") config.borderColour = colour;
            else if (name == L"colour.text") config.textColour = colour;
        } else if (name == L"monitor") {
            const size_t bar = value.rfind(L'|');
            if (bar == std::wstring::npos) continue;
            const int preset = ToInt(value.substr(bar + 1), 0);
            if (preset >= 1 && preset <= kPresetCount) config.monitorPreset[value.substr(0, bar)] = preset - 1;
        } else if (name == L"preset") {
            const size_t bar = value.find(L'|');
            const int number = ToInt(value.substr(0, bar), 0);
            current = number >= 1 && number <= kPresetCount ? number - 1 : -1;
            if (current >= 0) {
                config.presets[current].name = bar == std::wstring::npos ? L"" : value.substr(bar + 1);
                config.presets[current].zones.clear();
            }
        } else if (name == L"zone" && current >= 0) {
            std::wistringstream numbers(value);
            Zone z;
            if (numbers >> z.left >> z.top >> z.right >> z.bottom) config.presets[current].zones.push_back(z);
        }
    }

    // Names like "61 / 39" were given by early versions and went stale when a line moved. Unnamed, the
    // preset is shown by its real shape instead.
    for (Preset& preset : config.presets) {
        if (!preset.name.empty() && preset.name.find_first_not_of(L"0123456789 /") == std::wstring::npos) preset.name.clear();
    }

    // The defaults of earlier versions (tall before 0.5.0, then nearly square) become the wide one; a size chosen by hand stays.
    if ((config.settingsWidth == 874 && config.settingsHeight == 1052) || (config.settingsWidth == 1200 && config.settingsHeight == 1050)) {
        config.settingsWidth = Config{}.settingsWidth;
        config.settingsHeight = Config{}.settingsHeight;
    }

    // A preset that doesn't cover the screen exactly is dropped, not guessed at.
    for (Preset& preset : config.presets) {
        if (!preset.zones.empty() && !IsTiling(preset.zones)) preset.zones.clear();
    }
    return config;
}

bool LoadConfig(const std::wstring& path, Config& config)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::string bytes;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart < (1 << 20);
    if (ok) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = bytes.empty() || (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size());
    }
    CloseHandle(file);
    if (!ok) return false;
    config = Parse(FromUtf8(bytes));
    return true;
}

bool SaveConfig(const std::wstring& path, const Config& config)
{
    // Written beside the real file and swapped in, so a crash can't leave half a file.
    const std::string bytes = ToUtf8(Serialize(config));
    const std::wstring partial = path + L".tmp";
    HANDLE file = CreateFileW(partial.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(partial.c_str());
        return false;
    }
    return MoveFileExW(partial.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

} // namespace tessera
