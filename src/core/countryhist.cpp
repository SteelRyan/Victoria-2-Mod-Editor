// countryhist.py — Nations: game data, history files, new nations, nation edits. Line by line.
#include "core/countryhist.h"
#include "core/fs.h"
#include "core/localisation.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/scriptstore.h"

#include <QDateTime>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace countryhist {

using pypath::join;
using script::Node;
using script::Nodes;
using script::parse;
using script::parse_date;
using script::read_text;
using script::write_text;

const QStringList SCALARS = {u"capital"_s,   u"primary_culture"_s, u"religion"_s,
                             u"government"_s, u"plurality"_s,      u"nationalvalue"_s,
                             u"literacy"_s,  u"non_state_culture_literacy"_s, u"civilized"_s,
                             u"prestige"_s,  u"consciousness"_s,   u"nonstate_consciousness"_s,
                             u"ruling_party"_s, u"last_election"_s, u"schools"_s};
const QStringList UPPER_HOUSE_IDEOLOGIES = {u"fascist"_s,         u"liberal"_s,   u"conservative"_s, u"reactionary"_s,
                                            u"anarcho_liberal"_s, u"socialist"_s, u"communist"_s};

QString date_key(const Date &d) { return history::date_key(d); }

// ---------------------------------------------------------------------- small Python pieces
static bool has(const Node &n) { return n.has_key && !n.key.isEmpty(); }        // `n.key` is truthy
static bool is(const Node &n, const QString &k) { return n.has_key && n.key == k; }
static bool has(const Node &n);

// issues.txt: the political / social reforms, and the unciv ones (economic_reforms / military_reforms, C++ only) with
// each unciv option's civilization_progress_modifier
static void read_reforms(const std::vector<Node> &nodes, std::vector<Reform> &reforms, std::vector<Reform> &unciv,
                         OrderedMap<QString, QString> &reform_of, OrderedMap<QString, double> &progress)
{
    for (const Node &sec : nodes) {
        const bool civ = is(sec, u"political_reforms"_s) || is(sec, u"social_reforms"_s);
        const bool un = is(sec, u"economic_reforms"_s) || is(sec, u"military_reforms"_s);
        if (!(civ || un) || !sec.is_block)
            continue;
        for (const Node &issue : sec.kids)
            if (has(issue) && issue.is_block) {
                QStringList opts;
                for (const Node &o : issue.kids)
                    if (has(o) && o.is_block) {
                        opts << o.key;
                        if (un)
                            for (const Node &e : o.kids)
                                if (is(e, u"civilization_progress_modifier"_s) && !e.is_block)
                                    progress.insert(o.key, e.value.toDouble());
                    }
                if (!opts.isEmpty()) {
                    (civ ? reforms : unciv).push_back(Reform{sec.key, issue.key, opts});
                    for (const QString &o : opts)
                        reform_of.insert(o, issue.key);
                }
            }
    }
}
static std::optional<Date> date_of(const Node &n) { return has(n) ? parse_date(n.key) : std::nullopt; }
static QString slice(const QString &t, qsizetype a, qsizetype b)    // t[a:b]
{
    a = std::clamp<qsizetype>(a, 0, t.size());
    b = std::clamp<qsizetype>(b, 0, t.size());
    return b > a ? t.mid(a, b - a) : QString();
}
static qsizetype rfind_nl(const QString &t, qsizetype end)          // t.rfind("\n", 0, end)
{
    if (end <= 0)
        return -1;
    return t.lastIndexOf(u'\n', std::min(end, t.size()) - 1);
}
static QString rstrip_chars(QString s, const QString &chars)       // s.rstrip(chars)
{
    while (!s.isEmpty() && chars.contains(s.back()))
        s.chop(1);
    return s;
}
static QString stamp_now() { return QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s); }
static bool one_or_yes(const QString &v)                           // v.strip() in ("1", "yes")
{
    const QString s = py::strip(v);
    return s == u"1" || s == u"yes";
}
// Python's re for these patterns: lines end at "\n" only ((*LF)), \s \d \w by Unicode
static const QRegularExpression &rx(const QString &pattern)
{
    static QHash<QString, QRegularExpression> cache;
    auto it = cache.find(pattern);
    if (it == cache.end())
        it = cache.insert(pattern, QRegularExpression(u"(*LF)"_s + pattern,
                                                      QRegularExpression::UseUnicodePropertiesOption));
    return *it;
}
static std::vector<const Node *> ptrs(const Nodes &nodes)
{
    std::vector<const Node *> out;
    for (const Node &n : nodes)
        out.push_back(&n);
    return out;
}
using Edits = std::vector<std::tuple<qsizetype, qsizetype, QString>>;
static QString apply_edits(QString text, Edits edits)
{
    // for s, e, rep in sorted(edits, key=lambda x: (-x[0], -x[1])): text = text[:s] + rep + text[e:]
    std::stable_sort(edits.begin(), edits.end(), [](const auto &a, const auto &b) {
        return std::make_pair(-std::get<0>(a), -std::get<1>(a)) < std::make_pair(-std::get<0>(b), -std::get<1>(b));
    });
    for (const auto &[s, e, rep] : edits)
        text = slice(text, 0, s) + rep + text.mid(std::min(e, text.size()));
    return text;
}

// ---------------------------------------------------------------------------------------------- game data
const QStringList NationData::LIST_FILES = {u"common/governments.txt"_s, u"common/nationalvalues.txt"_s,
                                            u"common/technology.txt"_s, u"common/issues.txt"_s};

NationData::NationData(moddata::Mod &mod_) : mod(mod_) { _load(); }

void NationData::reinit()
{
    folders.clear();
    schools.clear();
    techs.clear();
    area_techs.clear();
    inventions.clear();
    reforms.clear();
    reform_of.clear();
    reform_issues.clear();
    unciv.clear();
    unciv_progress.clear();
    governments.clear();
    nationalvalues.clear();
    _parties.clear();
    _load();
}

QStringList NationData::_files(const QString &folder) const
{
    const QString d = mod.p({folder});
    if (!fs::isdir(d))
        return {};
    QStringList names = fs::listdir(d);
    std::sort(names.begin(), names.end());
    QStringList out;
    for (const QString &f : names)
        if (py::lower(f).endsWith(u".txt"))
            out << join(d, f);
    return out;
}

static Nodes parse_or_empty(const QString &path)
{
    try {
        return parse(read_text(path));
    } catch (const fs::OSError &) {
        return {};
    }
}

void NationData::_load()
{
    for (const Node &n : parse_or_empty(mod.p({u"common"_s, u"technology.txt"_s}))) {
        if (is(n, u"folders"_s) && n.is_block) {
            for (const Node &f : n.kids)
                if (has(f) && f.is_block) {
                    QStringList areas;
                    for (const Node &a : f.kids)
                        if (!a.has_key && !a.is_block)   // (a bare block here would stop Python at the next line)
                            areas << a.value;
                    folders.emplace_back(f.key, areas);
                }
        } else if (is(n, u"schools"_s) && n.is_block) {
            schools.clear();
            for (const Node &s : n.kids)
                if (has(s) && s.is_block)
                    schools << s.key;
        }
    }
    QHash<QString, QString> folder_of;
    for (const auto &[f, areas] : folders)
        for (const QString &a : areas)
            folder_of.insert(a, f);
    for (const QString &path : _files(u"technologies"_s)) {
        for (const Node &n : parse(read_text(path))) {
            if (!(has(n) && n.is_block))
                continue;
            OptStr area;
            QString year;
            for (const Node &c : n.kids)
                if (is(c, u"area"_s) && !c.is_block) {
                    area = c.value;
                    break;
                }
            for (const Node &c : n.kids)
                if (is(c, u"year"_s) && !c.is_block) {
                    year = c.value;
                    break;
                }
            if (!area)
                continue;
            QStringList &lst = area_techs[*area];
            techs.insert(n.key, TechInfo{*area, year, folder_of.value(*area, u"other"_s), lst.size()});
            lst << n.key;
        }
    }
    for (const QString &path : _files(u"inventions"_s)) {
        for (const Node &n : parse(read_text(path))) {
            if (!(has(n) && n.is_block))
                continue;
            QStringList needs;
            const Node *lim = nullptr;
            for (const Node &c : n.kids)
                if (is(c, u"limit"_s) && c.is_block) {
                    lim = &c;
                    break;
                }
            std::function<void(const Nodes &)> walk = [&](const Nodes &nodes) {
                for (const Node &c : nodes) {
                    if (c.has_key && techs.contains(c.key) && !c.is_block)
                        needs << c.key;
                    else if (is(c, u"AND"_s) && c.is_block)
                        walk(c.kids);
                }
            };
            if (lim)
                walk(lim->kids);
            inventions.insert(n.key, InventionInfo{pypath::basename(path), needs});
        }
    }
    read_reforms(parse_or_empty(mod.p({u"common"_s, u"issues.txt"_s})), reforms, unciv, reform_of, unciv_progress);
    for (const Reform &r : reforms)
        reform_issues.insert(r.issue);
    for (const Reform &r : unciv)
        reform_issues.insert(r.issue);
    for (const auto &[rel, out] : {std::pair{u"governments.txt"_s, &governments},
                                   std::pair{u"nationalvalues.txt"_s, &nationalvalues}}) {
        try {
            QStringList keys;
            for (const Node &n : parse(read_text(mod.p({u"common"_s, rel}))))
                if (has(n) && n.is_block)
                    keys << n.key;
            *out = keys;
        } catch (const fs::OSError &) {
        }
    }
}

void NationData::reload_lists(scriptstore::ScriptStore &st)
{
    // Governments, national values, schools and reforms again, from the editor's texts (they were changed in the
    // Politics / Technology tabs).
    auto keys = [&](const QString &rel, const OptStr &inside = std::nullopt) -> QStringList {
        Nodes nodes;
        try {
            nodes = parse(st.text(rel));
        } catch (...) {
            return {};                                       // (None: `or` takes the old list, as for [])
        }
        const Nodes *in = &nodes;
        static const Nodes NONE;
        if (inside) {
            in = &NONE;
            for (const Node &n : nodes)
                if (is(n, *inside) && n.is_block) {
                    in = &n.kids;
                    break;
                }
        }
        QStringList out;
        for (const Node &n : *in)
            if (has(n) && n.is_block)
                out << n.key;
        return out;
    };
    if (QStringList k = keys(u"common/governments.txt"_s); !k.isEmpty())
        governments = k;
    if (QStringList k = keys(u"common/nationalvalues.txt"_s); !k.isEmpty())
        nationalvalues = k;
    if (QStringList k = keys(u"common/technology.txt"_s, u"schools"_s); !k.isEmpty())
        schools = k;
    Nodes nodes;
    try {
        nodes = parse(st.text(u"common/issues.txt"_s));
    } catch (...) {
        return;
    }
    std::vector<Reform> rf, un;
    OrderedMap<QString, QString> rof;
    OrderedMap<QString, double> prog;
    read_reforms(nodes, rf, un, rof, prog);
    if (!rf.empty()) {
        reforms = rf;
        unciv = un;
        unciv_progress = prog;
        reform_of = rof;
        reform_issues.clear();
        for (const Reform &r : rf)
            reform_issues.insert(r.issue);
        for (const Reform &r : un)
            reform_issues.insert(r.issue);
    }
}

const std::vector<Party> &NationData::parties(const QString &tag)
{
    // [(name, ideology, start, end)] of a nation (common/countries/<file>).
    const moddata::Country *c = mod.countries.get(tag);
    OptStr text;
    if (store && c && !c->file.isEmpty()) {
        try {
            text = store->text(u"common/"_s + QString(c->file).replace(u'\\', u'/'));
        } catch (const fs::OSError &) {
            text.reset();
        }
        if (py::strip(text.value_or(QString())).isEmpty() && c->is_new && !c->template_.isEmpty()) {
            const moddata::Country *tpl = mod.countries.get(c->template_);   // (it gets its template's file on Apply)
            if (tpl && !tpl->file.isEmpty()) {
                try {
                    text = store->text(u"common/"_s + QString(tpl->file).replace(u'\\', u'/'));
                } catch (const fs::OSError &) {
                }
            }
        }
        const OptStr *was = _party_text.get(tag);
        if (!was || *was != text) {
            _parties.remove(tag);
            _party_text.insert(tag, text);
        }
    }
    if (!_parties.contains(tag)) {
        std::vector<Party> out;
        const OptStr path = c ? OptStr(mod.p({u"common"_s, QString(c->file).replace(u'/', u'\\')})) : std::nullopt;
        if (text || (path && !path->isEmpty() && fs::isfile(*path))) {
            for (const Node &n : parse(text ? *text : read_text(*path))) {
                if (is(n, u"party"_s) && n.is_block) {
                    OrderedMap<QString, QString> f;
                    for (const Node &x : n.kids)
                        if (has(x) && !x.is_block)
                            f.insert(x.key, x.value);
                    if (!f.value(u"name"_s).isEmpty())
                        out.push_back(Party{f.value(u"name"_s), f.value(u"ideology"_s), f.value(u"start_date"_s),
                                            f.value(u"end_date"_s)});
                }
            }
        }
        _parties.insert(tag, out);
    }
    return *_parties.get(tag);
}

QString NationData::folder_counts(const QSet<QString> &owned) const
{
    // "5/5/4/5/4": techs owned per folder, like the mod's `# Tech DE1 …` comments.
    QStringList parts;
    for (const auto &[f, areas] : folders) {
        long long n = 0;
        for (const QString &t : owned)
            if (const TechInfo *ti = techs.get(t); ti && ti->folder == f)
                ++n;
        parts << QString::number(n);
    }
    return parts.join(u'/');
}

// ---------------------------------------------------------------------------------------------- history
NationState empty_nation()
{
    NationState st;
    for (const QString &k : SCALARS)
        st.scalars.insert(k, std::nullopt);
    return st;
}

NationKey key_of(const NationState &st)
{
    // What counts for 'changed' (sources left out).
    NationKey k;
    for (const QString &s : SCALARS)
        k.scalars.push_back(st.at(s));
    k.cultures = st.cultures;
    for (const auto &kv : st.reforms)
        k.reforms.push_back(kv);
    std::sort(k.reforms.begin(), k.reforms.end());
    k.techs = QStringList(st.techs.begin(), st.techs.end());
    std::sort(k.techs.begin(), k.techs.end());
    k.inventions = QStringList(st.inventions.begin(), st.inventions.end());
    std::sort(k.inventions.begin(), k.inventions.end());
    for (const auto &kv : st.upper_house)
        k.upper_house.push_back(kv);
    std::sort(k.upper_house.begin(), k.upper_house.end());
    return k;
}

const Nodes &NodesOf::nodes() const
{
    static const Nodes NONE;
    return held ? held->second : NONE;
}

CountryHistory::CountryHistory(moddata::Mod &mod_, NationData &data_) : mod(mod_), data(data_)
{
    dir = join(mod.root, {u"history"_s, u"countries"_s});
    for (const moddata::LayeredFile &lf : mod.layered({u"history"_s, u"countries"_s})) {
        // re.match(r"^([A-Za-z0-9]{3})\s*-", rel)
        const QString &rel = lf.rel;
        bool m = rel.size() >= 3;
        for (qsizetype i = 0; m && i < 3; ++i) {
            const char16_t u = rel[i].unicode();
            m = (u >= u'A' && u <= u'Z') || (u >= u'a' && u <= u'z') || (u >= u'0' && u <= u'9');
        }
        if (m) {
            qsizetype i = 3;
            while (i < rel.size() && py::is_py_space(rel[i].unicode()))
                ++i;
            m = i < rel.size() && rel[i] == u'-';
        }
        if (rel.contains(u'/') || !m || !py::lower(rel).endsWith(u".txt"))
            continue;
        const QString tag = py::upper(rel.left(3));
        if (lf.own) {
            if (base_src.contains(tag) || !files.contains(tag)) {
                files.insert(tag, lf.path);
                base_src.remove(tag);
            }
        } else if (!files.contains(tag)) {
            files.insert(tag, lf.mine);
            base_src.insert(tag, lf.path);
        }
    }
}

QString CountryHistory::_now(const QString &tag, const QString &path) const
{
    // The file's text as it is now (the base game's while the mod has none of it yet).
    const QString src = base_src.value(tag);
    return (!src.isEmpty() && !fs::exists(path)) ? read_text(src) : read_text(path);
}

NodesOf CountryHistory::nodes(const QString &tag)
{
    const QString *path = files.get(tag);
    if (!path)
        return NodesOf{std::nullopt, QString(), nullptr};
    if (!_nodes.contains(tag)) {
        const QString *v = virtual_.get(tag);
        const QString text = (v && !fs::exists(*path)) ? *v : _now(tag, *path);
        _nodes.insert(tag, std::make_shared<const std::pair<QString, Nodes>>(text, parse(text)));
    }
    const auto held = *_nodes.get(tag);
    return NodesOf{*path, held->first, held};
}

void CountryHistory::_apply(NationState &st, const std::vector<const Node *> &nodes,
                            const std::optional<Date> &date) const
{
    for (const Node *np : nodes) {
        const Node &n = *np;
        if (!has(n))
            continue;
        const QString &k = n.key;
        const bool s = !n.is_block;
        const Src src{date, {n.kstart, n.vend}};
        if (SCALARS.contains(k) && s) {
            st[k] = n.value;
        } else if (k == u"culture" && s) {
            if (!st.cultures.contains(n.value))
                st.cultures << n.value;
            st.src.insert({u"culture"_s, n.value}, src);
        } else if (k == u"upper_house" && n.is_block) {
            OrderedMap<QString, QString> uh;
            for (const Node &c : n.kids)
                if (has(c) && !c.is_block)
                    uh.insert(c.key, c.value);
            st.upper_house = uh;
        } else if (s && data.reform_issues.contains(k)) {
            st.reforms.insert(k, n.value);
        } else if (data.techs.contains(k) && s) {
            if (one_or_yes(n.value)) {
                st.techs.insert(k);
                st.src.insert({u"tech"_s, k}, src);
            }
        } else if (data.inventions.contains(k) && s) {
            if (one_or_yes(n.value)) {
                st.inventions.insert(k);
                st.src.insert({u"invention"_s, k}, src);
            }
        }
    }
}

NationState CountryHistory::state_at(const QString &tag, const std::optional<Date> &date, bool include_date)
{
    NationState st = empty_nation();
    const NodesOf got = nodes(tag);
    const Nodes &ns = got.nodes();
    std::vector<const Node *> top;
    for (const Node &n : ns)
        if (!date_of(n))
            top.push_back(&n);
    _apply(st, top, std::nullopt);
    if (date) {
        std::vector<std::pair<Date, const Node *>> dated;
        for (const Node &n : ns) {
            const auto d = date_of(n);
            if (d && n.is_block && (*d < *date || (include_date && *d == *date)))
                dated.emplace_back(*d, &n);
        }
        std::stable_sort(dated.begin(), dated.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        for (const auto &[d, n] : dated)
            _apply(st, ptrs(n->kids), d);
    }
    return st;
}

const std::optional<Date> &CountryHistory::start()
{
    if (!_start)
        _start = history::start_date(mod);
    return *_start;
}

Span CountryHistory::_span(const QString &text, const Node &n)
{
    // A node with its whole line (when it stands alone on it).
    const qsizetype a = rfind_nl(text, n.kstart) + 1;
    if (!py::strip(slice(text, a, n.kstart)).isEmpty())
        return {n.kstart, n.vend};
    qsizetype b = text.indexOf(u'\n', n.vend);
    b = b < 0 ? text.size() : b + 1;
    const QString rest = py::strip(slice(text, n.vend, b));
    if (!rest.isEmpty() && !rest.startsWith(u'#'))
        return {a, n.vend};
    return {a, b};
}

QStringList CountryHistory::_tech_order(const QSet<QString> &ts) const
{
    // sorted(ts, key=lambda t: (techs[t]["folder"], techs[t]["area"], techs[t]["index"]))
    std::vector<std::tuple<QString, QString, long long, QString>> v;
    for (const QString &t : ts) {
        const TechInfo *ti = data.techs.get(t);
        if (!ti)
            throw std::out_of_range(("KeyError: '" + t + "'").toStdString());
        v.emplace_back(ti->folder, ti->area, ti->index, t);
    }
    std::sort(v.begin(), v.end());           // (the three keys are unique per tech)
    QStringList out;
    for (const auto &x : v)
        out << std::get<3>(x);
    return out;
}

static QStringList sorted_list(const QSet<QString> &s)
{
    QStringList out(s.begin(), s.end());
    std::sort(out.begin(), out.end());
    return out;
}

Edits CountryHistory::_top_edits(const QString &text, const Nodes &nodes, const NationState &want, const QString &nl,
                                 const Date &start)
{
    // The start date's state written into the undated part of a nation's file (user's rule: the undated part IS the
    // start date). Blocks dated on or before the start are folded in: what they say becomes undated (lines the
    // editor doesn't manage kept). Changed lines are edited where they stand; what is taken away is deleted, not
    // commented out; what is new goes at the end of the undated part.
    std::vector<const Node *> top, early, later;
    for (const Node &n : nodes) {
        if (!has(n))
            continue;
        const auto d = parse_date(n.key);
        if (!d)
            top.push_back(&n);
        else if (n.is_block && *d <= start)
            early.push_back(&n);
        else if (n.is_block && *d > start)
            later.push_back(&n);
    }
    auto managed = [&](const QString &k) {
        return SCALARS.contains(k) || k == u"culture" || k == u"upper_house" || data.reform_issues.contains(k) ||
               data.techs.contains(k) || data.inventions.contains(k);
    };
    NationState base = empty_nation();
    _apply(base, top, std::nullopt);             // the undated part alone
    Edits edits;
    QStringList add;
    for (const Node *n : early) {                // folded in: the block goes, its other lines come along
        const Span sp = _span(text, *n);
        edits.emplace_back(sp.first, sp.second, QString());
        for (const Node &c : n->kids)
            if (has(c) && !managed(c.key))
                add << slice(text, c.kstart, c.vend);
    }
    QHash<QString, const Node *> last;
    for (const Node *n : top)
        if (!n->is_block)
            last.insert(n->key, n);
    for (const QString &k : SCALARS) {
        const OptStr w = want.at(k);
        if (w && w != base.at(k)) {
            if (const Node *l = last.value(k, nullptr))
                edits.emplace_back(l->vstart, l->vend, *w);
            else
                add << u"%1 = %2"_s.arg(k, *w);
        }
    }
    for (const auto &[issue, opt] : want.reforms) {
        const QString *b = base.reforms.get(issue);
        if (!opt.isEmpty() && (!b || opt != *b)) {
            if (const Node *l = last.value(issue, nullptr))
                edits.emplace_back(l->vstart, l->vend, opt);
            else
                add << u"%1 = %2"_s.arg(issue, opt);
        }
    }
    for (const QString &c : want.cultures)
        if (!base.cultures.contains(c))
            add << u"culture = "_s + c;
    for (const QString &t : _tech_order(want.techs - base.techs))
        add << t + u" = 1"_s;
    for (const QString &i : sorted_list(want.inventions - base.inventions))
        add << i + u" = yes"_s;
    for (const Node *n : top) {                  // taken away: the line goes
        const OptStr v = n->is_block ? std::nullopt : OptStr(py::strip(n->value));
        const bool yes = v && (*v == u"1" || *v == u"yes");
        if ((n->key == u"culture" && !(v && want.cultures.contains(*v))) ||
            (data.techs.contains(n->key) && yes && !want.techs.contains(n->key)) ||
            (data.inventions.contains(n->key) && yes && !want.inventions.contains(n->key))) {
            const Span sp = _span(text, *n);
            edits.emplace_back(sp.first, sp.second, QString());
        }
    }
    const Node *uh = nullptr;
    for (const Node *n : top)
        if (n->key == u"upper_house" && n->is_block) {
            uh = n;
            break;
        }
    if (!want.upper_house.isEmpty() && !(want.upper_house == base.upper_house)) {
        QStringList rows;
        for (const auto &[k, v] : want.upper_house)
            rows << u"\t%1 = %2"_s.arg(k, v);
        const QString block = u"upper_house = {"_s + nl + rows.join(nl) + nl + u'}';
        if (uh)
            edits.emplace_back(uh->kstart, uh->vend, block);
        else
            add << block;
    }
    if (!add.isEmpty()) {
        // min([n.kstart for n in early + later] or [len(text)]) — (a node always starts inside the text)
        qsizetype first_dated = text.size();
        for (const auto *grp : {&early, &later})
            for (const Node *n : *grp)
                first_dated = std::min(first_dated, n->kstart);
        const qsizetype at = first_dated < text.size() ? rfind_nl(text, first_dated) + 1 : text.size();
        const QString head = slice(text, 0, at);
        const QString pre = (at == 0 || head.endsWith(u'\n') || head.endsWith(u'\r')) ? QString() : nl;
        QString ins = pre;
        for (const QString &l : add)
            ins += l + nl;
        edits.emplace_back(at, at, ins);
    }
    return edits;
}

QStringList CountryHistory::save(const OrderedMap<QString, NationState> &changes, const Date &date,
                                 const QString &backup_root)
{
    // changes: {tag: wanted state}. Returns the written paths.
    const QString dkey = date_key(date);
    QStringList written;
    const QString bdir = join(backup_root, {u"history_countries"_s, stamp_now()});
    auto backup = [&](const QString &path) {
        fs::makedirs(bdir);
        if (fs::isfile(path))
            fs::copy2(path, join(bdir, pypath::basename(path)));
    };
    for (const auto &[tag, want] : changes) {
        const NodesOf got_nodes = nodes(tag);
        if (!got_nodes.path)
            throw std::runtime_error(
                u"%1 has no history file (history/countries/%1 - ….txt)."_s.arg(tag).toStdString());
        const QString path = *got_nodes.path;
        QString text = _now(tag, path);          // the file as it is now
        fs::makedirs(pypath::dirname(path));
        const Was *got = _was.get(path);
        const NationKey said = key_of(state_at(tag, date));
        if (got && got->date == date && got->after == text && got->said == key_of(want)) {
            // wanted exactly as it was before the last Apply (the change was undone): its old text, to the letter
            const QString old = got->before;
            backup(path);
            write_text(path, old);
            _was.insert(path, Was{date, text, old, said});
            _nodes.remove(tag);
            written << path;
            continue;
        }
        const QString text0 = text;
        const Nodes ns = parse(text);
        const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
        const std::optional<Date> st0 = start();
        if (st0 && date <= *st0) {
            const Edits edits = _top_edits(text, ns, want, nl, *st0);
            if (edits.empty())
                continue;
            backup(path);
            text = apply_edits(text, edits);
            write_text(path, text);
            _was.insert(path, Was{date, text0, text, said});
            _nodes.remove(tag);
            written << path;
            continue;
        }
        const NationState before = state_at(tag, date, false);
        const NationState now = state_at(tag, date);
        Edits edits;
        QStringList lines;
        for (const QString &k : SCALARS) {
            const OptStr w = want.at(k);
            if (w && w != before.at(k))
                lines << u"%1 = %2"_s.arg(k, *w);
        }
        for (const QString &c : want.cultures)
            if (!before.cultures.contains(c))
                lines << u"culture = "_s + c;
        for (const auto &[issue, opt] : want.reforms) {
            const QString *b = before.reforms.get(issue);
            if (!opt.isEmpty() && (!b || opt != *b))
                lines << u"%1 = %2"_s.arg(issue, opt);
        }
        for (const QString &t : _tech_order(want.techs - before.techs))
            lines << t + u" = 1"_s;
        for (const QString &i : sorted_list(want.inventions - before.inventions))
            lines << i + u" = yes"_s;
        QStringList uh;
        if (!want.upper_house.isEmpty() && !(want.upper_house == before.upper_house)) {
            uh << u"upper_house = {"_s;
            for (const auto &[k, v] : want.upper_house)
                uh << u"\t%1 = %2"_s.arg(k, v);
            uh << u"}"_s;
        }
        // taken away: where the file gives it earlier, that line is commented out
        std::vector<SrcKey> gone;
        for (const QString &t : before.techs - want.techs)
            gone.emplace_back(u"tech"_s, t);
        for (const QString &i : before.inventions - want.inventions)
            gone.emplace_back(u"invention"_s, i);
        for (const QString &c : before.cultures)
            if (!want.cultures.contains(c))
                gone.emplace_back(u"culture"_s, c);
        for (const SrcKey &wk : gone) {
            const Src *src = now.src.get(wk);
            if (!src)
                src = before.src.get(wk);
            if (src && src->date != std::optional<Date>(date))
                edits.emplace_back(src->span.first, src->span.first, u"#"_s);
        }
        // the date's block: keep what the editor doesn't manage, write the managed lines again
        const Node *block = nullptr;
        for (const Node &n : ns)
            if (date_of(n) == std::optional<Date>(date) && n.is_block) {
                block = &n;
                break;
            }
        QStringList body;
        if (block) {
            for (const Node &c : block->kids) {
                const bool m = c.has_key && (SCALARS.contains(c.key) || c.key == u"culture" ||
                                             c.key == u"upper_house" || data.reform_issues.contains(c.key) ||
                                             data.techs.contains(c.key) || data.inventions.contains(c.key));
                if (!m)
                    body << u'\t' + slice(text, c.kstart, c.vend);
            }
        }
        for (const QString &l : lines)
            body << u'\t' + l;
        for (const QString &l : uh)
            body << u'\t' + l;
        if (block) {
            const QString rep = body.isEmpty() ? QString() : block->key + u" = {"_s + nl + body.join(nl) + nl + u'}';
            edits.emplace_back(block->kstart, block->vend, rep);
        } else if (!body.isEmpty()) {
            const QString pre = (text.endsWith(u'\n') || text.endsWith(u'\r') || text.isEmpty()) ? QString() : nl;
            edits.emplace_back(text.size(), text.size(),
                               pre + nl + dkey + u" = {"_s + nl + body.join(nl) + nl + u'}' + nl);
        }
        if (edits.empty())
            continue;
        backup(path);
        text = apply_edits(text, edits);
        write_text(path, text);
        _was.insert(path, Was{date, text0, text, said});
        _nodes.remove(tag);
        written << path;
    }
    return written;
}

// ---------------------------------------------------------------------------------------------- new nations
const QSet<QString> RESERVED = {u"REB"_s, u"UNC"_s, u"NAT"_s,  u"AND"_s,  u"NOT"_s, u"ALL"_s, u"ANY"_s,
                                u"THIS"_s, u"FROM"_s, u"CON"_s, u"PRN"_s, u"AUX"_s, u"NUL"_s};

bool tag_ok(const QString &tag)
{
    // TAG_RE.match: ^[A-Z][A-Z0-9]{2}$ ($ also before a last "\n")
    QString t = tag;
    if (t.size() == 4 && t.back() == u'\n')
        t.chop(1);
    if (t.size() != 3)
        return false;
    for (qsizetype i = 0; i < 3; ++i) {
        const char16_t u = t[i].unicode();
        if (!((u >= u'A' && u <= u'Z') || (i > 0 && u >= u'0' && u <= u'9')))
            return false;
    }
    return true;
}

QString safe_name(const QString &name)
{
    QString s;
    for (QChar c : name)
        if (!QStringView(u"\\/:*?\"<>|").contains(c))
            s += c;
    s = rstrip_chars(py::strip(s), u"."_s);
    return s.isEmpty() ? u"Nation"_s : s;
}

QStringList party_names(const QString &ctext, const std::optional<Date> &date)
{
    // The parties of a nation's file (common/countries/…): [name], those running at `date` (y, m, d) first.
    std::vector<std::tuple<bool, long long, QString>> out;
    auto it = rx(u"(?ms)^[ \\t]*party[ \\t]*=[ \\t]*\\{.*?^[ \\t]*\\}"_s).globalMatch(ctext);
    while (it.hasNext()) {
        const QString block = it.next().captured(0);
        const auto nm = rx(u"(?m)\\bname[ \\t]*=[ \\t]*\"?([^\"\\r\\n]+?)\"?[ \\t]*(?:#[^\\r\\n]*)?\\r?$"_s).match(block);
        if (!nm.hasMatch())
            continue;
        auto when = [&](const QString &key, const Date &dflt) -> Date {
            const auto d = rx(u"\\b%1[ \\t]*=[ \\t]*(\\d+)\\.(\\d+)\\.(\\d+)"_s.arg(key)).match(block);
            return d.hasMatch() ? Date{py::to_int(d.captured(1)), py::to_int(d.captured(2)), py::to_int(d.captured(3))}
                                : dflt;
        };
        const bool runs = !date || (when(u"start_date"_s, {0, 1, 1}) <= *date && *date <= when(u"end_date"_s, {9999, 1, 1}));
        out.emplace_back(!runs, static_cast<long long>(out.size()), py::strip(nm.captured(1)));
    }
    std::sort(out.begin(), out.end());
    QStringList names;
    for (const auto &x : out)
        names << std::get<2>(x);
    return names;
}

std::pair<QString, QString> generic_party(const moddata::Mod &mod, const QString &tag)
{
    // A plain party for a nation that has none: the mod's first ideology, the first choice of every party issue,
    // running through the whole game.
    auto first_keys = [&](const QString &rel, const OptStr &top = std::nullopt) -> Nodes {
        Nodes nodes;
        try {
            nodes = parse(read_text(mod.p(rel.split(u'/'))));
        } catch (...) {
            return {};
        }
        if (top) {
            Nodes in;
            for (const Node &n : nodes)
                if (is(n, *top) && n.is_block) {
                    in = n.kids;
                    break;
                }
            nodes = in;
        }
        Nodes out;
        for (const Node &n : nodes)
            if (has(n) && n.is_block)
                out.push_back(n);
        return out;
    };
    OptStr ideology;
    for (const Node &group : first_keys(u"common/ideologies.txt"_s)) {
        QStringList keys;
        for (const Node &c : group.kids)
            if (has(c) && c.is_block)
                keys << c.key;
        if (keys.contains(u"conservative"_s)) {          // (the plainest one, where the mod has it)
            ideology = u"conservative"_s;
            break;
        }
        if (!ideology || ideology->isEmpty())
            ideology = keys.isEmpty() ? std::nullopt : OptStr(keys[0]);
    }
    QStringList lines = {u"party = {"_s, u"\tname = \"%1_generic\""_s.arg(tag), u"\tstart_date = 1800.1.1"_s,
                         u"\tend_date = 2000.1.1"_s,
                         u"\tideology = "_s + ((ideology && !ideology->isEmpty()) ? *ideology : u"conservative"_s)};
    for (const Node &issue : first_keys(u"common/issues.txt"_s, u"party_issues"_s)) {
        for (const Node &c : issue.kids)
            if (has(c) && c.is_block) {
                lines << u"\t%1 = %2"_s.arg(issue.key, c.key);
                break;
            }
    }
    lines << u"}"_s;
    return {lines.join(u"\r\n"_s) + u"\r\n"_s, tag + u"_generic"_s};
}

WithParty with_party(const moddata::Mod &mod, const QString &tag, QString ctext, const QString &htext0,
                     const std::optional<Date> &date)
{
    // A new nation's two files with what the game needs to set the nation up: at least one party in its file, and
    // in its history a ruling party that IS one of its parties (running at the start where one does).
    WithParty r;
    QStringList names = party_names(ctext, date), did;
    if (names.isEmpty()) {
        const auto [block, key] = generic_party(mod, tag);
        ctext = rstrip_chars(ctext, u"\r\n"_s) + u"\r\n\r\n"_s + block;
        names = {key};
        r.texts.insert(key, u"Generic Party"_s);
        did << u"it had no party: a generic one was added"_s;
    }
    QString htext = htext0;
    const auto m = rx(u"(?m)^([ \\t]*ruling_party[ \\t]*=[ \\t]*)\"?([^\"\\r\\n#]*?)\"?[ \\t]*(#[^\\r\\n]*)?\\r?$"_s)
                       .match(htext);
    if (!m.hasMatch()) {
        htext = rstrip_chars(htext, u"\r\n"_s) + u"\r\nruling_party = %1\r\n"_s.arg(names[0]);
        did << u"it had no ruling party: "_s + names[0];
    } else if (!names.contains(py::strip(m.captured(2)))) {
        const qsizetype s2 = m.capturedStart(2), e2 = m.capturedEnd(2);
        const qsizetype a = s2 - (htext[s2 - 1] == u'"' ? 1 : 0);
        const qsizetype b = e2 + (slice(htext, e2, e2 + 1) == u"\"" ? 1 : 0);
        const QString was = py::strip(m.captured(2));
        htext = htext.left(a) + names[0] + htext.mid(b);
        did << u"its ruling party %1 is not one of its parties: %2"_s.arg(was.isEmpty() ? u"(none)"_s : was, names[0]);
    }
    r.ctext = ctext;
    r.htext = htext;
    r.did = did.join(u"; "_s);
    return r;
}

NewNations::NewNations(moddata::Mod &mod_, CountryHistory &hist_) : mod(mod_), hist(hist_) {}

QString NewNations::history_text(const QString &template_text)
{
    // The template's history without its armies (an `oob = …` line would give the new nation the template's units).
    QString t = template_text;
    return t.replace(rx(u"(?m)^[ \\t]*oob[ \\t]*=[^\\r\\n]*(\\r?\\n)?"_s), QString());
}

QString NewNations::problem(const QString &tag0, const QString &name) const
{
    const QString tag = py::upper(py::strip(tag0));
    if (!tag_ok(tag))
        return u"The tag must be 3 letters / digits, starting with a letter (e.g. NE1)."_s;
    if (mod.countries.contains(tag) || RESERVED.contains(tag))
        return tag + u" is already used."_s;
    if (py::strip(name).isEmpty())
        return u"Give it a name."_s;
    const QString fn = u"countries/%1.txt"_s.arg(safe_name(name));
    bool taken = false;
    for (const auto &kv : mod.countries)
        taken = taken || py::lower(kv.second.file) == py::lower(fn);
    if (taken || fs::exists(mod.p(QStringList{u"common"_s} + fn.split(u'/'))))
        return u"common/%1 already exists — pick another name."_s.arg(fn);
    return {};
}

void NewNations::add(const QString &tag0, const QString &name, const QString &adj, const Rgb &color,
                     const QString &tmpl)
{
    const QString tag = py::upper(py::strip(tag0));
    NewNation rec;
    rec.name = py::strip(name);
    rec.adj = py::strip(adj).isEmpty() ? py::strip(name) : py::strip(adj);
    rec.color = color;
    rec.template_ = tmpl;
    rec.file = u"countries/%1.txt"_s.arg(safe_name(name));
    added.insert(tag, rec);
    _show();
}

void NewNations::add_imported(const QString &tag0, const QString &name, const QString &adj, const Rgb &color,
                              const QString &file, const OptStr &ctext, const OptStr &htext)
{
    // A nation taken over from another mod: no template nation — its history comes along as text, and its
    // common/countries file is one of the editor's texts (ctext None: the parties, the colour can be worked on before
    // Apply, which writes it with the other scripts). Flags and names are queued by the importer.
    const QString tag = py::upper(py::strip(tag0));
    NewNation rec;
    const QString nm = py::strip(name), ad = py::strip(adj);
    rec.name = nm.isEmpty() ? tag : nm;
    rec.adj = !ad.isEmpty() ? ad : !nm.isEmpty() ? nm : tag;
    rec.color = color;
    rec.file = file;
    rec.ctext = ctext;
    rec.htext = htext;
    rec.stored = !ctext;
    rec.imported = true;
    added.insert(tag, rec);
    _show();
}

void NewNations::_show()
{
    // The new nations are in mod.countries (pickers, map colours) and have a (virtual) history file.
    QStringList drop;
    for (const auto &[t, c] : mod.countries)
        if (c.is_new && !added.contains(t))
            drop << t;
    for (const QString &tag : drop) {
        mod.countries.remove(tag);
        hist.files.remove(tag);
        hist.virtual_.remove(tag);
        hist._nodes.remove(tag);
    }
    for (const auto &[tag, rec] : added) {
        moddata::Country c;
        c.tag = tag;
        c.name = rec.name;
        c.color = rec.color;
        c.dynamic = false;
        c.file = rec.file;
        c.is_new = true;
        c.template_ = rec.template_;
        mod.countries.insert(tag, c);
        if (!hist.virtual_.contains(tag)) {
            const QString text = rec.htext ? *rec.htext : hist.nodes(rec.template_).text;   // (imported: its own)
            hist.virtual_.insert(tag, history_text(text));
            hist.files.insert(tag, join(hist.dir, u"%1 - %2.txt"_s.arg(tag, safe_name(rec.name))));
            hist._nodes.remove(tag);
        }
    }
}

store::Snap NewNations::snapshot() const
{
    // tuple((t, tuple(sorted(r.items()))) for t, r in sorted(self.added.items())) — pairs by tag (a keyed picture)
    std::vector<std::pair<QString, NewNation>> v(added.begin(), added.end());
    std::sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    store::Items it;
    for (const auto &[t, r] : v)
        it.insert(store::Key(t), store::box(r));
    return std::make_shared<const store::ItemsSnap>(it, true);
}

void NewNations::restore(const store::Snap &snap)
{
    added.clear();
    for (const auto &[k, r] : dynamic_cast<const store::ItemsSnap &>(*snap).items)
        added.insert(k.text(), store::unbox<NewNation>(r));
    _show();
}

QStringList NewNations::dirty() const
{
    QStringList out;
    for (const auto &kv : added)
        out << kv.first;
    std::sort(out.begin(), out.end());
    return out;
}

static bool starts_tpl(const QString &line, const QString &key)   // re.match(r"^\s*KEY\s*=", line)
{
    qsizetype i = 0;
    while (i < line.size() && py::is_py_space(line[i].unicode()))
        ++i;
    if (line.mid(i, key.size()) != key)
        return false;
    i += key.size();
    while (i < line.size() && py::is_py_space(line[i].unicode()))
        ++i;
    return i < line.size() && line[i] == u'=';
}

std::optional<QStringList> NewNations::save(const QString &backup_root)
{
    if (added.isEmpty())
        return std::nullopt;
    const QString bdir = join(backup_root, {u"new_nations"_s, stamp_now()});
    fs::makedirs(bdir);
    QStringList written;
    // countries.txt: each new line right after its template's (never after `dynamic_tags`)
    const QString src = mod.p({u"common"_s, u"countries.txt"_s});
    const QString text = read_text(src);
    fs::copy2(src, join(bdir, u"countries.txt"_s));
    // always the mod's own countries.txt — a mod that uses the base game's gets its own copy (never the game's)
    const QString path = join(mod.root, {u"common"_s, u"countries.txt"_s});
    fs::makedirs(pypath::dirname(path));
    const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    QStringList lines = text.split(nl);
    for (const auto &[tag, rec] : added) {
        // already listed (an earlier Apply wrote this file, then failed further on): never a second row
        // (C++ only, 2026-11-14: the Python editor added the row again on the retry)
        if (std::any_of(lines.cbegin(), lines.cend(), [&](const QString &l) { return starts_tpl(l, tag); }))
            continue;
        const QString row =u"%1\t\t= \"%2\""_s.arg(tag, rec.file);
        std::optional<qsizetype> at;
        if (!rec.template_.isEmpty())
            for (qsizetype i = 0; i < lines.size(); ++i)
                if (starts_tpl(lines[i], rec.template_)) {
                    at = i;
                    break;
                }
        qsizetype dyn = lines.size();
        for (qsizetype i = 0; i < lines.size(); ++i)
            if (starts_tpl(lines[i], u"dynamic_tags"_s)) {
                dyn = i;
                break;
            }
        if (!at || *at > dyn)
            at = dyn - 1;
        lines.insert(std::clamp<qsizetype>(*at + 1, 0, lines.size()), row);
    }
    write_text(path, lines.join(nl));
    written << path;
    OrderedMap<QString, QString> extra_names;
    party_notes.clear();
    for (const auto &[tag, rec] : added) {
        const moddata::Country *tpl = mod.countries.get(rec.template_);
        // common/countries/<Name>.txt: the template's, with the new colour
        const OptStr csrc = (tpl && !tpl->file.isEmpty())
                                ? OptStr(mod.p(QStringList{u"common"_s} + tpl->file.split(u'/')))
                                : std::nullopt;
        const QString dst = join(mod.root, QStringList{u"common"_s} + rec.file.split(u'/'));   // (the mod's)
        QString ctext = (csrc && !csrc->isEmpty() && fs::isfile(*csrc)) ? read_text(*csrc)
                                                                         : u"color = { 128 128 128 }\r\n"_s;
        if (rec.ctext)                           // (imported: its own file, as it was in the other mod)
            ctext = *rec.ctext;
        const auto [r, g, b] = rec.color;
        const auto cm = rx(u"(?m)^([ \\t]*color[ \\t]*=[ \\t]*)\\{[^}]*\\}"_s).match(ctext);
        if (cm.hasMatch())
            ctext = ctext.left(cm.capturedStart(0)) + cm.captured(1) + u"{%1 %2 %3}"_s.arg(r).arg(g).arg(b) +
                    ctext.mid(cm.capturedEnd(0));
        else
            ctext = u"color = {%1 %2 %3}\r\n"_s.arg(r).arg(g).arg(b) + ctext;
        // a nation needs a party of its own, and a ruling party that is one of them (else the game ends)
        const bool stored_there = rec.stored && fs::isfile(dst);
        const QString hpath0 = hist.files.value(tag);
        const bool fresh = !hpath0.isEmpty() && !fs::exists(hpath0);
        if (fresh && !stored_there) {
            WithParty wp = with_party(mod, tag, ctext, hist.virtual_.value(tag), date);
            ctext = wp.ctext;
            hist.virtual_.insert(tag, wp.htext);
            for (const auto &kv : wp.texts)
                extra_names.insert(kv.first, kv.second);
            if (!wp.did.isEmpty())
                party_notes << u"%1: %2"_s.arg(tag, wp.did);
        }
        if (!stored_there) {                     // (stored: written with the scripts)
            fs::makedirs(pypath::dirname(dst));
            write_text(dst, ctext);
            written << dst;
        }
        // history — with its own capital at the top (the template's would be another nation's province)
        const QString hpath = hist.files.value(tag);
        if (!hpath.isEmpty() && !fs::exists(hpath)) {
            QString htext = hist.virtual_.value(tag);
            OptStr cap;
            if (nations) {
                try {
                    cap = nations->state(tag).at(u"capital"_s);
                } catch (...) {
                    cap.reset();
                }
            }
            if (cap && !cap->isEmpty()) {
                const auto km = rx(u"(?m)^([ \\t]*capital[ \\t]*=[ \\t]*)[^\\s\\x1c-\\x1f]+"_s).match(htext);
                if (km.hasMatch())
                    htext = htext.left(km.capturedStart(0)) + km.captured(1) + *cap + htext.mid(km.capturedEnd(0));
                else
                    htext = u"capital = %1\r\n"_s.arg(*cap) + htext;
            }
            // a mod without history/countries of its own (it uses the base game's): the folder first
            // (C++ only, 2026-11-14: the Python editor failed here with "No such file or directory")
            fs::makedirs(pypath::dirname(hpath));
            write_text(hpath, htext);
            written << hpath;
        }
        hist.virtual_.remove(tag);
        hist._nodes.remove(tag);
        // flags: the template's under the new tag
        const QString fdir = mod.p({u"gfx"_s, u"flags"_s});              // read from (the mod's, else the game's) …
        const QString own = join(mod.root, {u"gfx"_s, u"flags"_s});      // … written into the mod's, never the game's
        if (!rec.template_.isEmpty() && fs::isdir(fdir)) {
            for (const QString &fn : fs::listdir(fdir)) {
                if (py::upper(fn).startsWith(py::upper(rec.template_)) && py::lower(fn).endsWith(u".tga")) {
                    const QString fresh_name = tag + fn.mid(rec.template_.size());
                    if (!fs::exists(join(own, fresh_name))) {
                        fs::makedirs(own);
                        fs::copy2(join(fdir, fn), join(own, fresh_name));
                        written << join(own, fresh_name);
                    }
                }
            }
        }
        if (moddata::Country *c = mod.countries.get(tag))
            c->is_new = false;
    }
    OrderedMap<QString, QString> names;
    for (const auto &[tag, rec] : added) {
        names.insert(tag, rec.name);
        names.insert(tag + u"_ADJ"_s, rec.adj);
    }
    for (const auto &kv : extra_names)
        names.insert(kv.first, kv.second);
    OrderedMap<QString, QString> sections;
    for (const auto &kv : names)
        sections.insert(kv.first, u"country"_s);
    localisation::write_names(mod, names, backup_root, sections);
    added.clear();
    return written;
}

// ---------------------------------------------------------------------------------------------- nation edits
NationState NationEdits::state(const QString &tag) const
{
    if (const NationState *w = want.get(tag))
        return *w;
    return _orig_state(tag);
}

void NationEdits::edit(const QString &tag, const std::function<void(NationState &)> &fn)
{
    NationState st = clone(state(tag));
    fn(st);
    want.insert(tag, st);
}

const NationState &NationEdits::_orig_state(const QString &tag) const
{
    if (!_orig.contains(tag))
        _orig.insert(tag, hist.state_at(tag, date));
    return *_orig.get(tag);
}

store::Snap NationEdits::snapshot() const
{
    store::Items it;
    for (const auto &[t, s] : want)
        it.insert(store::Key(t), store::box(clone(s)));
    return std::make_shared<const store::ItemsSnap>(it, false);
}

void NationEdits::restore(const store::Snap &snap)
{
    want.clear();
    for (const auto &[k, v] : dynamic_cast<const store::ItemsSnap &>(*snap).items)
        want.insert(k.text(), store::unbox<NationState>(v));
}

QStringList NationEdits::dirty() const
{
    QStringList out;
    for (const auto &[t, s] : want)
        if (!(key_of(s) == key_of(_orig_state(t))))
            out << t;
    return out;
}

// --- undo past an Apply: a nation not in a step's picture was "as in its file" — said outright once it changes
store::Snap NationEdits::rebase_begin()
{
    store::Items it;
    for (const QString &t : dirty())
        it.insert(store::Key(t), store::box(clone(_orig_state(t))));
    return std::make_shared<const store::ItemsSnap>(it, false);
}

store::Snap NationEdits::rebase(const store::Snap &snap, const store::Snap &old)
{
    store::Items it = dynamic_cast<const store::ItemsSnap &>(*snap).items;
    for (const auto &[k, v] : dynamic_cast<const store::ItemsSnap &>(*old).items)
        if (!it.contains(k))
            it.insert(k, v);
    return std::make_shared<const store::ItemsSnap>(it, false);
}

std::optional<QStringList> NationEdits::save(const QString &backup_root)
{
    const QStringList tags = dirty();
    if (tags.isEmpty() || !date)
        return std::nullopt;
    OrderedMap<QString, NationState> ch;
    for (const QString &t : tags)
        ch.insert(t, *want.get(t));
    const QStringList written = hist.save(ch, *date, backup_root);
    want.clear();
    _orig.clear();
    return written;
}

}  // namespace countryhist
