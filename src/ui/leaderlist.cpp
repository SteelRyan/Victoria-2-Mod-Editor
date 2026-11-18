// leaderlist — see leaderlist.h.
#include "ui/leaderlist.h"
#include "core/countryhist.h"
#include "core/localisation.h"
#include "core/script.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "ui/leaderbox.h"
#include "ui/theme.h"
#include "ui/traitnames.h"
#include "ui/widgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QRandomGenerator>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <map>

using namespace Qt::StringLiterals;
using widgets::label;

namespace leaderlist {

namespace {
const QRegularExpression NUM_RE(u"^-?\\d+(\\.\\d+)?$"_s);
const QRegularExpression DATE_RE(u"^\\d{1,4}\\.\\d{1,2}\\.\\d{1,2}$"_s);
constexpr int KEY_ROLE = Qt::UserRole, NUM_ROLE = Qt::UserRole + 1;

class Item : public QTreeWidgetItem {         // numbers sort as numbers
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &o) const override
    {
        const int c = treeWidget() ? treeWidget()->sortColumn() : 0;
        const QVariant a = data(c, NUM_ROLE), b = o.data(c, NUM_ROLE);
        if (a.isValid() && b.isValid())
            return a.toDouble() < b.toDouble();
        return text(c).toLower() < o.text(c).toLower();
    }
};

QString signed_num(double v) { return (v > 0 ? u"+"_s : QString()) + traits::number(v); }

QString stat_value(const QString &stat, double v)   // "+3" / "-10 %"
{
    if (traits::is_percent(stat))
        return signed_num(std::round(v * 1000) / 10) + u" %"_s;
    return signed_num(v);
}

QString stat_name(const QString &stat)
{
    if (stat == u"attack+defence")
        return u"Attack + defence"_s;
    const QString w = traits::stat_word(stat);
    return w.left(1).toUpper() + w.mid(1);
}
}  // namespace

// One thick bar per value (the highest value on top): the value, the bar, how many leaders and their share. A click
// on a bar shows those leaders in the list (the filter takes "attack = 3").
class CountBars : public QWidget {
public:
    using QWidget::QWidget;
    QString stat;
    std::vector<std::pair<double, int>> rows;   // value, how many
    int total = 0;
    std::function<void(double)> click;
    static constexpr int ROW = 28;
    void set(const QString &s, std::vector<std::pair<double, int>> r, int t)
    {
        stat = s;
        rows = std::move(r);
        total = t;
        setFixedHeight(std::max(1, int(rows.size())) * ROW + 4);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor gold(theme::GOLD), ink(theme::INK), dim(theme::INK_DIM), danger(theme::DANGER),
            panel(theme::PANEL_ALT);
        QFont f = font();
        if (rows.empty()) {
            p.setPen(dim);
            p.drawText(QRectF(0, 0, width(), ROW), Qt::AlignVCenter | Qt::AlignLeft, u"No leaders listed."_s);
            return;
        }
        int top = 0;
        for (const auto &[v, n] : rows)
            top = std::max(top, n);
        f.setBold(true);
        const QFontMetrics fb(f);
        int lw = 0;
        for (const auto &[v, n] : rows)
            lw = std::max(lw, fb.horizontalAdvance(stat_value(stat, v)));
        lw += 10;
        f.setBold(false);
        const int rw = QFontMetrics(f).horizontalAdvance(u"9999  (100 %)"_s) + 8;
        const double bw = std::max(20, width() - lw - rw);
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto [v, n] = rows[i];
            const double y = double(i) * ROW + 2;
            f.setBold(true);
            p.setFont(f);
            p.setPen(v < 0 ? danger : v > 0 ? gold : ink);
            p.drawText(QRectF(0, y, lw - 8, ROW - 4), Qt::AlignVCenter | Qt::AlignRight, stat_value(stat, v));
            p.setPen(Qt::NoPen);
            p.setBrush(panel);
            p.drawRoundedRect(QRectF(lw, y + 3, bw, ROW - 10), 3, 3);
            const double w = top ? bw * n / top : 0;
            p.setBrush(v < 0 ? danger : gold);
            p.setPen(QPen(QColor(20, 20, 24), 2));
            p.drawRoundedRect(QRectF(lw, y + 3, std::max(4.0, w), ROW - 10), 3, 3);
            f.setBold(false);
            p.setFont(f);
            p.setPen(ink);
            p.drawText(QRectF(lw + bw + 8, y, rw, ROW - 4), Qt::AlignVCenter | Qt::AlignLeft,
                       u"%1  (%2 %)"_s.arg(n).arg(total ? std::round(1000.0 * n / total) / 10 : 0));
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        const int i = int((e->position().y() - 2) / ROW);
        if (click && i >= 0 && i < int(rows.size()))
            click(rows[size_t(i)].first);
    }
};

double LeaderList::value(const Row &r, const QString &stat)
{
    if (stat == u"attack+defence")
        return r.atk + r.def;
    return r.st.value(stat);
}

LeaderList::LeaderList(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *s = new widgets::Section();
    head = label(u"Generals"_s, u"title"_s);
    s->lay->addWidget(head);
    auto *sr = new QHBoxLayout();
    sr->setSpacing(0);
    auto *grp = new QButtonGroup(this);
    for (const auto &[key, text] : {std::pair{u"picked"_s, u"Picked nations"_s}, std::pair{u"world"_s, u"World"_s}}) {
        auto *b = new widgets::WrapButton(text);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(key == scope);
        connect(b, &QPushButton::clicked, this, [this, k = key] {
            scope = k;
            _fill();
            Q_EMIT scopeChanged();
        });
        grp->addButton(b);
        sr->addWidget(b, 1);
        scope_btns.emplace_back(key, b);
    }
    s->lay->addLayout(sr);
    picked_note = label(u""_s, u"faint"_s);
    s->lay->addWidget(picked_note);
    filter = new QLineEdit();
    filter->setPlaceholderText(u"Filter: a name, a nation or a trait"_s);
    filter->setClearButtonEnabled(true);
    connect(filter, &QLineEdit::textChanged, this, [this] { _fill(); });
    s->lay->addWidget(filter);
    count = label(u""_s, u"sub"_s);
    count->setTextFormat(Qt::RichText);
    s->lay->addWidget(count);
    tree = new QTreeWidget();
    tree->setColumnCount(5);
    tree->setHeaderLabels({u"Name"_s, u"Nation"_s, u"Attack"_s, u"Defence"_s, u"Other"_s});
    tree->setRootIsDecorated(false);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree->setTextElideMode(Qt::ElideRight);
    tree->setSortingEnabled(true);
    tree->sortByColumn(0, Qt::AscendingOrder);
    QHeaderView *h = tree->header();
    h->setStretchLastSection(false);
    h->setSectionResizeMode(0, QHeaderView::Stretch);
    h->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    h->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    h->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    h->setSectionResizeMode(4, QHeaderView::Stretch);
    tree->setMinimumHeight(320);
    connect(tree, &QTreeWidget::itemSelectionChanged, this, [this] { _picked_row(); });
    s->lay->addWidget(tree);
    auto *br = new QHBoxLayout();
    br->setSpacing(6);
    new_btn = new QPushButton(u"＋ New"_s);
    connect(new_btn, &QPushButton::clicked, this, [this] { new_leader(); });
    copy_btn = new QPushButton(u"⧉ Copy"_s);
    connect(copy_btn, &QPushButton::clicked, this, [this] { copy_leader(); });
    del_btn = new QPushButton(u"✕ Delete"_s);
    connect(del_btn, &QPushButton::clicked, this, [this] { delete_leader(); });
    br->addWidget(new_btn, 1);
    br->addWidget(copy_btn, 1);
    br->addWidget(del_btn, 1);
    s->lay->addLayout(br);
    // --- make several at once
    gen_open = new QPushButton(u"⚡ Make several at once ▾"_s);
    gen_open->setCheckable(true);
    s->lay->addWidget(gen_open);
    gen_box = new QWidget();
    {
        auto *gl = new QGridLayout(gen_box);
        gl->setContentsMargins(0, 4, 0, 0);
        gl->setHorizontalSpacing(8);
        gl->setVerticalSpacing(5);
        gl->setColumnStretch(1, 1);
        gen_n = new widgets::Stepper(1, 500, 10);
        gl->addWidget(label(u"How many"_s, u"sub"_s, false), 0, 0);
        gl->addWidget(gen_n, 0, 1, Qt::AlignLeft);
        gen_pers = new QComboBox();
        gen_pers->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        gl->addWidget(label(u"Personality"_s, u"sub"_s, false), 1, 0);
        gl->addWidget(gen_pers, 1, 1);
        gen_back = new QComboBox();
        gen_back->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        gl->addWidget(label(u"Background"_s, u"sub"_s, false), 2, 0);
        gl->addWidget(gen_back, 2, 1);
        gen_only = new QCheckBox(u"Only pairs that give together"_s);
        gl->addWidget(gen_only, 3, 0, 1, 2);
        auto *ar = new QHBoxLayout();
        ar->setSpacing(6);
        gen_atk = new widgets::Stepper(-20, 20, 1);
        gen_def = new widgets::Stepper(-20, 20, 1);
        ar->addWidget(label(u"Attack"_s, u"sub"_s, false));
        ar->addWidget(gen_atk);
        ar->addSpacing(10);
        ar->addWidget(label(u"Defence"_s, u"sub"_s, false));
        ar->addWidget(gen_def);
        ar->addStretch(1);
        gl->addLayout(ar, 4, 0, 1, 2);
        gen_note = label(u""_s, u"faint"_s);
        gen_note->setTextFormat(Qt::RichText);
        gl->addWidget(gen_note, 5, 0, 1, 2);
        gen_go = new QPushButton(u"Make them"_s);
        gen_go->setObjectName(u"primary"_s);
        gl->addWidget(gen_go, 6, 0, 1, 2);
    }
    gen_box->setVisible(false);
    s->lay->addWidget(gen_box);
    connect(gen_open, &QPushButton::toggled, this, [this](bool on) {
        gen_box->setVisible(on);
        gen_open->setText(on ? u"⚡ Make several at once ▴"_s : u"⚡ Make several at once ▾"_s);
        _gen_note();
    });
    connect(gen_n, &widgets::Stepper::valueChanged, this, [this](int) { _gen_note(); });
    connect(gen_atk, &widgets::Stepper::valueChanged, this, [this](int) { _gen_note(); });
    connect(gen_def, &widgets::Stepper::valueChanged, this, [this](int) { _gen_note(); });
    connect(gen_only, &QCheckBox::toggled, this, [this](bool) { _gen_note(); });
    connect(gen_pers, &QComboBox::activated, this, [this](int) { _gen_note(); });
    connect(gen_back, &QComboBox::activated, this, [this](int) { _gen_note(); });
    connect(gen_go, &QPushButton::clicked, this, [this] { generate(); });
    lay->addWidget(s);

    s = new widgets::Section();
    stats_head = label(u"Statistics"_s, u"label"_s);
    stats_head->setTextFormat(Qt::RichText);
    s->lay->addWidget(stats_head);
    auto combo = [&](const QString &text, QComboBox *&c, const QString &pick) {
        auto *r = new QHBoxLayout();
        r->setSpacing(6);
        r->addWidget(label(text, u"sub"_s, false));
        c = new QComboBox();
        c->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        QStringList keys{u"attack+defence"_s};
        keys += traits::STATS;
        for (const QString &k : keys)
            c->addItem(stat_name(k), k);
        c->setCurrentIndex(c->findData(pick));
        connect(c, &QComboBox::activated, this, [this](int) { _stats(); });
        r->addWidget(c, 1);
        s->lay->addLayout(r);
    };
    combo(u"How many have each"_s, count_by, u"attack"_s);
    bars = new CountBars();
    bars->click = [this](double v) {
        const QString k = count_by->currentData().toString();
        filter->setText(u"%1 = %2"_s.arg(k == u"attack+defence" ? k : traits::stat_word(k),
                                         traits::is_percent(k) ? traits::number(std::round(v * 1000) / 10) + u"%"_s
                                                               : traits::number(v)));
    };
    bars->setToolTip(u"Click a bar to list only those leaders (the filter above)."_s);
    s->lay->addWidget(bars);
    combo(u"The best by"_s, best_by, u"attack+defence"_s);
    best = new QTreeWidget();
    best->setColumnCount(4);
    best->setHeaderLabels({u"#"_s, u"Name"_s, u"Nation"_s, u"Value"_s});
    best->setRootIsDecorated(false);
    best->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    best->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    best->setTextElideMode(Qt::ElideRight);
    QHeaderView *bh = best->header();
    bh->setStretchLastSection(false);
    bh->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    bh->setSectionResizeMode(1, QHeaderView::Stretch);
    bh->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    bh->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    connect(best, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it) {
        const qlonglong want = it->data(0, KEY_ROLE).toLongLong();
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->data(0, KEY_ROLE).toLongLong() == want) {
                tree->setCurrentItem(tree->topLevelItem(i));
                tree->scrollToItem(tree->topLevelItem(i));
            }
    });
    best->setToolTip(u"Click a leader to pick it in the list."_s);
    s->lay->addWidget(best);
    lay->addWidget(s);

    s = new widgets::Section();
    where = label(u""_s, u"label"_s);
    s->lay->addWidget(where);
    auto *g = new QGridLayout();
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(5);
    g->setColumnStretch(1, 1);
    auto line = [&](int r, const QString &text, QWidget *w) {
        g->addWidget(label(text, u"sub"_s, false), r, 0);
        g->addWidget(w, r, 1);
    };
    name = new QLineEdit();
    connect(name, &QLineEdit::editingFinished, this, [this] {
        commit(u"name"_s, py::split(name->text().remove(u'"')).join(u' '));
    });
    line(0, u"Name"_s, name);
    date = new QLineEdit();
    date->setPlaceholderText(u"1830.1.1"_s);
    connect(date, &QLineEdit::editingFinished, this, [this] { commit(u"date"_s, py::strip(date->text())); });
    line(1, u"Date"_s, date);
    pers = new QComboBox();
    pers->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(pers, &QComboBox::activated, this, [this](int) { commit(u"personality"_s, pers->currentData().toString()); });
    line(2, u"Personality"_s, pers);
    back = new QComboBox();
    back->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(back, &QComboBox::activated, this, [this](int) { commit(u"background"_s, back->currentData().toString()); });
    line(3, u"Background"_s, back);
    prestige = new QLineEdit();
    prestige->setFixedWidth(90);
    connect(prestige, &QLineEdit::editingFinished, this, [this] {
        QString p = py::strip(prestige->text());
        commit(u"prestige"_s, p.isEmpty() ? u"0"_s : p);
    });
    line(4, u"Prestige"_s, prestige);
    s->lay->addLayout(g);
    combined = label(u""_s, u"sub"_s);
    combined->setTextFormat(Qt::RichText);
    s->lay->addWidget(combined);
    warn = label(u""_s, u"sub"_s);
    warn->setStyleSheet(u"color: #ff786e;"_s);
    s->lay->addWidget(warn);
    traits_btn = new widgets::WrapButton(u"The traits' odds and mass edit: Warfare → Leader traits ▸"_s);
    connect(traits_btn, &QPushButton::clicked, this, [this] { Q_EMIT traitsRequested(); });
    s->lay->addWidget(traits_btn);
    lay->addWidget(s);
}

QString LeaderList::_who() const { return kind == u"sea" ? u"admiral"_s : u"general"_s; }

void LeaderList::set_kind(const QString &k)
{
    if (k == kind)
        return;
    kind = k;
    _cur.reset();
    _fill();
}

void LeaderList::toggle_pick(const QString &t)
{
    if (picked.isEmpty() && tag && *tag != t)     // (the page's nation counts as picked until the first Ctrl+click)
        picked.insert(*tag);
    if (picked.contains(t))
        picked.remove(t);
    else
        picked.insert(t);
    scope = u"picked"_s;
    _fill();
    Q_EMIT scopeChanged();
}

void LeaderList::pick_only(const QString &t)
{
    picked = {t};
    scope = u"picked"_s;
    _fill();
    Q_EMIT scopeChanged();
}

QStringList LeaderList::tags_shown() const
{
    QStringList out;
    if (scope == u"world") {
        if (oob)
            for (const auto &[t, _c] : mod.countries)
                if (oob->exists(t) || oob->want.contains(t))
                    out << t;
        return out;
    }
    if (!picked.isEmpty()) {
        out = QStringList(picked.begin(), picked.end());
        std::sort(out.begin(), out.end());
        return out;
    }
    if (tag)
        out << *tag;
    return out;
}

void LeaderList::refresh(document::Document *d, oob::OOBEdits *o, const std::optional<QString> &t)
{
    doc = d;
    oob = o;
    if (t != tag && t && !picked.contains(*t))   // (another nation picked another way, e.g. the list above: it alone)
        picked = {*t};
    tag = t;
    _load_traits();
    _fill();
}

void LeaderList::_load_traits()
{
    if (!doc || !doc->maps.contains(u"scripts"_s))
        return;
    const QString text = doc->map<scriptstore::ScriptStore>(u"scripts"_s).text(u"common/traits.txt"_s);
    if (_traits_sig && *_traits_sig == text)
        return;
    _traits_sig = text;
    _traits = traits::parse(text);
    _fill_combos();
}

QString LeaderList::_tname(const QString &key) const
{
    if (doc && doc->maps.contains(u"loc"_s))
        if (const auto t = doc->map<localisation::LocEdits>(u"loc"_s).text(key); t && !t->isEmpty())
            return traitnames::plain(*t);           // (a name's colour codes, "§R-1 Attack", not shown in a picker)
    return QString(key).replace(u'_', u' ');
}

void LeaderList::_fill_combos()
{
    for (const auto &[box, list] : {std::pair{pers, u"personality"_s}, std::pair{back, u"background"_s}}) {
        const QVariant cur = box->currentData();
        box->blockSignals(true);
        box->clear();
        for (const traits::Trait &t : _traits.of(list))
            box->addItem(u"%1 — %2"_s.arg(_tname(t.key), traits::summary(t)), t.key);
        if (cur.isValid())
            box->setCurrentIndex(box->findData(cur));
        box->blockSignals(false);
    }
    for (const auto &[box, list] : {std::pair{gen_pers, u"personality"_s}, std::pair{gen_back, u"background"_s}}) {
        const QVariant cur = box->currentData();
        box->blockSignals(true);
        box->clear();
        box->addItem(u"Any, at random"_s, u"*"_s);
        for (const traits::Trait &t : _traits.of(list))
            box->addItem(u"%1 — %2"_s.arg(_tname(t.key), traits::summary(t)), t.key);
        box->setCurrentIndex(cur.isValid() ? std::max(0, box->findData(cur)) : 0);
        box->blockSignals(false);
    }
}

QString LeaderList::_culture_of(const QString &t) const
{
    if (doc && doc->maps.contains(u"nations"_s))
        try {
            const countryhist::NationState st = doc->map<countryhist::NationEdits>(u"nations"_s).state(t);
            if (const auto pc = st.at(u"primary_culture"_s); pc && !pc->isEmpty())
                return *pc;
        } catch (...) {
        }
    return QString();
}

std::pair<QStringList, QStringList> LeaderList::culture_names(const moddata::Mod &mod, const QString &culture)
{
    QStringList first, last, gfirst, glast;
    script::Nodes parsed;
    try {
        parsed = script::parse(script::read_text(mod.p({u"common"_s, u"cultures.txt"_s})));
    } catch (...) {
        return {};
    }
    auto words = [](const script::Node &b) {
        QStringList out;
        for (const script::Node &w : b.kids)
            if (!w.has_key && !w.is_block) {
                QString v = w.value;
                v.remove(u'"');
                v = v.trimmed();
                if (!v.isEmpty())
                    out << v;
            }
        return out;
    };
    for (const script::Node &g : parsed) {
        if (!g.has_key || !g.is_block)
            continue;
        bool here = false;
        QStringList f, l;
        for (const script::Node &c : g.kids) {
            if (!c.has_key || !c.is_block)
                continue;
            for (const script::Node &k : c.kids)
                if (k.has_key && k.is_block && (k.key == u"first_names" || k.key == u"last_names")) {
                    const QStringList w = words(k);
                    (k.key == u"first_names" ? f : l) << w;
                    if (c.key == culture)
                        (k.key == u"first_names" ? first : last) << w;
                }
            here = here || c.key == culture;
        }
        if (here) {
            gfirst = f;
            glast = l;
        }
    }
    if (first.isEmpty())
        first = gfirst;
    if (last.isEmpty())
        last = glast;
    first.removeDuplicates();
    last.removeDuplicates();
    return {first, last};
}

void LeaderList::_gen_note()
{
    if (!gen_box->isVisible())
        return;
    const QStringList tags = tags_shown();
    const long long n = gen_n->value();
    const QString who = _who() + (n == 1 ? QString() : u"s"_s);
    gen_atk->setEnabled(gen_only->isChecked());
    gen_def->setEnabled(gen_only->isChecked());
    QStringList lines;
    if (tags.isEmpty() || scope == u"world") {
        gen_go->setEnabled(false);
        gen_go->setText(u"Pick a nation first"_s);
        gen_note->setText(scope == u"world" ? u"Pick nations (not the world) to make leaders for them."_s : QString());
        return;
    }
    for (const QString &t : tags.mid(0, 6)) {
        const QString cu = _culture_of(t);
        const auto [f, l] = culture_names(mod, cu);
        const moddata::Culture *ci = cu.isEmpty() ? nullptr : mod.cultures.get(cu);
        lines << (cu.isEmpty() ? u"%1: no primary culture, so no names: \"%2 1\", \"%2 2\" …"_s.arg(t, _who())
                  : f.isEmpty() || l.isEmpty()
                      ? u"%1: %2 has no name lists: \"%3 1\", \"%3 2\" …"_s.arg(t, ci ? ci->name : cu, _who())
                      : u"%1: names of the %2 culture (%3 first names × %4 last names)"_s.arg(t, ci ? ci->name : cu)
                            .arg(f.size())
                            .arg(l.size()));
    }
    if (tags.size() > 6)
        lines << u"… and %1 more nations"_s.arg(tags.size() - 6);
    gen_note->setText(lines.join(u"<br>"_s));
    gen_go->setEnabled(true);
    gen_go->setText(tags.size() == 1 ? u"Make %1 %2 for %3"_s.arg(n).arg(who, tags[0])
                                     : u"Make %1 %2 for each of the %3 nations"_s.arg(n).arg(who).arg(tags.size()));
}

void LeaderList::generate()
{
    const QStringList tags = tags_shown();
    if (tags.isEmpty() || scope == u"world" || !oob || !edit_fn)
        return;
    warn->setText(QString());
    // the pairs to draw from: every personality × background the two choices allow (never no_personality /
    // no_background when "any"), only those with the wanted attack and defence when ticked
    const QString wp = gen_pers->currentData().toString(), wb = gen_back->currentData().toString();
    std::vector<std::pair<const traits::Trait *, const traits::Trait *>> pairs;
    for (const traits::Trait &p : _traits.personality) {
        if (wp == u"*" ? traits::is_none(p.key) : p.key != wp)
            continue;
        for (const traits::Trait &b : _traits.background) {
            if (wb == u"*" ? traits::is_none(b.key) : b.key != wb)
                continue;
            if (gen_only->isChecked() &&
                (std::abs(p.get(u"attack"_s) + b.get(u"attack"_s) - double(gen_atk->value())) > 1e-6 ||
                 std::abs(p.get(u"defence"_s) + b.get(u"defence"_s) - double(gen_def->value())) > 1e-6))
                continue;
            pairs.emplace_back(&p, &b);
        }
    }
    if (pairs.empty()) {
        gen_note->setText(u"<span style='color:%1'>No personality and background give attack %2 and defence %3 "
                          u"together. Pick other values or other traits.</span>"_s.arg(theme::DANGER)
                              .arg(gen_atk->value())
                              .arg(gen_def->value()));
        return;
    }
    const long long n = gen_n->value();
    const history::Date dt = doc && doc->date ? *doc->date : history::Date{1836, 1, 1};
    const QString date = u"%1.%2.%3"_s.arg(std::max(1LL, std::get<0>(dt) - 1)).arg(std::get<1>(dt)).arg(std::get<2>(dt));
    QRandomGenerator *rnd = QRandomGenerator::global();
    long long last_at = -1;
    QString last_tag;
    for (const QString &t : tags) {
        const auto [first, lastn] = culture_names(mod, _culture_of(t));
        QSet<QString> taken;
        const oob::OOB &st = oob->state(t);
        for (const oob::Leader &l : st.leaders)
            taken.insert(l.get(u"name"_s));
        std::vector<oob::Leader> made;
        for (long long i = 0; i < n; ++i) {
            QString name;
            if (!first.isEmpty() && !lastn.isEmpty())
                for (int tries = 0; tries < 60 && (name.isEmpty() || taken.contains(name)); ++tries)
                    name = first[rnd->bounded(int(first.size()))] + u' ' + lastn[rnd->bounded(int(lastn.size()))];
            if (name.isEmpty() || taken.contains(name)) {
                long long k = 1;
                do
                    name = u"%1 %2"_s.arg(kind == u"sea" ? u"Admiral"_s : u"General"_s).arg(k++);
                while (taken.contains(name));
            }
            taken.insert(name);
            const auto &[p, b] = pairs[size_t(rnd->bounded(int(pairs.size())))];
            oob::Leader l;
            l.set(u"name"_s, name);
            l.set(u"date"_s, date);
            l.set(u"type"_s, kind);
            l.set(u"personality"_s, p->key);
            l.set(u"background"_s, b->key);
            l.set(u"prestige"_s, u"0"_s);
            made.push_back(l);
        }
        last_at = static_cast<long long>(st.leaders.size());
        last_tag = t;
        edit_fn(u"%1 new %2s for %3"_s.arg(n).arg(_who(), t), t, [made](oob::OOB &o) {
            for (const oob::Leader &l : made)
                o.leaders.push_back(l);
        });
    }
    if (last_at >= 0)
        _cur = std::make_tuple(0, last_tag, last_at);
    _fill();
}

void LeaderList::_fill()
{
    _filling = true;
    for (const auto &[k, b] : scope_btns)
        b->setChecked(k == scope);
    const QString who = _who(), Who = kind == u"sea" ? u"Admirals"_s : u"Generals"_s;
    head->setText(Who);
    {
        QStringList names;
        QStringList pk(picked.begin(), picked.end());
        std::sort(pk.begin(), pk.end());
        for (const QString &t : pk)
            names << t;
        if (names.isEmpty() && tag)
            names << *tag;
        picked_note->setText(
            scope == u"world" ? u"Every nation of the world."_s
                              : u"Click a nation on the map to pick it, Ctrl+click adds or takes one away. Picked: %1."_s.arg(
                                    names.isEmpty() ? u"none"_s : names.join(u", "_s)));
    }
    // --- the rows
    rows.clear();
    if (oob)
        for (const QString &t : tags_shown()) {
            const oob::OOB &st = oob->state(t);
            auto add = [&](int w, long long i, const QHash<QString, QString> &f, const QString &unit) {
                Row r{t, w, i, f, unit};
                for (const QString &k : {u"personality"_s, u"background"_s})
                    if (const traits::Trait *tr = _traits.find(f.value(k))) {
                        r.atk += tr->get(u"attack"_s);
                        r.def += tr->get(u"defence"_s);
                        for (const QString &st : traits::STATS)
                            r.st[st] += tr->get(st);
                    }
                rows.push_back(r);
            };
            for (size_t i = 0; i < st.leaders.size(); ++i) {
                const oob::Leader &l = st.leaders[i];
                const bool sea = l.get(u"type"_s) == u"sea";
                if (sea != (kind == u"sea"))
                    continue;
                QHash<QString, QString> f;
                for (const auto &[k, v] : l.fields)
                    f.insert(k, v);
                add(0, static_cast<long long>(i), f, QString());
            }
            if (kind == u"land") {
                for (size_t i = 0; i < st.armies.size(); ++i)
                    if (const auto [li, f] = leaderbox::leader_of(st.armies[i].extra); li)
                        add(1, static_cast<long long>(i), f, st.armies[i].name);
            } else {
                for (size_t i = 0; i < st.navies.size(); ++i)
                    if (const auto [li, f] = leaderbox::leader_of(st.navies[i].extra); li)
                        add(2, static_cast<long long>(i), f, st.navies[i].name);
            }
        }
    // --- the list
    const QString flt = py::strip(filter->text()).toLower();
    tree->setSortingEnabled(false);
    tree->blockSignals(true);
    tree->clear();
    QTreeWidgetItem *sel = nullptr;
    double sa = 0, sd = 0;
    long long shown = 0;
    shown_rows.clear();
    // "attack = 3", "defence=-1", "speed = 10%", "attack+defence = 4": the leaders with that value (a click on a bar)
    static const QRegularExpression VAL_RE(u"^\\s*([a-z+ ]+?)\\s*=\\s*(-?\\d+(?:\\.\\d+)?)\\s*(%?)\\s*$"_s);
    std::optional<std::pair<QString, double>> want;
    if (const auto m = VAL_RE.match(flt); m.hasMatch()) {
        QString k = m.captured(1).remove(u' ');
        for (const QString &st : traits::STATS)
            if (traits::stat_word(st).toLower() == k)
                k = st;
        if (k == u"attack+defence" || traits::STATS.contains(k)) {
            double v = m.captured(2).toDouble();
            if (traits::is_percent(k))
                v /= 100;
            want = std::pair{k, v};
        }
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row &r = rows[i];
        const moddata::Country *c = mod.countries.get(r.tag);
        const QString nation = c && !c->name.isEmpty() ? c->name : r.tag;
        QStringList other;
        for (const QString &k : {u"personality"_s, u"background"_s})
            if (const traits::Trait *tr = _traits.find(r.f.value(k))) {
                traits::Trait rest = *tr;
                rest.v.remove(u"attack"_s);
                rest.v.remove(u"defence"_s);
                if (!rest.v.isEmpty())
                    other << traits::summary(rest);
            }
        if (want) {
            if (std::abs(value(r, want->first) - want->second) > 1e-6)
                continue;
        } else if (!flt.isEmpty()) {
            const QString hay = QStringList{r.f.value(u"name"_s), r.tag, nation, r.f.value(u"personality"_s),
                                            r.f.value(u"background"_s), _tname(r.f.value(u"personality"_s)),
                                            _tname(r.f.value(u"background"_s)), r.unit}
                                    .join(u' ')
                                    .toLower();
            if (!hay.contains(flt))
                continue;
        }
        ++shown;
        shown_rows.push_back(i);
        sa += r.atk;
        sd += r.def;
        auto *it = new Item(tree);
        it->setText(0, r.f.value(u"name"_s) + (r.unit.isEmpty() ? QString() : u"  (%1)"_s.arg(r.unit)));
        it->setText(1, r.tag);
        it->setToolTip(1, nation);
        it->setText(2, signed_num(r.atk));
        it->setData(2, NUM_ROLE, r.atk);
        it->setText(3, signed_num(r.def));
        it->setData(3, NUM_ROLE, r.def);
        it->setText(4, other.join(u", "_s));
        it->setToolTip(0, u"%1 — %2 / %3"_s.arg(r.f.value(u"name"_s), _tname(r.f.value(u"personality"_s)),
                                                  _tname(r.f.value(u"background"_s))));
        it->setData(0, KEY_ROLE, static_cast<qlonglong>(i));
        for (int col : {2, 3})
            it->setTextAlignment(col, Qt::AlignRight | Qt::AlignVCenter);
        if (_cur && std::get<0>(*_cur) == r.where && std::get<1>(*_cur) == r.tag && std::get<2>(*_cur) == r.index)
            sel = it;
    }
    tree->setSortingEnabled(true);
    if (sel)
        tree->setCurrentItem(sel);
    tree->blockSignals(false);
    const QStringList shown_tags = tags_shown();
    if (shown_tags.isEmpty())
        count->setText(u"Click a nation on the map, or pick one above."_s);
    else
        count->setText(u"<b>%1 %2%3</b>%4"_s.arg(shown).arg(shown == 1 ? who : who + u"s"_s,
                                                    scope == u"world" ? u" in the world"_s
                                                    : shown_tags.size() == 1 ? u" of %1"_s.arg(shown_tags[0])
                                                                             : u" of %1 nations"_s.arg(shown_tags.size()),
                                                    shown ? u" — on average %1 attack, %2 defence"_s.arg(
                                                                signed_num(std::round(sa / shown * 10) / 10),
                                                                signed_num(std::round(sd / shown * 10) / 10))
                                                          : QString()));
    _stats();
    _gen_note();
    _filling = false;
    _picked_row();
}

void LeaderList::_stats()
{
    const QString who = _who() + (shown_rows.size() == 1 ? QString() : u"s"_s);
    stats_head->setText(u"<b>Statistics</b> of the %1 %2 listed above"_s.arg(shown_rows.size()).arg(who));
    const QString ck = count_by->currentData().toString(), bk = best_by->currentData().toString();
    std::map<double, int> n;
    for (size_t i : shown_rows)
        ++n[std::round(value(rows[i], ck) * 1e6) / 1e6];
    std::vector<std::pair<double, int>> r(n.rbegin(), n.rend());
    bars->set(ck, std::move(r), int(shown_rows.size()));
    // the best five (more when tied with the fifth, at most 30)
    std::vector<size_t> order = shown_rows;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return value(rows[a], bk) > value(rows[b], bk); });
    best->clear();
    int rank = 0;
    double last = 0;
    for (size_t k = 0; k < order.size() && k < 30; ++k) {
        const Row &row = rows[order[k]];
        const double v = value(row, bk);
        if (k >= 5 && std::abs(v - last) > 1e-6)
            break;
        if (k == 0 || std::abs(v - last) > 1e-6)
            rank = int(k) + 1;
        last = v;
        auto *it = new QTreeWidgetItem(best);
        it->setText(0, QString::number(rank));
        it->setText(1, row.f.value(u"name"_s));
        it->setToolTip(1, u"%1: %2 / %3"_s.arg(row.f.value(u"name"_s), _tname(row.f.value(u"personality"_s)),
                                                 _tname(row.f.value(u"background"_s))));
        it->setText(2, row.tag);
        it->setText(3, stat_value(bk, v));
        it->setForeground(3, QColor(v < 0 ? theme::DANGER : theme::GOLD));
        it->setData(0, KEY_ROLE, static_cast<qlonglong>(order[k]));
        it->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
    }
    const int rh = std::max(18, best->fontMetrics().height() + 4);
    best->setFixedHeight(best->header()->sizeHint().height() + std::max(1, best->topLevelItemCount()) * rh + 4);
}

const Row *LeaderList::_row() const
{
    QTreeWidgetItem *it = tree->currentItem();
    if (!it || !it->isSelected())
        return nullptr;
    const long long i = it->data(0, KEY_ROLE).toLongLong();
    return i >= 0 && i < static_cast<long long>(rows.size()) ? &rows[size_t(i)] : nullptr;
}

void LeaderList::_picked_row()
{
    const Row *r = _row();
    if (!_filling)
        _cur = r ? std::optional(std::make_tuple(r->where, r->tag, r->index)) : std::nullopt;
    for (QWidget *w : std::initializer_list<QWidget *>{name, date, pers, back, prestige, copy_btn, del_btn})
        w->setEnabled(r != nullptr);
    new_btn->setEnabled(oob && (r || tag));
    new_btn->setText(u"＋ New %1"_s.arg(_who()));
    warn->clear();
    if (!r) {
        where->setText(u"Pick a %1 in the list to change it."_s.arg(_who()));
        for (QLineEdit *e : {name, date, prestige})
            e->clear();
        combined->clear();
        return;
    }
    const moddata::Country *c = mod.countries.get(r->tag);
    where->setText(u"%1 — %2, %3"_s.arg(r->f.value(u"name"_s), c && !c->name.isEmpty() ? c->name : r->tag,
                                         r->where == 0 ? u"in the pool (no %1 to lead yet)"_s.arg(kind == u"sea" ? u"navy"_s : u"army"_s)
                                                       : u"leads %1"_s.arg(r->unit)));
    if (!name->hasFocus())
        name->setText(r->f.value(u"name"_s));
    if (!date->hasFocus())
        date->setText(r->f.value(u"date"_s));
    if (!prestige->hasFocus())
        prestige->setText(r->f.value(u"prestige"_s, u"0"_s));
    for (const auto &[box, k] : {std::pair{pers, u"personality"_s}, std::pair{back, u"background"_s}}) {
        const QString v = r->f.value(k);
        box->blockSignals(true);
        if (box->findData(v) < 0)
            box->addItem(v.isEmpty() ? u"(none)"_s : u"%1 — not in traits.txt"_s.arg(v), v);
        box->setCurrentIndex(box->findData(v));
        box->blockSignals(false);
    }
    traits::Trait sum;
    for (const QString &k : {u"personality"_s, u"background"_s})
        if (const traits::Trait *tr = _traits.find(r->f.value(k)))
            for (const auto &[s, v] : tr->v)
                sum.v[s] = traits::vval(traits::vkey(sum.get(s) + v));
    combined->setText(u"Together: <b>%1</b>"_s.arg(traits::summary(sum).toHtmlEscaped()));
}

void LeaderList::select(const QString &t, int w, long long index)
{
    _cur = std::make_tuple(w, t, index);
    _fill();
}

void LeaderList::commit(const QString &field, const QString &value)
{
    const Row *r = _row();
    if (!r || !edit_fn || _filling)
        return;
    if (r->f.value(field) == value)
        return;
    if (field == u"name" && value.isEmpty()) {
        warn->setText(u"Give the %1 a name."_s.arg(_who()));
        name->setText(r->f.value(u"name"_s));
        return;
    }
    if (field == u"prestige" && !NUM_RE.match(value).hasMatch()) {
        warn->setText(u"The prestige needs a number (like 0.1)."_s);
        return;
    }
    if (field == u"date" && !DATE_RE.match(value).hasMatch()) {
        warn->setText(u"The date is written year.month.day, like 1830.1.1."_s);
        return;
    }
    const QString lbl = u"%1 %2: %3 → %4"_s.arg(kind == u"sea" ? u"Admiral"_s : u"General"_s, r->f.value(u"name"_s), field,
                                               field == u"personality" || field == u"background" ? _tname(value) : value);
    const int w = r->where;
    const long long i = r->index;
    const QString t = r->tag;
    _cur = std::make_tuple(w, t, i);
    edit_fn(lbl, t, [w, i, field, value](oob::OOB &o) {
        if (w == 0) {
            if (i < static_cast<long long>(o.leaders.size()))
                o.leaders[size_t(i)].set(field, value);
            return;
        }
        if (w == 1 && i >= static_cast<long long>(o.armies.size()))
            return;
        if (w == 2 && i >= static_cast<long long>(o.navies.size()))
            return;
        QStringList &ex = w == 1 ? o.armies[size_t(i)].extra : o.navies[size_t(i)].extra;
        auto [li, f] = leaderbox::leader_of(ex);
        if (!li)
            return;
        f[field] = value;
        ex[*li] = leaderbox::leader_text(f);
    });
}

void LeaderList::new_leader()
{
    const Row *r = _row();
    const QStringList shown_tags = tags_shown();
    const std::optional<QString> t = shown_tags.size() == 1 ? std::optional(shown_tags[0]) : r ? std::optional(r->tag) : tag;
    if (!t || !oob || !edit_fn) {
        warn->setText(u"Pick a nation first."_s);
        return;
    }
    const history::Date dt = doc && doc->date ? *doc->date : history::Date{1836, 1, 1};
    oob::Leader l;
    const QString who = kind == u"sea" ? u"Admiral"_s : u"General"_s;
    l.set(u"name"_s, u"New %1"_s.arg(_who()));
    l.set(u"date"_s, u"%1.%2.%3"_s.arg(std::max(1LL, std::get<0>(dt) - 1)).arg(std::get<1>(dt)).arg(std::get<2>(dt)));
    l.set(u"type"_s, kind);
    l.set(u"personality"_s, _traits.find(u"no_personality"_s) || _traits.personality.empty()
                                ? u"no_personality"_s : _traits.personality.front().key);
    l.set(u"background"_s, _traits.find(u"no_background"_s) || _traits.background.empty()
                               ? u"no_background"_s : _traits.background.front().key);
    l.set(u"prestige"_s, u"0"_s);
    const long long at = static_cast<long long>(oob->state(*t).leaders.size());
    _cur = std::make_tuple(0, *t, at);
    edit_fn(u"New %1 for %2"_s.arg(who.toLower(), *t), *t, [l](oob::OOB &o) { o.leaders.push_back(l); });
}

void LeaderList::copy_leader()
{
    const Row *r = _row();
    if (!r || !oob || !edit_fn)
        return;
    oob::Leader l;
    for (const QString &k : oob::LEADER_KEYS)
        if (r->f.contains(k))
            l.set(k, r->f.value(k));
    l.set(u"type"_s, kind);
    if (r->where == 0) {
        const oob::OOB &st = oob->state(r->tag);
        if (r->index < static_cast<long long>(st.leaders.size())) {
            l.fields = st.leaders[size_t(r->index)].fields;
            l.extra = st.leaders[size_t(r->index)].extra;
        }
    }
    const QString t = r->tag;
    const long long at = static_cast<long long>(oob->state(t).leaders.size());
    _cur = std::make_tuple(0, t, at);
    edit_fn(u"Copy of %1 %2 (%3)"_s.arg(_who(), r->f.value(u"name"_s), t), t, [l](oob::OOB &o) { o.leaders.push_back(l); });
}

void LeaderList::delete_leader()
{
    const Row *r = _row();
    if (!r || !edit_fn)
        return;
    const int w = r->where;
    const long long i = r->index;
    const QString t = r->tag, nm = r->f.value(u"name"_s);
    _cur.reset();
    edit_fn(u"%1 %2 (%3) deleted"_s.arg(kind == u"sea" ? u"Admiral"_s : u"General"_s, nm, t), t, [w, i](oob::OOB &o) {
        if (w == 0) {
            if (i < static_cast<long long>(o.leaders.size()))
                o.leaders.erase(o.leaders.begin() + i);
            return;
        }
        if ((w == 1 && i >= static_cast<long long>(o.armies.size())) || (w == 2 && i >= static_cast<long long>(o.navies.size())))
            return;
        QStringList &ex = w == 1 ? o.armies[size_t(i)].extra : o.navies[size_t(i)].extra;
        if (const auto [li, _f] = leaderbox::leader_of(ex); li)
            ex.removeAt(*li);
    });
}

}  // namespace leaderlist
