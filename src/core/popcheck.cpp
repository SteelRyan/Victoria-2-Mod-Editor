// popcheck.py — pops the mod can't have. Line by line.
#include "core/popcheck.h"
#include "core/document.h"
#include "core/fs.h"
#include "core/importer.h"
#include "core/pypath.h"
#include "core/pyre.h"
#include "core/pytext.h"
#include "core/scriptstore.h"

#include <QFile>

using namespace Qt::StringLiterals;

namespace popcheck {

static QString splitext_stem(const QString &p)       // os.path.splitext(p)[0]
{
    const qsizetype sep = std::max(p.lastIndexOf(u'/'), p.lastIndexOf(u'\\'));
    const qsizetype dot = p.lastIndexOf(u'.');
    if (dot > sep)
        for (qsizetype i = sep + 1; i < dot; ++i)
            if (p[i] != u'.')
                return p.left(dot);
    return p;
}

QSet<QString> valid_types(const moddata::Mod &mod, scriptstore::ScriptStore *store)
{
    // The pop types of this mod — with the pop type files made / imported in this session (not applied yet).
    QSet<QString> out;
    for (const auto &[k, _t] : mod.poptypes)
        out.insert(k);
    if (store) {
        QStringList rels;
        for (const auto &[rel, _d] : store->docs)
            rels << rel;
        for (const QString &rel : rels) {
            const QString low = py::lower(rel);
            if (low.startsWith(u"poptypes/") && low.endsWith(u".txt") && !store->gone.contains(rel))
                if (!py::strip(store->text(rel)).isEmpty())
                    out.insert(splitext_stem(pypath::basename(rel)));
        }
    }
    return out;
}

Problems problems(document::Document &doc, const moddata::Mod &mod)
{
    scriptstore::ScriptStore *store = nullptr;
    if (const store::StoreP *s = doc.maps.get(u"scripts"_s))
        store = dynamic_cast<scriptstore::ScriptStore *>(s->get());
    const QSet<QString> types = valid_types(mod, store);
    QSet<QString> cultures, religions;
    for (const auto &[k, _c] : mod.cultures)
        cultures.insert(k);
    for (const auto &[k, _r] : mod.religions)
        religions.insert(k);
    QSet<long long> drop;
    if (const store::StoreP *s = doc.maps.get(u"drop_pop_blocks"_s))
        if (const auto *d = dynamic_cast<const document::DropPopBlocks *>(s->get()))
            drop = d->pids;
    Problems out;
    for (const auto &[pid, pops] : doc.pops) {
        if (drop.contains(pid) || pops.empty())
            continue;
        const moddata::PopProvince *src = doc.src.get(pid);
        const Where src_where = src ? Where(src->file, src->line) : Where(std::nullopt, std::nullopt);
        if (!mod.provinces.contains(pid)) {
            Problem pr;
            pr.pops = static_cast<long long>(pops.size());
            for (const moddata::Pop &p : pops)
                pr.size += p.size;
            pr.where = src_where;
            out.province.insert(pid, pr);
            continue;
        }
        for (const moddata::Pop &p : pops) {
            const std::pair<const char16_t *, std::optional<QString>> fields[3] = {
                {u"type", p.type}, {u"culture", p.culture}, {u"religion", p.religion}};
            for (const auto &[field, k] : fields) {
                const bool is_type = field == std::u16string_view(u"type");
                const QSet<QString> &valid = is_type ? types : field == std::u16string_view(u"culture") ? cultures : religions;
                if (!k || k->isEmpty() || valid.contains(*k) || (!is_type && valid.isEmpty()))
                    continue;
                OrderedMap<QString, Problem> &m = is_type ? out.type : field == std::u16string_view(u"culture") ? out.culture
                                                                                                             : out.religion;
                Problem &d = m[*k];
                if (std::find(d.pids.begin(), d.pids.end(), pid) == d.pids.end())
                    d.pids.push_back(pid);
                d.pops += 1;
                d.size += p.size;
                if (!d.where)
                    d.where = p.o ? Where(p.o->file, p.o->line) : src_where;
            }
        }
    }
    return out;
}

long long count(const Problems &probs)
{
    return probs.type.size() + probs.culture.size() + probs.religion.size() + probs.province.size();
}

QString where_text(const std::optional<Where> &where)
{
    const Where w = where.value_or(Where(std::nullopt, std::nullopt));
    if (!w.first || w.first->isEmpty())
        return u"pasted in this session (not in a file yet)"_s;
    return (w.second && *w.second) ? u"%1, line %2"_s.arg(*w.first, QString::number(*w.second)) : *w.first;
}

std::vector<std::pair<QString, QString>> sources_with(const moddata::Mod &mod, const QString &kind, const QString &key)
{
    // [(name, folder)] of the mods (and the game) the editor can find that have this pop type / culture / religion
    std::vector<std::pair<QString, QString>> out;
    for (const auto &[name, folder] : importer::find_sources(mod)) {
        if (pypath::normcase(pypath::abspath(folder)) == pypath::normcase(pypath::abspath(mod.root)))
            continue;
        if (kind == u"type") {
            if (fs::isfile(pypath::join(folder, {u"poptypes"_s, key + u".txt"_s})))
                out.emplace_back(name, folder);
            continue;
        }
        const QString p = pypath::join(folder, {u"common"_s, kind == u"culture" ? u"cultures.txt"_s : u"religion.txt"_s});
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QString txt = QString::fromLatin1(f.readAll());
        if (py::re_search(u"(?m)^"_s + py::PY_S + u"*"_s + QRegularExpression::escape(key) + py::PY_S + u"*="_s + py::PY_S +
                              u"*\\{"_s,
                          txt)
                .hasMatch())
            out.emplace_back(name, folder);
    }
    return out;
}

static std::optional<QString> field_of(const moddata::Pop &p, const QString &field)
{
    if (field == u"type")
        return p.type;
    if (field == u"culture")
        return p.culture;
    if (field == u"religion")
        return p.religion;
    return std::nullopt;
}

long long change_into(document::Document &doc, const QString &field, const QString &old, const QString &nw,
                      const std::optional<QList<long long>> &pids0)
{
    // Every pop whose `field` (type / culture / religion) is `old` becomes `new` — one undo step.
    QList<long long> all;
    if (pids0)
        all = *pids0;
    else
        for (const auto &[pid, _p] : doc.pops)
            all << pid;
    QList<long long> pids;
    long long n0 = 0;
    for (long long pid : all) {
        bool any = false;
        for (const moddata::Pop &p : doc.province_pops(pid))
            if (field_of(p, field) == std::optional<QString>(old))
                any = true;
        if (any)
            pids << pid;
    }
    for (long long pid : pids)
        for (const moddata::Pop &p : doc.province_pops(pid))
            if (field_of(p, field) == std::optional<QString>(old))
                ++n0;
    const QString label = u"%1 pops: %2 %3 → %4"_s.arg(QString::number(n0), field == u"type" ? u"pop type"_s : field, old, nw);
    return doc.edit(label, pids, [&]() -> long long {
        long long n = 0;
        for (long long pid : pids)
            if (std::vector<moddata::Pop> *ps = doc.pops.get(pid))
                for (moddata::Pop &p : *ps)
                    if (field_of(p, field) == std::optional<QString>(old)) {
                        if (field == u"type")
                            p.type = nw;
                        else if (field == u"culture")
                            p.culture = nw;
                        else
                            p.religion = nw;
                        ++n;
                    }
        return n;
    });
}

}  // namespace popcheck
