// traitnames — Warfare → Leader traits → Names & colours (C++ only, the user 2026-11-18: "I want to be able to assign
// colors to certain ranges of certain values … anything that gets below -3 defense should have this color … and be able
// to generate … the localization, which contains the colorization … for all my backgrounds and personalities … if I add
// more, I need the localization to be there").
//
// A trait's name in the game can be made from its stats: "+1 Attack", "§R-2 Defence" (the game's colour codes: §R red,
// §G green, §Y yellow, §B blue, §W white, §b black, §g grey, §! back to normal). The rules say which stats go into the
// name and which colour a value range gets; the names of every personality and background follow from them. The rules
// can be read from the mod's own names (a mod that already names its traits this way), and new, copied and changed
// traits can get their name from them on their own (Odds & mass edit).
//
// The rules are the editor's (its settings, one set per mod folder), never written into the mod.
#pragma once
#include "core/document.h"
#include "core/moddata.h"
#include "core/traits.h"

#include <QColor>
#include <QString>
#include <QStyledItemDelegate>
#include <QWidget>
#include <functional>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QVBoxLayout;

namespace traitnames {

struct Rule {
    QString stat;                           // "" = any stat
    std::optional<double> lo, hi;           // as the name shows the value (a share stat in %); none = no limit
    QString code;                           // W Y R G B b g
};
struct Rules {
    QStringList stats{QStringLiteral("attack"), QStringLiteral("defence")};   // written into the name, in this order
    QString none = QStringLiteral("No Bonus");     // the name of a trait that changes none of them
    QString none_code;                      // its colour ("" none)
    std::vector<Rule> rules;
    bool auto_names = false;                // new / copied / changed traits get their name from the rules
    QString to_json() const;
    static Rules from_json(const QString &text);
    static Rules defaults();
};

extern const QStringList CODES;             // W Y R G B b g
QString code_word(const QString &code);     // "white", "yellow" …
QColor colour(const QString &code);         // as shown in the editor (the game's colour, lighter where too dark to read)

QString plain(const QString &text);         // without the colour codes
QString shown_value(const QString &stat, double v);   // "+1", "-2", "+5%"
QString name_of(const traits::Trait &t, const Rules &r);
// the rules the mod's names follow; matched = how many names fit them (0: the names are not made from the stats)
Rules learn(const traits::Lists &lists, const std::function<QString(const QString &)> &name, int *matched);

std::optional<Rules> stored(const moddata::Mod &mod);
void store(const moddata::Mod &mod, const Rules &r);
Rules for_mod(const moddata::Mod &mod, const traits::Lists &lists, const std::function<QString(const QString &)> &name);

class ColourDelegate : public QStyledItemDelegate {   // paints a text's § codes in their colours
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
};

class NamesView : public QWidget {
    Q_OBJECT
public:
    explicit NamesView(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    Rules rules;
    traits::Lists lists;
    void refresh(document::Document *d);
    void write(bool only_missing);

    // (for the check)
    std::vector<std::pair<QString, QCheckBox *>> stat_ticks;
    QLineEdit *none_name;
    QComboBox *none_colour;
    QVBoxLayout *rule_lay;
    QPushButton *add_rule, *learn_btn, *write_all, *write_missing;
    QCheckBox *auto_box;
    QTreeWidget *preview;
    QLabel *note;
    void read_from_mod();
    void add_rule_row(const Rule &r);
Q_SIGNALS:
    void message(const QString &text);

private:
    struct RuleRow {
        QWidget *w;
        QComboBox *stat, *col;
        QLineEdit *lo, *hi;
    };
    std::vector<RuleRow> _rows;
    bool _building = false;
    QString _name(const QString &key) const;
    QString _want(const traits::Trait &t, const QString &now) const;
    void _from_widgets();                   // the widgets → rules (saved), then the preview
    void _to_widgets();
    void _preview();
};

}  // namespace traitnames
