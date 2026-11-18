// maplook.py, its data half — see maplook.h.
#include "core/maplook.h"
#include "core/pyjson.h"
#include "core/pynum.h"
#include "core/pytext.h"

#include <QStringList>
#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace maplook {

const Stops DEFAULT = {{0.0, {38, 44, 54}}, {0.35, {70, 92, 110}}, {0.7, {160, 140, 90}}, {1.0, {240, 205, 130}}};
const std::vector<std::pair<QString, Stops>> PRESETS = {
    {u"Slate → gold"_s, DEFAULT},
    {u"Blue → red"_s, {{0.0, {40, 70, 150}}, {0.5, {225, 225, 215}}, {1.0, {190, 40, 40}}}},
    {u"Green → red"_s, {{0.0, {40, 130, 70}}, {0.5, {225, 205, 80}}, {1.0, {190, 40, 40}}}},
    {u"Red → green"_s, {{0.0, {190, 40, 40}}, {0.5, {225, 205, 80}}, {1.0, {40, 130, 70}}}},
    {u"Dark → bright"_s, {{0.0, {24, 26, 32}}, {1.0, {245, 245, 235}}}},
    {u"Purple → yellow"_s, {{0.0, {60, 20, 90}}, {0.33, {50, 100, 140}}, {0.66, {60, 180, 120}}, {1.0, {250, 230, 60}}}},
    {u"Brown → teal"_s, {{0.0, {110, 70, 30}}, {0.5, {235, 230, 215}}, {1.0, {20, 120, 115}}}},
};
Stops STOPS = DEFAULT;
const Rgb GREEN{70, 190, 90}, YELLOW{225, 200, 70}, ORANGE{230, 140, 50}, REDC{210, 60, 50};
const Rgb BLUE{80, 130, 220}, PURPLE{150, 90, 200}, TEAL{50, 170, 160}, GREY{120, 122, 128};

static Size default_size()
{
    Size s;
    s.kinds[u"state_people"_s] = {{0, 250000, GREEN}, {250001, 500000, YELLOW}, {500001, 1000000, ORANGE},
                                  {1000001, std::nullopt, REDC}};
    s.kinds[u"state_provinces"_s] = {{1, 2, GREEN}, {3, 5, YELLOW}, {6, 8, ORANGE}, {9, std::nullopt, REDC}};
    s.kinds[u"prov_people"_s] = {{0, 25000, GREEN}, {25001, 100000, YELLOW}, {100001, 250000, ORANGE},
                                 {250001, std::nullopt, REDC}};
    return s;
}
Size SIZE = default_size();
const std::vector<std::pair<QString, QString>> KINDS = {{u"state_people"_s, u"States by people"_s},
                                                        {u"state_provinces"_s, u"States by provinces"_s},
                                                        {u"prov_people"_s, u"Provinces by people"_s}};
const Rgb OUTSIDE{58, 62, 70};

static const Ranges LV = {{1, 1, {90, 110, 150}}, {2, 2, {80, 150, 170}}, {3, 3, {90, 180, 120}},
                          {4, 4, {200, 190, 80}}, {5, 5, {225, 140, 60}}, {6, std::nullopt, {210, 70, 60}}};
static OrderedMap<QString, Info> make_modes()
{
    OrderedMap<QString, Info> m;
    m.insert(u"population"_s, {u"Population"_s, u"people"_s, 0,
                               Ranges{{1, 25000, GREEN}, {25001, 100000, YELLOW}, {100001, 250000, ORANGE},
                                      {250001, std::nullopt, REDC}}});
    m.insert(u"filter"_s, {u"Filtered pops"_s, u"people"_s, 0, std::nullopt});
    m.insert(u"liferating"_s, {u"Life rating"_s, u"life rating"_s, 0,
                               Ranges{{0, 9, REDC}, {10, 19, ORANGE}, {20, 29, YELLOW}, {30, std::nullopt, GREEN}}});
    m.insert(u"fort"_s, {u"Forts"_s, u"level"_s, 0, LV});
    m.insert(u"naval_base"_s, {u"Naval bases"_s, u"level"_s, 0, LV});
    m.insert(u"railroad"_s, {u"Railroads"_s, u"level"_s, 0, LV});
    m.insert(u"factories"_s, {u"Factories"_s, u"factories in the state"_s, 0,
                              Ranges{{1, 1, {90, 110, 150}}, {2, 3, GREEN}, {4, 6, YELLOW}, {7, 8, ORANGE},
                                     {9, std::nullopt, REDC}}});
    m.insert(u"soldiers"_s, {u"Soldiers per state"_s, u"soldiers"_s, 0,
                             Ranges{{1, 2999, {90, 110, 150}}, {3000, 9999, GREEN}, {10000, 29999, YELLOW},
                                    {30000, std::nullopt, REDC}}});
    m.insert(u"relvalues"_s, {u"Relations with the main nation"_s, u"relations"_s, 0,
                              Ranges{{-200, -100, {214, 58, 48}}, {-99, -1, {225, 140, 60}}, {0, 0, GREY},
                                     {1, 99, {150, 200, 110}}, {100, 200, {64, 190, 88}}}});
    m.insert(u"literacy"_s, {u"Literacy at the start"_s, u"%"_s, 1,
                             Ranges{{0, 9.9, REDC}, {10, 29.9, ORANGE}, {30, 59.9, YELLOW}, {60, std::nullopt, GREEN}}});
    m.insert(u"plurality"_s, {u"Plurality"_s, u"%"_s, 1,
                              Ranges{{0, 24.9, {90, 110, 150}}, {25, 49.9, TEAL}, {50, 74.9, YELLOW},
                                     {75, std::nullopt, ORANGE}}});
    m.insert(u"accepted"_s, {u"Accepted pops (by country)"_s, u"%"_s, 1,
                             Ranges{{0, 49.9, REDC}, {50, 74.9, ORANGE}, {75, 89.9, YELLOW}, {90, std::nullopt, GREEN}}});
    m.insert(u"pt_"_s, {u"Pop type share (by country)"_s, u"% of the pops"_s, 1, std::nullopt});
    m.insert(u"tech_all"_s, {u"Technology (all)"_s, u"technologies"_s, 0, std::nullopt});
    m.insert(u"tech:"_s, {u"Technology of a school"_s, u"technologies"_s, 0, std::nullopt});
    return m;
}
const OrderedMap<QString, Info> MODES = make_modes();
OrderedMap<QString, Scale> SCALES;

QString family(const QString &mode)
{
    if (mode.startsWith(u"pt_"_s))
        return u"pt_"_s;
    if (mode.startsWith(u"tech:"_s))
        return u"tech:"_s;
    return mode;
}

const Info *info(const QString &mode)
{
    const auto *i = MODES.get(family(mode));
    return i;
}

bool has_scale(const QString &mode) { return info(mode) != nullptr; }

Scale scale(const QString &mode)
{
    const Scale *s = SCALES.get(mode);
    return s ? *s : Scale{};
}

Rgb _ramp(const Stops &stops, double t)
{
    t = std::max(0.0, std::min(1.0, t));
    for (size_t i = 0; i + 1 < stops.size(); ++i) {
        const auto &[t0, c0] = stops[i];
        const auto &[t1, c1] = stops[i + 1];
        if (t <= t1) {
            const double f = t1 > t0 ? (t - t0) / (t1 - t0) : 0;
            auto mix = [f](int a, int b) { return static_cast<int>(a + (b - a) * f); };
            return {mix(std::get<0>(c0), std::get<0>(c1)), mix(std::get<1>(c0), std::get<1>(c1)),
                    mix(std::get<2>(c0), std::get<2>(c1))};
        }
    }
    return stops.back().second;
}

Rgb ramp(double t) { return _ramp(STOPS, t); }

std::optional<Rgb> in_ranges(const Ranges &ranges, std::optional<double> value)
{
    if (!value)
        return std::nullopt;
    for (const Range &r : ranges)
        if (*value >= r.lo && (!r.hi || *value <= *r.hi))
            return r.rgb;
    return std::nullopt;
}

Rgb colour(const QString &mode, std::optional<double> value, double t, const std::function<Rgb(double)> &smooth)
{
    const Scale *s = SCALES.get(mode);
    if (s && s->how == u"ranges"_s && s->ranges) {
        const auto c = in_ranges(*s->ranges, _rounded(mode, value));
        return c ? *c : OUTSIDE;
    }
    if (s && s->own && !s->own->empty())
        return _ramp(*s->own, t);
    return smooth ? smooth(t) : ramp(t);
}

bool uses_ranges(const QString &mode)
{
    const Scale *s = SCALES.get(mode);
    return s && s->how == u"ranges"_s && s->ranges.has_value();
}

std::optional<double> _rounded(const QString &mode, std::optional<double> value)
{
    const Info *i = info(mode);
    if (!value || !i)
        return value;
    return i->decimals ? py::round_to(*value, i->decimals) : static_cast<double>(py::round_int(*value));
}

QString fmt(const QString &mode, std::optional<double> v)
{
    const Info *i = info(mode);
    const int d = i ? i->decimals : 0;
    if (!v)
        return QString();
    if (d)
        return py::fmt_f(*v, d);
    return py::fmt_commas(static_cast<double>(py::round_int(*v)), 0);
}

QString legend(const QString &mode)
{
    const Info *i = info(mode);
    if (!i || !uses_ranges(mode))
        return QString();
    QStringList parts;
    for (const Range &r : *SCALES.get(mode)->ranges)
        parts << (r.hi ? u"%1 – %2"_s.arg(fmt(mode, r.lo), fmt(mode, *r.hi)) : u"%1 and more"_s.arg(fmt(mode, r.lo)));
    const QString unit = py::lower(i->name).contains(py::lower(i->unit)) ? QString() : u" (%1)"_s.arg(i->unit);
    return u"%1%2: %3 — the rest grey"_s.arg(i->name, unit, parts.join(u" · "_s));
}

double nice(double v, bool up)
{
    if (v <= 0)
        return 0;
    const double e = std::pow(10.0, std::floor(std::log10(v)));
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
        if (up ? (m * e >= v) : (m * e >= v * 0.75))
            return m * e;
    return 10 * e;
}

Ranges from_values(const QString &mode, const std::vector<double> &values)
{
    const Info *i = info(mode);
    const int d = i ? i->decimals : 0;
    std::vector<double> vals;
    for (double v : values)
        if (v > 0)
            vals.push_back(v);
    std::stable_sort(vals.begin(), vals.end());
    if (vals.empty())
        return {{1, std::nullopt, ramp(0.8)}};
    std::vector<double> cuts;
    for (double q : {0.25, 0.5, 0.75}) {
        const auto k = std::min(static_cast<long long>(vals.size()) - 1, py::trunc_int(q * double(vals.size())));
        double c = nice(vals[size_t(k)]);
        if (d == 0)
            c = static_cast<double>(py::round_int(c));
        if (c > 0 && (cuts.empty() || c > cuts.back()))
            cuts.push_back(c);
    }
    const double step = d ? std::pow(10.0, -d) : 1;
    std::vector<double> lows = {d ? py::round_to(step, d) : 1};
    for (double c : cuts)
        lows.push_back(d ? py::round_to(c + step, d) : c + 1);
    Ranges out;
    for (size_t k = 0; k < lows.size(); ++k) {
        const std::optional<double> hi = k < cuts.size() ? std::optional<double>(cuts[k]) : std::nullopt;
        out.push_back({lows[k], hi, ramp(0.25 + 0.75 * double(k + 1) / double(lows.size()))});
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- settings
static std::optional<double> number(const pyjson::Value &v, bool floats)
{
    if (const auto *i = std::get_if<long long>(&v.v))
        return static_cast<double>(*i);
    if (const auto *d = std::get_if<double>(&v.v))
        return floats ? *d : static_cast<double>(py::trunc_int(*d));   // int(2.7) == 2
    if (const auto *b = std::get_if<bool>(&v.v))
        return *b ? 1.0 : 0.0;
    if (const auto *s = std::get_if<QString>(&v.v)) {
        if (floats)
            return py::to_float(*s);
        const QString t = py::strip(*s);
        bool ok = false;
        const long long n = t.toLongLong(&ok);
        return ok ? std::optional<double>(static_cast<double>(n)) : std::nullopt;
    }
    return std::nullopt;
}

static std::optional<Rgb> rgb_of(const pyjson::Value &v)
{
    if (!v.is_arr())
        return std::nullopt;
    std::vector<int> c;
    for (const pyjson::Value &x : v.arr()) {
        const auto n = number(x, true);
        if (!n)
            return std::nullopt;
        c.push_back(static_cast<int>(py::trunc_int(std::max(0.0, std::min(255.0, *n)))));
    }
    if (c.size() < 3)
        return std::nullopt;
    return Rgb{c[0], c[1], c[2]};
}

static Ranges clean(const pyjson::Value &ranges, bool floats = false)
{
    Ranges out;
    if (!ranges.is_arr())
        return out;
    for (const pyjson::Value &r : ranges.arr()) {
        if (!r.is_arr() || r.arr().size() < 3)
            continue;
        const auto lo = number(r.arr()[0], floats);
        if (!lo)
            continue;
        std::optional<double> hi;
        const pyjson::Value &h = r.arr()[1];
        if (!(h.is_null() || (std::holds_alternative<QString>(h.v) && h.str().isEmpty()))) {
            hi = number(h, floats);
            if (!hi)
                continue;
        }
        const auto rgb = rgb_of(r.arr()[2]);
        if (!rgb)
            continue;
        if ((floats || *lo >= 0) && (!hi || *hi >= *lo))
            out.push_back({*lo, hi, *rgb});
    }
    if (out.size() > 12)
        out.resize(12);
    return out;
}

static std::optional<Stops> stops_of(const pyjson::Value &v)
{
    if (!v.is_arr())
        return std::nullopt;
    Stops s;
    for (const pyjson::Value &e : v.arr()) {
        if (!e.is_arr() || e.arr().size() != 2)
            return std::nullopt;
        const auto p = number(e.arr()[0], true);
        const auto rgb = rgb_of(e.arr()[1]);
        if (!p || !rgb)
            return std::nullopt;
        s.push_back({*p, *rgb});
    }
    return s;
}

static std::optional<pyjson::Value> setting(QSettings &settings, const QString &key)
{
    const QString t = settings.value(key, QString()).toString();
    return pyjson::loads(t.isEmpty() ? u"null"_s : t);
}

void load(QSettings &settings)
{
    if (const auto d = setting(settings, u"mapRamp"_s); d && d->truthy())
        if (const auto s = stops_of(*d))
            set_stops(*s);
    if (const auto d = setting(settings, u"mapSizeFilter"_s); d && d->is_obj()) {
        for (const auto &[k, _t] : KINDS) {
            const pyjson::Value *x = d->get(k);
            const Ranges v = x ? clean(*x) : Ranges{};
            if (!v.empty())
                SIZE.kinds[k] = v;
        }
        if (const pyjson::Value *by = d->get(u"by"_s); by && (by->str() == u"provinces"_s || by->str() == u"people"_s))
            SIZE.by = by->str();
    }
    if (const auto d = setting(settings, u"mapScales"_s); d && d->is_obj()) {
        SCALES.clear();
        for (const auto &[k, s] : d->obj()) {
            if (!s.is_obj())
                continue;
            Scale sc;
            if (const pyjson::Value *own = s.get(u"own"_s); own && own->truthy())
                if (const auto st = stops_of(*own))
                    sc.own = _clean_stops(*st);
            const pyjson::Value *rs = s.get(u"ranges"_s);
            if (rs && !rs->is_null())
                sc.ranges = clean(*rs, true);
            const pyjson::Value *how = s.get(u"how"_s);
            sc.how = how && how->str() == u"ranges"_s && sc.ranges ? u"ranges"_s : u"smooth"_s;
            SCALES.insert(k, sc);
        }
    }
}

static pyjson::Value num_value(double x)
{
    return x == std::floor(x) && std::abs(x) < 9e15 ? pyjson::Value(static_cast<long long>(x)) : pyjson::Value(x);
}
static pyjson::Value rgb_value(const Rgb &c)
{
    return pyjson::Array{std::get<0>(c), std::get<1>(c), std::get<2>(c)};
}
static pyjson::Value ranges_value(const Ranges &rs)
{
    pyjson::Array a;
    for (const Range &r : rs)
        a.push_back(pyjson::Array{num_value(r.lo), r.hi ? num_value(*r.hi) : pyjson::Value(), rgb_value(r.rgb)});
    return a;
}
static pyjson::Value stops_value(const Stops &st)
{
    pyjson::Array a;
    for (const auto &[p, c] : st)
        a.push_back(pyjson::Array{p, rgb_value(c)});
    return a;
}

void save(QSettings &settings)
{
    settings.setValue(u"mapRamp"_s, pyjson::dumps(stops_value(STOPS)));
    pyjson::Object size;
    size.insert(u"by"_s, SIZE.by);
    for (const auto &[k, _t] : KINDS)
        size.insert(k, ranges_value(SIZE.kinds[k]));
    settings.setValue(u"mapSizeFilter"_s, pyjson::dumps(size));
    pyjson::Object scales;
    for (const auto &[k, s] : SCALES) {
        pyjson::Object o;
        o.insert(u"how"_s, s.how);
        o.insert(u"own"_s, s.own && !s.own->empty() ? stops_value(*s.own) : pyjson::Value());
        o.insert(u"ranges"_s, s.ranges ? ranges_value(*s.ranges) : pyjson::Value());
        scales.insert(k, o);
    }
    settings.setValue(u"mapScales"_s, pyjson::dumps(scales));
}

std::optional<Stops> _clean_stops(const Stops &stops)
{
    Stops s;
    for (const auto &[p, c] : stops) {
        auto cl = [](int x) { return std::max(0, std::min(255, x)); };
        s.push_back({std::max(0.0, std::min(1.0, p)), {cl(std::get<0>(c)), cl(std::get<1>(c)), cl(std::get<2>(c))}});
    }
    std::stable_sort(s.begin(), s.end());
    if (s.size() < 2)
        return std::nullopt;
    s.front().first = 0.0;
    s.back().first = 1.0;
    return s;
}

void set_stops(const Stops &stops, Stops *target)
{
    const auto s = _clean_stops(stops);
    if (!s)
        return;
    *(target ? target : &STOPS) = *s;
}

QString state_kind() { return u"state_"_s + SIZE.by; }

std::optional<Rgb> size_colour(const QString &kind, std::optional<double> value)
{
    return in_ranges(SIZE.kinds[kind], value);
}

QString size_legend(const QString &kind)
{
    auto f = [](double v) { return py::fmt_commas(v, 0); };
    const QString unit = kind.endsWith(u"provinces"_s) ? u"provinces"_s : u"people"_s;
    QStringList parts;
    for (const Range &r : SIZE.kinds[kind])
        parts << (r.hi ? u"%1 – %2"_s.arg(f(r.lo), f(*r.hi)) : u"%1 and more"_s.arg(f(r.lo)));
    QString name;
    for (const auto &[k, t] : KINDS)
        if (k == kind)
            name = t;
    return u"%1 (%2): %3 — the rest grey"_s.arg(py::split(name, u" by "_s)[0] + u" size"_s, unit, parts.join(u" · "_s));
}

std::optional<long long> num(const QString &text)
{
    QString t = py::lower(py::strip(text)).remove(u' ').remove(u',').remove(u'.');
    long long mul = 1;
    const bool neg = t.startsWith(u'-');
    if (neg)
        t = t.mid(1);
    if (t.endsWith(u'k')) {
        t.chop(1);
        mul = 1000;
    } else if (t.endsWith(u'm')) {
        t.chop(1);
        mul = 1000000;
    }
    if (!py::isdigit(t))
        return std::nullopt;
    return (neg ? -1 : 1) * py::to_int(t) * mul;
}

std::optional<double> num_dec(const QString &text)
{
    const QString t = py::strip(text).remove(u' ').remove(u'%').replace(u',', u'.');
    return py::to_float(t);
}

}  // namespace maplook
