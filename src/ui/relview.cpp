// relview.py (see relview.h) — line by line.
#include "ui/relview.h"
#include "core/pytext.h"
#include "ui/colsizes.h"
#include "ui/relgraph.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QButtonGroup>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace relview {

const OrderedMap<QString, QString> SHORT = {
    {u"def"_s, u"defines"_s}, {u"fire"_s, u"fires"_s},     {u"set"_s, u"sets"_s},     {u"clear"_s, u"clears"_s},
    {u"check"_s, u"checks"_s}, {u"add"_s, u"adds"_s},      {u"remove"_s, u"removes"_s}, {u"change"_s, u"changes"_s},
    {u"use"_s, u"uses"_s},    {u"scope"_s, u"in it"_s}};
const QStringList RELATED_KINDS = {u"cflag"_s, u"gflag"_s, u"pflag"_s, u"var"_s, u"modifier"_s, u"event"_s, u"decision"_s};

static QString short_of(const QString &r) { return SHORT.value(r, r); }

QString roles_text(const Counter &counter)
{
    std::vector<std::pair<QString, long long>> items(counter.begin(), counter.end());
    std::stable_sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return -a.second < -b.second; });
    QStringList out;
    for (const auto &[r, n] : items)
        out << (n > 1 ? u"%1 %2"_s.arg(short_of(r)).arg(n) : short_of(r));
    return out.join(u" · "_s);
}

QString entry_name(const events::EventDB &db, const Entry &e)
{
    return db.name(e) + (e.is_event() ? u" #"_s + (e.id ? *e.id : u"None"_s) : u" (decision)"_s);
}

static bool in_scripts(const QString &rel) { return rel.startsWith(u"events/"_s) || rel.startsWith(u"decisions/"_s); }

Groups usage_groups(const refs::RefIndex &refs, events::EventDB &db, const QString &kind, const QString &target)
{
    Groups out;
    for (const refs::Use &u : refs.usages(kind, target)) {
        Entry *e = in_scripts(u.rel) ? db.entry_at(u.rel, u.pos) : nullptr;
        out[u.rel][e].emplace_back(u.pos, u.role);
    }
    return out;
}

QString usage_summary(const refs::RefIndex &refs, events::EventDB &db, const QString &kind, const QString &target)
{
    const Groups g = usage_groups(refs, db, kind, target);
    long long n = 0;
    std::vector<std::pair<long long, Entry *>> ents;
    Counter roles;
    for (const auto &[rel, per] : g)
        for (const auto &[e, v] : per) {
            n += static_cast<long long>(v.size());
            if (e)
                ents.emplace_back(static_cast<long long>(v.size()), e);
            for (const auto &[p, r] : v)
                roles[r] += 1;
        }
    const std::pair<long long, Entry *> *top = nullptr;   // max(ents, key=x[0]): the first of the most
    for (const auto &x : ents)
        if (!top || x.first > top->first)
            top = &x;
    const long long ne = static_cast<long long>(ents.size()), nf = g.size();
    return u"%1 mention%2 · %3 event%4 / decisions · %5 file%6%7<br>%8"_s.arg(n)
        .arg(n == 1 ? QString() : u"s"_s)
        .arg(ne)
        .arg(ne == 1 ? QString() : u"s"_s)
        .arg(nf)
        .arg(nf == 1 ? QString() : u"s"_s)
        .arg(top ? u" · most in <b>%1</b> (%2)"_s.arg(entry_name(db, *top->second)).arg(top->first) : QString())
        .arg(roles_text(roles));
}

QString line_snippet(const QString &text, qsizetype pos, qsizetype width)
{
    const qsizetype a = (pos > 0 ? text.lastIndexOf(u'\n', std::min(pos, text.size()) - 1) : -1) + 1;
    qsizetype b = text.indexOf(u'\n', pos);
    b = b < 0 ? text.size() : b;
    const QString line = py::strip(text.mid(a, std::max<qsizetype>(0, b - a)));
    return line.size() <= width ? line : line.left(width - 1) + u"…"_s;
}

QString sorted_short(const QSet<QString> &roles)
{
    QStringList xs(roles.begin(), roles.end());
    std::sort(xs.begin(), xs.end());
    QStringList out;
    for (const QString &x : xs)
        out << short_of(x);
    return out.join(u'/');
}

// ---------------------------------------------------------------------------------------------- RefTree
RefTree::RefTree(QWidget *parent, const QString &key) : QTreeWidget(parent)
{
    setColumnCount(3);
    setHeaderLabels({u"Where"_s, u"Mentions"_s, u"How"_s});
    setRootIsDecorated(true);
    setUniformRowHeights(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTextElideMode(Qt::ElideRight);
    setSelectionMode(QAbstractItemView::SingleSelection);
    colsizes::make_resizable(this, key, {std::nullopt, 76});   // Where · Mentions resizable, How fills the rest
    connect(this, &QTreeWidget::itemClicked, this, &RefTree::_clicked);
    _bold = QFont(font());
    _bold.setBold(true);
}

void RefTree::_clicked(QTreeWidgetItem *it, int)
{
    const QStringList d = it->data(0, Qt::UserRole).toStringList();
    if (d.size() == 2)
        Q_EMIT goTo(d[0], d[1].toLongLong());
}

QTreeWidgetItem *RefTree::item(QTreeWidgetItem *parent, const QStringList &texts,
                               const std::optional<std::pair<QString, qsizetype>> &go, bool bold,
                               const std::optional<std::tuple<int, int, int>> &rgb, const QString &tip)
{
    auto *it = new QTreeWidgetItem(texts);
    if (go)
        it->setData(0, Qt::UserRole, QStringList{go->first, QString::number(go->second)});
    it->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    if (bold)
        for (int c = 0; c < 3; ++c)
            it->setFont(c, _bold);
    if (rgb)
        it->setForeground(0, QBrush(QColor(std::get<0>(*rgb), std::get<1>(*rgb), std::get<2>(*rgb))));
    if (!tip.isEmpty()) {
        it->setToolTip(0, tip);
        it->setToolTip(2, tip);
    }
    if (parent)
        parent->addChild(it);
    else
        addTopLevelItem(it);
    return it;
}

static std::tuple<int, int, int> kind_rgb(const QString &k)   // KINDS[k][1]
{
    const auto *kk = refs::KINDS.get(k);
    return kk ? kk->second : std::tuple{220, 220, 220};
}

long long RefTree::add_usage(QTreeWidgetItem *parent, const refs::RefIndex &refs, events::EventDB &db,
                             const std::function<QString(const QString &)> &text_of, const QString &kind,
                             const QString &target, int expand_files)
{
    // Files (most mentions first) → events / decisions (most first) → the lines.
    const Groups g = usage_groups(refs, db, kind, target);
    auto count = [](const PerEntry &per) {
        long long n = 0;
        for (const auto &[e, v] : per)
            n += static_cast<long long>(v.size());
        return n;
    };
    std::vector<const std::pair<QString, PerEntry> *> files;
    for (const auto &kv : g)
        files.push_back(&kv);
    std::stable_sort(files.begin(), files.end(), [&](const auto *a, const auto *b) {
        const long long na = count(a->second), nb = count(b->second);
        return na != nb ? na > nb : a->first < b->first;
    });
    for (size_t fi = 0; fi < files.size(); ++fi) {
        const QString &rel = files[fi]->first;
        const PerEntry &per = files[fi]->second;
        Counter roles;
        qsizetype first = -1;
        for (const auto &[e, v] : per)
            for (const auto &[p, r] : v) {
                roles[r] += 1;
                if (first < 0 || p < first)
                    first = p;
            }
        QTreeWidgetItem *fit = item(parent, {rel.split(u'/').last(), QString::number(count(per)), roles_text(roles)},
                                    std::pair{rel, first}, true, std::nullopt, rel);
        const QString text = text_of(rel);
        std::vector<const std::pair<Entry *, Places> *> ents;
        for (const auto &kv : per)
            ents.push_back(&kv);
        std::stable_sort(ents.begin(), ents.end(), [&](const auto *a, const auto *b) {
            if (a->second.size() != b->second.size())
                return a->second.size() > b->second.size();
            const bool an = a->first == nullptr, bn = b->first == nullptr;
            if (an != bn)
                return !an;
            const QString na = a->first ? db.name(*a->first) : QString(), nb = b->first ? db.name(*b->first) : QString();
            return na < nb;
        });
        if (ents.size() > static_cast<size_t>(MAX_ENTRIES)) {
            long long rest_n = 0;
            for (size_t i = MAX_ENTRIES; i < ents.size(); ++i)
                rest_n += static_cast<long long>(ents[i]->second.size());
            const size_t rest = ents.size() - MAX_ENTRIES;
            ents.resize(MAX_ENTRIES);
            item(fit, {u"… %1 more events / decisions with fewer mentions"_s.arg(rest), QString::number(rest_n), QString()});
        }
        for (const auto *ev : ents) {
            Entry *e = ev->first;
            const Places &v = ev->second;
            Counter r2;
            for (const auto &[p, r] : v)
                r2[r] += 1;
            const QString name = e ? entry_name(db, *e) : u"outside any event"_s;
            QTreeWidgetItem *eit =
                item(fit, {name, QString::number(v.size()), roles_text(r2)}, std::pair{rel, v[0].first}, false,
                     e ? std::optional(kind_rgb(e->is_event() ? u"event"_s : u"decision"_s)) : std::nullopt, name);
            Places sv = v;
            std::sort(sv.begin(), sv.end());
            for (size_t i = 0; i < sv.size() && i < static_cast<size_t>(MAX_LINES); ++i) {
                const auto &[p, r] = sv[i];
                item(eit, {u"line %1   %2"_s.arg(refs::line_of(text, p)).arg(line_snippet(text, p)), QString(),
                           refs::ROLES.value(r, r)},
                     std::pair{rel, p}, false, std::nullopt, line_snippet(text, p, 400));
            }
            if (v.size() > static_cast<size_t>(MAX_LINES))
                item(eit, {u"… %1 more lines (Ctrl+F finds them all)"_s.arg(v.size() - MAX_LINES), QString(), QString()});
        }
        fit->setExpanded(static_cast<int>(fi) < expand_files && files.size() <= 6);
    }
    return static_cast<long long>(files.size());
}

// ---------------------------------------------------------------------------------------------- related entries
std::vector<std::pair<Entry *, Shared>> related_entries(const refs::RefIndex &refs, events::EventDB &db, Entry *e)
{
    OrderedMap<refs::ThingKey, QSet<QString>> here;
    for (const refs::Ref &r : refs.in_range(e->rel.value_or(QString()), e->span.first, e->span.second))
        if (RELATED_KINDS.contains(r.kind))
            here[{r.kind, r.target}].insert(r.role);
    if (e->is_event() && e->id && !e->id->isEmpty())
        here[{u"event"_s, *e->id}].insert(u"def"_s);
    if (!e->is_event())
        here[{u"decision"_s, e->key}].insert(u"def"_s);
    OrderedMap<Entry *, OrderedMap<refs::ThingKey, QSet<QString>>> shared;
    for (const auto &[sym, roles] : here)
        for (const refs::Use &u : refs.usages(sym.first, sym.second)) {
            Entry *o = in_scripts(u.rel) ? db.entry_at(u.rel, u.pos) : nullptr;
            if (!o || o == e)
                continue;
            shared[o][sym].insert(u.role);
        }
    std::vector<std::pair<Entry *, Shared>> out;
    for (const auto &[o, syms] : shared) {
        Shared s;
        for (const auto &[k, r] : syms)
            s.insert(k, {*here.get(k), r});
        out.emplace_back(o, s);
    }
    // causal links (one sets / fires what the other checks / defines) weigh more than both merely checking
    static const QSet<QString> DO = {u"set"_s, u"add"_s, u"fire"_s, u"change"_s, u"clear"_s, u"remove"_s},
                               SEE = {u"check"_s, u"def"_s};
    auto score = [](const Shared &syms) {
        double sc = 0.0;
        for (const auto &[k, hrtr] : syms) {
            const auto &[hr, tr] = hrtr;
            sc += ((hr.intersects(DO) && tr.intersects(SEE)) || (tr.intersects(DO) && hr.intersects(SEE))) ? 2.0 : 1.0;
        }
        return -sc;
    };
    std::vector<std::tuple<double, QString, size_t>> keys;
    for (size_t i = 0; i < out.size(); ++i)
        keys.emplace_back(score(out[i].second), db.name(*out[i].first), i);
    std::stable_sort(keys.begin(), keys.end(), [](const auto &a, const auto &b) {
        return std::get<0>(a) != std::get<0>(b) ? std::get<0>(a) < std::get<0>(b) : std::get<1>(a) < std::get<1>(b);
    });
    std::vector<std::pair<Entry *, Shared>> sorted;
    for (const auto &k : keys)
        sorted.push_back(out[std::get<2>(k)]);
    return sorted;
}

// ---------------------------------------------------------------------------------------------- RelationsView
RelationsView::RelationsView(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    head = new QLabel(QString());
    head->setObjectName(u"sub"_s);
    head->setWordWrap(true);
    head->setTextFormat(Qt::RichText);
    head->setContentsMargins(6, 4, 6, 4);
    lay->addWidget(head);
    // a picture (boxes and arrows) or the list; about the word at the cursor or the event at the cursor
    auto *bar = new QHBoxLayout();
    bar->setContentsMargins(6, 0, 6, 2);
    const std::vector<std::vector<std::pair<QString, QString>>> groups = {
        {{u"graph"_s, u"◎ Diagram"_s}, {u"list"_s, u"☰ List"_s}}, {{u"word"_s, u"The word"_s}, {u"event"_s, u"The event"_s}}};
    for (const auto &grp_keys : groups) {
        auto *grp = new QButtonGroup(this);
        for (const auto &[k, t] : grp_keys) {
            auto *b = new QPushButton(t);
            b->setObjectName(u"seg"_s);
            b->setCheckable(true);
            connect(b, &QPushButton::clicked, this, [this] { _redo(); });
            grp->addButton(b);
            bar->addWidget(b);
            btn.insert(k, b);
        }
        bar->addSpacing(14);
    }
    bar->addStretch(1);
    (*btn.get(u"graph"_s))->setChecked(true);
    (*btn.get(u"word"_s))->setChecked(true);
    lay->addLayout(bar);
    stack = new QStackedWidget();
    graph = new relgraph::RelGraph();
    connect(graph, &relgraph::RelGraph::goTo, this, &RelationsView::goTo);
    tree = new RefTree(nullptr, u"relTree"_s);
    connect(tree, &RefTree::goTo, this, &RelationsView::goTo);
    stack->addWidget(graph);
    stack->addWidget(tree);
    lay->addWidget(stack, 1);
}

void RelationsView::_redo()
{
    if (_args) {
        const Args a = *_args;
        show_for(a.refs, a.db, a.text_of, a.sym, a.entry, a.fires_map);
    }
}

void RelationsView::show_for(const refs::RefIndex *refs, events::EventDB *db,
                             const std::function<QString(const QString &)> &text_of, const std::optional<refs::Ref> &sym,
                             Entry *entry, const FiresMap &fires_map)
{
    _args = Args{refs, db, text_of, sym, entry, fires_map};
    if (!refs || !db)
        return;
    QPushButton *b_graph = *btn.get(u"graph"_s), *b_word = *btn.get(u"word"_s), *b_event = *btn.get(u"event"_s);
    if (b_graph->isChecked()) {
        stack->setCurrentIndex(0);
        const bool word = b_word->isChecked() && sym;
        b_word->setEnabled(sym.has_value());
        b_event->setEnabled(entry != nullptr);
        if (word || !entry) {
            if (sym) {
                graph->show_symbol(*refs, *db, sym->kind, sym->target);
                head->setText(usage_summary(*refs, *db, sym->kind, sym->target));
            }
        } else {
            graph->show_entry(*refs, *db, entry, fires_map);
            head->setText(u"<b>%1</b> — click a box to go there · drag to move · wheel to zoom"_s.arg(entry_name(*db, *entry)));
        }
        return;
    }
    stack->setCurrentIndex(1);
    RefTree *t = tree;
    t->clear();
    QStringList parts;
    if (sym) {
        const QString &kind = sym->kind, &target = sym->target;
        const auto *kk = refs::KINDS.get(kind);
        const QString label = kk ? kk->first : kind;
        const auto rgb = kk ? kk->second : std::tuple{200, 200, 200};
        QTreeWidgetItem *top = t->item(nullptr, {u"%1: %2"_s.arg(label, refs->name_of(kind, target)), QString(), QString()},
                                       std::nullopt, true, rgb);
        t->add_usage(top, *refs, *db, text_of, kind, target, 2);
        top->setExpanded(true);
        parts << usage_summary(*refs, *db, kind, target);
    }
    if (entry) {
        Entry *e = entry;
        const QString erel = e->rel.value_or(QString());
        QTreeWidgetItem *top = t->item(
            nullptr, {u"%1: %2"_s.arg(e->is_event() ? u"Event"_s : u"Decision"_s, entry_name(*db, *e)), QString(), QString()},
            std::pair{erel, e->span.first}, true, kind_rgb(u"event"_s));
        QTreeWidgetItem *up = t->item(top, {u"↑ What fires it"_s, QString(), QString()}, std::nullopt, true);
        int n_up = 0;
        if (e->is_event())
            if (const auto *lst = db->fired_by.get(e->id.value_or(events::NO_ID)))
                for (size_t i = 0; i < lst->size() && i < 40; ++i) {
                    const events::Fired &f = (*lst)[i];
                    if (!f.src)
                        t->item(up, {f.how, QString(), QString()});
                    else
                        t->item(up, {entry_name(*db, *f.src), QString(), f.how},
                                std::pair{f.src->rel.value_or(QString()), f.src->span.first});
                    ++n_up;
                }
        up->setText(1, QString::number(n_up));
        QTreeWidgetItem *down = t->item(top, {u"↓ What it fires"_s, QString(), QString()}, std::nullopt, true);
        int n_dn = 0;
        if (const auto it = fires_map.find(e); it != fires_map.end())
            for (size_t i = 0; i < it->size() && i < 40; ++i) {
                const auto &[tgt, how] = (*it)[i];
                Entry *const *y = db->by_id.get(tgt);
                t->item(down,
                        {y ? entry_name(*db, **y) : u"#%1 — no such event"_s.arg(tgt == events::NO_ID ? u"None"_s : tgt),
                         QString(), how},
                        y ? std::optional(std::pair{(*y)->rel.value_or(QString()), (*y)->span.first}) : std::nullopt);
                ++n_dn;
            }
        down->setText(1, QString::number(n_dn));
        const auto rel_items = related_entries(*refs, *db, e);
        QTreeWidgetItem *rel_top = t->item(top, {u"≈ Most related (shared flags, modifiers, variables, events)"_s,
                                                 QString::number(rel_items.size()), QString()},
                                           std::nullopt, true);
        for (size_t i = 0; i < rel_items.size() && i < 40; ++i) {
            const auto &[o, syms] = rel_items[i];
            QTreeWidgetItem *oi = t->item(rel_top,
                                          {entry_name(*db, *o), QString::number(syms.size()), u"shares %1"_s.arg(syms.size())},
                                          std::pair{o->rel.value_or(QString()), o->span.first}, false,
                                          kind_rgb(o->is_event() ? u"event"_s : u"decision"_s));
            std::vector<const std::pair<refs::ThingKey, std::pair<QSet<QString>, QSet<QString>>> *> ss;
            for (const auto &kv : syms)
                ss.push_back(&kv);
            std::sort(ss.begin(), ss.end(), [](const auto *a, const auto *b) { return a->first < b->first; });
            for (const auto *kv : ss) {
                const auto &[k, tg] = kv->first;
                const auto *kk = refs::KINDS.get(k);
                t->item(oi, {u"%1 %2"_s.arg(py::lower(kk ? kk->first : k), tg), QString(),
                             u"here %1 · there %2"_s.arg(sorted_short(kv->second.first), sorted_short(kv->second.second))});
            }
        }
        for (QTreeWidgetItem *it : {top, up, down, rel_top})
            it->setExpanded(true);
        parts << u"<b>%1</b>: %2 fire it, it fires %3, related to %4"_s.arg(entry_name(*db, *e)).arg(n_up).arg(n_dn).arg(
            rel_items.size());
    }
    head->setText(!parts.isEmpty() ? parts.join(u"<br>"_s) : u"Put the cursor on a word or in an event."_s);
}

}  // namespace relview
