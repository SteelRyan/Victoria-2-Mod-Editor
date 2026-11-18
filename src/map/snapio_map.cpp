// snapio_map.h
#include "map/snapio_map.h"
#include "core/snapio_types.h"
#include "map/layerbmp.h"
#include "map/moreviews_data.h"
#include "map/nationextras_data.h"
#include "map/newprov.h"
#include "map/provbmp.h"

#include <QBuffer>
#include <QImage>

using namespace Qt::StringLiterals;

namespace snapio {

template <>
struct Codec<QImage> {
    // (recovery.py: copyreg — a QImage is pickled as its PNG bytes and loaded from them)
    static void put(Writer &w, const QImage &img)
    {
        QByteArray ba;
        QBuffer buf(&ba);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        buf.close();
        snapio::put(w, ba);
    }
    static QImage get(Reader &r)
    {
        QImage img;
        img.loadFromData(snapio::get<QByteArray>(r), "PNG");
        return img;
    }
};

template <>
struct Codec<history::HistFileP> {
    // a history file of a deleted province: its text (the nodes are read from it again) and its marks
    static void put(Writer &w, const history::HistFileP &p)
    {
        put_shared<history::HistFile>(w, p, [&](const history::HistFile &f) {
            snapio::put(w, f.rel);
            snapio::put(w, f.path);
            snapio::put(w, f.pending);
            snapio::put(w, f.base);
            snapio::put(w, f.text);
            snapio::put(w, f.hash);
        });
    }
    static history::HistFileP get(Reader &r)
    {
        return get_shared<history::HistFile>(r, [&] {
            const QString rel = snapio::get<QString>(r), path = snapio::get<QString>(r);
            const bool pending = snapio::get<bool>(r), base = snapio::get<bool>(r);
            const QString text = snapio::get<QString>(r), hash = snapio::get<QString>(r);
            auto f = std::make_shared<history::HistFile>(rel, path, text);
            f->pending = pending;
            f->base = base;
            f->hash = hash;
            return f;
        });
    }
};

SNAPIO_FIELDS(newprov::Info, x.owner, x.controller, x.cores, x.life_rating, x.trade_goods)
SNAPIO_FIELDS(newprov::NewProv, x.name, x.rgb, x.sea, x.tmpl, x.hist_rel, x.hist_text, x.info, x.done_version,
              x.done_area)
SNAPIO_FIELDS(newprov::Deleted, x.pr, x.files, x.heir)
SNAPIO_FIELDS(newprov::DefsSnap, x.added, x.gone)
SNAPIO_FIELDS(fourk::Pic, x.h, x.w, x.ch, x.d)
SNAPIO_FIELDS(fourk::Info, x.kind, x.pack, x.alpha, x.grey, x.bits)
SNAPIO_FIELDS(moreviews::PicData, x.rgba, x.info)
SNAPIO_FIELDS(moreviews::Asset, x.how, x.img, x.path, x.pic)
SNAPIO_FIELDS(nationextras::FlagEdits::Snap, x.fresh, x.gone)
SNAPIO_FIELDS(layerbmp::Recovery, x.idx, x.vals)
SNAPIO_FIELDS(provbmp::Recovery, x.idx, x.vals)

void register_map()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    register_core();
    reg<newprov::DefsSnap>(u"definitions"_s);
    reg<OrderedMap<QString, moreviews::Asset>>(u"assets"_s);
    reg<nationextras::FlagEdits::Snap>(u"flags"_s);
    reg<layerbmp::Recovery>(u"layerbmp.pixels"_s);
    reg<provbmp::Recovery>(u"provbmp.pixels"_s);
}

}  // namespace snapio
