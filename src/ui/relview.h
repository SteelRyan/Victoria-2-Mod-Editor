// relview.py — How things in the scripts hang together, shown as trees instead of long lists of ids:
// RefTree       one thing (a flag, a nation, a modifier …): the files it is in (most mentions first), in each file the
//               events / decisions (most mentions first, with what they do with it: sets 2 · checks 4), under each
//               the lines themselves. A click goes there.
// RelationsView the word at the cursor as above, and the event / decision at the cursor: its chain (what fires it,
//               what it fires) and the entries it is most related to — ranked by how many flags, modifiers, variables
//               and events they share, with what each does with them.
#pragma once
#include "core/events.h"
#include "core/ordered_map.h"
#include "core/refs.h"

#include <QFont>
#include <QHash>
#include <QSet>
#include <QTreeWidget>
#include <QWidget>
#include <functional>
#include <optional>
#include <tuple>
#include <vector>

class QLabel;
class QPushButton;
class QStackedWidget;

namespace relgraph { class RelGraph; }

namespace relview {

constexpr int MAX_LINES = 20;                    // lines listed under one event / decision
constexpr int MAX_ENTRIES = 60;                  // events / decisions listed under one file
extern const OrderedMap<QString, QString> SHORT;
extern const QStringList RELATED_KINDS;

using events::Entry;
using Counter = OrderedMap<QString, long long>;  // (Python: defaultdict(int) of roles)
using Places = std::vector<std::pair<qsizetype, QString>>;   // [(pos, role)]
using PerEntry = OrderedMap<Entry *, Places>;    // entry or None -> places
using Groups = OrderedMap<QString, PerEntry>;    // rel -> …
// the event an entry fires: {entry: [(event id, how)]} (eventpanel._fires_map)
using FiresMap = QHash<const Entry *, std::vector<std::pair<QString, QString>>>;
// [(other entry, {(kind, target): (roles here, roles there)})]
using Shared = OrderedMap<refs::ThingKey, std::pair<QSet<QString>, QSet<QString>>>;

QString roles_text(const Counter &counter);
QString entry_name(const events::EventDB &db, const Entry &e);
Groups usage_groups(const refs::RefIndex &refs, events::EventDB &db, const QString &kind, const QString &target);
QString usage_summary(const refs::RefIndex &refs, events::EventDB &db, const QString &kind, const QString &target);
QString line_snippet(const QString &text, qsizetype pos, qsizetype width = 110);
std::vector<std::pair<Entry *, Shared>> related_entries(const refs::RefIndex &refs, events::EventDB &db, Entry *e);
QString sorted_short(const QSet<QString> &roles);   // "/".join(SHORT.get(x, x) for x in sorted(roles))

class RefTree : public QTreeWidget {
    // Where · Mentions · How — items carry (rel, pos) to go to.
    Q_OBJECT
public:
    explicit RefTree(QWidget *parent = nullptr, const QString &key = QStringLiteral("refTree"));
    QTreeWidgetItem *item(QTreeWidgetItem *parent, const QStringList &texts,
                          const std::optional<std::pair<QString, qsizetype>> &go = std::nullopt, bool bold = false,
                          const std::optional<std::tuple<int, int, int>> &rgb = std::nullopt, const QString &tip = {});
    long long add_usage(QTreeWidgetItem *parent, const refs::RefIndex &refs, events::EventDB &db,
                        const std::function<QString(const QString &)> &text_of, const QString &kind,
                        const QString &target, int expand_files = 3);
Q_SIGNALS:
    void goTo(const QString &rel, long long pos);

private:
    QFont _bold;
    void _clicked(QTreeWidgetItem *it, int col);
};

class RelationsView : public QWidget {
    // The mini window's second page: how the word and the event at the cursor relate to the rest.
    Q_OBJECT
public:
    explicit RelationsView(QWidget *parent = nullptr);
    QLabel *head;
    OrderedMap<QString, QPushButton *> btn;
    QStackedWidget *stack;
    relgraph::RelGraph *graph;
    RefTree *tree;
    void show_for(const refs::RefIndex *refs, events::EventDB *db, const std::function<QString(const QString &)> &text_of,
                  const std::optional<refs::Ref> &sym, Entry *entry, const FiresMap &fires_map);
Q_SIGNALS:
    void goTo(const QString &rel, long long pos);

private:
    struct Args {
        const refs::RefIndex *refs;
        events::EventDB *db;
        std::function<QString(const QString &)> text_of;
        std::optional<refs::Ref> sym;
        Entry *entry;
        FiresMap fires_map;
    };
    std::optional<Args> _args;
    void _redo();
};

}  // namespace relview
