// traitodds — Warfare → Leader traits (C++ only, the tester and the user 2026-11-17).
//
// The page has three views: "Odds & mass edit" (this file), "Names & colours" (traitnames: the names in the game from the
// stats, coloured by rules) and "One trait" (the entry view it always had).
//
// The user's words: MP mods use one list (personalities) for attack and the other (backgrounds) for defence, or one for
// attack + defence and the other for the bonus stats — "I need the ability to edit and see one at a time, in mass, but
// to overlay and see their chances in how they combine to generate one general". So: one list at a time in a table
// (pick many rows, set / add / clear a stat for all of them, copy them to make them more likely, delete them), and the
// odds drawn beside it: that list's values, the other list's, the two together (a new leader gets one of each), and a
// grid of attack × defence together (any two stats can be picked).
//
// Every trait of a list counts as equally likely (the user's rule; NOT measured in the game). no_personality /
// no_background are never counted (the user 2026-11-18: "it should never be counted").
#pragma once
#include "core/document.h"
#include "core/moddata.h"
#include "core/traits.h"

#include <QSet>
#include <QString>
#include <QWidget>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
namespace traitnames { class NamesView; }
class QTreeWidget;
namespace entryview {
class EntryView;
}
namespace widgets {
class Stepper;
}

namespace traitodds {

class OddsChart : public QWidget {            // the bars, or (grid_part) the grid: two of them on the page
    Q_OBJECT
public:
    explicit OddsChart(bool grid_part = false, QWidget *parent = nullptr);
    bool grid_part = false;
    QString lists = QStringLiteral("both");   // the first bars: "both", or only "personality" / "background"
    QString list, other, stat, sx, sy;
    traits::Dist one, two, both;
    traits::Dist2 grid;
    long long n_one = 0, n_two = 0;
    void set(const traits::Lists &l, const QString &list, const QString &stat, const QString &sx, const QString &sy);
    QSize sizeHint() const override;
Q_SIGNALS:
    void barClicked(double value);            // a bar of the edited list: its traits get picked

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;

private:
    std::vector<std::pair<QRectF, long long>> _bars;          // (the edited list's bars, value key)
    std::vector<std::tuple<QRectF, QString>> _tips;
    int _height() const;
};

class OddsView : public QWidget {
    Q_OBJECT
public:
    explicit OddsView(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    QString list = QStringLiteral("personality");
    traits::Lists lists;
    QSet<QString> sel;                        // the picked traits (keys)
    void refresh(document::Document *d);

    // (for the check)
    std::vector<std::pair<QString, QPushButton *>> list_btns;
    QTreeWidget *table;
    QComboBox *stat_box, *chart_stat, *bar_lists, *grid_x, *grid_y;
    QLineEdit *value, *new_name;
    QPushButton *set_btn, *add_btn, *clear_btn, *copy_btn, *del_btn, *new_btn;
    widgets::Stepper *copies;
    QLabel *sel_note, *warn;
    OddsChart *chart, *grid_chart;
    void mass_set(const QString &how);        // "set" / "add" / "clear"
    void copy_selected();
    void delete_selected();
    void new_trait();
    void pick_value(double v);
Q_SIGNALS:
    void message(const QString &text);

private:
    std::optional<QString> _sig;
    bool _filling = false;
    QString _text() const;
    QString _tname(const QString &key) const;
    void _fill();
    void _picked();
    void _chart();
    void _heads();                            // the column heads written out, or short where they do not fit
    QString _tname_raw(const QString &key) const;   // the name as written (colour codes kept), "" none
    void _commit(const QString &label, const QString &text, const std::vector<std::pair<QString, QString>> &names,
                 const QSet<QString> &gone);
};

class TraitsPage : public QWidget {           // the switch between the two views
    Q_OBJECT
public:
    explicit TraitsPage(moddata::Mod &mod, QWidget *parent = nullptr);
    OddsView *odds;
    traitnames::NamesView *names;
    entryview::EntryView *entry;
    QStackedWidget *stack;
    std::vector<std::pair<QString, QPushButton *>> seg;
    void set_view(const QString &key);
    void refresh(document::Document *d);
Q_SIGNALS:                                    // (the entry view's, passed on)
    void scriptEdit(const QString &label, const QString &rel, const QString &text);
    void nameEdit(const QString &key, const QString &text);
    void fileRemove(const QString &label, const QString &rel);
    void bigRequested(const QString &rel, const QStringList &path, const QString &noun);
    void message(const QString &text);

private:
    document::Document *doc = nullptr;
    QString _view = QStringLiteral("odds");
};

}  // namespace traitodds
