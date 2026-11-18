// utilpanel.py — Provinces tab → Utility states: super regions some mods keep in map/region.txt only to group provinces
// for events and decisions (Femgirl_DoD's ANY_PROVINCE holds every province). They are no state — the game counts the
// FIRST region listing a province as its state — so the editor keeps them apart: the state checks ignore them and a
// province's state is the first real state listing it. Which regions are utility states is kept per mod in the
// editor's settings (nothing is written into the mod for that); their province lists are region.txt and are edited
// here like any state.
#pragma once
#include "core/document.h"
#include "core/moddata.h"

#include <QWidget>
#include <optional>
#include <utility>

class QLabel;
class QListWidget;
class QPushButton;

namespace dialogs {
class PickCombo;
}
namespace widgets {
class Section;
}

namespace utilpanel {

using OptStr = std::optional<QString>;

class UtilityPanel : public QWidget {
    Q_OBJECT
public:
    explicit UtilityPanel(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    OptStr view;
    QLabel *list_title, *none_note, *v_title, *v_sub, *v_warn, *edit_hint, *sugg_title;
    QListWidget *lst, *sugg;
    widgets::Section *view_section, *sugg_section;
    QPushButton *edit_btn, *add_sel, *rm_sel, *unmark_btn, *sugg_one, *sugg_all;
    dialogs::PickCombo *pick;
    std::optional<std::pair<qsizetype, qsizetype>> _pick_sig;

    QString rname(const QString &rid) const;
    void refresh(document::Document *doc, const OptStr &view, bool editing, const std::vector<long long> &selection);

Q_SIGNALS:
    void viewRequested(const QString &rid);                 // a utility state to show on the map ("" = none)
    void markRequested(const QStringList &rids, bool on);   // regions: mark / unmark as utility states
    void editToggled(bool on);                              // map clicks add / remove provinces of the viewed one
    void selectedRequested(bool add);                       // the selected provinces: add (True) / take out (False)
};

}  // namespace utilpanel
