// moreviews.py, its data half (PLAN.md §4.1): doc.maps["assets"] — files added in this session (from the user's
// library, an import, a lengthened icon strip): {rel in the mod: ("img", QImage) / ("strip", QImage) / ("file", path)
// / ("pic", …)}; Apply writes / copies them into the mod (what they replace is backed up). Also a war file of
// history/wars read into its sides and goals (read_war). The pages are kind D.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/store.h"
#include "map/fourk.h"

#include <QImage>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

namespace moreviews {

// A picture waiting for Apply: its pixels and what kind of file it is (Python: lookpages.Pic — one object, compared by
// being the same one: the undo steps compare what is pending, and pixel arrays do not compare as a whole).
struct PicData {
    fourk::Pic rgba;
    fourk::Info info;
};
using PicP = std::shared_ptr<const PicData>;

struct Asset {                                    // (how, what)
    QString how;                                  // "img" / "strip" / "file" / "pic"
    QImage img;                                   // img, strip
    QString path;                                 // file: the library file
    PicP pic;                                     // pic
    bool operator==(const Asset &o) const { return how == o.how && img == o.img && path == o.path && pic == o.pic; }
};

class AssetEdits : public store::Store {
public:
    static constexpr const char *kind = "assets";
    explicit AssetEdits(moddata::Mod &mod) : mod(mod) {}
    moddata::Mod &mod;
    OrderedMap<QString, Asset> fresh;             // (Python: new)
    store::Snap snapshot() const override { return store::box(fresh); }
    void restore(const store::Snap &s) override { fresh = store::unbox<OrderedMap<QString, Asset>>(s); }
    QStringList dirty() const override;
    std::optional<QStringList> save(const QString &backup_root) override;
};

QString safe_stem(const QString &name);
QImage fit(const QImage &img, int w, int h);

// ---------------------------------------------------------------------------------------------- wars at the start
extern const QRegularExpression DATE_RE;
QString unq(const QString &v);             // unq(v) of a str value
struct War {                               // read_war's dict
    QString name, start, end;
    QStringList att, def;
    std::vector<std::tuple<QString, QString, QString>> goals;   // (casus_belli, actor, receiver)
    bool simple = true;
};
War read_war(const QString &text);
QString war_text(const War &w);

}  // namespace moreviews
