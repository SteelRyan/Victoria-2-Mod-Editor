// maplook.py, its data half — the colours of the map modes that show an amount (the ◐ button next to the map modes).
// Every such mode has its own colouring (the user, 2026-10-15): a SMOOTH scale (the shared one, or one of its own) or
// RANGES — from / to / colour, what lies in no range stays grey (like the State size / Province size filters, which
// always use ranges). Kept in the editor's settings (not in the mod). The popup that edits them (LookPopup) is in
// ui/maplook.
//
// The modes compute their values and ask colour(mode, value, t): `value` in the mode's own unit (people, %, level …),
// `t` its place on a smooth scale (0 … 1, as the mode spreads it).
#pragma once
#include "core/ordered_map.h"

#include <QSettings>
#include <QString>
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace maplook {

using Rgb = std::tuple<int, int, int>;
using Stops = std::vector<std::pair<double, Rgb>>;
struct Range {                                   // [from, to (None: and above), (r, g, b)]
    double lo;
    std::optional<double> hi;
    Rgb rgb;
    bool operator==(const Range &o) const = default;
};
using Ranges = std::vector<Range>;

extern const Stops DEFAULT;
extern const std::vector<std::pair<QString, Stops>> PRESETS;
extern Stops STOPS;
extern const Rgb GREEN, YELLOW, ORANGE, REDC, BLUE, PURPLE, TEAL, GREY;
struct Size {                                    // the size filters: which one the states use, each kind's ranges
    QString by = QStringLiteral("people");
    std::map<QString, Ranges> kinds;
};
extern Size SIZE;
extern const std::vector<std::pair<QString, QString>> KINDS;
extern const Rgb OUTSIDE;

struct Info {                                    // (name, unit, decimals, default ranges or None)
    QString name, unit;
    int decimals;
    std::optional<Ranges> ranges;
};
extern const OrderedMap<QString, Info> MODES;
struct Scale {                                   // {"how": "smooth" | "ranges", "own": stops or None, "ranges": …}
    QString how = QStringLiteral("smooth");
    std::optional<Stops> own;
    std::optional<Ranges> ranges;
};
extern OrderedMap<QString, Scale> SCALES;

QString family(const QString &mode);
const Info *info(const QString &mode);
bool has_scale(const QString &mode);
Scale scale(const QString &mode);
Rgb _ramp(const Stops &stops, double t);
Rgb ramp(double t);
std::optional<Rgb> in_ranges(const Ranges &ranges, std::optional<double> value);
Rgb colour(const QString &mode, std::optional<double> value, double t,
           const std::function<Rgb(double)> &smooth = nullptr);
bool uses_ranges(const QString &mode);
std::optional<double> _rounded(const QString &mode, std::optional<double> value);
QString fmt(const QString &mode, std::optional<double> v);
QString legend(const QString &mode);
double nice(double v, bool up = false);
Ranges from_values(const QString &mode, const std::vector<double> &values);
void load(QSettings &settings);
void save(QSettings &settings);
std::optional<Stops> _clean_stops(const Stops &stops);
void set_stops(const Stops &stops, Stops *target = nullptr);
QString state_kind();
std::optional<Rgb> size_colour(const QString &kind, std::optional<double> value);
QString size_legend(const QString &kind);
std::optional<long long> num(const QString &text);
std::optional<double> num_dec(const QString &text);

}  // namespace maplook
