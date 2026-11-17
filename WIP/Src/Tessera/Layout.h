// Zone layouts and the edits the editor makes to them. No windows here, so it can be tested on its own.
#pragma once
#include <string>
#include <vector>

namespace tessera {

constexpr int kUnits = 10000;   // a monitor's work area is kUnits wide and kUnits tall, whatever its pixels
constexpr int kMinZone = 500;   // no zone gets narrower or shorter than 5 %
constexpr int kPresetCount = 9; // keys 1 to 9

struct Zone {
    int left = 0, top = 0, right = 0, bottom = 0;
    bool operator==(const Zone&) const = default;
};

struct Preset {
    std::wstring name;
    std::vector<Zone> zones; // empty: the preset isn't in use
    bool Empty() const { return zones.empty(); }
};

// Vertical: an upright line, with zones to its left and right. Horizontal: zones above and below.
enum class Axis { Vertical, Horizontal };

// A stretch of a line that zones on both sides share, which moves as one piece.
struct Divider {
    Axis axis = Axis::Vertical;
    int position = 0;        // x of an upright line, y of a level one
    int from = 0, to = 0;    // where it runs along the other direction
    std::vector<int> before; // zones ending on it (their right or bottom edge)
    std::vector<int> after;  // zones starting on it (their left or top edge)
    bool Valid() const { return !before.empty() && !after.empty(); }
};

// The zone holding a point, or -1.
int ZoneAt(const std::vector<Zone>& zones, int x, int y);

// Cuts a zone in two at position. False when either part would be smaller than kMinZone.
bool SplitZone(std::vector<Zone>& zones, int index, Axis axis, int position);

// The divider nearest a point, within the tolerances given. Not Valid() when there is none.
Divider FindDivider(const std::vector<Zone>& zones, int x, int y, int toleranceX, int toleranceY);

// Moves a divider as far towards position as kMinZone allows, and returns where it ended.
int MoveDivider(std::vector<Zone>& zones, const Divider& divider, int position);

// Joins the two zones a divider separates. False when it separates more than two.
bool RemoveDivider(std::vector<Zone>& zones, const Divider& divider);

// A name for a layout from its shape: "41% / 59%" for columns side by side (or rows stacked), "3 zones"
// for anything else, nothing for no zones. Used wherever a preset has not been given a name.
std::wstring AutoName(const std::vector<Zone>& zones);

// True when the zones cover the whole area with no overlap.
bool IsTiling(const std::vector<Zone>& zones);

} // namespace tessera
