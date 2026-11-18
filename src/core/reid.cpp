// reid.py — the event-id system and Re-ID. Line by line.
#include "core/reid.h"
#include "core/blockcode.h"
#include "core/pynum.h"
#include "core/pyre.h"
#include "core/pytext.h"
#include "core/script.h"
#include "core/scriptstore.h"

#include <QFileInfo>
#include <algorithm>
#include <functional>
#include <set>
#include <tuple>

using namespace Qt::StringLiterals;

namespace reid {

using script::Node;

const QString SYSTEM_FILE = u"ModEditor ID system.txt"_s;
const QString DOC_FILE = u"ModEditor ID system.md"_s;
const OrderedMap<QString, QString> MODELS = {{u"blocks"_s, u"A block of ids per file"_s},
                                             {u"running"_s, u"One running number for the whole mod"_s},
                                             {u"tidy"_s, u"Each file from its own lowest id"_s}};
const QStringList CALL_FOLDERS = {u"events"_s, u"decisions"_s, u"common"_s, u"inventions"_s, u"technologies"_s};

// ---------------------------------------------------------------------------------------------- where ids stand
eventtools::Ids defined(const moddata::Mod &mod, scriptstore::ScriptStore &store, bool own_only, const Texts *after)
{
    // {id: [(rel, pos, len)]} of the events defined — the mod's own files only (a base-game file is not changed).
    eventtools::Ids out;
    for (const auto &[i, lst] : eventtools::event_ids(mod, store, after))
        for (const auto &p : lst)
            if (!own_only || is_own(mod, std::get<0>(p)))
                out[i].push_back(p);
    return out;
}

bool is_own(const moddata::Mod &mod, const QString &rel)
{
    return QFileInfo(mod.root + u'/' + rel).isFile();
}

eventtools::Ids calls(const moddata::Mod &mod, scriptstore::ScriptStore &store)
{
    // {id: [(rel, pos, len)]} of every place an event is called.
    eventtools::Ids out;
    auto site = [&](const QString &rel, const Node &n) {
        const QString v = py::strip(n.value);
        if (py::isdigit(v))
            out[v].emplace_back(rel, n.vstart, n.vend - n.vstart);
    };
    std::function<void(const QString &, const script::Nodes &, bool)> walk = [&](const QString &rel,
                                                                                 const script::Nodes &nodes, bool top) {
        for (const Node &n : nodes) {
            const bool ev = n.has_key && eventtools::EVENT_KEYS.contains(n.key);
            if (top && ev && n.is_block) {
                walk(rel, n.kids, false);            // an event itself (its id is a definition): only what it calls
                continue;
            }
            if (ev) {
                if (n.is_block) {
                    for (const Node &c : n.kids)
                        if (c.has_key && c.key == u"id" && !c.is_block) {
                            site(rel, c);
                            break;
                        }
                } else {
                    site(rel, n);
                }
                continue;
            }
            if (n.is_block)
                walk(rel, n.kids, false);
        }
    };
    for (const QString &folder : CALL_FOLDERS) {
        for (const QString &rel : blockcode::files_of(mod, &store, folder)) {
            if (!py::lower(rel).endsWith(u".txt"))
                continue;
            script::Nodes nodes;
            try {
                nodes = script::parse(store.text(rel));
            } catch (...) {
                continue;
            }
            if (py::lower(rel).endsWith(u"common/on_actions.txt")) {
                for (const Node &top : nodes)
                    if (top.is_block)
                        for (const Node &c : top.kids) {
                            if (c.is_block) {
                                for (const Node &d : c.kids)
                                    if (!d.is_block)
                                        site(rel, d);
                            } else {
                                site(rel, c);
                            }
                        }
                continue;
            }
            walk(rel, nodes, folder == u"events");
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- the system
std::optional<System> read_system(const QString &text)
{
    // The ID system from its file, or nothing.
    if (text.isEmpty())
        return std::nullopt;
    script::Nodes nodes;
    try {
        nodes = script::parse(text);
    } catch (...) {
        return std::nullopt;
    }
    System sysd;
    for (const Node &n : nodes) {
        if (!n.has_key)
            continue;
        if (n.key == u"model" && !n.is_block) {
            sysd.model = py::strip(n.value);
        } else if ((n.key == u"start" || n.key == u"block") && !n.is_block && py::isdigit(py::strip(n.value))) {
            (n.key == u"start" ? sysd.start : sysd.block) = py::to_int(n.value);
        } else if (n.key == u"files" && n.is_block) {
            for (const Node &c : n.kids)
                if (c.has_key && !c.key.isEmpty() && !c.is_block && py::isdigit(py::strip(c.value)))
                    sysd.files.emplace_back(py::strip(c.key, u"\""_s), py::to_int(c.value));
        }
    }
    return MODELS.contains(sysd.model) ? std::optional<System>(sysd) : std::nullopt;
}

QString write_system(const System &sysd)
{
    QStringList lines = {u"# The event id system of this mod - kept by ModEditor (Scripts > ID system). The game does not read"_s,
                         u"# this file. A new event of a file gets the next free id of the file's block."_s,
                         u"model = "_s + sysd.model, u"start = "_s + QString::number(sysd.start),
                         u"block = "_s + QString::number(sysd.block), u"files = {"_s};
    for (const auto &[rel, first] : sysd.files)
        lines << u"\t\"%1\" = %2"_s.arg(rel, QString::number(first));
    lines << u"}"_s;
    return lines.join(u"\r\n"_s) + u"\r\n"_s;
}

static QStringList own_event_files(const moddata::Mod &mod, scriptstore::ScriptStore &store)
{
    QStringList out;
    for (const QString &rel : blockcode::files_of(mod, &store, u"events"_s))
        if (is_own(mod, rel))
            out << rel;
    return out;
}

// {rel: its ids in the order they stand} of the definitions
static OrderedMap<QString, QStringList> order_of(const eventtools::Ids &dfn)
{
    OrderedMap<QString, std::vector<std::pair<qsizetype, QString>>> by_file;
    for (const auto &[i, lst] : dfn)
        for (const auto &p : lst)
            by_file[std::get<0>(p)].emplace_back(std::get<1>(p), i);
    OrderedMap<QString, QStringList> order;
    for (auto &[rel, v] : by_file) {
        std::sort(v.begin(), v.end());
        QStringList ids;
        for (const auto &pi : v)
            ids << pi.second;
        order.insert(rel, ids);
    }
    return order;
}

Plan plan(const moddata::Mod &mod, scriptstore::ScriptStore &store, const System &sysd, const QStringList &files0)
{
    // The new ids: (mapping {old: new}, the system with every file's first id filled in, notes).
    Plan out;
    QStringList &notes = out.notes;
    const eventtools::Ids dfn = defined(mod, store);
    const eventtools::Ids alld = eventtools::event_ids(mod, store);
    QSet<QString> doubled;
    for (const auto &[i, lst] : alld)
        if (lst.size() > 1)
            doubled.insert(i);
    const QStringList own_files = own_event_files(mod, store);
    QStringList files;
    for (const QString &f : (files0.isEmpty() ? own_files : files0))
        if (own_files.contains(f))
            files << f;
    const OrderedMap<QString, QStringList> order = order_of(dfn);
    QStringList skip;
    for (const QString &f : files)
        for (const QString &i : order.value(f))
            if (doubled.contains(i))
                skip << i;
    std::sort(skip.begin(), skip.end());
    if (!skip.isEmpty())
        notes << u"%1 id%2 defined twice (%3) left as they are: which event a call meant cannot be told — "
                 u"Errors → doubled event ids gives the later ones free ids first"_s
                     .arg(QString::number(skip.size()), skip.size() == 1 ? QString() : u"s"_s, skip.mid(0, 8).join(u", "_s));
    QSet<QString> moving;
    for (const QString &f : files)
        for (const QString &i : order.value(f))
            if (!doubled.contains(i))
                moving.insert(i);
    std::set<long long> used;                                    // ids that stay: never given to another event
    for (const auto &kv : alld)
        if (!moving.contains(kv.first) && py::isdigit(kv.first))
            used.insert(py::to_int(kv.first));
    OrderedMap<QString, QString> mapping;
    const long long start = std::max(1LL, sysd.start), block = std::max(1LL, sysd.block);
    OrderedMap<QString, long long> firsts;
    for (const auto &[rel, first] : sysd.files)
        firsts.insert(rel, first);
    const QString &model = sysd.model;

    auto take = [&](long long n) {
        while (used.count(n))
            ++n;
        used.insert(n);
        return n;
    };
    if (model == u"running") {
        long long n = start;
        for (const QString &f : files)
            for (const QString &i : order.value(f))
                if (moving.contains(i)) {
                    n = take(n);
                    mapping.insert(i, QString::number(n));
                    ++n;
                }
    } else if (model == u"tidy") {
        for (const QString &f : files) {
            QStringList ids;
            for (const QString &i : order.value(f))
                if (moving.contains(i))
                    ids << i;
            std::vector<long long> nums;
            for (const QString &i : ids)
                if (py::isdigit(i))
                    nums.push_back(py::to_int(i));
            if (nums.empty())
                continue;
            long long n = *std::min_element(nums.begin(), nums.end());
            for (const QString &i : ids) {
                n = take(n);
                mapping.insert(i, QString::number(n));
                ++n;
            }
        }
    } else {                                                     // blocks
        std::set<long long> taken_blocks;
        for (const auto &kv : firsts)
            taken_blocks.insert(kv.second);
        long long nxt = start;
        for (const QString &f : own_files)
            if (!firsts.contains(f)) {
                while (taken_blocks.count(nxt))
                    nxt += block;
                firsts.insert(f, nxt);
                taken_blocks.insert(nxt);
            }
        for (const QString &f : files) {
            QStringList ids;
            for (const QString &i : order.value(f))
                if (moving.contains(i))
                    ids << i;
            const long long first = *firsts.get(f);
            long long n = first;
            for (const QString &i : ids) {
                n = take(n);
                if (n >= first + block)
                    notes << u"%1: more events than its block of %2 — they run on past %3"_s.arg(
                        f, QString::number(block), QString::number(first + block - 1));
                mapping.insert(i, QString::number(n));
                ++n;
            }
        }
    }
    if (model != u"blocks") {                                    // each file's first id as it is afterwards
        firsts.clear();
        for (const QString &f : own_files) {
            std::vector<long long> nums;
            for (const QString &i : order.value(f)) {
                const QString v = mapping.value(i, i);
                if (py::isdigit(v))
                    nums.push_back(py::to_int(v));
            }
            if (!nums.empty())
                firsts.insert(f, *std::min_element(nums.begin(), nums.end()));
        }
    }
    for (const auto &[a, b] : mapping)
        if (a != b)
            out.mapping.insert(a, b);
    out.sys = sysd;
    out.sys.files.clear();
    for (const auto &kv : firsts)
        out.sys.files.emplace_back(kv.first, kv.second);
    std::stable_sort(out.sys.files.begin(), out.sys.files.end(),
                     [](const auto &x, const auto &y) { return x.second < y.second; });
    return out;
}

std::pair<Texts, Counts> rewrite(const moddata::Mod &mod, scriptstore::ScriptStore &store,
                                 const OrderedMap<QString, QString> &mapping)
{
    // {rel: new text} with every definition and every call of the ids in `mapping` changed.
    using Site = std::tuple<qsizetype, qsizetype, QString, QString>;   // (pos, len, new, "d" / "c")
    OrderedMap<QString, std::set<Site>> sites;
    for (const auto &[i, lst] : defined(mod, store))
        if (const QString *nw = mapping.get(i))
            for (const auto &[rel, pos, ln] : lst)
                sites[rel].insert({pos, ln, *nw, u"d"_s});
    for (const auto &[i, lst] : calls(mod, store))
        if (const QString *nw = mapping.get(i))
            for (const auto &[rel, pos, ln] : lst)
                sites[rel].insert({pos, ln, *nw, u"c"_s});
    Texts texts;
    Counts cnt;
    for (const auto &[rel, lst] : sites) {
        QString text = store.text(rel);
        for (auto it = lst.rbegin(); it != lst.rend(); ++it) {   // from the end: earlier positions stay right
            const auto &[pos, ln, nw, kind] = *it;
            const QString old = py::slice(text, pos, pos + ln);
            const auto m = py::re_match(uR"re(("?)(\d+)("?)$)re"_s, old);
            if (!m.hasMatch())
                continue;
            text = py::head(text, pos) + m.captured(1) + nw + m.captured(3) + py::tail(text, pos + ln);
            if (kind == u"d")
                ++cnt.defined;
            else
                ++cnt.calls;
        }
        texts.insert(rel, text);
    }
    cnt.files = texts.size();
    return {texts, cnt};
}

OptStr next_id(const moddata::Mod &mod, scriptstore::ScriptStore &store, const QString &rel,
               const std::optional<System> &sysd, const QStringList &taken, const Texts *after)
{
    // The id a new event of `rel` gets under the system (nothing: no system, or the file has no block).
    if (!sysd || sysd->model != u"blocks")
        return std::nullopt;
    std::optional<long long> first;
    for (const auto &[r, f] : sysd->files)                       // dict(files).get(rel): the last one counts
        if (r == rel)
            first = f;
    if (!first)
        return std::nullopt;
    std::set<long long> used;
    for (const auto &kv : eventtools::event_ids(mod, store, after))
        if (py::isdigit(kv.first))
            used.insert(py::to_int(kv.first));
    for (const QString &t : taken)
        if (py::isdigit(t))
            used.insert(py::to_int(t));
    long long n = *first;
    while (used.count(n))
        ++n;
    return QString::number(n);
}

OptStr suggest(const moddata::Mod &mod, scriptstore::ScriptStore &store, const QString &rel)
{
    // The id a new event of `rel` gets: the mod's system's — or nothing when the mod has no system (the old way then).
    std::optional<System> sysd;
    try {
        sysd = read_system(store.text(SYSTEM_FILE));
    } catch (...) {
        sysd = std::nullopt;
    }
    if (!sysd)
        return std::nullopt;
    if (sysd->model == u"blocks" && !rel.isEmpty() &&
        std::none_of(sysd->files.begin(), sysd->files.end(), [&](const auto &kv) { return kv.first == rel; })) {
        std::set<long long> taken;
        for (const auto &kv : sysd->files)
            taken.insert(kv.second);
        long long n = sysd->start;
        while (taken.count(n))
            n += sysd->block;
        sysd->files.emplace_back(rel, n);
    }
    OptStr got = next_id(mod, store, rel, sysd);
    if (!got && (sysd->model == u"running" || sysd->model == u"tidy")) {
        long long top = sysd->start - 1;
        for (const auto &kv : eventtools::event_ids(mod, store))
            if (py::isdigit(kv.first))
                top = std::max(top, py::to_int(kv.first));
        got = QString::number(top + 1);
    }
    return got;
}

QString document(const moddata::Mod &mod, scriptstore::ScriptStore &store, const System &sysd, const Texts *after)
{
    // The system described for people (Markdown): the model, every file's range, how many it holds, the next free id.
    OrderedMap<QString, std::vector<long long>> per;
    for (const auto &[i, lst] : defined(mod, store, true, after))
        for (const auto &p : lst)
            if (py::isdigit(i))
                per[std::get<0>(p)].push_back(py::to_int(i));
    const QString extra = sysd.model == u"blocks"
                              ? u" — every file has a block of %1 ids, the first from %2"_s.arg(sysd.block).arg(sysd.start)
                          : sysd.model == u"running" ? u" from %1"_s.arg(sysd.start)
                                                      : QString();
    QStringList lines = {u"# Event ids of "_s + mod.name(), QString(),
                         u"Kept by ModEditor (Scripts → ID system). Model: **%1**%2."_s
                             .arg(MODELS.value(sysd.model, sysd.model), extra),
                         QString(), u"| File | Ids | Events | Next free |"_s, u"|---|---|---|---|"_s};
    for (const auto &[rel, first] : sysd.files) {
        std::vector<long long> nums = per.value(rel);
        std::sort(nums.begin(), nums.end());
        const long long last = sysd.model == u"blocks" ? first + sysd.block - 1 : (nums.empty() ? first : nums.back());
        OptStr nxt = sysd.model == u"blocks" ? next_id(mod, store, rel, sysd, {}, after)
                                              : (nums.empty() ? OptStr(u"—"_s) : OptStr(QString::number(nums.back() + 1)));
        lines << u"| %1 | %2 – %3 | %4 | %5 |"_s.arg(rel, QString::number(first), QString::number(last), QString::number(nums.size()), nxt && !nxt->isEmpty() ? *nxt : u"—"_s);
    }
    lines << QString()
          << u"Calls of an event (country_event = …, province_event = …, on_actions) follow its id when the editor "
             u"renumbers. The text keys (EVTNAME…) keep their names."_s;
    return lines.join(u'\n') + u'\n';
}

}  // namespace reid
