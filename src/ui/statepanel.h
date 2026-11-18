// statepanel.py — Provinces & states → States (Sam's item 2): every state of map/region.txt in one list — its name,
// owners, provinces and capital; rename it, add provinces by clicking them, make one the capital, delete it. Before a
// state is deleted the page shows every script that names it (staterefs) and can write its provinces there instead.
#pragma once
#include "core/document.h"
#include "core/moddata.h"
#include "core/staterefs.h"
#include "ui/mapview.h"

#include <QWidget>
#include <optional>
#include <tuple>
#include <vector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace dialogs {
class PickCombo;
}
namespace widgets {
class Section;
}

namespace statepanel {

using OptStr = std::optional<QString>;
extern const QString WARN;

class StatePanel;

class AddHandler : public mapview::MouseHandler {
    // While "Add provinces" is on (States page): a click on a province moves it into the state.
public:
    explicit AddHandler(StatePanel &p) : p(p) {}
    StatePanel &p;
    bool rclick_passes() const override { return true; }
    bool cancel();
    bool press(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
    void move(double, double) override {}
    void release(double, double, double, Qt::KeyboardModifiers) override {}
    bool rclick(double, double, double, Qt::KeyboardModifiers) override { return false; }
};

class StatePanel : public QWidget {
    Q_OBJECT
public:
    StatePanel(moddata::Mod &mod, const mapview::MapData *md, QWidget *parent = nullptr);
    moddata::Mod &mod;
    const mapview::MapData *md;
    document::Document *doc = nullptr;
    mapview::MapView *view = nullptr;
    OptStr rid;
    AddHandler handler;
    std::optional<std::tuple<long long, qsizetype, QString, bool, qsizetype, qsizetype>> _sig;
    struct Refs {                                  // (rid, region version, scripts signature) -> [refs]
        QString rid;
        long long version;
        QString live;
        std::vector<staterefs::Place> refs;
    };
    std::optional<Refs> _refs;
    std::optional<long long> _member;

    QLabel *src, *count, *ed_title, *ed_note, *name_where, *del_info, *refs_info, *del_warn;
    QLineEdit *filter, *name;
    QCheckBox *only_odd, *fix_refs;
    QTreeWidget *tree, *members, *refs;
    widgets::Section *ed, *del_sec;
    QPushButton *cap_btn, *add_btn, *del_btn;
    dialogs::PickCombo *target;

    QString state_name(const QString &rid) const;
    QStringList owners(const QString &rid) const;
    bool odd(const QString &rid) const;
    void refresh(document::Document &doc, const std::vector<long long> &selection);
    void _fill_tree(bool force = false);
    void _select_row();
    void _picked();
    void _zoom();
    void _fill_state();
    std::vector<long long> former(const QString &rid) const;
    std::vector<staterefs::Place> references();
    void _name_done();
    void _member_picked();
    void _make_capital();
    void _add_toggled(bool on);
    void add_province(long long pid);
    void _delete();

Q_SIGNALS:
    void selectRequested(const QList<long long> &pids);   // provinces to select on the map
    void zoomRequested(long long pid);                    // a province to show
    void nameRequested(const QString &key, const QString &text, const QString &section);
    void scriptRequested(const QString &rel, long long pos);   // open a script file at a position
    void message(const QString &text);
    void addModeChanged(bool on);
};

}  // namespace statepanel
