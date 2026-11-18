// newprov.py, its data half (PLAN.md §4.1): new provinces — definition.csv row, history file, localisation,
// default.map, state, continent, climate — and, once painted, what it took from its neighbours. ProvinceDefs is
// doc.maps["definitions"]. The dialog (NewProvinceDialog) is kind C: ui\.
// A new province takes the next id and the colour of the first spare row at the end of definition.csv (rows
// without an id), or a colour that neither a province nor any pixel of provinces.bmp uses (numpy's default_rng).
#pragma once
#include "core/adjacency.h"
#include "core/document.h"
#include "core/history.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/positions.h"
#include "core/store.h"
#include "map/mapview_data.h"
#include "map/provbmp.h"

#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <vector>

namespace newprov {

using moddata::Rgb;
using OptStr = std::optional<QString>;
extern const QStringList BUILDING_KEYS;
extern const QRegularExpression BAD_FILE_CHARS;

struct Info {                                  // NewProv.info: what the template's history gives (Python: sorted pairs)
    OptStr owner, controller;
    QStringList cores;
    std::optional<long long> life_rating;
    OptStr trade_goods;
    bool operator==(const Info &o) const = default;
};
struct NewProv {                               // namedtuple("NewProv", "name rgb sea template hist_rel hist_text info done_version done_area")
    QString name;
    Rgb rgb;
    bool sea = false;
    std::optional<long long> tmpl;             // (Python: template)
    OptStr hist_rel, hist_text;
    Info info;
    std::optional<long long> done_version;
    long long done_area = 0;
    bool operator==(const NewProv &o) const = default;
};
struct Deleted {                               // (Province, its history files or None, its heir)
    moddata::Province pr;
    std::optional<std::vector<history::HistFileP>> files;
    std::optional<long long> heir;
    bool operator==(const Deleted &o) const = default;
};
struct DefsSnap {
    OrderedMap<long long, NewProv> added;
    OrderedMap<long long, Deleted> gone;
    bool operator==(const DefsSnap &o) const = default;
};

class ProvinceDefs : public store::Store {
public:
    static constexpr const char *kind = "definitions";
    ProvinceDefs(moddata::Mod &mod, mapview::MapData &md, history::ProvinceHistory &hist);
    moddata::Mod &mod;
    mapview::MapData &md;
    history::ProvinceHistory &hist;
    QString path;
    OrderedMap<long long, NewProv> added;      // pid -> NewProv
    OrderedMap<long long, Deleted> gone;       // (Python: deleted) pid -> deleted, not saved yet
    std::vector<std::function<void()>> listeners;   // fn() after provinces were added / removed
    std::optional<QString> hash;

    store::Snap snapshot() const override { return store::box(DefsSnap{added, gone}); }
    void restore(const store::Snap &snap) override;
    QStringList dirty() const override;
    QList<long long> deleted() const override;
    std::optional<QStringList> save(const QString &backup_root) override;
    std::optional<QString> save_file(const QString &backup_root);

    void del(const QList<long long> &pids, const OrderedMap<long long, long long> &heirs = {});   // (Python: delete)
    void _sync_deleted(const OrderedMap<long long, Deleted> &old);
    void add(long long pid, const NewProv &rec);
    void update(long long pid, std::optional<long long> done_version, long long done_area);
    void _sync(const OrderedMap<long long, NewProv> &old, bool tell = false);

    long long next_id() const;
    const std::vector<bool> &file_colours();
    std::vector<Rgb> spare_rows() const;
    std::pair<Rgb, QString> pick_colour();
    std::optional<QString> _save_localisation(const QString &bdir);

private:
    std::optional<std::vector<bool>> _file_colours;   // 2**24 colours: used by a pixel of provinces.bmp on disk
};

std::optional<Rgb> _spare_rgb(const QString &line);
QString history_without_buildings(const QString &text);
std::pair<qsizetype, qsizetype> _line_span(const QString &text, qsizetype s, qsizetype e);
QString safe_file_name(const QString &name);

struct Plan {                                  // (pid, NewProv, state, continent, climate, want_state)
    long long pid;
    NewProv rec;
    OptStr rid, continent, climate;
    std::optional<history::ProvState> want;
};
Plan plan(document::Document &doc, ProvinceDefs &defs, const QString &name, bool sea, std::optional<long long> tmpl);
QString _history_folder_near(const history::ProvinceHistory &hist, long long pid);

OrderedMap<long long, long long> pixel_donors(const provbmp::ProvinceBitmap &bmp, long long pid,
                                              std::optional<long long> since = std::nullopt);
std::pair<QString, QString> new_status(const ProvinceDefs &defs, const provbmp::ProvinceBitmap &bmp,
                                       const mapview::MapData &md, long long pid);
std::vector<std::pair<long long, double>> pop_shares(const document::Document &doc, const mapview::MapData &md,
                                                     const OrderedMap<long long, long long> &donors);

// Document.create_province / finish_province (they need ProvinceDefs, so they live here)
void create_province(document::Document &doc, long long pid, const NewProv &rec, const OptStr &rid = std::nullopt,
                     const OptStr &continent = std::nullopt, const OptStr &climate = std::nullopt,
                     const std::optional<history::ProvState> &want = std::nullopt);
long long finish_province(document::Document &doc, long long pid, const std::vector<std::pair<long long, double>> &moves,
                          const OrderedMap<long long, positions::Entry> &entries,
                          const std::vector<adjacency::RowFields> &add_specs, const QList<long long> &stale_uids,
                          std::optional<long long> done_version, long long done_area);

}  // namespace newprov
