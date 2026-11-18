// cbpanel.py — The "War goals" button of the Scripts page: every war goal (casus belli) of common/cb_types.txt in a
// list — a click opens it in the script editor, where it is written like an event or a decision (the same colours,
// the notes on hover, Ctrl+click to what a word names) — and the one picked read out next to it: its picture and
// texts, its numbers, what it demands in a peace, and every part of it under its own heading (who may use it —
// can_use, which states — allowed_states, what happens when it is added — on_add …), what gives it, and what the
// check found.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/script.h"

#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <optional>
#include <tuple>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
namespace document { class Document; }
namespace events { class EventDB; }
namespace refs { class RefIndex; }
namespace scriptstore { class ScriptStore; }
namespace widgets { class Section; }

namespace cbpanel {

using OptStr = std::optional<QString>;
extern const QString ORDER, STRIP, TEMPLATE;
// a peace option: (in the list's column, said in full)
extern const OrderedMap<QString, std::pair<QString, QString>> DEMANDS;
extern const OrderedMap<QString, std::pair<QString, QString>> PARTS;   // the parts of a war goal: (heading, "cond" / "effect")
extern const OrderedMap<QString, QString> NICE;
QString strip_colours(const QString &s);     // COLOR_RE.sub("", s): "§." and a lone "§"
bool f_own(const QString &loc_key, const QString &key);   // a text that is this war goal's alone

class WarGoalView : public QWidget {
    Q_OBJECT
public:
    explicit WarGoalView(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    scriptstore::ScriptStore *store = nullptr;
    events::EventDB *db = nullptr;
    refs::RefIndex *refs = nullptr;
    // the Scripts panel's (the same lines as an event's; Python passes conds_html a second argument, None)
    std::function<QString(const script::Nodes &)> conds_html, effects_html;
    OptStr cur;
    QLineEdit *search, *new_edit;
    QLabel *count, *new_note, *new_warn, *head, *pic, *title, *meta, *problems, *body;
    QWidget *new_row, *names_host;
    QTreeWidget *lst;
    widgets::Section *detail_sec;
    QPushButton *del_btn;
    OrderedMap<QString, std::pair<QLabel *, QLineEdit *>> name_edits;

    void refresh(document::Document *doc, events::EventDB *db, refs::RefIndex *refs);
    void _read();
    OptStr loc(const QString &key) const;
    QString nm(const QString &key) const;
    const script::Node *node(const QString &key) const;
    std::optional<std::pair<qsizetype, qsizetype>> span(const QString &key) const;
    bool exists(const QString &key) const { return node(key) != nullptr; }
    OptStr goal_at(qsizetype pos) const;
    QStringList demands(const script::Node &n) const;
    void _fill_list();
    void mark(const QString &key);
    void select(const QString &key);
    void _fill_detail();
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);   // undo label, rel file, new text
    void nameEdit(const QString &key, const QString &text);                           // localisation key, text
    void goalPicked(const QString &key, bool own_tab);   // a war goal picked in the list (True: a tab of its own)
    void linkActivated(const QString &href);             // a link in what is read out (an event, a modifier, a line …)

private:
    std::vector<std::pair<QString, const script::Node *>> _goals;
    const script::Node *_order = nullptr;
    script::Nodes _nodes;                    // (the parse _goals and _order point into)
    std::optional<QString> _text;
    OrderedMap<QString, std::vector<std::pair<QString, QString>>> _problems;   // key -> [(severity, text)]
    OrderedMap<QString, std::vector<std::optional<qsizetype>>> _where;        // key -> the position of each
    OrderedMap<QString, std::vector<std::tuple<QString, qsizetype, QString>>> _given;   // key -> [(rel, pos, role)]
    bool _armed = false;
    OrderedMap<QString, OptStr> _name_keys;
    document::Document *_strips_doc = nullptr;
    long long _frames();
    void _check();
    void _picked(QTreeWidgetItem *cur, QTreeWidgetItem *prev);
    void _double(QTreeWidgetItem *it);
    OptStr _name_key(const QString &fid, const QString &key, const OptStr &war) const;
    void _fill_order(const script::Node &n);
    void _fill_picture(const OptStr &idx);
    void _loc_done(QLineEdit *ed, const QString &fid);
    void _new_open();
    void _new_done();
    void _remove();
};

}  // namespace cbpanel
