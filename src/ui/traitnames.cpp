// traitnames.cpp — see traitnames.h
#include "ui/traitnames.h"
#include "core/localisation.h"
#include "core/pypath.h"
#include "core/scriptstore.h"
#include "core/settings.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cmath>
#include <map>

using namespace Qt::StringLiterals;
using widgets::label;

namespace traitnames {

const QStringList CODES = {u"W"_s, u"Y"_s, u"R"_s, u"G"_s, u"B"_s, u"b"_s, u"g"_s};

QString code_word(const QString &code)
{
    static const QHash<QString, QString> W = {{u"W"_s, u"white"_s}, {u"Y"_s, u"yellow"_s}, {u"R"_s, u"red"_s},
                                              {u"G"_s, u"green"_s}, {u"B"_s, u"blue"_s},   {u"b"_s, u"black"_s},
                                              {u"g"_s, u"grey"_s}};
    return W.value(code, code);
}

QColor colour(const QString &code)
{
    // the game's own (gamefont COLOURS); blue and black lighter: the editor's lists are dark
    static const QHash<QString, QColor> C = {{u"W"_s, QColor(255, 255, 255)}, {u"Y"_s, QColor(255, 189, 0)},
                                             {u"R"_s, QColor(255, 50, 50)},    {u"G"_s, QColor(0, 190, 20)},
                                             {u"B"_s, QColor(70, 120, 255)},   {u"b"_s, QColor(110, 110, 110)},
                                             {u"g"_s, QColor(176, 176, 176)}};
    return C.value(code, QColor(theme::INK));
}

QString plain(const QString &text)
{
    QString out;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] == u'§') {
            ++i;                              // (the code letter goes too)
            continue;
        }
        out += text[i];
    }
    return out;
}

namespace {
QString signed_num(double v) { return (v > 0 ? u"+"_s : QString()) + traits::number(v); }
QString word(const QString &stat)
{
    const QString w = traits::stat_word(stat);
    return w.left(1).toUpper() + w.mid(1);
}
double in_name(const QString &stat, double v)   // the value as the name shows it
{
    return traits::is_percent(stat) ? std::round(v * 1000) / 10 : v;
}
std::optional<double> num(const QString &s)
{
    bool ok = false;
    const double v = QString(s).remove(u'%').remove(u'+').trimmed().toDouble(&ok);
    return ok ? std::optional(v) : std::nullopt;
}
}  // namespace

QString shown_value(const QString &stat, double v)
{
    return signed_num(in_name(stat, v)) + (traits::is_percent(stat) ? u"%"_s : QString());
}

QString name_of(const traits::Trait &t, const Rules &r)
{
    QStringList parts;
    bool coloured = false;
    for (const QString &st : r.stats) {
        const double v = t.get(st);
        if (v == 0)
            continue;
        const double shown = in_name(st, v);
        QString code;
        for (const Rule &rule : r.rules)
            if ((rule.stat.isEmpty() || rule.stat == st) && (!rule.lo || shown >= *rule.lo - 1e-9) &&
                (!rule.hi || shown <= *rule.hi + 1e-9)) {
                code = rule.code;
                break;
            }
        const QString part = shown_value(st, v) + u' ' + word(st);
        parts << (!code.isEmpty() ? u"§"_s + code + part : coloured ? u"§!"_s + part : part);
        coloured = !code.isEmpty();
    }
    if (parts.isEmpty() || traits::is_none(t.key))
        return (r.none_code.isEmpty() ? QString() : u"§"_s + r.none_code) + r.none;
    return parts.join(u", "_s);
}

Rules learn(const traits::Lists &lists, const std::function<QString(const QString &)> &name, int *matched)
{
    Rules r;
    r.rules.clear();
    int n = 0;
    std::map<QString, std::map<double, QString>> seen;   // stat -> value shown -> code
    bool none_found = false;
    for (const auto *l : {&lists.personality, &lists.background})
        for (const traits::Trait &t : *l) {
            const QString nm = name(t.key);
            if (nm.isEmpty())
                continue;
            if (traits::is_none(t.key)) {
                if (!none_found) {
                    none_found = true;
                    r.none = plain(nm).trimmed();
                    r.none_code = nm.startsWith(u'§') && nm.size() > 1 ? QString(nm[1]) : QString();
                }
                continue;
            }
            Rules bare = r;
            bare.rules.clear();
            bare.none_code.clear();
            if (plain(nm).trimmed() != plain(name_of(t, bare)))
                continue;
            ++n;
            // the parts in order: each one's leading code
            const QStringList parts = nm.split(u", "_s);
            qsizetype k = 0;
            for (const QString &st : r.stats) {
                const double v = t.get(st);
                if (v == 0 || k >= parts.size())
                    continue;
                const QString &p = parts[k++];
                QString code = p.startsWith(u'§') && p.size() > 1 ? QString(p[1]) : QString();
                if (code == u"!")
                    code.clear();
                seen[st][in_name(st, v)] = code;
            }
        }
    if (matched)
        *matched = n;
    if (!n)
        return r;
    // the same colours for every stat: one set of rules for any stat
    bool same = true;
    for (const auto &[st, m] : seen)
        same = same && m == seen.begin()->second;
    std::vector<std::pair<QString, std::map<double, QString>>> groups;
    if (same)
        groups.emplace_back(QString(), seen.begin()->second);
    else
        for (const auto &[st, m] : seen)
            groups.emplace_back(st, m);
    for (const auto &[st, m] : groups) {
        // runs of one colour over the values seen; the lowest and the highest run reach on without a limit
        std::vector<std::pair<double, QString>> vals(m.begin(), m.end());
        for (size_t i = 0; i < vals.size();) {
            size_t j = i;
            while (j + 1 < vals.size() && vals[j + 1].second == vals[i].second)
                ++j;
            if (!vals[i].second.isEmpty()) {
                Rule rule{st, vals[i].first, vals[j].first, vals[i].second};
                if (i == 0)
                    rule.lo.reset();
                if (j == vals.size() - 1)
                    rule.hi.reset();
                r.rules.push_back(rule);
            }
            i = j + 1;
        }
    }
    return r;
}

Rules Rules::defaults()
{
    Rules r;
    r.rules = {Rule{QString(), std::nullopt, -1.0, u"R"_s}, Rule{QString(), 3.0, std::nullopt, u"G"_s}};
    return r;
}

QString Rules::to_json() const
{
    QJsonObject o;
    o[u"stats"_s] = QJsonArray::fromStringList(stats);
    o[u"none"_s] = none;
    o[u"none_code"_s] = none_code;
    o[u"auto"_s] = auto_names;
    QJsonArray a;
    for (const Rule &r : rules) {
        QJsonObject x;
        x[u"stat"_s] = r.stat;
        if (r.lo)
            x[u"lo"_s] = *r.lo;
        if (r.hi)
            x[u"hi"_s] = *r.hi;
        x[u"code"_s] = r.code;
        a.append(x);
    }
    o[u"rules"_s] = a;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

Rules Rules::from_json(const QString &text)
{
    const QJsonObject o = QJsonDocument::fromJson(text.toUtf8()).object();
    Rules r;
    QStringList st;
    for (const auto &v : o[u"stats"_s].toArray())
        if (traits::STATS.contains(v.toString()))
            st << v.toString();
    r.stats = st;
    r.none = o[u"none"_s].toString(r.none);
    r.none_code = o[u"none_code"_s].toString();
    r.auto_names = o[u"auto"_s].toBool();
    for (const auto &v : o[u"rules"_s].toArray()) {
        const QJsonObject x = v.toObject();
        Rule rule;
        rule.stat = x[u"stat"_s].toString();
        if (x.contains(u"lo"_s))
            rule.lo = x[u"lo"_s].toDouble();
        if (x.contains(u"hi"_s))
            rule.hi = x[u"hi"_s].toDouble();
        rule.code = x[u"code"_s].toString(u"W"_s);
        r.rules.push_back(rule);
    }
    return r;
}

static QString key_of(const moddata::Mod &mod)
{
    return u"trait_names/"_s + pypath::basename(mod.root).toLower().replace(u'/', u'_');
}

std::optional<Rules> stored(const moddata::Mod &mod)
{
    const auto s = settings_::settings();
    const QString t = s->value(key_of(mod)).toString();
    if (t.isEmpty())
        return std::nullopt;
    return Rules::from_json(t);
}

void store(const moddata::Mod &mod, const Rules &r)
{
    settings_::settings()->setValue(key_of(mod), r.to_json());
}

Rules for_mod(const moddata::Mod &mod, const traits::Lists &lists, const std::function<QString(const QString &)> &name)
{
    if (auto r = stored(mod))
        return *r;
    int n = 0;
    Rules r = learn(lists, name, &n);
    return n ? r : Rules::defaults();
}

// --- the delegate
void ColourDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const
{
    QStyleOptionViewItem o(opt);
    initStyleOption(&o, idx);
    const QString text = o.text;
    o.text.clear();
    const QWidget *w = o.widget;
    QStyle *style = w ? w->style() : nullptr;
    if (style)
        style->drawControl(QStyle::CE_ItemViewItem, &o, p, w);
    const QRect r = style ? style->subElementRect(QStyle::SE_ItemViewItemText, &o, w).adjusted(3, 0, -3, 0) : o.rect;
    p->save();
    p->setClipRect(r);
    p->setFont(o.font);
    const QFontMetrics fm(o.font);
    const QColor base = o.palette.color(QPalette::Text);
    QColor cur = base;
    int x = r.left();
    const int y = r.top() + (r.height() + fm.ascent() - fm.descent()) / 2;
    QString seg;
    auto flush = [&] {
        if (seg.isEmpty())
            return;
        p->setPen(cur);
        p->drawText(x, y, seg);
        x += fm.horizontalAdvance(seg);
        seg.clear();
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] == u'§' && i + 1 < text.size()) {
            flush();
            const QString c(text[i + 1]);
            cur = c == u"!" ? base : CODES.contains(c) ? colour(c) : cur;
            ++i;
            continue;
        }
        seg += text[i];
    }
    flush();
    p->restore();
}

// --- the view
namespace {
constexpr int KEY_ROLE = Qt::UserRole;
QIcon swatch(const QString &code)
{
    QPixmap px(16, 16);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setPen(QPen(QColor(20, 20, 24), 2));
    p.setBrush(colour(code));
    p.drawRoundedRect(QRectF(1, 1, 14, 14), 3, 3);
    return QIcon(px);
}
void fill_colours(QComboBox *c, bool with_none)
{
    if (with_none)
        c->addItem(u"no colour"_s, QString());
    for (const QString &k : CODES)
        c->addItem(swatch(k), u"%1 (§%2)"_s.arg(code_word(k), k), k);
}
}  // namespace

NamesView::NamesView(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // --- left: the rules
    auto *s = new widgets::Section();
    s->lay->addWidget(label(u"Names in the game"_s, u"title"_s));
    s->lay->addWidget(label(u"A trait's name can be made from its stats, coloured by value with the game's colour codes "
                            u"(§R red, §G green …). Set how here; the right side shows every name now and from these "
                            u"rules. The rules are kept by the editor for this mod."_s,
                            u"faint"_s));
    s->lay->addWidget(label(u"Stats written into the name"_s, u"sub"_s));
    auto *tg = new QGridLayout();
    tg->setHorizontalSpacing(10);
    tg->setVerticalSpacing(4);
    for (qsizetype i = 0; i < traits::STATS.size(); ++i) {
        const QString &st = traits::STATS[i];
        auto *c = new QCheckBox(word(st));
        connect(c, &QCheckBox::toggled, this, [this](bool) { _from_widgets(); });
        tg->addWidget(c, int(i / 3), int(i % 3));
        stat_ticks.emplace_back(st, c);
    }
    s->lay->addLayout(tg);
    auto *nr = new QHBoxLayout();
    nr->setSpacing(6);
    nr->addWidget(label(u"A trait with none of them"_s, u"sub"_s, false));
    none_name = new QLineEdit();
    connect(none_name, &QLineEdit::editingFinished, this, [this] { _from_widgets(); });
    nr->addWidget(none_name, 2);
    none_colour = new QComboBox();
    fill_colours(none_colour, true);
    connect(none_colour, &QComboBox::activated, this, [this](int) { _from_widgets(); });
    nr->addWidget(none_colour, 1);
    s->lay->addLayout(nr);
    s->lay->addSpacing(6);
    s->lay->addWidget(label(u"Colours (the first rule that fits a value colours it; values as the name shows them)"_s,
                            u"sub"_s));
    rule_lay = new QVBoxLayout();
    rule_lay->setSpacing(4);
    s->lay->addLayout(rule_lay);
    add_rule = new QPushButton(u"＋ Add a colour rule"_s);
    connect(add_rule, &QPushButton::clicked, this, [this] {
        add_rule_row(Rule{QString(), std::nullopt, std::nullopt, u"R"_s});
        _from_widgets();
    });
    s->lay->addWidget(add_rule);
    learn_btn = new QPushButton(u"Read the rules from the mod's names"_s);
    learn_btn->setToolTip(u"For a mod that already names its traits by their stats (\"§R-1 Attack\"): the colours "
                          u"its names use become the rules."_s);
    connect(learn_btn, &QPushButton::clicked, this, [this] { read_from_mod(); });
    s->lay->addWidget(learn_btn);
    s->lay->addSpacing(6);
    auto_box = new QCheckBox(u"New, copied and changed traits get their name from these rules (Odds && mass edit)"_s);
    connect(auto_box, &QCheckBox::toggled, this, [this](bool) { _from_widgets(); });
    s->lay->addWidget(auto_box);
    write_all = new QPushButton(u"Write every name"_s);
    write_all->setObjectName(u"primary"_s);
    connect(write_all, &QPushButton::clicked, this, [this] { write(false); });
    write_missing = new QPushButton(u"Only the traits without a name"_s);
    connect(write_missing, &QPushButton::clicked, this, [this] { write(true); });
    auto *wr = new QHBoxLayout();
    wr->setSpacing(6);
    wr->addWidget(write_all, 1);
    wr->addWidget(write_missing, 1);
    s->lay->addLayout(wr);
    note = label(u""_s, u"sub"_s);
    note->setTextFormat(Qt::RichText);
    s->lay->addWidget(note);
    s->lay->addStretch(1);
    lay->addWidget(s, 4);

    // --- right: every name, now and from the rules
    s = new widgets::Section();
    preview = new QTreeWidget();
    preview->setColumnCount(4);
    preview->setHeaderLabels({u"Trait"_s, u"List"_s, u"Name now"_s, u"From the rules"_s});
    preview->setRootIsDecorated(false);
    preview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    preview->setTextElideMode(Qt::ElideRight);
    preview->setSelectionMode(QAbstractItemView::NoSelection);
    auto *del = new ColourDelegate(preview);
    preview->setItemDelegateForColumn(2, del);
    preview->setItemDelegateForColumn(3, del);
    QHeaderView *h = preview->header();
    h->setStretchLastSection(false);
    h->setSectionResizeMode(0, QHeaderView::Stretch);
    h->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    h->setSectionResizeMode(2, QHeaderView::Stretch);
    h->setSectionResizeMode(3, QHeaderView::Stretch);
    preview->setMinimumHeight(400);
    s->lay->addWidget(preview, 1);
    lay->addWidget(s, 5);
}

QString NamesView::_name(const QString &key) const
{
    if (doc && doc->maps.contains(u"loc"_s))
        if (const auto t = doc->map<localisation::LocEdits>(u"loc"_s).text(key); t)
            return *t;
    return QString();
}

QString NamesView::_want(const traits::Trait &t, const QString &now) const
{
    // no_personality / no_background keep a name they have (the rules name the traits with stats)
    return traits::is_none(t.key) && !now.isEmpty() ? now : name_of(t, rules);
}

void NamesView::refresh(document::Document *d)
{
    doc = d;
    const QString text = doc && doc->maps.contains(u"scripts"_s)
                             ? doc->map<scriptstore::ScriptStore>(u"scripts"_s).text(u"common/traits.txt"_s)
                             : QString();
    lists = traits::parse(text);
    rules = for_mod(mod, lists, [this](const QString &k) { return _name(k); });
    _to_widgets();
    _preview();
}

void NamesView::add_rule_row(const Rule &r)
{
    auto *w = new QWidget();
    auto *hl = new QHBoxLayout(w);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(6);
    RuleRow row{w, new QComboBox(), new QComboBox(), new QLineEdit(), new QLineEdit()};
    row.stat->addItem(u"Any stat"_s, QString());
    for (const QString &st : traits::STATS)
        row.stat->addItem(word(st), st);
    row.stat->setCurrentIndex(std::max(0, row.stat->findData(r.stat)));
    row.lo->setPlaceholderText(u"lowest"_s);
    row.hi->setPlaceholderText(u"highest"_s);
    row.lo->setText(r.lo ? traits::number(*r.lo) : QString());
    row.hi->setText(r.hi ? traits::number(*r.hi) : QString());
    row.lo->setFixedWidth(70);
    row.hi->setFixedWidth(70);
    fill_colours(row.col, false);
    row.col->setCurrentIndex(std::max(0, row.col->findData(r.code)));
    auto *x = new QPushButton(u"✕"_s);
    x->setFixedWidth(34);
    x->setToolTip(u"Take this rule away"_s);
    hl->addWidget(row.stat, 2);
    hl->addWidget(label(u"from"_s, u"sub"_s, false));
    hl->addWidget(row.lo);
    hl->addWidget(label(u"to"_s, u"sub"_s, false));
    hl->addWidget(row.hi);
    hl->addWidget(row.col, 2);
    hl->addWidget(x);
    rule_lay->addWidget(w);
    _rows.push_back(row);
    connect(row.stat, &QComboBox::activated, this, [this](int) { _from_widgets(); });
    connect(row.col, &QComboBox::activated, this, [this](int) { _from_widgets(); });
    connect(row.lo, &QLineEdit::editingFinished, this, [this] { _from_widgets(); });
    connect(row.hi, &QLineEdit::editingFinished, this, [this] { _from_widgets(); });
    connect(x, &QPushButton::clicked, this, [this, w] {
        std::erase_if(_rows, [w](const RuleRow &r) { return r.w == w; });
        w->hide();
        w->deleteLater();
        _from_widgets();
    });
}

void NamesView::_to_widgets()
{
    _building = true;
    for (const auto &[st, c] : stat_ticks)
        c->setChecked(rules.stats.contains(st));
    none_name->setText(rules.none);
    none_colour->setCurrentIndex(std::max(0, none_colour->findData(rules.none_code)));
    for (const RuleRow &r : _rows) {
        r.w->hide();
        r.w->deleteLater();
    }
    _rows.clear();
    for (const Rule &r : rules.rules)
        add_rule_row(r);
    auto_box->setChecked(rules.auto_names);
    _building = false;
}

void NamesView::_from_widgets()
{
    if (_building)
        return;
    Rules r;
    r.stats.clear();
    for (const QString &st : traits::STATS)        // (in the stats' order)
        for (const auto &[k, c] : stat_ticks)
            if (k == st && c->isChecked())
                r.stats << st;
    r.none = none_name->text().trimmed();
    r.none_code = none_colour->currentData().toString();
    for (const RuleRow &row : _rows)
        r.rules.push_back(Rule{row.stat->currentData().toString(), num(row.lo->text()), num(row.hi->text()),
                               row.col->currentData().toString()});
    r.auto_names = auto_box->isChecked();
    rules = r;
    store(mod, rules);
    _preview();
}

void NamesView::read_from_mod()
{
    int n = 0;
    Rules r = learn(lists, [this](const QString &k) { return _name(k); }, &n);
    if (!n) {
        note->setText(u"<span style='color:%1'>The mod's trait names are not made from their stats, so there is "
                      u"nothing to read; the rules stay as they are.</span>"_s.arg(theme::DANGER));
        return;
    }
    r.auto_names = rules.auto_names;
    rules = r;
    store(mod, rules);
    _to_widgets();
    _preview();
    note->setText(u"Read from %1 trait names of the mod."_s.arg(n));
}

void NamesView::_preview()
{
    preview->clear();
    long long change = 0, missing = 0;
    for (const QString &list : traits::LISTS)
        for (const traits::Trait &t : lists.of(list)) {
            const QString now = _name(t.key), want = _want(t, now);
            auto *it = new QTreeWidgetItem(preview);
            it->setText(0, t.key);
            it->setToolTip(0, traits::summary(t));
            it->setText(1, list == u"background" ? u"background"_s : u"personality"_s);
            it->setText(2, now.isEmpty() ? QString() : now);
            it->setText(3, want);
            it->setToolTip(2, now);
            it->setToolTip(3, want);
            it->setData(0, KEY_ROLE, t.key);
            if (now != want) {
                ++change;
                it->setText(0, u"● "_s + t.key);
                it->setForeground(0, QColor(theme::GOLD));
            }
            missing += now.isEmpty();
        }
    write_all->setText(u"Write every name (%1 change)"_s.arg(change));
    write_all->setEnabled(change > 0);
    write_missing->setText(u"Only the traits without a name (%1)"_s.arg(missing));
    write_missing->setEnabled(missing > 0);
}

void NamesView::write(bool only_missing)
{
    if (!doc || !doc->maps.contains(u"loc"_s))
        return;
    auto &loc = doc->map<localisation::LocEdits>(u"loc"_s);
    std::vector<std::pair<QString, QString>> names;
    for (const QString &list : traits::LISTS)
        for (const traits::Trait &t : lists.of(list)) {
            const QString now = _name(t.key), want = _want(t, now);
            if (now != want && (!only_missing || now.isEmpty()))
                names.emplace_back(t.key, want);
        }
    if (names.empty())
        return;
    const QString lbl = u"Leader traits: %1 name%2 from the rules"_s.arg(names.size()).arg(names.size() == 1 ? QString() : u"s"_s);
    doc->edit(lbl, {}, [&] {
        for (const auto &[k, t] : names)
            loc.set(k, t);
    }, {}, {}, {u"loc"_s});
    Q_EMIT message(lbl + u" — written to the localisation on Apply (Ctrl+Z undoes it)."_s);
    note->setText(u"%1 written (on Apply)."_s.arg(names.size()));
    _preview();
}

}  // namespace traitnames
