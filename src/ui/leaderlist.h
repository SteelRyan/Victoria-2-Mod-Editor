// leaderlist — Armies → Generals and Navies → Admirals (C++ only, a tester 2026-11-17: "we don't have any listing for
// generals and admirals"). Every general (or admiral) at the start: the nation's pool (top-level `leader = { … }` blocks
// of its history/units file, oob::OOB::leaders) and the ones leading an army / navy (the leader block inside it).
//
// Which nations (the user): the picked nations — a click on the map picks one, Ctrl+click adds / takes one away (one
// selection, 2026-11-18: "no differentiation between picked nation and selected on a map"), outlined red on the map —
// or the whole world. A row shows the name, the nation, the attack and defence the two traits give together, and the other
// stats; picking a row opens it below to edit (name, date, personality, background, prestige) — each change one
// undoable step, written on Apply. New / copy / delete; the traits' odds are on Warfare → Leader traits.
#pragma once
#include "core/document.h"
#include "core/moddata.h"
#include "core/oob.h"
#include "core/traits.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QWidget>
#include <functional>
#include <optional>
#include <tuple>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QCheckBox;
namespace widgets { class Stepper; }

namespace leaderlist {

class CountBars;                            // the statistics: how many leaders have each value

using EditFn = std::function<void(oob::OOB &)>;

struct Row {
    QString tag;
    int where = 0;                          // 0 the pool, 1 an army, 2 a navy
    long long index = 0;                    // in the pool / the army or navy's index
    QHash<QString, QString> f;              // name date type personality background prestige
    QString unit;                           // the army / navy it leads
    double atk = 0, def = 0;
    QHash<QString, double> st;              // every stat of the two traits together (traits::STATS)
    bool operator==(const Row &o) const = default;
};

class LeaderList : public QWidget {
    Q_OBJECT
public:
    explicit LeaderList(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    oob::OOBEdits *oob = nullptr;
    std::optional<QString> tag;             // the picked nation
    QString kind = QStringLiteral("land");  // land / sea
    QString scope = QStringLiteral("picked");   // picked / world
    QSet<QString> picked;                   // the picked nations (empty: the nation of the page)
    std::function<void(const QString &label, const QString &tag, const EditFn &fn)> edit_fn;

    void set_kind(const QString &k);
    void refresh(document::Document *d, oob::OOBEdits *o, const std::optional<QString> &t);
    void toggle_pick(const QString &t);     // Ctrl+click on the map
    void pick_only(const QString &t);       // a plain click: this nation alone
    QStringList tags_shown() const;         // the nations whose leaders are listed (the map lights them up)

    // (for the check)
    std::vector<Row> rows;
    QTreeWidget *tree;
    QLineEdit *name, *date, *prestige, *filter;
    QComboBox *pers, *back;
    QPushButton *new_btn, *copy_btn, *del_btn, *traits_btn;
    std::vector<std::pair<QString, QPushButton *>> scope_btns;
    QLabel *head, *count, *picked_note, *where, *warn, *combined;
    // the statistics of the leaders shown (the user 2026-11-18: "how many 5 attacks they have, how many 4 attacks …
    // the general with the highest … attack plus defence")
    QLabel *stats_head;
    QComboBox *count_by, *best_by;
    CountBars *bars;
    QTreeWidget *best;
    std::vector<size_t> shown_rows;         // the rows the list shows (after the filter)
    static double value(const Row &r, const QString &stat);   // a stat, or "attack+defence"
    // make several at once (the user 2026-11-18: "a lot of 1-1s … I might pump out 10 of them at one time … go to the
    // cultures list and auto-generate the names … one name list, one surname list, just mix them")
    QPushButton *gen_open, *gen_go;
    QWidget *gen_box;
    widgets::Stepper *gen_n, *gen_atk, *gen_def;
    QComboBox *gen_pers, *gen_back;
    QCheckBox *gen_only;
    QLabel *gen_note;
    void generate();
    // a culture's first and last names (common/cultures.txt; none of its own: its group's)
    static std::pair<QStringList, QStringList> culture_names(const moddata::Mod &mod, const QString &culture);
    void select(const QString &tag, int where, long long index);
    void commit(const QString &field, const QString &value);
    void new_leader();
    void copy_leader();
    void delete_leader();
Q_SIGNALS:
    void traitsRequested();                 // → Warfare → Leader traits
    void scopeChanged();                    // (the map lights up other nations)

private:
    traits::Lists _traits;
    std::optional<QString> _traits_sig;
    std::optional<std::tuple<int, QString, long long>> _cur;   // where, tag, index of the row being edited
    bool _filling = false;
    void _load_traits();
    QString _tname(const QString &key) const;
    void _fill_combos();
    void _fill();
    void _stats();
    void _gen_note();
    QString _culture_of(const QString &tag) const;
    void _picked_row();
    const Row *_row() const;
    QString _who() const;
};

}  // namespace leaderlist
