// Python's dict: walks in the order keys were first put in; a deleted key put in again goes to the end
// (PLAN.md §4.4 — wherever a dict's order can reach a file or the screen, this is used, never QHash / std::map).
#pragma once
#include <QHash>
#include <QString>
#include <optional>
#include <utility>
#include <vector>

// a text or None as a key (Python: dict keys that may be None) — in QString's namespace, where QHash looks
inline size_t qHash(const std::optional<QString> &o, size_t seed = 0) noexcept
{
    return o ? qHash(*o, seed) : (seed ^ size_t(0x51ed27u));
}

template <typename K, typename V>
class OrderedMap {
public:
    using Item = std::pair<K, V>;

    OrderedMap() = default;
    OrderedMap(std::initializer_list<Item> items)     // a dict literal {k: v, …} (a repeated key: last value, first place)
    {
        for (const Item &i : items)
            insert(i.first, i.second);
    }

    V &operator[](const K &k)                         // d[k] (made with V() when missing, like setdefault(k, V()))
    {
        auto it = idx_.find(k);
        if (it != idx_.end())
            return items_[size_t(*it)].second;
        idx_.insert(k, qsizetype(items_.size()));
        items_.emplace_back(k, V());
        return items_.back().second;
    }
    void insert(const K &k, V v)                      // d[k] = v (an existing key keeps its place)
    {
        (*this)[k] = std::move(v);
    }
    const V *get(const K &k) const                    // d.get(k) — nullptr when missing
    {
        auto it = idx_.find(k);
        return it == idx_.end() ? nullptr : &items_[size_t(*it)].second;
    }
    V *get(const K &k)
    {
        auto it = idx_.find(k);
        return it == idx_.end() ? nullptr : &items_[size_t(*it)].second;
    }
    V value(const K &k, V fallback = V()) const       // d.get(k, fallback)
    {
        const V *p = get(k);
        return p ? *p : fallback;
    }
    bool contains(const K &k) const { return idx_.contains(k); }
    bool remove(const K &k)                           // del d[k] / d.pop(k, None)
    {
        auto it = idx_.find(k);
        if (it == idx_.end())
            return false;
        const qsizetype at = *it;
        items_.erase(items_.begin() + at);
        idx_.erase(it);
        for (auto &i : idx_)
            if (i > at)
                --i;
        return true;
    }
    void clear()
    {
        items_.clear();
        idx_.clear();
    }
    qsizetype size() const { return qsizetype(items_.size()); }
    bool isEmpty() const { return items_.empty(); }
    auto begin() { return items_.begin(); }
    auto end() { return items_.end(); }
    auto begin() const { return items_.begin(); }
    auto end() const { return items_.end(); }
    bool operator==(const OrderedMap &o) const        // as Python's dict ==: same keys and values, ORDER IGNORED
    {                                                 // (undo snapshots compare this way: no extra step from order)
        if (items_.size() != o.items_.size())
            return false;
        for (const Item &i : items_) {
            const V *p = o.get(i.first);
            if (!p || !(*p == i.second))
                return false;
        }
        return true;
    }
private:
    std::vector<Item> items_;
    QHash<K, qsizetype> idx_;
};
