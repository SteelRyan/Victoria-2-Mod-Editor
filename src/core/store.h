// The stores of doc.maps (PLAN.md §4.2): one kind of file with its unapplied edits. In Python each is a class with
// snapshot / restore / dirty / save and a few optional abilities (partial_undo, part_keys, rebase, live_sig,
// undo_after_apply). Here: one interface, and an adapter that wraps a ported class as it is.
#pragma once
#include "core/ordered_map.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <memory>
#include <optional>
#include <variant>

namespace store {

// A key of a keyed picture: a province number (positions) or a text (files, names, nations …)
struct Key {
    std::variant<long long, QString> v;
    Key() = default;
    Key(long long i) : v(i) {}
    Key(const QString &s) : v(s) {}
    bool is_int() const { return std::holds_alternative<long long>(v); }
    long long num() const { return std::get<long long>(v); }
    const QString &text() const { return std::get<QString>(v); }
    QString str() const { return is_int() ? QString::number(num()) : text(); }   // Python's str(x)
    bool operator==(const Key &o) const = default;
    bool operator<(const Key &o) const { return v < o.v; }                       // (ints before texts)
};

// (in the key's own namespace: that is where QHash looks for it)
inline size_t qHash(const Key &k, size_t seed = 0) noexcept
{
    return k.is_int() ? ::qHash(k.num(), seed) : ::qHash(k.text(), seed ^ 0x9e3779b9u);
}

// A store's picture: compared by value, as Python compares its snapshots with ==
class SnapBase {
public:
    virtual ~SnapBase() = default;
    virtual bool equals(const SnapBase &o) const = 0;
};
using Snap = std::shared_ptr<const SnapBase>;
inline bool same(const Snap &a, const Snap &b)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    return a->equals(*b);
}

template <typename T>
class ValueSnap : public SnapBase {
public:
    explicit ValueSnap(T x) : v(std::move(x)) {}
    T v;
    bool equals(const SnapBase &o) const override
    {
        const auto *p = dynamic_cast<const ValueSnap<T> *>(&o);
        return p && p->v == v;
    }
};
template <typename T>
Snap box(T v) { return std::make_shared<const ValueSnap<T>>(std::move(v)); }
template <typename T>
const T &unbox(const Snap &s) { return dynamic_cast<const ValueSnap<T> &>(*s).v; }

// A keyed picture ({key: value} in Python, or a tuple of (key, value) pairs: `pairs`) — what partial undo works on
using Items = OrderedMap<Key, Snap>;
class ItemsSnap : public SnapBase {
public:
    ItemsSnap(Items i, bool p) : items(std::move(i)), pairs(p) {}
    Items items;
    bool pairs = false;
    bool equals(const SnapBase &o) const override
    {
        const auto *p = dynamic_cast<const ItemsSnap *>(&o);
        if (!p || p->items.size() != items.size())
            return false;
        for (const auto &[k, val] : items) {
            const Snap *q = p->items.get(k);
            if (!q || !same(*q, val))
                return false;
        }
        return true;
    }
};

class Store {
public:
    virtual ~Store() = default;
    virtual Snap snapshot() const = 0;
    virtual void restore(const Snap &s) = 0;
    virtual QStringList dirty() const = 0;                          // (Python's lists, as text)
    virtual std::optional<QStringList> save(const QString &backup_root) = 0;   // None, or the paths written
    virtual bool undo_after_apply() const { return false; }
    virtual bool partial_undo() const { return false; }
    virtual bool has_part_keys() const { return false; }
    virtual QSet<Key> part_keys(const Items &, const Items &) const { return {}; }
    virtual bool has_rebase() const { return false; }
    virtual Snap rebase_begin() { return nullptr; }
    virtual Snap rebase(const Snap &s, const Snap &) { return s; }
    virtual bool has_live_sig() const { return false; }
    virtual QString live_sig() const { return {}; }
    virtual QList<long long> deleted() const { return {}; }      // (the definitions store: provinces deleted)
    // the recovery file (recovery.py: hasattr(mf, "recovery_state")): the bitmaps give their changed pixels instead
    // of their picture (a version number); nullptr = None
    virtual bool has_recovery() const { return false; }
    virtual Snap recovery_snap() const { return nullptr; }
    virtual void recover_snap(const Snap &) {}
};
using StoreP = std::shared_ptr<Store>;

// ---------------------------------------------------------------------- the adapter for ported classes
namespace detail {
inline QStringList as_list(const QStringList &l) { return l; }
inline QStringList as_list(const QList<long long> &l)
{
    QStringList out;
    for (long long x : l)
        out << QString::number(x);
    return out;
}
inline std::optional<QStringList> as_saved(const std::optional<QString> &p)
{
    return p ? std::optional<QStringList>(QStringList{*p}) : std::nullopt;
}
inline std::optional<QStringList> as_saved(const std::optional<QStringList> &p) { return p; }
inline std::optional<QStringList> as_saved(const QStringList &p) { return p; }
template <typename T>
concept HasUndoAfterApply = requires { T::undo_after_apply; };
}  // namespace detail

template <typename T>
class StoreOf : public Store {
public:
    template <typename... A>
    explicit StoreOf(A &&...args) : obj(std::forward<A>(args)...) {}
    T obj;
    using SnapT = std::decay_t<decltype(std::declval<const T &>().snapshot())>;
    Snap snapshot() const override { return box<SnapT>(obj.snapshot()); }
    void restore(const Snap &s) override { obj.restore(unbox<SnapT>(s)); }
    QStringList dirty() const override { return detail::as_list(obj.dirty()); }
    std::optional<QStringList> save(const QString &backup_root) override { return detail::as_saved(obj.save(backup_root)); }
    bool undo_after_apply() const override
    {
        if constexpr (detail::HasUndoAfterApply<T>)
            return T::undo_after_apply;
        else
            return false;
    }
};

}  // namespace store
