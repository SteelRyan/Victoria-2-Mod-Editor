// imgio + ddsimage against Python, the C++ side of tools/check_imgio.py.
// Usage: med_imgio --dump <out folder> <list file> <write folder> <work folder>
#include "../core/nopopup.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "map/ddsimage.h"
#include "map/imgio.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <cstdio>

using namespace Qt::StringLiterals;

static QString img_line(const std::optional<QImage> &img)
{
    if (!img)
        return u"None"_s;
    if (img->isNull())
        return u"null"_s;
    const int fmt = static_cast<int>(img->format());
    const QImage q = img->convertToFormat(QImage::Format_RGBA8888);
    QByteArray rows;
    for (int y = 0; y < q.height(); ++y)
        rows.append(reinterpret_cast<const char *>(q.constScanLine(y)), q.width() * 4);
    return u"%1 %2 %3 %4"_s.arg(QString::number(q.width()), QString::number(q.height()), QString::number(fmt),
                                QString::fromLatin1(QCryptographicHash::hash(rows, QCryptographicHash::Sha1).toHex().left(16)));
}

static int dump(const QString &out_dir, const QString &list_file, const QString &write_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    QFile lf(list_file);
    if (!lf.open(QIODevice::ReadOnly))
        return 4;
    QStringList paths;
    for (const QString &l : QString::fromUtf8(lf.readAll()).split(u'\n'))
        if (!l.trimmed().isEmpty())
            paths << l;
    QStringList out;
    std::vector<QImage> writes;
    for (const QString &p : paths) {
        const QString r = pypath::relpath(p, work).replace(u'\\', u'/');
        const QString low = py::lower(p);
        QStringList row = {r};
        if (low.endsWith(u".tga"))
            row << u"tga "_s + img_line(imgio::read_tga(p));
        if (low.endsWith(u".dds")) {
            row << u"dds "_s + img_line(imgio::read_dds(p));
            row << u"load "_s + img_line(ddsimage::load_dds(p));
        }
        const auto img = imgio::read_image(p);
        row << u"image "_s + img_line(img);
        row << u"tex "_s + img_line(ddsimage::load_texture(p));
        out << row.join(u" | "_s);
        if (img && !img->isNull() && (r.startsWith(u"ModEditor Cpp") || writes.size() < 60) &&
            static_cast<long long>(img->width()) * img->height() <= 512 * 512)
            writes.push_back(*img);
    }
    QDir().mkpath(write_dir);
    for (size_t i = 0; i < writes.size(); ++i) {
        const QString n = QString::number(i).rightJustified(3, u'0');
        imgio::write_tga(pypath::join(write_dir, n + u"_24.tga"_s), writes[i]);
        imgio::write_tga32(pypath::join(write_dir, n + u"_32.tga"_s), writes[i]);
        imgio::write_dds32(pypath::join(write_dir, n + u"_32.dds"_s), writes[i]);
        imgio::write_image(pypath::join(write_dir, {u"sub"_s, n + u"_im.DDS"_s}), writes[i]);
        imgio::write_image(pypath::join(write_dir, {u"sub"_s, n + u"_im.tga"_s}), writes[i]);
    }
    out << u"written %1"_s.arg(writes.size());
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
        const auto [tex, frames] = ddsimage::_sprite(m, u"GFX_icon_religion"_s);
        const QList<QPixmap> &icons = ddsimage::religion_icons(m);
        out << u"religion %1 %2 %3 %4"_s.arg(md.shortn, tex ? *tex : u"None"_s, QString::number(frames),
                                             QString::number(icons.size()));
        for (qsizetype k = 0; k < icons.size(); ++k)
            out << u"  icon %1 %2"_s.arg(QString::number(k), img_line(icons[k].toImage()));
        const auto [t2, f2] = ddsimage::_sprite(m, u"GFX_zz_none"_s);
        out << u"sprite %1 (%2, %3)"_s.arg(md.shortn, t2 ? py::repr_str(*t2) : u"None"_s, QString::number(f2));
    }
    QFile f(QDir(out_dir).filePath(u"g2.txt"_s));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 3;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    std::printf("g2 %lld lines, %lld written\n", static_cast<long long>(out.size()), static_cast<long long>(writes.size()));
    return 0;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 6 && a[1] == u"--dump")
            return dump(a[2], a[3], a[4], a[5]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
