// leaderbox.py — The general of an army / the admiral of a navy at the start: a `leader = { … }` block inside the army
// or navy in the nation's history/units file — name, personality and background (the traits of common/traits.txt),
// prestige.
#pragma once
#include "core/document.h"
#include "core/moddata.h"
#include "core/oob.h"
#include "ui/widgets.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <optional>
#include <utility>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace leaderbox {

// (index in the unit's extra texts, {field: value}) of its leader block, or (None, {})
std::pair<std::optional<qsizetype>, QHash<QString, QString>> leader_of(const QStringList &extra);
QString leader_text(const QHash<QString, QString> &f);

class LeaderBox : public widgets::Section {
    Q_OBJECT
public:
    LeaderBox(moddata::Mod &mod, const QString &kind, QWidget *parent = nullptr);
    moddata::Mod &mod;
    QString kind;                                  // "land" (a general) / "sea" (an admiral)
    QString who;
    document::Document *doc = nullptr;
    std::optional<QStringList> unit;               // the unit's extra texts (None: no unit)
    std::optional<QString> _traits_sig;
    QLabel *head, *note, *warn;
    QLineEdit *name, *prestige;
    QComboBox *pers, *back;
    QPushButton *set_btn, *rm_btn;

    void _traits();
    void show_for(document::Document *doc, const std::optional<QStringList> &unit, const QString &unit_name);
    void _commit();
    void _remove();
Q_SIGNALS:
    void changed(const QString &label, const QStringList &extra);   // undo label, the unit's new list of extra texts
};

}  // namespace leaderbox
