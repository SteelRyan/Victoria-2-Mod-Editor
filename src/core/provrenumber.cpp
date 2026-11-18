// provrenumber.py — closing up the province numbers after a delete. Line by line.
#include "core/provrenumber.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pyre.h"
#include "core/pytext.h"
#include "core/refs.h"
#include "core/script.h"

#include <QDateTime>
#include <QFile>
#include <algorithm>
#include <functional>
#include <set>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace provrenumber {

namespace {
const QString TOK = u"#[^\\n]*|\"[^\"\\n]*\"|[{}=]|[^\\s\\x1c-\\x1f{}=#\"]+"_s;

struct Tok {
    qsizetype s, e;
    QString t;
};
std::vector<Tok> tokens(const QString &text)
{
    std::vector<Tok> out;
    auto it = py::rx(TOK).globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString t = m.captured(0);
        if (t[0] != u'#')
            out.push_back({m.capturedStart(0), m.capturedEnd(0), t});
    }
    return out;
}

[[noreturn]] void runtime_error(const QString &msg) { throw std::runtime_error(msg.toStdString()); }

// os.walk(top): (folder, its file names) top-down, folders in the order Windows lists them
void walk(const QString &top, const std::function<void(const QString &, const QStringList &)> &fn)
{
    QStringList names;
    try {
        names = fs::listdir(top);
    } catch (const fs::OSError &) {
        return;
    }
    QStringList dirs, files;
    for (const QString &n : names)
        (fs::isdir(pypath::join(top, n)) ? dirs : files) << n;
    fn(top, files);
    for (const QString &d : dirs)
        walk(pypath::join(top, d), fn);
}

QStringList txt_files(const QString &folder)
{
    QStringList out;
    walk(folder, [&](const QString &d, const QStringList &fs0) {
        QStringList fs = fs0;
        std::sort(fs.begin(), fs.end());
        for (const QString &f : fs)
            if (py::lower(f).endsWith(u".txt"))
                out << pypath::join(d, f);
    });
    return out;
}

QString plural(long long n, const char16_t *one, const char16_t *many)
{
    return QString::fromUtf16(n == 1 ? one : many);
}
template <typename I> QString N(I n) { return QString::number(static_cast<long long>(n)); }

// (C++ only, 2026-11-08) the text without its outermost `key = { … }` blocks whose key is in keys (a game pop file:
// the pops of a deleted province) -> (text, how many blocks went)
std::pair<QString, long long> drop_top(QString text, const QSet<QString> &keys)
{
    const std::vector<Tok> toks = tokens(text);
    const size_t n = toks.size();
    std::vector<std::pair<qsizetype, qsizetype>> cuts;
    long long depth = 0;
    for (size_t i = 0; i < n; ++i) {
        const QString &t = toks[i].t;
        if (t == u"{") {
            ++depth;
        } else if (t == u"}") {
            --depth;
        } else if (depth == 0 && keys.contains(t) && i + 2 < n && toks[i + 1].t == u"=" && toks[i + 2].t == u"{") {
            long long d = 0;
            size_t j = i + 2;
            for (; j < n; ++j) {
                if (toks[j].t == u"{")
                    ++d;
                else if (toks[j].t == u"}" && --d == 0)
                    break;
            }
            if (j >= n)
                break;                                   // (unclosed: left as it is)
            cuts.emplace_back(toks[i].s, toks[j].e);
            i = j;
        }
    }
    for (auto it = cuts.rbegin(); it != cuts.rend(); ++it)
        text = _cut(text, it->first, it->second);
    return {text, static_cast<long long>(cuts.size())};
}
}  // namespace

const QSet<QString> PROV_KEYS = {u"province_id"_s, u"capital"_s,  u"owns"_s,     u"controls"_s,
                                 u"add_core"_s,    u"remove_core"_s, u"is_core"_s, u"province"_s,
                                 u"location"_s,    u"home"_s,     u"state_id"_s};
const QSet<QString> EFFECT_BLOCKS = {u"option"_s,          u"effect"_s,           u"immediate"_s,
                                     u"on_add"_s,          u"on_po_accepted"_s,   u"siege_won_effect"_s,
                                     u"demands_enforced_effect"_s};
const QStringList SCRIPT_FOLDERS = {u"events"_s,       u"decisions"_s,    u"common"_s,
                                    u"inventions"_s,   u"technologies"_s, u"history/wars"_s};
const QString NEVER = u"always = no"_s;
const QString GONE_NOTE = u"# ModEditor: named province %1, which was deleted"_s;

QString Job::summary() const
{
    QStringList out;
    if (!moves.isEmpty()) {
        std::vector<std::pair<long long, long long>> mv(moves.begin(), moves.end());
        std::stable_sort(mv.begin(), mv.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
        QStringList parts;
        for (size_t i = 0; i < mv.size() && i < 4; ++i) {
            QString nm = py::strip(names.value(mv[i].first));
            if (nm.isEmpty())
                nm = u"?"_s;
            parts << u"#%1 %2 → #%3"_s.arg(N(mv[i].first), nm, N(mv[i].second));
        }
        out << u"The numbers are without a gap again: %1 province%2 renumbered (%3%4), highest number now %5."_s
                   .arg(N(moves.size()), moves.size() == 1 ? u""_s : u"s"_s, parts.join(u", "_s),
                        moves.size() > 4 ? u" …"_s : u""_s, N(top));
    }
    const long long moved = counts.value(u"script places of a moved province"_s, 0);
    if (moved)
        out << u"%1 script place%2 followed to the new number%3."_s.arg(N(moved), plural(moved, u"", u"s"),
                                                                         moves.size() == 1 ? u""_s : u"s"_s);
    if (retargeted)
        out << u"%1 script place%2 that named a deleted province now name%3 the province that took its land."_s
                   .arg(N(retargeted), plural(retargeted, u"", u"s"), plural(retargeted, u"s", u""));
    if (neutral)
        out << u"%1 script place%2 about a province that is gone for good %3 taken out (conditions: never "
               u"true; effects: removed)."_s.arg(N(neutral), plural(neutral, u"", u"s"), plural(neutral, u"was", u"were"));
    if (!left.empty()) {
        long long n = 0;
        for (const auto &[_f, k] : left)
            n += k;
        QStringList parts;
        for (size_t i = 0; i < left.size() && i < 4; ++i)
            parts << u"%1 ×%2"_s.arg(left[i].first, N(left[i].second));
        out << u"⚠ %1 place%2 still name%3 a deleted number and could not be placed (not inside a condition "
               u"or an effect): %4."_s.arg(N(n), plural(n, u"", u"s"), plural(n, u"s", u""),
                                           parts.join(u", "_s) + (left.size() > 4 ? u" …"_s : u""_s));
    }
    const QString s = out.join(u' ');
    return s.isEmpty() ? u"Nothing had to be renumbered."_s : s;
}

std::pair<OrderedMap<long long, long long>, std::vector<long long>> moves_for(const std::vector<long long> &real0,
                                                                           const std::vector<long long> &free0,
                                                                           const Heirs &heirs)
{
    std::vector<long long> real = real0, free = free0;
    std::sort(real.begin(), real.end(), std::greater<>());
    std::sort(free.begin(), free.end());
    OrderedMap<long long, long long> out;
    std::vector<long long> dropped;
    size_t i = 0;
    for (long long x : free) {
        if (i < real.size() && real[i] > x) {
            out[real[i]] = x;
            ++i;
        } else {
            dropped.push_back(x);
        }
    }
    if (!heirs.isEmpty() && !out.isEmpty()) {
        std::set<long long> targets;
        for (const auto &[_t, x] : out)
            targets.insert(x);
        OrderedMap<long long, long long> want;
        std::set<long long> want_vals;
        std::vector<std::pair<long long, std::optional<long long>>> hs(heirs.begin(), heirs.end());
        std::sort(hs.begin(), hs.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        for (const auto &[x, h] : hs)
            if (h && out.contains(*h) && targets.count(x) && !want.contains(*h) && !want_vals.count(x)) {
                want[*h] = x;
                want_vals.insert(x);
            }
        if (!want.isEmpty()) {
            std::vector<long long> rest_m, rest_x;
            for (const auto &[t, _x] : out)
                if (!want.contains(t))
                    rest_m.push_back(t);
            std::sort(rest_m.begin(), rest_m.end(), std::greater<>());
            for (long long x : targets)
                if (!want_vals.count(x))
                    rest_x.push_back(x);
            OrderedMap<long long, long long> o = want;
            for (size_t k = 0; k < rest_m.size() && k < rest_x.size(); ++k)
                o[rest_m[k]] = rest_x[k];
            out = o;
        }
    }
    return {out, dropped};
}

std::pair<QString, long long> rewrite(QString text, const QHash<QString, QString> &mapping, const QString &mode)
{
    const std::vector<Tok> toks = tokens(text);
    std::vector<std::tuple<qsizetype, qsizetype, QString>> edits;
    long long depth = 0;
    const size_t n = toks.size();
    for (size_t i = 0; i < n; ++i) {
        const QString &t = toks[i].t;
        if (t == u"{") {
            ++depth;
            continue;
        }
        if (t == u"}") {
            --depth;
            continue;
        }
        const auto it = mapping.constFind(t);
        if (it == mapping.cend())
            continue;
        const QString prev = i ? toks[i - 1].t : QString();
        const QString nxt = i + 1 < n ? toks[i + 1].t : QString();
        if (mode == u"list" ? (prev != u"=" && nxt != u"=") : (depth == 0 && nxt == u"="))
            edits.emplace_back(toks[i].s, toks[i].e, *it);
    }
    for (auto e = edits.rbegin(); e != edits.rend(); ++e)
        text = text.left(std::get<0>(*e)) + std::get<2>(*e) + text.mid(std::get<1>(*e));
    return {text, static_cast<long long>(edits.size())};
}

QString _cut(const QString &text, qsizetype a, qsizetype b)
{
    // The text without [a, b); a line that this leaves empty goes too.
    const qsizetype ls = py::rfind(text, u'\n', a) + 1;
    qsizetype le = text.indexOf(u'\n', b);
    le = le < 0 ? text.size() : le;
    if (py::strip(py::slice(text, ls, a)).isEmpty() && py::strip(py::slice(text, b, le)).isEmpty())
        return py::head(text, ls) + py::tail(text, std::min(text.size(), le + 1));
    return py::head(text, a) + py::tail(text, b);
}

// (C++ only, 2026-11-08) cuts [a, b) ranges out of text, back to front; a range inside one already cut goes with it
static QString cut_ranges(QString text, std::vector<std::pair<qsizetype, qsizetype>> cuts)
{
    std::stable_sort(cuts.begin(), cuts.end(),
                     [](const auto &x, const auto &y) { return x.first != y.first ? x.first < y.first : x.second > y.second; });
    std::vector<std::pair<qsizetype, qsizetype>> kept;
    for (const auto &c : cuts)
        if (kept.empty() || c.first >= kept.back().second)
            kept.push_back(c);
    for (auto it = kept.rbegin(); it != kept.rend(); ++it)
        text = _cut(text, it->first, it->second);
    return text;
}

// (C++ only, 2026-11-08) history/units: an army / navy whose own `location` is a province gone for good is taken
// out, so is a regiment whose `home` is (and an army left without any regiment). MEASURED in the game (the tester's
// cut map, 2026-11-08): the campaign start reads a regiment's home as a province with no range check
// (v2game 0x5BE946, subunit.cpp) — a number past the last province crashed it.
static std::pair<QString, long long> drop_units(const QString &text, const QSet<QString> &drop)
{
    const std::vector<Tok> toks = tokens(text);
    const size_t n = toks.size();
    struct Blk {
        QString key;
        qsizetype a = 0, b = -1;
        long long parent = -1;
        bool bad = false;                              // its own location / home is gone
        long long regs = 0, regs_bad = 0;
    };
    std::vector<Blk> blks;
    std::vector<long long> stack;
    for (size_t i = 0; i < n; ++i) {
        const QString &t = toks[i].t;
        if (t == u"{") {
            Blk b;
            if (i >= 2 && toks[i - 1].t == u"=") {
                b.key = py::lower(toks[i - 2].t);
                b.a = toks[i - 2].s;
            } else {
                b.a = toks[i].s;
            }
            b.parent = stack.empty() ? -1 : stack.back();
            blks.push_back(b);
            stack.push_back(static_cast<long long>(blks.size()) - 1);
        } else if (t == u"}") {
            if (!stack.empty()) {
                blks[size_t(stack.back())].b = toks[i].e;
                stack.pop_back();
            }
        } else if (!stack.empty() && i + 2 < n && toks[i + 1].t == u"=" && drop.contains(toks[i + 2].t)) {
            Blk &cur = blks[size_t(stack.back())];
            const QString k = py::lower(t);
            if ((k == u"location" && (cur.key == u"army" || cur.key == u"navy")) ||
                (k == u"home" && cur.key == u"regiment"))
                cur.bad = true;
        }
    }
    for (const Blk &b : blks)                         // regiments counted per army
        if (b.key == u"regiment" && b.parent >= 0 && blks[size_t(b.parent)].key == u"army") {
            blks[size_t(b.parent)].regs += 1;
            blks[size_t(b.parent)].regs_bad += b.bad;
        }
    std::vector<std::pair<qsizetype, qsizetype>> cuts;
    for (const Blk &b : blks) {
        if (b.b < 0)
            continue;                                  // (unclosed: left as it is)
        const bool all_gone = b.key == u"army" && b.regs > 0 && b.regs == b.regs_bad;
        if (b.bad || all_gone)
            cuts.emplace_back(b.a, b.b);
    }
    if (cuts.empty())
        return {text, 0};
    return {cut_ranges(text, cuts), static_cast<long long>(cuts.size())};
}

// (C++ only, 2026-11-08) history/countries: `capital = <a province gone for good>` names the country's lowest
// remaining province instead (repl), or goes when it has none left (the tester's Tonga: capital past the last
// province, the country without land).
static std::pair<QString, long long> drop_capital(const QString &text, const QSet<QString> &drop, const std::optional<QString> &repl)
{
    const std::vector<Tok> toks = tokens(text);
    std::vector<std::pair<qsizetype, qsizetype>> cuts;
    std::vector<std::pair<qsizetype, qsizetype>> swaps;
    for (size_t i = 0; i + 2 < toks.size(); ++i)
        if (py::lower(toks[i].t) == u"capital" && toks[i + 1].t == u"=" && drop.contains(toks[i + 2].t)) {
            if (repl)
                swaps.emplace_back(toks[i + 2].s, toks[i + 2].e);
            else
                cuts.emplace_back(toks[i].s, toks[i + 2].e);
        }
    QString out = text;
    for (auto it = swaps.rbegin(); it != swaps.rend(); ++it)
        out = out.left(it->first) + *repl + out.mid(it->second);
    if (!cuts.empty())
        out = cut_ranges(out, cuts);
    return {out, static_cast<long long>(swaps.size() + cuts.size())};
}

ScriptResult rewrite_script(QString text, const QHash<QString, QString> &mapping, const QSet<QString> &drop)
{
    const std::vector<Tok> toks = tokens(text);
    const size_t n = toks.size();
    QStringList stack;
    struct Edit {
        qsizetype a, b;
        QString rep;
        bool cut;
    };
    std::vector<Edit> edits, cuts;
    long long left = 0;
    for (size_t i = 0; i < n; ++i) {
        const auto &[s, e, t] = toks[i];
        if (t == u"{") {
            stack << (i >= 2 && toks[i - 1].t == u"=" ? py::lower(toks[i - 2].t) : QString());
            continue;
        }
        if (t == u"}") {
            if (!stack.isEmpty())
                stack.removeLast();
            continue;
        }
        const auto mit = mapping.constFind(t);
        if (mit == mapping.cend() && !drop.contains(t))
            continue;
        const QString prev = i ? toks[i - 1].t : QString();
        const QString nxt = i + 1 < n ? toks[i + 1].t : QString();
        bool scope;
        if (nxt == u"=") {
            if (!(i + 2 < n && toks[i + 2].t == u"{"))
                continue;
            // (C++ only, 2026-11-09: found on a cut of the game's map) `10 = { … }` right inside a random_list is a
            // chance weight, not a province — taken for province 10 it was cut out, leaving `random_list = { }`
            if (!stack.isEmpty() && stack.last() == u"random_list")
                continue;
            scope = true;
        } else if (prev == u"=" && i >= 2 && PROV_KEYS.contains(py::lower(toks[i - 2].t))) {
            scope = false;
        } else {
            continue;
        }
        if (mit != mapping.cend()) {
            edits.push_back({s, e, *mit, false});
            continue;
        }
        std::optional<QString> ctx;
        for (auto k = stack.crbegin(); k != stack.crend(); ++k)
            if (refs::COND_BLOCKS.contains(*k) || EFFECT_BLOCKS.contains(*k)) {
                ctx = *k;
                break;
            }
        if (!ctx) {
            ++left;
            continue;
        }
        qsizetype a, b;
        if (scope) {                                 // the whole block of that province
            long long depth = 0;
            size_t j = i + 2;
            while (j < n) {
                if (toks[j].t == u"{") {
                    ++depth;
                } else if (toks[j].t == u"}") {
                    --depth;
                    if (depth == 0)
                        break;
                }
                ++j;
            }
            if (j >= n) {
                ++left;
                continue;
            }
            a = s;
            b = toks[j].e;
        } else {
            a = toks[i - 2].s;
            b = e;
        }
        // (C++ only, 2026-11-09, the user's wish: such places must show in the Errors tab) a note stays on the line —
        // mapcheck lists every "# ModEditor: named province N, which was deleted" as an error to look at
        const QString note = GONE_NOTE.arg(t);
        cuts.push_back({a, b, refs::COND_BLOCKS.contains(*ctx) ? NEVER + u' ' + note : note, true});
    }
    std::vector<Edit> sorted_cuts = cuts;
    std::stable_sort(sorted_cuts.begin(), sorted_cuts.end(),
                     [](const Edit &x, const Edit &y) { return x.a != y.a ? x.a < y.a : x.b > y.b; });
    std::vector<Edit> kept;                          // (a block that goes takes what is inside it along)
    for (const Edit &c : sorted_cuts)
        if (kept.empty() || c.a >= kept.back().b)
            kept.push_back(c);
    std::vector<Edit> ed;
    for (const Edit &x : edits)
        if (!std::any_of(kept.begin(), kept.end(), [&](const Edit &c) { return c.a <= x.a && x.b <= c.b; }))
            ed.push_back(x);
    ScriptResult r;
    for (const Edit &x : ed)
        r.hits[text.mid(x.a, x.b - x.a)] += 1;
    std::vector<Edit> all = ed;
    all.insert(all.end(), kept.begin(), kept.end());
    std::stable_sort(all.begin(), all.end(), [](const Edit &x, const Edit &y) { return x.a > y.a; });
    for (const Edit &x : all) {
        QString rep = x.rep;
        if (x.cut && rep.contains(u'#')) {           // (the note is a comment: what follows on the line moves down)
            qsizetype le = text.indexOf(u'\n', x.b);
            le = le < 0 ? text.size() : le;
            if (!py::strip(py::slice(text, x.b, le)).isEmpty()) {
                const qsizetype ls = py::rfind(text, u'\n', x.a) + 1;
                qsizetype k = ls;
                while (k < x.a && (text[k] == u' ' || text[k] == u'\t'))
                    ++k;
                rep += (text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s) + text.mid(ls, k - ls);
            }
        }
        text = rep.isEmpty() ? _cut(text, x.a, x.b) : py::head(text, x.a) + rep + py::tail(text, x.b);
    }
    r.text = text;
    r.cut = static_cast<long long>(kept.size());
    r.left = left;
    return r;
}

std::vector<ProvRef> province_refs(const QString &t)
{
    static const QRegularExpression NOTE(u"# ModEditor: named province (\\d+), which was deleted"_s);
    static const QRegularExpression REF(
        u"(?<![\\w.])([A-Za-z_]+)[ \\t]*=[ \\t]*(\\d+)(?![\\w.])|^[ \\t]*(\\d+)[ \\t]*=[ \\t]*\\{"_s,
        QRegularExpression::MultilineOption);
    static const QRegularExpression KEY(u"([A-Za-z_]+)[ \\t\\r\\n]*=[ \\t\\r\\n]*$"_s);
    std::vector<ProvRef> out;
    for (auto it = NOTE.globalMatch(t); it.hasNext();) {
        const auto m = it.next();
        out.push_back({m.capturedStart(0), m.captured(1).toLongLong(), true});
    }
    for (auto it = REF.globalMatch(t); it.hasNext();) {
        const auto m = it.next();
        const bool keyed = m.capturedStart(1) >= 0;
        if (keyed && !PROV_KEYS.contains(py::lower(m.captured(1))))
            continue;
        const qsizetype pos = m.capturedStart(keyed ? 1 : 3);
        const qsizetype ls = t.lastIndexOf(u'\n', pos) + 1;
        if (t.mid(ls, pos - ls).contains(u'#'))
            continue;                                // (inside a comment)
        if (!keyed) {                                // `10 = {` right inside a random_list is a chance weight
            long long depth = 0;
            qsizetype k = pos - 1;
            for (; k >= 0; --k) {
                if (t[k] == u'}')
                    ++depth;
                else if (t[k] == u'{' && depth-- == 0)
                    break;
            }
            if (k >= 0) {
                const auto km = KEY.match(t.left(k));
                if (km.hasMatch() && py::lower(km.captured(1)) == u"random_list")
                    continue;
            }
        }
        out.push_back({pos, m.captured(keyed ? 2 : 3).toLongLong(), false});
    }
    return out;
}

std::optional<Job> plan(moddata::Mod &mod)
{
    // What has to be done after a delete (nothing is written). -> Job, or nullopt when there is nothing.
    const OrderedMap<long long, moddata::Rgb> free = mod.free_slots;
    const Heirs raw = mod.free_heirs;
    if (free.isEmpty() && raw.isEmpty())
        return std::nullopt;
    Job job;
    std::set<long long> gone;
    for (const auto &kv : free)
        gone.insert(kv.first);
    for (const auto &kv : raw)
        gone.insert(kv.first);

    auto heir = [&](long long x) -> std::optional<long long> {
        std::optional<long long> h = raw.contains(x) ? raw.value(x) : std::nullopt;
        std::set<long long> seen;
        while (h && !mod.provinces.contains(*h) && raw.contains(*h) && !seen.count(*h)) {
            seen.insert(*h);
            h = raw.value(*h);
        }
        return h && mod.provinces.contains(*h) ? h : std::nullopt;
    };
    for (long long x : gone)
        job.heirs[x] = heir(x);
    {
        std::vector<long long> real, fr;
        for (const auto &kv : mod.provinces)
            real.push_back(kv.first);
        for (const auto &kv : free)
            fr.push_back(kv.first);
        std::tie(job.moves, job.dropped) = moves_for(real, fr, job.heirs);
    }
    for (const auto &[t, _x] : job.moves)
        job.names[t] = mod.provinces.value(t).name;
    for (const auto &[_x, h] : job.heirs)
        if (h)
            job.names[*h] = mod.provinces.value(*h).name;
    {
        std::set<long long> s;
        for (const auto &kv : mod.provinces)
            if (!job.moves.contains(kv.first))
                s.insert(kv.first);
        for (const auto &[_t, x] : job.moves)
            s.insert(x);
        job.top = s.empty() ? 0 : *s.rbegin();
    }
    QHash<QString, QString> mp;
    for (const auto &[t, x] : job.moves)
        mp.insert(QString::number(t), QString::number(x));
    QHash<QString, QString> stale;
    QSet<QString> drop, targets;
    for (const auto &[x, h] : job.heirs) {
        if (h) {
            const long long to = job.moves.contains(*h) ? job.moves.value(*h) : *h;
            if (to != x)
                stale.insert(QString::number(x), QString::number(to));
        } else {
            drop.insert(QString::number(x));
        }
    }
    for (const auto &kv : free)
        targets.insert(QString::number(kv.first));
    QSet<QString> gone_s;                            // (C++ only) every deleted number
    for (long long x : gone)
        gone_s.insert(QString::number(x));
    const bool base_hist = mod.base && !mod.replaced({u"history"_s, u"provinces"_s});
    QSet<QString> nums_set = targets + drop;
    for (auto it = mp.cbegin(); it != mp.cend(); ++it)
        nums_set.insert(it.key());
    for (auto it = stale.cbegin(); it != stale.cend(); ++it)
        nums_set.insert(it.key());
    QStringList numbers(nums_set.begin(), nums_set.end());
    std::sort(numbers.begin(), numbers.end());
    std::stable_sort(numbers.begin(), numbers.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
    const QRegularExpression &quick = py::rx(u"(?<![\\w.])(?:%1)(?![\\w.])"_s.arg(numbers.join(u'|')));

    auto count = [&](const QString &kind, long long n) {
        if (n)
            job.counts[kind] += n;
    };
    auto need = [&](const QStringList &parts) -> std::optional<QString> {
        QString p = mod.p(parts);
        if (fs::isfile(p) && !mod.in_mod(p))
            p = mod.adopt(p);                        // (the game's own file: copied into the mod first)
        return fs::isfile(p) ? std::optional<QString>(p) : std::nullopt;
    };

    if (!free.isEmpty()) {
        // --- map/definition.csv: the moved province's row takes the freed number; the rows above the new top go
        const auto dpath = need({u"map"_s, u"definition.csv"_s});
        if (!dpath)
            throw std::runtime_error("expected str, bytes or os.PathLike object, not NoneType");
        const QString path = *dpath;
        const QString text = script::read_text(path);
        const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
        const QStringList lines = text.split(nl);
        QHash<long long, QString> row_of;
        for (const QString &l : lines) {
            const QStringList f = l.split(u';');
            if (py::isdigit(py::strip(f[0])))
                row_of[py::to_int(f[0])] = l;
        }
        QSet<long long> out_rows;
        for (const auto &[t, _x] : job.moves)
            out_rows.insert(t);
        for (long long x : job.dropped)
            out_rows.insert(x);
        QHash<long long, long long> back;
        for (const auto &[t, x] : job.moves)
            back.insert(x, t);
        QStringList out;
        for (QString l : lines) {
            const QStringList f = l.split(u';');
            const std::optional<long long> pid =
                py::isdigit(py::strip(f[0])) ? std::optional<long long>(py::to_int(f[0])) : std::nullopt;
            if (pid && out_rows.contains(*pid))
                continue;
            if (pid && back.contains(*pid)) {
                const long long src = back.value(*pid);
                if (!row_of.contains(src))
                    runtime_error(u"map/definition.csv has no row for #%1"_s.arg(src));
                l = u"%1;%2"_s.arg(*pid).arg(py::split(row_of.value(src), u";"_s, 1).at(1));
            } else if (pid && f.size() > 4 && py::strip(f[4]) == moddata::FREE_NAME) {
                runtime_error(u"no province is left to take number %1"_s.arg(*pid));
            }
            out << l;
        }
        job.texts.insert(path, out.join(nl));
        count(u"definition.csv rows"_s, static_cast<long long>(job.moves.size()));
        // --- map/default.map
        if (const auto dm = need({u"map"_s, u"default.map"_s})) {
            QString t = script::read_text(*dm);
            t = py::re_sub(u"(max_provinces"_s + py::PY_S + u"*="_s + py::PY_S + u"*)\\d+"_s, t,
                           [&](const QRegularExpressionMatch &m) { return m.captured(1) + QString::number(job.top + 1); }, 1);
            const auto m = py::re_search(u"sea_starts"_s + py::PY_S + u"*="_s + py::PY_S + u"*\\{([^}]*)\\}"_s, t);
            if (m.hasMatch()) {
                const auto [body, k] = rewrite(m.captured(1), mp, u"list"_s);
                t = t.left(m.capturedStart(1)) + body + t.mid(m.capturedEnd(1));
                count(u"sea_starts entries"_s, k);
            }
            // (C++ only, 2026-11-08) the renumbered definition.csv is the mod's own: a bare file name would make the
            // game build its province picture from the GAME's rows (measured: random crash at load) — see
            // mapfiles::DefaultMapFile::new_definitions
            const auto dfn = py::re_search(u"(definitions"_s + py::PY_S + u"*="_s + py::PY_S +
                                               u"*)\"(definition\\.csv)\""_s, t);
            if (dfn.hasMatch()) {
                t = t.left(dfn.capturedStart(0)) + dfn.captured(1) +
                    u"\"../mod/%1/map/%2\""_s.arg(pypath::basename(mod.root), dfn.captured(2)) + t.mid(dfn.capturedEnd(0));
                count(u"definitions line pointed at the mod's own definition.csv"_s, 1);
            }
            job.texts.insert(*dm, t);
        }
        // --- lists and blocks of the map folder
        const std::vector<std::tuple<QString, QString, QString>> lists = {
            {u"region.txt"_s, u"list"_s, u"state entries"_s},
            {u"continent.txt"_s, u"list"_s, u"continent entries"_s},
            {u"climate.txt"_s, u"list"_s, u"climate entries"_s},
            {u"positions.txt"_s, u"top"_s, u"places"_s}};
        for (const auto &[name, mode, kind] : lists) {
            if (const auto p = need({u"map"_s, name})) {
                const auto [nw, k] = rewrite(script::read_text(*p), mp, mode);
                if (k) {
                    job.texts.insert(*p, nw);
                    count(kind, k);
                }
            }
        }
        if (const auto p = need({u"map"_s, u"adjacencies.csv"_s})) {
            const QString t = script::read_text(*p);
            const QString nl2 = t.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
            QStringList rows;
            long long k = 0;
            for (const QString &l : t.split(nl2)) {
                QStringList f = l.split(u';');
                if (!py::lstrip(l).startsWith(u'#'))
                    for (int i : {0, 1, 3})
                        if (i < f.size()) {
                            const QString key = py::strip(f[i]);
                            const auto it = mp.constFind(key);
                            if (it != mp.cend()) {
                                f[i] = f[i].replace(key, *it);
                                ++k;
                            }
                        }
                rows << f.join(u';');
            }
            if (k) {
                job.texts.insert(*p, rows.join(nl2));
                count(u"adjacency rows"_s, k);
            }
        }
        // --- history: the province's own file (its name starts with the number), pops of every start date
        const QString hdir = mod.p({u"history"_s, u"provinces"_s});
        if (fs::isdir(hdir) && mod.in_mod(hdir)) {
            walk(hdir, [&](const QString &d, const QStringList &fs0) {
                for (const QString &f : fs0) {
                    const auto m = py::re_match(u"^("_s + py::PY_S + u"*)(\\d+)(.*)$"_s, f);
                    if (m.hasMatch() && mp.contains(m.captured(2)) && py::lower(f).endsWith(u".txt")) {
                        const QString nw = pypath::join(d, m.captured(1) + mp.value(m.captured(2)) + m.captured(3));
                        if (fs::exists(nw))
                            runtime_error(u"history/provinces already has a file for number %1 (%2)"_s.arg(
                                mp.value(m.captured(2)), pypath::basename(nw)));
                        job.renames.emplace_back(pypath::join(d, f), nw);
                        if (base_hist && fs::isfile(pypath::join(*mod.base, QStringList{u"history"_s, u"provinces"_s} +
                                                                                pypath::relpath(pypath::join(d, f), hdir)
                                                                                    .replace(u'\\', u'/')
                                                                                    .split(u'/'))))
                            job.blanks << pypath::join(d, f);   // (the game's file of that name would come back)
                    }
                }
            });
            count(u"history files"_s, static_cast<long long>(job.renames.size()));
        }
        // (C++ only, 2026-11-08) the GAME's history files the mod does not have: the tester's cut map crashed at
        // the campaign start with them left — a moved number's file is copied into the mod under the new number
        // (its old name written empty), a deleted number's file is written empty (else it would be the history of
        // whatever province takes that number)
        if (base_hist) {
            long long moved_h = 0, blanked_h = 0;
            for (const moddata::LayeredFile &lf : mod.layered({u"history"_s, u"provinces"_s})) {
                if (lf.own || !lf.rel.contains(u'/'))
                    continue;
                const QString f = pypath::basename(lf.mine);
                const auto m = py::re_match(u"^("_s + py::PY_S + u"*)(\\d+)(.*)$"_s, f);
                if (!m.hasMatch() || !py::lower(f).endsWith(u".txt"))
                    continue;
                if (mp.contains(m.captured(2))) {
                    const QString nw =
                        pypath::join(pypath::dirname(lf.mine), m.captured(1) + mp.value(m.captured(2)) + m.captured(3));
                    if (fs::exists(nw) || job.texts.contains(nw))
                        runtime_error(u"history/provinces already has a file for number %1 (%2)"_s.arg(
                            mp.value(m.captured(2)), pypath::basename(nw)));
                    job.texts.insert(nw, script::read_text(lf.path));
                    job.blanks << lf.mine;
                    ++moved_h;
                } else if (gone_s.contains(m.captured(2))) {
                    job.blanks << lf.mine;
                    ++blanked_h;
                }
            }
            count(u"game history files copied under the new number"_s, moved_h);
            count(u"game history files of a deleted number emptied"_s, blanked_h);
        }
        // (C++ only, 2026-11-08) the game's pop files the mod does not have: rewritten into the mod (a deleted
        // province's pops go first, then the moved ones take the new number)
        for (const moddata::LayeredFile &lf : mod.layered({u"history"_s, u"pops"_s})) {
            if (lf.own || !py::lower(lf.rel).endsWith(u".txt"))
                continue;
            const QString t = script::read_text(lf.path);
            if (!quick.match(t).hasMatch())
                continue;
            const auto [t1, k1] = drop_top(t, gone_s);
            const auto [t2, k2] = rewrite(t1, mp, u"top"_s);
            if (k1 || k2) {
                job.texts.insert(lf.mine, t2);
                count(u"pop blocks"_s, k2);
                count(u"pop blocks of a deleted province taken out"_s, k1);
            }
        }
        if (fs::isdir(mod.pops_dir())) {
            for (const QString &p : txt_files(mod.pops_dir())) {
                const QString t = script::read_text(p);
                if (quick.match(t).hasMatch()) {
                    const auto [nw, k] = rewrite(t, mp, u"top"_s);
                    if (k) {
                        job.texts.insert(p, nw);
                        count(u"pop blocks"_s, k);
                    }
                }
            }
        }
        // --- names: PROV<number> in the localisation (the deleted province's own line goes)
        // (C++ only, 2026-11-08: the game's own files too — a mod file of the same name replaces the game's)
        {
            for (const moddata::LayeredFile &lf : mod.layered({u"localisation"_s})) {
                const QString f = lf.rel;
                if (f.contains(u'/') || !py::lower(f).endsWith(u".csv"))
                    continue;
                const QString p = lf.own ? lf.path : lf.mine;
                const QString t = script::read_text(lf.path);
                if (!t.contains(u"PROV"_s))
                    continue;
                const QString nl2 = t.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
                QStringList rows;
                long long k = 0;
                for (QString l : t.split(nl2)) {
                    const auto m = py::re_match(u"^PROV(\\d+);"_s, l);
                    if (m.hasMatch() && targets.contains(m.captured(1))) {
                        ++k;
                        continue;
                    }
                    if (m.hasMatch() && mp.contains(m.captured(1))) {
                        l = u"PROV%1;%2"_s.arg(mp.value(m.captured(1)), l.mid(m.capturedEnd(0)));
                        ++k;
                    }
                    rows << l;
                }
                if (k) {
                    job.texts.insert(p, rows.join(nl2));
                    count(u"names"_s, k);
                }
            }
        }
    }
    // --- what starts / is raised where, capitals (every date): moved numbers follow, a deleted one names its heir
    QHash<QString, QString> both = mp;
    for (auto it = stale.cbegin(); it != stale.cend(); ++it)
        both.insert(it.key(), it.value());
    const std::vector<std::pair<QStringList, QString>> hist = {
        {{u"history"_s, u"units"_s}, u"army / navy entries"_s}, {{u"history"_s, u"countries"_s}, u"capitals"_s}};
    // (C++ only, 2026-11-08) a capital on a province gone for good (no heir) moves to the country's lowest province
    // that is left (its number after the renumbering)
    QHash<QString, long long> lowest_owned;
    for (const auto &kv : mod.provinces) {
        if (!kv.second.owner || gone.count(kv.first))
            continue;
        const QString tag = kv.second.owner->toUpper();
        const long long now = job.moves.contains(kv.first) ? job.moves.value(kv.first) : kv.first;
        if (!lowest_owned.contains(tag) || now < lowest_owned.value(tag))
            lowest_owned.insert(tag, now);
    }
    for (const auto &[parts, kind] : hist) {
        // (C++ only, 2026-11-08: the game's own files too, rewritten into the mod — the cut game map crashed on
        // capitals and armies at numbers above the last province)
        if (both.isEmpty() && drop.isEmpty())
            continue;
        const bool units = parts.last() == u"units";
        std::vector<std::pair<QString, QString>> files;   // (path to read, path to write)
        const QString d = pypath::join(mod.root, parts);
        if (fs::isdir(d))
            for (const QString &p : txt_files(d))
                files.emplace_back(p, p);
        for (const moddata::LayeredFile &lf : mod.layered(parts))
            if (!lf.own && py::lower(lf.rel).endsWith(u".txt"))
                files.emplace_back(lf.path, lf.mine);
        for (const auto &[p, out] : files) {
                const QString t = script::read_text(p);
                if (quick.match(t).hasMatch()) {
                    const ScriptResult r = rewrite_script(t, both);
                    QString t2 = r.text;
                    long long k = 0;
                    if (!drop.isEmpty()) {
                        if (units) {
                            std::tie(t2, k) = drop_units(t2, drop);
                        } else {
                            const QString tag = py::head(pypath::basename(p), 3).toUpper();
                            std::optional<QString> repl;
                            if (lowest_owned.contains(tag))
                                repl = QString::number(lowest_owned.value(tag));
                            std::tie(t2, k) = drop_capital(t2, drop, repl);
                        }
                    }
                    if (!r.hits.isEmpty() || k) {
                        job.texts.insert(out, t2);
                        long long n = 0;
                        for (const auto &[_t, c] : r.hits)
                            n += c;
                        count(kind, n);
                        count(units ? u"armies / navies / regiments at a province gone for good taken out"_s
                                    : u"capitals on a province gone for good moved (or taken out: no land left)"_s,
                              k);
                    }
                }
            }
    }
    // --- scripts: nothing may name another province than it meant
    for (const QString &folder : SCRIPT_FOLDERS) {
        const QString d = pypath::join(mod.root, folder.split(u'/'));
        std::vector<std::pair<QString, QString>> files;   // (path to read, path to write)
        if (fs::isdir(d))
            for (const QString &p : txt_files(d))
                files.emplace_back(p, p);
        // (C++ only, 2026-11-08: the game's own scripts too — written into the mod when one changes)
        for (const moddata::LayeredFile &lf : mod.layered(folder.split(u'/')))
            if (!lf.own && py::lower(lf.rel).endsWith(u".txt"))
                files.emplace_back(lf.path, lf.mine);
        for (const auto &[p0, out] : files) {
            const QString &p = out;
            const QString t = script::read_text(p0);
            if (!quick.match(t).hasMatch())
                continue;
            const ScriptResult r = rewrite_script(t, both, drop);
            if (r.text != t)
                job.texts.insert(out, r.text);
            long long moved = 0, re = 0;
            for (const auto &[k, c] : r.hits) {
                if (mp.contains(k))
                    moved += c;
                if (stale.contains(k))
                    re += c;
            }
            count(u"script places of a moved province"_s, moved);
            job.retargeted += re;
            job.neutral += r.cut;
            if (r.left)
                job.left.emplace_back(pypath::relpath(p, mod.root).replace(u'\\', u'/'), r.left);
        }
    }
    return job;
}

std::pair<QStringList, QString> apply(moddata::Mod &mod, const Job &job, const QString &backup_root)
{
    // Write what plan() worked out. -> (files written, backup folder)
    const QString stamp = QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s);
    const QString bdir = pypath::join(backup_root, {u"renumbered_provinces"_s, stamp});
    auto keep = [&](const QString &path) {
        if (!fs::isfile(path)) {                     // (C++ only: a game file written into the mod for the first time)
            fs::makedirs(pypath::dirname(path));
            return;
        }
        const QString dst = pypath::join(bdir, pypath::relpath(path, mod.root));
        fs::makedirs(pypath::dirname(dst));
        fs::copy2(path, dst);
    };
    QStringList written;
    for (const auto &[path, _t] : job.texts)
        if (!mod.in_mod(path))
            runtime_error(u"%1 is not one of the mod's own files"_s.arg(path));
    for (const auto &[path, text] : job.texts) {
        keep(path);
        script::write_text(path, text);
        written << path;
    }
    for (const auto &[old, nw] : job.renames) {
        keep(old);
        fs::replace(old, nw);
        written << nw;
    }
    for (const QString &p : job.blanks) {            // (C++ only, 2026-11-08: see Job::blanks)
        if (!mod.in_mod(p))
            runtime_error(u"%1 is not one of the mod's own files"_s.arg(p));
        if (job.texts.contains(p))
            continue;
        keep(p);
        script::write_text(p, QString());
        written << p;
    }
    QStringList lines = {u"After deleting provinces, "_s + stamp, QString()};
    std::vector<std::pair<long long, long long>> mv(job.moves.begin(), job.moves.end());
    std::stable_sort(mv.begin(), mv.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
    for (const auto &[t, x] : mv)
        lines << u"#%1 %2 -> #%3"_s.arg(N(t), py::strip(job.names.value(t)), N(x));
    for (long long x : job.dropped)
        lines << u"row of #%1 taken out (above every province left)"_s.arg(x);
    lines << QString() << u"Scripts that named a deleted province:"_s;
    std::vector<std::pair<long long, std::optional<long long>>> hs(job.heirs.begin(), job.heirs.end());
    std::sort(hs.begin(), hs.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &[x, h] : hs) {
        QString what;
        if (h) {
            const long long to = job.moves.contains(*h) ? job.moves.value(*h) : *h;
            what = u"now name #%1 %2 (its heir%3)"_s.arg(to).arg(py::strip(job.names.value(*h)),
                                                              to == x ? u", which took its number"_s : QString());
        } else {
            what = u"no heir — taken out (conditions: %1; effects: removed)"_s.arg(NEVER);
        }
        lines << u"  #%1: %2"_s.arg(x).arg(what);
    }
    lines << u"  places changed to the heir: %1; places taken out: %2"_s.arg(job.retargeted).arg(job.neutral);
    lines << QString() << u"Changed:"_s;
    std::vector<std::pair<QString, long long>> cs(job.counts.begin(), job.counts.end());
    std::sort(cs.begin(), cs.end());
    for (const auto &[k, n] : cs)
        lines << u"  %1: %2"_s.arg(k, N(n));
    if (!job.left.empty()) {
        lines << QString() << u"Places that still name a deleted number (not inside a condition or an effect — look at them):"_s;
        for (const auto &[f, n] : job.left)
            lines << u"  %1  x%2"_s.arg(f, N(n));
    }
    fs::makedirs(bdir);
    QFile f(fs::watched(pypath::join(bdir, u"what was renumbered.txt"_s)));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        throw fs::OSError("could not write what was renumbered.txt", 13);
    // (Python's text mode on Windows: "\n" is written as "\r\n")
    f.write((lines.join(u'\n') + u'\n').replace(u"\n"_s, u"\r\n"_s).toUtf8());
    f.close();
    mod.free_slots.clear();
    mod.free_heirs.clear();
    return {written, bdir};
}

}  // namespace provrenumber
