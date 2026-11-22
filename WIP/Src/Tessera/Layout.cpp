#include "Layout.h"

#include <algorithm>
#include <cstdlib>

namespace tessera {

namespace {

int Start(const Zone& z, Axis axis) { return axis == Axis::Vertical ? z.left : z.top; }
int End(const Zone& z, Axis axis) { return axis == Axis::Vertical ? z.right : z.bottom; }
int SpanFrom(const Zone& z, Axis axis) { return axis == Axis::Vertical ? z.top : z.left; }
int SpanTo(const Zone& z, Axis axis) { return axis == Axis::Vertical ? z.bottom : z.right; }

void SetStart(Zone& z, Axis axis, int v) { (axis == Axis::Vertical ? z.left : z.top) = v; }
void SetEnd(Zone& z, Axis axis, int v) { (axis == Axis::Vertical ? z.right : z.bottom) = v; }

} // namespace

void SortZones(std::vector<Zone>& zones)
{
    std::sort(zones.begin(), zones.end(), [](const Zone& a, const Zone& b) {
        if (a.top != b.top) return a.top < b.top;
        if (a.left != b.left) return a.left < b.left;
        if (a.right != b.right) return a.right < b.right; // sub-zones can share a corner: the narrower, then the shorter, first
        return a.bottom < b.bottom;
    });
    for (Zone& zone : zones) {
        if (!zone.subs.empty()) SortZones(zone.subs);
    }
}

Zone Inside(const Zone& parent, const Zone& sub)
{
    const int w = parent.right - parent.left, h = parent.bottom - parent.top;
    const auto part = [](int share, int extent) { return static_cast<int>((static_cast<long long>(share) * extent + kUnits / 2) / kUnits); };
    return { parent.left + part(sub.left, w), parent.top + part(sub.top, h), parent.left + part(sub.right, w), parent.top + part(sub.bottom, h), {} };
}

int CountZones(const std::vector<Zone>& zones)
{
    int count = 0;
    for (const Zone& zone : zones) count += zone.subs.empty() ? 1 : static_cast<int>(zone.subs.size());
    return count;
}

int SubMinimum(const Zone& parent, Axis axis)
{
    const int extent = End(parent, axis) - Start(parent, axis);
    if (extent <= 0) return kUnits;
    return static_cast<int>((static_cast<long long>(kMinZone) * kUnits + extent - 1) / extent);
}

bool SubsSound(const Zone& zone)
{
    for (const Zone& sub : zone.subs) {
        if (!sub.subs.empty()) return false;
        if (sub.left < 0 || sub.top < 0 || sub.right > kUnits || sub.bottom > kUnits) return false;
        if (sub.left >= sub.right || sub.top >= sub.bottom) return false;
    }
    return true;
}

int SmallestAt(const std::vector<Zone>& zones, int x, int y)
{
    x = std::clamp(x, 0, kUnits - 1);
    y = std::clamp(y, 0, kUnits - 1);
    int found = -1;
    long long least = 0;
    for (size_t i = 0; i < zones.size(); ++i) {
        const Zone& z = zones[i];
        if (x < z.left || x >= z.right || y < z.top || y >= z.bottom) continue;
        const long long area = static_cast<long long>(z.right - z.left) * (z.bottom - z.top);
        if (found < 0 || area <= least) { // of two the same size, the later: the one drawn on top
            found = static_cast<int>(i);
            least = area;
        }
    }
    return found;
}

Zone FreePart(const Zone& own, const std::vector<Zone>& over)
{
    const auto area = [](const Zone& z) { return static_cast<long long>(z.right - z.left) * (z.bottom - z.top); };
    std::vector<Zone> free = { { own.left, own.top, own.right, own.bottom, {} } };
    for (const Zone& o : over) {
        std::vector<Zone> next;
        for (const Zone& c : free) {
            if (o.right <= c.left || c.right <= o.left || o.bottom <= c.top || c.bottom <= o.top) {
                next.push_back(c);
                continue;
            }
            // What is left of it: the strips to the left and right of the other, and above and below it.
            if (o.left > c.left) next.push_back({ c.left, c.top, o.left, c.bottom, {} });
            if (o.right < c.right) next.push_back({ o.right, c.top, c.right, c.bottom, {} });
            if (o.top > c.top) next.push_back({ c.left, c.top, c.right, o.top, {} });
            if (o.bottom < c.bottom) next.push_back({ c.left, o.bottom, c.right, c.bottom, {} });
        }
        // The strips overlap one another, and many are wholly inside another: only the ones that are not are
        // kept, or copies of the same few would crowd out a small part that is really free.
        std::vector<Zone> kept;
        for (size_t a = 0; a < next.size(); ++a) {
            bool inside = false;
            for (size_t b = 0; b < next.size() && !inside; ++b) {
                if (a == b) continue;
                const bool within = next[a].left >= next[b].left && next[a].top >= next[b].top && next[a].right <= next[b].right && next[a].bottom <= next[b].bottom;
                inside = within && (!(next[a] == next[b]) || b < a); // of two alike, the first stays
            }
            if (!inside) kept.push_back(next[a]);
        }
        next = std::move(kept);
        if (next.size() > 64) { // enough to choose from, however many lie over it
            std::sort(next.begin(), next.end(), [&](const Zone& a, const Zone& b) { return area(a) > area(b); });
            next.resize(64);
        }
        free = std::move(next);
    }
    Zone best{};
    for (const Zone& c : free) {
        if (area(c) > area(best)) best = c;
    }
    return best;
}

Divider FindEdge(const std::vector<Zone>& zones, int x, int y, int toleranceX, int toleranceY)
{
    Divider best;
    long long bestOutside = 0, bestDistance = 0, bestArea = 0;
    for (size_t i = 0; i < zones.size(); ++i) {
        const Zone& z = zones[i];
        const long long outside = x >= z.left && x <= z.right && y >= z.top && y <= z.bottom ? 0 : 1;
        const long long area = static_cast<long long>(z.right - z.left) * (z.bottom - z.top);
        for (Axis axis : { Axis::Vertical, Axis::Horizontal }) {
            const int across = axis == Axis::Vertical ? x : y;
            const int along = axis == Axis::Vertical ? y : x;
            const int tolerance = std::max(1, axis == Axis::Vertical ? toleranceX : toleranceY);
            if (along < SpanFrom(z, axis) || along > SpanTo(z, axis)) continue;
            for (const bool end : { false, true }) {
                const int edge = end ? End(z, axis) : Start(z, axis);
                const int distance = std::abs(across - edge);
                if (distance > tolerance) continue;
                const long long scaled = static_cast<long long>(distance) * 1000 / tolerance;
                const bool better = !best.Valid() || outside < bestOutside || (outside == bestOutside && (scaled < bestDistance ||
                                    (scaled == bestDistance && area < bestArea)));
                if (!better) continue;
                bestOutside = outside;
                bestDistance = scaled;
                bestArea = area;
                best = Divider{ axis, edge, SpanFrom(z, axis), SpanTo(z, axis), {}, {} };
                (end ? best.before : best.after).push_back(static_cast<int>(i));
            }
        }
    }
    return best;
}

int ZoneAt(const std::vector<Zone>& zones, int x, int y)
{
    x = std::clamp(x, 0, kUnits - 1);
    y = std::clamp(y, 0, kUnits - 1);
    for (size_t i = 0; i < zones.size(); ++i) {
        const Zone& z = zones[i];
        if (x >= z.left && x < z.right && y >= z.top && y < z.bottom) return static_cast<int>(i);
    }
    return -1;
}

bool SplitZone(std::vector<Zone>& zones, int index, Axis axis, int position, int minimum)
{
    if (index < 0 || index >= static_cast<int>(zones.size())) return false;
    if (zones.size() >= static_cast<size_t>(kMaxZones)) return false;
    Zone whole = zones[index];
    if (position < Start(whole, axis) + minimum || position > End(whole, axis) - minimum) return false;
    whole.subs.clear();

    Zone first = whole, second = whole;
    SetEnd(first, axis, position);
    SetStart(second, axis, position);
    zones[index] = first;
    zones.push_back(second);
    SortZones(zones);
    return true;
}

Divider FindDivider(const std::vector<Zone>& zones, int x, int y, int toleranceX, int toleranceY, const Axis* only)
{
    Divider best;
    long long bestDistance = -1;

    // The nearest inner edge under the point picks the line and where to start on it.
    for (const Zone& z : zones) {
        for (Axis axis : { Axis::Vertical, Axis::Horizontal }) {
            if (only && axis != *only) continue;
            const int across = axis == Axis::Vertical ? x : y;
            const int along = axis == Axis::Vertical ? y : x;
            const int tolerance = std::max(1, axis == Axis::Vertical ? toleranceX : toleranceY);
            if (along < SpanFrom(z, axis) || along > SpanTo(z, axis)) continue;
            for (int edge : { Start(z, axis), End(z, axis) }) {
                if (edge <= 0 || edge >= kUnits) continue;
                const int distance = std::abs(across - edge);
                if (distance > tolerance) continue;
                const long long scaled = static_cast<long long>(distance) * 1000 / tolerance;
                if (bestDistance < 0 || scaled < bestDistance) {
                    bestDistance = scaled;
                    best = Divider{ axis, edge, SpanFrom(z, axis), SpanTo(z, axis), {}, {} };
                }
            }
        }
    }
    if (bestDistance < 0) return best;

    // Grow along the line: every zone touching the stretch found so far belongs to it.
    std::vector<bool> taken(zones.size(), false);
    for (bool grew = true; grew;) {
        grew = false;
        for (size_t i = 0; i < zones.size(); ++i) {
            if (taken[i]) continue;
            const Zone& z = zones[i];
            const bool ends = End(z, best.axis) == best.position;
            const bool starts = Start(z, best.axis) == best.position;
            if (!ends && !starts) continue;
            if (std::min(best.to, SpanTo(z, best.axis)) <= std::max(best.from, SpanFrom(z, best.axis))) continue;
            taken[i] = true;
            grew = true;
            best.from = std::min(best.from, SpanFrom(z, best.axis));
            best.to = std::max(best.to, SpanTo(z, best.axis));
            (ends ? best.before : best.after).push_back(static_cast<int>(i));
        }
    }
    return best;
}

int MoveDivider(std::vector<Zone>& zones, const Divider& divider, int position, int minimum)
{
    if (!divider.Valid()) return divider.position;
    int low = 0, high = kUnits;
    for (int i : divider.before) low = std::max(low, Start(zones[i], divider.axis) + minimum);
    for (int i : divider.after) high = std::min(high, End(zones[i], divider.axis) - minimum);
    if (low > high) return divider.position;

    position = std::clamp(position, low, high);
    for (int i : divider.before) SetEnd(zones[i], divider.axis, position);
    for (int i : divider.after) SetStart(zones[i], divider.axis, position);
    return position;
}

bool RemoveDivider(std::vector<Zone>& zones, const Divider& divider)
{
    if (divider.before.size() != 1 || divider.after.size() != 1) return false;
    Zone& keep = zones[divider.before[0]];
    const Zone gone = zones[divider.after[0]];
    if (SpanFrom(keep, divider.axis) != SpanFrom(gone, divider.axis) ||
        SpanTo(keep, divider.axis) != SpanTo(gone, divider.axis)) return false;

    SetEnd(keep, divider.axis, End(gone, divider.axis));
    keep.subs.clear();
    zones.erase(zones.begin() + divider.after[0]);
    SortZones(zones);
    return true;
}

std::wstring AutoName(const std::vector<Zone>& zones)
{
    if (zones.empty()) return L"";
    for (Axis axis : { Axis::Vertical, Axis::Horizontal }) {
        // Columns: every zone runs the full height. Rows: the full width.
        bool strips = true;
        for (const Zone& z : zones) strips = strips && SpanFrom(z, axis) == 0 && SpanTo(z, axis) == kUnits;
        if (!strips) continue;
        if (zones.size() > 6) break; // too long to read, and for the name box to hold
        std::wstring name;
        for (const Zone& z : zones) {
            if (!name.empty()) name += L" / ";
            name += std::to_wstring((End(z, axis) - Start(z, axis) + 50) / 100) + L"%";
        }
        return name;
    }
    return std::to_wstring(zones.size()) + L" zones";
}

bool IsTiling(const std::vector<Zone>& zones)
{
    long long area = 0;
    for (const Zone& z : zones) {
        if (z.left < 0 || z.top < 0 || z.right > kUnits || z.bottom > kUnits) return false;
        if (z.left >= z.right || z.top >= z.bottom) return false;
        area += static_cast<long long>(z.right - z.left) * (z.bottom - z.top);
    }
    if (area != static_cast<long long>(kUnits) * kUnits) return false;

    for (size_t a = 0; a < zones.size(); ++a) {
        for (size_t b = a + 1; b < zones.size(); ++b) {
            const bool apart = zones[a].right <= zones[b].left || zones[b].right <= zones[a].left ||
                               zones[a].bottom <= zones[b].top || zones[b].bottom <= zones[a].top;
            if (!apart) return false;
        }
    }
    return true;
}

} // namespace tessera
