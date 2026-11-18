// unitpanel.py — Warfare → Unit stats: every unit of the mod (units/*.txt) with its base stats — the numbers a unit has
// before any technology — each one editable, and next to it what all the mod's technologies and inventions add.
// The units are listed on the left, the one picked is on the right (like the other pages of Warfare).
// The files' texts are the editor's (doc.maps["scripts"]): a change is one undoable step, written on Apply.
#pragma once
#include "core/ordered_map.h"
#include "core/script.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
namespace document { class Document; }
namespace moddata { class Mod; }
namespace scriptstore { class ScriptStore; }
namespace techdata { class TechDB; }
namespace widgets { class Section; }

namespace unitpanel {

using OptStr = std::optional<QString>;

extern const std::vector<std::pair<QString, QStringList>> GROUPS;
extern const std::vector<std::pair<QString, QString>> GOODS_BLOCKS;
extern const QHash<QString, double> HIDDEN;      // the user's rule: every (land) unit has a hidden base attack and
                                                 // defence of 10 — the files' numbers come on top of it
extern const QString HIDDEN_COL;                 // "base + hidden 10" everywhere (list, detail, timeline)
struct Col {                                     // (stat, header, "base + hidden 10" / "calc" / plain, header tooltip)
    QString stat, head, hidden, tip;             // hidden: "" / "yes" / "calc"
};
extern const std::vector<Col> LAND_COLS, NAVAL_COLS;

bool num_like(const QString &s);                  // NUM_RE.match(s)
using Vals = OrderedMap<QString, QString>;
std::optional<double> support_value(const QString &stat, const Vals &vals);
QString nice(const QString &k);
std::optional<double> real(const QString &k, const OptStr &v);
std::optional<double> support_factor(const OptStr &v);
QString fmt_num(const std::optional<double> &x);
using Bonus = std::map<QString, OrderedMap<QString, double>>;   // {unit: {stat: total}}
Bonus tech_bonuses(const techdata::TechDB *db);

class UnitStatsView : public QWidget {
    Q_OBJECT
public:
    explicit UnitStatsView(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    const techdata::TechDB *tech_db = nullptr;   // (Python: mod.tech_db)
    document::Document *doc = nullptr;
    scriptstore::ScriptStore *store = nullptr;
    OptStr _armed;
    OrderedMap<QString, QString> units;          // key -> rel file
    OptStr cur;
    QLabel *note, *new_msg, *head, *title, *sub, *warn, *used;
    QLineEdit *search, *new_name, *name_edit;
    QTabWidget *unit_tabs;
    OrderedMap<QString, QTreeWidget *> lists;
    QTreeWidget *lst;
    QPushButton *new_btn, *del_btn;
    widgets::Section *detail;
    QWidget *rows_host;
    QVBoxLayout *rows;

    QStringList _files() const;
    void refresh(document::Document *doc);
    std::pair<Vals, OrderedMap<QString, std::vector<std::pair<QString, QString>>>> _stats(const QString &key) const;
    QString uname(const QString &key) const;
    void _fill_list();
    void show_unit(const QString &key);
    void _picked(QTreeWidgetItem *it, QTreeWidgetItem *prev);
    void _fill_detail();
    std::tuple<std::vector<std::pair<QString, QString>>, long long, long long> users(const QString &key) const;
    QString _used_text(const QString &key) const;
    void _new();
    void _delete();
    void _name_done();
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);   // undo label, rel file, new text
    void nameEdit(const QString &key, const QString &text);                           // localisation key, text
    void fileRemove(const QString &label, const QString &rel);                        // undo label, rel file

private:
    struct Sig {
        QString key;
        QStringList vals;
        std::vector<std::pair<QString, QStringList>> goods;
        bool operator==(const Sig &o) const { return key == o.key && vals == o.vals && goods == o.goods; }
    };
    std::optional<Sig> _sig;
    std::vector<std::pair<std::pair<QString, QString>, QLineEdit *>> _edits;   // ("v", stat) / (block, good) -> its box
    std::vector<std::pair<QString, QCheckBox *>> _flags;   // (C++ only) the yes / no conditions: key -> its tick
    QWidget *_conditions(const Vals &vals, bool naval);
    void _set_flag(const QString &key, bool on);
    OrderedMap<QString, QLabel *> _reals;        // stat -> its "(real number)" label; "support": the back-row line
    std::optional<Bonus> _bonus;

    void _clear_rows();
    void _real_texts(const Vals &vals, const OrderedMap<QString, double> &bonus);
    QWidget *_group(const QString &title, const std::vector<std::pair<QString, QString>> &items,
                    const OrderedMap<QString, double> &bonus);
    QWidget *_goods(const QString &block, const QString &title, const std::vector<std::pair<QString, QString>> &rows);
    void _emit(const QString &what, const std::function<QString(const QString &)> &fn);
    OptStr _num(QLineEdit *ed, const QString &what);
    void _set(const QString &k, QLineEdit *ed);
    void _set_good(const QString &block, const QString &good, QLineEdit *ed);
    void _del_good(const QString &block, const QString &good);
    void _add_good(const QString &block, const QString &good, QLineEdit *amt);
};

}  // namespace unitpanel
