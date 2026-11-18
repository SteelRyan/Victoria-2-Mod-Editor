// recovery against Python, the C++ side of tools/check_recovery.py.  Usage: med_recovery --run <copy> <backups> <work>
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "core/fs.h"
#include "core/pypath.h"
#include "core/recovery.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "map/layerbmp.h"
#include "map/mapview_data.h"
#include "map/snapio_map.h"

#include <QGuiApplication>
#include <QRegularExpression>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::v;
using mapview::MapData;
using OptStr = std::optional<QString>;

static std::unique_ptr<document::Document> setup(moddata::Mod &m, MapData &md)
{
    // The document as the window makes it (load_scenario: the stores recovery reads).
    QString sc;
    for (const QString &x : m.pop_scenarios())
        if (script::parse_date(x)) {
            sc = x;
            break;
        }
    auto d = std::make_unique<document::Document>(m, sc);
    d->maps.insert(u"terrain_bmp"_s, std::make_shared<layerbmp::IndexedBitmap>(m, md, u"terrain.bmp"_s, u"terrain_bmp"_s));
    d->maps.insert(u"scripts"_s, std::make_shared<scriptstore::ScriptStore>(m));
    d->track_live(u"scripts"_s);
    return d;
}

static QString pic_txt(const std::optional<recovery::Pic> &p) { return p ? u"(a picture)"_s : u"None"_s; }

static int run(const QString &root, const QString &backups, const QString &work)
{
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    const QStringList REPLACE = {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s};
    QStringList out;
    moddata::Mod m(root, BASE, std::nullopt, REPLACE);
    m.load();
    MapData md(m, m.p({u"map"_s, m.provinces_bmp}));
    auto d = setup(m, md);
    out << u"nothing %1 %2"_s.arg(pic_txt(recovery::picture(*d)), v(recovery::units(recovery::picture(*d))));
    std::vector<long long> ps;
    for (const auto &kv : m.provinces)
        ps.push_back(kv.first);
    std::sort(ps.begin(), ps.end());
    std::vector<long long> land;
    for (long long p : ps)
        if (!m.provinces.get(p)->sea && md.area[p] > 0 && d->total(p) > 0)
            land.push_back(p);
    out << u"scale "_s + v(d->scale({land[0], land[1], land[2]}, document::PopFilter(), 150.0));
    QStringList regs;
    for (const auto &[rid, r] : m.regions)
        if (r.provinces.size() >= 3)
            regs << rid;
    std::sort(regs.begin(), regs.end());
    const QString a = regs[0], b = regs[1];
    out << u"move "_s + v(d->move_to_state({m.regions.get(a)->provinces.back()}, b));
    d->edit_provinces(u"Forts"_s, {land[5], land[6]}, [](long long, history::ProvState &st) { st.fort = 3; });
    auto &store = d->map<scriptstore::ScriptStore>(u"scripts"_s);
    QStringList names = fs::listdir(pypath::join(root, u"events"_s));
    std::sort(names.begin(), names.end());
    QStringList rels;
    for (const QString &n : names)
        if (n.endsWith(u".txt") && rels.size() < 2)
            rels << u"events/"_s + n;
    for (const QString &rel : rels) {
        const QString text = store.text(rel);
        d->edit(u"Edit %1"_s.arg(rel), {}, [&] { store.set_text(rel, text + u"\n# an edit\n"_s); }, {}, {},
                {u"scripts"_s});
    }
    auto &tb = dynamic_cast<layerbmp::IndexedBitmap &>(**d->maps.get(u"terrain_bmp"_s));
    tb.load();
    std::vector<std::int64_t> idx;
    for (long long i = 1000; i < 1000 + 60 * 997; i += 997)
        idx.push_back(i);
    d->edit(u"Paint"_s, {}, [&] { tb.apply(idx, std::vector<std::uint8_t>(idx.size(), 5)); }, {}, {}, {u"terrain_bmp"_s});
    // the recovery file (each editor's own form, in a folder of its own): written, read back, and the picture read
    // back is the one used from here on
    snapio::register_map();
    const QString rp = recovery::path_for(pypath::join(pypath::dirname(backups), u"rec"_s), root, d->scenario);
    const auto data = recovery::save(rp, *d, false, u"a note"_s);
    const auto loaded = recovery::load(rp);
    out << u"file %1 %2 %3 %4 %5 %6"_s.arg(v(data.has_value()), v(loaded->scenario), v(loaded->auto_))
               .arg(loaded->count)
               .arg(v(loaded->note), v(loaded->mod == root));
    recovery::keep_aside(rp);
    out << u"kept aside %1 %2"_s.arg(v(fs::isfile(rp))).arg(fs::listdir(pypath::dirname(rp)).size());
    out << u"load gone "_s + (recovery::load(rp) ? u"(data)"_s : u"None"_s);
    const std::optional<recovery::Pic> pic = loaded->picture;
    const auto us = recovery::units(pic);
    out << u"pending %1 units %2"_s.arg(d->pending_count()).arg(v(us));
    QStringList sp;
    for (const auto &[k, st] : pic->special)
        sp << pyform::L(k, u"pixels"_s, static_cast<long long>(store::unbox<layerbmp::Recovery>(st).idx.size()));
    out << u"special "_s + pyform::list(sp);
    QSet<QString> every;
    for (const auto &u : us)
        every.insert(u.first);
    const QSet<QString> keep = {u"pops"_s, u"file:"_s + rels[0], u"kind:terrain_bmp"_s};
    const auto mine = recovery::cut(pic, keep), rest = recovery::cut(pic, every - keep);
    out << u"mine "_s + v(recovery::units(mine));
    out << u"rest "_s + v(recovery::units(rest));
    out << u"cut nothing "_s + pic_txt(recovery::cut(pic, {u"kind:nothing"_s}));
    // mainwindow._revert_inside: the states as read, the document anew (load_scenario)
    for (const auto &[rid, order] : d->region_orig)
        if (moddata::Region *r = m.regions.get(rid))
            r->provinces = order;
    d = setup(m, md);
    out << u"anew pending %1"_s.arg(d->pending_count());
    QStringList ks(keep.begin(), keep.end());
    std::sort(ks.begin(), ks.end());
    out << u"put back "_s + v(recovery::put_back(*d, mine, u"To apply: "_s + ks.join(u", "_s).left(80)));
    out << u"pending %1 units %2"_s.arg(d->pending_count()).arg(v(recovery::units(recovery::picture(*d))));
    auto saved = [&] {
        const auto [written, err] = d->save(backups);
        QStringList rel;
        for (const QString &p : written)
            rel << pypath::relpath(p, root).replace(u'\\', u'/');
        std::sort(rel.begin(), rel.end());
        OptStr bdir;
        if (err) {
            bdir = pypath::relpath(*err, backups).replace(u'\\', u'/');
            bdir->replace(QRegularExpression(u"\\d{8}_\\d{6}"_s), u"STAMP"_s);
        }
        return u"saved %1 backups %2"_s.arg(v(rel), v(bdir));
    };
    out << saved();
    out << u"put back rest "_s + v(recovery::put_back(*d, rest, u"Still pending (not applied)"_s));
    out << u"pending %1 units %2"_s.arg(d->pending_count()).arg(v(recovery::units(recovery::picture(*d))));
    {
        const auto u = d->undo();
        out << u"undo %1 pending %2"_s.arg(v(u)).arg(d->pending_count());
        const auto r = d->redo();
        out << u"redo %1 pending %2"_s.arg(v(r)).arg(d->pending_count());
    }
    out << saved();
    out << u"after "_s + v(recovery::units(recovery::picture(*d)));
    const QString p = recovery::path_for(backups, u"C:/Some Folder/A Mod"_s, d->scenario);   // (the same on both sides)
    const QString pr = pypath::relpath(p, backups).replace(u'\\', u'/');
    out << u"path "_s + pr.left(pr.size() - recovery::EXT.size());
    const QByteArray o = (out.join(u'\n') + u'\n').toUtf8();
    std::fwrite(o.constData(), 1, size_t(o.size()), stdout);
    return 0;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 5 && a[1] == u"--run")
            return run(a[2], a[3], a[4]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
