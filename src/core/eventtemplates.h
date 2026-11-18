// eventtemplates.py — Templates for new events: the structure is there, only the content is left to fill in — and
// chains of them. A template makes one or more whole events (ids from the mod's ID system, reid::suggest, else the
// next free ones), with title / description / choice texts already in the localisation ("New event — title" …) so
// the game shows them at once. The chain templates wire the events together: a choice calls its follow-up with
// country_event = { id = … days = … }, a flag-held chain lets the next event come by itself while a flag is set.
// From the node editor a CHOICE grows its follow-up the same way ("＋ Follow-up event").
#pragma once
#include "core/ordered_map.h"

#include <QString>
#include <QStringList>
#include <map>
#include <tuple>
#include <vector>

namespace eventtemplates {

using Names = OrderedMap<QString, QString>;   // {text key: text}

extern const QString LETTERS;

Names _texts(const QString &eid, int n_opts, const QString &title = QStringLiteral("New event"),
             const QString &desc = QStringLiteral("What happens — write it here."));
// options: number of choices; option_lines: {index: [lines inside that choice]}
QString event_block(const QString &kind, const QString &eid, int options, bool triggered = false, int mtth_months = 12,
                    const QStringList &trigger_lines = {}, const QStringList &extra = {},
                    const QString &picture = {}, const std::map<int, QStringList> &option_lines = {});
QString call(const QString &eid, int days);

struct Template {
    QString key, name, what;
    int ids;                                  // how many ids it needs
};
extern const std::vector<Template> TEMPLATES;
extern const OrderedMap<QString, QString> NAME;

struct Made {
    QString text;                             // the events
    Names names;
    QString first;                            // the first event's id
};
Made make(const QString &key, const QStringList &ids);   // (an unknown key: std::out_of_range — Python's KeyError)
std::pair<QString, Names> follow_up(const QString &eid, const QString &kind = QStringLiteral("country_event"),
                                    int choices = 1);

}  // namespace eventtemplates
