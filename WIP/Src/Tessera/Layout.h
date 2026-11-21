// Zone layouts and the edits the editor makes to them. No windows here, so it can be tested on its own.
#pragma once
#include <string>
#include <vector>

namespace tessera {

constexpr int kUnits = 10000;   // a monitor's work area is kUnits wide and kUnits tall, whatever its pixels
constexpr int kMinZone = 500;   // no zone gets narrower or shorter than 5 %
constexpr int kPresetCount = 9; // keys 1 to 9
constexpr int kMaxZones = 128;  // the most one preset holds: a zone can't be split once there are this many

// Vertical: an upright line, with zones to its left and right. Horizontal: zones above and below.
enum class Axis { Vertical, Horizontal };

struct Zone {
    int left = 0, top = 0, right = 0, bottom = 0;
    // Sub-zones: the zone cut up further, for when a smaller place is wanted than the zone itself. They
    // cover the zone exactly, in units of their own: kUnits across the zone and kUnits down it, so they keep
    // their share of it when its lines move. Empty for a zone that has none, and always for a sub-zone.
    std::vector<Zone> subs;
    bool operator==(const Zone&) const = default;
};

struct Preset {
    std::wstring name;
    std::vector<Zone> zones; // empty: the preset isn't in use
    bool Empty() const { return zones.empty(); }
};


// A stretch of a line that zones on both sides share, which moves as one piece.
struct Divider {
    Axis axis = Axis::Vertical;
    int position = 0;        // x of an upright line, y of a level one
    int from = 0, to = 0;    // where it runs along the other direction
    std::vector<int> before; // zones ending on it (their right or bottom edge)
    std::vector<int> after;  // zones starting on it (their left or top edge)
    bool Valid() const { return !before.empty() && !after.empty(); }
};

// Reading order, so zone numbers run left to right, top to bottom. Splitting and joining leave the zones
// sorted; moving a line can change the order, so whoever moves one sorts afterwards.
void SortZones(std::vector<Zone>& zones);

// A sub-zone in the units of the screen, where the zone it lies in is given.
Zone Inside(const Zone& parent, const Zone& sub);

// Every place a window can be dropped on: the zones, and for one that has sub-zones those in its stead.
// It is this that kMaxZones limits.
int CountZones(const std::vector<Zone>& zones);

// The least a sub-zone of this zone may measure in that direction, in the zone's own units: 5 % of the
// screen, like any zone.
int SubMinimum(const Zone& parent, Axis axis);

// True when a zone's sub-zones, if it has any, are at least two and cover it with no overlap.
bool SubsSound(const Zone& zone);

// The zone holding a point, or -1.
int ZoneAt(const std::vector<Zone>& zones, int x, int y);

// Cuts a zone in two at position. False when either part would be smaller than minimum, or there are
// kMaxZones already. A zone that is cut loses its sub-zones.
bool SplitZone(std::vector<Zone>& zones, int index, Axis axis, int position, int minimum = kMinZone);

// The divider nearest a point, within the tolerances given. Not Valid() when there is none. only: look
// for upright lines alone, or level ones; where two lines meet, the point belongs to both.
Divider FindDivider(const std::vector<Zone>& zones, int x, int y, int toleranceX, int toleranceY, const Axis* only = nullptr);

// Moves a divider as far towards position as minimum allows, and returns where it ended.
int MoveDivider(std::vector<Zone>& zones, const Divider& divider, int position, int minimum = kMinZone);

// Joins the two zones a divider separates. False when it separates more than two. What is joined has no
// sub-zones.
bool RemoveDivider(std::vector<Zone>& zones, const Divider& divider);

// A name for a layout from its shape: "41% / 59%" for columns side by side (or rows stacked), "3 zones"
// for anything else, nothing for no zones. Used wherever a preset has not been given a name.
std::wstring AutoName(const std::vector<Zone>& zones);

// True when the zones cover the whole area with no overlap.
bool IsTiling(const std::vector<Zone>& zones);

} // namespace tessera
