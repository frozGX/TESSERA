// Everything Tessera remembers, kept in Tessera.cfg beside the exe as plain "name=value" lines.
#pragma once
#include "Layout.h"

#include <map>
#include <string>

namespace tessera {

enum class ActivationKey { None, Shift, Ctrl, Alt };

struct Config {
    Preset presets[kPresetCount];
    ActivationKey key = ActivationKey::None;  // held while dragging to show the zones; none: right-click only
    bool rightClickToggles = true;            // a right-click during a drag turns the zones on or off
    ActivationKey spanKey = ActivationKey::Ctrl; // held with the zones showing to stretch the window across several; none: off
    bool pauseWindowsSnap = true;             // Windows' own snapping is off while the zones are showing
    bool hidSnapBar = false;                  // Tessera switched Windows' snap bar off, so it is Tessera's to switch back on
    int opacity = 25;                         // of the zones over the desktop, in percent
    int gap = 0;                              // pixels left between snapped windows, and around the edge
    int fade = 80;                            // milliseconds the zones take to appear and disappear; 0: at once
    int padding = 2;                          // pixels between the zones as they are drawn; 0: edge to edge
    bool showNumbers = true;                  // the number in each zone
    bool showLabel = true;                    // the preset's name, top centre
    unsigned zoneColour = 0x22262E;           // colours are 0xRRGGBB
    unsigned highlightColour = 0x0078D7;
    unsigned borderColour = 0x96A0AF;
    unsigned textColour = 0xFFFFFF;
    int settingsWidth = 1370, settingsHeight = 870; // size the settings window was left at, at 100 % scaling
    std::map<std::wstring, int> monitorPreset; // display name -> preset index
};

// The three splits the user had in FancyZones, on keys 1 to 3.
Config DefaultConfig();

std::wstring Serialize(const Config& config);
Config Parse(const std::wstring& text);

bool LoadConfig(const std::wstring& path, Config& config);
bool SaveConfig(const std::wstring& path, const Config& config);

} // namespace tessera
