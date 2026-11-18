// recovery.py — line by line (the picture, its parts, cut, put back; the file's own form: recovery_file.cpp).
#include "core/recovery.h"
#include "core/fs.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/snapio_types.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace recovery {

const int VERSION = 1;
const QString EXT = u".medrec"_s;

QString path_for(const QString &backup_root, const QString &mod_root, const QString &scenario)
{
    const QByteArray key = QCryptographicHash::hash(
        u"%1|%2"_s.arg(pypath::normcase(pypath::abspath(mod_root)), scenario).toUtf8(), QCryptographicHash::Sha1);
    return pypath::join(backup_root, {u"recovery"_s, QString::fromLatin1(key.toHex().left(16)) + EXT});
}

// ---------------------------------------------------------------------------------------------- the picture
std::optional<Pic> picture(document::Document &doc)
{
    // Everything pending (None: nothing pending).
    doc.flush_live();
    const std::vector<long long> pids = doc.dirty_provinces();
    const QStringList rids = doc.dirty_regions();
    const std::vector<long long> hp = doc.dirty_histories();
    QStringList kinds;
    OrderedMap<QString, store::Snap> special;
    for (const auto &[k, mf] : doc.maps) {
        try {
            if (mf->dirty().isEmpty())
                continue;
        } catch (const std::exception &) {
            continue;
        }
        if (mf->has_recovery()) {
            if (store::Snap st = mf->recovery_snap())
                special.insert(k, st);
        } else
            kinds << k;
    }
    if (pids.empty() && rids.isEmpty() && hp.empty() && kinds.isEmpty() && special.isEmpty())
        return std::nullopt;
    Pic out;
    out.p = doc._snapshot(QSet<long long>(pids.begin(), pids.end()), rids, QSet<long long>(hp.begin(), hp.end()), kinds);
    out.special = special;
    return out;
}

const OrderedMap<QString, QString> KIND_WORDS = {
    {u"terrain_bmp"_s, u"map/terrain.bmp"_s}, {u"rivers_bmp"_s, u"map/rivers.bmp"_s},
    {u"provinces_bmp"_s, u"map/provinces.bmp"_s}, {u"climate"_s, u"map/climate.txt"_s},
    {u"continent"_s, u"map/continent.txt"_s}, {u"adjacency"_s, u"map/adjacencies.csv"_s},
    {u"default_map"_s, u"map/default.map"_s}, {u"positions"_s, u"map/positions.txt"_s},
    {u"definitions"_s, u"new / deleted provinces (map/definition.csv …)"_s}, {u"loc"_s, u"names (localisation)"_s},
    {u"techs"_s, u"technologies / inventions"_s}, {u"production"_s, u"goods, factories, production"_s},
    {u"nations"_s, u"nation history"_s}, {u"new_nations"_s, u"new nations"_s}, {u"oob"_s, u"armies and navies"_s},
    {u"diplomacy"_s, u"diplomacy"_s}, {u"flags"_s, u"flags"_s}, {u"assets"_s, u"added pictures / music"_s},
    {u"utility"_s, u"utility states"_s}, {u"drop_states"_s, u"empty states out of region.txt"_s},
    {u"drop_pop_blocks"_s, u"pops of provinces that don't exist"_s}, {u"islands"_s, u"island flags"_s},
    {u"hist_new"_s, u"new history files"_s}, {u"river_plan"_s, u"river plans"_s},
    {u"owners_top"_s, u"owners in the undated part"_s}, {u"start_blocks"_s, u"start-date blocks folded in"_s}};

static QString words_of(const QString &k)
{
    return KIND_WORDS.value(k, QString(k).replace(u'_', u' '));
}

static const store::ItemsSnap *scripts_of(const QString &k, const store::Snap &snap)
{
    // (Python: k == "scripts" and isinstance(snap, dict))
    return k == u"scripts" ? dynamic_cast<const store::ItemsSnap *>(snap.get()) : nullptr;
}
static const QString GONE = QString(QChar(0)) + u"gone"_s;
static QStringList gone_of(const store::ItemsSnap &s)
{
    const store::Snap *g = s.items.get(store::Key(GONE));
    return g ? store::unbox<QStringList>(*g) : QStringList();
}

std::vector<std::pair<QString, QString>> units(const std::optional<Pic> &pic)
{
    // [(unit, what it is in words)] of a picture — what can be applied / reverted on its own.
    std::vector<std::pair<QString, QString>> out;
    if (!pic)
        return out;
    auto n_of = [](qsizetype n, const QString &one) { return u"%1 %2%3"_s.arg(n).arg(one, n == 1 ? u""_s : u"s"_s); };
    if (!pic->p.pops.isEmpty())
        out.emplace_back(u"pops"_s, u"pops of "_s + n_of(pic->p.pops.size(), u"province"_s));
    if (!pic->p.regs.isEmpty())
        out.emplace_back(u"states"_s, n_of(pic->p.regs.size(), u"state"_s) + u" (map/region.txt)"_s);
    if (!pic->p.provs.isEmpty())
        out.emplace_back(u"history"_s, u"history of "_s + n_of(pic->p.provs.size(), u"province"_s));
    QStringList ks;
    for (const auto &kv : pic->p.lists)
        ks << kv.first;
    std::sort(ks.begin(), ks.end());
    for (const QString &k : ks) {
        if (const store::ItemsSnap *s = scripts_of(k, *pic->p.lists.get(k))) {
            QStringList rels;
            for (const auto &kv : s->items)
                if (kv.first.str() != GONE)
                    rels << kv.first.str();
            std::sort(rels.begin(), rels.end());
            for (const QString &rel : rels)
                out.emplace_back(u"file:"_s + rel, rel);
            for (const QString &rel : gone_of(*s))
                out.emplace_back(u"file:"_s + rel, rel + u" (taken out of the mod)"_s);
            continue;
        }
        out.emplace_back(u"kind:"_s + k, words_of(k));
    }
    QStringList sp;
    for (const auto &kv : pic->special)
        sp << kv.first;
    std::sort(sp.begin(), sp.end());
    for (const QString &k : sp)
        out.emplace_back(u"kind:"_s + k, words_of(k));
    return out;
}

std::optional<Pic> cut(const std::optional<Pic> &pic, const QSet<QString> &keep)
{
    // The picture with only the units in `keep` (a "kind:scripts" keeps every script file).
    if (!pic)
        return std::nullopt;
    Pic out;
    if (keep.contains(u"pops"_s))
        out.p.pops = pic->p.pops;
    if (keep.contains(u"states"_s))
        out.p.regs = pic->p.regs;
    if (keep.contains(u"history"_s))
        out.p.provs = pic->p.provs;
    for (const auto &[k, snap] : pic->p.lists) {
        if (keep.contains(u"kind:"_s + k))
            out.p.lists.insert(k, snap);
        else if (const store::ItemsSnap *s = scripts_of(k, snap)) {
            store::Items files;
            for (const auto &[rel, t] : s->items)
                if (rel.str() != GONE && keep.contains(u"file:"_s + rel.str()))
                    files.insert(rel, t);
            QStringList gone;
            for (const QString &r : gone_of(*s))
                if (keep.contains(u"file:"_s + r))
                    gone << r;
            if (!gone.isEmpty())
                files.insert(store::Key(GONE), store::box(gone));
            if (!files.isEmpty())
                out.p.lists.insert(k, std::make_shared<const store::ItemsSnap>(files, s->pairs));
        }
    }
    for (const auto &[k, st] : pic->special)
        if (keep.contains(u"kind:"_s + k))
            out.special.insert(k, st);
    if (out.p.pops.isEmpty() && out.p.regs.isEmpty() && out.p.provs.isEmpty() && out.p.lists.isEmpty() &&
        out.special.isEmpty())
        return std::nullopt;
    return out;
}

bool put_back(document::Document &doc, const std::optional<Pic> &pic, const QString &label)
{
    // The picture into the document as ONE undo step (on top of the files as they are now).
    if (!pic)
        return false;
    QStringList kinds;
    for (const auto &kv : pic->p.lists)
        if (doc.maps.contains(kv.first))
            kinds << kv.first;
    for (const auto &kv : pic->special)
        if (doc.maps.contains(kv.first))
            kinds << kv.first;
    QList<long long> pids, hpids;
    for (const auto &kv : pic->p.pops)
        pids << kv.first;
    for (const auto &kv : pic->p.provs)
        hpids << kv.first;
    QStringList rids;
    for (const auto &kv : pic->p.regs)
        rids << kv.first;
    doc.edit(label, pids, [&] {
        // (the kinds first — new provinces among them — then the pixels that may name them)
        document::Picture snap{pic->p.pops, pic->p.regs, pic->p.provs, {}};
        for (const auto &[k, v] : pic->p.lists)
            if (doc.maps.contains(k))
                snap.lists.insert(k, v);
        doc._restore(snap);
        for (const auto &[k, st] : pic->special)
            if (const store::StoreP *mf = doc.maps.get(k); mf && (*mf)->has_recovery())
                (*mf)->recover_snap(st);
    }, rids, hpids, kinds);
    return true;
}

// ---------------------------------------------------------------------------------------------- the file
}  // namespace recovery

namespace snapio {
SNAPIO_FIELDS(recovery::Pic, x.p, x.special)
SNAPIO_FIELDS(recovery::Data, x.version, x.mod, x.scenario, x.time, x.count, x.picture, x.auto_, x.note)
}  // namespace snapio

namespace recovery {

static const QByteArray MAGIC = QByteArrayLiteral("ModEditor C++ recovery file");

std::optional<Data> save(const QString &path, document::Document &doc, bool auto_, const QString &note)
{
    auto pic = picture(doc);
    if (!pic) {
        drop(path);
        return std::nullopt;
    }
    Data data;
    data.version = VERSION;
    data.mod = doc.mod.root;
    data.scenario = doc.scenario;
    data.time = QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH:mm"_s);
    data.count = doc.pending_count();
    data.picture = std::move(*pic);
    data.auto_ = auto_;
    data.note = note;
    const QByteArray bytes = pack(data);
    fs::makedirs(pypath::dirname(path));
    const QString tmp = path + u".tmp"_s;
    {
        QFile f(fs::watched(tmp));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(bytes) != bytes.size())
            throw fs::OSError(u"%1: %2"_s.arg(f.errorString(), py::repr_str(tmp)).toStdString(), 0);
    }
    fs::replace(tmp, path);
    return data;
}

std::optional<Data> load(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    try {
        return unpack(f.readAll());
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

QByteArray pack(const Data &data)
{
    QByteArray bytes;
    QDataStream s(&bytes, QIODevice::WriteOnly);
    s.setVersion(QDataStream::Qt_6_5);
    snapio::Writer w(s);
    snapio::put(w, MAGIC);
    snapio::put(w, data);
    return bytes;
}

std::optional<Data> unpack(const QByteArray &bytes)
{
    QDataStream s(bytes);
    s.setVersion(QDataStream::Qt_6_5);
    snapio::Reader r(s);
    if (snapio::get<QByteArray>(r) != MAGIC)
        return std::nullopt;
    Data data = snapio::get<Data>(r);
    if (data.version != VERSION)
        return std::nullopt;
    return data;
}

void drop(const QString &path)
{
    try {
        if (fs::isfile(path))
            fs::remove(path);
    } catch (const fs::OSError &) {
    }
}

void keep_aside(const QString &path)
{
    // A recovery file the user threw away is kept as …discarded-<time> (never deleted outright).
    if (fs::isfile(path)) {
        try {
            fs::replace(path, path.left(path.size() - EXT.size()) + u".discarded-"_s +
                                  QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s) + EXT);
        } catch (const fs::OSError &) {
        }
    }
}

}  // namespace recovery
