// iconstrips.py, the data half — line by line.
#include "map/iconstrips_data.h"
#include "ui/drawtext.h"
#include "core/blocks.h"
#include "core/document.h"
#include "core/entryview_data.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "map/imgio.h"

#include <QDateTime>
#include <QFileInfo>
#include <QFont>
#include <QPainter>
#include <QRectF>
#include <QRegularExpression>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <tuple>

using namespace Qt::StringLiterals;

namespace iconstrips {

const std::vector<Use> USES = {
    {u"focus"_s, u"National focuses"_s, u"GFX_province_natfocus_strip"_s, {u"GFX_province_natfocus_strip_small"_s},
     {u"common/national_focus.txt"_s}, {}, u"icon"_s, 0, 1, false, u"Game rules → National focuses"_s, {}},
    {u"nv"_s, u"National values"_s, u"GFX_politics_nationalvalue"_s, {}, {u"common/nationalvalues.txt"_s}, {}, {}, 1, 1,
     false, u"Game rules → National values"_s,
     u"The game takes them in the file's order: the first national value shows picture 1, the second picture 2 … A "
     u"value beyond the last picture shows a wrong or empty picture in the game."_s},
    {u"goods"_s, u"Goods"_s, u"GFX_resources"_s, {u"GFX_resources_big"_s, u"GFX_resources_small"_s},
     {u"common/goods.txt"_s}, {}, {}, 2, 2, false, u"Economy"_s,
     u"The game takes them in the order of common/goods.txt. Picture 1 is the empty one (no good): the first good "
     u"shows picture 2, the second picture 3 … so the strip needs one picture more than there are goods."_s},
    {u"pops"_s, u"Pop types"_s, u"GFX_pops_small"_s, {u"GFX_pops_mini"_s}, {}, u"poptypes"_s, u"sprite"_s, 0, 1, false,
     u"Game rules → Pop types"_s, {}},
    {u"crime"_s, u"Crimes"_s, u"GFX_crime_strip"_s, {}, {u"common/crime.txt"_s}, {}, u"icon"_s, 0, 2, true,
     u"Game rules → Crimes"_s,
     u"Picture 1 is the empty one (a province without crime): `icon = 1` shows picture 2, `icon = 2` picture 3 … (the "
     u"game's strip has 9 pictures for 8 crimes; seen by the user 2026-10-15)."_s},
    {u"modifier"_s, u"Modifiers"_s, u"GFX_modifier_icons"_s, {},
     {u"common/event_modifiers.txt"_s, u"common/triggered_modifiers.txt"_s, u"common/static_modifiers.txt"_s}, {},
     u"icon"_s, 0, 1, false, u"Scripts → Modifiers, Game rules → Triggered modifiers"_s, {}},
    {u"rebel"_s, u"Rebels"_s, u"GFX_politics_rebel_strip"_s, {}, {u"common/rebel_types.txt"_s}, {}, u"icon"_s, 0, 1,
     false, u"Game rules → Rebels"_s, {}},
    {u"cb"_s, u"War goals"_s, u"GFX_diplo_wargoal_icons"_s, {}, {u"common/cb_types.txt"_s}, {}, u"sprite_index"_s, 0, 1,
     false, u"Scripts → War goals"_s, {}},
    {u"religion"_s, u"Religions"_s, u"GFX_icon_religion"_s, {u"GFX_icon_religion_small"_s}, {u"common/religion.txt"_s},
     {}, u"icon"_s, 0, 1, false, u"Population → Edit religions"_s, {}},
    {u"unit"_s, u"Unit types"_s, u"GFX_unit_strip"_s, {}, {}, u"units"_s, u"icon"_s, 0, 1, false,
     u"Warfare → Unit stats"_s, {}},
};

const Use *use_of(const QString &sprite)
{
    for (const Use &u : USES)
        if (u.sprite == sprite)
            return &u;
    return nullptr;
}

std::optional<long long> pic_of(const Use *use, std::optional<long long> value)
{
    // The picture (1 = the strip's first) a `key = value` line shows; a `zero` strip (crimes) counts from 0.
    if (!value)
        return std::nullopt;
    return *value + (use && use->zero ? 1 : 0);
}

long long value_of(const Use *use, long long picture) { return picture - (use && use->zero ? 1 : 0); }

QString tex_rel(const QString &value)
{
    static const QRegularExpression re(uR"([\\/]+)"_s);
    QString s = py::strip(value);
    while (s.startsWith(u'"'))                  // .strip('"')
        s.remove(0, 1);
    while (s.endsWith(u'"'))
        s.chop(1);
    return s.replace(re, u"/"_s);
}

static std::optional<QImage> _read(const QString &path)
{
    static std::map<QString, std::pair<qint64, std::optional<QImage>>> cache;   // path -> (mtime, picture)
    const QFileInfo fi(path);
    if (!fi.exists())
        return std::nullopt;
    const qint64 mt = fi.lastModified().toMSecsSinceEpoch();
    auto it = cache.find(path);
    if (it == cache.end() || it->second.first != mt)
        it = cache.insert_or_assign(path, std::make_pair(mt, imgio::read_image(path))).first;
    return it->second.second;
}

QImage placeholder(int w, int h, long long number)
{
    // A frame the editor draws itself: a dark tile with a gold edge and the frame's number.
    const int s = 4;
    QImage img(w * s, h * s, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const double m = std::max(2.0, std::min(w, h) * s * 0.08);
    p.setPen(QPen(QColor(u"#c9a15a"_s), std::max(3.0, std::min(w, h) * s * 0.07)));
    p.setBrush(QColor(u"#2a303a"_s));
    p.drawRoundedRect(QRectF(m, m, w * s - 2 * m, h * s - 2 * m), m * 1.6, m * 1.6);
    QFont f(u"Segoe UI"_s);
    f.setBold(true);
    const QString text = QString::number(number);
    f.setPixelSize(std::max(8, int(py::trunc_int(std::min(h * s * 0.62, (w * s - 4 * m) / (0.62 * double(text.size())))))));
    p.setFont(f);
    p.setPen(QColor(u"#f2e3c0"_s));
    drawtext::draw(p, QRectF(0, 0, w * s, h * s), Qt::AlignCenter, text);
    p.end();
    return img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QImage fit_frame(const QImage &img, int w, int h)
{
    // A picture made to fit a frame: scaled whole (not cut), centred, the rest see-through.
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    const QImage s = img.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter p(&out);
    p.drawImage(int(py::floordiv(static_cast<long long>(w - s.width()), 2LL)),
                int(py::floordiv(static_cast<long long>(h - s.height()), 2LL)), s);
    p.end();
    return out;
}

const script::Node *_sprite_node(const script::Nodes &nodes, const QString &name)
{
    for (const script::Node &n : nodes)
        if (n.is_block) {
            if (n.has_key && py::lower(n.key) == u"spritetype" &&
                std::any_of(n.kids.begin(), n.kids.end(), [&](const script::Node &c) {
                    return !c.is_block && c.has_key && py::lower(c.key) == u"name" && c.value == name;
                }))
                return &n;
            if (const script::Node *got = _sprite_node(n.kids, name))
                return got;
        }
    return nullptr;
}

QString set_frames(QString text, const QStringList &names, const OrderedMap<QString, long long> &n_of)
{
    // noOfFrames of the sprites `names` in a .gfx text set to n_of[name].
    for (const QString &name : names) {
        const script::Nodes nodes = script::parse(text);
        const script::Node *node = _sprite_node(nodes, name);
        if (!node)
            continue;
        const QString val = QString::number(n_of.value(name));
        const script::Node *c = nullptr;
        for (const script::Node &k : node->kids)
            if (k.has_key && !k.key.isEmpty() && py::lower(k.key) == u"noofframes" && !k.is_block) {
                c = &k;
                break;
            }
        text = c ? text.left(c->vstart) + val + text.mid(c->vend)
                 : blocks::_insert_before_close(text, *node, u"noOfFrames = "_s + val);
    }
    return text;
}

QStringList Strips::gfx_rels() const
{
    OrderedMap<QString, QString> names;
    QStringList roots;
    if (mod.base && !mod.base->isEmpty() && !mod.replaced({u"interface"_s}))
        roots << *mod.base;
    roots << mod.root;
    for (const QString &root : roots) {
        const QString d = pypath::join(root, u"interface"_s);
        if (fs::isdir(d))
            for (const QString &fn : fs::listdir(d))
                if (py::lower(fn).endsWith(u".gfx"))
                    names.insert(py::lower(fn), u"interface/"_s + fn);
    }
    // (C++ only, 2026-11-17) a .gfx file the editor made and holds, not yet written (Apply): a new building's strip,
    // shown picture by picture on the Building types page before any Apply
    if (store)
        for (const auto &[rel, d] : store->docs) {
            const QString lr = py::lower(rel);
            if (lr.startsWith(u"interface/"_s) && lr.endsWith(u".gfx"_s) && lr.count(u'/') == 1 &&
                !names.contains(lr.mid(10)))
                names.insert(lr.mid(10), rel);
        }
    QStringList out;
    for (const auto &kv : names)
        out << kv.second;
    std::sort(out.begin(), out.end());
    return out;
}

Sprites Strips::sprites()
{
    // {name: {"tex": rel, "frames": n, "gfx": rel}} — every sprite cut into frames or not.
    Sprites out;
    for (const QString &rel : gfx_rels()) {
        const QString text = store->text(rel);
        const QString *was = _texts.get(rel);
        if (!was || *was != text) {
            OrderedMap<QString, std::pair<QString, long long>> found;
            _walk(script::parse(text), found);
            _texts.insert(rel, text);
            _by_file.insert(rel, found);
        }
        for (const auto &[name, tn] : *_by_file.get(rel))
            out.insert(name, {tn.first, tn.second, rel});
    }
    return out;
}

void Strips::_walk(const script::Nodes &nodes, OrderedMap<QString, std::pair<QString, long long>> &found)
{
    for (const script::Node &n : nodes) {
        if (!n.is_block)
            continue;
        if (n.has_key && py::lower(n.key) == u"spritetype") {
            OrderedMap<QString, QString> f;
            for (const script::Node &c : n.kids)
                if (c.has_key && !c.key.isEmpty() && !c.is_block)
                    f.insert(py::lower(c.key), c.value);
            const QString name = f.value(u"name"_s), tex = f.value(u"texturefile"_s);
            if (!name.isEmpty() && !tex.isEmpty()) {
                long long frames = 1;
                try {
                    frames = py::to_int(f.value(u"noofframes"_s, u"1"_s));
                } catch (const py::ValueError &) {
                    frames = 1;
                }
                found.insert(name, {tex_rel(tex), std::max(1LL, frames)});
            }
        } else {
            _walk(n.kids, found);
        }
    }
}

std::pair<QString, std::optional<QString>> Strips::resolve(const QString &tex) const
{
    // (rel as the files have it, path or None) of a texture: the mod's, else the game's; .dds / .tga swapped when the
    // one named isn't there.
    const qsizetype dot = tex.lastIndexOf(u'.'), slash = tex.lastIndexOf(u'/');
    const bool has_ext = dot > slash && dot > slash + 1;
    const QString stem = has_ext ? tex.left(dot) : tex, ext = has_ext ? tex.mid(dot) : QString();
    for (const QString &cand : {tex, stem + (py::lower(ext) == u".dds" ? u".tga"_s : u".dds"_s)}) {
        const QString p = mod.p(cand.split(u'/'));
        if (fs::isfile(p))
            return {cand, p};
    }
    return {tex, std::nullopt};
}

std::optional<QImage> Strips::image(const QString &name, const Sprites *sprites0)
{
    Sprites own;
    if (!sprites0 || sprites0->isEmpty()) {
        own = sprites();
        sprites0 = &own;
    }
    const Sprite *sp = sprites0->get(name);
    if (!sp)
        return std::nullopt;
    const auto [rel, path] = resolve(sp->tex);
    if (assets)
        for (const auto &[r, a] : assets->fresh)
            if (a.how == u"strip" && py::lower(r) == py::lower(rel))
                return a.img;
    return path ? _read(*path) : std::nullopt;
}

QStringList Strips::family(const QString &name)
{
    // The strips that have to grow together: the one named, its sister strips, and every other sprite cut from the
    // same pictures. -> [names], the first is `name`.
    const Sprites sps = sprites();
    const Use *use = use_of(name);
    QStringList names = {name};
    if (use)
        for (const QString &a : use->also)
            if (sps.contains(a))
                names << a;
    QSet<QString> tex;
    for (const QString &n : names)
        if (const Sprite *s = sps.get(n))
            tex.insert(py::lower(s->tex));
    std::vector<std::pair<QString, Sprite>> all(sps.begin(), sps.end());
    std::sort(all.begin(), all.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &[other, sp] : all)
        if (!names.contains(other) && tex.contains(py::lower(sp.tex)))
            names << other;
    QStringList out;
    for (const QString &n : names)
        if (sps.contains(n))
            out << n;
    return out;
}

std::variant<std::pair<StripFiles, OrderedMap<QString, QString>>, QString> Strips::extend(const QString &name,
                                                                                         const std::vector<Filler> &fillers)
{
    const Sprites sps = sprites();
    const QStringList fam = family(name);
    StripFiles files;
    OrderedMap<QString, long long> n_of;
    QSet<QString> done;
    for (const QString &nm : fam) {
        const Sprite &sp = *sps.get(nm);
        const auto img = image(nm, &sps);
        if (!img)
            return u"The picture of %1 (%2) can't be read."_s.arg(nm, sp.tex);
        const QString rel = resolve(sp.tex).first;
        const long long n = sp.frames;
        n_of.insert(nm, n + static_cast<long long>(fillers.size()));
        if (done.contains(py::lower(rel)))
            continue;
        const long long fw = img->width() / n;
        if (fw < 1 || fw * n != img->width())
            return u"%1 is %2 wide for %3 pictures — that doesn't divide evenly, so it can't be made longer safely."_s
                .arg(sp.tex).arg(img->width()).arg(n);
        QImage nimg(int(img->width() + fw * static_cast<long long>(fillers.size())), img->height(), QImage::Format_ARGB32);
        nimg.fill(Qt::transparent);
        QPainter p(&nimg);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(0, 0, *img);
        for (size_t i = 0; i < fillers.size(); ++i) {
            const Filler &f = fillers[i];
            const int x = int(img->width() + static_cast<long long>(i) * fw);
            QImage part;
            if (f.kind == u"copy" && 1 <= f.frame && f.frame <= n) {
                part = img->copy(int((f.frame - 1) * fw), 0, int(fw), img->height());
            } else if (f.kind == u"image") {
                const QImage *own = f.sisters.get(nm);
                part = fit_frame(own ? *own : f.img, int(fw), img->height());
            } else {
                part = placeholder(int(fw), img->height(), n + static_cast<long long>(i) + 1);
            }
            p.drawImage(x, 0, part);
        }
        p.end();
        files.insert(rel, {u"strip"_s, nimg, {}});
        done.insert(py::lower(rel));
    }
    OrderedMap<QString, QString> texts;
    for (const QString &nm : fam) {
        const QString g = sps.get(nm)->gfx;
        texts.insert(g, set_frames(texts.contains(g) ? *texts.get(g) : store->text(g), {nm}, n_of));
    }
    return std::make_pair(files, texts);
}

StripFiles Strips::put(const QString &name, long long number, const QImage &picture)
{
    // Picture `number` of the strip (and of its sister strips) replaced.
    const Sprites sps = sprites();
    StripFiles files;
    for (const QString &nm : family(name)) {
        const Sprite &sp = *sps.get(nm);
        const auto img = image(nm, &sps);
        const QString rel = resolve(sp.tex).first;
        if (!img || files.contains(rel) || !(1 <= number && number <= sp.frames))
            continue;
        const long long fw = img->width() / sp.frames;
        QImage nimg = img->convertToFormat(QImage::Format_ARGB32);
        QPainter p(&nimg);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(int((number - 1) * fw), 0, fit_frame(picture, int(fw), img->height()).convertToFormat(QImage::Format_ARGB32));
        p.end();
        files.insert(rel, {u"strip"_s, nimg, {}});
    }
    return files;
}

std::variant<std::tuple<StripFiles, OrderedMap<QString, QString>, long long>, QString> Strips::remove(const QString &name,
                                                                                                     long long number)
{
    const Sprites sps = sprites();
    const Sprite *sp = sps.get(name);
    if (!sp)
        return u"This strip isn't in the interface files."_s;
    const long long n = sp->frames;
    if (n <= 1)
        return u"A strip needs at least one picture."_s;
    if (!(1 <= number && number <= n))
        return u"The strip has no picture %1."_s.arg(number);
    const Use *use = use_of(name);
    if (use) {
        if (number < use->first)
            return u"Picture %1 is the empty one the game needs before the first entry — it stays."_s.arg(number);
        QStringList who;
        for (const auto &[k, i] : users(*use))
            if (i && *i == number)
                who << k;
        if (!who.isEmpty()) {
            const QString more = who.size() > 6 ? u" and %1 more"_s.arg(who.size() - 6) : QString();
            const QString it = who.size() == 1 ? u"it"_s : u"them"_s;
            return u"Picture %1 is used by %2%3 — give %4 another picture (or remove %5) first: %6."_s.arg(
                QString::number(number), who.mid(0, 6).join(u", "_s), more, it, it, use->where);
        }
    }
    const QStringList fam = family(name);
    StripFiles files;
    OrderedMap<QString, long long> n_of;
    QSet<QString> done;
    for (const QString &nm : fam) {
        const Sprite &s2 = *sps.get(nm);
        const auto img = image(nm, &sps);
        if (!img)
            return u"The picture of %1 (%2) can't be read."_s.arg(nm, s2.tex);
        const QString rel = resolve(s2.tex).first;
        const long long n2 = s2.frames;
        if (number > n2)
            return u"%1 has only %2 pictures, this strip %3 — make them the same length first."_s.arg(nm).arg(n2).arg(n);
        n_of.insert(nm, n2 - 1);
        if (done.contains(py::lower(rel)))
            continue;
        const long long fw = img->width() / n2;
        if (fw < 1 || fw * n2 != img->width())
            return u"%1 is %2 wide for %3 pictures — that doesn't divide evenly, so a picture can't be taken out safely."_s
                .arg(s2.tex).arg(img->width()).arg(n2);
        const QImage src = img->convertToFormat(QImage::Format_ARGB32);
        QImage nimg(int(img->width() - fw), img->height(), QImage::Format_ARGB32);
        nimg.fill(Qt::transparent);
        QPainter p(&nimg);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(0, 0, src.copy(0, 0, int((number - 1) * fw), img->height()));
        if (number < n2)
            p.drawImage(int((number - 1) * fw), 0, src.copy(int(number * fw), 0, int((n2 - number) * fw), img->height()));
        p.end();
        files.insert(rel, {u"strip"_s, nimg, {}});
        done.insert(py::lower(rel));
    }
    OrderedMap<QString, QString> texts;
    for (const QString &nm : fam) {
        const QString g = sps.get(nm)->gfx;
        texts.insert(g, set_frames(texts.contains(g) ? *texts.get(g) : store->text(g), {nm}, n_of));
    }
    long long moved = 0;
    if (use && !use->key.isEmpty()) {              // the lines that named a later picture: one down
        QStringList rels = use->rels;
        if (!use->folder.isEmpty())
            rels += entryview::_files(mod, store, use->folder);
        for (const QString &rel : rels) {
            QString text;
            try {
                text = store->text(rel);
            } catch (const fs::OSError &) {
                continue;
            }
            std::vector<std::tuple<qsizetype, qsizetype, QString>> spots;
            std::function<void(const script::Nodes &)> walk = [&](const script::Nodes &nodes) {
                for (const script::Node &c : nodes) {
                    if (c.is_block) {
                        walk(c.kids);
                    } else if (c.has_key && c.key == use->key && py::isdigit(c.value)) {
                        const auto pic = pic_of(use, py::to_int(c.value));
                        if (pic && *pic > number)
                            spots.emplace_back(c.vstart, c.vend, QString::number(py::to_int(c.value) - 1));
                    }
                }
            };
            walk(script::parse(text));
            std::sort(spots.begin(), spots.end(), [](const auto &a, const auto &b) { return a > b; });
            for (const auto &[a, b, v] : spots)
                text = text.left(a) + v + text.mid(b);
            if (!spots.empty()) {
                texts.insert(rel, text);
                moved += static_cast<long long>(spots.size());
            }
        }
    }
    return std::make_tuple(files, texts, moved);
}

std::variant<std::pair<StripFiles, OrderedMap<QString, QString>>, QString> Strips::reorder(const QString &name,
                                                                                          const std::vector<long long> &order)
{
    // The strip's pictures in a new order: the new picture i is the old picture order[i - 1] (a shorter list drops
    // the others). For the strips whose pictures go by an entry's place in a file.
    const Sprites sps = sprites();
    if (!sps.contains(name))
        return u"This strip isn't in the interface files."_s;
    const long long n = sps.get(name)->frames;
    if (order.empty() || std::any_of(order.begin(), order.end(), [n](long long k) { return !(1 <= k && k <= n); }))
        return u"The strip has %1 pictures."_s.arg(n);
    QStringList fam;
    for (const QString &nm : family(name))
        if (sps.get(nm)->frames == n)
            fam << nm;
    StripFiles files;
    OrderedMap<QString, long long> n_of;
    QSet<QString> done;
    for (const QString &nm : fam) {
        const Sprite &sp = *sps.get(nm);
        const auto img = image(nm, &sps);
        if (!img)
            return u"The picture of %1 (%2) can't be read."_s.arg(nm, sp.tex);
        const QString rel = resolve(sp.tex).first;
        n_of.insert(nm, static_cast<long long>(order.size()));
        if (done.contains(py::lower(rel)))
            continue;
        const long long fw = img->width() / n;
        if (fw < 1 || fw * n != img->width())
            return u"%1 is %2 wide for %3 pictures — that doesn't divide evenly."_s.arg(sp.tex).arg(img->width()).arg(n);
        const QImage src = img->convertToFormat(QImage::Format_ARGB32);
        QImage nimg(int(fw * static_cast<long long>(order.size())), img->height(), QImage::Format_ARGB32);
        nimg.fill(Qt::transparent);
        QPainter p(&nimg);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        for (size_t i = 0; i < order.size(); ++i)
            p.drawImage(int(static_cast<long long>(i) * fw), 0, src.copy(int((order[i] - 1) * fw), 0, int(fw), img->height()));
        p.end();
        files.insert(rel, {u"strip"_s, nimg, {}});
        done.insert(py::lower(rel));
    }
    OrderedMap<QString, QString> texts;
    if (static_cast<long long>(order.size()) != n)
        for (const QString &nm : fam) {
            const QString g = sps.get(nm)->gfx;
            texts.insert(g, set_frames(texts.contains(g) ? *texts.get(g) : store->text(g), {nm}, n_of));
        }
    return std::make_pair(files, texts);
}

Strips &strips_for(const moddata::Mod &mod, document::Document &doc)
{
    static std::map<const document::Document *, std::unique_ptr<Strips>> kept;   // (Python: doc._icon_strips)
    auto &s = kept[&doc];
    if (!s || &s->mod != &mod) {
        scriptstore::ScriptStore *store = nullptr;
        if (const store::StoreP *sp = doc.maps.get(u"scripts"_s))
            store = dynamic_cast<scriptstore::ScriptStore *>(sp->get());
        moreviews::AssetEdits *assets = nullptr;
        if (const store::StoreP *ap = doc.maps.get(u"assets"_s))
            assets = dynamic_cast<moreviews::AssetEdits *>(ap->get());
        s = std::make_unique<Strips>(mod, store, assets);
    }
    return *s;
}

// ------------------------------------------------------------------ who uses which picture
std::vector<std::pair<QString, std::optional<long long>>> Strips::users(const Use &use)
{
    std::vector<std::pair<QString, std::optional<long long>>> out;
    QStringList rels = use.rels;
    if (!use.folder.isEmpty())
        rels += entryview::_files(mod, store, use.folder);
    for (const QString &rel : rels) {
        script::Nodes nodes;
        try {
            nodes = script::parse(store->text(rel));
        } catch (const fs::OSError &) {
            continue;
        }
        const QString stem = pypath::splitext_stem(pypath::basename(rel));
        if (use.order) {
            std::vector<const script::Node *> level;
            for (const script::Node &n : nodes)
                if (n.has_key && !n.key.isEmpty() && n.is_block)
                    level.push_back(&n);
            if (use.order == 2) {
                std::vector<const script::Node *> inner;
                for (const script::Node *n : level)
                    for (const script::Node &c : n->kids)
                        if (c.has_key && !c.key.isEmpty() && c.is_block)
                            inner.push_back(&c);
                level = inner;
            }
            for (size_t i = 0; i < level.size(); ++i)
                out.emplace_back(level[i]->key, static_cast<long long>(i) + use.first);
            continue;
        }
        for (const script::Node &c : nodes)
            if (c.has_key && c.key == use.key && !c.is_block) {   // a file that is one entry (a pop type)
                out.emplace_back(stem, py::isdigit(c.value) ? pic_of(&use, py::to_int(c.value)) : std::nullopt);
                break;
            }
        _users(nodes, use.key, out, use);
    }
    return out;
}

void Strips::_users(const script::Nodes &nodes, const QString &key,
                    std::vector<std::pair<QString, std::optional<long long>>> &out, const Use &use)
{
    for (const script::Node &n : nodes)
        if (n.is_block && n.has_key && !n.key.isEmpty()) {
            for (const script::Node &c : n.kids)
                if (c.has_key && c.key == key && !c.is_block) {
                    out.emplace_back(n.key, py::isdigit(c.value) ? pic_of(&use, py::to_int(c.value)) : std::nullopt);
                    break;
                }
            _users(n.kids, key, out, use);
        }
}

QString picture_words(Strips &strips, const QString &sprite, std::optional<long long> num)
{
    const Sprites sps = strips.sprites();
    const Sprite *sp = sps.get(sprite);
    if (!sp)
        return u"Its picture comes from the sprite %1 - no .gfx file of the mod or the game defines it."_s.arg(sprite);
    QString tex = sp->tex;
    tex.replace(u'\\', u'/');
    const QString where = u"the picture strip %1 (sprite %2, named in %3)"_s.arg(tex, sprite, sp->gfx);
    if (!num)
        return u"Its picture: none picked - it should pick one of %1."_s.arg(where);
    if (*num < 1 || *num > sp->frames)
        return u"Its picture: number %1 - but %2 has only %3 picture(s), so this one points past its end (what the game "
               u"then shows is not measured). Add a picture to the strip for it."_s
            .arg(QString::number(*num), where, QString::number(sp->frames));
    return u"Its picture: number %1 of %2 in %3."_s.arg(QString::number(*num), QString::number(sp->frames), where);
}

}  // namespace iconstrips
