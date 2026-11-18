// diplomacy.py — treaties at the start (history/diplomacy). Line by line.
#include "core/diplomacy.h"
#include "core/countryhist.h"
#include "core/fs.h"
#include "core/oob.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"

#include <QDateTime>
#include <QSet>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace diplomacy {

using pypath::join;
using script::parse_date;

const QStringList KINDS = {u"alliance"_s, u"vassal"_s, u"substate"_s, u"union"_s, u"guarantee"_s};
static OrderedMap<QString, QString> om(std::initializer_list<std::pair<QString, QString>> l)
{
    OrderedMap<QString, QString> m;
    for (const auto &kv : l)
        m.insert(kv.first, kv.second);
    return m;
}
const OrderedMap<QString, QString> KIND_FILE = om({{u"alliance"_s, u"Alliances.txt"_s}, {u"vassal"_s, u"PuppetStates.txt"_s},
                                                   {u"substate"_s, u"PuppetStates.txt"_s}, {u"union"_s, u"Unions.txt"_s},
                                                   {u"guarantee"_s, u"Guarantees.txt"_s}});
const OrderedMap<QString, QString> KIND_NAME = om({{u"alliance"_s, u"Alliance"_s}, {u"vassal"_s, u"Puppet"_s},
                                                   {u"substate"_s, u"Substate"_s}, {u"union"_s, u"Union"_s},
                                                   {u"guarantee"_s, u"Guarantee"_s}});

QString date_str(const Date &d) { return history::date_key(d); }

static QString strip_quotes(QString s)       // s.strip('"')
{
    while (s.startsWith(u'"'))
        s.remove(0, 1);
    while (s.endsWith(u'"'))
        s.chop(1);
    return s;
}

// Counter(xs).most_common(1)[0][0]: the most frequent, the first seen among equals
static OptStr most_common(const QStringList &xs)
{
    OrderedMap<QString, long long> n;
    for (const QString &x : xs)
        n[x] += 1;
    OptStr best;
    long long top = 0;
    for (const auto &[x, c] : n)
        if (c > top) {
            best = x;
            top = c;
        }
    return best;
}

DiploEdits::DiploEdits(moddata::Mod &mod_, const std::optional<Date> &date_) : mod(mod_), date(date_)
{
    files = _files();
    orig = _read();
    records = orig;
    _next = static_cast<long long>(records.size());
}

OrderedMap<QString, QString> DiploEdits::_files() const
{
    OrderedMap<QString, QString> out;
    QStringList roots;
    if (mod.base && !mod.base->isEmpty() && !mod.replaced({u"history"_s, u"diplomacy"_s}))
        roots << *mod.base;
    roots << mod.root;
    for (const QString &root : roots) {
        const QString d = join(root, {u"history"_s, u"diplomacy"_s});
        if (!fs::isdir(d))
            continue;
        QStringList names = fs::listdir(d);
        std::sort(names.begin(), names.end());
        for (const QString &fn : names)
            if (py::lower(fn).endsWith(u".txt"))
                out.insert(fn, join(d, fn));
    }
    return out;
}

std::vector<Record> DiploEdits::_read() const
{
    std::vector<Record> out;
    for (const auto &[fn, path] : files) {
        const QString text = script::read_text(path);
        for (const script::Node &n : script::parse(text)) {
            if (!(n.has_key && KINDS.contains(n.key) && n.is_block))
                continue;
            OrderedMap<QString, QString> f;
            for (const script::Node &c : n.kids)
                if (c.has_key && !c.key.isEmpty() && !c.is_block)
                    f.insert(c.key, c.value);
            Record r;
            r.id = static_cast<long long>(out.size());
            r.kind = n.key;
            r.first = strip_quotes(f.value(u"first"_s));
            r.second = strip_quotes(f.value(u"second"_s));
            if (const QString *s = f.get(u"start_date"_s))
                r.start = *s;
            if (const QString *e = f.get(u"end_date"_s))
                r.end = *e;
            r.file = fn;
            r.span = Span{n.kstart, n.vend};
            out.push_back(r);
        }
    }
    return out;
}

bool DiploEdits::active(const Record &r) const
{
    if (!date)
        return true;
    const auto s = parse_date(r.start.value_or(QString())), e = parse_date(r.end.value_or(QString()));
    return (!s || *s <= *date) && (!e || *date < *e);
}

std::vector<Record> DiploEdits::at_start() const
{
    std::vector<Record> out;
    for (const Record &r : records)
        if (active(r))
            out.push_back(r);
    return out;
}

std::vector<Record> DiploEdits::between(const QString &a, const QString &b) const
{
    std::vector<Record> out;
    const QSet<QString> want{a, b};
    for (const Record &r : at_start())
        if (QSet<QString>{r.first, r.second} == want)
            out.push_back(r);
    return out;
}

std::vector<Record> DiploEdits::of(const QString &tag) const
{
    std::vector<Record> out;
    for (const Record &r : at_start())
        if (tag == r.first || tag == r.second)
            out.push_back(r);
    return out;
}

std::optional<std::pair<QString, QString>> DiploEdits::overlord(const QString &tag) const
{
    for (const Record &r : at_start())
        if (r.second == tag && (r.kind == u"vassal" || r.kind == u"substate"))
            return std::make_pair(r.first, r.kind);
    return std::nullopt;
}

void DiploEdits::remove(long long rid)
{
    std::erase_if(records, [&](const Record &r) { return r.id == rid; });
}

void DiploEdits::add(const QString &what, const QString &first, const QString &second)
{
    QStringList ends;
    for (const Record &r : records)
        if (r.kind == what && r.end && !r.end->isEmpty())
            ends << *r.end;
    if (ends.isEmpty())
        for (const Record &r : records)
            if (r.end && !r.end->isEmpty())
                ends << *r.end;
    QString end = ends.isEmpty() ? u"1936.1.1"_s : *most_common(ends);
    const QString start = date ? date_str(*date) : u"1836.1.1"_s;
    if (const auto pe = parse_date(end); pe && date && *pe <= *date)
        end = u"1936.1.1"_s;
    QStringList fs_;
    for (const Record &r : records)
        if (r.kind == what)
            fs_ << r.file;
    const QString fn = fs_.isEmpty() ? KIND_FILE.value(what) : *most_common(fs_);
    Record r;
    r.id = _next;
    r.kind = what;
    r.first = first;
    r.second = second;
    r.start = start;
    r.end = end;
    r.file = fn;
    records.push_back(r);
    _next += 1;
}

void DiploEdits::set_subject(const QString &a, const QString &b, const std::optional<std::pair<QString, QString>> &subj)
{
    // kind: None, ("vassal" | "substate", overlord) — the subject relation between a and b.
    for (const Record &r : between(a, b))
        if (r.kind == u"vassal" || r.kind == u"substate")
            remove(r.id);
    if (subj) {
        const auto &[k, lord] = *subj;
        add(k, lord, lord == a ? b : a);
    }
}

void DiploEdits::set_treaty(const QString &a, const QString &b, const QString &what, bool on)
{
    std::vector<Record> have;
    for (const Record &r : between(a, b))
        if (r.kind == what)
            have.push_back(r);
    if (on && have.empty())
        add(what, a, b);
    else if (!on)
        for (const Record &r : have)
            remove(r.id);
}

using DSnap = std::pair<std::vector<Record>, long long>;   // (the records, _next)

store::Snap DiploEdits::snapshot() const { return store::box(DSnap{records, _next}); }

void DiploEdits::restore(const store::Snap &snap)
{
    const DSnap &s = store::unbox<DSnap>(snap);
    records = s.first;
    _next = s.second;
}

QStringList DiploEdits::dirty() const
{
    QSet<long long> now;
    for (const Record &r : records)
        now.insert(r.id);
    QSet<QString> out;
    for (const Record &r : orig)
        if (!now.contains(r.id))
            out.insert(r.file);
    for (const Record &r : records)
        if (!r.span)
            out.insert(r.file);
    QStringList l(out.begin(), out.end());
    std::sort(l.begin(), l.end());
    return l;
}

static QString opt(const OptStr &s) { return s ? *s : u"None"_s; }   // "%s" % None

QString DiploEdits::block(const Record &r, const QString &nl)
{
    return QStringList{r.kind + u" = {"_s, u"\tfirst = "_s + r.first, u"\tsecond = "_s + r.second,
                       u"\tstart_date = "_s + opt(r.start), u"\tend_date = "_s + opt(r.end), u"}"_s}
        .join(nl);
}

std::optional<QStringList> DiploEdits::save(const QString &backup_root)
{
    const QStringList todo = dirty();
    if (todo.isEmpty())
        return std::nullopt;
    const QString bdir = join(backup_root, {u"history_diplomacy"_s,
                                            QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s)});
    QStringList written;
    QSet<long long> keep;
    for (const Record &r : records)
        keep.insert(r.id);
    for (const QString &fn : todo) {
        const QString src = files.value(fn);
        const QString dst = join(mod.root, {u"history"_s, u"diplomacy"_s, fn});
        QString text = (!src.isEmpty() && fs::isfile(src)) ? script::read_text(src) : QString();
        if (fs::isfile(dst)) {
            fs::makedirs(bdir);
            fs::copy2(dst, join(bdir, fn));
        }
        const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
        std::vector<std::tuple<qsizetype, qsizetype, QString, long long>> edits;
        const qsizetype n = text.size();
        for (const Record &r : orig) {
            if (r.file != fn || keep.contains(r.id))
                continue;
            qsizetype s = r.span->first, e = r.span->second;
            while (e < n && (text[e] == u' ' || text[e] == u'\t'))
                ++e;
            e += text.mid(e, 2) == u"\r\n" ? 2 : (text.mid(e, 1) == u"\n" ? 1 : 0);
            while (s > 0 && (text[s - 1] == u' ' || text[s - 1] == u'\t'))
                --s;
            edits.emplace_back(s, e, QString(), -1);
        }
        long long i = 0;
        for (const Record &r : records) {
            if (r.file != fn || r.span)
                continue;
            // after the last treaty of this kind with the same first nation, else at the end
            std::optional<qsizetype> at;
            for (const Record &q : orig)
                if (q.file == fn && keep.contains(q.id) && q.kind == r.kind && q.first == r.first)
                    at = std::max(at.value_or(q.span->second), q.span->second);
            if (at) {
                edits.emplace_back(*at, *at, nl + block(r, nl), i);
            } else {
                const QString pre = (text.isEmpty() || text.endsWith(u'\n') || text.endsWith(u'\r')) ? QString() : nl;
                edits.emplace_back(n, n, pre + block(r, nl) + nl, i);
            }
            ++i;
        }
        std::stable_sort(edits.begin(), edits.end(), [](const auto &a, const auto &b) {
            return std::make_tuple(-std::get<0>(a), -std::get<1>(a), -std::get<3>(a)) <
                   std::make_tuple(-std::get<0>(b), -std::get<1>(b), -std::get<3>(b));
        });
        for (const auto &[s, e, rep, _i] : edits)
            text = text.left(s) + rep + text.mid(e);
        fs::makedirs(pypath::dirname(dst));
        script::write_text(dst, text);
        written << dst;
        files.insert(fn, dst);
    }
    orig = _read();
    records = orig;
    _next = static_cast<long long>(records.size());
    return written;
}

QStringList great_powers(const moddata::Mod &mod, countryhist::NationEdits &nations, qsizetype n)
{
    // The great powers at the start: the top 8 westernized (civilized) nations by prestige. Only they have an
    // influence level (standing) with other nations and spheres of influence.
    std::vector<std::pair<double, QString>> rows;
    for (const auto &kv : nations.hist.files) {
        const QString &t = kv.first;
        if (!mod.countries.contains(t) || t == u"REB" || t == u"UNC")
            continue;
        const countryhist::NationState st = nations.state(t);
        if (py::lower(py::strip(st.at(u"civilized"_s).value_or(QString()))) != u"yes")
            continue;
        const OptStr p = st.at(u"prestige"_s);
        double pr = 0.0;
        if (p && !p->isEmpty())
            pr = py::to_float(*p).value_or(0.0);
        rows.emplace_back(-pr, t);
    }
    std::sort(rows.begin(), rows.end());
    QStringList out;
    for (qsizetype i = 0; i < qsizetype(rows.size()) && i < n; ++i)
        out << rows[size_t(i)].second;
    return out;
}

// ---------------------------------------------------------------------------------------------- relations (OOB)
std::pair<const oob::Relation *, const oob::Relation *> pair(const oob::OOBEdits &oob, const QString &a, const QString &b)
{
    return {oob.state(a).relations.get(b), oob.state(b).relations.get(a)};
}

long long relation_value(const oob::OOBEdits &oob, const QString &a, const QString &b)
{
    // The relation between a and b at the start: both files' values added (clamped to ±200).
    const auto [ra, rb] = pair(oob, a, b);
    long long tot = 0;
    for (const oob::Relation *r : {ra, rb}) {
        if (!r || !r->value)
            continue;
        const auto f = py::to_float(*r->value);   // int(float(r["value"])), a ValueError skipped
        if (f)
            tot += py::trunc_int(*f);
    }
    return std::max(-200LL, std::min(200LL, tot));
}

std::optional<long long> level_of(const oob::OOBEdits &oob, const QString &a, const QString &b)
{
    const oob::Relation *r = oob.state(a).relations.get(b);
    if (!r || !r->level)
        return std::nullopt;
    try {
        return py::to_int(*r->level);
    } catch (const py::ValueError &) {
        return std::nullopt;
    }
}

long long influence_of(const oob::OOBEdits &oob, const QString &a, const QString &b)
{
    const oob::Relation *r = oob.state(a).relations.get(b);
    if (!r || !r->influence_value)
        return 0;
    const auto f = py::to_float(*r->influence_value);
    return f ? py::trunc_int(*f) : 0;
}

OrderedMap<QString, QStringList> sphere_lords(const oob::OOBEdits &oob, const QStringList &tags)
{
    // {sphereling: [lords]} — nations with level 5 towards it.
    OrderedMap<QString, QStringList> out;
    for (const QString &t : tags)
        for (const auto &[b, r] : oob.state(t).relations)
            if (py::strip(r.level.value_or(QString())) == u"5")
                out[b] << t;
    return out;
}

}  // namespace diplomacy