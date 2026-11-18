// popfixbox.py — Population: the strip at the top that says when the pops have something this mod doesn't.
#include "ui/popfixbox.h"
#include "core/document.h"
#include "map/importer.h"
#include "ui/dialogs.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>

using namespace Qt::StringLiterals;
using widgets::fmt;
using widgets::label;

namespace popfixbox {

const std::map<QString, QString> WHAT = {
    {u"type"_s, u"pop type"_s}, {u"culture"_s, u"culture"_s}, {u"religion"_s, u"religion"_s}};

namespace {
QString pl(long long n) { return n == 1 ? QString() : u"s"_s; }
QString kind_word(const QString &kind) { return kind == u"type"_s ? u"pop type"_s : kind; }   // {"type": "pop type"}.get
}  // namespace

QString import_from(moddata::Mod &mod, document::Document &doc, const QString &kind, const QString &key,
                    const QString &folder)
{
    importer::Source src(folder, mod.base);
    importer::Importer imp(mod, doc, src);
    const QString cat = kind == u"type"_s ? u"poptypes"_s : kind == u"culture"_s ? u"cultures"_s : u"religions"_s;
    const importer::Plan plan = imp.plan({{cat, key}}, {}, {}, {});
    const importer::Id id{cat, key};
    if (std::none_of(plan.items.begin(), plan.items.end(), [&](const importer::ItemP &it) { return it->id() == id; }))
        throw std::runtime_error(u"%1 has no %2 %3 to import"_s.arg(src.name, kind_word(kind), key).toStdString());
    QString msg = imp.apply(plan, u"Import %1 %2 from %3"_s.arg(kind_word(kind), key, src.name));
    std::vector<std::pair<importer::ItemP, QString>> out;
    for (const importer::Needed &n : plan.needed)
        if (!n.comes)
            out.emplace_back(n.dep, n.why);
    if (!out.empty()) {                              // (the other mod's rules it names: they stay out — say which)
        QStringList parts;
        for (size_t i = 0; i < out.size() && i < 4; ++i)
            parts << u"%1 (%2)"_s.arg(out[i].first->name, out[i].second);
        msg += u" Left out (%1's rules, not in your mod): %2 — its lines about them are never true here; the Import "
               u"page can bring them."_s.arg(src.name, parts.join(u"; "_s) + (out.size() > 4 ? u" …"_s : QString()));
    }
    return msg;
}

PopFixBox::PopFixBox(moddata::Mod &mod_, std::function<document::Document *()> doc_,
                     std::function<void()> refresh_panel_, QWidget *parent)
    : QWidget(parent), mod(mod_), doc(std::move(doc_)), refresh_panel(std::move(refresh_panel_))
{
    auto *outer = new QVBoxLayout(this);             // (a little room around the framed card)
    outer->setContentsMargins(12, 10, 12, 4);
    card = new QFrame();
    card->setObjectName(u"popFix"_s);
    outer->addWidget(card);
    lay = new QVBoxLayout(card);
    lay->setContentsMargins(14, 10, 14, 12);
    lay->setSpacing(8);
    hide();
}

// ------------------------------------------------------------------ refresh
void PopFixBox::refresh()
{
    document::Document *d = doc();
    if (!d)
        return;
    popcheck::Problems probs = popcheck::problems(*d, mod);
    const long long n = popcheck::count(probs);
    // (Python: repr of the sorted (field, key, pops, size) and of the sorted provinces — only compared)
    std::vector<std::tuple<QString, QString, long long, long long>> rows;
    const std::vector<std::pair<QString, const OrderedMap<QString, popcheck::Problem> *>> fields = {
        {u"type"_s, &probs.type}, {u"culture"_s, &probs.culture}, {u"religion"_s, &probs.religion}};
    for (const auto &[f, m] : fields)
        for (const auto &[k, p] : *m)
            rows.emplace_back(f, k, p.pops, p.size);
    std::sort(rows.begin(), rows.end());
    QStringList parts;
    for (const auto &[f, k, a, b] : rows)
        parts << u"%1\x1f%2\x1f%3\x1f%4"_s.arg(f, k).arg(a).arg(b);
    std::vector<long long> provs;
    for (const auto &[pid, _p] : probs.province)
        provs.push_back(pid);
    std::sort(provs.begin(), provs.end());
    QStringList pp;
    for (long long p : provs)
        pp << QString::number(p);
    const QString sig = parts.join(u'\x1e') + u"|"_s + pp.join(u',');
    if (n > _last_count)                              // it just happened: say so where the user looks
        Q_EMIT message(u"⚠ The pops now have %1 thing%2 this mod doesn't have (a pop type, culture, religion "
                       u"or province) — see the strip at the top of Population."_s.arg(n).arg(pl(n)));
    _last_count = n;
    if (_sig && sig == *_sig)
        return;
    _sig = sig;
    _probs = probs;
    while (lay->count()) {
        QLayoutItem *it = lay->takeAt(0);
        if (QWidget *w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }
    if (!n) {
        hide();
        return;
    }
    const bool error = !probs.type.isEmpty();
    const QString col = error ? theme::ERROR_ : theme::WARN;
    card->setStyleSheet(u"QFrame#popFix { border: 2px solid %1; border-radius: 6px; background: %2; }"_s.arg(col, theme::PANEL));
    QLabel *head = label(u"⚠ The pops have %1 thing%2 this mod doesn't have"_s.arg(n).arg(pl(n)), u"title"_s);
    head->setStyleSheet(u"color: %1;"_s.arg(col));
    lay->addWidget(head);
    for (const auto &[field, m] : fields) {
        std::vector<std::pair<QString, popcheck::Problem>> items(m->begin(), m->end());
        std::stable_sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return -a.second.size < -b.second.size; });
        for (const auto &[key, p] : items)
            lay->addWidget(_row(field, key, p));
    }
    if (!probs.province.isEmpty())
        lay->addWidget(_prov_row(probs.province));
    show();
}

// ------------------------------------------------------------------ one problem
QWidget *PopFixBox::_row(const QString &field, const QString &key, const popcheck::Problem &d)
{
    auto *w = new QWidget();
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 4, 0, 4);
    v->setSpacing(5);
    const QString what = WHAT.at(field);
    const long long np = (long long)d.pids.size();
    QLabel *t = label(u"<b>%1 pop%2</b> (%3 people) of the %4 <b>“%5”</b> — %6. In %7 province%8; first: %9."_s.arg(
                          fmt(d.pops), pl(d.pops), fmt(d.size), what, key,
                          field == u"type"_s ? u"not a pop type of this mod: the game can't make them"_s
                                             : u"not defined in the mod"_s,
                          QString::number(np), pl(np), popcheck::where_text(d.where)),
                      u"sub"_s);
    t->setTextFormat(Qt::RichText);
    v->addWidget(t);
    // 1) import it
    auto found = _sources.find({field, key});
    if (found == _sources.end())
        found = _sources.emplace(std::make_pair(field, key), popcheck::sources_with(mod, field, key)).first;
    const auto &src = found->second;
    auto *row = new QHBoxLayout();
    row->setSpacing(6);
    if (!src.empty()) {
        row->addWidget(label(u"1. Import it from"_s, u"faint"_s, false));
        auto *box = new QComboBox();
        box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        for (const auto &[name, folder] : src)
            box->addItem(name, folder);
        row->addWidget(box, 1);
        auto *b = new QPushButton(u"Import ▸"_s);
        b->setToolTip(u"Takes the %1 over like the Import page does — with its name (and icon) — one undo step, "
                      u"written on Apply"_s.arg(what));
        connect(b, &QPushButton::clicked, this,
                [this, field, key, box] { _import(field, key, box->currentData().toString()); });
        row->addWidget(b);
    } else {
        row->addWidget(label(u"1. No mod the editor can find has a %1 “%2” to import."_s.arg(what, key), u"faint"_s));
    }
    v->addLayout(row);
    // 2) change them into one the mod has
    row = new QHBoxLayout();
    row->setSpacing(6);
    row->addWidget(label(u"2. Change them all into"_s, u"faint"_s, false));
    std::vector<dialogs::PickItem> items;
    if (field == u"type"_s) {
        QStringList keys;
        for (const auto &[k, _p] : mod.poptypes)
            keys << k;
        std::stable_sort(keys.begin(), keys.end(),
                         [&](const QString &a, const QString &b) { return mod.poptype_sort_key(a) < mod.poptype_sort_key(b); });
        for (const QString &k : keys) {
            const moddata::PopType &p = *mod.poptypes.get(k);
            items.push_back({k, u"%1 (%2)"_s.arg(p.name.isEmpty() ? k : p.name, k), p.color, std::nullopt});
        }
    } else {
        std::vector<std::tuple<QString, QString, QString, widgets::Rgb>> rows;   // (group, key, name, colour)
        if (field == u"culture"_s)
            for (const auto &[k, c] : mod.cultures)
                rows.emplace_back(c.group, k, c.name, c.color);
        else
            for (const auto &[k, c] : mod.religions)
                rows.emplace_back(c.group, k, c.name, c.color);
        std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
            if (std::get<0>(a) != std::get<0>(b))
                return std::get<0>(a) < std::get<0>(b);
            return std::get<1>(a) < std::get<1>(b);
        });
        for (const auto &[g, k, name, color] : rows)
            items.push_back({k, u"%1 (%2)"_s.arg(name.isEmpty() ? k : name, k), color, g});
    }
    auto *pick = new dialogs::PickCombo(items);
    pick->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    row->addWidget(pick, 1);
    auto *b = new QPushButton(u"Change ▸"_s);
    b->setToolTip(u"Every pop of this %1 becomes the one picked — one undo step, written on Apply"_s.arg(what));
    connect(b, &QPushButton::clicked, this, [this, field, key, pick] { _change(field, key, pick->current()); });
    row->addWidget(b);
    v->addLayout(row);
    row = new QHBoxLayout();
    b = new QPushButton(u"Show them on the map"_s);
    const QList<long long> pids(d.pids.begin(), d.pids.end());
    connect(b, &QPushButton::clicked, this, [this, pids] { Q_EMIT selectRequested(pids); });
    row->addWidget(b);
    row->addStretch(1);
    v->addLayout(row);
    return w;
}

QWidget *PopFixBox::_prov_row(const OrderedMap<long long, popcheck::Problem> &provs)
{
    auto *w = new QWidget();
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 4, 0, 4);
    v->setSpacing(5);
    std::vector<std::pair<long long, const popcheck::Problem *>> sorted;
    for (const auto &[pid, d] : provs)
        sorted.emplace_back(pid, &d);
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QStringList lst;
    for (size_t i = 0; i < sorted.size() && i < 6; ++i)
        lst << u"#%1 (%2)"_s.arg(sorted[i].first).arg(popcheck::where_text(sorted[i].second->where));
    const long long n = provs.size();
    QLabel *t = label(u"<b>%1 province number%2</b> in the pop files %3 not in this mod's definition.csv: %4%5."_s.arg(
                          QString::number(n), pl(n), n == 1 ? u"is"_s : u"are"_s, lst.join(u", "_s),
                          n > 6 ? u" …"_s : QString()),
                      u"sub"_s);
    t->setTextFormat(Qt::RichText);
    v->addWidget(t);
    auto *row = new QHBoxLayout();
    auto *b = new QPushButton(u"Take them out of the pop files (on Apply) ▸"_s);
    QList<long long> pids;
    for (const auto &[pid, _d] : sorted)
        pids << pid;
    connect(b, &QPushButton::clicked, this, [this, pids] { _drop(pids); });
    row->addWidget(b);
    row->addStretch(1);
    v->addLayout(row);
    return w;
}

// ------------------------------------------------------------------ the ways out
void PopFixBox::_import(const QString &field, const QString &key, const QString &folder)
{
    if (folder.isEmpty())
        return;
    QString msg;
    try {
        msg = import_from(mod, *doc(), field, key, folder);
    } catch (const std::exception &e) {               // (nothing half done: the importer makes its step at the end)
        Q_EMIT message(u"⚠ Couldn't import %1 %2: %3"_s.arg(WHAT.at(field), key, QString::fromUtf8(e.what())));
        return;
    }
    _sig.reset();
    Q_EMIT message(msg);
    refresh_panel();
}

void PopFixBox::_change(const QString &field, const QString &key, const std::optional<QString> &nw)
{
    if (!nw || nw->isEmpty()) {
        Q_EMIT message(u"Pick what they become first."_s);
        return;
    }
    popcheck::change_into(*doc(), field, key, *nw);
    _sig.reset();
    Q_EMIT message(u"Every pop of the %1 %2 is now %3 — Ctrl+Z undoes it, Apply writes it."_s.arg(WHAT.at(field), key, *nw));
    refresh_panel();
}

void PopFixBox::_drop(const QList<long long> &pids)
{
    document::Document &d = *doc();
    auto &drop = d.map<document::DropPopBlocks>(u"drop_pop_blocks"_s);
    d.edit(u"Pops of %1 province%2 that don't exist: out on Apply"_s.arg(pids.size()).arg(pl(pids.size())), {},
           [&drop, pids] {
               for (long long p : pids)
                   drop.pids.insert(p);
           }, {}, {}, {u"drop_pop_blocks"_s});
    _sig.reset();
    Q_EMIT message(u"Their blocks go from the pop files on Apply — Ctrl+Z brings them back."_s);
    refresh_panel();
}

}  // namespace popfixbox
