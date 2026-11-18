// eventtemplates.py — templates for new events. Line by line.
#include "core/eventtemplates.h"

#include <algorithm>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace eventtemplates {

const QString LETTERS = u"ABCDEFGHIJ"_s;

Names _texts(const QString &eid, int n_opts, const QString &title, const QString &desc)
{
    Names names;
    names.insert(u"EVTNAME"_s + eid, title + u' ' + eid);
    names.insert(u"EVTDESC"_s + eid, desc);
    for (int i = 0; i < n_opts; ++i) {
        if (i >= LETTERS.size())
            throw std::out_of_range("string index out of range");
        names.insert(u"EVTOPT"_s + LETTERS[i] + eid, u"Choice "_s + LETTERS[i]);
    }
    return names;
}

QString event_block(const QString &kind, const QString &eid, int options, bool triggered, int mtth_months,
                    const QStringList &trigger_lines, const QStringList &extra, const QString &picture,
                    const std::map<int, QStringList> &option_lines)
{
    // An event as text.
    QStringList t = {kind + u" = {"_s, u"\tid = "_s + eid, u"\ttitle = \"EVTNAME"_s + eid + u'"',
                     u"\tdesc = \"EVTDESC"_s + eid + u'"'};
    if (!picture.isEmpty())
        t << u"\tpicture = \""_s + picture + u'"';
    for (const QString &x : extra)
        t << u"\t"_s + x;
    t << QString();
    if (triggered) {
        t << u"\tis_triggered_only = yes"_s << QString();
    } else {
        t << u"\ttrigger = {"_s;
        for (const QString &x : trigger_lines)
            t << u"\t\t"_s + x;
        t << u"\t}"_s << QString() << u"\tmean_time_to_happen = {"_s << u"\t\tmonths = "_s + QString::number(mtth_months)
          << u"\t}"_s << QString();
    }
    for (int i = 0; i < std::max(1, options); ++i) {
        t << u"\toption = {"_s << u"\t\tname = \"EVTOPT"_s + LETTERS[i % LETTERS.size()] + eid + u'"';
        const auto it = option_lines.find(i);
        if (it != option_lines.end())
            for (const QString &x : it->second)
                t << u"\t\t"_s + x;
        t << u"\t\tai_chance = {"_s << u"\t\t\tfactor = 1"_s << u"\t\t}"_s << u"\t}"_s;
    }
    t << u"}"_s;
    return t.join(u'\n');
}

QString call(const QString &eid, int days)
{
    return u"country_event = { id = %1 days = %2 }"_s.arg(eid, QString::number(days));
}

const std::vector<Template> TEMPLATES = {
    {u"plain"_s, u"Event that comes by itself"_s, u"A trigger to fill in, how often (12 months), one choice"_s, 1},
    {u"triggered"_s, u"Event called by another"_s, u"is_triggered_only — no trigger, no timer: an effect calls it"_s, 1},
    {u"choice2"_s, u"A choice between two ways"_s, u"One event, two choices with their own effects"_s, 1},
    {u"choice3"_s, u"A choice between three ways"_s, u"One event, three choices"_s, 1},
    {u"province"_s, u"Province event"_s, u"province_event: happens in a province (its own scope)"_s, 1},
    {u"major"_s, u"Major news for everyone"_s, u"major = yes, fire_only_once = yes: every nation hears of it"_s, 1},
    {u"chain2"_s, u"Chain: a start and a follow-up for each choice"_s,
     u"Three events: the start's two choices each call their own follow-up (after 30 days)"_s, 3},
    {u"chain3"_s, u"Chain of three in a row"_s, u"Start → middle → end, each calling the next after 30 days"_s, 3},
    {u"flagchain"_s, u"Chain held by a flag"_s,
     u"The start sets a country flag; the next comes by itself (about 6 months) while the flag is set and clears it"_s,
     2},
};

static OrderedMap<QString, QString> make_names()
{
    OrderedMap<QString, QString> out;
    for (const Template &t : TEMPLATES)
        out.insert(t.key, t.name);
    return out;
}
const OrderedMap<QString, QString> NAME = make_names();

static void update(Names &a, const Names &b)
{
    for (const auto &[k, v] : b)
        a.insert(k, v);
}

Made make(const QString &key, const QStringList &ids)
{
    // -> (text of the events, {text key: text}, the first event's id). ids: as many as the template needs.
    const QString a = ids.at(0);
    const QStringList who = {u"# who and when"_s};
    if (key == u"plain")
        return {event_block(u"country_event"_s, a, 1, false, 12, who), _texts(a, 1), a};
    if (key == u"triggered")
        return {event_block(u"country_event"_s, a, 1, true), _texts(a, 1), a};
    if (key == u"choice2")
        return {event_block(u"country_event"_s, a, 2, false, 12, who), _texts(a, 2), a};
    if (key == u"choice3")
        return {event_block(u"country_event"_s, a, 3, false, 12, who), _texts(a, 3), a};
    if (key == u"province")
        return {event_block(u"province_event"_s, a, 1, false, 12, {u"# which provinces"_s}), _texts(a, 1), a};
    if (key == u"major")
        return {event_block(u"country_event"_s, a, 1, false, 12, who, {u"major = yes"_s, u"fire_only_once = yes"_s}),
                _texts(a, 1, u"News"_s), a};
    if (key == u"chain2") {
        const QString b = ids.at(1), c = ids.at(2);
        const QString text = QStringList{event_block(u"country_event"_s, a, 2, false, 12, who, {}, {},
                                                     {{0, {call(b, 30)}}, {1, {call(c, 30)}}}),
                                         event_block(u"country_event"_s, b, 1, true),
                                         event_block(u"country_event"_s, c, 1, true)}
                                 .join(u"\n\n"_s);
        Names names = _texts(a, 2, u"Chain start"_s);
        update(names, _texts(b, 1, u"After choice A"_s));
        update(names, _texts(c, 1, u"After choice B"_s));
        return {text, names, a};
    }
    if (key == u"chain3") {
        const QString b = ids.at(1), c = ids.at(2);
        const QString text =
            QStringList{event_block(u"country_event"_s, a, 1, false, 12, who, {}, {}, {{0, {call(b, 30)}}}),
                        event_block(u"country_event"_s, b, 1, true, 12, {}, {}, {}, {{0, {call(c, 30)}}}),
                        event_block(u"country_event"_s, c, 1, true)}
                .join(u"\n\n"_s);
        Names names = _texts(a, 1, u"Chain start"_s);
        update(names, _texts(b, 1, u"Chain middle"_s));
        update(names, _texts(c, 1, u"Chain end"_s));
        return {text, names, a};
    }
    if (key == u"flagchain") {
        const QString b = ids.at(1);
        const QString flag = u"chain_"_s + a;
        const QString text =
            QStringList{event_block(u"country_event"_s, a, 1, false, 12,
                                    {u"# who and when"_s, u"NOT = { has_country_flag = %1 }"_s.arg(flag)},
                                    {u"fire_only_once = yes"_s}, {}, {{0, {u"set_country_flag = "_s + flag}}}),
                        event_block(u"country_event"_s, b, 1, false, 6, {u"has_country_flag = "_s + flag}, {}, {},
                                    {{0, {u"clr_country_flag = "_s + flag}}})}
                .join(u"\n\n"_s);
        Names names = _texts(a, 1, u"Chain start"_s);
        update(names, _texts(b, 1, u"Some months later"_s));
        return {text, names, a};
    }
    throw std::out_of_range(key.toStdString());
}

std::pair<QString, Names> follow_up(const QString &eid, const QString &kind, int choices)
{
    // The event a choice calls: triggered only.
    return {event_block(kind, eid, choices, true), _texts(eid, choices, u"Follow-up"_s)};
}

}  // namespace eventtemplates
