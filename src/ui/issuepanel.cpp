// issuepanel.py — Reforms and party issues (see issuepanel.h).
#include "ui/issuepanel.h"
#include "core/blocks.h"
#include "core/countryhist.h"
#include "core/localisation.h"
#include "core/modifiers.h"
#include "core/pytext.h"
#include "ui/colsizes.h"
#include "ui/locwin.h"
#include "ui/modpanel.h"
#include "ui/reformwin.h"
#include "ui/shortcuts.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <set>

using namespace Qt::StringLiterals;
using widgets::BadgeButton;
using widgets::GridFlow;
using widgets::label;
using widgets::Section;

namespace issuepanel {

const QString REL = u"common/issues.txt"_s;
const OrderedMap<QString, QString> GROUP_NAMES = {
    {u"party_issues"_s, u"Party issues"_s},           {u"political_reforms"_s, u"Political reforms"_s},
    {u"social_reforms"_s, u"Social reforms"_s},       {u"economic_reforms"_s, u"Economic reforms (unciv)"_s},
    {u"military_reforms"_s, u"Military reforms (unciv)"_s}};

QString strip_colours(const QString &s)
{
    // COLOR_RE = "§.|§" ("." is any character but a line break)
    QString out;
    out.reserve(s.size());
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s[i] == u'§') {
            if (i + 1 < s.size() && s[i + 1] != u'\n')
                ++i;
            continue;
        }
        out += s[i];
    }
    return out;
}

namespace {

QStringList path_of(const Sel &s)                     // [x for x in sel if x]
{
    QStringList out;
    if (!std::get<0>(s).isEmpty())
        out << std::get<0>(s);
    if (!std::get<1>(s).isEmpty())
        out << std::get<1>(s);
    if (std::get<2>(s) && !std::get<2>(s)->isEmpty())
        out << *std::get<2>(s);
    return out;
}

QString under(const QString &k) { return QString(k).replace(u'_', u' '); }

QStringList item_data(const QString &grp, const OptStr &iss, const OptStr &opt)
{
    // the item's (group, issue, option): None kept as a missing entry
    QStringList d{grp};
    if (iss) {
        d << *iss;
        if (opt)
            d << *opt;
    }
    return d;
}

}  // namespace

IssueView::IssueView(moddata::Mod &mod_, const QStringList &groups_, const QString &intro, bool wide, QWidget *parent)
    : QWidget(parent), mod(mod_), groups(groups_)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    QVBoxLayout *lay;
    QSplitter *split = nullptr;
    if (wide) {                                       // a whole tab: the list left, the one picked right
        split = new QSplitter(Qt::Horizontal);
        split->setChildrenCollapsible(false);
        outer->addWidget(split, 1);
        auto *left = new QWidget();
        lay = new QVBoxLayout(left);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        split->addWidget(left);
    } else {
        lay = outer;
    }
    auto *s = new Section();
    s->lay->addWidget(label(intro, u"faint"_s));
    // "As the game's window": the game's own politics window with the reforms framed where it shows them
    // (reformwin.py) — the user, 2026-10-26: "a cluster of sections with their names … an overlay to better see what
    // you are editing"
    part = groups.contains(u"economic_reforms"_s) ? u"unciv_reforms_window"_s : u"reforms_window"_s;
    as_window = wide;
    if (wide) {
        auto *row = new QHBoxLayout();
        row->setSpacing(0);
        for (const auto &[k, t] : {std::pair{u"window"_s, u"As the game's window"_s}, std::pair{u"list"_s, u"As a list"_s}}) {
            auto *b = new QPushButton(t);
            b->setObjectName(u"seg"_s);
            b->setCheckable(true);
            b->setChecked(k == u"window");
            connect(b, &QPushButton::clicked, this, [this, kk = k] { _set_view(kk); });
            row->addWidget(b);
            view_btn[k] = b;
        }
        row->addStretch(1);
        s->lay->addLayout(row);
        win_view = new reformwin::GameReformView();
        connect(win_view, &reformwin::GameReformView::picked, this,
                [this](const QString &iss, const QString &opt) { _picked_in_window(iss, opt); });
        s->lay->addWidget(win_view, 1);
    }
    search = new QLineEdit();
    search->setPlaceholderText(u"Find a reform, an option or an effect…"_s);
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, [this] { _fill_list(); });
    s->lay->addWidget(search);
    lst = new QTreeWidget();
    lst->setColumnCount(2);
    lst->setHeaderLabels({u"Reform / option"_s, u"Effects"_s});
    lst->setRootIsDecorated(true);
    lst->setUniformRowHeights(true);
    lst->setSelectionMode(QAbstractItemView::SingleSelection);
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setMinimumHeight(320);
    colsizes::make_resizable(lst, u"issueList"_s, {std::nullopt, 70}, true);
    connect(lst, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *it, QTreeWidgetItem *prev) { _picked(it, prev); });
    s->lay->addWidget(lst, 1);
    list_sec = s;
    lay->addWidget(s, wide ? 1 : 0);
    if (wide) {
        auto *sc = new QScrollArea();
        sc->setWidgetResizable(true);
        sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sc->setObjectName(u"sideScroll"_s);
        auto *right = new QWidget();
        right->setObjectName(u"sideInner"_s);
        _split = split;
        lay = new QVBoxLayout(right);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        sc->setWidget(right);
        split->addWidget(sc);
        split->setStretchFactor(0, 5);
        split->setStretchFactor(1, 6);
        split->setSizes({700, 840});
    }
    s = detail = new Section();
    head = new QLabel();
    head->setWordWrap(true);
    s->lay->addWidget(head);
    title = label(u"Pick a reform or one of its options"_s, u"title"_s);
    s->lay->addWidget(title);
    sub = label(QString(), u"faint"_s);
    s->lay->addWidget(sub);
    auto *g = new QGridLayout();
    g->setColumnStretch(1, 1);
    g->addWidget(label(u"Name in the game"_s, u"sub"_s, false), 0, 0);
    name_edit = new QLineEdit();
    connect(name_edit, &QLineEdit::editingFinished, this, [this] { _loc_done(name_edit, _key()); });
    g->addWidget(name_edit, 0, 1);
    g->addWidget(label(u"Description"_s, u"sub"_s, false), 1, 0);
    desc_edit = new QLineEdit();
    connect(desc_edit, &QLineEdit::editingFinished, this, [this] {
        const OptStr k = _key();
        _loc_done(desc_edit, k && !k->isEmpty() ? OptStr(*k + u"_desc"_s) : k);
    });
    g->addWidget(desc_edit, 1, 1);
    names_host = new QWidget();
    names_host->setLayout(g);
    s->lay->addWidget(names_host);
    warn = label(QString(), u"sub"_s);
    warn->setStyleSheet(u"color: #ff786e;"_s);
    s->lay->addWidget(warn);
    rows_host = new QWidget();
    rows = new QVBoxLayout(rows_host);
    rows->setContentsMargins(0, 0, 0, 0);
    s->lay->addWidget(rows_host);
    lay->addWidget(s);
    lay->addStretch(1);
}

IssueView::~IssueView() = default;

// ------------------------------------------------------------------ data
QString IssueView::text() { return store->text(REL); }

void IssueView::refresh(document::Document *doc_)
{
    doc = doc_;
    store = doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    if (!store)
        return;
    _holders.reset();
    _fill_list();
    _fill_detail();
    _show_view();
}

// ------------------------------------------------------------------ as the game's window
void IssueView::_set_view(const QString &k)
{
    as_window = k == u"window";
    for (const auto &[kk, b] : view_btn)
        b->setChecked(kk == k);
    _show_view();
}

void IssueView::_show_view()
{
    if (!win_view)
        return;
    win_view->setVisible(as_window);
    lst->setVisible(!as_window);
    search->setVisible(!as_window);
    if (_split && _shown_as != as_window) {
        _shown_as = as_window;                        // (the game's window wants room: the left side wider)
        int tot = 0;
        for (int v : _split->sizes())
            tot += v;
        if (!tot)
            tot = 1540;
        const double share = as_window ? 0.62 : 0.45;
        _split->setSizes({int(tot * share), tot - int(tot * share)});
    }
    if (as_window && store)
        _fill_window();
}

void IssueView::_fill_window()
{
    if (!_look || _look->store != store)
        _look = std::make_unique<locwin::GameLook>(mod, store);
    std::optional<QImage> img;
    std::optional<shortcuts::Meta> meta;
    try {
        shortcuts::Projected got =
            shortcuts::project(*_look, reformwin::REL, u"country_politics"_s, {part}, true, false, 0, part);
        img = got.img;
        meta = got.meta;
    } catch (...) {
        img.reset();
        meta.reset();
    }
    const OrderedMap<QString, reformwin::XY> places = reformwin::slots(store->text(reformwin::REL), part);
    QStringList wanted;
    for (const QString &g : groups)
        if (g != u"party_issues")
            wanted << g;
    const Tree tr = tree();
    OrderedMap<QString, QString> issues;
    for (const auto &[grp, ls] : tr)
        if (wanted.contains(grp))
            for (const auto &[iss, _opts] : ls)
                issues.insert(iss, grp);
    OrderedMap<QString, QString> names, current;
    OrderedMap<QString, std::vector<reformwin::Option>> options;
    const auto &h = holders();
    auto count = [&h](const QString &iss, const QString &o) -> qsizetype {
        const auto it = h.find({iss, o});
        return it != h.end() ? it->second.size() : 0;
    };
    for (const auto &[grp, ls] : tr)
        for (const auto &[iss, opts] : ls)
            if (issues.contains(iss)) {
                names.insert(iss, nm(iss));
                OptStr best;                          // max(opts, key=…): the first of the most
                qsizetype most = -1;
                for (const QString &o : opts)
                    if (count(iss, o) > most) {
                        most = count(iss, o);
                        best = o;
                    }
                if (best && !best->isEmpty() && count(iss, *best))
                    current.insert(iss, u"%1  (%2 nations at the start)"_s.arg(nm(*best)).arg(count(iss, *best)));
                // the options as the game lists them; the tick at the one most nations start with
                std::vector<reformwin::Option> ol;
                for (const QString &o : opts)
                    ol.emplace_back(nm(o), best && o == *best && count(iss, o) > 0, o);
                options.insert(iss, ol);
            }
    const reformwin::XY off = meta ? reformwin::XY{meta->x0, meta->y0} : reformwin::XY{0.0, 0.0};
    std::optional<std::pair<QString, QString>> s;
    if (sel)
        s = std::pair{std::get<1>(*sel), std::get<2>(*sel).value_or(QString())};
    // only the reforms' part of the game's window (C++ only, the user 2026-11-15: no tabs, no left side)
    locwin::GameLook *look = _look.get();
    const QRectF shown = reformwin::extent(store->text(reformwin::REL), part,
                                           [look](const QString &sp) -> std::optional<QSizeF> {
                                               const std::optional<QImage> im = look->sprite(sp);
                                               if (!im || im->isNull())
                                                   return std::nullopt;
                                               return QSizeF(im->size());
                                           });
    // where the options and names stand: the mod's own window, not vanilla's (C++ only, 2026-11-17)
    win_view->lay = reformwin::layout(store->text(reformwin::REL));
    win_view->place_info = reformwin::places_of(store->text(reformwin::REL), part);
    win_view->set_data(img, off, places, issues, names, current, s, options, shown, look);
}

void IssueView::_picked_in_window(const QString &iss, const QString &opt)
{
    // a click in the game's window: on a reform's name = the reform, on one of its options = that option — either
    // way shown on the right to change
    QString grp;
    for (const auto &[g, ls] : tree()) {
        for (const auto &[i, _o] : ls)
            if (i == iss) {
                grp = g;
                break;
            }
        if (!grp.isEmpty())
            break;
    }
    if (!grp.isEmpty()) {
        sel = Sel{grp, iss, opt.isEmpty() ? std::nullopt : OptStr(opt)};
        _fill_list();
        _fill_detail();
    }
}

Tree IssueView::tree()
{
    Tree out;
    for (const script::Node &top : script::parse(text()))
        if (top.has_key && groups.contains(top.key) && top.is_block) {
            std::vector<std::pair<QString, QStringList>> issues;
            for (const script::Node &iss : top.kids)
                if (iss.has_key && !iss.key.isEmpty() && iss.is_block) {
                    QStringList opts;
                    for (const script::Node &o : iss.kids)
                        if (o.has_key && !o.key.isEmpty() && o.is_block)
                            opts << o.key;
                    issues.emplace_back(iss.key, opts);
                }
            out.emplace_back(top.key, issues);
        }
    return out;
}

OptStr IssueView::loc(const QString &key)
{
    localisation::LocEdits *lc =
        doc && doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    OptStr got;
    if (lc)
        got = lc->text(key);
    else if (const QString *t = mod.loc.get(key))
        got = *t;
    return got && !got->isEmpty() ? got : std::nullopt;
}

QString IssueView::nm(const QString &key)
{
    const QString t = py::strip(strip_colours(loc(key).value_or(QString())));
    return !t.isEmpty() ? t : under(key);
}

OptStr IssueView::_key() const
{
    if (!sel)
        return std::nullopt;
    const OptStr &o = std::get<2>(*sel);
    return o && !o->isEmpty() ? o : OptStr(std::get<1>(*sel));
}

const std::map<std::pair<QString, QString>, QStringList> &IssueView::holders()
{
    // {(issue, option): [tags]} — the nations that have each at the start
    if (!_holders) {
        std::map<std::pair<QString, QString>, QStringList> h;
        if (doc && doc->maps.contains(u"nations"_s)) {
            auto &nat = doc->map<countryhist::NationEdits>(u"nations"_s);
            std::set<QString> owners;
            for (const auto &[pid, pr] : mod.provinces)
                if (!pr.sea) {
                    const OptStr &o = doc->prov_state(pid).owner;
                    if (o && !o->isEmpty())
                        owners.insert(*o);
                }
            for (const QString &t : owners) {
                countryhist::NationState st;
                try {
                    st = nat.state(t);
                } catch (...) {
                    continue;
                }
                for (const auto &[iss, opt] : st.reforms)
                    h[{iss, opt}].append(t);
            }
        }
        _holders = std::move(h);
    }
    return *_holders;
}

void IssueView::_fill_list()
{
    if (!store)
        return;
    const QString q = py::lower(py::strip(search->text()));
    const QString txt = text();
    lst->blockSignals(true);
    lst->clear();
    QFont bold(lst->font());
    bold.setBold(true);
    QTreeWidgetItem *cur = nullptr;
    for (const auto &[grp, issues] : tree()) {
        const QString *gn = GROUP_NAMES.get(grp);
        auto *gi = new QTreeWidgetItem(QStringList{gn ? *gn : py::capitalize(under(grp)), QString()});
        gi->setFont(0, bold);
        gi->setForeground(0, QBrush(QColor(theme::GOLD)));
        gi->setData(0, Qt::UserRole, item_data(grp, std::nullopt, std::nullopt));
        lst->addTopLevelItem(gi);
        for (const auto &[iss, opts] : issues) {
            auto *ii = new QTreeWidgetItem(QStringList{
                nm(iss), u"%1 option%2"_s.arg(opts.size()).arg(opts.size() == 1 ? QString() : u"s"_s)});
            ii->setData(0, Qt::UserRole, item_data(grp, iss, std::nullopt));
            ii->setToolTip(0, iss);
            bool shown = false;
            for (const QString &o : opts) {
                QStringList eff;
                for (const script::Node &c : blocks::children(txt, {grp, iss, o}))
                    if (!c.is_block && modifiers::num_ok(py::strip(c.value)))
                        eff << (c.has_key ? c.key : QString());
                if (!q.isEmpty() &&
                    !py::lower(u"%1 %2 %3 %4 %5"_s.arg(iss, nm(iss), o, nm(o), eff.join(u' '))).contains(q))
                    continue;
                auto *oi = new QTreeWidgetItem(QStringList{nm(o), QString::number(eff.size())});
                oi->setData(0, Qt::UserRole, item_data(grp, iss, o));
                oi->setToolTip(0, u"%1\n%2"_s.arg(o, eff.join(u'\n')));
                oi->setTextAlignment(1, Qt::AlignCenter);
                ii->addChild(oi);
                shown = true;
                if (sel && *sel == Sel{grp, iss, o})
                    cur = oi;
            }
            if (!q.isEmpty() && !shown && !py::lower(u"%1 %2"_s.arg(iss, nm(iss))).contains(q)) {
                delete ii;
                continue;
            }
            gi->addChild(ii);
            if (sel && *sel == Sel{grp, iss, std::nullopt})
                cur = ii;
            ii->setExpanded(!q.isEmpty() || (sel && std::get<1>(*sel) == iss));
        }
        gi->setExpanded(true);
    }
    if (cur) {
        lst->setCurrentItem(cur);
        lst->scrollToItem(cur);
    }
    lst->blockSignals(false);
}

void IssueView::_picked(QTreeWidgetItem *it, QTreeWidgetItem *)
{
    const QStringList d = it ? it->data(0, Qt::UserRole).toStringList() : QStringList();
    if (d.size() >= 2 && !d[1].isEmpty()) {
        sel = Sel{d[0], d[1], d.size() > 2 ? OptStr(d[2]) : std::nullopt};
        _fill_detail();
        _sync_window();
    }
}

// ------------------------------------------------------------------ the one shown
void IssueView::_clear()
{
    while (rows->count()) {
        QLayoutItem *item = rows->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
}

void IssueView::_fill_detail()
{
    const QString txt = store ? text() : QString();
    const bool ok = sel && blocks::find(script::parse(txt), path_of(*sel)) != nullptr;
    names_host->setVisible(ok);
    rows_host->setVisible(ok);
    if (!ok) {
        head->setText(QString());
        title->setText(u"Pick a reform or one of its options"_s);
        sub->setText(QString());
        _sig.reset();
        return;
    }
    const auto [grp, iss, opt_] = *sel;
    const QString opt = opt_.value_or(QString());
    const QStringList path = path_of(*sel);
    head->setText(u"%1 %2"_s.arg(!opt.isEmpty() ? u"Option of"_s : u"Reform:"_s, GROUP_NAMES.value(grp, grp)));
    head->setStyleSheet(u"QLabel { background: %1; color: #14100a; font-weight: 700; padding: 5px 10px; "
                        u"border-radius: 4px; }"_s.arg(theme::GOLD));
    title->setText(nm(!opt.isEmpty() ? opt : iss));
    sub->setText(u"%1 › %2%3 · %4"_s.arg(grp, iss, !opt.isEmpty() ? u" › "_s + opt : QString(), REL));
    const QString key = !opt.isEmpty() ? opt : iss;
    for (const auto &[ed, k] : {std::pair{name_edit, key}, std::pair{desc_edit, key + u"_desc"_s}})
        if (!ed->hasFocus()) {
            ed->setText(loc(k).value_or(QString()));
            ed->setPlaceholderText(u"No localisation present — type one to add it"_s);
        }
    const std::vector<script::Node> kids = blocks::children(txt, path);
    Sig sig{path, {}};
    for (const script::Node &c : kids)
        sig.second.emplace_back(c.has_key ? c.key : QString(), c.is_block);
    if (_sig && *_sig == sig) {
        for (const auto &[k, ed] : _edits) {
            OptStr v;
            for (const script::Node &c : kids)
                if ((c.has_key ? c.key : QString()) == k && !c.is_block) {
                    v = c.value;
                    break;
                }
            if (v && !ed->hasFocus()) {
                ed->setText(py::strip(*v));
                ed->setStyleSheet(QString());
            }
        }
        return;
    }
    _sig = sig;
    _edits.clear();
    _clear();
    if (opt.isEmpty())
        _issue_rows(grp, iss, kids);
    else
        _option_rows(grp, iss, opt, kids);
}

void IssueView::_issue_rows(const QString &grp, const QString &iss, const std::vector<script::Node> &kids)
{
    auto *w = new QWidget();
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 6, 0, 0);
    OrderedMap<QString, QString> vals;
    for (const script::Node &c : kids)
        if (!c.is_block)
            vals.insert(c.has_key ? c.key : QString(), c.value);
    for (const auto &[f, tip] :
         {std::pair{u"next_step_only"_s, u"Only the next option can be picked (one step at a time)"_s},
          std::pair{u"administrative"_s, u"An administrative reform (counts for the administrative efficiency)"_s}}) {
        auto *cb = new QCheckBox(u"%1 — %2"_s.arg(under(f), tip));
        cb->setChecked(py::strip(vals.value(f, u"no"_s)) == u"yes");
        connect(cb, &QCheckBox::toggled, this, [this, grp, iss, ff = f](bool on) {
            _emit_change(u"%1: %2 %3"_s.arg(nm(iss), ff, on ? u"yes"_s : u"no"_s),
                         on ? std::function<QString(const QString &)>([grp, iss, ff](const QString &t) {
                             return blocks::set_value(t, {grp, iss}, ff, u"yes"_s);
                         })
                            : std::function<QString(const QString &)>([grp, iss, ff](const QString &t) {
                                  return blocks::del_value(t, {grp, iss}, ff);
                              }));
        });
        v->addWidget(cb);
    }
    QStringList opts;
    for (const script::Node &c : kids)
        if (c.is_block)
            opts << (c.has_key ? c.key : QString());
    v->addWidget(label(u"Its options, in order — click one for its effects:"_s, u"sub"_s));
    auto *fl = new GridFlow(nullptr, 5, 150, 30);
    for (const QString &o : opts) {
        auto *b = new QPushButton(nm(o));
        b->setToolTip(o);
        connect(b, &QPushButton::clicked, this, [this, grp, iss, o] { _go_option(grp, iss, o); });
        fl->addWidget(b);
    }
    auto *hold = new QWidget();
    hold->setLayout(fl);
    v->addWidget(hold);
    rows->addWidget(w);
}

void IssueView::_go_option(const QString &grp, const QString &iss, const QString &opt)
{
    sel = Sel{grp, iss, opt};
    _fill_list();
    _fill_detail();
    _sync_window();
}

void IssueView::_sync_window()
{
    // the game's window marks what is picked (also when it was picked in the list)
    if (win_view && sel) {
        win_view->sel = std::pair{std::get<1>(*sel), std::get<2>(*sel).value_or(QString())};
        win_view->update();
    }
}

void IssueView::_option_rows(const QString &grp, const QString &iss, const QString &opt,
                             const std::vector<script::Node> &kids)
{
    const QStringList path{grp, iss, opt};
    // --- the numbers it changes
    auto *w = new QWidget();
    auto *g = new QGridLayout(w);
    g->setContentsMargins(0, 6, 0, 2);
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(4);
    g->setColumnStretch(0, 1);
    QLabel *h = label(u"Effects (the numbers it changes)"_s, u"sub"_s, false);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    g->addWidget(h, 0, 0, 1, 3);
    int r = 1;
    std::vector<std::pair<QString, QString>> other;
    for (const script::Node &c : kids) {
        if (c.is_block)
            continue;
        const QString k = c.has_key ? c.key : QString();
        if (!modifiers::num_ok(py::strip(c.value))) {
            other.emplace_back(k, c.value);
            continue;
        }
        QLabel *lb = label(modpanel::attr_name(mod, k), u"sub"_s, false);
        lb->setToolTip(k);
        g->addWidget(lb, r, 0);
        auto *ed = new QLineEdit(py::strip(c.value));
        ed->setFixedWidth(90);
        ed->setAlignment(Qt::AlignRight);
        connect(ed, &QLineEdit::editingFinished, this, [this, path, k, ed] { _set_num(path, k, ed); });
        g->addWidget(ed, r, 1);
        _edits.insert(k, ed);
        auto *x = new QPushButton(u"✕"_s);
        x->setFixedWidth(30);
        x->setToolTip(u"Take “%1” off"_s.arg(k));
        connect(x, &QPushButton::clicked, this, [this, path, k, opt] {
            _emit_change(u"%1: %2 taken off"_s.arg(nm(opt), k),
                         [path, k](const QString &t) { return blocks::del_value(t, path, k); });
        });
        g->addWidget(x, r, 2);
        r += 1;
    }
    if (r == 1) {
        g->addWidget(label(u"none"_s, u"faint"_s), r, 0);
        r += 1;
    }
    auto *box = new QComboBox();
    box->setEditable(true);
    box->setInsertPolicy(QComboBox::NoInsert);
    box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    QStringList known;
    if (doc && doc->maps.contains(u"event_mods"_s))
        known = doc->map<modifiers::ModifierEdits>(u"event_mods"_s).db().known_attrs;
    for (const QString &extra :
         {u"rich_vote"_s, u"middle_vote"_s, u"poor_vote"_s, u"technology_cost"_s, u"global_immigrant_attract"_s})
        if (!known.contains(extra))
            known << extra;
    std::sort(known.begin(), known.end());
    box->addItems(known);
    box->setCurrentIndex(-1);
    box->lineEdit()->setPlaceholderText(u"Add an effect — pick or type its key"_s);
    g->addWidget(box, r, 0);
    auto *val = new QLineEdit();
    val->setPlaceholderText(u"value"_s);
    val->setFixedWidth(90);
    g->addWidget(val, r, 1);
    auto *add = new QPushButton(u"＋"_s);
    add->setFixedWidth(30);
    connect(add, &QPushButton::clicked, this, [this, path, box, val] { _add_num(path, box, val); });
    connect(val, &QLineEdit::returnPressed, this, [this, path, box, val] { _add_num(path, box, val); });
    g->addWidget(add, r, 2);
    rows->addWidget(w);
    if (!other.empty()) {
        QStringList kv;
        for (const auto &[k, v] : other)
            kv << u"%1 = %2"_s.arg(k, v);
        rows->addWidget(label(u"Also: "_s + kv.join(u" · "_s), u"faint"_s));
    }
    // --- the unciv reforms: what it buys
    const QString txt = text();
    const OptStr ex = blocks::block_text(txt, path + QStringList{u"on_execute"_s});
    if (ex && !ex->isEmpty()) {
        static const QRegularExpression re(uR"(activate_(?:technology|invention)\s*=\s*([A-Za-z0-9_]+))"_s,
                                           QRegularExpression::UseUnicodePropertiesOption);
        QStringList techs;
        for (auto it = re.globalMatch(*ex); it.hasNext();)
            techs << nm(it.next().captured(1));
        if (!techs.isEmpty())
            rows->addWidget(label(u"Gives: "_s + techs.join(u", "_s), u"sub"_s));
    }
    // --- who has it at the start
    const auto &hs = holders();
    const auto hit = hs.find({iss, opt});
    const QStringList hold = hit != hs.end() ? hit->second : QStringList();
    if (!hold.isEmpty() || grp == u"political_reforms" || grp == u"social_reforms" || grp == u"party_issues") {
        QStringList names;
        for (const QString &t : hold) {
            const moddata::Country *c = mod.countries.get(t);
            names << (c ? c->name : t);
        }
        const QString who = !hold.isEmpty() ? names.mid(0, 30).join(u", "_s) + (names.size() > 30 ? u" …"_s : QString())
                                            : u"none"_s;
        rows->addWidget(label(u"Nations with it at the start (%1): %2"_s.arg(hold.size()).arg(who), u"sub"_s));
    }
    // --- its other blocks, as text
    for (const script::Node &c : kids)
        if (c.is_block)
            rows->addWidget(_block_box(path, c.has_key ? c.key : QString()));
}

QWidget *IssueView::_block_box(const QStringList &path, const QString &key)
{
    auto *w = new QWidget();
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 8, 0, 0);
    v->setSpacing(3);
    auto *row = new QHBoxLayout();
    QLabel *h = label(py::capitalize(under(key)), u"sub"_s, false);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    row->addWidget(h, 1);
    auto *save = new QPushButton(u"Save “%1”"_s.arg(key));
    row->addWidget(save);
    v->addLayout(row);
    auto *ed = new QPlainTextEdit();
    ed->setFont(QFont(theme::MONO, 10));
    ed->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    ed->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const QString inner = blocks::block_text(text(), path + QStringList{key}).value_or(QString());
    ed->setPlainText(reindent(py::strip(inner, u"\n"_s), 0, u"    "_s));
    const long long lines = std::max(3LL, std::min(14LL, static_cast<long long>(inner.count(u'\n')) + 2));
    ed->setFixedHeight(int(ed->fontMetrics().lineSpacing() * lines + 16));
    v->addWidget(ed);
    connect(save, &QPushButton::clicked, this, [this, ed, path, key] {
        const QString body = ed->toPlainText();
        const qsizetype open = body.count(u'{'), close = body.count(u'}');
        if (open != close) {
            warn->setText(u"“%1”: the braces don't match ({ %2, } %3) — not saved."_s.arg(key).arg(open).arg(close));
            return;
        }
        warn->setText(QString());
        const QString neu = u"\n"_s + reindent(py::strip(body, u"\n"_s), path.size() + 1) + u"\n"_s +
                            QString(u"\t"_s).repeated(path.size());
        _emit_change(u"%1: %2"_s.arg(nm(path.back()), key), [path, key, neu](const QString &t) {
            return blocks::set_block(t, path + QStringList{key}, neu);
        });
    });
    return w;
}

// ------------------------------------------------------------------ changes
void IssueView::_emit_change(const QString &what, const std::function<QString(const QString &)> &fn)
{
    const QString old = text();
    const QString neu = fn(old);
    if (neu != old)
        Q_EMIT scriptEdit(what, REL, neu);
}

void IssueView::_set_num(const QStringList &path, const QString &k, QLineEdit *ed)
{
    const QString v = py::strip(ed->text());
    if (!modifiers::num_ok(v)) {
        ed->setStyleSheet(u"QLineEdit { border: 2px solid #ff786e; }"_s);
        warn->setText(u"“%1” needs a number."_s.arg(k));
        return;
    }
    ed->setStyleSheet(QString());
    warn->setText(QString());
    _emit_change(u"%1: %2 = %3"_s.arg(nm(path.back()), k, v),
                 [path, k, v](const QString &t) { return blocks::set_value(t, path, k, v); });
}

void IssueView::_add_num(const QStringList &path, QComboBox *box, QLineEdit *val)
{
    const QString k = py::strip(box->currentText());
    const QString v = py::strip(val->text());
    if (!modifiers::name_ok(k)) {
        warn->setText(u"Pick an effect (its key: letters, digits, _)."_s);
        return;
    }
    if (!modifiers::num_ok(v)) {
        warn->setText(u"Give it a number."_s);
        return;
    }
    warn->setText(QString());
    val->clear();
    const QString *o = store->orig.get(REL);
    const OptStr like = o ? OptStr(*o) : std::nullopt;
    _emit_change(u"%1: %2 = %3"_s.arg(nm(path.back()), k, v),
                 [path, k, v, like](const QString &t) { return blocks::set_value(t, path, k, v, like); });
}

void IssueView::_loc_done(QLineEdit *ed, const OptStr &key)
{
    if (!key || key->isEmpty())
        return;
    const QString t = py::split(ed->text()).join(u' ');
    if (!t.isEmpty() && t != loc(*key).value_or(QString()))
        Q_EMIT nameEdit(*key, t);
}

}  // namespace issuepanel
