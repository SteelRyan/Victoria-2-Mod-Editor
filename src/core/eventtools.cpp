// eventtools.py — event ids, free ids, renumbering, new events / decisions. Line by line.
#include "core/eventtools.h"
#include "core/blockcode.h"
#include "core/blocks.h"
#include "core/pynum.h"
#include "core/pyre.h"
#include "core/pytext.h"
#include "core/script.h"
#include "core/scriptstore.h"

#include <QHash>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace eventtools {

using script::Node;

const QStringList EVENT_KEYS = {u"country_event"_s, u"province_event"_s};

bool key_ok(const QString &s) { return py::re_match(u"^[A-Za-z_][A-Za-z0-9_]*$"_s, s).hasMatch(); }

Ids event_ids(const moddata::Mod &mod, scriptstore::ScriptStore &store, const OrderedMap<QString, QString> *after)
{
    // {id: [(rel, position of the id's value, its length)]} of every event defined in events/*.txt, in order.
    // (Python keeps the ids of each file on the store while its text is the same object — a cache only)
    static QHash<const scriptstore::ScriptStore *, QHash<QString, std::pair<QString, std::vector<std::tuple<QString, qsizetype, qsizetype>>>>> caches;
    auto &cache = caches[&store];
    Ids out;
    for (const QString &rel : blockcode::files_of(mod, &store, u"events"_s)) {
        QString text;
        try {
            const QString *a = after ? after->get(rel) : nullptr;
            text = a ? *a : store.text(rel);
        } catch (...) {
            continue;
        }
        auto it = cache.find(rel);
        if (it == cache.end() || it->first != text) {
            std::vector<std::tuple<QString, qsizetype, qsizetype>> ids;
            try {
                const script::Nodes nodes = script::parse(text);
                for (const Node &n : nodes) {
                    if (n.has_key && EVENT_KEYS.contains(n.key) && n.is_block)
                        for (const Node &c : n.kids)
                            if (c.has_key && c.key == u"id" && !c.is_block) {
                                ids.emplace_back(py::strip(c.value), c.vstart, c.vend - c.vstart);
                                break;
                            }
                }
            } catch (...) {
            }
            it = cache.insert(rel, {text, ids});
        }
        for (const auto &[i, pos, ln] : it->second)
            out[i].emplace_back(rel, pos, ln);
    }
    return out;
}

std::pair<OrderedMap<QString, QString>, std::vector<std::tuple<QString, QString, QString>>>
renumber(const moddata::Mod &mod, scriptstore::ScriptStore &store, const std::vector<std::tuple<QString, qsizetype, QString>> &places)
{
    // Give the events at `places` [(rel, pos, id)] free ids. -> {rel: new text}, [(old id, new id, rel)].
    const Ids used = event_ids(mod, store);
    QStringList used_keys;
    for (const auto &kv : used)
        used_keys << kv.first;
    QSet<QString> taken;
    OrderedMap<QString, QString> texts;
    std::vector<std::tuple<QString, QString, QString>> done;
    OrderedMap<QString, std::vector<std::pair<qsizetype, QString>>> by_rel;
    for (const auto &[rel, pos, eid] : places)
        by_rel[rel].emplace_back(pos, eid);
    for (auto &[rel, lst] : by_rel) {
        QString text = store.text(rel);
        QStringList near;
        for (const auto &[i, where] : used)
            if (std::any_of(where.begin(), where.end(), [&](const Place &p) { return std::get<0>(p) == rel; }))
                near << i;
        std::vector<std::pair<qsizetype, QString>> l = lst;
        std::sort(l.begin(), l.end(), std::greater<>());   // from the end: the positions before stay right
        for (const auto &[pos, eid] : l) {
            const auto m = py::re_match(u"\"?([A-Za-z0-9_.\\-]+)\"?"_s, text, pos);
            if (!m.hasMatch() || m.captured(1) != eid)
                continue;
            const QString fresh = free_id(used_keys, near, taken);
            taken.insert(fresh);
            near << fresh;
            text = py::head(text, pos) + fresh + py::tail(text, m.capturedEnd(0));
            done.emplace_back(eid, fresh, rel);
        }
        texts.insert(rel, text);
    }
    return {texts, done};
}

Ids duplicate_ids(const moddata::Mod &mod, scriptstore::ScriptStore &store)
{
    // {id: [(rel, pos, len)]} of the ids more than one event has.
    Ids out;
    for (const auto &[i, lst] : event_ids(mod, store))
        if (lst.size() > 1)
            out.insert(i, lst);
    return out;
}

OrderedMap<QString, std::vector<std::pair<QString, qsizetype>>> decision_keys(const moddata::Mod &mod,
                                                                             scriptstore::ScriptStore &store)
{
    // {key: [(rel, position)]} of every decision.
    OrderedMap<QString, std::vector<std::pair<QString, qsizetype>>> out;
    for (const QString &rel : blockcode::files_of(mod, &store, u"decisions"_s)) {
        script::Nodes nodes;
        try {
            nodes = script::parse(store.text(rel));
        } catch (...) {
            continue;
        }
        for (const Node &top : nodes)
            if (top.has_key && top.key == u"political_decisions" && top.is_block)
                for (const Node &n : top.kids)
                    if (n.has_key && !n.key.isEmpty() && n.is_block)
                        out[n.key].emplace_back(rel, n.kstart);
    }
    return out;
}

QString free_id(const QStringList &used, const QStringList &near, const QSet<QString> &taken)
{
    // An event id nothing uses: the one after the highest of the file (`near`: its ids), else after the highest of all.
    std::vector<long long> nums;
    for (const QString &i : near)
        if (py::isdigit(i))
            nums.push_back(py::to_int(i));
    std::sort(nums.begin(), nums.end());
    long long n;
    if (!nums.empty()) {
        n = nums.back() + 1;
    } else {
        long long top = 99999;
        bool any = false;
        for (const QString &i : used)
            if (py::isdigit(i)) {
                const long long x = py::to_int(i);
                top = any ? std::max(top, x) : x;
                any = true;
            }
        n = top + 1;
    }
    QSet<QString> all = taken;
    for (const QString &i : used)
        all.insert(i);
    while (all.contains(QString::number(n)))
        ++n;
    return QString::number(n);
}

QStringList ids_of_file(const moddata::Mod &mod, scriptstore::ScriptStore &store, const QString &rel)
{
    QStringList out;
    for (const auto &[i, lst] : event_ids(mod, store))
        if (std::any_of(lst.begin(), lst.end(), [&](const Place &p) { return std::get<0>(p) == rel; }))
            out << i;
    return out;
}

QString event_text(const QString &kind, const QString &eid, long long options, bool triggered, const QString &picture)
{
    // A new event with every part there to fill in.
    const QString letters = u"ABCDEFGH"_s;
    QStringList lines = {kind + u" = {"_s, u"\tid = "_s + eid, u"\ttitle = \"EVTNAME%1\""_s.arg(eid),
                         u"\tdesc = \"EVTDESC%1\""_s.arg(eid)};
    if (!picture.isEmpty())
        lines << u"\tpicture = \"%1\""_s.arg(picture);
    lines << QString();
    if (triggered)
        lines << u"\tis_triggered_only = yes"_s << QString();
    else
        lines << u"\ttrigger = {"_s << u"\t}"_s << QString() << u"\tmean_time_to_happen = {"_s << u"\t\tmonths = 12"_s
              << u"\t}"_s << QString();
    lines << u"\timmediate = {"_s << u"\t}"_s;
    for (long long i = 0; i < std::max(1LL, options); ++i)
        lines << QString() << u"\toption = {"_s << u"\t\tname = \"EVTOPT%1%2\""_s.arg(letters[int(i % letters.size())]).arg(eid)
              << u"\t\tai_chance = {"_s << u"\t\t\tfactor = 1"_s << u"\t\t}"_s << u"\t}"_s;
    lines << u"}"_s;
    return lines.join(u'\n');
}

QStringList decision_lines(const QString &picture)
{
    QStringList lines;
    if (!picture.isEmpty())
        lines << u"picture = \"%1\""_s.arg(picture);
    lines << u"potential = {"_s << u"}"_s << u"allow = {"_s << u"}"_s << u"effect = {"_s << u"}"_s << u"ai_will_do = {"_s
          << u"\tfactor = 1"_s << u"}"_s;
    return lines;
}

static QString rstrip_nl(QString s)
{
    while (s.endsWith(u'\n'))
        s.chop(1);
    return s;
}

QString add_event(const QString &text, const QString &block)
{
    // The file's text with the event at its end.
    const QString t = rstrip_nl(text);
    return (t.isEmpty() ? QString() : t + u"\n\n"_s) + block + u'\n';
}

QString add_decision(QString text, const QString &key, const QString &picture)
{
    // The file's text with a new decision inside political_decisions (made when the file has none).
    if (!blocks::find(script::parse(text), {u"political_decisions"_s})) {
        const QString t = rstrip_nl(text);
        text = (t.isEmpty() ? QString() : t + u"\n\n"_s) + u"political_decisions = {\n}\n"_s;
    }
    return blocks::add_block(text, {u"political_decisions"_s}, key, decision_lines(picture));
}

static QString splitext_stem(const QString &p)   // os.path.splitext(p)[0] (ntpath: "\\" and "/" both separate)
{
    const qsizetype sep = std::max(p.lastIndexOf(u'/'), p.lastIndexOf(u'\\'));
    const qsizetype dot = p.lastIndexOf(u'.');
    if (dot > sep) {
        for (qsizetype i = sep + 1; i < dot; ++i)
            if (p[i] != u'.')
                return p.left(dot);
    }
    return p;
}

OptStr safe_file(const QString &name)
{
    const QString stem = py::strip(py::re_sub(u"[^A-Za-z0-9_ \\-]+"_s, splitext_stem(py::strip(name)),
                                              [](const QRegularExpressionMatch &) { return QString(); }));
    return stem.isEmpty() ? std::nullopt : OptStr(stem + u".txt"_s);
}

}  // namespace eventtools
