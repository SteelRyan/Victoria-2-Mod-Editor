// nationextras.py, the data half.
#include "map/nationextras_data.h"
#include "core/fs.h"
#include "core/pypath.h"
#include "map/imgio.h"

#include <QDateTime>
#include <algorithm>

using namespace Qt::StringLiterals;
using pypath::join;

namespace nationextras {

bool FlagEdits::_own(const QString &name) const { return fs::isfile(join(mod.root, {u"gfx"_s, u"flags"_s, name})); }

std::optional<QImage> FlagEdits::image(const QString &name) const
{
    if (const QImage *i = fresh.get(name))
        return *i;
    if (gone.contains(name))
        return std::nullopt;
    const QString p = mod.p({u"gfx"_s, u"flags"_s, name});
    return fs::isfile(p) ? imgio::read_image(p) : std::nullopt;
}

void FlagEdits::put(const OrderedMap<QString, QImage> &files)
{
    for (const auto &[k, v] : files) {
        fresh.insert(k, v);
        gone.remove(k);
    }
}

void FlagEdits::drop(const QStringList &names)
{
    // Flags go: one made in this session is simply forgotten, a file of the mod is deleted on Apply.
    for (const QString &n : names) {
        fresh.remove(n);
        if (_own(n))
            gone.insert(n);
    }
}

store::Snap FlagEdits::snapshot() const
{
    QStringList g(gone.begin(), gone.end());
    std::sort(g.begin(), g.end());
    return store::box(Snap{fresh, g});
}

void FlagEdits::restore(const store::Snap &s)
{
    const Snap &v = store::unbox<Snap>(s);
    fresh = v.fresh;
    gone = QSet<QString>(v.gone.begin(), v.gone.end());
}

QStringList FlagEdits::dirty() const
{
    QSet<QString> all;
    for (const auto &kv : fresh)
        all.insert(kv.first);
    for (const QString &n : gone)
        if (_own(n))
            all.insert(n);
    QStringList out(all.begin(), all.end());
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<QStringList> FlagEdits::save(const QString &backup_root)
{
    if (fresh.isEmpty() && gone.isEmpty())
        return std::nullopt;
    const QString stamp = QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s);
    const QString d = join(mod.root, {u"gfx"_s, u"flags"_s});
    std::optional<QString> out;
    QStringList g(gone.begin(), gone.end());
    std::sort(g.begin(), g.end());
    for (const QString &name : g) {
        const QString dst = join(d, name);
        if (fs::isfile(dst)) {
            const QString bdir = join(backup_root, {u"flags"_s, stamp});
            fs::makedirs(bdir);
            fs::copy2(dst, join(bdir, name));
            fs::remove(dst);
            out = dst;
        }
    }
    gone.clear();
    std::vector<std::pair<QString, QImage>> items(fresh.begin(), fresh.end());
    std::sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &[name, img] : items) {
        const QString dst = join(d, name);
        if (fs::exists(dst)) {
            const QString bdir = join(backup_root, {u"flags"_s, stamp});
            fs::makedirs(bdir);
            fs::copy2(dst, join(bdir, name));
        }
        imgio::write_tga(dst, img);              // (as Python: gfx/flags must be there)
        out = dst;
    }
    fresh = {};
    return out ? std::optional<QStringList>(QStringList{*out}) : std::nullopt;
}

}  // namespace nationextras
