// nationextras.py, its data half (PLAN.md §4.1): doc.maps["flags"] — flags made in this session ({file name: QImage})
// and flags taken out (`gone`); Apply writes the new ones into gfx/flags and deletes the others (what is replaced or
// deleted is backed up). The parties / colour / flag-maker views are kind D.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/store.h"

#include <QImage>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>

namespace nationextras {

inline constexpr int FLAG_W = 93, FLAG_H = 64;

class FlagEdits : public store::Store {
public:
    static constexpr const char *kind = "flags";
    explicit FlagEdits(moddata::Mod &mod) : mod(mod) {}
    moddata::Mod &mod;
    OrderedMap<QString, QImage> fresh;            // (Python: new)
    QSet<QString> gone;
    bool _own(const QString &name) const;
    std::optional<QImage> image(const QString &name) const;
    void put(const OrderedMap<QString, QImage> &files);
    void drop(const QStringList &names);
    struct Snap {
        OrderedMap<QString, QImage> fresh;
        QStringList gone;                         // sorted (Python: snap[GONE] = tuple(sorted(gone)), when any)
        bool operator==(const Snap &o) const = default;
    };
    store::Snap snapshot() const override;
    void restore(const store::Snap &s) override;
    QStringList dirty() const override;
    std::optional<QStringList> save(const QString &backup_root) override;
};

}  // namespace nationextras
