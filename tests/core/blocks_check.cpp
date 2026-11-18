// blocks + blockcode against Python, the C++ side of tools/check_blocks.py.  Usage: med_blocks --dump <out> <work>
#include "nopopup.h"
#include "core/blockcode.h"
#include "core/blocks.h"
#include "core/countryhist.h"
#include "core/document.h"
#include "core/events.h"
#include "core/localisation.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/refs.h"
#include "core/scriptstore.h"
#include "pyform.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <algorithm>
#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;
using namespace blockcode;
using pyform::L;
using pyform::v;

static const QStringList BLOCK_FILES = {u"common/goods.txt"_s, u"common/issues.txt"_s, u"units/infantry.txt"_s,
                                        u"common/defines.lua"_s, u"common/production_types.txt"_s};
static const QSet<QString> COND_KEYS = {u"trigger"_s, u"potential"_s, u"allow"_s, u"mean_time_to_happen"_s, u"limit"_s,
                                        u"modifier"_s, u"ai_chance"_s, u"ai_will_do"_s};
static const QStringList SAMPLES = {u"THIS"_s, u"this"_s, u"FROM"_s, u"owner"_s, u"yes"_s, u"no"_s, u"culture"_s, u"1"_s,
                                    u"-2"_s, u"0.5"_s, u".5"_s, u"1."_s, u"abc"_s, u"a b"_s, u"x\"y"_s, u""_s, u"300"_s,
                                    u"ENG"_s, u"zz_flag"_s};

static QString h(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}
static QString item_v(const Item &it)
{
    QString val;
    if (it.list) {
        QStringList p;
        for (const Item &c : it.items)
            p << v(item_v(c));                    // (Python: item_v gives a text, v() quotes it)
        val = pyform::list(p);
    } else {
        val = v(it.text);
    }
    return pyform::list({v(it.k), val, v(it.q)});
}
static QString pairs_v(const std::vector<std::pair<QString, QString>> &l)
{
    QStringList p;
    for (const auto &[a, b] : l)
        p << L(a, b);
    return pyform::list(p);
}
static QString spec_v(const Spec &s)
{
    QStringList opt(s.optional.begin(), s.optional.end());
    std::sort(opt.begin(), opt.end());
    return pyform::list({v(s.key), v(s.mode), v(s.cat), v(s.scopes), v(s.text), v(s.vtype),
                         s.fields ? pairs_v(*s.fields) : u"None"_s, v(s.out), v(s.inner), v(s.keytype), v(opt),
                         v(s.hidden), v(s.kind()), v(s.script()), v(s.is_block())});
}
static void paths_of(const script::Nodes &nodes, const QStringList &prefix, int depth, std::vector<QStringList> &out)
{
    for (const script::Node &n : nodes)
        if (n.has_key && !n.key.isEmpty() && n.is_block) {
            out.push_back(prefix + QStringList{n.key});
            if (depth < 2)
                paths_of(n.kids, prefix + QStringList{n.key}, depth + 1, out);
        }
}
static QString sorted_v(const QSet<QString> &s, qsizetype n = -1)
{
    QStringList l(s.begin(), s.end());
    std::sort(l.begin(), l.end());
    return v(n >= 0 ? l.mid(0, n) : l);
}

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    struct M {
        QString shortn, rel;
        QStringList replace;
        bool find;
    };
    const std::vector<M> mods = {
        {u"ferrum_mare"_s, u"Ferrum_Mare 1.2.000"_s,
         {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s}, false},
        {u"vanilla"_s, u"Victoria 2"_s, {}, false},
        {u"corgi"_s, u"Victoria 2\\mod\\CoRGI_puir 18.1"_s, {}, true},
        {u"modus"_s, u"Victoria 2\\mod\\Modus Omnino Demens 1.6"_s, {}, true}};
    for (const M &md : mods) {
        const QString root = pypath::join(work, md.rel);
        QStringList replace = md.replace;
        if (md.find) {
            auto [list, _b] = moddata::discover_mods(pypath::join(BASE, u"mod"_s));
            for (const auto &e : list)
                if (e.folder && pypath::normcase(pypath::abspath(*e.folder)) == pypath::normcase(pypath::abspath(root)))
                    replace = e.replace;
        }
        moddata::Mod m(root, md.shortn == u"vanilla" ? std::nullopt : std::optional<QString>(BASE), std::nullopt, replace);
        m.load();
        m.nation_data = std::make_shared<countryhist::NationData>(m);
        QString sc;
        for (const QString &s : m.pop_scenarios())
            if (script::parse_date(s)) {
                sc = s;
                break;
            }
        document::Document d(m, sc);
        auto sp = std::make_shared<scriptstore::ScriptStore>(m);
        d.maps.insert(u"scripts"_s, sp);
        d.maps.insert(u"loc"_s, std::make_shared<localisation::LocEdits>(m));
        scriptstore::ScriptStore &store = *sp;
        QStringList out;
        // --- blocks
        for (const QString &r : BLOCK_FILES) {
            const QString text = store.text(r);
            if (py::strip(text).isEmpty())
                continue;
            std::vector<QStringList> paths{{}};
            std::vector<QStringList> all;
            paths_of(script::parse(text), {}, 0, all);
            for (size_t i = 0; i < all.size() && i < 60; ++i)
                paths.push_back(all[i]);
            for (const QStringList &p : paths) {
                const auto kids = blocks::children_top(text, p);
                OptStr first;
                for (const script::Node &k : kids)
                    if (!k.is_block) {
                        first = k.key;
                        break;
                    }
                QStringList kb;
                for (size_t i = 0; i < kids.size() && i < 8; ++i)
                    kb << L(kids[i].key, kids[i].is_block);
                const OptStr bt = blocks::block_text(text, p);
                QStringList row{v(static_cast<long long>(kids.size())), v(pyform::list(kb)), v(h(bt ? *bt : u"None"_s)),
                                v(h(blocks::set_value(text, p, u"zz_key"_s, u"1"_s))),
                                v(h(blocks::add_block(text, p, u"zz_blk"_s, {u"a = 1"_s, u"b = { c = 2 }"_s}))),
                                v(h(blocks::set_block(text, p, u" x = 1 "_s))),
                                v(h(blocks::set_block(text, p + QStringList{u"zz_missing"_s}, u"y = 2"_s)))};
                if (first) {
                    const QString gone = blocks::del_value(text, p, *first);
                    row << v(h(blocks::set_value(text, p, *first, u"9"_s))) << v(h(gone))
                        << v(blocks::set_value(gone, p, *first, u"7"_s, text) == blocks::set_value(text, p, *first, u"7"_s))
                        << v(h(blocks::set_value(gone, p, *first, u"7"_s)));
                }
                out << u"blk %1 %2 %3"_s.arg(r, v(p), pyform::list(row));
            }
        }
        // --- the catalogue
        for (const std::vector<Spec> *l : {&COND(), &EFF(), &GOAL()})
            for (const Spec &s : *l)
                out << u"spec "_s + spec_v(s);
        events::EventDB db(m, &store);
        refs::RefIndex ri(m, db, [&](const QString &r) { return store.text(r); });
        Env env(m, &d, &ri, &db);
        {
            QStringList iss;
            qsizetype n = 0;
            for (const auto &[k, o] : env.issues)
                if (n++ < 5)
                    iss << L(k, o);
            out << u"env "_s + pyform::list({sorted_v(env.techs, 20), v(static_cast<long long>(env.techs.size())),
                                             v(static_cast<long long>(env.inventions.size())), pyform::list(iss),
                                             v(env.options.mid(0, 20)), sorted_v(env.ideologies), sorted_v(env.goods, 10),
                                             sorted_v(env.poptypes)});
        }
        QStringList vts;
        for (const auto &kv : SLOT_NAME)
            vts << kv.first;
        vts << u"zz_none"_s;
        for (const QString &vt : vts) {
            auto ch = env.choices(vt);
            if (ch && (vt == u"tech" || vt == u"invention"))   // (Python: a set's order among equal names)
                std::stable_sort(ch->begin(), ch->end(), [](const auto &a, const auto &b) {
                    return std::make_pair(py::lower(a.second), a.first) < std::make_pair(py::lower(b.second), b.first);
                });
            QString first6 = u"None"_s, hh = u"None"_s;
            if (ch && !ch->empty()) {
                first6 = pairs_v(std::vector<std::pair<QString, QString>>(ch->begin(), ch->begin() + std::min<size_t>(6, ch->size())));
                hh = h(pairs_v(*ch));
            } else if (ch) {
                first6 = u"None"_s;                // (an empty list prints as None here, as `if ch` does)
            }
            out << u"ch %1 %2 %3 %4 %5"_s.arg(vt, ch ? v(static_cast<long long>(ch->size())) : u"None"_s, first6, hh,
                                              v(env.first(vt)));
            QStringList lg;
            for (const QString &s : SAMPLES)
                lg << v(env.legal(vt, s));
            out << u"  ft %1 legal %2"_s.arg(v(env.free_text(vt)), pyform::list(lg));
        }
        {
            qsizetype n = 0;
            for (const auto &kv : env.issues)
                if (n++ < 3)
                    out << u"opt %1 %2"_s.arg(kv.first, pairs_v(*env.choices(u"option"_s, kv.first)));
        }
        QStringList some;
        auto take = [&](const QStringList &l, qsizetype n) { some += l.mid(0, n); };
        {
            QStringList cs, ps, rs, is;
            for (const auto &kv : m.countries)
                cs << kv.first;
            for (const auto &kv : m.provinces)
                ps << QString::number(kv.first);
            for (const auto &kv : m.regions)
                rs << kv.first;
            for (const auto &kv : env.issues)
                is << kv.first;
            auto sl = [](const QSet<QString> &s) {
                QStringList l(s.begin(), s.end());
                std::sort(l.begin(), l.end());
                return l;
            };
            take(cs, 3);
            take(ps, 3);
            take(rs, 2);
            take(is, 2);
            take(sl(env.techs), 2);
            take(sl(env.ideologies), 2);
            take(sl(env.goods), 2);
            take(sl(env.poptypes), 2);
            some << u"WAR"_s << u"zz"_s;
        }
        QStringList nm, en;
        for (const QString &k : some) {
            nm << L(k, names_of(k, &env));
            en << L(k, env.name(k), env.loc(k), env.prov_name(k));
        }
        out << u"names "_s + pyform::list(nm) << u"env names "_s + pyform::list(en);
        for (const std::vector<Spec> *l : {&COND(), &EFF(), &GOAL()})
            for (const Spec &s : *l)
                out << u"new %1 %2 %3"_s.arg(s.mode, v(s.key), item_v(new_item(s, &env)));
        // --- the trees
        QStringList rels = db.files.mid(0, 12);
        {
            qsizetype n = 0;
            for (const QString &r : db.files)
                if (r.startsWith(u"decisions/") && n++ < 6)
                    rels << r;
        }
        for (const QString &r : rels) {
            const QString text = db.texts.value(r);
            const Items items = read_items(text);
            std::vector<Span> spans;
            const QString dumped = dump(items, 0, &spans);
            const Items fn = from_nodes(script::parse(text), text);
            QStringList sp_;
            for (const Span &s : spans)
                sp_ << pyform::list({v(s.first), v(s.last), v(item_v(*s.item))});
            out << u"tree %1 %2 %3 %4 %5"_s.arg(r).arg(items.size()).arg(h(dumped), h(dump(fn)), h(pyform::list(sp_)));
            std::function<void(const Items &, const QString &, const OptStr &, int)> walk =
                [&](const Items &its, const QString &mode, const OptStr &scope, int depth) {
                    for (const Item &it : its) {
                        if (it.k == OptStr(NOTE))
                            continue;
                        const Spec *s = spec_for(it.k, mode, scope, &env, &it);
                        out << u"  it %1 %2 %3 %4 %5"_s.arg(depth).arg(v(it.k), mode,
                                                                       s ? L(s->key, s->mode, s->text) : u"None"_s,
                                                                       v(sentence(it, s, env)));
                        if (it.list && depth < 6) {
                            const QString k = it.k.value_or(QString());
                            const QString m2 = COND_KEYS.contains(py::lower(k)) ? u"cond"_s : mode;
                            const OptStr sc2 = (s && s->out && !s->out->isEmpty()) ? s->out : scope;
                            walk(it.items, m2, sc2, depth + 1);
                        }
                    }
                };
            if (r == rels[0] || r.startsWith(u"decisions/"))
                walk(Items(items.begin(), items.begin() + std::min<size_t>(30, items.size())), u"eff"_s, u"c"_s, 0);
        }
        if (!rels.isEmpty()) {
            Items items = read_items(db.texts.value(rels[0]));
            Item *b = block(items, u"zz_block"_s, true, 0);
            set_value(b->items, u"zz_a"_s, u"1"_s);
            set_value(b->items, u"zz_b"_s, u"two words"_s, true);
            set_value(b->items, u"zz_a"_s, u"3"_s);
            set_value(b->items, u"zz_b"_s, std::nullopt);
            block(b->items, u"inner"_s, true);
            out << u"edit "_s + h(dump(items)) + u' ' + v(value(b->items, u"zz_a"_s)) + u' ' +
                       v(child(b->items, u"inner"_s) != nullptr);
        }
        out << u"raw "_s + h(dump(read_items(u"a = \"unclosed\nb = { c d e }\n# note\nx = # c1\n y\n}\nz"_s)));
        QFile f(QDir(out_dir).filePath(md.shortn + u".txt"_s));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        f.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %7lld lines\n", qPrintable(md.shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);              // (the editor's texts are QTextDocuments)
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
