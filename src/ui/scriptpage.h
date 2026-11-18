// scriptpage.py — A page for one kind of script that is not an event or a decision — rebel types (Politics), pop types
// (Game rules): its things listed on the left, the script editor in the middle (the very same one as on the Scripts
// page: coloured words, a note on hover, Ctrl+click to what a word names, search, folding), and on the right the one
// picked read out — every part under its own heading (who joins — will_rise, what it turns into — promote_to …) with
// its conditions and effects in plain words, and what the checks found in it, each with the way to its line.
//
// The scripts stay on their own pages; only the support is shared.
#pragma once
#include "core/ordered_map.h"
#include "core/script.h"
#include "ui/scriptedit.h"

#include <QWidget>
#include <map>
#include <optional>
#include <utility>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace document {
class Document;
}
namespace moddata {
class Mod;
}
namespace scriptstore {
class ScriptStore;
}
namespace eventpanel {
class EventPanel;
}

namespace scriptpage {

using OptStr = std::optional<QString>;
extern const QStringList EFFECT_WORDS;

struct Spec {
    QString id, title, noun;
    QString rel, folder;                         // one of the two (the other empty: Python's key not there)
    QString intro;
    std::vector<std::pair<QString, QString>> names;   // (suffix, label)
    OrderedMap<QString, QString> parts;
};
extern const Spec REBELS, POP_TYPES;

class ScriptListPage : public QWidget {
    Q_OBJECT
public:
    ScriptListPage(moddata::Mod &mod, const Spec &spec, eventpanel::EventPanel *hub, QWidget *parent = nullptr);
    moddata::Mod &mod;
    const Spec &spec;
    eventpanel::EventPanel *hub;                 // the Scripts panel (it holds what the words mean)
    document::Document *doc = nullptr;
    scriptstore::ScriptStore *store = nullptr;
    std::optional<std::pair<QString, QString>> cur;   // (rel, key)
    struct Item {                                // (key, rel, start, end, node or None)
        QString key, rel;
        qsizetype a, b;
        const script::Node *node;
    };
    std::vector<Item> _entries;
    scriptedit::ProblemsByRel _problems;         // rel -> [(position or None, severity, text)]
    int _prob_i = -1;
    QLineEdit *search;
    QTreeWidget *lst;
    scriptedit::ScriptWorkspace *ws;
    QLabel *head, *title, *meta, *prob, *body;
    QPushButton *jump;
    struct NameRow {
        QString sfx;
        QLineEdit *ed;
        QWidget *w;
    };
    std::vector<NameRow> name_rows;

    void _hub_changed();
    void refresh(document::Document *doc);
    QStringList rels();
    OptStr loc(const QString &key);
    QString nm(const QString &key);
    void _read();
    void set_problems(const scriptedit::ProblemsByRel &by_rel);
    std::vector<scriptedit::Problem> _probs(const QString &rel, qsizetype a, qsizetype b, bool whole) const;
    void _fill_list();
    const Item *_entry() const;
    void _picked(QTreeWidgetItem *it, QTreeWidgetItem *prev);
    void select(const QString &rel, const OptStr &key = std::nullopt, std::optional<qsizetype> pos = std::nullopt);
    void _place_changed(const QString &rel, long long pos);
    void _link(const QString &href);
    void _jump_problem();
    std::optional<QString> _weights(const QString &text, const script::Nodes &nodes, int depth = 0);
    static QString _shift(const QString &htm, int depth);
    QString _part(const QString &text, const script::Node &c);
    void _fill_detail();
    void _loc_done(QLineEdit *ed, const QString &sfx);

Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);   // (the workspaces connect these)
    void nameEdit(const QString &key, const QString &text);
    void outside(const QString &rel, long long pos);   // a link leads into another file: the Scripts page shows it

private:
    std::map<QString, std::pair<QString, script::Nodes>> _parsed;   // (Python: parse_cached) rel -> (text, its tree)
    const script::Nodes &_nodes(const QString &rel, const QString &text);
};

}  // namespace scriptpage
