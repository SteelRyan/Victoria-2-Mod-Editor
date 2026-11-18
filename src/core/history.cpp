// history.py — Province history: read state at a date, write edits into a dated block. Line by line.
#include "core/history.h"
#include "core/countryhist.h"
#include "core/document.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"

#include <QDateTime>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace history {

using pypath::join;
using script::Node;
using script::Nodes;
using script::parse_date;

const QStringList SCALAR_KEYS = {u"owner"_s, u"controller"_s, u"life_rating"_s, u"trade_goods"_s,
                                 u"colonial"_s, u"fort"_s,    u"naval_base"_s,  u"railroad"_s};
const QString ISLAND_FLAG = u"island"_s;
const QStringList ISLAND_NAMES = {u"island"_s, u"islands"_s, u"has_island"_s};
const QStringList LEVEL_KEYS = {u"fort"_s, u"naval_base"_s, u"railroad"_s};
const QSet<QString> MANAGED = {u"owner"_s, u"controller"_s, u"life_rating"_s, u"trade_goods"_s, u"colonial"_s,
                               u"fort"_s,  u"naval_base"_s, u"railroad"_s,    u"add_core"_s,    u"remove_core"_s,
                               u"state_building"_s};
static const QStringList COLONIAL_AND_LEVELS = {u"colonial"_s, u"fort"_s, u"naval_base"_s, u"railroad"_s};

// ---------------------------------------------------------------------- small Python pieces
static qsizetype rfind_nl(const QString &t, qsizetype end)        // t.rfind("\n", 0, end)
{
    if (end <= 0)
        return -1;
    return t.lastIndexOf(u'\n', std::min(end, t.size()) - 1);
}
static QString slice(const QString &t, qsizetype a, qsizetype b)  // t[a:b]
{
    a = std::clamp<qsizetype>(a, 0, t.size());
    b = std::clamp<qsizetype>(b, 0, t.size());
    return b > a ? t.mid(a, b - a) : QString();
}
static bool blank(const QString &s) { return py::strip(s).isEmpty(); }
static std::optional<Date> date_of(const Node &n)                 // parse_date(n.key) if n.key else None
{
    return (n.has_key && !n.key.isEmpty()) ? parse_date(n.key) : std::nullopt;
}
static QString stamp_now() { return QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s); }

// _int(v, default): int(float(v)), TypeError / ValueError -> default
static std::optional<long long> _int(const QString &v, std::optional<long long> dflt)
{
    auto f = py::to_float(v);
    if (!f)
        return dflt;
    try {
        return py::trunc_int(*f);
    } catch (const py::ValueError &) {          // (Python: OverflowError for inf, uncaught — see PROGRESS)
        return dflt;
    }
}

QString date_key(const Date &d)
{
    return u"%1.%2.%3"_s.arg(std::get<0>(d)).arg(std::get<1>(d)).arg(std::get<2>(d));
}

std::optional<Date> start_date(const moddata::Mod &mod)
{
    // The game's start date (start_date in common/defines.lua, the mod's or the game's) or None. The undated part
    // of a history file IS that date — edits for it go there, never into a block of that date (user's rule).
    // re.search(r"\bstart_date\s*=\s*['\"](\d+\.\d+\.\d+)['\"]", text, re.I), written out
    const QStringList roots{mod.root, mod.base ? *mod.base : QString()};
    for (const QString &root : roots) {
        if (root.isEmpty())
            continue;
        const QString p = join(root, {u"common"_s, u"defines.lua"_s});
        if (!fs::isfile(p))
            continue;
        QString text;
        try {
            text = script::read_text(p);
        } catch (const fs::OSError &) {
            continue;
        }
        const QString low = py::lower(text);
        const QString word = u"start_date"_s;
        const qsizetype n = text.size();
        for (qsizetype at = low.indexOf(word); at >= 0; at = low.indexOf(word, at + 1)) {
            if (at > 0 && py::is_word_char(text[at - 1]))
                continue;                                   // \b
            qsizetype i = at + word.size();
            while (i < n && py::is_py_space(text[i].unicode()))
                ++i;
            if (i >= n || text[i] != u'=')
                continue;
            ++i;
            while (i < n && py::is_py_space(text[i].unicode()))
                ++i;
            if (i >= n || (text[i] != u'\'' && text[i] != u'"'))
                continue;
            ++i;
            const qsizetype g0 = i;
            bool ok = true;
            for (int part = 0; part < 3 && ok; ++part) {
                const qsizetype d0 = i;
                while (i < n && text[i].category() == QChar::Number_DecimalDigit)
                    ++i;
                ok = i > d0;
                if (ok && part < 2) {
                    ok = i < n && text[i] == u'.';
                    ++i;
                }
            }
            if (!ok || i >= n || (text[i] != u'\'' && text[i] != u'"'))
                continue;
            return parse_date(text.mid(g0, i - g0));
        }
    }
    return std::nullopt;
}

ProvState empty_state() { return ProvState{}; }

// ---------------------------------------------------------------------- HistFile
HistFile::HistFile(const QString &rel_, const QString &path_, const std::optional<QString> &text_)
    : rel(rel_), path(path_)
{
    pending = text_.has_value();
    if (!text_)
        reload();
    else
        _set(*text_);
}

void HistFile::_set(const QString &t)
{
    text = t;
    hash = moddata::text_hash(t);
    nodes = script::parse(t);
}

void HistFile::reload()
{
    _set(script::read_text(path));
    pending = false;
}

// ---------------------------------------------------------------------- ProvinceHistory
ProvinceHistory::ProvinceHistory(moddata::Mod &mod_) : mod(mod_)
{
    hdir = join(mod.root, {u"history"_s, u"provinces"_s});
    for (const auto &[k, b] : mod.buildings)
        if (b.province && !LEVEL_KEYS.contains(k) && k != u"colonial")
            more << k;
    for (const moddata::LayeredFile &lf : mod.layered({u"history"_s, u"provinces"_s})) {
        const QString fn = lf.rel.mid(lf.rel.lastIndexOf(u'/') + 1);
        // re.match(r"^\s*(\d+)", fn)
        qsizetype i = 0;
        while (i < fn.size() && py::is_py_space(fn[i].unicode()))
            ++i;
        const qsizetype d0 = i;
        while (i < fn.size() && fn[i].category() == QChar::Number_DecimalDigit)
            ++i;
        if (!lf.rel.contains(u'/') || i == d0 || !py::lower(fn).endsWith(u".txt"))
            continue;
        HistFileP hf;
        try {
            if (lf.own) {
                hf = std::make_shared<HistFile>(lf.rel, lf.path);
            } else {                 // the base game's file: read from there, written into the MOD when it changes
                hf = std::make_shared<HistFile>(lf.rel, lf.mine, script::read_text(lf.path));
                hf->base = true;
                from_base += 1;
            }
        } catch (const fs::OSError &e) {
            mod.warnings << u"history %1: %2"_s.arg(lf.rel, e.text());
            continue;
        }
        files[py::to_int(fn.mid(d0, i - d0))].push_back(hf);
    }
}

QStringList ProvinceHistory::levels() const { return COLONIAL_AND_LEVELS + more; }

bool ProvinceHistory::managed(const QString &key) const { return MANAGED.contains(key) || more.contains(key); }

const std::optional<Date> &ProvinceHistory::start()
{
    if (!_start)
        _start = start_date(mod);
    return *_start;
}

void ProvinceHistory::_top_edits(const HistFile &hf, const QString &text, const ProvState &want, const QString &nl,
                                 std::vector<std::tuple<qsizetype, qsizetype, QString>> &out)
{
    // The start date's state written into the undated part: its managed lines regenerated, blocks dated on or
    // before the start folded in (their other lines kept, now undated).
    std::vector<const Node *> top, early;
    for (const Node &n : hf.nodes) {
        const auto d = date_of(n);
        if (!d)
            top.push_back(&n);
        if (n.has_key && !n.key.isEmpty() && n.is_block && d && *d <= *start())
            early.push_back(&n);
    }
    QStringList lines;
    if (want.owner && !want.owner->isEmpty())
        lines << u"owner = %1"_s.arg(*want.owner);
    if (want.controller && !want.controller->isEmpty())
        lines << u"controller = %1"_s.arg(*want.controller);
    for (const QString &c : want.cores)
        lines << u"add_core = %1"_s.arg(c);
    if (want.trade_goods && !want.trade_goods->isEmpty())
        lines << u"trade_goods = %1"_s.arg(*want.trade_goods);
    if (want.life_rating)
        lines << u"life_rating = %1"_s.arg(*want.life_rating);
    for (const QString &k : levels())
        if (want.level_of(k))
            lines << u"%1 = %2"_s.arg(k).arg(want.level_of(k));
    for (const Node *b : early)
        for (const Node &c : b->kids)
            if (c.has_key && !c.key.isEmpty() && !managed(c.key))
                lines << slice(text, c.kstart, c.vend);
    for (const Factory &f : want.factories)
        lines << u"state_building = {"_s << u"\tlevel = %1"_s.arg(f.level)
              << u"\tbuilding = %1"_s.arg(f.building.value_or(u"None"_s))
              << u"\tupgrade = %1"_s.arg(f.upgrade ? u"yes"_s : u"no"_s) << u"}"_s;
    QString fresh;
    for (const QString &l : lines)
        fresh += l + nl;
    std::vector<Span> spans, managed;
    for (const Node *n : top)
        if (n->has_key && this->managed(n->key)) {
            spans.push_back(_line_span(text, n->kstart, n->vend));
            managed.push_back(_line_span(text, n->kstart, n->vend));
        }
    for (const Node *n : early)
        spans.push_back(_line_span(text, n->kstart, n->vend));
    std::stable_sort(spans.begin(), spans.end());
    if (!managed.empty()) {
        const Span at = *std::min_element(managed.begin(), managed.end());
        for (const Span &se : spans)
            out.emplace_back(se.first, se.second, se == at ? fresh : QString());
    } else {
        for (const Span &se : spans)
            out.emplace_back(se.first, se.second, QString());
        qsizetype pos = 0;
        for (const QString &line : py::splitlines_keep(text)) {
            if (py::strip(line).startsWith(u'#'))
                pos += line.size();
            else
                break;
        }
        out.emplace_back(pos, pos, fresh);
    }
}

HistFileP ProvinceHistory::main_file(long long pid) const
{
    // The file edits go to: the first non-empty history file of the province.
    const auto *fsl = files.get(pid);
    if (!fsl || fsl->empty())
        return nullptr;
    for (const HistFileP &f : *fsl)
        if (!f->nodes.empty())
            return f;
    return (*fsl)[0];
}

void ProvinceHistory::add_virtual(long long pid, const QString &rel, const QString &text)
{
    // History of a province created in the editor (written to disk with its definition on Apply).
    const auto *fsl = files.get(pid);
    const HistFileP hf = (fsl && !fsl->empty()) ? (*fsl)[0] : nullptr;
    if (hf && hf->pending && hf->rel == rel && hf->text == text)
        return;
    files.insert(pid, {std::make_shared<HistFile>(rel, join(hdir, rel.split(u'/')), text)});
}

void ProvinceHistory::remove_virtual(long long pid)
{
    const auto *fsl = files.get(pid);
    if (fsl && std::any_of(fsl->begin(), fsl->end(), [](const HistFileP &f) { return f->pending && !f->base; }))
        files.remove(pid);
}

void ProvinceHistory::_apply(ProvState &st, const std::vector<const Node *> &nodes, const HistFile &hf,
                             const std::optional<Date> &date) const
{
    for (const Node *np : nodes) {
        const Node &n = *np;
        if (!n.has_key)
            continue;
        const QString &k = n.key;
        const bool s = !n.is_block;
        if ((k == u"owner" || k == u"controller" || k == u"trade_goods") && s) {
            (k == u"owner" ? st.owner : k == u"controller" ? st.controller : st.trade_goods) = n.value;
        } else if (k == u"life_rating" && s) {
            st.life_rating = _int(n.value, std::nullopt);
        } else if (s && (COLONIAL_AND_LEVELS.contains(k) || more.contains(k))) {
            st.set_level(k, *_int(n.value, 0));
        } else if (k == u"add_core" && s && !st.cores.contains(n.value)) {
            st.cores << n.value;
        } else if (k == u"remove_core" && s && st.cores.contains(n.value)) {
            st.cores.removeOne(n.value);
        } else if (k == u"set_province_flag" && s && !st.flags.contains(n.value)) {
            st.flags << n.value;
        } else if (k == u"state_building" && n.is_block) {
            Factory f;
            std::optional<Span> level_span;
            for (const Node &c : n.kids) {
                if (!c.has_key || c.is_block)
                    continue;
                if (c.key == u"building")
                    f.building = c.value;
                else if (c.key == u"level") {
                    f.level = *_int(c.value, 1);
                    level_span = Span{c.vstart, c.vend};
                } else if (c.key == u"upgrade")
                    f.upgrade = py::lower(c.value) == u"yes";
            }
            if (f.building && !f.building->isEmpty()) {
                auto src = std::make_shared<FactorySrc>();
                src->path = hf.path;
                src->span = {n.kstart, n.vend};
                src->level_span = level_span;
                src->date = date;
                src->level = f.level;
                f.src = src;
                st.factories.push_back(f);
            }
        }
    }
}

ProvState ProvinceHistory::state_at(long long pid, const std::optional<Date> &date, bool include_date) const
{
    // Effective history state of a province at `date` ((y, m, d) or None = base values only).
    ProvState st = empty_state();
    static const std::vector<HistFileP> NONE;
    const auto *fp = files.get(pid);
    const std::vector<HistFileP> &fsl = fp ? *fp : NONE;
    for (const HistFileP &hf : fsl) {
        std::vector<const Node *> top;
        for (const Node &n : hf->nodes)
            if (!date_of(n))
                top.push_back(&n);
        _apply(st, top, *hf, std::nullopt);
    }
    if (date) {
        std::vector<std::tuple<Date, const HistFile *, const Node *>> dated;
        for (const HistFileP &hf : fsl)
            for (const Node &n : hf->nodes) {
                const auto d = date_of(n);
                if (d && n.is_block && (*d < *date || (include_date && *d == *date)))
                    dated.emplace_back(*d, hf.get(), &n);
            }
        std::stable_sort(dated.begin(), dated.end(),
                         [](const auto &a, const auto &b) { return std::get<0>(a) < std::get<0>(b); });
        for (const auto &[d, hf, n] : dated) {
            std::vector<const Node *> kids;
            for (const Node &c : n->kids)
                kids.push_back(&c);
            _apply(st, kids, *hf, d);
        }
    }
    return st;
}

std::vector<DatedBlock> ProvinceHistory::dated_blocks(long long pid) const
{
    // [(date, file rel path, key/value lines)] — for showing what later dates change.
    std::vector<DatedBlock> out;
    if (const auto *fsl = files.get(pid))
        for (const HistFileP &hf : *fsl)
            for (const Node &n : hf->nodes) {
                const auto d = date_of(n);
                if (d && n.is_block) {
                    DatedBlock b{*d, hf->rel, {}};
                    for (const Node &c : n.kids)
                        b.lines.emplace_back(c.has_key ? OptStr(c.key) : std::nullopt,
                                             c.is_block ? u"{…}"_s : c.value);
                    out.push_back(b);
                }
            }
    std::stable_sort(out.begin(), out.end(), [](const DatedBlock &a, const DatedBlock &b) {
        // (date, rel, lines) — a None key sorts before a text, as far as Python can compare them at all
        return std::tie(a.date, a.rel, a.lines) < std::tie(b.date, b.rel, b.lines);
    });
    return out;
}

QString ProvinceHistory::_nl(const QString &text)
{
    return (text.contains(u"\r\n"_s) || text.isEmpty()) ? u"\r\n"_s : u"\n"_s;
}

Span ProvinceHistory::_line_span(const QString &text, qsizetype s, qsizetype e)
{
    const qsizetype ls = rfind_nl(text, s) + 1;
    if (!blank(slice(text, ls, s)))
        return {s, e};
    const qsizetype j = text.indexOf(u'\n', e);
    const QString rest = py::strip(slice(text, e, j >= 0 ? j : text.size()));
    if (!rest.isEmpty() && !rest.startsWith(u'#'))
        return {ls, e};
    return {ls, j >= 0 ? j + 1 : text.size()};
}

void ProvinceHistory::_owner_on_top(const HistFile &hf, const QString &text, const ProvState &want,
                                    std::vector<std::tuple<qsizetype, qsizetype, QString>> &out, const QString &nl)
{
    // Edits that set owner / controller (and the owner's core) in the undated part of a history file.
    std::vector<const Node *> top;
    for (const Node &n : hf.nodes)
        if (!date_of(n))
            top.push_back(&n);
    const OptStr owner = want.owner;
    const OptStr ctrl = (want.controller && !want.controller->isEmpty()) ? want.controller : owner;
    QStringList inserts;
    for (const auto &[key, val] : {std::pair{u"owner"_s, owner}, std::pair{u"controller"_s, ctrl}}) {
        const Node *node = nullptr;
        for (const Node *n : top)
            if (n->has_key && n->key == key && !n->is_block) {
                node = n;
                break;
            }
        if (node)
            out.emplace_back(node->vstart, node->vend, val.value_or(u"None"_s));
        else
            inserts << u"%1 = %2"_s.arg(key, val.value_or(u"None"_s));
    }
    if (owner && want.cores.contains(*owner) &&
        !std::any_of(top.begin(), top.end(), [&](const Node *n) {
            return n->has_key && n->key == u"add_core" && !n->is_block && n->value == *owner;
        }))
        inserts << u"add_core = %1"_s.arg(*owner);
    if (inserts.isEmpty())
        return;
    QString ins;
    for (const QString &l : inserts)
        ins += l + nl;
    std::vector<const Node *> anchor;
    for (const Node *n : top)
        if (n->has_key && (n->key == u"owner" || n->key == u"controller" || n->key == u"add_core"))
            anchor.push_back(n);
    if (!anchor.empty()) {
        qsizetype at = 0;
        for (const Node *n : anchor)
            at = std::max(at, n->vend);
        const qsizetype le = text.indexOf(u'\n', at);
        at = le < 0 ? text.size() : le + 1;
        out.emplace_back(at, at, ins);
    } else {
        // the first line of the file after its leading comments
        qsizetype at = 0;
        for (const QString &line : py::splitlines_keep(text)) {
            if (py::strip(line).startsWith(u'#'))
                at += line.size();
            else
                break;
        }
        out.emplace_back(at, at, ins);
    }
}

PlainKey ProvinceHistory::plain_key(const ProvState &st)
{
    // What a province's state says, without where it stands in the files.
    PlainKey k;
    k.owner = st.owner;
    k.controller = st.controller;
    k.cores = st.cores;
    std::sort(k.cores.begin(), k.cores.end());
    k.life_rating = st.life_rating;
    k.trade_goods = st.trade_goods;
    k.colonial = st.colonial;
    k.fort = st.fort;
    k.naval_base = st.naval_base;
    k.railroad = st.railroad;
    k.more = st.more;
    for (const Factory &f : st.factories)
        k.factories.emplace_back(f.building.value_or(QString()), f.level, f.upgrade);
    std::sort(k.factories.begin(), k.factories.end());
    return k;
}

QStringList ProvinceHistory::save(const OrderedMap<long long, ProvState> &changes, const Date &date,
                                  const QString &backup_root, const QString &scenario)
{
    // changes: {pid: desired_state}. Writes one dated block per province (+ source edits for removed /
    // re-levelled factories that come from earlier history). Returns written paths.
    using Edits = std::vector<std::tuple<qsizetype, qsizetype, QString>>;
    const QString dkey = date_key(date);
    OrderedMap<QString, Edits> edits;            // path -> [(start, end, replacement)]
    OrderedMap<QString, QString> texts;
    OrderedMap<QString, HistFileP> touched_files;
    // a file Apply changed before, now wanted exactly as it was then (the change was undone): it gets its old text
    // back, to the letter
    OrderedMap<long long, std::tuple<QString, QString, QString>> back;
    for (const auto &[pid, want] : changes) {
        const HistFileP hf = main_file(pid);
        const Was *got = hf ? _was.get(hf->path) : nullptr;
        if (!got || got->date != date || got->states.size() != 1 || !got->states.contains(pid) ||
            !(*got->states.get(pid) == plain_key(want)))
            continue;
        long long same_file = 0;
        for (const auto &kv : changes)
            same_file += main_file(kv.first) == hf;
        if (same_file != 1)                      // (the file holds more that changes)
            continue;
        try {
            if (!hf->pending && script::read_text(hf->path) == got->after)
                back.insert(pid, {hf->path, got->before, got->after});
        } catch (const fs::OSError &) {
        }
    }
    OrderedMap<long long, PlainKey> was_now;
    for (const auto &kv : changes)
        if (main_file(kv.first))
            was_now.insert(kv.first, plain_key(state_at(kv.first, date)));

    auto text_of = [&](const QString &path) -> QString {
        if (!texts.contains(path)) {
            HistFileP hf;
            for (const auto &kv : files) {
                for (const HistFileP &f : kv.second)
                    if (f->path == path) {
                        hf = f;
                        break;
                    }
                if (hf)
                    break;
            }
            if (hf && hf->pending) {
                texts.insert(path, hf->text);
                return hf->text;
            }
            const QString t = script::read_text(path);
            if (hf && moddata::text_hash(t) != hf->hash)
                throw std::runtime_error((path + u" was changed on disk after it was loaded. Reload before saving."_s)
                                             .toStdString());
            texts.insert(path, t);
        }
        return *texts.get(path);
    };

    // Nations the game creates in its first history pass: those owning a province in the UNDATED history. A nation
    // that only gets land in a dated block (a new one, a releasable) crashes the game while it loads (verified in
    // game) — so its first provinces are written at the top of their files, not in the block.
    QSet<QString> alive;                         // (None among them never equals a nation's tag)
    for (const auto &kv : files)
        if (const OptStr o = state_at(kv.first, std::nullopt).owner)
            alive.insert(*o);
    for (const auto &[pid, want] : changes) {
        if (back.contains(pid))
            continue;
        const HistFileP hf = main_file(pid);
        if (!hf)
            throw std::runtime_error(("Province " + QString::number(pid) + " has no history file to write into.")
                                         .toStdString());
        const QString text = text_of(hf->path);
        const QString nl = _nl(text);
        if (start() && date <= *start()) {
            // (factories of earlier blocks of OTHER files are not regenerated here: only this file is rewritten)
            _top_edits(*hf, text, want, nl, edits[hf->path]);
            touched_files.insert(hf->path, hf);
            continue;
        }
        ProvState before = state_at(pid, date, false);
        const OptStr owner = want.owner;
        if (owner && !owner->isEmpty() && owner != before.owner && !alive.contains(*owner)) {
            _owner_on_top(*hf, text, want, edits[hf->path], nl);
            bool later = false;
            for (const Node &n : hf->nodes) {
                const auto d = date_of(n);
                if (d && n.is_block && *d < date)
                    for (const Node &c : n.kids)
                        later = later || (c.has_key && (c.key == u"owner" || c.key == u"controller"));
            }
            if (!later) {                        // nothing earlier overrides it: the block needn't say it
                before.owner = owner;
                before.controller = (want.controller && !want.controller->isEmpty()) ? want.controller : owner;
                QStringList cores = before.cores;
                if (want.cores.contains(*owner) && !before.cores.contains(*owner))
                    cores << *owner;
                before.cores = cores;
            }
        }
        QStringList lines;
        if (want.owner && !want.owner->isEmpty() && want.owner != before.owner)
            lines << u"owner = %1"_s.arg(*want.owner);
        if (want.controller && !want.controller->isEmpty() && want.controller != before.controller)
            lines << u"controller = %1"_s.arg(*want.controller);
        for (const QString &c : want.cores)
            if (!before.cores.contains(c))
                lines << u"add_core = %1"_s.arg(c);
        for (const QString &c : before.cores)
            if (!want.cores.contains(c))
                lines << u"remove_core = %1"_s.arg(c);
        if (want.life_rating && want.life_rating != before.life_rating)
            lines << u"life_rating = %1"_s.arg(*want.life_rating);
        if (want.trade_goods && !want.trade_goods->isEmpty() && want.trade_goods != before.trade_goods)
            lines << u"trade_goods = %1"_s.arg(*want.trade_goods);
        for (const QString &k : levels())
            if (want.level_of(k) != before.level_of(k))
                lines << u"%1 = %2"_s.arg(k).arg(want.level_of(k));
        // factories: new ones (and ones already living in this date's block) go into the block
        std::vector<const Factory *> block_facts;
        for (const Factory &f : want.factories)
            if (!f.src || f.src->date == std::optional<Date>(date))
                block_facts.push_back(&f);
        // factories from earlier history: removals / level changes are edited at their source
        QHash<const FactorySrc *, const Factory *> kept;
        for (const Factory &f : want.factories)
            if (f.src && f.src->date != std::optional<Date>(date))
                kept.insert(f.src.get(), &f);
        for (const Factory &bf : before.factories) {
            const FactorySrc *src = bf.src.get();
            const Factory *w = src ? kept.value(src, nullptr) : nullptr;
            // match by source position (state_at builds new dicts each call)
            if (!w)
                for (const Factory &f : want.factories)
                    if (f.src && f.src->path == src->path && f.src->span == src->span) {
                        w = &f;
                        break;
                    }
            if (!w) {
                const Span se = _line_span(text_of(src->path), src->span.first, src->span.second);
                edits[src->path].emplace_back(se.first, se.second, QString());
            } else if (w->level != src->level && src->level_span) {
                edits[src->path].emplace_back(src->level_span->first, src->level_span->second, QString::number(w->level));
            }
        }
        // the dated block: keep unmanaged content, regenerate managed lines
        const Node *block = nullptr;
        for (const Node &n : hf->nodes)
            if (date_of(n) == std::optional<Date>(date) && n.is_block) {
                block = &n;
                break;
            }
        QStringList body;
        if (block)
            for (const Node &c : block->kids)
                if (!(c.has_key && managed(c.key)))
                    body << u'\t' + slice(text, c.kstart, c.vend);
        for (const QString &l : lines)
            body << u'\t' + l;
        for (const Factory *f : block_facts)
            body << u"\tstate_building = {"_s << u"\t\tlevel = %1"_s.arg(f->level)
                 << u"\t\tbuilding = %1"_s.arg(f->building.value_or(u"None"_s))
                 << u"\t\tupgrade = %1"_s.arg(f->upgrade ? u"yes"_s : u"no"_s) << u"\t}"_s;
        if (block) {
            if (!body.isEmpty()) {
                edits[hf->path].emplace_back(block->kstart, block->vend,
                                             u"%1 = {%2%3%4}"_s.arg(block->key, nl, body.join(nl), nl));
            } else {
                const Span se = _line_span(text, block->kstart, block->vend);
                edits[hf->path].emplace_back(se.first, se.second, QString());
            }
        } else if (!body.isEmpty()) {
            const QString pre = (text.endsWith(u'\n') || text.endsWith(u'\r') || text.isEmpty()) ? QString() : nl;
            edits[hf->path].emplace_back(text.size(), text.size(),
                                         pre + nl + dkey + u" = {"_s + nl + body.join(nl) + nl + u'}' + nl);
        }
        touched_files.insert(hf->path, hf);
    }

    OrderedMap<QString, QString> whole;          // {path: old text} — undone Applies, given back to the letter
    for (const auto &[pid, b] : back)
        if (!edits.contains(std::get<0>(b)))
            whole.insert(std::get<0>(b), std::get<1>(b));
    if (edits.isEmpty() && whole.isEmpty())
        return {};
    const QString bdir = join(backup_root, {u"history_provinces"_s, scenario, stamp_now()});
    fs::makedirs(bdir);
    QStringList paths;
    for (const auto &kv : edits)
        paths << kv.first;
    for (const auto &kv : whole)
        paths << kv.first;
    for (const QString &path : paths) {
        if (!fs::isfile(path))
            continue;
        const QString dst = join(bdir, pypath::relpath(path, hdir));
        fs::makedirs(pypath::dirname(dst));
        fs::copy2(path, dst);
    }
    OrderedMap<QString, QList<long long>> owners;
    for (const auto &kv : changes)
        if (const HistFileP hf = main_file(kv.first))
            owners[hf->path] << kv.first;
    auto states_of = [&](const QString &path) {
        OrderedMap<long long, PlainKey> s;
        for (long long pid : owners.value(path))
            if (const PlainKey *k = was_now.get(pid))
                s.insert(pid, *k);
        return s;
    };
    for (const auto &[path, lst] : edits) {
        const QString t0 = text_of(path);
        QString t = t0;
        Edits es = lst;
        std::stable_sort(es.begin(), es.end(), [](const auto &a, const auto &b) {
            return std::make_pair(-std::get<0>(a), -std::get<1>(a)) < std::make_pair(-std::get<0>(b), -std::get<1>(b));
        });
        for (const auto &[s, e, rep] : es)
            t = slice(t, 0, s) + rep + t.mid(e);
        fs::makedirs(pypath::dirname(path));
        script::write_text(path, t);
        _was.insert(path, Was{date, t0, t, states_of(path)});
    }
    for (const auto &[path, old] : whole) {
        const QString now = script::read_text(path);
        script::write_text(path, old);
        _was.insert(path, Was{date, now, old, states_of(path)});
    }
    for (const auto &kv : whole)
        edits.insert(kv.first, {});              // (read again and reported like the others)
    // reload the files we wrote
    for (auto &kv : files)
        for (const HistFileP &f : kv.second)
            if (edits.contains(f->path))
                f->reload();
    QStringList out;
    for (const auto &kv : edits)
        out << kv.first;
    std::sort(out.begin(), out.end());
    return out;
}

// ---------------------------------------------------------------------- island flags
bool is_island_flag(const Node &n)
{
    return n.has_key && n.key == u"set_province_flag" && !n.is_block &&
           ISLAND_NAMES.contains(py::lower(py::strip(n.value)));
}

std::vector<const Node *> island_flag_nodes(const Nodes &nodes)
{
    // The set_province_flag = island entries of a history file: top level and inside dated blocks.
    std::vector<const Node *> out;
    for (const Node &n : nodes) {
        if (is_island_flag(n))
            out.push_back(&n);
        else if (n.is_block && date_of(n))
            for (const Node &c : n.kids)
                if (is_island_flag(c))
                    out.push_back(&c);
    }
    return out;
}

IslandFlags::IslandFlags(ProvinceHistory &h) : hist(h)
{
    orig = _scan();
    marked = orig;
}

QSet<long long> IslandFlags::_scan() const
{
    QSet<long long> out;
    for (const auto &[pid, fsl] : hist.files)
        for (const HistFileP &hf : fsl)
            if (!island_flag_nodes(hf->nodes).empty()) {
                out.insert(pid);
                break;
            }
    return out;
}

void IslandFlags::set(const QList<long long> &pids, bool on)
{
    for (long long p : pids) {
        if (on)
            marked.insert(p);
        else
            marked.remove(p);
    }
}

QList<long long> IslandFlags::dirty() const
{
    QSet<long long> x = (marked - orig) | (orig - marked);
    QList<long long> out(x.begin(), x.end());
    std::sort(out.begin(), out.end());
    return out;
}

QString IslandFlags::add_to(const QString &text)
{
    // The text with the flag added: right after the file's other set_province_flag lines, else after its last
    // plain entry (a blank line before, like the mod's files), else at the top.
    const Nodes parsed = script::parse(text);
    if (!island_flag_nodes(parsed).empty())
        return text;
    const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    std::vector<const Node *> top, flags;
    for (const Node &n : parsed)
        if (n.has_key && !n.key.isEmpty() && !parse_date(n.key))
            top.push_back(&n);
    for (const Node *n : top)
        if (n->key == u"set_province_flag")
            flags.push_back(n);
    const QString line = u"set_province_flag = %1"_s.arg(ISLAND_FLAG);
    if (!top.empty()) {
        const Node *anchor = (flags.empty() ? top : flags).back();
        const qsizetype j = text.indexOf(u'\n', anchor->vend);
        if (j < 0)                               // the last line, without a line break
            return text + nl + (flags.empty() ? nl : QString()) + line;
        const qsizetype e = j + 1;
        const QString after = text.mid(e);
        // a blank line before it where the file groups its lines with blank lines (so removing it is exact)
        const QString gap = (flags.empty() && (after.isEmpty() || after.startsWith(nl))) ? nl : QString();
        return text.left(e) + gap + line + nl + after;
    }
    return line + nl + (blank(text) ? QString() : nl) + text;
}

QString IslandFlags::remove_from(const QString &text0)
{
    // The text without its island flag lines — exactly as it was before add_to() for a flag it added.
    QString text = text0;
    const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    const Nodes parsed = script::parse(text);
    auto nodes = island_flag_nodes(parsed);
    std::stable_sort(nodes.begin(), nodes.end(), [](const Node *a, const Node *b) { return -a->kstart < -b->kstart; });
    for (const Node *n : nodes) {
        auto [s, e] = ProvinceHistory::_line_span(text, n->kstart, n->vend);
        if (e >= text.size() && !text.endsWith(u'\n')) {
            // the last line without a line break: the break before it goes too (two if a blank line came along)
            for (int k = 0; k < 2; ++k) {
                if (text.left(s).endsWith(nl))
                    s -= nl.size();
                if (!text.left(s).endsWith(nl))
                    break;
            }
        } else if (s == 0) {
            if (text.mid(e).startsWith(nl))
                e += nl.size();
        } else if (text.left(s).endsWith(nl + nl) && (e >= text.size() || text.mid(e).startsWith(nl))) {
            s -= nl.size();
        }
        text = slice(text, 0, s) + text.mid(e);
    }
    return text;
}

std::optional<QStringList> IslandFlags::save(const QString &backup_root)
{
    const QList<long long> changed = dirty();
    if (changed.isEmpty())
        return std::nullopt;
    OrderedMap<QString, std::pair<HistFileP, QString>> out;      // path -> (HistFile, new text)
    auto text_of = [&](const HistFileP &hf) -> QString {
        if (const auto *o = out.get(hf->path))
            return o->second;
        if (hf->pending)
            return hf->text;
        const QString t = script::read_text(hf->path);
        if (moddata::text_hash(t) != hf->hash)
            throw std::runtime_error((hf->path + u" was changed on disk after it was loaded. Reload before saving."_s)
                                         .toStdString());
        return t;
    };
    for (long long pid : changed) {
        if (marked.contains(pid)) {
            const HistFileP hf = hist.main_file(pid);
            if (!hf)
                throw std::runtime_error(("Province " + QString::number(pid) + " has no history file for its island flag.")
                                             .toStdString());
            out.insert(hf->path, {hf, add_to(text_of(hf))});
        } else if (const auto *fsl = hist.files.get(pid)) {
            for (const HistFileP &hf : *fsl) {
                const QString t = text_of(hf);
                const QString nt = remove_from(t);
                if (nt != t)
                    out.insert(hf->path, {hf, nt});
            }
        }
    }
    const QString bdir = join(backup_root, {u"history_provinces"_s, u"island_flags"_s, stamp_now()});
    for (const auto &[path, v] : out) {
        if (fs::isfile(path)) {
            const QString dst = join(bdir, pypath::relpath(path, hist.hdir));
            fs::makedirs(pypath::dirname(dst));
            fs::copy2(path, dst);
        }
        fs::makedirs(pypath::dirname(path));
        script::write_text(path, v.second);
        v.first->reload();
    }
    orig = _scan();
    const QSet<long long> ch(changed.begin(), changed.end());
    marked = orig | (marked - ch);
    QStringList res;
    for (const auto &kv : out)
        res << kv.first;
    std::sort(res.begin(), res.end());
    return res;
}

// ---------------------------------------------------------------------- new history files
void NewHistFiles::add_many(const OrderedMap<long long, QString> &plan)
{
    for (const auto &[pid, rel] : plan) {
        const auto *fsl = hist.files.get(pid);
        if (!fsl || fsl->empty()) {
            made.insert(pid, rel);
            hist.add_virtual(pid, rel, QString());
        }
    }
}

std::vector<std::pair<long long, QString>> NewHistFiles::snapshot() const
{
    std::vector<std::pair<long long, QString>> s(made.begin(), made.end());
    std::sort(s.begin(), s.end());
    return s;
}

void NewHistFiles::restore(const std::vector<std::pair<long long, QString>> &snap)
{
    OrderedMap<long long, QString> want;
    for (const auto &kv : snap)
        want.insert(kv.first, kv.second);
    QList<long long> mine;
    for (const auto &kv : made)
        mine << kv.first;
    for (long long pid : mine)
        if (!want.contains(pid))
            hist.remove_virtual(pid);
    for (const auto &[pid, rel] : want)
        if (!made.contains(pid))
            hist.add_virtual(pid, rel, QString());
    made = want;
}

QList<long long> NewHistFiles::dirty() const
{
    QList<long long> out;
    for (const auto &kv : made)
        out << kv.first;
    std::sort(out.begin(), out.end());
    return out;
}

QStringList NewHistFiles::save(const QString &)
{
    QStringList out;
    for (long long pid : dirty())
        if (const auto *fsl = hist.files.get(pid))
            for (const HistFileP &hf : *fsl)
                if (hf->pending) {                // (history edits may have written it already)
                    fs::makedirs(pypath::dirname(hf->path));
                    if (!fs::exists(hf->path))
                        script::write_text(hf->path, hf->text);
                    hf->reload();
                    out << hf->path;
                }
    made.clear();
    return out;
}

ProvState clone_state(const ProvState &st) { return st; }

// ---------------------------------------------------------------------- owners on top, start-date blocks
void OwnersOnTop::add(const QList<long long> &ps)
{
    for (long long p : ps)
        pids.insert(p);
}

static QList<long long> sorted_pids(const QSet<long long> &s)
{
    QList<long long> l(s.begin(), s.end());
    std::sort(l.begin(), l.end());
    return l;
}

QStringList OwnersOnTop::dirty() const
{
    QStringList out;
    for (long long p : sorted_pids(pids))
        out << u"owner of #%1 at the top of its file"_s.arg(p);
    return out;
}

std::optional<QStringList> OwnersOnTop::save(const QString &backup_root)
{
    document::Document &d = doc;
    if (pids.isEmpty() || !d.date)
        return std::nullopt;
    OrderedMap<long long, ProvState> ch;
    for (long long p : sorted_pids(pids))
        ch.insert(p, d.prov_state(p));
    const QStringList written = d.hist->save(ch, *d.date, backup_root, d.scenario);
    pids.clear();
    return written;
}

std::pair<std::vector<long long>, QStringList> start_blocks(document::Document &doc, moddata::Mod &mod)
{
    const std::optional<Date> start = doc.hist->start();
    if (!start)
        return {};
    auto early = [&](const Nodes &nodes) {
        return std::any_of(nodes.begin(), nodes.end(), [&](const Node &n) {
            const auto d = date_of(n);
            return n.is_block && d && *d <= *start;
        });
    };
    std::vector<long long> pids;
    for (const auto &[p, fsl] : doc.hist->files)
        if (std::any_of(fsl.begin(), fsl.end(), [&](const HistFileP &hf) { return early(hf->nodes); }))
            pids.push_back(p);
    std::sort(pids.begin(), pids.end());
    QStringList tags;
    if (countryhist::CountryHistory *ch = mod.country_hist.get()) {
        QStringList all;
        for (const auto &kv : ch->files)
            all << kv.first;
        std::sort(all.begin(), all.end());
        for (const QString &tag : all) {
            try {
                if (early(ch->nodes(tag).nodes()))
                    tags << tag;
            } catch (...) {
            }
        }
    }
    return {pids, tags};
}

void StartBlocks::add(const QList<long long> &ps, const QStringList &ts)
{
    for (long long p : ps)
        pids.insert(p);
    for (const QString &t : ts)
        tags.insert(t);
}

QStringList StartBlocks::dirty() const
{
    QStringList out;
    for (long long p : sorted_pids(pids))
        out << u"start-date block of #%1 folded in"_s.arg(p);
    QStringList ts(tags.begin(), tags.end());
    std::sort(ts.begin(), ts.end());
    for (const QString &t : ts)
        out << u"start-date block of %1 folded in"_s.arg(t);
    return out;
}

std::optional<QStringList> StartBlocks::save(const QString &backup_root)
{
    document::Document &d = doc;
    const std::optional<Date> start = d.hist->start();
    if (!start || (pids.isEmpty() && tags.isEmpty()))
        return std::nullopt;
    const bool same = d.date && *d.date == *start;
    QStringList written;
    if (!pids.isEmpty()) {
        OrderedMap<long long, ProvState> ch;
        for (long long p : sorted_pids(pids))
            ch.insert(p, same ? d.prov_state(p) : d.hist->state_at(p, start));
        written = d.hist->save(ch, *start, backup_root, date_key(*start));
    }
    countryhist::NationEdits *nations = nullptr;
    if (const store::StoreP *s = d.maps.get(u"nations"_s))
        nations = dynamic_cast<countryhist::NationEdits *>(s->get());
    countryhist::CountryHistory *ch = mod.country_hist.get();
    if (!tags.isEmpty() && ch) {
        QStringList ts(tags.begin(), tags.end());
        std::sort(ts.begin(), ts.end());
        OrderedMap<QString, countryhist::NationState> want;
        for (const QString &t : ts)
            want.insert(t, (same && nations) ? nations->state(t) : ch->state_at(t, start));
        written += ch->save(want, *start, backup_root);
        if (nations)
            for (const QString &t : tags)
                nations->_orig.remove(t);
    }
    pids.clear();
    tags.clear();
    return written;
}

}  // namespace history
