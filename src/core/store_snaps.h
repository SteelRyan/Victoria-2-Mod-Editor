// The pictures Python sees as keyed ({key: value} or a tuple of (key, value) pairs — Document._items): for these
// stores the C++ picture is keyed too, so step_scope and partial undo see the same keys.
//   MembershipFile.snapshot() = {"names": …, "lists": …, "mods": …}
//   NewHistFiles.snapshot()   = tuple(sorted(made.items()))  — (pid, rel) pairs
// Included wherever StoreOf<MembershipFile> / StoreOf<NewHistFiles> is made (before it is used).
#pragma once
#include "core/history.h"
#include "core/mapfiles.h"
#include "core/store.h"

namespace store {

template <>
inline Snap StoreOf<mapfiles::MembershipFile>::snapshot() const
{
    const mapfiles::MembershipSnap s = obj.snapshot();
    Items it;
    it.insert(Key(QStringLiteral("names")), box(s.names));
    it.insert(Key(QStringLiteral("lists")), box(s.lists));
    it.insert(Key(QStringLiteral("mods")), box(s.mods));
    return std::make_shared<const ItemsSnap>(it, false);
}

template <>
inline void StoreOf<mapfiles::MembershipFile>::restore(const Snap &s)
{
    const Items &it = dynamic_cast<const ItemsSnap &>(*s).items;
    mapfiles::MembershipSnap m;
    m.names = unbox<QStringList>(*it.get(Key(QStringLiteral("names"))));
    m.lists = unbox<OrderedMap<QString, std::vector<long long>>>(*it.get(Key(QStringLiteral("lists"))));
    m.mods = unbox<OrderedMap<QString, mapfiles::Mods>>(*it.get(Key(QStringLiteral("mods"))));
    obj.restore(m);
}

template <>
inline Snap StoreOf<history::NewHistFiles>::snapshot() const
{
    Items it;
    for (const auto &[pid, rel] : obj.snapshot())
        it.insert(Key(pid), box(rel));
    return std::make_shared<const ItemsSnap>(it, true);
}

template <>
inline void StoreOf<history::NewHistFiles>::restore(const Snap &s)
{
    std::vector<std::pair<long long, QString>> v;
    for (const auto &[k, val] : dynamic_cast<const ItemsSnap &>(*s).items)
        v.emplace_back(k.num(), unbox<QString>(val));
    obj.restore(v);
}

}  // namespace store
