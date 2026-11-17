// Checks the layout edits and the settings file, with no windows involved.
#include "../Src/Tessera/Config.h"

#include <algorithm>
#include <cstdio>

using namespace tessera;

static int s_failed = 0;
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::printf("FAILED line %d: %s\n", __LINE__, #condition); \
            ++s_failed; \
        } \
    } while (0)

int main()
{
    const std::vector<Zone> whole = { { 0, 0, kUnits, kUnits } };

    // Splitting keeps a full tiling and reading order.
    std::vector<Zone> zones = whole;
    CHECK(SplitZone(zones, 0, Axis::Vertical, 6000));
    CHECK(zones.size() == 2 && zones[0].right == 6000 && zones[1].left == 6000);
    CHECK(IsTiling(zones));
    CHECK(!SplitZone(zones, 1, Axis::Vertical, 6200));  // would leave a 2 % zone
    CHECK(!SplitZone(zones, 5, Axis::Vertical, 5000));  // no such zone
    CHECK(SplitZone(zones, 1, Axis::Horizontal, 4000)); // right half: top and bottom
    CHECK(zones.size() == 3 && IsTiling(zones));
    CHECK(zones[0] == (Zone{ 0, 0, 6000, 10000 }));
    CHECK(zones[1] == (Zone{ 6000, 0, 10000, 4000 }));
    CHECK(zones[2] == (Zone{ 6000, 4000, 10000, 10000 }));

    CHECK(ZoneAt(zones, 100, 100) == 0);
    CHECK(ZoneAt(zones, 7000, 100) == 1);
    CHECK(ZoneAt(zones, 7000, 9999) == 2);
    CHECK(ZoneAt(zones, kUnits, kUnits) == 2); // the far corner still lands in a zone

    // The upright line carries one zone on the left and both on the right.
    Divider upright = FindDivider(zones, 6010, 5000, 50, 50);
    CHECK(upright.Valid() && upright.axis == Axis::Vertical && upright.position == 6000);
    CHECK(upright.before.size() == 1 && upright.after.size() == 2);
    CHECK(upright.from == 0 && upright.to == kUnits);
    CHECK(!FindDivider(zones, 3000, 5000, 50, 50).Valid());
    CHECK(!FindDivider(zones, 5, 5000, 50, 50).Valid()); // the screen edge is not a divider

    // Moving it stops where a zone would get too small.
    CHECK(MoveDivider(zones, upright, 3000) == 3000);
    CHECK(IsTiling(zones) && zones[0].right == 3000 && zones[1].left == 3000 && zones[2].left == 3000);
    upright.position = 3000;
    CHECK(MoveDivider(zones, upright, 9900) == kUnits - kMinZone);
    CHECK(MoveDivider(zones, upright, 10) == kMinZone);
    CHECK(IsTiling(zones));

    // It can't be removed (three zones on it); the level line can, and then so can the upright one.
    upright = FindDivider(zones, kMinZone, 5000, 50, 50);
    CHECK(!RemoveDivider(zones, upright));
    Divider level = FindDivider(zones, 5000, 4000, 50, 50);
    CHECK(level.Valid() && level.axis == Axis::Horizontal && level.before.size() == 1 && level.after.size() == 1);
    CHECK(RemoveDivider(zones, level));
    CHECK(zones.size() == 2 && IsTiling(zones));
    upright = FindDivider(zones, kMinZone, 5000, 50, 50);
    CHECK(RemoveDivider(zones, upright));
    CHECK(zones == whole);

    // In a 2 x 2 grid the two halves of the upright line move on their own.
    zones = whole;
    SplitZone(zones, 0, Axis::Vertical, 5000);
    SplitZone(zones, 0, Axis::Horizontal, 5000);
    SplitZone(zones, 1, Axis::Horizontal, 5000);
    CHECK(zones.size() == 4 && IsTiling(zones));
    Divider top = FindDivider(zones, 5000, 2000, 50, 50);
    CHECK(top.Valid() && top.axis == Axis::Vertical && top.from == 0 && top.to == 5000);
    CHECK(top.before.size() == 1 && top.after.size() == 1);
    MoveDivider(zones, top, 7000);
    CHECK(IsTiling(zones));

    // Names from shapes.
    CHECK(AutoName({}) == L"");
    CHECK(AutoName({ { 0, 0, 4113, kUnits }, { 4113, 0, kUnits, kUnits } }) == L"41% / 59%");
    CHECK(AutoName({ { 0, 0, kUnits, 3000 }, { 0, 3000, kUnits, kUnits } }) == L"30% / 70%");
    CHECK(AutoName(zones) == L"4 zones");
    CHECK(AutoName(whole) == L"100%");
    {
        std::vector<Zone> strips;
        for (int i = 0; i < 8; ++i) strips.push_back({ i * 1250, 0, (i + 1) * 1250, kUnits });
        CHECK(IsTiling(strips) && AutoName(strips) == L"8 zones");
    }
    CHECK(Parse(L"preset=1|61 / 39\nzone=0 0 10000 10000\npreset=2|Work\nzone=0 0 10000 10000\n").presets[0].name.empty());
    CHECK(Parse(L"preset=2|Work\nzone=0 0 10000 10000\n").presets[1].name == L"Work");

    // A long run of random edits. Whatever is done, in whatever order, the zones must still cover the
    // screen exactly, none may be smaller than the rule allows, and a line must move as one piece.
    {
        unsigned seed = 20261116;
        const auto random = [&seed](int below) {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<int>((seed >> 8) % static_cast<unsigned>(below));
        };
        std::vector<Zone> layout = whole;
        int splits = 0, moves = 0, removals = 0, broken = 0, tooSmall = 0, most = 1;
        for (int step = 0; step < 200000 && !broken && !tooSmall; ++step) {
            const int x = random(kUnits + 1), y = random(kUnits + 1);
            const int action = random(10);
            if (action < 4) {
                if (SplitZone(layout, ZoneAt(layout, x, y), random(2) ? Axis::Vertical : Axis::Horizontal, random(2) ? x : y)) ++splits;
            } else if (action < 8) {
                const Divider line = FindDivider(layout, x, y, 150, 150);
                if (line.Valid()) {
                    const int to = random(kUnits + 1);
                    const int moved = MoveDivider(layout, line, to);
                    if (moved != line.position) ++moves;
                    // Every zone on the line went with it.
                    for (int i : line.before) {
                        if ((line.axis == Axis::Vertical ? layout[i].right : layout[i].bottom) != moved) ++broken;
                    }
                    for (int i : line.after) {
                        if ((line.axis == Axis::Vertical ? layout[i].left : layout[i].top) != moved) ++broken;
                    }
                }
            } else {
                const Divider line = FindDivider(layout, x, y, 150, 150);
                if (line.Valid() && RemoveDivider(layout, line)) ++removals;
            }
            if (!IsTiling(layout)) ++broken;
            for (const Zone& z : layout) {
                if (z.right - z.left < kMinZone || z.bottom - z.top < kMinZone) ++tooSmall;
            }
            most = std::max(most, static_cast<int>(layout.size()));
            if (step % 5000 == 4999) layout = whole; // start over now and then, to see the small layouts again
        }
        CHECK(broken == 0);
        CHECK(tooSmall == 0);
        CHECK(splits > 1000 && moves > 1000 && removals > 100);
        std::printf("random edits: %d splits, %d moves, %d removals, up to %d zones at once\n", splits, moves, removals, most);

        // And whatever came out of it survives being written down and read back.
        Config written;
        written.presets[4].zones = layout;
        CHECK(Parse(Serialize(written)).presets[4].zones == layout);
    }

    // Not tilings.    CHECK(!IsTiling({}));
    CHECK(!IsTiling({ { 0, 0, 5000, kUnits } }));
    CHECK(!IsTiling({ { 0, 0, 6000, kUnits }, { 4000, 0, kUnits, kUnits } }));
    CHECK(!IsTiling({ { 0, 0, 6000, kUnits }, { 4000, 0, 8000, kUnits } })); // same area as the screen, but overlapping

    // The settings file: what is written is what is read.
    Config config = DefaultConfig();
    CHECK(!config.presets[0].Empty() && !config.presets[2].Empty() && config.presets[3].Empty());
    for (const Preset& preset : config.presets) CHECK(preset.Empty() || IsTiling(preset.zones));
    config.key = ActivationKey::Ctrl;
    config.rightClickToggles = false;
    config.opacity = 70;
    config.pauseWindowsSnap = false;
    config.spanKey = ActivationKey::Alt;
    config.fade = 0;
    config.gap = 12;
    config.padding = 0;
    config.showNumbers = false;
    config.showLabel = false;
    config.zoneColour = 0x0A0B0C;
    config.highlightColour = 0xFF8800;
    config.borderColour = 0x000001;
    config.textColour = 0x123456;
    {
        const Config look = Parse(Serialize(config));
        CHECK(!look.pauseWindowsSnap && Parse(L"").pauseWindowsSnap);
        CHECK(look.spanKey == ActivationKey::Alt && Parse(L"").spanKey == ActivationKey::Ctrl && Parse(L"span=1\n").spanKey == ActivationKey::Ctrl &&
              Parse(L"span=0\n").spanKey == ActivationKey::None && look.fade == 0 && Parse(L"fade=9999\n").fade == 200 && Parse(L"").fade == 80 && Parse(L"fade=110\n").fade == 80);
        CHECK(look.gap == 12 && look.padding == 0 && !look.showNumbers && !look.showLabel);
        CHECK(look.zoneColour == 0x0A0B0C && look.highlightColour == 0xFF8800 && look.borderColour == 0x000001 && look.textColour == 0x123456);
        CHECK(Parse(L"gap=500\ncolour.zone=zzz\n").gap == 40);
    }
    config.monitorPreset[L"\\\\.\\DISPLAY1"] = 2;
    config.presets[8] = { L"Odd | name = here", zones };
    const Config back = Parse(Serialize(config));
    CHECK(back.key == ActivationKey::Ctrl && !back.rightClickToggles && back.opacity == 70);
    CHECK(back.monitorPreset.size() == 1 && back.monitorPreset.at(L"\\\\.\\DISPLAY1") == 2);
    for (int i = 0; i < kPresetCount; ++i) {
        CHECK(back.presets[i].name == config.presets[i].name);
        CHECK(back.presets[i].zones == config.presets[i].zones);
    }

    // Damage is dropped, not trusted.
    const Config damaged = Parse(L"opacity=900\npreset=1|Bad\nzone=0 0 5000 10000\npreset=12|Nowhere\nzone=0 0 10000 10000\nrubbish\n");
    CHECK(damaged.opacity == 95);
    CHECK(Parse(L"opacity=1\n").opacity == 5);
    CHECK(Parse(L"settingswidth=672\nsettingsheight=810\n").settingsWidth == 1370); // every earlier default becomes today's
    CHECK(Parse(L"settingswidth=874\nsettingsheight=1052\n").settingsHeight == 870);
    CHECK(Parse(L"settingswidth=874\nsettingsheight=900\n").settingsWidth == 874); // a size chosen by hand stays
    CHECK(damaged.presets[0].name == L"Bad" && damaged.presets[0].Empty());
    for (const Preset& preset : damaged.presets) CHECK(preset.Empty());

    std::printf(s_failed ? "%d check(s) failed\n" : "All layout checks passed\n", s_failed);
    return s_failed ? 1 : 0;
}
