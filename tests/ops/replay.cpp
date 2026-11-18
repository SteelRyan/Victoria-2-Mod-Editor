// The C++ side of a scripted session (tests/ops/<name>.json): the same operations as tools/oracle.py, through the
// C++ Document, and the same log. Usage: med_replay <ops.json> <mod copy> <backups folder> <log file>
#include "../core/nopopup.h"
#include "core/adjacency.h"
#include "core/countryhist.h"
#include "core/culturefile.h"
#include "core/diplomacy.h"
#include "core/oob.h"
#include "core/events.h"
#include "core/modifiers.h"
#include "core/production.h"
#include "core/techdata.h"
#include "core/document.h"
#include "core/fs.h"
#include "core/localisation.h"
#include "core/scriptstore.h"
#include "core/pypath.h"
#include "core/pynum.h"
#include "core/recovery.h"
#include "core/snapio_types.h"
#include "../core/pyform.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace document;
using pyform::L;
using pyform::v;

static QList<long long> ints(const QJsonValue &a)
{
    QList<long long> out;
    for (const QJsonValue &x : a.toArray())
        out << x.toInteger();
    return out;
}
static QStringList strs(const QJsonValue &a)
{
    QStringList out;
    for (const QJsonValue &x : a.toArray())
        out << x.toString();
    return out;
}
static OptStr ostr(const QJsonValue &x) { return x.isNull() || x.isUndefined() ? OptStr() : OptStr(x.toString()); }

static PopFilter make_filter(const QJsonValue &fv)
{
    const QJsonObject f = fv.toObject();
    PopFilter flt;
    if (f.contains(u"types"_s) && !f[u"types"_s].isNull()) {
        QSet<QString> s;
        for (const QString &x : strs(f[u"types"_s]))
            s.insert(x);
        flt.types = s;
    }
    if (f.contains(u"cultures"_s) && !f[u"cultures"_s].isNull()) {
        QSet<OptStr> s;
        for (const QString &x : strs(f[u"cultures"_s]))
            s.insert(x);
        flt.cultures = s;
    }
    for (const QString &x : strs(f[u"no_types"_s]))
        flt.no_types.insert(x);
    for (const QString &x : strs(f[u"no_cultures"_s]))
        flt.no_cultures.insert(x);
    for (const QString &x : strs(f[u"no_religions"_s]))
        flt.no_religions.insert(x);
    return flt;
}

static std::vector<Pop> make_pops(const QJsonValue &a)
{
    std::vector<Pop> out;
    for (const QJsonValue &x : a.toArray()) {
        const QJsonObject t = x.toObject();
        Pop p;
        p.type = t[u"type"_s].toString();
        p.culture = ostr(t[u"culture"_s]);
        p.religion = ostr(t[u"religion"_s]);
        p.size = t[u"size"_s].toInteger();
        out.push_back(p);
    }
    return out;
}

static QString run_op(Document &d, const QJsonObject &op, const QString &backups)
{
    const QString o = op[u"op"_s].toString();
    if (o == u"scale") {
        std::optional<double> percent;
        std::optional<long long> nt;
        if (op.contains(u"percent"_s))
            percent = op[u"percent"_s].toDouble();
        if (op.contains(u"new_total"_s))
            nt = op[u"new_total"_s].toInteger();
        return v(d.scale(ints(op[u"pids"_s]), make_filter(op[u"filter"_s]), percent, nt));
    }
    if (o == u"add_pop")
        return v(d.add_pop(ints(op[u"pids"_s]), op[u"type"_s].toString(), ostr(op[u"culture"_s]), ostr(op[u"religion"_s]),
                           op[u"size"_s].toInteger()));
    if (o == u"paste") {
        std::optional<long long> total;
        if (op.contains(u"total"_s) && !op[u"total"_s].isNull())
            total = op[u"total"_s].toInteger();
        return v(d.paste(ints(op[u"pids"_s]), make_pops(op[u"template"_s]), op[u"replace"_s].toBool(false),
                         op.contains(u"factor"_s) ? op[u"factor"_s].toDouble() : 1.0, total));
    }
    if (o == u"remix_shift") {                  // targets = what the filtered pops have, `people` moved from -> to
        const PopFilter flt = make_filter(op[u"filter"_s]);
        const QString field = op[u"field"_s].toString();
        OrderedMap<OptStr, long long> cur;
        for (long long pid : ints(op[u"pids"_s]))
            for (const Pop &p : d.province_pops(pid))
                if (flt.match(p))
                    cur[field == u"culture" ? p.culture : p.religion] += p.size;
        OrderedMap<OptStr, long long> targets = cur;
        targets[op[u"from"_s].toString()] -= op[u"people"_s].toInteger();
        targets[op[u"to"_s].toString()] += op[u"people"_s].toInteger();
        return v(d.remix(ints(op[u"pids"_s]), flt, field, targets));
    }
    if (o == u"split") {
        std::vector<std::pair<long long, double>> t;
        for (const QJsonValue &x : op[u"targets"_s].toArray())
            t.emplace_back(x.toArray()[0].toInteger(), x.toArray()[1].toDouble());
        return v(d.split(op[u"source"_s].toInteger(), t, make_filter(op[u"filter"_s]), op[u"keep"_s].toInteger(0)));
    }
    if (o == u"lower_to_cap") {
        std::vector<std::tuple<QString, QList<long long>, double>> todo;
        for (const QJsonValue &x : op[u"todo"_s].toArray())
            todo.emplace_back(x.toArray()[0].toString(), ints(x.toArray()[1]), x.toArray()[2].toDouble());
        return v(d.lower_to_cap(todo));
    }
    if (o == u"consolidate") {
        QSet<QString> types;
        for (const QString &t : strs(op[u"types"_s]))
            types.insert(t);
        std::optional<long long> cap;
        if (op.contains(u"capital"_s) && !op[u"capital"_s].isNull())
            cap = op[u"capital"_s].toInteger();
        return v(d.consolidate(op[u"rid"_s].toString(), types, cap, op[u"same_owner"_s].toBool(false)));
    }
    if (o == u"move_to_state") {
        auto [ok, msg] = d.move_to_state(ints(op[u"pids"_s]), op[u"target"_s].toString(), op[u"create"_s].toBool(false));
        return L(ok, msg);
    }
    if (o == u"set_capital") {
        d.set_capital(op[u"rid"_s].toString(), op[u"pid"_s].toInteger());
        return u"None"_s;
    }
    if (o == u"delete_state") {
        auto [ok, msg] = d.delete_state(op[u"rid"_s].toString(), ostr(op[u"target"_s]));
        return L(ok, msg);
    }
    if (o == u"drop_empty_states")
        return v(d.drop_empty_states(strs(op[u"rids"_s])));
    if (o == u"history") {                      // edit_provinces: set fields, add / remove cores
        d.edit_provinces(op[u"label"_s].toString(u"History"_s), ints(op[u"pids"_s]), [&](long long, ProvState &st) {
            if (op.contains(u"owner"_s)) st.owner = ostr(op[u"owner"_s]);
            if (op.contains(u"controller"_s)) st.controller = ostr(op[u"controller"_s]);
            if (op.contains(u"trade_goods"_s)) st.trade_goods = ostr(op[u"trade_goods"_s]);
            if (op.contains(u"life_rating"_s))
                st.life_rating = op[u"life_rating"_s].isNull() ? std::nullopt : std::optional<long long>(op[u"life_rating"_s].toInteger());
            for (const QString k : {u"colonial"_s, u"fort"_s, u"naval_base"_s, u"railroad"_s})
                if (op.contains(k))
                    st.set_level(k, op[k].toInteger());
            for (const QString &c : strs(op[u"add_cores"_s]))
                if (!st.cores.contains(c))
                    st.cores << c;
            for (const QString &c : strs(op[u"remove_cores"_s]))
                st.cores.removeOne(c);
        });
        return u"None"_s;
    }
    if (o == u"remove_first_factory") {
        const long long pid = op[u"pid"_s].toInteger();
        const history::Factory f = d.prov_state(pid).factories[0];
        d.remove_factories({FactoryProblem{d.mod.provinces.get(pid)->region.value_or(QString()), pid, f, u"test"_s}});
        return u"None"_s;
    }
    if (o == u"set_membership") {
        d.set_membership(op[u"kind"_s].toString(), ints(op[u"pids"_s]), op[u"name"_s].toString());
        return u"None"_s;
    }
    if (o == u"set_definitions") {
        OrderedMap<QString, std::vector<std::pair<QString, QString>>> mods;
        const QJsonObject mo = op[u"mods"_s].toObject();
        for (auto it = mo.begin(); it != mo.end(); ++it) {
            std::vector<std::pair<QString, QString>> lst;
            for (const QJsonValue &x : it.value().toArray())
                lst.emplace_back(x.toArray()[0].toString(), x.toArray()[1].toString());
            mods.insert(it.key(), lst);
        }
        OrderedMap<QString, QString> moves;
        const QJsonObject mv = op[u"moves"_s].toObject();
        for (auto it = mv.begin(); it != mv.end(); ++it)
            moves.insert(it.key(), it.value().toString());
        d.set_definitions(op[u"kind"_s].toString(), mods, strs(op[u"new_names"_s]), strs(op[u"removed"_s]), moves);
        return u"None"_s;
    }
    if (o == u"adj_add") {
        std::vector<adjacency::RowFields> rows;
        for (const QJsonValue &x : op[u"rows"_s].toArray()) {
            const QJsonArray r = x.toArray();
            rows.push_back({r[0].toInteger(), r[1].toInteger(), r[2].toString(), r[3].toInteger(), r[4].toInteger(),
                            r[5].toString()});
        }
        auto &adj = d.map<adjacency::AdjacencyFile>(u"adjacency"_s);
        return v(d.edit(u"Add rows"_s, {}, [&] { return adj.add(rows); }, {}, {}, {u"adjacency"_s}));
    }
    if (o == u"loc_set") {                      // a name changed (the pages: one step, lists=["loc"])
        auto &loc = d.map<localisation::LocEdits>(u"loc"_s);
        d.edit(u"Name %1"_s.arg(op[u"key"_s].toString()), {}, [&] {
            loc.set(op[u"key"_s].toString(), op[u"text"_s].toString(), ostr(op[u"section"_s]));
        }, {}, {}, {u"loc"_s});
        return u"None"_s;
    }
    if (o == u"loc_drop") {
        auto &loc = d.map<localisation::LocEdits>(u"loc"_s);
        d.edit(u"Name gone"_s, {}, [&] { loc.drop(op[u"key"_s].toString()); }, {}, {}, {u"loc"_s});
        return u"None"_s;
    }
    if (o == u"script_set" || o == u"script_replace") {   // a script change through an undo step
        auto &store = d.map<scriptstore::ScriptStore>(u"scripts"_s);
        const QString rel = op[u"rel"_s].toString();
        QString text;
        if (o == u"script_set") {
            text = op[u"text"_s].toString();
        } else {
            text = store.text(rel);
            const qsizetype at = text.indexOf(op[u"old"_s].toString());
            if (at >= 0)
                text.replace(at, op[u"old"_s].toString().size(), op[u"new"_s].toString());
        }
        d.edit(u"Edit %1"_s.arg(rel), {}, [&] { store.set_text(rel, text); }, {}, {}, {u"scripts"_s});
        return u"None"_s;
    }
    if (o == u"script_type") {                   // typing: the text changes, then the document records it
        auto &store = d.map<scriptstore::ScriptStore>(u"scripts"_s);
        const QString rel = op[u"rel"_s].toString();
        QString text = store.text(rel);
        const qsizetype at = text.indexOf(op[u"old"_s].toString());
        if (at >= 0)
            text.replace(at, op[u"old"_s].toString().size(), op[u"new"_s].toString());
        store.set_text(rel, text);
        return v(d.record_live(u"scripts"_s, rel, u"Typed in %1"_s.arg(rel)));
    }
    if (o == u"script_remove") {
        auto &store = d.map<scriptstore::ScriptStore>(u"scripts"_s);
        const QString rel = op[u"rel"_s].toString();
        d.edit(u"Remove %1"_s.arg(rel), {}, [&] { store.remove(rel); }, {}, {}, {u"scripts"_s});
        return u"None"_s;
    }
    if (o == u"cultures_apply" || o == u"religions_apply") {
        std::vector<std::pair<QString, OptStr>> groups;
        for (const QJsonValue &g : op[u"new_groups"_s].toArray())
            groups.emplace_back(g.toArray()[0].toString(), ostr(g.toArray()[1]));
        std::vector<std::pair<QString, QString>> moves;
        for (const QJsonValue &mv : op[u"moves"_s].toArray())
            moves.emplace_back(mv.toArray()[0].toString(), mv.toArray()[1].toString());
        auto rgb = [](const QJsonValue &c) {
            const QJsonArray a = c.toArray();
            return std::make_tuple(a[0].toInt(), a[1].toInt(), a[2].toInt());
        };
        QString r;
        if (o == u"cultures_apply") {
            std::vector<culturefile::NewCulture> nc;
            for (const QJsonValue &x : op[u"new_cultures"_s].toArray()) {
                const QJsonArray a = x.toArray();
                nc.push_back({a[0].toString(), a[1].toString(), rgb(a[2]), ostr(a[3]), ostr(a[4])});
            }
            r = culturefile::apply_changes(d.mod, groups, nc, moves, backups);
        } else {
            std::vector<religionfile::NewReligion> nr;
            for (const QJsonValue &x : op[u"new_religions"_s].toArray()) {
                const QJsonArray a = x.toArray();
                nr.push_back({a[0].toString(), a[1].toString(), rgb(a[2]), ostr(a[3]),
                              a[4].isNull() ? std::nullopt : std::optional<long long>(a[4].toInteger())});
            }
            r = religionfile::apply_changes(d.mod, groups, nr, moves, backups);
        }
        return v(r == u"written on Apply" ? r : u"backup"_s);
    }
    if (o == u"nation_edit") {                  // the Nation page: one step, lists=["nations"] (pairs keep their order)
        auto &nations = d.map<countryhist::NationEdits>(u"nations"_s);
        const QString tag = op[u"tag"_s].toString();
        auto nfn = [&](countryhist::NationState &st) {
            for (const QJsonValue &kv : op[u"set"_s].toArray())
                st[kv.toArray()[0].toString()] = kv.toArray()[1].toString();
            for (const QString &t : strs(op[u"add_techs"_s]))
                st.techs.insert(t);
            for (const QString &t : strs(op[u"remove_techs"_s]))
                st.techs.remove(t);
            for (const QString &t : strs(op[u"add_inventions"_s]))
                st.inventions.insert(t);
            for (const QString &t : strs(op[u"remove_inventions"_s]))
                st.inventions.remove(t);
            for (const QString &c : strs(op[u"add_cultures"_s]))
                if (!st.cultures.contains(c))
                    st.cultures << c;
            for (const QString &c : strs(op[u"remove_cultures"_s]))
                st.cultures.removeOne(c);
            for (const QJsonValue &kv : op[u"reforms"_s].toArray())
                st.reforms.insert(kv.toArray()[0].toString(), kv.toArray()[1].toString());
            if (op.contains(u"upper_house"_s)) {
                OrderedMap<QString, QString> uh;
                for (const QJsonValue &kv : op[u"upper_house"_s].toArray())
                    uh.insert(kv.toArray()[0].toString(), kv.toArray()[1].toString());
                st.upper_house = uh;
            }
        };
        d.edit(op[u"label"_s].toString(u"Nation "_s + tag), {}, [&] { nations.edit(tag, nfn); }, {}, {}, {u"nations"_s});
        return u"None"_s;
    }
    if (o == u"new_nation") {
        auto &nn = d.map<countryhist::NewNations>(u"new_nations"_s);
        const QJsonArray c = op[u"color"_s].toArray();
        d.edit(u"New nation %1 %2"_s.arg(op[u"tag"_s].toString(), op[u"name"_s].toString()), {}, [&] {
            nn.add(op[u"tag"_s].toString(), op[u"name"_s].toString(), op[u"adj"_s].toString(),
                   {c[0].toInt(), c[1].toInt(), c[2].toInt()}, op[u"template"_s].toString());
        }, {}, {}, {u"new_nations"_s});
        return u"None"_s;
    }
    if (o == u"oob_edit") {                     // mainwindow.on_oob_edit: one step, lists=["oob"]
        auto &oe = d.map<oob::OOBEdits>(u"oob"_s);
        const QString tag = op[u"tag"_s].toString(), w = op[u"what"_s].toString();
        d.edit(op[u"label"_s].toString(u"Army of "_s + tag), {}, [&] {
            oe.edit(tag, [&](oob::OOB &s) {
                if (w == u"add_army") {
                    oob::Army a;
                    a.name = op[u"name"_s].toString();
                    if (op.contains(u"location"_s) && !op[u"location"_s].isNull())
                        a.location = op[u"location"_s].toInteger();
                    for (const QJsonValue &x : op[u"regiments"_s].toArray()) {
                        const QJsonArray r = x.toArray();
                        a.regiments.push_back({r[0].toString(), r[1].toString(),
                                               r[2].isNull() ? oob::OptInt() : oob::OptInt(r[2].toInteger())});
                    }
                    s.armies.push_back(a);
                } else if (w == u"drop_regiment") {
                    auto &regs = s.armies[size_t(op[u"army"_s].toInteger())].regiments;
                    regs.erase(regs.begin() + op[u"reg"_s].toInteger());
                } else if (w == u"rename_army") {
                    s.armies[size_t(op[u"army"_s].toInteger())].name = op[u"name"_s].toString();
                } else if (w == u"set_relation") {
                    const QString other = op[u"other"_s].toString();
                    if (!s.relations.contains(other))
                        s.relations.insert(other, oob::Relation{});
                    oob::Relation &r = *s.relations.get(other);
                    if (op.contains(u"value"_s)) r.value = ostr(op[u"value"_s]);
                    if (op.contains(u"level"_s)) r.level = ostr(op[u"level"_s]);
                    if (op.contains(u"influence_value"_s)) r.influence_value = ostr(op[u"influence_value"_s]);
                } else if (w == u"drop_relation") {
                    s.relations.remove(op[u"other"_s].toString());
                }
            });
        }, {}, {}, {u"oob"_s});
        const oob::OOB &st = oe.state(tag);
        std::vector<long long> regs;
        for (const oob::Army &a : st.armies)
            regs.push_back(static_cast<long long>(a.regiments.size()));
        QStringList rel;
        for (const auto &kv : st.relations)
            rel << kv.first;
        std::sort(rel.begin(), rel.end());
        return pyform::list({v(static_cast<long long>(st.armies.size())), v(regs), v(rel), v(oe.dirty())});
    }
    if (o == u"treaty" || o == u"subject") {    // mainwindow.on_diplo_edit: lists=["oob", "diplomacy"]
        auto &dip = d.map<diplomacy::DiploEdits>(u"diplomacy"_s);
        const QString a = op[u"a"_s].toString(), b = op[u"b"_s].toString();
        if (o == u"treaty") {
            d.edit(u"Treaty"_s, {}, [&] { dip.set_treaty(a, b, op[u"kind"_s].toString(), op[u"on"_s].toBool()); }, {}, {},
                   {u"oob"_s, u"diplomacy"_s});
        } else {
            std::optional<std::pair<QString, QString>> k;
            if (op[u"kind"_s].isArray())
                k = std::make_pair(op[u"kind"_s].toArray()[0].toString(), op[u"kind"_s].toArray()[1].toString());
            d.edit(u"Subject"_s, {}, [&] { dip.set_subject(a, b, k); }, {}, {}, {u"oob"_s, u"diplomacy"_s});
        }
        QStringList p;
        for (const diplomacy::Record &r : dip.between(a, b))
            p << L(r.kind, r.first, r.second);
        p << v(dip.dirty());
        return pyform::list(p);
    }
    if (o == u"nation_problem")
        return v(d.map<countryhist::NewNations>(u"new_nations"_s).problem(op[u"tag"_s].toString(), op[u"name"_s].toString()));
    if (o == u"owners_top") {
        auto &top = d.map<history::OwnersOnTop>(u"owners_top"_s);
        d.edit(u"Owners at the top"_s, {}, [&] { top.add(ints(op[u"pids"_s])); }, {}, {}, {u"owners_top"_s});
        return u"None"_s;
    }
    if (o == u"start_blocks") {                  // the Errors tab's fix: everything start_blocks() finds
        auto &sb = d.map<history::StartBlocks>(u"start_blocks"_s);
        const auto found = history::start_blocks(d, d.mod);
        d.edit(u"Fold start blocks"_s, {}, [&] {
            sb.add(QList<long long>(found.first.begin(), found.first.end()), found.second);
        }, {}, {}, {u"start_blocks"_s});
        return L(found.first, found.second);
    }
    if (o == u"event_db") {                     // the Events page reads everything again (from the editor's texts)
        auto &cats = d.map<events::EventCats>(u"event_cats"_s);
        auto &store = d.map<scriptstore::ScriptStore>(u"scripts"_s);
        static std::shared_ptr<events::EventDB> db;   // (Python: cats.db; the new one reads with it as prev)
        db = std::make_shared<events::EventDB>(d.mod, &store, db.get());
        cats.attach(db.get());
        QStringList withc, filec, lit;
        for (const events::EntryP &e : db->entries)
            if (!e->cats.isEmpty())
                withc << L(e->uid, e->cats);
        for (const auto &[r, c] : db->file_cats)
            if (!c.isEmpty())
                filec << L(r, c);
        std::vector<std::pair<QString, QString>> lm;
        for (const auto &x : events::lit_marked(db->files, [&](const QString &rel) { return store.text(rel); }))
            lm.push_back(x);
        std::sort(lm.begin(), lm.end());
        for (const auto &x : lm)
            lit << L(x.first, x.second);
        return pyform::list({v(static_cast<long long>(db->entries.size())), v(db->files),
                             v(static_cast<long long>(db->by_uid.size())), v(cats.all_names()), v(db->errors),
                             pyform::list(withc), pyform::list(filec), pyform::list(lit)});
    }
    if (o == u"event_cats") {                   // mainwindow.on_event_cats: one step, lists=["scripts"]
        auto &cats = d.map<events::EventCats>(u"event_cats"_s);
        const events::EventCats::CatKey key{op[u"rel"_s].toString(), ostr(op[u"uid"_s])};
        d.edit(op[u"label"_s].toString(u"Categories"_s), {}, [&] { cats.set(key, strs(op[u"names"_s])); }, {}, {},
               {u"scripts"_s});
        return v(cats.get(key));
    }
    if (o == u"lit_line") {                     // mainwindow.on_event_literacy
        auto &store = d.map<scriptstore::ScriptStore>(u"scripts"_s);
        const QString rel = op[u"rel"_s].toString();
        const bool on = op[u"on"_s].toBool();
        const QString old = store.text(rel);
        const QString fresh = events::set_lit_line(old, rel.split(u'/')[0], op[u"uid"_s].toString(), on);
        if (fresh == old)
            return v(false);
        d.edit(u"%1: %2 the line %3"_s.arg(rel.split(u'/').last(), on ? u"＋"_s : u"−"_s, events::LIT_LINE), {},
               [&] { store.set_text(rel, fresh); }, {}, {}, {u"scripts"_s});
        return v(true);
    }
    if (o == u"mod_edit") {                     // mainwindow.on_event_mod: one step, lists=["scripts"]
        auto &mods = d.map<modifiers::ModifierEdits>(u"event_mods"_s);
        const QString name = op[u"name"_s].toString(), what = op[u"what"_s].toString();
        d.edit(op[u"label"_s].toString(u"Modifier "_s + name), {}, [&] {
            if (what == u"set_attr")
                mods.set(name, modifiers::set_attr(mods.text(name), op[u"key"_s].toString(), op[u"value"_s].toString()));
            else if (what == u"del_attr")
                mods.set(name, modifiers::del_attr(mods.text(name), op[u"key"_s].toString()));
            else if (what == u"add")
                mods.add(name);
            else if (what == u"remove")
                mods.remove(name);
        }, {}, {}, {u"scripts"_s});
        const QStringList names = mods.names();
        QStringList ends = names.mid(0, 3);
        ends += names.mid(std::max<qsizetype>(0, names.size() - 3));
        QString attrs = u"None"_s;
        if (mods.exists(name)) {
            QStringList al;
            for (const auto &a : mods.attrs(name))
                al << L(a.first, a.second);
            attrs = pyform::list(al);
        }
        return pyform::list({v(ends), v(static_cast<long long>(names.size())), v(mods.exists(name)), attrs});
    }
    if (o == u"tech_set" || o == u"tech_move") {   // mainwindow.on_tech_edit / on_tech_order
        auto &techs = d.map<techdata::TechEdits>(u"techs"_s);
        const QString key = op.contains(u"key"_s) ? op[u"key"_s].toString() : techs.keys()[op[u"index"_s].toInt()];
        if (o == u"tech_set") {
            std::vector<qsizetype> path;
            for (const QJsonValue &x : op[u"path"_s].toArray())
                path.push_back(x.toInteger());
            const QString text = techdata::set_value(techs.text(key), path, op[u"value"_s].toString());
            d.edit(u"Tech "_s + key, {}, [&] { techs.set(key, text); }, {}, {}, {u"techs"_s});
            return pyform::list({v(key), v(techs.dirty())});
        }
        const techdata::OptStr area = techs.area_of(key);
        d.edit(key + u": later"_s, {}, [&] { techs.move(area, key, op[u"step"_s].toInteger()); }, {}, {}, {u"techs"_s});
        return pyform::list({v(key), v(techs.column(area).mid(0, 6)), v(techs.dirty())});
    }
    if (o == u"prod_other") {                   // mainwindow.on_prod_other: a price / a building
        auto &prod = d.map<production::ProdEdits>(u"production"_s);
        QString key;
        if (op.contains(u"key"_s))
            key = op[u"key"_s].toString();
        else {
            qsizetype i = 0;
            for (const auto &kv : prod.db.costs)
                if (i++ == op[u"index"_s].toInt()) {
                    key = kv.first;
                    break;
                }
        }
        d.edit(u"Price"_s, {}, [&] { prod.set_other(op[u"what"_s].toString(), key, op[u"value"_s].toString()); }, {}, {},
               {u"production"_s});
        return pyform::list({v(key), v(prod.dirty())});
    }
    if (o == u"undo")
        return v(d.undo());
    if (o == u"redo")
        return v(d.redo());
    if (o == u"save") {
        auto [written, bdir] = d.save(backups);
        QStringList w;
        for (const QString &p : written)
            w << (pypath::isabs(p) ? pypath::relpath(p, d.mod.root) : p);
        std::sort(w.begin(), w.end());
        QStringList fails;
        for (const auto &[k, e] : d.apply_failures)
            fails << L(k, e);
        return pyform::list({v(w), v(bdir.has_value()), pyform::list(fails), v(d.undo_lost.second), v(d.undo_lost.first)});
    }
    throw py::ValueError(("unknown op " + o).toStdString());
}

static QStringList state_lines(Document &d, QList<long long> pids, QStringList rids, QStringList keys = {},
                               const QStringList &tags = {})
{
    QStringList out;
    QStringList dp;
    out << u"  pending %1 dirty_provinces %2 dirty_regions %3 dirty_histories %4 undo %5 redo %6"_s
               .arg(d.pending_count()).arg(v(d.dirty_provinces()), v(d.dirty_regions()), v(d.dirty_histories()))
               .arg(d.undo_stack.size()).arg(d.redo_stack.size());
    if (!d.undo_stack.empty()) {
        const auto [n, desc] = d.step_scope(*d.undo_stack.back());
        out << u"  last %1 scope %2 %3"_s.arg(v(d.undo_stack.back()->label)).arg(n).arg(v(desc));
    }
    QSet<long long> ps(pids.begin(), pids.end());
    pids = QList<long long>(ps.begin(), ps.end());
    std::sort(pids.begin(), pids.end());
    for (long long pid : pids) {
        QStringList pl;
        for (const Pop &p : d.province_pops(pid)) {
            QStringList ex;
            for (const auto &[k, t] : p.extra)
                ex << L(k, t);
            pl << pyform::list({v(p.type), v(p.culture), v(p.religion), v(p.size), pyform::list(ex), v(p.o != nullptr)});
        }
        out << u"  pops %1 %2"_s.arg(pid).arg(pyform::list(pl));
        const ProvState &st = d.prov_state(pid);
        out << u"  hist %1 %2"_s.arg(pid).arg(L(st.owner, st.controller, st.cores, st.life_rating, st.trade_goods,
                                                 st.fort, static_cast<long long>(st.factories.size())));
    }
    QSet<QString> rs(rids.begin(), rids.end());
    rids = QStringList(rs.begin(), rs.end());
    std::sort(rids.begin(), rids.end());
    for (const QString &rid : rids) {
        const moddata::Region *r = d.mod.any_region(rid);
        out << u"  region %1 %2"_s.arg(rid, r ? v(r->provinces) : u"None"_s);
    }
    if (d.maps.contains(u"scripts"_s)) {
        auto &store = d.map<scriptstore::ScriptStore>(u"scripts"_s);
        QStringList g(store.gone.begin(), store.gone.end());
        std::sort(g.begin(), g.end());
        out << u"  scripts dirty %1 gone %2"_s.arg(v(store.dirty()), v(g));
        QStringList rels;
        for (const auto &kv : store.docs)
            rels << kv.first;
        std::sort(rels.begin(), rels.end());
        for (const QString &rel : rels)
            out << u"  text %1 %2 same %3"_s.arg(v(rel), QString::fromLatin1(QCryptographicHash::hash(
                                                           store.text(rel).toUtf8(), QCryptographicHash::Sha1).toHex()),
                                                 v(store.same(rel)));
    }
    if (d.maps.contains(u"loc"_s)) {
        auto &loc = d.map<localisation::LocEdits>(u"loc"_s);
        QStringList wh;
        for (const QString &k : loc.dirty())
            wh << L(k, loc.where(k));
        out << u"  loc dirty %1 where %2"_s.arg(v(loc.dirty()), pyform::list(wh));
        QSet<QString> ks(keys.begin(), keys.end());
        keys = QStringList(ks.begin(), ks.end());
        std::sort(keys.begin(), keys.end());
        for (const QString &k : keys)
            out << u"  name %1 %2"_s.arg(v(k), v(loc.text(k)));
    }
    if (d.maps.contains(u"nations"_s)) {
        auto &ne = d.map<countryhist::NationEdits>(u"nations"_s);
        auto &nn = d.map<countryhist::NewNations>(u"new_nations"_s);
        out << u"  nations dirty %1 new %2 top %3 blocks %4"_s.arg(
                   v(ne.dirty()), v(nn.dirty()), v(d.map<history::OwnersOnTop>(u"owners_top"_s).dirty()),
                   v(d.map<history::StartBlocks>(u"start_blocks"_s).dirty()));
        for (const QString &t : tags) {
            const countryhist::NationKey k = countryhist::key_of(ne.state(t));
            out << u"  nation %1 %2 in_countries %3"_s.arg(
                       t, L(k.scalars, k.cultures, k.reforms, k.techs, k.inventions, k.upper_house),
                       v(d.mod.countries.contains(t)));
        }
    }
    return out;
}

// The recovery file's form (core/snapio): every store's picture, every undo / redo step and the recovery picture of
// the session, written and read back, must come back equal (a field the codec leaves out shows here). Not in the log:
// the Python side has no such thing (it pickles).
static bool same_value(const store::Snap &a, const store::Snap &b) { return store::same(a, b); }
static bool same_value(const Picture &a, const Picture &b) { return a == b; }
static bool same_value(const recovery::Data &a, const recovery::Data &b)
{
    if (!(a.picture.p == b.picture.p) || a.picture.special.size() != b.picture.special.size() || a.mod != b.mod ||
        a.scenario != b.scenario || a.count != b.count)
        return false;
    for (const auto &[k, s] : a.picture.special)
        if (const store::Snap *o = b.picture.special.get(k); !o || !store::same(s, *o))
            return false;
    return true;
}
static QStringList snapio_roundtrip(Document &d)
{
    snapio::register_core();
    QStringList bad;
    auto round = [&](const QString &what, const auto &x) {
        using T = std::decay_t<decltype(x)>;
        try {
            QByteArray b;
            {
                QDataStream s(&b, QIODevice::WriteOnly);
                snapio::Writer w(s);
                snapio::put(w, x);
            }
            QDataStream s(b);
            snapio::Reader r(s);
            const T back = snapio::get<T>(r);
            if (!s.atEnd() || !same_value(x, back))
                bad << what;
        } catch (const std::exception &e) {
            bad << what + u": "_s + QString::fromUtf8(e.what());
        }
    };
    for (const auto &[k, mf] : d.maps)
        round(u"store "_s + k, mf->snapshot());
    for (const auto *stack : {&d.undo_stack, &d.redo_stack})
        for (const StepP &st : *stack) {
            round(u"step %1 (before)"_s.arg(st->label), st->before);
            round(u"step %1 (after)"_s.arg(st->label), st->after);
        }
    if (auto pic = recovery::picture(d)) {
        recovery::Data data;
        data.version = recovery::VERSION;
        data.mod = d.mod.root;
        data.scenario = d.scenario;
        data.count = d.pending_count();
        data.picture = *pic;
        try {
            const auto back = recovery::unpack(recovery::pack(data));
            if (!back || !same_value(data, *back))
                bad << u"the recovery file"_s;
        } catch (const std::exception &e) {
            bad << u"the recovery file: "_s + QString::fromUtf8(e.what());
        }
    }
    return bad;
}

static QString err_name(const std::exception &e)
{
    if (dynamic_cast<const py::ValueError *>(&e))
        return u"ValueError"_s;
    if (dynamic_cast<const fs::OSError *>(&e))
        return u"OSError"_s;
    if (dynamic_cast<const std::out_of_range *>(&e))
        return u"KeyError"_s;
    return u"RuntimeError"_s;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);             // (texts are QTextDocuments: an invisible Gui application)
    const QStringList a = app.arguments();
    if (a.size() < 5)
        return 2;
    QFile jf(a[1]);
    if (!jf.open(QIODevice::ReadOnly))
        return 3;
    const QJsonObject session = QJsonDocument::fromJson(jf.readAll()).object();
    const QString base = pypath::join(pypath::dirname(pypath::dirname(pypath::abspath(QCoreApplication::applicationDirPath()))),
                                      u"Victoria 2"_s);
    try {
        QStringList replace;
        if (session[u"mod"_s].toString() == u"ferrum_mare")
            replace = {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s};
        moddata::Mod m(a[2], a.size() > 5 ? a[5] : base, std::nullopt, replace);
        m.load();
        QString sc = session[u"scenario"_s].toString();
        if (sc.isEmpty())
            for (const QString &s : m.pop_scenarios())
                if (script::parse_date(s)) {
                    sc = s;
                    break;
                }
        Document d(m, sc);
        for (const QJsonValue &st : session[u"stores"_s].toArray())
            if (st.toString() == u"loc")
                d.maps.insert(u"loc"_s, std::make_shared<localisation::LocEdits>(m));
            else if (st.toString() == u"scripts") {
                d.maps.insert(u"scripts"_s, std::make_shared<scriptstore::ScriptStore>(m));
                d.track_live(u"scripts"_s);
            }
        if (session[u"stores"_s].toArray().contains(u"events"_s)) {   // (after the scripts: their texts)
            auto &st = d.map<scriptstore::ScriptStore>(u"scripts"_s);
            d.maps.insert(u"event_cats"_s, std::make_shared<events::EventCats>(m, st));
            d.maps.insert(u"event_mods"_s, std::make_shared<modifiers::ModifierEdits>(m, st));
        }
        std::unique_ptr<techdata::TechDB> tdb;
        std::unique_ptr<production::ProdDB> pdb;
        if (session[u"stores"_s].toArray().contains(u"techprod"_s)) {  // technologies and production
            tdb = std::make_unique<techdata::TechDB>(m);
            pdb = std::make_unique<production::ProdDB>(m);
            d.maps.insert(u"techs"_s, std::make_shared<techdata::TechEdits>(m, *tdb));
            d.maps.insert(u"production"_s, std::make_shared<production::ProdEdits>(m, *pdb));
        }
        if (session[u"stores"_s].toArray().contains(u"nations"_s)) {   // as the window makes them, in its order
            m.nation_data = std::make_shared<countryhist::NationData>(m);
            m.country_hist = std::make_shared<countryhist::CountryHistory>(m, *m.nation_data);
            auto nn = std::make_shared<countryhist::NewNations>(m, *m.country_hist);
            auto ne = std::make_shared<countryhist::NationEdits>(*m.country_hist, d.date);
            nn->nations = ne.get();
            d.maps.insert(u"new_nations"_s, nn);
            d.maps.insert(u"nations"_s, ne);
            if (session[u"stores"_s].toArray().contains(u"oob"_s)) {
                d.maps.insert(u"oob"_s, std::make_shared<oob::OOBEdits>(m, *m.country_hist, d.date));
                d.maps.insert(u"diplomacy"_s, std::make_shared<diplomacy::DiploEdits>(m, d.date));
            }
            d.maps.insert(u"owners_top"_s, std::make_shared<history::OwnersOnTop>(d));
            d.maps.insert(u"start_blocks"_s, std::make_shared<history::StartBlocks>(d, m));
        }
        std::unique_ptr<document::PendingScripts> pending;
        if (session[u"pending"_s].toBool(false)) {
            pending = std::make_unique<document::PendingScripts>(d);
            m.pending_script = pending.get();
        }
        QStringList lines{u"session %1 scenario %2 problems %3"_s.arg(pypath::basename(a[1]), sc).arg(d.problems.size())};
        const QJsonArray ops = session[u"ops"_s].toArray();
        for (qsizetype i = 0; i < ops.size(); ++i) {
            const QJsonObject op = ops[i].toObject();
            QString res;
            try {
                res = run_op(d, op, a[3]);
            } catch (const std::exception &e) {
                res = u"!%1: %2"_s.arg(err_name(e), QString::fromUtf8(e.what()));
            }
            lines << u"op %1 %2 -> %3"_s.arg(i).arg(op[u"op"_s].toString(), res);
            QList<long long> pids = ints(op[u"pids"_s]);
            for (const QString k : {u"pid"_s, u"source"_s})
                if (op.contains(k))
                    pids << op[k].toInteger();
            for (const QJsonValue &t : op[u"targets"_s].toArray())
                if (t.isArray())
                    pids << t.toArray()[0].toInteger();
            for (const QJsonValue &t : op[u"todo"_s].toArray())
                pids += ints(t.toArray()[1]);
            QStringList rids;
            if (op.contains(u"rid"_s))
                rids << op[u"rid"_s].toString();
            if (op[u"target"_s].isString())
                rids << op[u"target"_s].toString();
            QStringList tags;
            if (op.contains(u"tag"_s))
                tags << py::upper(py::strip(op[u"tag"_s].toString()));
            lines += state_lines(d, pids, rids, op.contains(u"key"_s) ? QStringList{op[u"key"_s].toString()} : QStringList(),
                                 tags);
        }
        QFile lf(a[4]);
        if (!lf.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 4;
        lf.write((lines.join(u'\n') + u'\n').toUtf8());
        lf.close();
        if (const QStringList bad = snapio_roundtrip(d); !bad.isEmpty()) {
            std::fprintf(stderr, "snapio: not the same when read back: %s\n", qPrintable(bad.join(u"; "_s)));
            return 7;
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
