// snapio.h — the registry of snapshot types, keyed pictures, and the core's types.
#include "core/snapio_types.h"

#include <typeinfo>

using namespace Qt::StringLiterals;

namespace snapio {

static std::map<std::type_index, SnapCodec> &by_type()
{
    static std::map<std::type_index, SnapCodec> m;
    return m;
}
static std::map<QString, const SnapCodec *> &by_name()
{
    static std::map<QString, const SnapCodec *> m;
    return m;
}

void add(const std::type_index &type, SnapCodec codec)
{
    auto &t = by_type();
    if (const auto it = by_name().find(codec.name); it != by_name().end() && it->second != &t[type])
        throw Error("two picture types registered as " + codec.name.toStdString());
    t[type] = std::move(codec);
    by_name()[t[type].name] = &t[type];
}

static const QString ITEMS = u"items"_s;

void put_snap(Writer &w, const store::Snap &s)
{
    if (!s) {
        put(w, QString());                                 // (None)
        return;
    }
    if (const auto *it = dynamic_cast<const store::ItemsSnap *>(s.get())) {   // a keyed picture: its values each
        put(w, ITEMS);
        put(w, it->pairs);
        put_count(w, size_t(it->items.size()));
        for (const auto &[k, v] : it->items) {
            put(w, k.is_int());
            if (k.is_int())
                put(w, k.num());
            else
                put(w, k.text());
            put_snap(w, v);
        }
        return;
    }
    const auto f = by_type().find(std::type_index(typeid(*s)));
    if (f == by_type().end())
        throw Error(std::string("a picture the recovery file can't hold yet: ") + typeid(*s).name());
    put(w, f->second.name);
    f->second.put(w, *s);
}

store::Snap get_snap(Reader &r)
{
    const QString name = get<QString>(r);
    if (name.isEmpty())
        return nullptr;
    if (name == ITEMS) {
        const bool pairs = get<bool>(r);
        const size_t n = get_count(r);
        store::Items items;
        for (size_t i = 0; i < n; ++i) {
            const bool is_int = get<bool>(r);
            const store::Key k = is_int ? store::Key(get<long long>(r)) : store::Key(get<QString>(r));
            items.insert(k, get_snap(r));
        }
        return std::make_shared<const store::ItemsSnap>(std::move(items), pairs);
    }
    const auto f = by_name().find(name);
    if (f == by_name().end())
        throw Error("the recovery file holds a picture this editor doesn't know: " + name.toStdString());
    return f->second->get(r);
}

void register_core()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    reg<int>(u"int"_s);
    reg<long long>(u"i64"_s);
    reg<QString>(u"str"_s);
    reg<QStringList>(u"strs"_s);
    reg<QSet<long long>>(u"set.i64"_s);
    reg<QSet<QString>>(u"set.str"_s);
    reg<std::pair<QString, QString>>(u"pair.str.str"_s);
    reg<OrderedMap<QString, QString>>(u"map.str.str"_s);
    reg<OrderedMap<QString, std::vector<long long>>>(u"map.str.i64s"_s);
    reg<OrderedMap<QString, mapfiles::Mods>>(u"map.str.mods"_s);
    reg<std::pair<QSet<long long>, QSet<QString>>>(u"pair.seti64.setstr"_s);
    reg<std::vector<std::pair<long long, QString>>>(u"hist_new"_s);
    reg<adjacency::AdjacencyFile::Snap>(u"adjacency"_s);
    reg<mapfiles::DefaultMapFile::Snap>(u"default_map.2"_s);   // (2026-11-08: + the definitions value)
    reg<countryhist::NewNation>(u"new_nation"_s);
    reg<countryhist::NationState>(u"nation_state"_s);
    reg<oob::OOB>(u"oob"_s);
    reg<std::pair<std::vector<diplomacy::Record>, long long>>(u"diplomacy"_s);
    reg<production::ProdSnap>(u"production"_s);
    reg<techdata::TechSnap>(u"techs"_s);
    add(typeid(positions::EntrySnap),
        {u"positions.entry"_s,
         [](Writer &w, const store::SnapBase &s) { put(w, static_cast<const positions::EntrySnap &>(s).e); },
         [](Reader &r) -> store::Snap { return std::make_shared<const positions::EntrySnap>(get<positions::EntryP>(r)); }});
}

}  // namespace snapio
