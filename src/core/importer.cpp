// importer.py — so far find_sources. Line by line.
#include "core/importer.h"
#include "core/fs.h"
#include "core/pypath.h"
#include "core/pytext.h"

#include <QSet>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace importer {

static QStringList sorted_lower(QStringList l)       // sorted(…, key=str.lower)
{
    std::stable_sort(l.begin(), l.end(), [](const QString &a, const QString &b) { return py::lower(a) < py::lower(b); });
    return l;
}

std::vector<std::pair<QString, QString>> find_sources(const moddata::Mod &mod)
{
    std::vector<std::pair<QString, QString>> out;
    QSet<QString> seen = {pypath::normcase(pypath::abspath(mod.root))};
    auto looks_like = [](const QString &d) {
        for (const QString &s : {u"common"_s, u"events"_s, u"decisions"_s, u"history"_s})
            if (fs::isdir(pypath::join(d, s)))
                return true;
        return false;
    };
    auto add = [&](const QString &d, const QString &name = {}) {
        const QString k = pypath::normcase(pypath::abspath(d));
        if (seen.contains(k) || !fs::isdir(d) || !looks_like(d))
            return;
        seen.insert(k);
        out.emplace_back(!name.isEmpty() ? name : pypath::basename(d), pypath::abspath(d));
    };
    const std::optional<QString> &base = mod.base;
    const QString parent = pypath::dirname(pypath::abspath(mod.root));
    QStringList dirs = {parent, pypath::join(parent, u"study mods"_s)};
    if (base && !base->isEmpty())
        dirs << pypath::join(*base, u"mod"_s);
    for (const QString &d : dirs) {
        if (!fs::isdir(d))
            continue;
        for (const QString &fn : sorted_lower(fs::listdir(d))) {   // (an OSError here goes up, as in Python)
            const QString full = pypath::join(d, fn);
            if (base && !base->isEmpty() &&
                pypath::normcase(pypath::abspath(full)) == pypath::normcase(pypath::abspath(*base)))
                continue;
            if (fs::isdir(full) && !looks_like(full)) {   // a download that keeps the mod one folder down
                QStringList subs;
                try {
                    subs = sorted_lower(fs::listdir(full));
                } catch (const fs::OSError &) {
                    subs.clear();
                }
                for (const QString &s : subs)
                    add(pypath::join(full, s), u"%1 / %2"_s.arg(fn, s));
                continue;
            }
            add(full);
        }
    }
    if (base && !base->isEmpty())
        add(*base, u"The game itself (Victoria 2, unmodded)"_s);
    if (fs::isfile(pypath::join(mod.root, {u"map"_s, u"provinces.bmp"_s})))   // to move its own provinces
        out.emplace_back(u"This mod itself — move its own provinces (as on disk: Apply first)"_s, pypath::abspath(mod.root));
    return out;
}

}  // namespace importer
