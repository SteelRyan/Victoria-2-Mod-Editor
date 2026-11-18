// diplopanel.py — Diplomacy tab: relations, influence / spheres and treaties at the start.
//
// Pick a main nation (A): a click on the map when none is picked, a right-click any time (✕ / Esc clears it). Then
// every nation clicked is B: the relation between the two (both OOB files' values count), the great power's standing
// with the other one (only great powers — the top 8 westernized nations by prestige — have one; level 5 = in its
// sphere), puppets / substates and alliances (history/diplomacy).
//
// Two views (Relations | Status, like Armies | Navies), each with its own map and its own part of the panel:
// Relations — every nation filled in its relation with A (−200 red … 0 grey … +200 green), the value on a disc at its
// capital; Status — the fill is the diplomatic status with A (striped when there are several: e.g. a puppet in A's
// sphere), with the great powers' standing, influence and the treaties in the panel.
#pragma once
#include "core/diplomacy.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/oob.h"
#include "core/ordered_map.h"
#include "map/mapview_data.h"
#include "ui/mapview.h"
#include "ui/widgets.h"

#include <QVariantList>
#include <QWidget>
#include <functional>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTimer;

namespace dialogs {
class PickCombo;
}

namespace diplopanel {

using OptStr = std::optional<QString>;
using Rgb = std::tuple<int, int, int>;
using EditFn = std::function<void(oob::OOBEdits &, diplomacy::DiploEdits &)>;

struct Status {
    QString key;
    Rgb rgb;
    QString text;
};
extern const std::vector<Status> STATUS;     // the fill: diplomatic status with the main nation
extern const OrderedMap<QString, Rgb> STATUS_RGB;
extern const Rgb COL_OTHER;
extern const std::vector<std::pair<OptStr, QString>> SUBJECT_CHOICES;

Rgb relation_rgb(double v);
QStringList py_tags_with_units(const oob::OOBEdits &oob);   // oob.tags_with_units() in the walk order of Python's set
OrderedMap<QString, QStringList> statuses(const oob::OOBEdits &oob, const diplomacy::DiploEdits &dip, const QString &a);

struct WarRow {                              // wars(): (file, name, attackers, defenders, start, end, missing tags)
    QString rel, name;
    QStringList att, def;
    QString start, end;
    QStringList missing;
};

class DiploPanel : public QWidget {
    Q_OBJECT
public:
    DiploPanel(moddata::Mod &mod, mapview::MapData &mapdata, QWidget *parent = nullptr);
    static const std::vector<std::pair<QString, QString>> VIEWS;
    static const OrderedMap<QString, QString> MODE;
    moddata::Mod &mod;
    mapview::MapData &mapdata;
    document::Document *doc = nullptr;
    OptStr a, b;
    QStringList gps;
    QString view = QStringLiteral("relations");
    QString shown = QStringLiteral("map");
    bool _building = false;
    std::map<QString, double> _pending;
    std::optional<long long> _psig;
    QTimer *_timer;
    QWidget *web_view = nullptr;               // the view instead of the map (diploweb.DiploWebView, set by the window)
    std::function<void()> show_web;            // (the window's: web_view.related / show_for)

    std::vector<std::pair<QString, QPushButton *>> shown_btn, seg;
    dialogs::PickCombo *picker;
    QLabel *title, *sub, *hint, *gp_label, *pair_title, *rel_note, *lvl_title, *lvl_note, *other_treaties,
        *treaty_note, *list_title, *war_note, *legend_a, *legend_a2;
    QPushButton *clear_btn, *swap_btn;
    widgets::Section *pair_section, *list_section, *war_section, *legend_status, *legend_rel;
    QWidget *rel_box, *st_box, *infl_row;
    widgets::SliderField *rel, *infl;
    widgets::StepSlider *level;
    QComboBox *subject;
    QCheckBox *alliance;
    QListWidget *rel_list, *war_list;

    // --- the two views
    void set_view(const QString &key, bool emit = true);
    void sync_mode(const QString &mode);
    void _show_view();
    QLabel *_chip(widgets::GridFlow *gf, const std::optional<Rgb> &rgb, const QString &text);
    // --- helpers
    oob::OOBEdits *oob_map() const;
    diplomacy::DiploEdits *dip_map() const;
    QString cname(const QString &t) const;
    QString chip(const QString &t) const;
    // --- refresh
    void refresh(document::Document *doc, const OptStr &a, const OptStr &b);
    void set_shown(const QString &key);
    std::vector<WarRow> wars() const;
    void _fill_wars();
    void _fill();
    std::optional<std::pair<QString, QString>> _gp_side() const;
    void _fill_pair();
    QStringList _related(const QString &a) const;
    void _fill_list();
    // --- edits
    void _later(const QString &key, double value);
    void _flush();
    void _level(int lvl);
    void _subject();
    void _treaty(const QString &kind, bool on);
    // --- map
    std::vector<mapview::Shape> shapes() const;
    std::optional<QPointF> _cap(const QString &tag) const;
    void map_clicked(const QVariantList &tag);
    QString describe(const QVariantList &tag) const;
Q_SIGNALS:
    void edit(const QString &label, const diplopanel::EditFn &fn);   // undo label, fn(oob, diplomacy)
    void nationPicked(const QString &tag);     // the main nation (A); "" clears it
    void otherPicked(const QString &tag);      // B
    void overlayChanged();
    void viewChanged(const QString &key);      // "relations" / "status": the map mode goes with it
    void warRequested(const QString &rel);     // a war file ("" = the page): Warfare → Wars at the start
    void shownChanged(const QString &key);     // "map" / "web": what stands beside the panel
};

// ---------------------------------------------------------------------------------------------- relation edits
void set_entry(oob::OOBEdits &oob, const QString &x, const QString &y, const QString &key, const OptStr &value);
void set_relation(oob::OOBEdits &oob, const QString &a, const QString &b, long long v);

}  // namespace diplopanel
