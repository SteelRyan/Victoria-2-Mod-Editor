// countryhist against Python, the C++ side of tools/check_countryhist.py.
// Usage: med_countryhist --dump <out folder> <work folder>
//        med_countryhist --edit <mod copy> <backups> <base game> <Ferrum Mare's folder>
#include "nopopup.h"
#include "core/countryhist.h"
#include "core/fs.h"
#include "core/pypath.h"
#include "pyform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace countryhist;
using pyform::L;
using pyform::v;

static const QStringList NAMES = {u"Zed Land"_s, u"  A/B:C*D?  "_s, u"dots..."_s, u""_s, u"   "_s, u"q\"<>|x."_s,
                                  u"Ünïcödé Land"_s, u"..."_s, u"x\\y"_s};
static const QStringList TAGS = {u"ABC"_s, u"A1B"_s, u"1AB"_s, u"AB"_s, u"ABCD"_s, u"abc"_s, u"ABC\n"_s, u"AB_"_s, u"ZZ9"_s};

static QString sorted_v(const QSet<QString> &s)
{
    QStringList l(s.begin(), s.end());
    std::sort(l.begin(), l.end());
    return v(l);
}

static QString items_v(const OrderedMap<QString, QString> &m)
{
    QStringList p;
    for (const auto &[k, x] : m)
        p << L(k, x);
    return pyform::list(p);
}

static QString st_line(const NationState &st)
{
    std::vector<OptStr> sc;
    for (const QString &k : SCALARS)
        sc.push_back(st.at(k));
    QStringList src;
    for (const auto &[wk, s] : st.src)
        src << L(wk.first, wk.second, s.date,
                 std::make_pair(static_cast<long long>(s.span.first), static_cast<long long>(s.span.second)));
    return pyform::list({v(sc), v(st.cultures), items_v(st.reforms), sorted_v(st.techs), sorted_v(st.inventions),
                         items_v(st.upper_house), pyform::list(src)});
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
        NationData d(m);
        CountryHistory h(m, d);
        std::optional<Date> sd;
        for (const QString &s : m.pop_scenarios())
            if ((sd = script::parse_date(s)))
                break;
        QStringList out;
        QStringList fo;
        for (const auto &[f, a] : d.folders)
            fo << L(f, a);
        out << u"folders "_s + pyform::list(fo) << u"schools "_s + v(d.schools);
        for (const auto &[t, i] : d.techs)
            out << u"tech "_s + L(t, i.area, i.year, i.folder, i.index);
        for (const auto &[a, lst] : d.area_techs)
            out << u"area "_s + L(a, lst);
        for (const auto &[n, i] : d.inventions)
            out << u"inv "_s + L(n, i.file, i.needs);
        QStringList rf;
        for (const Reform &r : d.reforms)
            rf << L(r.section, r.issue, r.options);
        out << u"reforms "_s + pyform::list(rf) << u"reform_of "_s + items_v(d.reform_of)
            << u"issues "_s + sorted_v(d.reform_issues) << u"gov "_s + v(d.governments) << u"nv "_s + v(d.nationalvalues);
        QStringList ctags;
        for (const auto &kv : m.countries)
            ctags << kv.first;
        for (const QString &tag : ctags) {
            QStringList ps;
            for (const Party &p : d.parties(tag))
                ps << L(p.name, p.ideology, p.start, p.end);
            out << u"parties %1 %2"_s.arg(tag, pyform::list(ps));
        }
        out << u"files "_s + items_v(h.files) << u"base_src "_s + items_v(h.base_src)
            << u"start "_s + v(h.start());
        const auto gp = generic_party(m, u"ZZZ"_s);
        out << u"generic "_s + L(gp.first, gp.second);
        QStringList tags;
        for (const auto &kv : h.files)
            tags << kv.first;
        std::sort(tags.begin(), tags.end());
        for (const QString &tag : tags) {
            out << u"st0 %1 %2"_s.arg(tag, st_line(h.state_at(tag, std::nullopt)));
            if (sd) {
                const NationState st = h.state_at(tag, sd);
                out << u"std %1 %2"_s.arg(tag, st_line(st));
                out << u"stx %1 %2"_s.arg(tag, st_line(h.state_at(tag, sd, false)));
                out << u"counts %1 %2"_s.arg(tag, v(d.folder_counts(st.techs)));
            }
            if (h.start())
                out << u"sts %1 %2"_s.arg(tag, st_line(h.state_at(tag, h.start())));
            const moddata::Country *c = m.countries.get(tag);
            const QString path = (c && !c->file.isEmpty()) ? m.p(QStringList{u"common"_s} + c->file.split(u'/')) : QString();
            const QString ctext = (!path.isEmpty() && fs::isfile(path)) ? script::read_text(path) : QString();
            const QString htext = h.nodes(tag).text;
            out << u"pn %1 %2 %3"_s.arg(tag, v(party_names(ctext)), v(party_names(ctext, h.start())));
            const WithParty wp = with_party(m, tag, ctext, htext, h.start());
            out << u"wp %1 %2"_s.arg(tag, pyform::list({v(moddata::text_hash(wp.ctext)), v(moddata::text_hash(wp.htext)),
                                                        items_v(wp.texts), v(wp.did)}));
        }
        QStringList safe, tagok;
        for (const QString &n : NAMES)
            safe << safe_name(n);
        for (const QString &t : TAGS)
            tagok << (tag_ok(t) ? u"True"_s : u"False"_s);
        out << u"safe "_s + v(safe) << u"tag "_s + pyform::list(tagok);
        QFile f(QDir(out_dir).filePath(md.shortn + u".txt"_s));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        f.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %7lld lines\n", qPrintable(md.shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

// ---------------------------------------------------------------------- the edits, printed as Python prints them
static void say(const QString &s)
{
    const QByteArray b = (s + u'\n').toUtf8();
    std::fwrite(b.constData(), 1, size_t(b.size()), stdout);
}
static QString rels(const std::optional<QStringList> &paths, const QString &root)
{
    QStringList r;
    for (const QString &p : paths.value_or(QStringList()))
        r << pypath::relpath(p, root);
    return v(r);
}

static int edit(const QString &root, const QString &backups, const QString &base, const QString &src)
{
    moddata::Mod m(root, base, std::nullopt, {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s});
    m.load();
    NationData data(m);
    CountryHistory h(m, data);
    const Date start = *h.start();
    QStringList tags;
    for (const auto &kv : h.files)
        if (m.countries.contains(kv.first))
            tags << kv.first;
    std::sort(tags.begin(), tags.end());
    OrderedMap<QString, NationState> st;
    for (const QString &t : tags)
        st.insert(t, h.state_at(t, start));
    auto pick = [&](const std::function<bool(const QString &, const NationState &)> &ok) {
        for (const QString &t : tags)
            if (ok(t, *st.get(t)))
                return t;
        throw std::runtime_error("StopIteration");
    };
    const QString a = pick([](const QString &, const NationState &s) { return !s.techs.isEmpty() && s.at(u"capital"_s); });
    const QString b = pick([&](const QString &t, const NationState &s) { return t != a && s.cultures.size() >= 2; });
    const QString c = pick([&](const QString &t, const NationState &) { return t != a && t != b; });
    const QString d = pick([&](const QString &t, const NationState &s) {
        return t != a && t != b && t != c && !s.techs.isEmpty() && !s.cultures.isEmpty();
    });
    say(u"picked %1 %2 %3 %4"_s.arg(a, b, c, d));
    QStringList techs, invs;
    for (const auto &kv : data.techs)
        techs << kv.first;
    for (const auto &kv : data.inventions)
        invs << kv.first;
    auto first_not_in = [](const QStringList &all, const QSet<QString> &have) {
        for (const QString &x : all)
            if (!have.contains(x))
                return x;
        throw std::runtime_error("StopIteration");
    };
    auto sorted_of = [](const QSet<QString> &s) {
        QStringList l(s.begin(), s.end());
        std::sort(l.begin(), l.end());
        return l;
    };
    // 1) at the start date
    {
        NationEdits ne(h, start);
        ne.edit(a, [&](NationState &s) {
            s[u"capital"_s] = QString::number(py::to_int(*s.at(u"capital"_s)) + 1);
            s[u"literacy"_s] = u"0.42"_s;
            s.techs.remove(sorted_of(s.techs).first());
            s.techs.insert(first_not_in(techs, s.techs));
            s.inventions.insert(first_not_in(invs, s.inventions));
        });
        ne.edit(b, [&](NationState &s) {
            QStringList cs = s.cultures.mid(1);
            cs << u"zz_culture"_s;
            s.cultures = cs;
            const Reform &r = data.reforms[0];
            const QString *now = s.reforms.get(r.issue);
            s.reforms.insert(r.issue, (!now || *now != r.options.last()) ? r.options.last() : r.options.first());
            s[u"schools"_s] = data.schools.last();
        });
        ne.edit(c, [&](NationState &s) {
            OrderedMap<QString, QString> uh;
            const int shares[] = {10, 20, 30, 10, 10, 10, 10};
            for (qsizetype i = 0; i < UPPER_HOUSE_IDEOLOGIES.size(); ++i)
                uh.insert(UPPER_HOUSE_IDEOLOGIES[i], QString::number(shares[i]));
            s.upper_house = uh;
            s[u"prestige"_s] = u"12"_s;
        });
        ne.edit(c, [](NationState &) {});           // (an edit that changes nothing more)
        say(u"start dirty "_s + v(ne.dirty()));
        const store::Snap snap = ne.snapshot();
        ne.restore(snap);
        say(u"start save "_s + rels(ne.save(backups), root));
    }
    // 2) a later date
    const Date later{std::get<0>(start) + 4, 1, 1};
    const NationState orig_d = clone(h.state_at(d, later));
    {
        NationEdits ne(h, later);
        ne.edit(d, [&](NationState &s) {
            s[u"government"_s] = data.governments.last();
            s.techs.remove(sorted_of(s.techs).last());
            QStringList cs = s.cultures.mid(1);
            cs << u"zz_other"_s;
            s.cultures = cs;
            OrderedMap<QString, QString> uh;
            uh.insert(u"liberal"_s, u"60"_s);
            uh.insert(u"conservative"_s, u"40"_s);
            s.upper_house = uh;
        });
        ne.edit(a, [&](NationState &s) {
            s[u"ruling_party"_s] = u"zz_party"_s;
            for (qsizetype i = techs.size() - 1; i >= 0; --i)
                if (!s.techs.contains(techs[i])) {
                    s.techs.insert(techs[i]);
                    break;
                }
        });
        say(u"later dirty "_s + v(ne.dirty()));
        say(u"later save "_s + rels(ne.save(backups), root));
    }
    // 3) undone: d wanted again as before (its file goes back to the letter)
    {
        NationEdits ne(h, later);
        ne.want.insert(d, clone(orig_d));
        say(u"undo dirty "_s + v(ne.dirty()));
        say(u"undo save "_s + rels(ne.save(backups), root));
    }
    // 4) new nations
    const QString flags = pypath::join(src, {u"gfx"_s, u"flags"_s});
    const QString own = pypath::join(root, {u"gfx"_s, u"flags"_s});
    fs::makedirs(own);
    QStringList fns = fs::listdir(flags);
    std::sort(fns.begin(), fns.end());
    for (const QString &fn : fns)
        if (py::upper(fn).startsWith(a) && py::lower(fn).endsWith(u".tga"))
            fs::copy2(pypath::join(flags, fn), pypath::join(own, fn));
    NewNations nn(m, h);
    NationEdits nne(h, start);
    nn.nations = &nne;
    QStringList probs;
    for (const auto &[t, n] : {std::pair{u"qx1"_s, u"Zed Land"_s}, std::pair{u"ZZ"_s, u"x"_s}, std::pair{a, u"x"_s},
                               std::pair{u"QX1"_s, u""_s}, std::pair{u"REB"_s, u"x"_s}, std::pair{u"QX1"_s, u"Zed Land"_s}})
        probs << nn.problem(t, n);
    say(u"problems "_s + v(probs));
    nn.add(u"qx1"_s, u" New Land "_s, u""_s, {10, 20, 30}, a);
    nn.add_imported(u"QX3"_s, u"Third"_s, u""_s, {4, 5, 6}, u"countries/Third.txt"_s,
                    u"color = {1 1 1}\r\nparty = {\r\n\tname = \"QX3_lib\"\r\n\tideology = liberal\r\n}\r\n"_s,
                    u"ruling_party = \"wrong\"\r\ncapital = 7\r\noob = \"x.txt\"\r\n"_s);
    nn.add_imported(u"QX2"_s, u"Imported One"_s, u"Imp"_s, {1, 2, 3}, u"countries/Imported One.txt"_s, std::nullopt,
                    u"capital = 5\r\nprimary_culture = x\r\n"_s);
    auto new_flag = [&](const QString &t) { return m.countries.get(t)->is_new ? u"True"_s : u"None"_s; };
    QStringList cs;
    for (const QString &t : {u"QX1"_s, u"QX2"_s, u"QX3"_s})
        cs << u"("_s + v(t) + u", "_s + v(m.countries.get(t)->file) + u", "_s + new_flag(t) + u")"_s;
    say(u"countries "_s + pyform::list(cs));
    std::vector<std::pair<QString, QString>> virt(h.virtual_.begin(), h.virtual_.end());
    std::sort(virt.begin(), virt.end());
    QStringList vs;
    for (const auto &[k, x] : virt)
        vs << u"("_s + v(k) + u", "_s + v(x) + u")"_s;
    say(u"virtual "_s + pyform::list(vs));
    say(u"problem now "_s + nn.problem(u"QX1"_s, u"Other"_s) + u' ' + nn.problem(u"QX9"_s, u"New Land"_s));
    nne.edit(u"QX1"_s, [](NationState &s) { s[u"capital"_s] = u"77"_s; });
    say(u"new dirty "_s + v(nn.dirty()));
    nn.restore(nn.snapshot());
    QStringList order;
    for (const auto &kv : nn.added)
        order << kv.first;
    say(u"new order "_s + v(order));
    say(u"new save "_s + rels(nn.save(backups), root));
    say(u"party notes "_s + v(nn.party_notes));
    QStringList after;
    for (const QString &t : {u"QX1"_s, u"QX2"_s, u"QX3"_s})
        after << u"("_s + v(t) + u", "_s + new_flag(t) + u")"_s;
    QStringList vk;
    for (const auto &kv : h.virtual_)
        vk << kv.first;
    std::sort(vk.begin(), vk.end());
    say(u"after "_s + pyform::list(after) + u' ' + v(vk));
    return 0;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
        if (a.size() >= 6 && a[1] == u"--edit")
            return edit(a[2], a[3], a[4], a[5]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
