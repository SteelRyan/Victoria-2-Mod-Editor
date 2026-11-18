// The recovery file's own form (recovery.py pickles; the port writes its pictures with this): a binary stream of
// the values the stores' snapshots hold. Each struct names its fields once (SNAPIO_FIELDS); a snapshot (store::Snap)
// is written with the name its type was registered under, so the reader knows what to make of it. Shared pointers are
// written once and shared again when read (as pickle's memo keeps them).
// Registered: core types in snapio.cpp (register_core), the map's in map/snapio_map.cpp (register_map).
#pragma once
#include "core/ordered_map.h"
#include "core/store.h"

#include <QByteArray>
#include <QDataStream>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace snapio {

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Writer {
public:
    explicit Writer(QDataStream &s) : s(s) {}
    QDataStream &s;
    std::unordered_map<const void *, long long> memo;     // shared object -> its number (1, 2, …)
};
class Reader {
public:
    explicit Reader(QDataStream &s) : s(s) {}
    QDataStream &s;
    std::vector<std::shared_ptr<const void>> memo;         // number - 1 -> the object read
    void check() const
    {
        if (s.status() != QDataStream::Ok)
            throw Error("the recovery file is cut short or damaged");
    }
};

template <typename T>
struct Fields;                                             // each(T &, f): f(x.a, x.b, …) — SNAPIO_FIELDS

template <typename T>
void put(Writer &w, const T &x);
template <typename T>
T get(Reader &r);

// put(Writer &, const T &) / get(Reader &) -> T. The plain template: a struct with SNAPIO_FIELDS (declared before
// anything that writes it); the specializations below: plain values and containers.
template <typename T, typename = void>
struct Codec {
    static void put(Writer &w, const T &x)
    {
        Fields<T>::each(const_cast<T &>(x), [&](auto &...m) { (snapio::put(w, m), ...); });
    }
    static T get(Reader &r)
    {
        T x{};
        Fields<T>::each(x, [&](auto &...m) { ((m = snapio::get<std::decay_t<decltype(m)>>(r)), ...); });
        return x;
    }
};
// (inside namespace snapio)
#define SNAPIO_FIELDS(T, ...)                                                                                          \
    template <>                                                                                                        \
    struct Fields<T> {                                                                                                 \
        template <typename F>                                                                                          \
        static void each(T &x, F &&f)                                                                                  \
        {                                                                                                              \
            f(__VA_ARGS__);                                                                                            \
        }                                                                                                              \
    };

template <typename T>
void put(Writer &w, const T &x) { Codec<T>::put(w, x); }
template <typename T>
T get(Reader &r) { return Codec<T>::get(r); }

// ---- plain values
template <>
struct Codec<bool> {
    static void put(Writer &w, bool x) { w.s << quint8(x ? 1 : 0); }
    static bool get(Reader &r)
    {
        quint8 b = 0;
        r.s >> b;
        r.check();
        return b != 0;
    }
};
template <typename T>
struct Codec<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
    static void put(Writer &w, T x) { w.s << qint64(x); }
    static T get(Reader &r)
    {
        qint64 v = 0;
        r.s >> v;
        r.check();
        return static_cast<T>(v);
    }
};
template <>
struct Codec<double> {
    static void put(Writer &w, double x) { w.s << x; }
    static double get(Reader &r)
    {
        double v = 0;
        r.s >> v;
        r.check();
        return v;
    }
};
template <>
struct Codec<QString> {
    static void put(Writer &w, const QString &x) { w.s << x; }
    static QString get(Reader &r)
    {
        QString v;
        r.s >> v;
        r.check();
        return v;
    }
};
template <>
struct Codec<QByteArray> {
    static void put(Writer &w, const QByteArray &x) { w.s << x; }
    static QByteArray get(Reader &r)
    {
        QByteArray v;
        r.s >> v;
        r.check();
        return v;
    }
};

// ---- containers
inline void put_count(Writer &w, size_t n) { w.s << qint64(n); }
inline size_t get_count(Reader &r)
{
    qint64 n = -1;
    r.s >> n;
    r.check();
    if (n < 0 || n > (qint64(1) << 34))
        throw Error("the recovery file is damaged (a count out of range)");
    return size_t(n);
}

template <typename T>
struct Codec<std::optional<T>> {
    static void put(Writer &w, const std::optional<T> &x)
    {
        snapio::put(w, x.has_value());
        if (x)
            snapio::put(w, *x);
    }
    static std::optional<T> get(Reader &r)
    {
        if (!snapio::get<bool>(r))
            return std::nullopt;
        return snapio::get<T>(r);
    }
};
template <typename A, typename B>
struct Codec<std::pair<A, B>> {
    static void put(Writer &w, const std::pair<A, B> &x)
    {
        snapio::put(w, x.first);
        snapio::put(w, x.second);
    }
    static std::pair<A, B> get(Reader &r)
    {
        A a = snapio::get<A>(r);
        B b = snapio::get<B>(r);
        return {std::move(a), std::move(b)};
    }
};
template <typename... T>
struct Codec<std::tuple<T...>> {
    static void put(Writer &w, const std::tuple<T...> &x)
    {
        std::apply([&](const auto &...m) { (snapio::put(w, m), ...); }, x);
    }
    static std::tuple<T...> get(Reader &r)
    {
        std::tuple<T...> x;
        std::apply([&](auto &...m) { ((m = snapio::get<std::decay_t<decltype(m)>>(r)), ...); }, x);
        return x;
    }
};
template <typename T>
struct Codec<std::vector<T>> {
    static void put(Writer &w, const std::vector<T> &x)
    {
        put_count(w, x.size());
        if constexpr (std::is_same_v<T, std::uint8_t> || std::is_same_v<T, std::uint16_t> ||
                      std::is_same_v<T, std::int64_t>)
            w.s.writeRawData(reinterpret_cast<const char *>(x.data()), int(x.size() * sizeof(T)));
        else
            for (const T &e : x)
                snapio::put(w, e);
    }
    static std::vector<T> get(Reader &r)
    {
        const size_t n = get_count(r);
        std::vector<T> x;
        if constexpr (std::is_same_v<T, std::uint8_t> || std::is_same_v<T, std::uint16_t> ||
                      std::is_same_v<T, std::int64_t>) {
            x.resize(n);
            if (r.s.readRawData(reinterpret_cast<char *>(x.data()), int(n * sizeof(T))) != int(n * sizeof(T)))
                throw Error("the recovery file is cut short");
        } else {
            x.reserve(n);
            for (size_t i = 0; i < n; ++i)
                x.push_back(snapio::get<T>(r));
        }
        return x;
    }
};
template <typename T>
struct Codec<QList<T>> {
    static void put(Writer &w, const QList<T> &x)
    {
        put_count(w, size_t(x.size()));
        for (const T &e : x)
            snapio::put(w, e);
    }
    static QList<T> get(Reader &r)
    {
        const size_t n = get_count(r);
        QList<T> x;
        x.reserve(qsizetype(n));
        for (size_t i = 0; i < n; ++i)
            x.push_back(snapio::get<T>(r));
        return x;
    }
};
// (QStringList is QList<QString> in Qt 6: the QList codec)
template <typename T>
struct Codec<QSet<T>> {
    static void put(Writer &w, const QSet<T> &x)
    {
        put_count(w, size_t(x.size()));
        for (const T &e : x)
            snapio::put(w, e);
    }
    static QSet<T> get(Reader &r)
    {
        const size_t n = get_count(r);
        QSet<T> x;
        for (size_t i = 0; i < n; ++i)
            x.insert(snapio::get<T>(r));
        return x;
    }
};
template <typename T>
struct Codec<std::set<T>> {
    static void put(Writer &w, const std::set<T> &x)
    {
        put_count(w, x.size());
        for (const T &e : x)
            snapio::put(w, e);
    }
    static std::set<T> get(Reader &r)
    {
        const size_t n = get_count(r);
        std::set<T> x;
        for (size_t i = 0; i < n; ++i)
            x.insert(snapio::get<T>(r));
        return x;
    }
};
template <typename K, typename V>
struct Codec<OrderedMap<K, V>> {
    static void put(Writer &w, const OrderedMap<K, V> &x)
    {
        put_count(w, size_t(x.size()));
        for (const auto &[k, v] : x) {
            snapio::put(w, k);
            snapio::put(w, v);
        }
    }
    static OrderedMap<K, V> get(Reader &r)
    {
        const size_t n = get_count(r);
        OrderedMap<K, V> x;
        for (size_t i = 0; i < n; ++i) {
            K k = snapio::get<K>(r);
            V v = snapio::get<V>(r);
            x.insert(k, std::move(v));
        }
        return x;
    }
};
template <typename K, typename V>
struct Codec<std::map<K, V>> {
    static void put(Writer &w, const std::map<K, V> &x)
    {
        put_count(w, x.size());
        for (const auto &[k, v] : x) {
            snapio::put(w, k);
            snapio::put(w, v);
        }
    }
    static std::map<K, V> get(Reader &r)
    {
        const size_t n = get_count(r);
        std::map<K, V> x;
        for (size_t i = 0; i < n; ++i) {
            K k = snapio::get<K>(r);
            x[k] = snapio::get<V>(r);
        }
        return x;
    }
};

// a shared object: written the first time, then only its number
template <typename T>
void put_shared(Writer &w, const std::shared_ptr<T> &p, const std::function<void(const T &)> &write)
{
    if (!p) {
        w.s << qint64(0);
        return;
    }
    const void *key = p.get();
    if (auto it = w.memo.find(key); it != w.memo.end()) {
        w.s << qint64(it->second);
        return;
    }
    const long long id = static_cast<long long>(w.memo.size()) + 1;
    w.memo[key] = id;
    w.s << qint64(-id);                                    // (negative: the object follows)
    write(*p);
}
template <typename T>
std::shared_ptr<T> get_shared(Reader &r, const std::function<std::shared_ptr<T>()> &read)
{
    qint64 id = 0;
    r.s >> id;
    r.check();
    if (id == 0)
        return nullptr;
    if (id > 0) {
        if (size_t(id) > r.memo.size())
            throw Error("the recovery file is damaged (a shared value it has not given)");
        return std::const_pointer_cast<T>(std::static_pointer_cast<const T>(r.memo[size_t(id) - 1]));
    }
    if (size_t(-id) != r.memo.size() + 1)
        throw Error("the recovery file is damaged (shared values out of order)");
    r.memo.push_back(nullptr);                             // (its place, before what it holds is read)
    std::shared_ptr<T> p = read();
    r.memo[size_t(-id) - 1] = p;
    return p;
}
template <typename T>
struct Codec<std::shared_ptr<const T>> {
    static void put(Writer &w, const std::shared_ptr<const T> &p)
    {
        put_shared<const T>(w, p, [&](const T &x) { snapio::put(w, x); });
    }
    static std::shared_ptr<const T> get(Reader &r)
    {
        return get_shared<const T>(r, [&] { return std::make_shared<const T>(snapio::get<T>(r)); });
    }
};

// ---- snapshots: by the name their type was registered under
struct SnapCodec {
    QString name;
    std::function<void(Writer &, const store::SnapBase &)> put;
    std::function<store::Snap(Reader &)> get;
};
void add(const std::type_index &type, SnapCodec codec);
template <typename T>
void reg(const QString &name)                              // a store::box(T)
{
    add(typeid(store::ValueSnap<T>),
        {name, [](Writer &w, const store::SnapBase &s) { snapio::put(w, static_cast<const store::ValueSnap<T> &>(s).v); },
         [](Reader &r) -> store::Snap { return store::box(snapio::get<T>(r)); }});
}
void put_snap(Writer &w, const store::Snap &s);
store::Snap get_snap(Reader &r);
template <>
struct Codec<store::Snap> {
    static void put(Writer &w, const store::Snap &s) { put_snap(w, s); }
    static store::Snap get(Reader &r) { return get_snap(r); }
};

void register_core();                                      // (once; the map's: map/snapio_map.h)

}  // namespace snapio
