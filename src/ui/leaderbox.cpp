// leaderbox.py — the general / admiral of a unit (see leaderbox.h).
#include "ui/leaderbox.h"
#include "core/localisation.h"
#include "core/pytext.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "ui/nationextras.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSizePolicy>

using namespace Qt::StringLiterals;
using widgets::label;

namespace leaderbox {

namespace {
const QRegularExpression NUM_RE(u"^-?\\d+(\\.\\d+)?$"_s);
const QRegularExpression LEADER_RE(u"^\\s*leader\\s*="_s);
}  // namespace

std::pair<std::optional<qsizetype>, QHash<QString, QString>> leader_of(const QStringList &extra)
{
    for (qsizetype i = 0; i < extra.size(); ++i) {
        const QString &e = extra[i];
        if (LEADER_RE.match(e).hasMatch()) {
            try {
                const script::Nodes ns = script::parse(e);
                if (ns.empty())
                    return {i, {}};
                return {i, nationextras::fields(ns[0])};
            } catch (...) {
                return {i, {}};
            }
        }
    }
    return {std::nullopt, {}};
}

QString leader_text(const QHash<QString, QString> &f)
{
    const QStringList lines{u"leader = {"_s,
                            u"\t\tname = \"%1\""_s.arg(f.value(u"name"_s)),
                            u"\t\tdate = %1"_s.arg(f.value(u"date"_s)),
                            u"\t\ttype = %1"_s.arg(f.value(u"type"_s)),
                            u"\t\tpersonality = %1"_s.arg(f.value(u"personality"_s)),
                            u"\t\tbackground = %1"_s.arg(f.value(u"background"_s)),
                            u"\t\tprestige = %1"_s.arg(f.value(u"prestige"_s)),
                            u"\t}"_s};
    return lines.join(u'\n');
}

LeaderBox::LeaderBox(moddata::Mod &mod, const QString &kind, QWidget *parent)
    : widgets::Section(parent), mod(mod), kind(kind), who(kind == u"land" ? u"General"_s : u"Admiral"_s)
{
    head = label(who);
    lay->addWidget(head);
    auto *g = new QGridLayout();
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(5);
    g->setColumnStretch(1, 1);
    g->addWidget(label(u"Name"_s, u"sub"_s, false), 0, 0);
    name = new QLineEdit();
    name->setPlaceholderText(u"no %1 — type a name to give it one"_s.arg(who.toLower()));
    g->addWidget(name, 0, 1);
    g->addWidget(label(u"Personality"_s, u"sub"_s, false), 1, 0);
    pers = new QComboBox();
    pers->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    g->addWidget(pers, 1, 1);
    g->addWidget(label(u"Background"_s, u"sub"_s, false), 2, 0);
    back = new QComboBox();
    back->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    g->addWidget(back, 2, 1);
    g->addWidget(label(u"Prestige"_s, u"sub"_s, false), 3, 0);
    prestige = new QLineEdit(u"0"_s);
    prestige->setFixedWidth(90);
    g->addWidget(prestige, 3, 1);
    lay->addLayout(g);
    auto *row = new QHBoxLayout();
    set_btn = new QPushButton(u"Set the %1"_s.arg(who.toLower()));
    connect(set_btn, &QPushButton::clicked, this, [this] { _commit(); });
    row->addWidget(set_btn, 1);
    rm_btn = new QPushButton(u"✕ No %1"_s.arg(who.toLower()));
    connect(rm_btn, &QPushButton::clicked, this, [this] { _remove(); });
    row->addWidget(rm_btn);
    lay->addLayout(row);
    note = label(u"What each trait does is on the Warfare page (Leader traits)."_s, u"faint"_s);
    lay->addWidget(note);
    warn = label(u""_s, u"sub"_s);
    warn->setStyleSheet(u"color: #ff786e;"_s);
    lay->addWidget(warn);
}

void LeaderBox::_traits()
{
    if (!doc || !doc->maps.contains(u"scripts"_s))
        return;
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    const QString text = store.text(u"common/traits.txt"_s);
    if (_traits_sig && *_traits_sig == text)
        return;
    _traits_sig = text;
    localisation::LocEdits *loc =
        doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    auto nm = [&](const QString &k) {
        const std::optional<QString> t = loc ? loc->text(k) : std::nullopt;
        return t && !t->isEmpty() ? *t : QString(k).replace(u'_', u' ');
    };
    script::Nodes nodes;
    try {
        nodes = script::parse(text);
    } catch (...) {
        nodes.clear();
    }
    for (const auto &[box, grp] : {std::pair{pers, u"personality"_s}, std::pair{back, u"background"_s}}) {
        const QVariant cur = box->currentData();
        box->clear();
        const script::Node *gn = nullptr;
        for (const script::Node &n : nodes)
            if (n.has_key && n.key == grp && n.is_block) {
                gn = &n;
                break;
            }
        if (gn)
            for (const script::Node &c : gn->kids)
                if (c.has_key && !c.key.isEmpty() && c.is_block)
                    box->addItem(nm(c.key) != c.key ? u"%1  (%2)"_s.arg(nm(c.key), c.key) : c.key, c.key);
        if (cur.isValid())
            box->setCurrentIndex(std::max(0, box->findData(cur)));
    }
}

void LeaderBox::show_for(document::Document *d, const std::optional<QStringList> &u, const QString &unit_name)
{
    doc = d;
    unit = u;
    setVisible(u.has_value());
    if (!u)
        return;
    _traits();
    const auto [_i, f] = leader_of(*u);
    head->setText(u"%1 of %2"_s.arg(who, !unit_name.isEmpty() ? unit_name : u"this one"_s));
    if (!name->hasFocus())
        name->setText(f.value(u"name"_s));
    for (const auto &[box, k] : {std::pair{pers, u"personality"_s}, std::pair{back, u"background"_s}}) {
        const QString v = f.value(k);
        if (!v.isEmpty()) {
            if (box->findData(v) < 0)
                box->addItem(v, v);
            box->setCurrentIndex(box->findData(v));
        }
    }
    if (!prestige->hasFocus()) {
        const QString p = f.value(u"prestige"_s, u"0"_s);
        prestige->setText(!p.isEmpty() ? p : u"0"_s);
    }
    rm_btn->setEnabled(!f.isEmpty());
    set_btn->setText((!f.isEmpty() ? u"Change the %1"_s : u"Set the %1"_s).arg(who.toLower()));
}

void LeaderBox::_commit()
{
    if (!unit)
        return;
    const QString nm = py::split(name->text().remove(u'"')).join(u' ');
    QString pre = py::strip(prestige->text());
    if (pre.isEmpty())
        pre = u"0"_s;
    if (nm.isEmpty()) {
        warn->setText(u"Give the %1 a name."_s.arg(who.toLower()));
        return;
    }
    if (!NUM_RE.match(pre).hasMatch()) {
        warn->setText(u"The prestige needs a number (like 0.1)."_s);
        return;
    }
    warn->setText(u""_s);
    const auto [i, f] = leader_of(*unit);
    const history::Date dt = doc->date.value_or(history::Date{1836, 1, 1});
    QString date = f.value(u"date"_s);
    if (date.isEmpty())
        date = u"%1.%2.%3"_s.arg(std::max(1LL, std::get<0>(dt) - 1)).arg(std::get<1>(dt)).arg(std::get<2>(dt));
    QString pd = pers->currentData().toString(), bd = back->currentData().toString();
    const QString text = leader_text({{u"name"_s, nm},
                                      {u"date"_s, date},
                                      {u"type"_s, kind},
                                      {u"personality"_s, !pd.isEmpty() ? pd : u"no_personality"_s},
                                      {u"background"_s, !bd.isEmpty() ? bd : u"no_background"_s},
                                      {u"prestige"_s, pre}});
    QStringList extra = *unit;
    if (!i)
        extra.insert(0, text);
    else
        extra[*i] = text;
    Q_EMIT changed(nm.toLower().startsWith(who.toLower()) ? nm : u"%1 %2"_s.arg(who, nm), extra);
}

void LeaderBox::_remove()
{
    if (!unit)
        return;
    const auto [i, _f] = leader_of(*unit);
    if (!i)
        return;
    QStringList extra = *unit;
    extra.removeAt(*i);
    name->clear();
    Q_EMIT changed(u"No %1"_s.arg(who.toLower()), extra);
}

}  // namespace leaderbox
