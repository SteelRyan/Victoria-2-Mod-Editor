// nationextras.py — the parties / colour / graphical culture box and the flags box of the Nation page.
#include "ui/nationextras.h"
#include "core/blocks.h"
#include "core/countryhist.h"
#include "core/fs.h"
#include "core/localisation.h"
#include "core/pyre.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "core/settings.h"
#include "ui/colorwheel.h"
#include "ui/colsizes.h"
#include "ui/textselect.h"
#include "ui/theme.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <set>

using namespace Qt::StringLiterals;
using widgets::label;

namespace nationextras {

const std::vector<std::pair<QString, QString>> VARIANTS = {{u""_s, u"Normal"_s},
                                                            {u"_monarchy"_s, u"Monarchy"_s},
                                                            {u"_republic"_s, u"Republic"_s},
                                                            {u"_communist"_s, u"Communist"_s},
                                                            {u"_fascist"_s, u"Fascist"_s}};

namespace {

const QString DATE_RE = u"^\\d{1,4}\\.\\d{1,2}\\.\\d{1,2}$"_s;
const QString KEY_RE = u"^[A-Za-z_][A-Za-z0-9_]*$"_s;
const QString GC_RE = u"(?m)^([ \\t]*graphical_culture[ \\t]*=[ \\t]*)([A-Za-z_][A-Za-z0-9_]*)"_s;

scriptstore::ScriptStore *scripts_of(document::Document *doc)
{
    return doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
}

QString strip(const QString &s) { return py::lstrip(py::rstrip(s)); }   // str.strip()
QString words(const QString &s) { return py::split(s).join(u' '); }     // " ".join(s.split())
bool matches(const QString &pattern, const QString &s) { return py::re_match(pattern, s).hasMatch(); }

bool is(const script::Node &c, const QString &key) { return c.has_key && c.key == key; }

}  // namespace

QString _unq(const script::Node &c)               // _unq(c.value): a block's value is no text
{
    return c.is_block ? QString() : py::strip(strip(c.value), u"\""_s);
}

QHash<QString, QString> fields(const script::Node &n)   // {c.key: _unq(c.value) for c in n.value if c.key and not block}
{
    QHash<QString, QString> f;
    for (const script::Node &c : n.kids)
        if (c.has_key && !c.key.isEmpty() && !c.is_block)
            f.insert(c.key, _unq(c));
    return f;
}

namespace {

// _set_in: the key's value in this block replaced, or the line added before its closing brace
QString _set_in(const QString &text, const script::Node &node, const QString &key, const QString &value)
{
    for (const script::Node &c : node.kids)
        if (is(c, key) && !c.is_block)
            return text.left(c.vstart) + value + text.mid(c.vend);
    return blocks::_insert_before_close(text, node, key + u" = "_s + value);
}

}  // namespace

std::map<QString, QString> government_flags(document::Document *doc)
{
    scriptstore::ScriptStore *store = scripts_of(doc);
    std::map<QString, QString> out;
    if (!store)
        return out;
    try {
        for (const script::Node &g : script::parse(store->text(u"common/governments.txt"_s))) {
            if (!(g.has_key && !g.key.isEmpty() && g.is_block))
                continue;
            for (const script::Node &c : g.kids)
                if (c.has_key && !c.key.isEmpty() && py::lower(c.key) == u"flagtype" && !c.is_block) {
                    const QString ft = py::strip(strip(c.value), u"\""_s);
                    if (!ft.isEmpty())
                        out[g.key] = ft;
                    break;
                }
        }
    } catch (const std::exception &) {
    }
    return out;
}

OptStr start_flag(const moddata::Mod &mod, document::Document *doc, const QString &tag_,
                  const std::map<QString, QString> *gov_flags)
{
    const QString tag = py::upper(tag_);
    OptStr gov;
    if (doc && doc->maps.contains(u"nations"_s)) {
        try {
            gov = doc->map<countryhist::NationEdits>(u"nations"_s).state(tag).at(u"government"_s);
        } catch (const std::exception &) {
            gov.reset();
        }
    }
    OptStr ft;
    if (gov && !gov->isEmpty()) {
        const std::map<QString, QString> own = gov_flags ? std::map<QString, QString>{} : government_flags(doc);
        const std::map<QString, QString> &gf = gov_flags ? *gov_flags : own;
        if (auto it = gf.find(strip(*gov)); it != gf.end())
            ft = it->second;
    }
    QStringList names;
    if (ft && !ft->isEmpty())
        names << u"%1_%2.tga"_s.arg(tag, *ft);
    names << tag + u".tga"_s;
    for (const QString &name : names) {
        const QString p = mod.p({u"gfx"_s, u"flags"_s, name});
        if (QFileInfo(p).isFile())
            return p;
    }
    return std::nullopt;
}

std::vector<std::pair<QString, QString>> flag_variants(document::Document *doc)
{
    QStringList types;
    if (scriptstore::ScriptStore *store = scripts_of(doc)) {
        try {
            auto it = py::rx(u"\\bflagType\\s*=\\s*\"?([A-Za-z0-9_]+)"_s).globalMatch(
                store->text(u"common/governments.txt"_s));
            while (it.hasNext())
                types << it.next().captured(1);
        } catch (const fs::OSError &) {
            types.clear();
        }
    }
    std::vector<std::pair<QString, QString>> out = VARIANTS;
    std::set<QString> uniq(types.begin(), types.end());
    for (const QString &t : uniq) {
        const QString sfx = u"_"_s + t;
        if (std::none_of(out.begin(), out.end(), [&](const auto &v) { return v.first == sfx; }))
            out.emplace_back(sfx, py::capitalize(QString(t).replace(u'_', u' ')));
    }
    return out;
}

std::vector<const script::Node *> party_nodes(const script::Nodes &nodes)
{
    std::vector<const script::Node *> out;
    for (const script::Node &n : nodes)
        if (is(n, u"party"_s) && n.is_block)
            out.push_back(&n);
    return out;
}

QStringList ideologies(scriptstore::ScriptStore *store)
{
    script::Nodes nodes;
    try {
        nodes = script::parse(store->text(u"common/ideologies.txt"_s));
    } catch (const std::exception &) {
        return {};
    }
    QStringList out;
    for (const script::Node &g : nodes)
        if (g.is_block)
            for (const script::Node &c : g.kids)
                if (c.has_key && !c.key.isEmpty() && c.is_block)
                    out << c.key;
    return out;
}

// ---------------------------------------------------------------------------------------------- flags
_Thumb::_Thumb(QWidget *parent) : QLabel(parent)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
}

void _Thumb::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        Q_EMIT clicked();
}

FlagsBox::FlagsBox(moddata::Mod &mod_, QWidget *parent) : widgets::Section(parent), mod(mod_)
{
    lay->addWidget(label(u"Flags"_s));
    thumb_host = new QWidget();
    thumb_grid = new QGridLayout(thumb_host);
    thumb_grid->setContentsMargins(0, 0, 0, 0);
    thumb_grid->setHorizontalSpacing(6);
    thumb_grid->setVerticalSpacing(4);
    lay->addWidget(thumb_host, 0, Qt::AlignLeft);
    note = label(QString(), u"faint"_s);
    lay->addWidget(note);
    open_btn = new QPushButton(u"⤢ Open the flag studio"_s);
    open_btn->setToolTip(u"Make, change or delete this nation's flags in a window of their own (a click on a flag "
                         u"above opens it with that flag)"_s);
    connect(open_btn, &QPushButton::clicked, this, [this] { Q_EMIT studioRequested(std::nullopt); });
    lay->addWidget(open_btn);
}

void FlagsBox::_build_variants()
{
    while (thumb_grid->count()) {
        QLayoutItem *it = thumb_grid->takeAt(0);
        if (QWidget *w = it->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete it;
    }
    thumbs.clear();
    for (size_t i = 0; i < variants.size(); ++i) {
        const auto &[sfx, name] = variants[i];
        auto *cell = new QWidget(thumb_host);
        auto *col = new QVBoxLayout(cell);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(2);
        auto *t = new _Thumb(cell);
        t->setFixedSize(78, 54);
        t->setAlignment(Qt::AlignCenter);
        t->setCursor(Qt::PointingHandCursor);
        t->setStyleSheet(u"border: 1px solid %1; background: %2;"_s.arg(theme::LINE, theme::BG));
        connect(t, &_Thumb::clicked, this, [this, s = sfx] { Q_EMIT studioRequested(s); });
        col->addWidget(t);
        QLabel *cap = label(name, u"faint"_s, false);
        col->addWidget(cap, 0, Qt::AlignHCenter);
        thumb_grid->addWidget(cell, int(i) / 5, int(i) % 5);
        thumbs.emplace_back(sfx, t);
    }
}

void FlagsBox::refresh(document::Document *doc_, const OptStr &tag_)
{
    doc = doc_;
    tag = tag_;
    auto v = flag_variants(doc);
    if (v != variants) {
        variants = v;
        _build_variants();
    }
    FlagEdits *fe = doc && doc->maps.contains(u"flags"_s) ? &doc->map<FlagEdits>(u"flags"_s) : nullptr;
    QStringList missing;
    for (size_t i = 0; i < variants.size(); ++i) {
        const auto &[sfx, name] = variants[i];
        _Thumb *t = thumbs[i].second;
        const OptStr fn = tag ? OptStr(*tag + sfx + u".tga"_s) : std::nullopt;
        const std::optional<QImage> img = fe && fn ? fe->image(*fn) : std::nullopt;
        if (!img) {
            t->setPixmap(QPixmap());
            t->setText(u"—"_s);
            if (tag)
                missing << py::lower(name);
        } else {
            t->setText(QString());
            t->setPixmap(QPixmap::fromImage(*img).scaled(76, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        t->setToolTip(fn ? u"gfx/flags/%1 — click to edit it in the flag studio"_s.arg(*fn) : QString());
    }
    note->setText(!missing.isEmpty() && tag
                      ? u"No flag file for: %1. The game shows the one a government form asks for (Politics → "
                        u"Government forms → Flag); with none set it is always the normal one."_s.arg(missing.join(u", "_s))
                      : QString());
    note->setVisible(!missing.isEmpty() && tag);
    open_btn->setEnabled(bool(tag));
}

// ---------------------------------------------------------------------------------------------- parties, colour
PartiesBox::PartiesBox(moddata::Mod &mod_, QWidget *parent, bool full_, bool colour)
    : widgets::Section(parent), mod(mod_), full(full_)
{
    lay->addWidget(label(colour ? u"Parties and map colour"_s : u"The party picked"_s));
    file_note = label(QString(), u"faint"_s);
    lay->addWidget(file_note);
    auto *crow = new QHBoxLayout();
    crow->setSpacing(6);
    color_label = label(u"Colour on the map"_s, u"sub"_s, false);
    crow->addWidget(color_label);
    color_btn = new colorwheel::ColorButton(u"#808080"_s);
    connect(color_btn, &colorwheel::ColorButton::changed, this, &PartiesBox::_color);
    connect(color_btn, &colorwheel::ColorButton::preview, this, &PartiesBox::_color_preview);
    crow->addWidget(color_btn);
    color_note = label(QString(), u"faint"_s, false);
    crow->addWidget(color_note, 1);
    lay->addLayout(crow);
    auto *grow = new QHBoxLayout();
    grow->setSpacing(6);
    gc_label = label(u"Look of its units and towns"_s, u"sub"_s, false);
    gc_label->setToolTip(u"The nation's graphical culture (graphical_culture in its file in common/countries): which "
                         u"unit models and town pictures the game draws for it.\nThe list is the game's "
                         u"(common/graphicalculturetype.txt)."_s);
    grow->addWidget(gc_label);
    gc_box = new QComboBox();
    gc_box->setToolTip(gc_label->toolTip());
    connect(gc_box, &QComboBox::activated, this, [this](int) { _gc(); });
    grow->addWidget(gc_box, 1);
    lay->addLayout(grow);
    live = new QCheckBox(u"Show the colour on the map at once (the map is drawn again)"_s);
    live->setToolTip(u"On: the map follows the colour wheel while you look for a colour, and shows the chosen colour "
                     u"right away. Off: the map keeps the colour it has until the next Apply."_s);
    live->setChecked(settings_::settings()->value(u"liveNationColor"_s, true).toBool());
    connect(live, &QCheckBox::toggled, this, [this](bool on) {
        settings_::settings()->setValue(u"liveNationColor"_s, on);
        liveToggled();
    });
    lay->addWidget(live);
    for (QWidget *w : std::initializer_list<QWidget *>{color_label, color_btn, color_note, live, gc_label, gc_box})
        w->setVisible(colour);
    summary = label(QString(), u"sub"_s);
    lay->addWidget(summary);
    win_btn = new QPushButton(u"⤢ Open the parties window"_s);
    win_btn->setToolTip(u"Every party on a timeline, in its ideology's colour — with its policies, filters, and "
                        u"everything about a party to edit"_s);
    connect(win_btn, &QPushButton::clicked, this, [this] { Q_EMIT windowRequested(); });
    lay->addWidget(win_btn);
    summary->setVisible(!full);
    win_btn->setVisible(!full);
    new_host = new QWidget();
    auto *nl = new QVBoxLayout(new_host);
    nl->setContentsMargins(0, 0, 0, 6);
    nl->setSpacing(4);
    QLabel *h = label(u"A new party"_s, u"sub"_s, false);
    QFont hf(h->font());
    hf.setBold(true);
    h->setFont(hf);
    nl->addWidget(h);
    auto *nrow = new QHBoxLayout();
    nrow->setSpacing(6);
    new_name = new QLineEdit();
    new_name->setPlaceholderText(u"Its name in the game — e.g. National Liberal Party"_s);
    connect(new_name, &QLineEdit::returnPressed, this, &PartiesBox::_new_named);
    nrow->addWidget(new_name, 1);
    new_btn = new QPushButton(u"＋ Make it"_s);
    new_btn->setObjectName(u"apply"_s);
    connect(new_btn, &QPushButton::clicked, this, [this] { _new_named(); });
    nrow->addWidget(new_btn);
    nl->addLayout(nrow);
    new_note = label(u"It starts as a copy of the party picked below (ideology, years, policies) under the name typed "
                     u"here; the party picked is not changed. Then change what differs."_s,
                     u"faint"_s);
    nl->addWidget(new_note);
    lay->addWidget(new_host);
    new_host->setVisible(full);
    lst = new QTreeWidget();
    lst->setColumnCount(3);
    lst->setHeaderLabels({u"Party"_s, u"Ideology"_s, u"Years"_s});
    lst->setRootIsDecorated(false);
    lst->setUniformRowHeights(true);
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setTextElideMode(Qt::ElideRight);
    lst->setFixedHeight(190);
    colsizes::make_resizable(lst, u"partyList"_s, {std::nullopt, 110, 96}, true);
    connect(lst, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it, QTreeWidgetItem *) { _picked(it); });
    lay->addWidget(lst);
    form_host = new QWidget();
    form_lay = new QVBoxLayout(form_host);
    form_lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(form_host);
    lst->setVisible(full);
    form_host->setVisible(full);
    warn = label(QString(), u"sub"_s);
    warn->setStyleSheet(u"color: #ff786e;"_s);
    lay->addWidget(warn);
    if (full)
        lay->addStretch(1);
}

void PartiesBox::liveToggled()
{
    Q_EMIT colorPreview(std::nullopt);            // the main window puts the map right for the new setting
}

bool PartiesBox::is_live() const { return live->isChecked(); }

void PartiesBox::_color_preview(const QString &col)
{
    if (live->isChecked()) {
        const QColor c(col);
        Q_EMIT colorPreview(widgets::Rgb{c.red(), c.green(), c.blue()});
    }
}

void PartiesBox::select(int i)
{
    if (QTreeWidgetItem *it = lst->topLevelItem(i))
        lst->setCurrentItem(it);
}

OptStr PartiesBox::loc(const QString &key) const
{
    OptStr t;
    if (doc && doc->maps.contains(u"loc"_s))
        t = doc->map<localisation::LocEdits>(u"loc"_s).text(key);
    else if (const QString *s = mod.loc.get(key))
        t = *s;
    return t && !t->isEmpty() ? t : std::nullopt;
}

std::vector<std::pair<QString, QStringList>> PartiesBox::issue_groups() const
{
    script::Nodes nodes;
    try {
        nodes = script::parse(store->text(u"common/issues.txt"_s));
    } catch (const std::exception &) {
        return {};
    }
    std::vector<std::pair<QString, QStringList>> out;
    const script::Node *pi = nullptr;
    for (const script::Node &n : nodes)
        if (is(n, u"party_issues"_s) && n.is_block) {
            pi = &n;
            break;
        }
    if (!pi)
        return out;
    for (const script::Node &i : pi->kids) {
        if (!(i.has_key && !i.key.isEmpty() && i.is_block))
            continue;
        QStringList opts;
        for (const script::Node &o : i.kids)
            if (o.has_key && !o.key.isEmpty() && o.is_block)
                opts << o.key;
        out.emplace_back(i.key, opts);
    }
    return out;
}

void PartiesBox::refresh(document::Document *doc_, const OptStr &tag_)
{
    doc = doc_;
    store = scripts_of(doc);
    const moddata::Country *c = tag_ ? mod.countries.get(*tag_) : nullptr;
    const OptStr rel_ = c && !c->file.isEmpty() ? OptStr(u"common/"_s + QString(c->file).replace(u'\\', u'/'))
                                                 : std::nullopt;
    if (std::make_pair(tag_, rel_) != std::make_pair(tag, rel)) {
        cur = 0;
        _sig.reset();
    }
    tag = tag_;
    rel = rel_;
    const bool ok = store && rel;
    setVisible(ok);
    if (!ok)
        return;
    file_note->setText(*rel);
    const QString text = store->text(*rel);
    const auto m = py::re_search(u"(?m)^\\s*color\\s*=\\s*\\{\\s*(\\d+)\\s+(\\d+)\\s+(\\d+)\\s*\\}"_s, text);
    if (m.hasMatch())
        color_btn->set(QColor(std::min(255, m.captured(1).toInt()), std::min(255, m.captured(2).toInt()),
                              std::min(255, m.captured(3).toInt()))
                           .name(),
                       false);
    _show_gc(text);
    const script::Nodes nodes = script::parse(text);
    const auto ps = party_nodes(nodes);
    color_note->setText(QString());
    std::set<QString> ideos;
    for (const script::Node *n : ps)
        for (const script::Node &k : n->kids)
            if (is(k, u"ideology"_s))
                ideos.insert(loc(_unq(k)).value_or(_unq(k)));
    QStringList il(ideos.begin(), ideos.end());
    const QString joined = il.join(u", "_s);
    summary->setText(u"%1 part%2 — %3"_s.arg(ps.size()).arg(ps.size() == 1 ? u"y"_s : u"ies"_s,
                                                            joined.isEmpty() ? u"none yet"_s : joined));
    if (!full)
        return;
    lst->blockSignals(true);
    lst->clear();
    for (size_t i = 0; i < ps.size(); ++i) {
        const QHash<QString, QString> f = fields(*ps[i]);
        const QString nm = f.value(u"name"_s);
        const QString id = f.value(u"ideology"_s);
        const QString end4 = f.value(u"end_date"_s).left(4);
        auto *it = new QTreeWidgetItem(QStringList{
            loc(nm).value_or(f.contains(u"name"_s) ? nm : u"?"_s), loc(id).value_or(id),
            u"%1 – %2"_s.arg(f.value(u"start_date"_s, u"?"_s).left(4), end4.isEmpty() ? u"…"_s : end4)});
        it->setData(0, Qt::UserRole, qlonglong(i));
        it->setToolTip(0, nm);
        lst->addTopLevelItem(it);
    }
    cur = std::max<long long>(0, std::min<long long>(cur, (long long)ps.size() - 1));
    if (!ps.empty())
        lst->setCurrentItem(lst->topLevelItem(int(cur)));
    lst->blockSignals(false);
    _fill_form(text, ps);
}

void PartiesBox::_picked(QTreeWidgetItem *it)
{
    if (!it)
        return;
    cur = it->data(0, Qt::UserRole).toLongLong();
    _sig.reset();
    const QString text = store->text(*rel);
    const script::Nodes nodes = script::parse(text);
    _fill_form(text, party_nodes(nodes));
    Q_EMIT partyPicked(cur);
}

void PartiesBox::_fill_form(const QString &text, const std::vector<const script::Node *> &ps)
{
    const script::Node *n = !ps.empty() && cur < (long long)ps.size() ? ps[size_t(cur)] : nullptr;
    const QString inner = n ? text.mid(n->kstart, n->vend - n->kstart) : QString();
    QString nm_key;
    if (n)
        for (const script::Node &c : n->kids)
            if (is(c, u"name"_s)) {
                nm_key = _unq(c);
                break;
            }
    auto sig = std::make_tuple(rel, cur, inner, qsizetype(ps.size()), loc(nm_key));
    if (_sig && *_sig == sig)
        return;
    _sig = sig;
    QWidget *old = form;
    form = new QWidget(form_host);
    auto *g = new QGridLayout(form);
    g->setContentsMargins(0, 4, 0, 0);
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(5);
    g->setColumnStretch(1, 1);
    int r = 0;
    if (n) {
        const QHash<QString, QString> f = fields(*n);
        const QString key = f.value(u"name"_s);
        auto *head = new QLabel(u"The party picked: %1"_s.arg(loc(key).value_or(key)), form);
        head->setWordWrap(true);
        head->setStyleSheet(u"QLabel { background: %1; color: #14100a; font-weight: 700; padding: 5px 10px; "
                            u"border-radius: 4px; }"_s.arg(theme::GOLD));
        g->addWidget(head, r, 0, 1, 2);
        ++r;
        g->addWidget(new QLabel(u"Its name in the game"_s, form), r, 0);
        auto *ne = new QLineEdit(loc(key).value_or(QString()), form);
        ne->setPlaceholderText(u"No localisation present — type one to add it"_s);
        connect(ne, &QLineEdit::editingFinished, this, [this, ne, key] {
            const QString t = words(ne->text());
            if (!key.isEmpty() && t != u"" && t != loc(key).value_or(QString()))
                Q_EMIT nameEdit(key, t);
        });
        g->addWidget(ne, r, 1);
        ++r;
        g->addWidget(new QLabel(u"Its key"_s, form), r, 0);
        auto *ke = new QLineEdit(key, form);
        connect(ke, &QLineEdit::editingFinished, this, [this, ke] {
            const QString t = strip(ke->text());
            _field(u"name"_s, u"\"%1\""_s.arg(t), ke, matches(KEY_RE, t));
        });
        g->addWidget(ke, r, 1);
        ++r;
        const int others = _key_users(key) - 1;
        if (others > 0) {                            // one name for many nations: say so, offer its own
            auto *sh = new QLabel(u"%1 other nation%2 use the same party key “%3”: the name above is theirs too — "
                                  u"changing it renames the party for all of them."_s
                                      .arg(others)
                                      .arg(others == 1 ? QString() : u"s"_s, key),
                                  form);
            sh->setWordWrap(true);
            sh->setStyleSheet(u"color: #e0a050;"_s);
            g->addWidget(sh, r, 0, 1, 2);
            ++r;
            auto *own = new QPushButton(u"Give this party a key of its own (only %1's)"_s.arg(tag.value_or(u"None"_s)),
                                        form);
            connect(own, &QPushButton::clicked, this, [this, key] { _own_key(key); });
            g->addWidget(own, r, 0, 1, 2);
            ++r;
        }
        g->addWidget(new QLabel(u"Ideology"_s, form), r, 0);
        auto *ib = new QComboBox(form);
        ib->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        for (const QString &ideo : ideologies(store))
            ib->addItem(loc(ideo).value_or(ideo), ideo);
        const QString fi = f.value(u"ideology"_s);
        if (ib->findData(fi) < 0)
            ib->addItem(fi, fi);
        ib->setCurrentIndex(ib->findData(fi));
        connect(ib, &QComboBox::activated, this, [this, ib](int) { _field(u"ideology"_s, ib->currentData().toString()); });
        g->addWidget(ib, r, 1);
        ++r;
        for (const auto &[k, text_] : {std::pair{u"start_date"_s, u"Exists from"_s}, std::pair{u"end_date"_s, u"until"_s}}) {
            g->addWidget(new QLabel(text_, form), r, 0);
            auto *de = new QLineEdit(f.value(k), form);
            de->setPlaceholderText(u"year.month.day, like 1836.1.1"_s);
            de->setFixedWidth(150);
            connect(de, &QLineEdit::editingFinished, this, [this, de, kk = k, was = f.value(k)] {
                const QString t = strip(de->text());
                if (t != was)
                    _field(kk, t, de, matches(DATE_RE, t));
            });
            g->addWidget(de, r, 1, Qt::AlignLeft);
            ++r;
        }
        for (const auto &[issue, opts] : issue_groups()) {
            g->addWidget(new QLabel(loc(issue).value_or(py::capitalize(QString(issue).replace(u'_', u' '))), form), r, 0);
            auto *ob = new QComboBox(form);
            ob->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            for (const QString &o : opts)
                ob->addItem(loc(o).value_or(o), o);
            const QString cv = f.value(issue);
            if (ob->findData(cv) < 0)
                ob->addItem(cv.isEmpty() ? u"(not set)"_s : cv, cv);
            ob->setCurrentIndex(ob->findData(cv));
            connect(ob, &QComboBox::activated, this,
                    [this, kk = issue, ob](int) { _field(kk, ob->currentData().toString()); });
            g->addWidget(ob, r, 1);
            ++r;
        }
    }
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 8, 0, 0);
    if (n) {
        auto *rm = new QPushButton(u"✕ Remove this party"_s, form);
        connect(rm, &QPushButton::clicked, this, [this] { _remove(); });
        row->addWidget(rm);
    }
    row->addStretch(1);
    g->addLayout(row, r, 0, 1, 2);
    form_lay->addWidget(form);
    if (old) {
        form_lay->removeWidget(old);
        old->hide();
        old->setParent(nullptr);
        old->deleteLater();
    }
}

// ------------------------------------------------------------------ graphical culture
QStringList PartiesBox::gc_names() const
{
    QString text;
    try {
        text = store->text(u"common/graphicalculturetype.txt"_s);
    } catch (const std::exception &) {
        return {};
    }
    text = py::re_sub(u"#.*"_s, text, [](const QRegularExpressionMatch &) { return QString(); });
    QStringList out;
    auto it = py::rx(u"(?m)^[ \\t]*([A-Za-z_][A-Za-z0-9_]*)"_s).globalMatch(text);
    while (it.hasNext())
        out << it.next().captured(1);
    return out;
}

void PartiesBox::_show_gc(const QString &text)
{
    const QStringList names = gc_names();
    const auto m = py::re_search(GC_RE, text);
    const QString cv = m.hasMatch() ? m.captured(2) : QString();
    if (!_gc_list || names != *_gc_list || (!cv.isEmpty() && gc_box->findData(cv) < 0)) {
        _gc_list = names;
        gc_box->blockSignals(true);
        gc_box->clear();
        gc_box->addItem(u"(none in its file — the game's Generic)"_s, u""_s);
        QStringList all = names;
        if (!cv.isEmpty() && !names.contains(cv))
            all << cv;
        for (const QString &n : all) {
            const QString base = n.endsWith(u"GC"_s) ? n.left(n.size() - 2) : n;
            const QString nice =
                py::re_sub(u"(?<=[a-z])(?=[A-Z])"_s, base, [](const QRegularExpressionMatch &) { return u" "_s; });
            gc_box->addItem(u"%1   (%2)%3"_s.arg(nice, n, names.contains(n) ? QString() : u" — NOT in the game's list"_s),
                            n);
        }
        gc_box->blockSignals(false);
    }
    gc_box->blockSignals(true);
    gc_box->setCurrentIndex(std::max(0, gc_box->findData(cv)));
    gc_box->blockSignals(false);
}

void PartiesBox::_gc()
{
    const QString want = gc_box->currentData().toString();
    const QString text = store->text(*rel);
    const auto m = py::re_search(GC_RE, text);
    QString fresh;
    if (m.hasMatch() && !want.isEmpty())
        fresh = text.left(m.capturedStart(2)) + want + text.mid(m.capturedEnd(2));
    else if (m.hasMatch()) {                      // none: the line goes
        const qsizetype le = text.indexOf(u'\n', m.capturedEnd(0));
        fresh = text.left(m.capturedStart(0)) + (le >= 0 ? text.mid(le + 1) : QString());
    } else if (!want.isEmpty()) {
        const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
        const auto c = py::re_search(u"(?m)^[ \\t]*color\\s*=\\s*\\{[^}]*\\}[^\\n]*\\n"_s, text);
        const qsizetype at = c.hasMatch() ? c.capturedEnd(0) : 0;
        fresh = text.left(at) + u"graphical_culture = %1%2"_s.arg(want, nl) + text.mid(at);
    } else
        return;
    _emit(u"graphical culture %1"_s.arg(want.isEmpty() ? u"none"_s : want), fresh);
}

// ------------------------------------------------------------------ changes
void PartiesBox::_emit(const QString &what, const QString &fresh)
{
    if (fresh != store->text(*rel))
        Q_EMIT scriptEdit(u"%1: %2"_s.arg(tag.value_or(u"None"_s), what), *rel, fresh);
}

void PartiesBox::_field(const QString &key, const QString &value, QLineEdit *ed, bool ok)
{
    if (!ok || value.isEmpty()) {
        if (ed)
            ed->setStyleSheet(u"QLineEdit { border: 2px solid #ff786e; }"_s);
        warn->setText(u"That isn't a valid %1."_s.arg(key.contains(u"date"_s) ? u"date (year.month.day)"_s : u"key"_s));
        return;
    }
    warn->setText(QString());
    const QString text = store->text(*rel);
    const script::Nodes nodes = script::parse(text);
    const auto ps = party_nodes(nodes);
    if (cur < (long long)ps.size())
        _emit(u"party %1 %2"_s.arg(QString(key).replace(u'_', u' '), py::strip(value, u"\""_s)),
              _set_in(text, *ps[size_t(cur)], key, value));
}

int PartiesBox::_key_users(const QString &key)
{
    // How many nations' files have a party with this key (the files on disk, read once).
    if (!_key_use) {
        _key_use.emplace();
        QSet<QString> seen;
        for (const auto &[_t, c] : mod.countries) {
            const QString &f = c.file;
            if (f.isEmpty() || seen.contains(f))
                continue;
            seen.insert(f);
            try {
                QStringList parts{u"common"_s};
                parts << QString(f).replace(u'\\', u'/').split(u'/');
                const QString body = script::read_text(mod.p(parts));
                QSet<QString> ks;
                auto it = py::rx(u"name\\s*=\\s*\"?([A-Za-z0-9_]+)\"?"_s).globalMatch(body);
                while (it.hasNext())
                    ks.insert(it.next().captured(1));
                for (const QString &k : ks)
                    (*_key_use)[k] += 1;
            } catch (const fs::OSError &) {
            }
        }
    }
    return std::max(1, _key_use->value(key, 1));
}

void PartiesBox::_own_key(const QString &key)
{
    // The party picked gets a key only this nation has; its name in the game comes along.
    const QString name = loc(key).value_or(py::title(QString(key).replace(u'_', u' ')));
    QSet<QString> have;
    {
        const script::Nodes nodes = script::parse(store->text(*rel));
        for (const script::Node *n : party_nodes(nodes))
            for (const script::Node &c : n->kids)
                if (is(c, u"name"_s))
                    have.insert(_unq(c));
    }
    const QString base = u"%1_%2"_s.arg(tag.value_or(u"None"_s), py::re_sub(u"^generic_"_s, key,
                                                                             [](const QRegularExpressionMatch &) {
                                                                                 return QString();
                                                                             }));
    QString fresh = base;
    int i = 2;
    while (have.contains(fresh) || _key_users(fresh) > 1 || loc(fresh)) {
        fresh = u"%1_%2"_s.arg(base).arg(i);
        ++i;
    }
    _field(u"name"_s, u"\"%1\""_s.arg(fresh));
    Q_EMIT nameEdit(fresh, name);
}

void PartiesBox::_new_named()
{
    // A new party from the name typed above the list: its key is made from the nation's tag and the name.
    const QString name = words(new_name->text());
    if (name.isEmpty()) {
        warn->setText(u"Type the new party's name first (above the list)."_s);
        return;
    }
    QString slug = py::strip(py::re_sub(u"[^a-z0-9]+"_s, py::lower(name),
                                        [](const QRegularExpressionMatch &) { return u"_"_s; }),
                             u"_"_s);
    if (slug.isEmpty())
        slug = u"party"_s;
    QSet<QString> have;
    {
        const script::Nodes nodes = script::parse(store->text(*rel));
        for (const script::Node *n : party_nodes(nodes))
            for (const script::Node &c : n->kids)
                if (is(c, u"name"_s))
                    have.insert(_unq(c));
    }
    const QString base = u"%1_%2"_s.arg(tag.value_or(u"None"_s), slug);
    QString key = base;
    int i = 2;
    while (have.contains(key)) {
        key = u"%1_%2"_s.arg(base).arg(i);
        ++i;
    }
    if (_new(key)) {
        new_name->clear();
        Q_EMIT nameEdit(key, name);
    }
}

bool PartiesBox::_new(const QString &key)
{
    if (!matches(KEY_RE, key)) {
        warn->setText(u"The new party needs a key: letters, digits or _ (no spaces)."_s);
        return false;
    }
    const QString text = store->text(*rel);
    const script::Nodes nodes = script::parse(text);
    const auto ps = party_nodes(nodes);
    for (const script::Node *n : ps)
        for (const script::Node &c : n->kids)
            if (is(c, u"name"_s) && _unq(c) == key) {
                warn->setText(u"This nation has a party “%1” already."_s.arg(key));
                return false;
            }
    warn->setText(QString());
    QString fresh;
    if (!ps.empty()) {
        const script::Node &src = *ps[size_t(std::min<long long>(cur, (long long)ps.size() - 1))];
        QString block = text.mid(src.kstart, src.vend - src.kstart);
        const auto m = py::re_search(u"name\\s*=\\s*\"?[^\"\\s]*\"?"_s, block);
        if (m.hasMatch())
            block = block.left(m.capturedStart(0)) + u"name = \"%1\""_s.arg(key) + block.mid(m.capturedEnd(0));
        const qsizetype at = ps.back()->vend;
        fresh = text.left(at) + u"\n\n"_s + block + text.mid(at);
        cur = (long long)ps.size();
    } else {
        QString t = text;
        while (t.endsWith(u'\n'))
            t.chop(1);
        fresh = t + u"\n\nparty = {\n\tname = \"%1\"\n\tstart_date = 1800.1.1\n\tend_date = 2000.1.1"
                    u"\n\tideology = conservative\n}\n"_s.arg(key);
        cur = 0;
    }
    _sig.reset();
    _emit(u"new party %1"_s.arg(key), fresh);
    return true;
}

void PartiesBox::_remove()
{
    const QString text = store->text(*rel);
    const script::Nodes nodes = script::parse(text);
    const auto ps = party_nodes(nodes);
    if (cur >= (long long)ps.size())
        return;
    const script::Node &n = *ps[size_t(cur)];
    const qsizetype s = n.kstart > 0 ? text.lastIndexOf(u'\n', n.kstart - 1) + 1 : 0;
    qsizetype e = text.indexOf(u'\n', n.vend);
    e = e < 0 ? text.size() : e + 1;
    QString name = u"?"_s;
    for (const script::Node &c : n.kids)
        if (is(c, u"name"_s)) {
            name = _unq(c);
            break;
        }
    _emit(u"party %1 removed"_s.arg(name), text.left(s) + text.mid(e));
}

void PartiesBox::_color(const QString &col)
{
    const QColor c(col);
    const QString text = store->text(*rel);
    const QString val = u"color = { %1 %2 %3 }"_s.arg(c.red()).arg(c.green()).arg(c.blue());
    const auto m = py::re_search(u"(?m)^(\\s*)color\\s*=\\s*\\{[^}]*\\}"_s, text);
    const QString fresh = m.hasMatch() ? text.left(m.capturedStart(0)) + m.captured(1) + val + text.mid(m.capturedEnd(0))
                                       : val + u"\n"_s + text;
    _emit(u"map colour %1"_s.arg(c.name()), fresh);
}

}  // namespace nationextras
