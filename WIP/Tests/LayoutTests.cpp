// Checks the layout edits and the settings file, with no windows involved.
#include "../Src/Tessera/Config.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

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
    CHECK(MoveDivider(zones, top, 7000) == 7000);
    CHECK(IsTiling(zones));
    CHECK(zones[0].right == 7000 && zones[1].left == 7000 && zones[2].right == 5000 && zones[3].left == 5000); // the bottom half stayed

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

    {
        // A preset holds kMaxZones and no more: sixteen columns of eight rows is 128, and there it stops.
        std::vector<Zone> full = whole;
        for (int column = 15; column >= 1; --column) CHECK(SplitZone(full, 0, Axis::Vertical, column * 625));
        for (int column = 0; column < 16; ++column) {
            for (int row = 7; row >= 1; --row) CHECK(SplitZone(full, ZoneAt(full, column * 625 + 10, 10), Axis::Horizontal, row * 1250));
        }
        CHECK(static_cast<int>(full.size()) == kMaxZones && IsTiling(full));
        CHECK(!SplitZone(full, 0, Axis::Vertical, 312) || full[0].right - full[0].left < kMinZone); // too narrow anyway
        std::vector<Zone> roomy = { { 0, 0, 5000, kUnits }, { 5000, 0, kUnits, kUnits } };
        while (static_cast<int>(roomy.size()) < kMaxZones) roomy.push_back(roomy.back()); // the count is what stops it, not the room
        CHECK(!SplitZone(roomy, 0, Axis::Vertical, 2500) && static_cast<int>(roomy.size()) == kMaxZones);
    }
    {
        // Sub-zones: a zone cut up further, in units of its own.
        const Zone leftHalf = { 0, 0, 5000, kUnits, {} };
        CHECK((Inside(leftHalf, { 5000, 7500, kUnits, kUnits, {} }) == Zone{ 2500, 7500, 5000, kUnits, {} }));
        CHECK((Inside({ 3333, 0, 6667, kUnits, {} }, { 0, 0, kUnits, kUnits, {} }) == Zone{ 3333, 0, 6667, kUnits, {} })); // all of it is all of it
        CHECK(SubMinimum(leftHalf, Axis::Vertical) == 1000 && SubMinimum(leftHalf, Axis::Horizontal) == 500); // 5 % of the screen
        CHECK(SubMinimum({ 0, 0, 500, kUnits, {} }, Axis::Vertical) == kUnits); // too narrow to cut at all

        Config config = DefaultConfig();
        std::vector<Zone>& mains = config.presets[0].zones;
        mains[0].subs = { { 0, 0, kUnits, kUnits, {} } };
        CHECK(SubsSound(mains[0])); // one that is all of its zone: pointless, but sound
        CHECK(!SplitZone(mains[0].subs, 0, Axis::Vertical, 500, SubMinimum(mains[0], Axis::Vertical))); // under 5 % of the screen
        CHECK(SplitZone(mains[0].subs, 0, Axis::Vertical, 5000, SubMinimum(mains[0], Axis::Vertical)));
        CHECK(SplitZone(mains[0].subs, 1, Axis::Horizontal, 6000, SubMinimum(mains[0], Axis::Horizontal)));
        CHECK(SubsSound(mains[0]) && mains[0].subs.size() == 3 && CountZones(mains) == 4 && IsTiling(mains));
        config.subKey = ActivationKey::Alt;

        bool damaged = true;
        const Config back = Parse(Serialize(config), &damaged);
        CHECK(!damaged && back.presets[0].zones == mains && back.subKey == ActivationKey::Alt);
        CHECK(Parse(L"").subKey == ActivationKey::Shift && Parse(L"subkey=off\n").subKey == ActivationKey::None);
        // A file from before sub-zones, whose Shift is taken: the first free key, not a clash nobody chose.
        CHECK(Parse(L"key=shift\n").subKey == ActivationKey::Alt && Parse(L"span=shift\n").subKey == ActivationKey::Alt &&
              Parse(L"key=shift\nspan=alt\n").subKey == ActivationKey::Ctrl && Parse(L"key=alt\n").subKey == ActivationKey::Shift);
        CHECK(Parse(L"key=shift\nsubkey=shift\n").subKey == ActivationKey::Shift); // chosen, clash and all: it stays
        CHECK(Serialize(back) == Serialize(config));

        // Sub-zones that reach outside their zone are dropped; the zone, and the rest, stay.
        std::wstring text = Serialize(config);
        const std::wstring good = L"sub=5000 6000 10000 10000\n";
        const size_t at = text.find(good);
        CHECK(at != std::wstring::npos);
        if (at != std::wstring::npos) text.replace(at, good.size(), L"sub=5000 6000 10000 10001\n");
        const Config bad = Parse(text, &damaged);
        CHECK(damaged && bad.presets[0].zones.size() == 2 && bad.presets[0].zones[0].subs.empty() && IsTiling(bad.presets[0].zones));
        CHECK(Parse(L"preset=1|\nzone=0 0 10000 10000\nsub=0 0 10000 10000\n", &damaged).presets[0].zones[0].subs.size() == 1 && !damaged);
        CHECK(Parse(L"preset=1|\nzone=0 0 10000 10000\nsub=0 0 0 10000\n", &damaged).presets[0].zones[0].subs.empty() && damaged); // of no width
        CHECK(Parse(L"preset=1|\nzone=0 0 10000 10000\nsub=-1 0 5000 10000\n", &damaged).presets[0].zones[0].subs.empty() && damaged);

        // Sub-zones may lie over one another, and leave some of their zone to itself: the same corner at
        // 60 % of the width and at 80 %.
        {
            Config stacked = DefaultConfig();
            std::vector<Zone>& over = stacked.presets[0].zones[0].subs;
            over = { { 0, 0, 6000, kUnits, {} }, { 0, 0, 8000, kUnits, {} } };
            CHECK(SubsSound(stacked.presets[0].zones[0]));
            const Config again = Parse(Serialize(stacked), &damaged);
            CHECK(!damaged && again.presets[0].zones == stacked.presets[0].zones);
            CHECK(SmallestAt(over, 3000, 5000) == 0 && SmallestAt(over, 7000, 5000) == 1 && SmallestAt(over, 9000, 5000) == -1);
            // Where the wider one's number goes: the strip the narrower leaves of it. And what is left of the zone.
            CHECK((FreePart(over[1], { over[0] }) == Zone{ 6000, 0, 8000, kUnits, {} }));
            CHECK((FreePart({ 0, 0, kUnits, kUnits, {} }, over) == Zone{ 8000, 0, kUnits, kUnits, {} }));
            {
                // Of two the same size lying partly over one another, the later is the one meant: it is drawn on top.
                const std::vector<Zone> twins = { { 0, 0, 6000, kUnits, {} }, { 4000, 0, kUnits, kUnits, {} } };
                CHECK(SmallestAt(twins, 5000, 5000) == 1 && SmallestAt(twins, 2000, 5000) == 0);
                // A grid of thirty-six with one cell missing: the missing cell is found, however many strips it took.
                std::vector<Zone> grid;
                for (int row = 0; row < 6; ++row) {
                    for (int column = 0; column < 6; ++column) {
                        if (row == 1 && column == 4) continue;
                        grid.push_back({ column * 1500, row * 1500, column == 5 ? kUnits : column * 1500 + 1500, row == 5 ? kUnits : row * 1500 + 1500, {} });
                    }
                }
                CHECK((FreePart({ 0, 0, kUnits, kUnits, {} }, grid) == Zone{ 6000, 1500, 7500, 3000, {} }));
            }
            const Zone none = FreePart(over[0], { over[1] });
            CHECK(none.right <= none.left || none.bottom <= none.top); // covered altogether
            std::vector<Zone> shuffled = { over[1], over[0] };
            SortZones(shuffled);
            CHECK(shuffled == over); // the same corner: the narrower first, however they came

            // One sub-zone's own edge: of two meeting on a line, the one the point is in; and the edge of the
            // zone itself counts.
            std::vector<Zone> pair = { { 0, 0, 5000, kUnits, {} }, { 5000, 0, kUnits, kUnits, {} } };
            const Divider left = FindEdge(pair, 5040, 5000, 60, 60);
            CHECK(left.Valid() && left.before.empty() && left.after == std::vector<int>{ 1 } && left.position == 5000);
            const Divider right = FindEdge(pair, 4960, 5000, 60, 60);
            CHECK(right.Valid() && right.after.empty() && right.before == std::vector<int>{ 0 });
            CHECK(!FindEdge(pair, 2500, 5000, 60, 60).Valid());
            CHECK(MoveDivider(pair, left, 2000, 1000) == 2000 && pair[0].right == 5000 && pair[1].left == 2000); // the one moved, the other stayed
            const Divider rim = FindEdge(pair, 9980, 5000, 60, 60);
            CHECK(rim.Valid() && rim.position == kUnits && rim.before == std::vector<int>{ 1 });
            CHECK(MoveDivider(pair, rim, 8000, 1000) == 8000 && MoveDivider(pair, FindEdge(pair, 8000, 5000, 60, 60), 99999, 1000) == kUnits); // in, and no further out than the zone
            CHECK(MoveDivider(pair, FindEdge(pair, 2010, 5000, 60, 60), 9900, 1000) == 9000); // not narrower than the least
        }
        CHECK(Parse(L"sub=0 0 5000 10000\nsub=5000 0 10000 10000\n", &damaged).presets[0].Empty()); // sub-zones of nothing

        // They go where their zone goes: sorted with it, moved with its lines, and lost when it is cut or joined.
        std::vector<Zone> moved = mains;
        const Divider line = FindDivider(moved, 5000, 5000, 10, 10);
        CHECK(line.Valid() && MoveDivider(moved, line, 3000) == 3000 && moved[0].subs == mains[0].subs && SubsSound(moved[0]));
        std::swap(moved[0], moved[1]);
        SortZones(moved);
        CHECK(moved[0].subs.size() == 3 && moved[1].subs.empty());
        std::vector<Zone> cut = mains;
        CHECK(SplitZone(cut, 0, Axis::Horizontal, 5000) && cut.size() == 3 && CountZones(cut) == 3);
        std::vector<Zone> joined = mains;
        CHECK(RemoveDivider(joined, FindDivider(joined, 5000, 5000, 10, 10)) && joined.size() == 1 && joined[0].subs.empty());
    }
    // A long run of random edits to one zone's sub-zones, as the editor makes them: cut, a line moved, one
    // edge moved by itself, a line removed, one taken away. Whatever is done, each stays inside the zone and
    // no smaller than the least, and none is lost or made by an edit that should not.
    {
        unsigned seed = 20261122;
        const auto random = [&seed](int below) {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<int>((seed >> 8) % static_cast<unsigned>(below));
        };
        constexpr int least = 800;
        std::vector<Zone> subs = { { 0, 0, kUnits, kUnits, {} } };
        int cuts = 0, lines = 0, edges = 0, joins = 0, most = 0;
        bool sound = true;
        for (int step = 0; step < 60000 && sound; ++step) {
            const int x = random(kUnits + 1), y = random(kUnits + 1);
            const size_t before = subs.size();
            switch (random(10)) {
            case 0: case 1: case 2: {
                const int at = SmallestAt(subs, x, y);
                const Axis axis = random(2) ? Axis::Vertical : Axis::Horizontal;
                if (at >= 0 && SplitZone(subs, at, axis, axis == Axis::Vertical ? x : y, least)) {
                    ++cuts;
                    sound = sound && subs.size() == before + 1;
                } else {
                    sound = sound && subs.size() == before;
                }
                break;
            }
            case 3: case 4: {
                const Divider line = FindDivider(subs, x, y, 300, 300);
                if (line.Valid()) {
                    MoveDivider(subs, line, random(kUnits + 1), least);
                    SortZones(subs);
                    ++lines;
                }
                sound = sound && subs.size() == before;
                break;
            }
            case 5: case 6: case 7: {
                const Divider edge = FindEdge(subs, x, y, 300, 300);
                if (edge.Valid()) {
                    sound = sound && edge.before.size() + edge.after.size() == 1;
                    MoveDivider(subs, edge, random(kUnits + 1), least);
                    SortZones(subs);
                    ++edges;
                }
                sound = sound && subs.size() == before;
                break;
            }
            case 8: {
                const Divider line = FindDivider(subs, x, y, 300, 300);
                if (line.Valid() && RemoveDivider(subs, line)) {
                    ++joins;
                    sound = sound && subs.size() == before - 1;
                } else {
                    sound = sound && subs.size() == before;
                }
                break;
            }
            default:
                if (subs.size() > 40 || (subs.size() > 1 && random(4) == 0)) subs.erase(subs.begin() + random(static_cast<int>(subs.size())));
                break;
            }
            Zone holder = { 0, 0, kUnits, kUnits, subs };
            sound = sound && SubsSound(holder) && !subs.empty() && subs.size() <= static_cast<size_t>(kMaxZones);
            for (const Zone& s : subs) sound = sound && s.right - s.left >= least && s.bottom - s.top >= least;
            most = std::max(most, static_cast<int>(subs.size()));
        }
        CHECK(sound);
        std::printf("random sub-zone edits: %d cuts, %d lines moved, %d edges moved alone, %d joins, up to %d at once\n", cuts, lines, edges, joins, most);
        CHECK(cuts > 500 && lines > 1000 && edges > 1000 && joins > 20);
    }
    // Not tilings.
    CHECK(!IsTiling({}));
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
    // Old defaults are only replaced in a file an old version wrote. One this version wrote says so, and in
    // it the same values were chosen by hand: a fade of 110 ms, a preset named "50 / 50".
    CHECK(Parse(L"format=2\nfade=110\n").fade == 110);
    {
        // Two rows, the top one cut in half: the level line's middle is where the upright one meets it. Asked
        // for by kind, each is found there.
        const std::vector<Zone> tee = { { 0, 0, 5000, 5000 }, { 5000, 0, 10000, 5000 }, { 0, 5000, 10000, 10000 } };
        const Axis onlyLevel = Axis::Horizontal, onlyUpright = Axis::Vertical;
        const Divider across = FindDivider(tee, 5000, 5000, 1, 1, &onlyLevel);
        CHECK(across.Valid() && across.axis == Axis::Horizontal && across.from == 0 && across.to == 10000 && across.before.size() == 2);
        CHECK(FindDivider(tee, 5000, 5000, 1, 1, &onlyUpright).axis == Axis::Vertical);
    }
    {
        // A file that reads but can't all be used is said to be damaged, so a copy can be kept.
        bool unusable = true;
        Parse(L"# a comment\nkey=shift\npreset=1|\nzone=0 0 10000 10000\n", &unusable);
        CHECK(!unusable);
        Parse(L"", &unusable);
        CHECK(!unusable);
        Parse(L"preset=1|\nzone=0 0 9999 10000\n", &unusable); // doesn't cover the screen
        CHECK(unusable);
        Parse(L"nothing Tessera wrote\nat all\n", &unusable);
        CHECK(unusable);
    }
    // Who switched Windows' snap bar off: Tessera, if an earlier version saved the option as ticked; not
    // Tessera in a file that says so, or with no file at all.
    CHECK(Parse(L"pausesnap=1\n").hidSnapBar && !Parse(L"pausesnap=0\n").hidSnapBar && !Parse(L"").hidSnapBar);
    CHECK(!Parse(L"pausesnap=1\nhidsnapbar=0\n").hidSnapBar && Parse(L"pausesnap=0\nhidsnapbar=1\n").hidSnapBar);
    CHECK(Parse(L"format=2\nsettingswidth=874\nsettingsheight=1052\n").settingsHeight == 1052);
    CHECK(Parse(L"format=2\npreset=1|50 / 50\nzone=0 0 10000 10000\n").presets[0].name == L"50 / 50");
    {
        Config chosen;
        chosen.fade = 110;
        CHECK(Parse(Serialize(chosen)).fade == 110);
    }
    {
        // Moving a line can put zones out of reading order; SortZones puts them back.
        std::vector<Zone> shuffled = { { 0, 5000, 5000, 10000 }, { 5000, 3000, 10000, 10000 }, { 0, 0, 5000, 5000 }, { 5000, 0, 10000, 3000 } };
        SortZones(shuffled);
        CHECK(shuffled[0].left == 0 && shuffled[1].left == 5000 && shuffled[1].top == 0 && shuffled[2].top == 3000 && shuffled[3].top == 5000);
    }
    CHECK(damaged.presets[0].name == L"Bad" && damaged.presets[0].Empty());
    for (const Preset& preset : damaged.presets) CHECK(preset.Empty());

    {
        // A file can hold anything: a name is cut to what could have been typed, and a preset of more zones
        // than Tessera could ever make is dropped.
        CHECK(Parse(L"format=2\npreset=1|" + std::wstring(500, L'x') + L"\nzone=0 0 10000 10000\n").presets[0].name.size() == 40);
        CHECK(Parse(L"format=2\npreset=1|a\tb\nzone=0 0 10000 10000\n").presets[0].name == L"ab");
        std::wstring grid = L"preset=1|\n";
        for (int row = 0; row < 25; ++row) {
            for (int column = 0; column < 20; ++column) {
                grid += L"zone=" + std::to_wstring(column * 500) + L" " + std::to_wstring(row * 400) + L" " + std::to_wstring(column * 500 + 500) + L" " +
                        std::to_wstring(row * 400 + 400) + L"\n";
            }
        }
        bool unusable = false;
        CHECK(Parse(grid, &unusable).presets[0].Empty() && unusable);
    }
    // Thousands of garbled settings files. Whatever is in one, reading it must leave every setting within
    // its range and every preset either empty or a full tiling, and writing that out and reading it back
    // must give the same again.
    {
        unsigned seed = 20261118;
        const auto random = [&seed](int below) {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<int>((seed >> 8) % static_cast<unsigned>(below));
        };
        const wchar_t* names[] = { L"format", L"key", L"rightclick", L"opacity", L"span", L"pausesnap", L"hidsnapbar", L"gap", L"padding",
                                   L"fade", L"settingswidth", L"settingsheight", L"shownumbers", L"showlabel", L"colour.zone",
                                   L"colour.highlight", L"colour.border", L"colour.text", L"colour.nothing", L"monitor", L"preset", L"zone",
                                   L"sub", L"sub", L"subkey", L"", L"#", L"nonsense" };
        const wchar_t* values[] = { L"", L"0", L"1", L"2", L"-1", L"99999999999999999999", L"-99999999999999999999", L"shift", L"ctrl", L"alt",
                                    L"off", L"none", L"ffffff", L"zzzz", L"1|", L"9|Name", L"10|x", L"0|x", L"|", L"||3", L"\\\\.\\DISPLAY1|2",
                                    L"\\\\.\\DISPLAY1|0", L"x|99", L"0 0 10000 10000", L"0 0 5000 10000", L"5000 0 10000 10000",
                                    L"0 0 10000 5000", L"0 5000 10000 10000", L"0 0 0 0", L"5 5 1 1", L"-5 0 10000 10000", L"0 0 99999 99999",
                                    L"1 2 3", L"a b c d", L"0 0 10000 10000 7", L"2147483647 0 -2147483648 1", L"50 / 50", L"=", L"\r", L" " };
        int files = 0, kept = 0, cut = 0;
        for (; files < 4000; ++files) {
            std::wstring text;
            for (int lines = random(30); lines > 0; --lines) {
                text += names[random(static_cast<int>(std::size(names)))];
                if (random(12)) text += L"=";
                for (int parts = random(3); parts >= 0; --parts) text += values[random(static_cast<int>(std::size(values)))];
                text += random(6) ? L"\n" : L"\r\n";
            }
            bool unusable = false;
            const Config read = Parse(text, &unusable);
            bool sound = read.opacity >= 5 && read.opacity <= 95 && read.gap >= 0 && read.gap <= 40 && read.padding >= 0 && read.padding <= 24 &&
                         read.fade >= 0 && read.fade <= 200 && read.settingsWidth >= 300 && read.settingsWidth <= 6000 &&
                         read.settingsHeight >= 400 && read.settingsHeight <= 6000 && read.zoneColour <= 0xFFFFFF && read.textColour <= 0xFFFFFF;
            for (const Preset& preset : read.presets) {
                sound = sound && (preset.Empty() || IsTiling(preset.zones));
                kept += preset.Empty() ? 0 : 1;
                for (const Zone& zone : preset.zones) { // and every zone's sub-zones, if it has any, cover it
                    sound = sound && SubsSound(zone);
                    cut += zone.subs.empty() ? 0 : 1;
                }
            }
            for (const auto& [monitor, preset] : read.monitorPreset) sound = sound && preset >= 0 && preset < kPresetCount;
            const std::wstring written = Serialize(read);
            sound = sound && Serialize(Parse(written)) == written;
            if (!sound) {
                CHECK(sound);
                break;
            }
        }
        std::printf("garbled settings files: %d read, %d presets among them sound, %d zones with sub-zones\n", files, kept, cut);
    }
    // The same from the other end: files Tessera wrote, zones and sub-zones and all, with a few lines then
    // lost, doubled, swapped or altered. Garbage alone seldom makes a preset worth keeping; these mostly do.
    {
        unsigned seed = 20261121;
        const auto random = [&seed](int below) {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<int>((seed >> 8) % static_cast<unsigned>(below));
        };
        int files = 0, kept = 0, cut = 0, spoilt = 0;
        for (; files < 3000; ++files) {
            Config made;
            for (int p = random(4); p >= 0; --p) {
                std::vector<Zone>& tiles = made.presets[random(kPresetCount)].zones;
                tiles = { { 0, 0, kUnits, kUnits, {} } };
                for (int cuts = random(8); cuts > 0; --cuts) {
                    SplitZone(tiles, random(static_cast<int>(tiles.size())), random(2) ? Axis::Vertical : Axis::Horizontal, random(99) * 100 + 100);
                }
                for (int cuts = random(8); cuts > 0; --cuts) {
                    Zone& zone = tiles[random(static_cast<int>(tiles.size()))];
                    const Axis axis = random(2) ? Axis::Vertical : Axis::Horizontal;
                    const bool first = zone.subs.empty();
                    if (first) zone.subs = { { 0, 0, kUnits, kUnits, {} } };
                    const bool done = SplitZone(zone.subs, random(static_cast<int>(zone.subs.size())), axis, random(99) * 100 + 100, SubMinimum(zone, axis));
                    if (!done && first) zone.subs.clear();
                }
            }
            const std::wstring full = Serialize(made);
            {
                const Config same = Parse(full);
                bool equal = true;
                for (int i = 0; i < kPresetCount; ++i) equal = equal && same.presets[i].zones == made.presets[i].zones;
                if (!equal) {
                    CHECK(equal); // what Tessera writes, it reads back as it was
                    break;
                }
            }
            std::vector<std::wstring> lines;
            for (size_t from = 0; from < full.size();) {
                const size_t to = full.find(L'\n', from);
                lines.push_back(full.substr(from, to - from));
                from = to == std::wstring::npos ? full.size() : to + 1;
            }
            for (int harm = 1 + random(3); harm > 0 && !lines.empty(); --harm) {
                const size_t at = static_cast<size_t>(random(static_cast<int>(lines.size())));
                switch (random(5)) {
                case 0: lines.erase(lines.begin() + at); break;
                case 1: lines.insert(lines.begin() + at, lines[at]); break;
                case 2: std::swap(lines[at], lines[static_cast<size_t>(random(static_cast<int>(lines.size())))]); break;
                case 3: if (!lines[at].empty()) lines[at][static_cast<size_t>(random(static_cast<int>(lines[at].size())))] = L"0159 =-sz"[random(9)]; break;
                default: lines.insert(lines.begin() + at, random(2) ? L"sub=0 0 5000 10000" : L"sub=5000 0 10000 10000"); break;
                }
            }
            std::wstring text;
            for (const std::wstring& line : lines) text += line + L"\n";
            bool unusable = false;
            const Config read = Parse(text, &unusable);
            spoilt += unusable ? 1 : 0;
            bool sound = true;
            for (const Preset& preset : read.presets) {
                sound = sound && (preset.Empty() || IsTiling(preset.zones));
                kept += preset.Empty() ? 0 : 1;
                for (const Zone& zone : preset.zones) {
                    sound = sound && SubsSound(zone);
                    cut += zone.subs.empty() ? 0 : 1;
                }
            }
            const std::wstring written = Serialize(read);
            sound = sound && Serialize(Parse(written)) == written;
            if (!sound) {
                CHECK(sound);
                break;
            }
        }
        std::printf("spoilt settings files: %d read, %d found damaged; %d presets and %d zones with sub-zones kept\n", files, spoilt, kept, cut);
        CHECK(kept > 1000 && cut > 500 && spoilt > 300);
    }

    std::printf(s_failed ? "%d check(s) failed\n" : "All layout checks passed\n", s_failed);
    return s_failed ? 1 : 0;
}
