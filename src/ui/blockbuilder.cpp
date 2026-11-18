// blockbuilder.py — the block builder (see blockbuilder.h).
#include "ui/blockbuilder.h"

#include "core/document.h"
#include "core/events.h"
#include "core/eventtools.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/refs.h"
#include "core/reid.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "ui/scriptedit.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/textselect.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCompleter>
#include <QDrag>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;
namespace bc = blockcode;
using namespace blockshapes;

namespace blockbuilder {

const QString MIME = u"application/x-vic2-block"_s;
const QSet<QString> HANDLED_EVENT = {u"id"_s, u"title"_s, u"desc"_s, u"picture"_s, u"trigger"_s,
                                     u"mean_time_to_happen"_s, u"immediate"_s, u"option"_s, u"fire_only_once"_s,
                                     u"is_triggered_only"_s, u"major"_s, u"news"_s, u"allow_multiple_instances"_s,
                                     u"election"_s};
// words a block whose key is a name is also found by
static const QHash<QString, QString> ALIAS = {
    {u"tag"_s, u"tag nation country scope ENG"_s},       {u"prov"_s, u"province number id scope"_s},
    {u"region"_s, u"state region scope"_s},              {u"poptype"_s, u"pop type farmers scope"_s},
    {u"issue"_s, u"reform issue slavery policy"_s},      {u"tech"_s, u"technology researched"_s},
    {u"ideology"_s, u"ideology liberal"_s},              {u"option"_s, u"issue jingoism"_s},
    {u"goods"_s, u"goods stockpile coal"_s}};
const QSet<QString> HANDLED_DECISION = {u"picture"_s, u"potential"_s, u"allow"_s, u"effect"_s, u"ai_will_do"_s};

const Spec &WEIGHT()
{
    static const Spec w(u""_s, u"eff"_s, u"Chance"_s, u"*"_s, u"a possibility with the weight {key}"_s, u"->"_s);
    return w;
}

static const QRegularExpression LOC_KEY(u"^[A-Za-z0-9_.\\-]+$"_s);
static const QRegularExpression SLOT_RE(u"(\\{[a-z_]*\\})"_s);
static const QRegularExpression SLOT_ONE(u"^\\{([a-z_]*)\\}$"_s);

static const QString PAGE_QSS = uR"(
QLabel#bt { color: #ffffff; font-weight: 600; font-size: 13px; background: transparent; }
QLabel#bw { color: #ffd0cc; font-weight: 600; background: transparent; }
QLineEdit#slot, QComboBox#slot { background: rgba(10,12,16,215); border: 1px solid rgba(255,255,255,80);
    border-radius: 9px; padding: 2px 8px; color: #ffffff; min-height: 18px; }
QLineEdit#slot:focus, QComboBox#slot:focus { border: 2px solid #ffffff; }
QComboBox#slot QLineEdit { background: transparent; border: none; padding: 0px; color: #ffffff; }
QPushButton#bx { background: transparent; border: none; color: rgba(255,255,255,150); padding: 0px;
    font-weight: 700; }
QPushButton#bx:hover { color: #ff786e; }
QPushButton#plus { padding: 2px 9px; border-radius: 9px; }
)"_s;

QString rgb(const QString &kind, double a)
{
    const auto [r, g, b] = RGB_OF(kind);
    return u"rgba(%1,%2,%3,%4)"_s.arg(r).arg(g).arg(b).arg(int(a * 255));
}

static QString kstr(const OptStr &k) { return k ? *k : u"None"_s; }   // str(item["k"])
static bool kin(const OptStr &k, const QSet<QString> &s) { return k && s.contains(*k); }
static bool one_of(const QString &s, std::initializer_list<const char16_t *> xs)
{
    for (const char16_t *x : xs)
        if (s == QStringView(x))
            return true;
    return false;
}

// re.split(r"(\{[a-z_]*\})", text): the parts and the slots between them
static QStringList split_slots(const QString &text)
{
    QStringList out;
    qsizetype last = 0;
    auto it = SLOT_RE.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        out << text.mid(last, m.capturedStart(0) - last) << m.captured(0);
        last = m.capturedEnd(0);
    }
    out << text.mid(last);
    return out;
}

// --- the tree by reference: blockcode's helpers for a PList
static PItem *pchild(const PList &items, const OptStr &key)
{
    for (const PPtr &c : items)
        if (c->k == key)
            return c.get();
    return nullptr;
}

static OptStr pvalue(const PList &items, const QString &key, const OptStr &dflt = std::nullopt)
{
    const PItem *c = pchild(items, key);
    return (c && !c->list) ? OptStr(c->text) : dflt;
}

static QString pval(const PList &items, const QString &key, const QString &dflt) { return *pvalue(items, key, dflt); }

static void pset_value(PList &items, const QString &key, const OptStr &val, bool quoted = false,
                       std::optional<qsizetype> at = std::nullopt)
{
    auto it = std::find_if(items.begin(), items.end(), [&](const PPtr &c) { return c->k == OptStr(key) && !c->list; });
    if (!val) {
        if (it != items.end())
            items.erase(it);
        return;
    }
    if (it == items.end()) {
        const qsizetype where = at ? std::clamp<qsizetype>(*at, 0, qsizetype(items.size())) : qsizetype(items.size());
        items.insert(items.begin() + where, bc::pitem(key, *val, quoted));
    } else {
        (*it)->text = *val;
        (*it)->q = quoted || (*it)->q;
    }
}

static PPtr pblock(PList &items, const QString &key, bool make = false, std::optional<qsizetype> at = std::nullopt)
{
    PItem *p = bc::block(items, key, make, at);
    for (const PPtr &c : items)
        if (c.get() == p)
            return c;
    return nullptr;
}

static QString pdump(const PList &items, long long depth, std::vector<PSpan> *spans = nullptr)
{
    const bc::Items copy = bc::from_ref(items);
    if (!spans)
        return bc::dump(copy, depth);
    std::vector<bc::Span> sp;
    const QString s = bc::dump(copy, depth, &sp);
    QHash<const bc::Item *, const PItem *> of;
    std::function<void(const bc::Items &, const PList &)> walk = [&](const bc::Items &a, const PList &b) {
        for (size_t i = 0; i < a.size(); ++i) {
            of.insert(&a[i], b[i].get());
            if (a[i].list)
                walk(a[i].items, b[i]->items);
        }
    };
    walk(copy, items);
    for (const bc::Span &x : sp)
        spans->push_back(PSpan{x.first, x.last, of.value(x.item)});
    return s;
}

static QString lstrip_tabs(const QString &s)
{
    qsizetype i = 0;
    while (i < s.size() && s.at(i) == u'\t')
        ++i;
    return s.mid(i);
}

template <class T> static qsizetype idx(const std::vector<T> &items, const T &it)
{
    // Where the very item `it` stands among items (two blocks can say the same).
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i] == it)
            return qsizetype(i);
    return -1;
}

static void drop_widget(QWidget *w)
{
    w->hide();
    w->setParent(nullptr);
    w->deleteLater();
}

static void clear_layout(QLayout *lay)
{
    while (lay->count()) {
        QLayoutItem *li = lay->takeAt(0);
        if (QWidget *w = li->widget())
            drop_widget(w);
        delete li;
    }
}

QString Ctx::name() const
{
    return u"%1 about %2"_s.arg(bc::MODE_NAMES.value(mode, u"lines"_s), bc::SCOPE_NAME.value(scope, u"anything"_s));
}

QString slot_type(const Spec &spec, const QString &name)
{
    // What the slot {name} of a block's sentence takes: "" the value, "key" the key that is a name, else a field.
    if (name == u"key" && spec.keytype)
        return bc::KEY_TYPE.value(*spec.keytype);
    if (name.isEmpty())
        return spec.vtype.value_or(u"None"_s);
    if (spec.fields)
        for (const auto &[f, t] : *spec.fields)
            if (f == name)
                return t;
    return u"text"_s;
}

int slot_of(const QString &scope)
{
    return (scope == u"c" || scope == u"p" || scope == u"s") ? SLOT_AT(scope.at(0)) : SLOT_AT(u'c');
}

static BlockW *own_block(QWidget *w)
{
    // The block a value's editor sits in.
    QWidget *p = w->parentWidget();
    while (p && !dynamic_cast<BlockW *>(p))
        p = p->parentWidget();
    return dynamic_cast<BlockW *>(p);
}

// ============================================================================================== value editors
LazyCombo::LazyCombo(Get get_choices_, const QString &value_, bool free, std::function<QString(const QString &)> name_of,
                     QWidget *parent, int width, bool optional_)
    : QComboBox(parent), get_choices(std::move(get_choices_)), free_(free), value(value_), optional(optional_), _max(width)
{
    setObjectName(u"slot"_s);
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setMaxVisibleItems(18);
    QString shown = (optional && value.isEmpty()) ? u"—"_s : (name_of ? name_of(value) : value);
    if (shown.isEmpty())
        shown = value;
    addItem(shown, value);
    fit();
    connect(this, &QComboBox::activated, this, [this](int i) { _activated(i); });
    connect(lineEdit(), &QLineEdit::editingFinished, this, [this] { _typed(); });
}

void LazyCombo::fit()
{
    // As wide as what it shows (a piece is no longer than its sentence) — never wider than its limit.
    setFixedWidth(std::max(70, std::min(_max, fontMetrics().horizontalAdvance(currentText()) + 52)));
    view()->setMinimumWidth(std::min(420, std::max(width(), 260)));
    if (!hasFocus())
        lineEdit()->setCursorPosition(0);   // (a long name shows its start, not its end)
}

void LazyCombo::_fill()
{
    if (_filled)
        return;
    _filled = true;
    bc::Choices ch;
    if (optional)
        ch.push_back({QString(), u"— (left out)"_s});
    if (get_choices)
        if (const auto got = get_choices())
            ch.insert(ch.end(), got->begin(), got->end());
    blockSignals(true);
    clear();
    for (const auto &[k, n] : ch)
        addItem((n == k || n.isEmpty() || k.isEmpty()) ? n : u"%1  (%2)"_s.arg(n, k), k);
    int i = findData(value);
    if (i < 0) {
        insertItem(0, value, value);
        i = 0;
    }
    setCurrentIndex(i);
    QStringList texts;
    for (int j = 0; j < count(); ++j)
        texts << itemText(j);
    auto *comp = new QCompleter(texts, this);
    comp->setCaseSensitivity(Qt::CaseInsensitive);
    comp->setFilterMode(Qt::MatchContains);
    setCompleter(comp);
    blockSignals(false);
    fit();
}

void LazyCombo::showPopup()
{
    _fill();
    QComboBox::showPopup();
}

void LazyCombo::focusInEvent(QFocusEvent *e)
{
    _fill();
    QComboBox::focusInEvent(e);
    if (BlockW *bw = own_block(this))
        bw->b->select(bw->it);
}

void LazyCombo::wheelEvent(QWheelEvent *e)
{
    e->ignore();   // (the wheel scrolls the page, it doesn't change values)
}

void LazyCombo::_set(const QString &v)
{
    fit();
    if (v != value) {
        value = v;
        Q_EMIT picked(v);
    }
}

void LazyCombo::_activated(int i)
{
    _set(itemData(i).toString());
    _mark(true);
}

void LazyCombo::_typed()
{
    const QString t = py::strip(lineEdit()->text());
    const QString low = py::lower(t);
    if (optional && (t.isEmpty() || t == u"—")) {
        _set(QString());
        _mark(true);
        return;
    }
    for (int j = 0; j < count(); ++j)
        if (py::lower(itemText(j)) == low || py::lower(itemData(j).toString()) == low) {
            setCurrentIndex(j);
            _set(itemData(j).toString());
            _mark(true);
            return;
        }
    static const QRegularExpression FREE(u"^[A-Za-z0-9_.\\-]+$"_s);
    if (free_ && FREE.match(t).hasMatch()) {
        _set(t);
        _mark(true);
        return;
    }
    _mark(false);   // not one of the mod's: the value stays what it was
}

void LazyCombo::_mark(bool ok)
{
    setStyleSheet(ok ? QString() : u"QComboBox { border: 2px solid #ff786e; }"_s);
    setToolTip(ok ? QString()
                  : u"That isn't one the mod has — pick one from the list. The block keeps “%1”."_s.arg(value));
}

ValueEdit::ValueEdit(std::shared_ptr<bc::Env> env_, const QString &vtype_, const QString &value_, QWidget *parent,
                     bool optional_)
    : QLineEdit(value_, parent), env(std::move(env_)), vtype(vtype_), value(value_), optional(optional_)
{
    if (optional) {
        setPlaceholderText(u"—"_s);
        setToolTip(u"May stay empty: the line is then left out of the script."_s);
    }
    setObjectName(u"slot"_s);
    if (one_of(vtype, {u"num", u"int", u"prov", u"event"}))
        setAlignment(Qt::AlignCenter);
    connect(this, &QLineEdit::editingFinished, this, [this] { _done(); });
    connect(this, &QLineEdit::textChanged, this, [this](const QString &) { fit(); });
    fit();
    _tip();
}

void ValueEdit::fit()
{
    const bool small = one_of(vtype, {u"num", u"int", u"prov", u"event"});
    const int lo = small ? 58 : 110, hi = small ? 110 : 320;
    setFixedWidth(std::max(lo, std::min(hi, fontMetrics().horizontalAdvance(text()) + 30)));
}

void ValueEdit::_tip()
{
    if (vtype == u"prov") {
        setToolTip(env->legal(u"prov"_s, value) ? env->prov_name(value) : u"A province's number"_s);
    } else if (vtype == u"event" && env->db) {
        events::Entry *const *e = env->db->by_id.get(value);
        setToolTip(e ? env->db->name(**e) : u"No event has this id (yet)"_s);
    }
}

void ValueEdit::focusInEvent(QFocusEvent *e)
{
    QLineEdit::focusInEvent(e);
    if (BlockW *bw = own_block(this))
        bw->b->select(bw->it);
}

void ValueEdit::_done()
{
    const QString t = py::strip(text());
    if (t == value)
        return;
    if (!(optional && t.isEmpty()) && !env->legal(vtype, t)) {
        setStyleSheet(u"QLineEdit { border: 2px solid #ff786e; }"_s);
        static const QHash<QString, QString> WHY = {{u"num"_s, u"A number is needed here (like 5 or 0.25)."_s},
                                                    {u"int"_s, u"A whole number is needed here."_s},
                                                    {u"prov"_s, u"There is no province with this number."_s},
                                                    {u"event"_s, u"An event id is a whole number."_s}};
        setToolTip(WHY.value(vtype, u"That can't be used here."_s));
        return;
    }
    setStyleSheet(QString());
    value = t;
    _tip();
    Q_EMIT picked(t);
}

QWidget *make_editor(BlockBuilder *b, const QString &vtype, const QString &value,
                     std::function<void(const QString &)> on_change, QWidget *parent, const OptStr &issue, bool optional)
{
    // The widget for one value of a block; on_change(new value) when a legal one was set ("" for a field that may be
    // left out: it goes).
    std::shared_ptr<bc::Env> env = b->env;
    if (one_of(vtype, {u"num", u"int", u"prov", u"text"})) {
        auto *w = new ValueEdit(env, vtype, value, parent, optional);
        QObject::connect(w, &ValueEdit::picked, w, on_change);
        return w;
    }
    LazyCombo *w;
    if (vtype == u"event") {
        w = new LazyCombo([env] { return env->choices(u"event"_s); }, value, true,
                          [env](const QString &v) {
                              return (env->db && env->db->by_id.contains(v))
                                         ? u"%1 — %2"_s.arg(v, env->db->name(**env->db->by_id.get(v)))
                                         : v;
                          },
                          parent, 300);
    } else {
        const bool plain = one_of(vtype, {u"bool", u"cflag", u"gflag", u"pflag", u"var"});
        w = new LazyCombo([env, vtype, issue] { return env->choices(vtype, issue); }, value, env->free_text(vtype),
                          [env, plain](const QString &v) {
                              if (plain)
                                  return v;
                              const QString n = env->name(v);
                              return n != v ? u"%1  (%2)"_s.arg(n, v) : v;
                          },
                          parent, one_of(vtype, {u"bool", u"strata"}) ? 110 : 290, optional);
    }
    QObject::connect(w, &LazyCombo::picked, w, on_change);
    return w;
}

// ============================================================================================== blocks
Piece::Piece(QWidget *parent, const QString &kind_) : QWidget(parent), kind(kind_)
{
    setProperty("is_piece", true);
    _v = new QVBoxLayout(this);
    _v->setContentsMargins(0, 0, 0, NH);   // (room below for the bump)
    _v->setSpacing(0);
    head = new QWidget(this);
    head->setMinimumWidth(MIN_W);
    head_lay = new QVBoxLayout(head);
    head_lay->setContentsMargins(10, NH + 3, 6, 5);
    head_lay->setSpacing(2);
    _v->addWidget(head, 0, Qt::AlignLeft);
}

void Piece::set_body(QWidget *w, const std::pair<std::vector<int>, QString> &inner_)
{
    // The mouth: a container of pieces, and the bump inside it (what clips in).
    body = w;
    inner = inner_;
    head->setMinimumWidth(MIN_C);
    _v->addWidget(w);
    _v->addSpacing(LIP);
}

QPainterPath Piece::shape() const
{
    const int hw = head->width(), hh = head->height();
    std::optional<Mouth> mouth;
    if (body)
        mouth = Mouth{double(hh), double(body->geometry().bottom() + 1), inner->first, inner->second};
    return piece_path(hw, hh, tops, top_mode, bottoms, bottom_mode, mouth);
}

void Piece::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    if (kind == u"note") {
        p.setRenderHint(QPainter::Antialiasing);
        const auto [r, g, b] = RGB_OF(u"note"_s);
        p.setBrush(QColor(int(r * 0.36), int(g * 0.36), int(b * 0.30)));
        p.setPen(QPen(QColor(r, g, b), 2, Qt::DashLine));
        p.drawPath(shape());
        if (picked()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(PICKED), 4));
            p.drawPath(shape());
        }
    } else {
        paint_piece(&p, shape(), kind, wrong, false, picked());
    }
    p.end();
}

BlockW::BlockW(BlockBuilder *b_, ContainerW *cont_, PPtr it_) : Piece(cont_), b(b_), cont(cont_), it(std::move(it_))
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    const Ctx &ctx = cont->ctx;
    const OptStr key = it->k;
    const QString mode = one_of(ctx.mode, {u"cond", u"eff"}) ? ctx.mode : u"eff"_s;
    top_mode = bottom_mode = (ctx.is_list || ctx.mode == u"goal") ? u"list"_s : mode;
    bottoms = {slot_of(ctx.scope)};   // the bump goes on to the next piece of the stack
    tops = slots(u"*"_s);
    head->setCursor(Qt::OpenHandCursor);
    auto *rowl = new QHBoxLayout();
    rowl->setContentsMargins(0, 0, 0, 0);
    rowl->setSpacing(5);
    auto *host = new QWidget(head);
    row = new widgets::WrapRow(host, 5);
    rowl->addWidget(host, 1);
    auto *x = new QPushButton(u"✕"_s, head);
    x->setObjectName(u"bx"_s);
    x->setFixedSize(20, 22);
    x->setCursor(Qt::ArrowCursor);
    x->setToolTip(u"Take this block off (or drag it back onto the list on the left)"_s);
    ContainerW *c0 = cont;
    PPtr item = it;
    connect(x, &QPushButton::clicked, this, [c0, item] { c0->remove(item); });
    rowl->addWidget(x, 0, Qt::AlignTop);
    head_lay->addLayout(rowl);
    if (key == OptStr(bc::NOTE) && !it->list) {
        kind = u"note"_s;
        _note(host);
        return;
    }
    const bc::Item val = bc::shallow(*it);
    if (ctx.is_list)
        spec = &WEIGHT();
    else
        spec = one_of(ctx.mode, {u"cond", u"eff", u"goal"})
                   ? bc::spec_for(key, ctx.mode, ctx.scope, b->env.get(), &val)
                   : nullptr;
    const bool is_block = it->list;
    const bool is_w = spec == &WEIGHT();
    bool wrong_ = spec && !is_w && !spec->fits(ctx.scope);
    QString kind_ = spec ? spec->kind() : u"raw"_s;
    if (spec && !is_w && (one_of(spec->vtype.value_or(QString()), {u"block", u"fields"}) != is_block) &&
        spec->vtype != OptStr(u"event"_s)) {
        spec = nullptr;   // written another way than the catalogue knows: as it is
        kind_ = u"raw"_s;
        wrong_ = false;
    }
    kind = kind_;
    wrong = wrong_;
    if (spec && spec != &WEIGHT())
        tops = slots(spec->scopes);   // a notch for every place the block fits
    if (!spec)
        _raw(host, is_block);
    else
        _sentence(host, *spec);
    if (wrong) {
        QStringList about;
        for (const QChar s : spec->scopes)
            if (bc::SCOPE_NAME.contains(QString(s)))
                about << bc::SCOPE_NAME.value(QString(s));
        auto *w = new QLabel(u"⚠ This is about %1, but here %2 is meant: it doesn't sit on the bump — the game ignores it "
                             "or reports an error."_s.arg(about.join(u" or "_s),
                                                          bc::SCOPE_NAME.value(ctx.scope, u"something else"_s)),
                             head);
        w->setObjectName(u"bw"_s);
        w->setWordWrap(true);
        w->setMinimumWidth(260);
        head_lay->addWidget(w);
    }
    if (is_block && (!spec || spec->vtype == OptStr(u"block"_s))) {
        Ctx in;
        if (!spec) {
            in = Ctx{ctx.mode, ctx.scope};
        } else if (spec == &WEIGHT()) {
            in = Ctx{u"eff"_s, ctx.scope};
        } else {
            const QString inn = spec->inner.value_or(QString());
            const QString m2 = one_of(inn, {u"cond", u"eff", u"goal"}) ? inn : ctx.mode;
            in = Ctx{m2, (spec->out && !spec->out->isEmpty()) ? *spec->out : ctx.scope, inn == u"list"};
        }
        QSet<QString> skip;
        if (spec && spec->fields && !spec->fields->empty())
            for (const auto &f : *spec->fields)
                skip.insert(f.first);
        auto *bodyw = new ContainerW(b, it, in, this, skip);
        set_body(bodyw, {{slot_of(in.scope)},
                         (in.is_list || in.mode == u"goal") ? u"list"_s
                         : one_of(in.mode, {u"cond", u"eff"}) ? in.mode
                                                              : u"eff"_s});
    }
}

ContainerW *BlockW::body_c() const { return static_cast<ContainerW *>(body); }

bool BlockW::picked() const { return b->sel.get() == it.get(); }

void BlockW::_note(QWidget *host)
{
    // A note: a line of the user's own words; in the script it is a comment (# …) the game skips.
    auto *lb = new QLabel(u"Note"_s, host);
    lb->setObjectName(u"bt"_s);
    lb->setStyleSheet(u"color: rgb(240,230,160);"_s);
    row->addWidget(lb);
    auto *ed = note_edit = new QLineEdit(py::strip(it->text), host);
    ed->setObjectName(u"slot"_s);
    ed->setPlaceholderText(u"Write anything — it stays in the script as a # comment"_s);
    ed->setFixedWidth(std::max(300, std::min(520, ed->fontMetrics().horizontalAdvance(ed->text()) + 40)));
    connect(ed, &QLineEdit::textChanged, ed, [ed](const QString &) {
        ed->setFixedWidth(std::max(300, std::min(520, ed->fontMetrics().horizontalAdvance(ed->text()) + 40)));
    });
    connect(ed, &QLineEdit::editingFinished, this, [this, ed] {
        QString t = py::split(ed->text()).join(u' ');
        // (the game's files are Windows-1252: signs they can't hold become plain ones, or Apply would fail)
        QString u;
        for (const QChar c : t) {
            switch (c.unicode()) {
            case 0x2014: case 0x2013: u += u'-'; break;
            case 0x2018: case 0x2019: u += u'\''; break;
            case 0x201C: case 0x201D: u += u'"'; break;
            case 0x2026: u += u"..."_s; break;
            default: u += c.unicode() > 255 ? QChar(u'?') : c;
            }
        }
        t = u;
        if (t != ed->text())
            ed->setText(t);
        const QString fresh = t.isEmpty() ? QString() : u" "_s + t;
        if (py::strip(fresh) != py::strip(it->text)) {
            it->text = fresh;
            b->changed(u"note"_s);
        }
    });
    row->addWidget(ed);
}

void BlockW::_say(QWidget *host, const QString &text, bool)
{
    if (py::strip(text).isEmpty())
        return;
    auto *lb = new QLabel(py::strip(text), host);
    lb->setObjectName(u"bt"_s);
    row->addWidget(lb);
}

void BlockW::_sentence(QWidget *host, const Spec &sp)
{
    PPtr item = it;
    BlockBuilder *bb = b;
    const bool bold = sp.vtype == OptStr(u"block"_s);
    if (&sp == &WEIGHT()) {
        _say(host, u"a possibility with the weight"_s, true);
        auto *ed = new ValueEdit(b->env, u"int"_s, kstr(it->k), host);
        connect(ed, &ValueEdit::picked, this, [item, bb](const QString &v) {
            item->k = v;
            bb->changed(u"weight %1"_s.arg(v));
        });
        row->addWidget(ed);
        _say(host, u"— then:"_s);
        return;
    }
    for (const QString &part : split_slots(sp.text)) {
        const auto m = SLOT_ONE.match(part);
        if (!m.hasMatch()) {
            _say(host, part, bold);
            continue;
        }
        const QString f = m.captured(1);
        QWidget *ed;
        if (f == u"key" && sp.keytype) {   // the key is a name: picked like a value
            ed = make_editor(b, bc::KEY_TYPE.value(*sp.keytype), kstr(it->k), [this](const QString &v) { _set_key(v); },
                             host);
        } else if (!f.isEmpty()) {         // a field of the block
            const QString vt = [&] {
                if (sp.fields)
                    for (const auto &[ff, t] : *sp.fields)
                        if (ff == f)
                            return t;
                return u"text"_s;
            }();
            const QString cur = pval(it->items, f, QString());
            ed = make_editor(b, vt, cur, [this, f, vt](const QString &v) { _set_field(f, v, vt); }, host, std::nullopt,
                             sp.optional.contains(f));
        } else if (sp.vtype == OptStr(u"event"_s)) {
            _event_editors(host);
            continue;
        } else {
            const OptStr issue = (sp.vtype == OptStr(u"option"_s) && it->k && b->env->issues.contains(*it->k))
                                     ? it->k
                                     : std::nullopt;
            ed = make_editor(b, sp.vtype.value_or(QString()), it->text,
                             [item, bb](const QString &v) {
                                 item->text = v;
                                 bb->changed(u"%1 = %2"_s.arg(kstr(item->k), v));
                             },
                             host, issue);
        }
        row->addWidget(ed);
    }
    host->setToolTip(u"In the script: %1"_s.arg(kstr(it->k)));
}

void BlockW::_set_field(const QString &f, const QString &val, const QString &vt)
{
    pset_value(it->items, f, val.isEmpty() ? std::nullopt : OptStr(val), vt == u"text");
    b->changed(u"%1: %2 = %3"_s.arg(kstr(it->k), f, val.isEmpty() ? u"left out"_s : val));
}

void BlockW::_set_key(const QString &val)
{
    // Another nation / province / state / reform … picked for a block whose key is that name.
    it->k = val;
    if (spec && spec->keytype == OptStr(u"issue"_s)) {   // another reform: one of its own options
        QStringList opts = b->env->issues.value(val);
        if (opts.isEmpty())
            opts = QStringList{QString()};
        if (it->list || !opts.contains(it->text)) {
            it->list = false;
            it->items.clear();
            it->text = opts[0];
        }
        QPointer<BlockW> self(this);
        QPointer<ContainerW> c(cont);
        QTimer::singleShot(0, b, [self, c] {
            if (self && c)
                c->remake(self);
        });
    }
    b->changed(val);
}

void BlockW::_event_editors(QWidget *host)
{
    // country_event = 123  or  country_event = { id = 123 days = 30 }.
    PPtr item = it;
    BlockBuilder *bb = b;
    const bool is_blk = it->list;
    const QString eid = is_blk ? pval(it->items, u"id"_s, QString()) : it->text;
    const QString days = is_blk ? pval(it->items, u"days"_s, u"0"_s) : u"0"_s;
    auto put = [item, bb](const OptStr &new_id, const OptStr &new_days) {
        const QString i = new_id ? *new_id : (item->list ? pval(item->items, u"id"_s, QString()) : item->text);
        const QString d = new_days ? *new_days : (item->list ? pval(item->items, u"days"_s, u"0"_s) : u"0"_s);
        if (d != u"0" && !d.isEmpty()) {
            item->list = true;
            item->text.clear();
            item->items = PList{bc::pitem(u"id"_s, i), bc::pitem(u"days"_s, d)};
        } else {
            item->list = false;
            item->items.clear();
            item->text = i;
        }
        bb->changed(u"%1 %2"_s.arg(kstr(item->k), i));
    };
    row->addWidget(make_editor(b, u"event"_s, eid, [put](const QString &v) { put(v, std::nullopt); }, host));
    _say(host, u"after"_s);
    auto *ed = new ValueEdit(b->env, u"int"_s, days, host);
    connect(ed, &ValueEdit::picked, this, [put](const QString &v) { put(std::nullopt, v); });
    row->addWidget(ed);
    _say(host, u"days (0: at once)"_s);
}

void BlockW::_raw(QWidget *host, bool is_block)
{
    PPtr item = it;
    BlockBuilder *bb = b;
    auto *key = new QLineEdit(it->k ? *it->k : QString(), host);
    key->setObjectName(u"slot"_s);
    key->setFixedWidth(std::max(90, std::min(240, key->fontMetrics().horizontalAdvance(key->text()) + 30)));
    key->setToolTip(u"A line the builder has no block for — kept exactly as written. Its key:"_s);
    connect(key, &QLineEdit::editingFinished, this, [key, item, bb] {
        const QString t = py::strip(key->text());
        if (!t.isEmpty() && t != kstr(item->k)) {
            item->k = t;
            bb->changed(u"key %1"_s.arg(t));
        }
    });
    row->addWidget(key);
    _say(host, u"="_s);
    if (is_block) {
        _say(host, u"{ these }"_s);
    } else {
        auto *val = new QLineEdit(it->text, host);
        val->setObjectName(u"slot"_s);
        val->setFixedWidth(std::max(90, std::min(260, val->fontMetrics().horizontalAdvance(val->text()) + 30)));
        connect(val, &QLineEdit::editingFinished, this, [val, item, bb] {
            const QString t = py::strip(val->text());
            if (t != item->text && !t.isEmpty()) {
                item->text = t;
                bb->changed(u"%1 = %2"_s.arg(kstr(item->k), t));
            }
        });
        row->addWidget(val);
    }
    _say(host, u"(kept as written)"_s);
}

// --- held with the mouse: the whole block is its own grip
void BlockW::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        _press = e->position().toPoint();
        b->target = body ? body_c() : cont;
        b->select(it);
        e->accept();
    }
}

void BlockW::mouseMoveEvent(QMouseEvent *e)
{
    if (_press && (e->position().toPoint() - *_press).manhattanLength() > 8) {
        _press.reset();
        start_drag();
    }
}

void BlockW::mouseReleaseEvent(QMouseEvent *) { _press.reset(); }

void BlockW::start_drag()
{
    b->dragging = Dragging{u"move"_s, nullptr, it, cont};
    auto *drag = new QDrag(this);
    auto *md = new QMimeData();
    md->setData(MIME, "move");
    drag->setMimeData(md);
    QPixmap pm = grab();
    if (pm.height() > 130)
        pm = pm.copy(0, 0, pm.width(), 130);
    drag->setPixmap(pm.scaledToWidth(std::min(pm.width(), 460), Qt::SmoothTransformation));
    drag->setHotSpot(QPoint(12, 12));
    b->say(u"Let go where the gold bar shows — or over the list on the left to take the block off."_s);
    drag->exec(Qt::MoveAction);
    const std::optional<Drop> drop = b->pending_drop;   // (applied after the drag has ended: safe to rebuild)
    const std::optional<Dragging> moved = b->dragging;
    b->dragging.reset();
    b->pending_drop.reset();
    if (drop && drop->del)
        cont->remove(it);
    else if (drop && drop->cont)
        b->finish_drop(drop->cont, drop->index, nullptr, moved);
}

void BlockW::enterEvent(QEnterEvent *e)
{
    b->explain(it.get(), spec, &cont->ctx);
    QWidget::enterEvent(e);
}

long long lines_inside(const PList &items)
{
    // How many lines stand in a list of items, at any depth.
    long long n = 0;
    std::vector<const PList *> stack{&items};
    while (!stack.empty()) {
        const PList *top = stack.back();
        stack.pop_back();
        for (const PPtr &c : *top) {
            ++n;
            if (c->list)
                stack.push_back(&c->items);
        }
    }
    return n;
}

// ============================================================================================== a stack of pieces
ContainerW::ContainerW(BlockBuilder *b_, PPtr owner_, const Ctx &ctx_, QWidget *parent, const QSet<QString> &skip_,
                       const QString &hint_, const QSet<QString> &skip_blocks_, std::function<void()> on_first_)
    : QWidget(parent), b(b_), owner(std::move(owner_)), ctx(ctx_), skip(skip_), skip_blocks(skip_blocks_), hint(hint_),
      on_first(std::move(on_first_)), top(!dynamic_cast<Piece *>(parent))
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setAcceptDrops(true);
    lay = new PieceStack(this, top ? QMargins(8, 12, 8, 6) : QMargins(ARM, 0, 0, 0));
    line = new QFrame(this);   // where a dragged block would land
    line->setStyleSheet(u"background: %1; border-radius: 2px;"_s.arg(theme::GOLD));
    line->hide();
    rebuild();
}

void ContainerW::paintEvent(QPaintEvent *)
{
    if (!top)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(theme::LINE), 1, Qt::DashLine));
    p.setBrush(QColor(0, 0, 0, 46));
    p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1, height() - 1), 6, 6);
    // the socket: a strip with the bump the first piece clips onto
    const QString mode = ctx.is_list ? u"list"_s : one_of(ctx.mode, {u"cond", u"eff"}) ? ctx.mode : u"eff"_s;
    const int x0 = 8;
    const int sx = x0 + slot_of(ctx.scope);
    QPainterPath path;
    path.moveTo(x0 + 3, 3);
    path.lineTo(x0 + 104, 3);
    path.lineTo(x0 + 104, 12);
    const std::vector<QPointF> pts = conn(sx, 12, mode);
    for (auto q = pts.rbegin(); q != pts.rend(); ++q)
        path.lineTo(*q);
    path.lineTo(x0, 12);
    path.lineTo(x0, 6);
    path.closeSubpath();
    const auto [r, g, bl] = RGB_OF(u"socket"_s);
    p.setPen(QPen(QColor(r, g, bl).lighter(140), 2));
    p.setBrush(QColor(r, g, bl));
    p.drawPath(path);
    p.end();
}

PList ContainerW::shown() const
{
    PList out;
    for (const PPtr &it : items())
        if (!(kin(it->k, skip) && !it->list) && !(kin(it->k, skip_blocks) && it->list))
            out.push_back(it);
    return out;
}

void ContainerW::rebuild()
{
    clear_layout(lay);
    blocks.clear();
    const PList sh = shown();
    const bool lazy = b->lazy;
    if (lazy && b->open_lists && !b->open_lists->contains(&items())) {
        const long long inside = lines_inside(sh);
        if (inside > LAZY_INSIDE) {   // a large event: this stack is made when it is opened
            auto *more = new QPushButton(
                u"▸  %1 line%2 inside — click to open"_s.arg(inside).arg(inside == 1 ? u""_s : u"s"_s), this);
            more->setObjectName(u"plus"_s);
            more->setStyleSheet(u"QPushButton { color: %1; font-weight: 700; text-align: left; padding: 6px 10px; }"_s.arg(
                theme::GOLD));
            connect(more, &QPushButton::clicked, this, [this] { _open_lazy(); });
            lay->addWidget(more);
            line->raise();
            updateGeometry();
            update();
            return;
        }
    }
    const qsizetype limit = lazy ? _limit.value_or(LAZY_STEP) : qsizetype(sh.size());
    for (qsizetype i = 0; i < std::min<qsizetype>(limit, qsizetype(sh.size())); ++i) {
        auto *bw = new BlockW(b, this, sh[size_t(i)]);
        lay->addWidget(bw);
        blocks.push_back(bw);
    }
    if (qsizetype(sh.size()) > limit) {
        const qsizetype rest = qsizetype(sh.size()) - limit;
        auto *more = new QPushButton(
            u"▸  the next %1 of %2 more blocks"_s.arg(std::min<qsizetype>(LAZY_STEP, rest)).arg(rest), this);
        more->setObjectName(u"plus"_s);
        more->setStyleSheet(
            u"QPushButton { color: %1; font-weight: 700; text-align: left; padding: 6px 10px; }"_s.arg(theme::GOLD));
        connect(more, &QPushButton::clicked, this, [this] { _more_lazy(); });
        lay->addWidget(more);
    }
    auto *foot = new QWidget(this);
    auto *fl = new QHBoxLayout(foot);
    fl->setContentsMargins(0, 0, 0, 2);
    fl->setSpacing(6);
    const QString what = ctx.is_list ? u"a possibility"_s : bc::MODE_NAME.value(ctx.mode, u"a line"_s);
    const QStringList wp = py::split(what, u" "_s, 1);
    auto *add = new QPushButton(u"＋ %1"_s.arg(wp.value(1)), foot);
    add->setObjectName(u"plus"_s);
    add->setToolTip(u"Add %1 here: the blocks that fit, to pick from (or drag one in from the list on the left)"_s.arg(what));
    add->setStyleSheet(u"QPushButton { color: %1; }"_s.arg(theme::GOLD));
    connect(add, &QPushButton::clicked, this, [this, add] { b->open_add(this, add); });
    fl->addWidget(add);
    auto *note = new QPushButton(u"＋ note"_s, foot);
    note->setObjectName(u"plus"_s);
    note->setToolTip(u"A line of your own words, anywhere among the blocks. In the script it becomes a comment: # your "
                     "text — the game skips it."_s);
    note->setStyleSheet(u"QPushButton { color: rgb(240,230,160); }"_s);
    connect(note, &QPushButton::clicked, this, [this] { add_note(); });
    fl->addWidget(note);
    if (sh.empty()) {
        auto *t = new QLabel(!hint.isEmpty() ? hint
                             : top           ? u"Nothing here yet — drag a block onto the bump, or press ＋."_s
                                             : u"empty — drag a block in, or press ＋"_s,
                             foot);
        t->setObjectName(u"faint"_s);
        t->setWordWrap(top);
        fl->addWidget(t, 1);
    } else {
        fl->addStretch(1);
    }
    lay->addWidget(foot);
    line->raise();
    updateGeometry();
    update();
}

void ContainerW::_open_lazy()
{
    if (b->open_lists)
        b->open_lists->insert(&items());
    b->_keep.push_back(owner);   // (so the list's identity stays its own)
    rebuild();
    _grew();
}

void ContainerW::_more_lazy()
{
    _limit = _limit.value_or(LAZY_STEP) + LAZY_STEP;
    rebuild();
    _grew();
}

// --- changes
void ContainerW::add_note()
{
    PPtr it = bc::pitem(bc::NOTE, QString());
    b->target = this;
    insert(it);
    b->changed(u"note added"_s);
    for (BlockW *bw : blocks)   // ready to type
        if (bw->it == it && bw->note_edit)
            bw->note_edit->setFocus();
}

void ContainerW::_grew()
{
    // A piece came or went: the pieces around this stack take their new size.
    lay->invalidate();
    QWidget *w = this;
    while (w && w != b->page) {
        w->updateGeometry();
        w->update();
        w = w->parentWidget();
    }
}

void ContainerW::take(PPtr it)
{
    // The block leaves the stack (only its own piece goes; the others stay as they are).
    PList &lst = items();
    lst.erase(std::remove(lst.begin(), lst.end(), it), lst.end());
    auto f = std::find_if(blocks.begin(), blocks.end(), [&](BlockW *x) { return x->it == it; });
    if (f == blocks.end() || blocks.size() <= 1) {
        rebuild();
        return;
    }
    BlockW *bw = *f;
    blocks.erase(f);
    lay->removeWidget(bw);
    drop_widget(bw);
    _grew();
}

void ContainerW::remove(PPtr it)
{
    take(it);
    if (b->sel == it)
        b->select(nullptr);
    b->changed(u"%1 taken off"_s.arg(kstr(it->k)));
}

void ContainerW::remake(BlockW *bw)
{
    // The block's piece made again (its key changed, so its slots are others).
    const qsizetype i = idx(blocks, bw);
    if (i < 0)
        return;
    blocks.erase(blocks.begin() + i);
    lay->removeWidget(bw);
    drop_widget(bw);
    auto *fresh = new BlockW(b, this, bw->it);
    lay->insertWidget(int(i), fresh);
    blocks.insert(blocks.begin() + i, fresh);
    fresh->show();
    _grew();
}

void ContainerW::join()
{
    if (on_first) {
        auto f = std::move(on_first);
        on_first = nullptr;
        f();
    }
}

void ContainerW::insert(const PPtr &it, std::optional<qsizetype> index)
{
    join();
    const PList sh = shown();
    qsizetype at;
    PList &lst = items();
    if (!index || *index >= qsizetype(sh.size())) {
        lst.push_back(it);
        at = qsizetype(blocks.size());
    } else {
        lst.insert(lst.begin() + idx(lst, sh[size_t(*index)]), it);
        at = *index;
    }
    if (blocks.empty()) {
        rebuild();   // (the "nothing here yet" line goes)
        return;
    }
    auto *bw = new BlockW(b, this, it);   // only the new piece is made
    lay->insertWidget(int(at), bw);
    blocks.insert(blocks.begin() + std::min<qsizetype>(at, qsizetype(blocks.size())), bw);
    bw->show();
    _grew();
}

// --- dropping
int ContainerW::_index(double y) const
{
    for (size_t i = 0; i < blocks.size(); ++i)
        if (y < blocks[i]->y() + (blocks[i]->height() - NH) / 2.0)
            return int(i);
    return int(blocks.size());
}

void ContainerW::_show_line(int i)
{
    const QMargins m = lay->contentsMargins();
    int y;
    if (!blocks.empty())
        y = i < int(blocks.size()) ? blocks[size_t(i)]->y() - 2 : blocks.back()->geometry().bottom() - NH;
    else
        y = m.top();
    line->setGeometry(m.left(), y, 170, 5);
    line->show();
    line->raise();
}

void ContainerW::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasFormat(MIME))
        e->acceptProposedAction();
}

void ContainerW::dragMoveEvent(QDragMoveEvent *e)
{
    if (!e->mimeData()->hasFormat(MIME))
        return;
    const OptStr why = b->why_not(this);
    if (why) {
        line->hide();
        b->say(*why, true);
        e->ignore();
        return;
    }
    _show_line(_index(e->position().y()));
    b->say(u"Let go: it clips in here (%1)."_s.arg(ctx.name()));
    b->autoscroll(mapToGlobal(e->position().toPoint()));
    e->acceptProposedAction();
}

void ContainerW::dragLeaveEvent(QDragLeaveEvent *) { line->hide(); }

void ContainerW::dropEvent(QDropEvent *e)
{
    line->hide();
    if (!e->mimeData()->hasFormat(MIME) || b->why_not(this))
        return;
    b->pending_drop = Drop{false, this, _index(e->position().y())};
    e->acceptProposedAction();
}

void ContainerW::mousePressEvent(QMouseEvent *e)
{
    b->target = this;   // (a double-click in the list on the left adds here)
    b->select(nullptr);
    b->say(u"Blocks double-clicked in the list on the left go into these %1."_s.arg(ctx.name()));
    e->accept();
}

// ============================================================================================== factors
FactorCard::FactorCard(Factor *fac, PPtr m) : Piece(fac, u"chance"_s)
{
    setProperty("is_piece", false);   // (it stands in a plain column, not in a stack of pieces)
    BlockBuilder *b = fac->b;
    PPtr owner = fac->owner;
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    auto *lb = new QLabel(fac->more_text, head);
    lb->setObjectName(u"bt"_s);
    row->addWidget(lb);
    row->addWidget(bc_num(b, pval(m->items, u"factor"_s, u"1"_s), head, [m, b](const QString &v) {
        pset_value(m->items, u"factor"_s, v, false, 0);
        b->changed(u"modifier factor %1"_s.arg(v));
    }));
    lb = new QLabel(u"when ALL of this is true:"_s, head);
    lb->setObjectName(u"bt"_s);
    row->addWidget(lb);
    auto *x = new QPushButton(u"✕"_s, head);
    x->setObjectName(u"bx"_s);
    x->setFixedSize(20, 22);
    connect(x, &QPushButton::clicked, fac, [fac, owner, m, b] {
        PList &items = owner->items;
        auto f = std::find(items.begin(), items.end(), m);
        if (f != items.end())
            items.erase(f);
        fac->rebuild();
        b->changed(u"modifier off"_s);
    });
    row->addWidget(x);
    head_lay->addLayout(row);
    set_body(new ContainerW(b, m, Ctx{u"cond"_s, fac->scope}, this, {u"factor"_s}), {{slot_of(fac->scope)}, u"cond"_s});
}

Factor::Factor(BlockBuilder *b_, PPtr owner_, const QString &scope_, QWidget *parent, const QString &base_key_,
               const QString &base_text_, const QString &more_text_, const QString &hint_)
    : QFrame(parent), b(b_), owner(std::move(owner_)), scope(scope_), base_key(base_key_), base_text(base_text_),
      more_text(more_text_), hint(hint_)
{
    lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(5);
    rebuild();
}

static QString time_unit(const PList &items)
{
    for (const QString &u : {u"months"_s, u"days"_s, u"years"_s})
        if (pchild(items, u))
            return u;
    return u"months"_s;
}

void Factor::rebuild()
{
    clear_layout(lay);
    PPtr own = owner;
    BlockBuilder *bb = b;
    auto *topw = new QWidget(this);
    auto *row = new widgets::WrapRow(topw, 6);
    row->addWidget(new QLabel(base_text, topw));
    if (base_key == u"time") {
        const QString unit = time_unit(items());
        row->addWidget(bc_num(b, pval(items(), unit, u"12"_s), topw, [this](const QString &v) { _set_time(v, std::nullopt); }));
        auto *ub = new QComboBox(topw);
        ub->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        ub->setFixedWidth(100);
        for (const QString &u : {u"days"_s, u"months"_s, u"years"_s})
            ub->addItem(u, u);
        ub->setCurrentIndex(ub->findData(unit));
        connect(ub, &QComboBox::activated, this, [this, ub](int) { _set_time(std::nullopt, ub->currentData().toString()); });
        row->addWidget(ub);
    } else {
        row->addWidget(bc_num(b, pval(items(), u"factor"_s, u"1"_s), topw, [own, bb](const QString &v) {
            pset_value(own->items, u"factor"_s, v, false, 0);
            bb->changed(u"factor %1"_s.arg(v));
        }));
    }
    auto *h = new QLabel(hint, topw);
    h->setObjectName(u"faint"_s);
    row->addWidget(h);
    lay->addWidget(topw);
    const PList now = items();
    for (const PPtr &m : now)
        if (m->k == OptStr(u"modifier"_s) && m->list)
            lay->addWidget(new FactorCard(this, m));
    auto *add = new QPushButton(u"＋ %1 … when …"_s.arg(more_text), this);
    add->setObjectName(u"plus"_s);
    add->setStyleSheet(u"QPushButton { color: %1; }"_s.arg(theme::GOLD));
    connect(add, &QPushButton::clicked, this, [this, own, bb] {
        own->items.push_back(bc::pitem(u"modifier"_s, PList{bc::pitem(u"factor"_s, u"0.5"_s)}));
        rebuild();
        bb->changed(u"modifier added"_s);
    });
    lay->addWidget(add, 0, Qt::AlignLeft);
}

void Factor::_set_time(const OptStr &v, const OptStr &unit)
{
    PList &its = items();
    const QString cur = time_unit(its);
    const QString val = v ? *v : pval(its, cur, u"12"_s);
    const QString fresh = (unit && !unit->isEmpty()) ? *unit : cur;
    for (const QString &u : {u"months"_s, u"days"_s, u"years"_s})
        pset_value(its, u, std::nullopt);
    pset_value(its, fresh, val, false, 0);
    b->changed(u"every %1 %2"_s.arg(val, fresh));
}

QWidget *bc_num(BlockBuilder *b, const QString &value, QWidget *parent, std::function<void(const QString &)> on_change)
{
    auto *ed = new ValueEdit(b->env, u"num"_s, value, parent);
    QObject::connect(ed, &ValueEdit::picked, ed, std::move(on_change));
    return ed;
}

void _Words::focusOutEvent(QFocusEvent *e)
{
    QPlainTextEdit::focusOutEvent(e);
    Q_EMIT left();
}

TextField::TextField(BlockBuilder *b_, const QString &key_, QWidget *parent, int lines) : QWidget(parent), b(b_), key(key_)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(1);
    const OptStr text = !key.isEmpty() ? b->env->loc(key) : std::nullopt;
    if (lines > 1) {
        auto *w = new _Words(this);
        w->setPlainText(text.value_or(QString()));
        w->setFixedHeight(22 * lines + 12);
        w->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        w->setPlaceholderText(u"What the player reads — type it here"_s);
        connect(w, &_Words::left, this, [this] { _done(); });
        ed = w;
    } else {
        auto *w = new QLineEdit(text.value_or(QString()), this);
        w->setPlaceholderText(u"What the player reads — type it here"_s);
        connect(w, &QLineEdit::editingFinished, this, [this] { _done(); });
        ed = w;
    }
    v->addWidget(ed);
    auto *k = new QLabel(u"its key in the files: %1"_s.arg(key), this);
    k->setObjectName(u"faint"_s);
    v->addWidget(k);
}

void TextField::_done()
{
    QString t = qobject_cast<QPlainTextEdit *>(ed) ? static_cast<QPlainTextEdit *>(ed)->toPlainText()
                                                   : static_cast<QLineEdit *>(ed)->text();
    t = py::split(t).join(u' ');
    if (!t.isEmpty() && !key.isEmpty() && t != b->env->loc(key).value_or(QString()))
        Q_EMIT b->nameEdit(key, t);
}

Card::Card(QWidget *parent, const QString &title, const QString &about, const QString &kind) : QFrame(parent)
{
    setObjectName(u"card"_s);
    setStyleSheet(u"#card { background: %1; border: 2px solid %2; border-radius: 8px; }"_s.arg(theme::PANEL, rgb(kind, 0.75)));
    v = new QVBoxLayout(this);
    v->setContentsMargins(12, 9, 12, 11);
    v->setSpacing(6);
    auto *row = new QHBoxLayout();
    auto *t = new QLabel(title, this);
    t->setStyleSheet(u"font-size: 15px; font-weight: 700; color: %1;"_s.arg(rgb(kind)));
    row->addWidget(t, 1);
    head = row;
    v->addLayout(row);
    if (!about.isEmpty()) {
        auto *a = new QLabel(about, this);
        a->setWordWrap(true);
        a->setObjectName(u"sub"_s);
        v->addWidget(a);
    }
}

// ============================================================================================== the window
std::vector<std::pair<QString, QString>> spec_chunks(const Spec &spec)
{
    // A catalogue entry as words and value slots (what a slot takes, in a word or two).
    std::vector<std::pair<QString, QString>> out;
    for (const QString &part : split_slots(spec.text)) {
        const auto m = SLOT_ONE.match(part);
        if (m.hasMatch()) {
            const QString vt = slot_type(spec, m.captured(1));
            out.push_back({u"s"_s, bc::SLOT_NAME.value(vt, vt)});
        } else {
            for (const QString &w : py::split(part))
                out.push_back({u"t"_s, w});
        }
    }
    return out;
}

static QFont pal_font()
{
    QFont f(u"Segoe UI"_s);
    f.setPixelSize(13);
    f.setWeight(QFont::DemiBold);
    return f;
}

static QFont pal_slot_font()
{
    QFont f(u"Segoe UI"_s);
    f.setPixelSize(11);
    return f;
}

PaletteBlock::PaletteBlock(BlockBuilder *b_, const Spec *spec_, QWidget *parent)
    : QWidget(parent), b(b_), spec(spec_), flow(spec_chunks(*spec_), pal_font(), pal_slot_font())
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    kind = spec->kind();
    tops = slots(spec->scopes);
    mode = spec->mode;
    if (spec->is_block()) {
        const QString in = spec->inner.value_or(QString());
        const QString inner = one_of(in, {u"list", u"goal"}) ? u"list"_s : one_of(in, {u"cond", u"eff"}) ? in : spec->mode;
        QString out = spec->out.value_or(QString());
        if (out.isEmpty()) {
            out = u"c"_s;
            for (const QChar s : u"cps"_s)
                if (spec->scopes.contains(s)) {
                    out = QString(s);
                    break;
                }
        }
        mouth = std::make_pair(std::vector<int>{slot_of(out)}, inner);
    }
    setCursor(Qt::OpenHandCursor);
    relayout(330);
}

void PaletteBlock::relayout(int maxw)
{
    if (_maxw && maxw == *_maxw)
        return;
    _maxw = maxw;
    const auto [w, h] = flow.lay(maxw, mouth ? MIN_C : MIN_W);
    hh = h;
    setFixedSize(w, h + (mouth ? (16 + LIP) : 0) + NH);
}

void PaletteBlock::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    std::optional<Mouth> m;
    if (mouth)
        m = Mouth{double(hh), double(hh + 16), mouth->first, mouth->second};
    paint_piece(&p, piece_path(width() - 1, hh, tops, mode, tops, mode, m), kind, false, lit);
    flow.paint(&p);
    p.end();
}

void PaletteBlock::enterEvent(QEnterEvent *e)
{
    lit = true;
    update();
    b->explain(nullptr, spec, nullptr);
    QWidget::enterEvent(e);
}

void PaletteBlock::leaveEvent(QEvent *e)
{
    lit = false;
    update();
    QWidget::leaveEvent(e);
}

void PaletteBlock::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        _press = e->position().toPoint();
}

void PaletteBlock::mouseMoveEvent(QMouseEvent *e)
{
    if (_press && (e->position().toPoint() - *_press).manhattanLength() > 8) {
        _press.reset();
        b->dragging = Dragging{u"new"_s, spec, nullptr, nullptr};
        auto *drag = new QDrag(this);
        auto *md = new QMimeData();
        md->setData(MIME, "new");
        drag->setMimeData(md);
        drag->setPixmap(grab());
        drag->setHotSpot(QPoint(12, 12));
        b->say(u"Drag it over a card: it clips in where the gold bar shows."_s);
        drag->exec(Qt::CopyAction);
        const std::optional<Drop> drop = b->pending_drop;
        b->dragging.reset();
        b->pending_drop.reset();
        if (drop && !drop->del && drop->cont)
            b->finish_drop(drop->cont, drop->index, spec);
    }
}

void PaletteBlock::mouseReleaseEvent(QMouseEvent *e)
{
    if (_press && e->button() == Qt::LeftButton) {
        _press.reset();
        Q_EMIT clicked(spec);
    }
}

void PaletteBlock::mouseDoubleClickEvent(QMouseEvent *) { Q_EMIT doubleClicked(spec); }

BlockList::BlockList(BlockBuilder *b_, const std::vector<const Spec *> &specs, OnSpec on_click, OnSpec on_double,
                     QWidget *parent)
    : QScrollArea(parent), b(b_)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setAcceptDrops(true);
    auto *inner = new QWidget();
    lay = new QVBoxLayout(inner);
    lay->setContentsMargins(4, 4, 12, 10);
    lay->setSpacing(7);
    for (const Spec *s : specs) {
        if (!heads.contains(s->cat)) {
            auto *h = new QLabel(s->cat, inner);
            h->setStyleSheet(u"color: %1; font-weight: 700; font-size: 13px; padding-top: 8px;"_s.arg(theme::GOLD));
            lay->addWidget(h);
            heads.insert(s->cat, h);
        }
        auto *pb = new PaletteBlock(b, s, inner);
        if (on_click)
            connect(pb, &PaletteBlock::clicked, pb, on_click);
        if (on_double)
            connect(pb, &PaletteBlock::doubleClicked, pb, on_double);
        lay->addWidget(pb, 0, Qt::AlignLeft);
        rows.push_back({s, pb});
    }
    none = new QLabel(u"No block has these words."_s, inner);
    none->setObjectName(u"faint"_s);
    none->hide();
    lay->addWidget(none);
    lay->addStretch(1);
    setWidget(inner);
}

void BlockList::resizeEvent(QResizeEvent *e)
{
    QScrollArea::resizeEvent(e);
    const int maxw = std::max(160, viewport()->width() - 20);
    for (auto &[s, pb] : rows)
        pb->relayout(maxw);
}

const Spec *BlockList::apply_filter(const QString &q_)
{
    const QString q = py::lower(py::strip(q_));
    QSet<QString> seen;
    const Spec *first = nullptr;
    const QStringList words = py::split(q);
    for (auto &[s, pb] : rows) {
        const QString hay = py::lower(u"%1 %2 %3 %4"_s.arg(s->key, QString(s->key).replace(u'_', u' '), s->text,
                                                           s->keytype ? ALIAS.value(*s->keytype) : QString()));
        bool on = q.isEmpty() || std::all_of(words.begin(), words.end(), [&](const QString &w) { return hay.contains(w); });
        pb->setVisible(on);
        if (on) {
            seen.insert(s->cat);
            if (!first)
                first = s;
        }
    }
    for (auto &[cat, h] : heads)
        h->setVisible(seen.contains(cat));
    none->setVisible(seen.isEmpty());
    return first;
}

void BlockList::jump(const QString &cat)
{
    if (QLabel *const *h = heads.get(cat))
        verticalScrollBar()->setValue(std::max(0, (*h)->y() - 2));
}

// --- a block let go over the list: it is taken off
void BlockList::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasFormat(MIME) && b->dragging && b->dragging->what == u"move") {
        b->say(u"Let go here to take the block off."_s);
        e->acceptProposedAction();
    }
}

void BlockList::dragMoveEvent(QDragMoveEvent *e)
{
    if (e->mimeData()->hasFormat(MIME) && b->dragging && b->dragging->what == u"move")
        e->acceptProposedAction();
}

void BlockList::dropEvent(QDropEvent *e)
{
    if (e->mimeData()->hasFormat(MIME) && b->dragging && b->dragging->what == u"move") {
        b->pending_drop = Drop{true, nullptr, 0};
        e->acceptProposedAction();
    }
}

// ---------------------------------------------------------------------------------------------- the script's colours
namespace {
class _NoRefs : public scriptedit::EditorHost {   // (the preview's highlighter: no references to look up)
public:
    scriptedit::ScriptEditor *current() const override { return nullptr; }
    void _cursor_moved(scriptedit::ScriptEditor *) override {}
    void _decorate(scriptedit::ScriptEditor *) override {}
    void zoom(int) override {}
    void back() override {}
    void forward() override {}
    void follow(const refs::Ref &, bool) override {}
    QString hover_html(const QString &, const refs::Ref &) const override { return QString(); }
};
}  // namespace

// ============================================================================================== BlockBuilder
BlockBuilder::BlockBuilder(moddata::Mod &mod_, QWidget *parent, std::function<QStringList()> pictures_)
    : QDialog(parent), mod(mod_), pictures(std::move(pictures_))
{
    setWindowFlags(Qt::Window);
    setWindowTitle(u"Block builder"_s);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *bar = new QWidget();
    bar->setObjectName(u"side"_s);
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(12, 8, 12, 8);
    bl->setSpacing(8);
    title = widgets::label(u"Block builder"_s, u"title"_s, false);
    bl->addWidget(title);
    sub = new QLabel(QString());
    sub->setObjectName(u"faint"_s);
    bl->addWidget(sub, 1);
    const std::vector<std::tuple<QString, QString, std::function<void()>>> btns = {
        {u"＋ New event"_s, u"A new event with every part ready to fill in"_s, [this] { show_new(u"event"_s); }},
        {u"＋ New decision"_s, u"A new decision with every part ready to fill in"_s, [this] { show_new(u"decision"_s); }},
        {u"↶ Undo"_s, u"Undo the last change (Ctrl+Z)"_s, [this] { Q_EMIT undoRequested(); }},
        {u"↷ Redo"_s, u"Redo (Ctrl+Y)"_s, [this] { Q_EMIT redoRequested(); }},
        {u"Show it in the script editor"_s, u"Go to this event / decision in Scripts"_s, [this] { _to_editor(); }}};
    for (const auto &[t, tip, fn] : btns) {
        auto *b = new QPushButton(t);
        b->setToolTip(tip);
        if (t.startsWith(u"＋"_s))
            b->setObjectName(u"apply"_s);
        connect(b, &QPushButton::clicked, this, fn);
        bl->addWidget(b);
    }
    outer->addWidget(bar);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    outer->addWidget(split, 1);
    // --- left: every block there is
    auto *left = new QWidget();
    left->setObjectName(u"side"_s);
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(10, 8, 8, 8);
    ll->setSpacing(6);
    ll->addWidget(widgets::label(u"The blocks"_s, u"title"_s));
    ll->addWidget(widgets::label(u"Hold a block and drag it into a card — it clips in where the gold bar shows. (Or click "
                                 "where it should go, then double-click the block here.) A block dragged back onto this "
                                 "list is taken off."_s,
                                 u"faint"_s));
    ll->addWidget(new Legend());
    auto *seg = new QHBoxLayout();
    seg->setSpacing(0);
    auto *grp = new QButtonGroup(this);
    for (const auto &[k, t] : std::vector<std::pair<QString, QString>>{{u"cond"_s, u"Conditions"_s}, {u"eff"_s, u"Effects"_s}}) {
        auto *b = new QPushButton(t);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(k == u"cond");
        const QString kk = k;
        connect(b, &QPushButton::clicked, this, [this, kk] { _set_mode(kk); });
        grp->addButton(b);
        seg->addWidget(b, 1);
        mode_btn.insert(k, b);
    }
    ll->addLayout(seg);
    find = new QLineEdit();
    find->setPlaceholderText(u"Find a block (prestige, flag, war …)"_s);
    find->setClearButtonEnabled(true);
    connect(find, &QLineEdit::textChanged, this, [this](const QString &) { _fill_palette(); });
    ll->addWidget(find);
    chips = new QWidget();
    chip_row = new widgets::WrapRow(chips, 4);
    ll->addWidget(chips);
    palette = new QStackedWidget();
    ll->addWidget(palette, 1);
    split->addWidget(left);
    // --- middle: the cards
    scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    page = new QWidget();
    page->setStyleSheet(PAGE_QSS);
    page_lay = new QVBoxLayout(page);
    page_lay->setContentsMargins(12, 10, 12, 14);
    page_lay->setSpacing(10);
    scroll->setWidget(page);
    split->addWidget(scroll);
    // --- right: what a block does; the script the blocks make
    auto *right = new QWidget();
    right->setObjectName(u"side"_s);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(10, 8, 10, 8);
    rl->setSpacing(6);
    rl->addWidget(widgets::label(u"What the block does"_s, u"title"_s));
    help = new QLabel(u"Point at a block."_s);
    help->setWordWrap(true);
    help->setTextFormat(Qt::RichText);
    help->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    help->setMinimumHeight(130);
    rl->addWidget(help);
    rl->addWidget(widgets::label(u"The script the blocks make"_s, u"title"_s));
    rl->addWidget(widgets::label(u"This is what is written into the file — it follows every change. The block you click "
                                 "lights up here; click a line here and its block lights up."_s,
                                 u"faint"_s));
    preview = new QPlainTextEdit();
    preview->setReadOnly(true);
    preview->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    preview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    preview->setStyleSheet(u"QPlainTextEdit { background: #101318; font-family: %1; font-size: 10pt; }"_s.arg(theme::MONO));
    preview->setTabStopDistance(preview->fontMetrics().horizontalAdvance(u' ') * 4);
    _norefs = std::make_unique<_NoRefs>();
    _hl = new scriptedit::ScriptHighlighter(preview->document(), _norefs.get(), QString());
    connect(preview, &QPlainTextEdit::cursorPositionChanged, this, [this] { _script_clicked(); });
    rl->addWidget(preview, 1);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    split->setSizes({400, 900, 400});
    auto *foot = new QWidget();
    foot->setObjectName(u"side"_s);
    auto *fl = new QHBoxLayout(foot);
    fl->setContentsMargins(12, 5, 12, 6);
    status = new QLabel(QString());
    status->setWordWrap(true);
    fl->addWidget(status, 1);
    outer->addWidget(foot);
    auto *sz = new QShortcut(QKeySequence(u"Ctrl+Z"_s), this);
    connect(sz, &QShortcut::activated, this, [this] { Q_EMIT undoRequested(); });
    auto *sy = new QShortcut(QKeySequence(u"Ctrl+Y"_s), this);
    connect(sy, &QShortcut::activated, this, [this] { Q_EMIT redoRequested(); });
    _scroll_timer = new QTimer(this);
    _scroll_timer->setInterval(30);
    connect(_scroll_timer, &QTimer::timeout, this, [this] { _scroll_tick(); });
}

BlockBuilder::~BlockBuilder()
{
    delete _hl;   // (before the host it asks goes)
    _hl = nullptr;
}

void BlockBuilder::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Escape)   // (a dialog would close)
        return;
    if (e->key() == Qt::Key_Delete && sel) {   // the picked block goes
        if (BlockW *bw = block_of(sel.get()))
            bw->cont->remove(bw->it);
        return;
    }
    QDialog::keyPressEvent(e);
}

// ------------------------------------------------------------------ the block that is picked
static std::vector<BlockW *> all_blocks(QWidget *page)
{
    std::vector<BlockW *> out;
    for (QWidget *w : page->findChildren<QWidget *>())
        if (auto *bw = dynamic_cast<BlockW *>(w))
            out.push_back(bw);
    return out;
}

BlockW *BlockBuilder::block_of(const PItem *it) const
{
    if (!it)
        return nullptr;
    for (BlockW *bw : all_blocks(page))
        if (bw->it.get() == it)
            return bw;
    return nullptr;
}

void BlockBuilder::select(const PPtr &it, bool show, bool script)
{
    // One block is the picked one: yellow on the page, its lines lit in the script on the right.
    const PPtr old = sel;
    sel = it;
    if (old != it) {
        for (BlockW *bw : all_blocks(page))
            if (bw->it == old || bw->it == it) {
                bw->update();
                if (show && bw->it == it)
                    scroll->ensureWidgetVisible(bw->head, 30, 90);
            }
    }
    _mark_script(script);
}

void BlockBuilder::_mark_script(bool scroll_)
{
    QPlainTextEdit *ed = preview;
    std::optional<std::pair<long long, long long>> span;
    if (sel)
        for (const PSpan &s : _spans)
            if (s.item == sel.get()) {
                span = std::make_pair(s.first, s.last);
                break;
            }
    QList<QTextEdit::ExtraSelection> sels;
    QTextDocument *d = ed->document();
    if (span) {
        for (long long ln = span->first; ln <= span->second; ++ln) {
            const QTextBlock blk = d->findBlockByNumber(int(ln));
            if (!blk.isValid())
                break;
            QTextCharFormat fmt;
            fmt.setBackground(QColor(255, 216, 74, ln == span->first ? 120 : 62));
            fmt.setProperty(QTextFormat::FullWidthSelection, true);
            QTextEdit::ExtraSelection s;
            s.format = fmt;
            s.cursor = QTextCursor(blk);
            sels.append(s);
        }
    }
    ed->setExtraSelections(sels);
    if (span && scroll_) {
        _marking = true;
        for (long long ln : {span->second, span->first}) {   // (the end, then the start: both in view if they fit)
            const QTextBlock blk = d->findBlockByNumber(int(ln));
            if (blk.isValid()) {
                ed->setTextCursor(QTextCursor(blk));
                ed->ensureCursorVisible();
            }
        }
        _marking = false;
    }
}

void BlockBuilder::_script_clicked()
{
    // A line of the script was clicked: the block it belongs to is the picked one.
    if (_marking || !preview->hasFocus() || !root)
        return;
    const long long ln = preview->textCursor().blockNumber();
    std::vector<std::pair<long long, size_t>> inside;
    for (size_t i = 0; i < _spans.size(); ++i)
        if (_spans[i].first <= ln && ln <= _spans[i].last)
            inside.push_back({_spans[i].last - _spans[i].first, i});
    std::sort(inside.begin(), inside.end());
    for (const auto &[size, i] : inside) {   // the innermost thing that is a block on the page
        if (BlockW *bw = block_of(_spans[i].item)) {
            select(bw->it, true, false);
            return;
        }
    }
    select(nullptr, false, false);
}

// ------------------------------------------------------------------ talking
void BlockBuilder::say(const QString &text, bool bad)
{
    if (_said && _said->first == text && _said->second == bad)   // (asked for on every move of a drag)
        return;
    _said = std::make_pair(text, bad);
    status->setText(text);
    status->setStyleSheet(u"color: %1;"_s.arg(bad ? u"#ff786e"_s : theme::INK_DIM));
}

void BlockBuilder::explain(const PItem *it, const Spec *spec, const Ctx *)
{
    if (it && it->k == OptStr(bc::NOTE) && !it->list) {
        help->setText(u"<b style='color: rgb(240,230,160)'>A note</b><br>Your own words, for you and whoever reads the "
                      "script later. In the file it is a comment — a line starting with # — and the game skips it. Hold "
                      "it and drag it to where it belongs."_s);
        return;
    }
    if (!spec || spec == &WEIGHT()) {
        help->setText(u"<b>%1</b><br>%2"_s.arg(
            it ? refs::esc(kstr(it->k)) : QString(),
            spec == &WEIGHT()
                ? u"One of the possibilities of “exactly one of these”: the bigger its weight next to the others, the "
                  "more often it is the one."_s
                : u"The builder has no block for this line, so it is kept exactly as it was written. It may be right — "
                  "the builder just can't check it."_s));
        return;
    }
    QStringList legal_l;
    for (const QChar s : spec->scopes)
        if (bc::SCOPE_NAME.contains(QString(s)))
            legal_l << bc::SCOPE_NAME.value(QString(s));
    const bool any = spec->scopes.contains(u'*');
    const QString legal = any ? u"anywhere"_s : legal_l.join(u" or "_s);
    QString said;
    {
        const QString t = refs::esc(spec->text);
        static const QRegularExpression R(u"\\{([a-z_]*)\\}"_s);
        qsizetype last = 0;
        auto mi = R.globalMatch(t);
        while (mi.hasNext()) {
            const auto m = mi.next();
            said += t.mid(last, m.capturedStart(0) - last);
            said += u"<b>[%1]</b>"_s.arg(bc::SLOT_NAME.value(slot_type(*spec, m.captured(1)), u"…"_s));
            last = m.capturedEnd(0);
        }
        said += t.mid(last);
    }
    QString what;
    if (spec->vtype == OptStr(u"block"_s)) {
        static const QHash<QString, QString> IN = {{u"cond"_s, u"conditions"_s}, {u"eff"_s, u"effects"_s},
                                                   {u"list"_s, u"possibilities"_s}, {u"goal"_s, u"war goals"_s}};
        const QString key = (spec->inner && !spec->inner->isEmpty()) ? *spec->inner : spec->mode;
        what = u"A block with a mouth: it holds %1%2. The bump inside the mouth shows what clips in."_s.arg(
            IN.value(key, u"blocks"_s),
            (spec->out && !spec->out->isEmpty()) ? u" about "_s + bc::SCOPE_NAME.value(*spec->out)
                                                 : u" about the same as outside"_s);
    } else {
        static const QHash<QString, QString> W = {
            {u"cond"_s, u"A condition: it is either true or not — the pointed notch."_s},
            {u"eff"_s, u"An effect: it changes something — the flat notch."_s},
            {u"goal"_s, u"A war goal: what one side of a war fights for — it only clips into a “declare war” block (the "
                        "round notch)."_s}};
        what = W.value(spec->mode);
    }
    QStringList types;
    if (spec->keytype)
        types << bc::KEY_TYPE.value(*spec->keytype);
    const QString vt = spec->vtype.value_or(u"None"_s);
    if (vt != u"block" && vt != u"fields")
        types << vt;
    if (spec->fields)
        for (const auto &f : *spec->fields)
            types << f.second;
    types.removeDuplicates();
    QString vals;
    for (const QString &ty : types)
        vals += u"<br>• <b>[%1]</b>: %2"_s.arg(
            bc::SLOT_NAME.value(ty, ty),
            refs::esc(bc::VALUE_HELP.value(ty, u"picked from the mod's own list — only what really exists can be chosen"_s)));
    if (!spec->optional.isEmpty()) {
        QStringList o(spec->optional.begin(), spec->optional.end());
        o.sort();
        vals += u"<br>• may stay empty (the line is then left out): %1"_s.arg(o.join(u", "_s));
    }
    if (spec->keytype)
        vals += u"<br>• the first slot is the block's own key: in the script the line starts with that name"_s;
    QString notch;
    if (any) {
        notch = u"all three places"_s;
    } else {
        QStringList ps;
        for (const QChar s : u"cps"_s)
            if (spec->scopes.contains(s))
                ps << (s == u'c' ? u"left"_s : s == u'p' ? u"middle"_s : u"right"_s);
        notch = ps.join(u" and "_s);
    }
    help->setText(u"<b style='color:%1'>%2</b><br>%3<br><br>%4<br>It fits where <b>%5</b> is meant (its notch: %6).%7"
                  "<br><br><span style='color:#8a8272'>In the script: <span style='font-family:Consolas'>%8</span></span>"_s
                      .arg(rgb(spec->kind()), refs::esc(spec->cat), said, what, legal, notch, vals,
                           refs::esc(spec->script())));
}

// ------------------------------------------------------------------ the palette
void BlockBuilder::_set_mode(const QString &mode)
{
    _mode = mode;
    for (auto &[k, b] : mode_btn)
        b->setChecked(k == mode);
    _fill_palette();
}

void BlockBuilder::_fill_palette()
{
    // The list of the kind picked (made when first shown), its headings as jump buttons, the search.
    BlockList *lst = lists.value(_mode, nullptr);
    if (!lst) {
        std::vector<const Spec *> specs;
        auto add = [&](const std::vector<Spec> &v) {
            for (const Spec &s : v)
                if (!s.hidden)
                    specs.push_back(&s);
        };
        if (_mode == u"cond") {
            add(bc::COND());
        } else {
            add(bc::EFF());
            add(bc::GOAL());
        }
        lst = new BlockList(this, specs, {}, [this](const Spec *s) { add_from_palette(s); });
        lists.insert(_mode, lst);
        palette->addWidget(lst);
    }
    palette->setCurrentWidget(lst);
    lst->apply_filter(find->text());
    clear_layout(chip_row);
    for (auto &[cat, h] : lst->heads) {
        auto *c = new QPushButton(cat, chips);
        c->setStyleSheet(u"QPushButton { padding: 2px 8px; font-size: 11px; }"_s);
        c->setToolTip(u"Go to “%1” in the list"_s.arg(cat));
        const QString k = cat;
        connect(c, &QPushButton::clicked, lst, [lst, k] { lst->jump(k); });
        chip_row->addWidget(c);
        c->show();
    }
    chips->updateGeometry();
}

// ------------------------------------------------------------------ what may go where
OptStr BlockBuilder::why_not(ContainerW *cont, const Spec *spec)
{
    // None when what is being dragged (or `spec`) may go into the container — else the reason.
    const Ctx &ctx = cont->ctx;
    const std::optional<Dragging> &d = dragging;
    PPtr it;
    if (!spec) {
        if (!d)
            return u"Nothing is being dragged."_s;
        if (d->what == u"new") {
            spec = d->spec;
        } else {
            it = d->it;
            if (it->k == OptStr(bc::NOTE) && !it->list)
                return std::nullopt;   // a note may stand anywhere
            for (QWidget *w = cont; w; w = w->parentWidget())   // not into itself
                if (auto *bw = dynamic_cast<BlockW *>(w); bw && bw->it == it)
                    return u"A block can't be put into itself."_s;
        }
    }
    if (ctx.is_list) {
        if (it && py::isdigit(kstr(it->k)) && it->list)
            return std::nullopt;
        return u"Only possibilities (with a weight) go here — press “＋ a possibility”, then put effects into it."_s;
    }
    if (it) {
        const Ctx src = d->src ? d->src->ctx : Ctx{};
        if (py::isdigit(kstr(it->k)) && src.is_list)
            return u"A possibility belongs into “exactly one of these”."_s;
        const bc::Item val = bc::shallow(*it);
        const Spec *s_here = bc::spec_for(it->k, ctx.mode, ctx.scope, env.get(), &val);
        const Spec *s_src = one_of(src.mode, {u"cond", u"eff", u"goal"})
                                ? bc::spec_for(it->k, src.mode, src.scope, env.get(), &val)
                                : nullptr;
        if (!s_here) {
            if (s_src && src.mode != ctx.mode)
                return u"That is %1 — here only %2 go."_s.arg(bc::MODE_NAME.value(src.mode),
                                                              bc::MODE_NAMES.value(ctx.mode, u"other lines"_s));
            return std::nullopt;   // a line kept as written: wherever the user puts it
        }
        spec = s_here;
    }
    if (!one_of(ctx.mode, {u"cond", u"eff", u"goal"}))
        return std::nullopt;
    if (spec->mode != ctx.mode)
        return u"That is %1 — here only %2 go.%3"_s.arg(
            bc::MODE_NAME.value(spec->mode), bc::MODE_NAMES.value(ctx.mode),
            spec->mode == u"goal" ? u" A war goal clips into a “declare war” block."_s : QString());
    if (!spec->fits(ctx.scope)) {
        static const QRegularExpression R(u"\\{[a-z_]*\\}"_s);
        QStringList about;
        for (const QChar s : spec->scopes)
            if (bc::SCOPE_NAME.contains(QString(s)))
                about << bc::SCOPE_NAME.value(QString(s));
        static const QHash<QChar, QString> LEAD = {{u'c', u"the province's owner"_s},
                                                   {u'p', u"every one of its provinces"_s},
                                                   {u's', u"every pop"_s}};
        return u"“%1” is about %2 — here %3 is meant. Put it into a block that leads there (like “%4”)."_s.arg(
            QString(spec->text).replace(R, u"…"_s), about.join(u" or "_s),
            bc::SCOPE_NAME.value(ctx.scope, u"something else"_s),
            spec->scopes.isEmpty() ? u"…"_s : LEAD.value(spec->scopes.at(0), u"…"_s));
    }
    return std::nullopt;
}

void BlockBuilder::finish_drop(ContainerW *cont, int index, const Spec *spec, const std::optional<Dragging> &moved)
{
    if (spec) {
        PPtr it = bc::to_ref(bc::new_item(*spec, env.get()));
        cont->insert(it, index);
        select(it);
        changed(u"%1 added"_s.arg(kstr(it->k)));
        return;
    }
    if (!moved || moved->what != u"move")
        return;
    PPtr it = moved->it;
    ContainerW *src = moved->src;
    const PList sh = cont->shown();
    const PPtr before = index < int(sh.size()) ? sh[size_t(index)] : nullptr;
    if (before == it)
        return;
    if (src)
        src->take(it);   // (only this piece moves: nothing else is made again)
    const qsizetype at = before ? idx(cont->shown(), before) : -1;
    cont->insert(it, at >= 0 ? std::optional<qsizetype>(at) : std::nullopt);
    select(it);
    changed(u"%1 moved"_s.arg(kstr(it->k)));
}

void BlockBuilder::autoscroll(const QPoint &gpos)
{
    const int y = scroll->viewport()->mapFromGlobal(gpos).y();
    const int h = scroll->viewport()->height();
    _scroll_step = y < 46 ? -22 : y > h - 46 ? 22 : 0;
    if (_scroll_step && !_scroll_timer->isActive())
        _scroll_timer->start();
}

void BlockBuilder::_scroll_tick()
{
    if (!_scroll_step || !dragging) {
        _scroll_timer->stop();
        return;
    }
    QScrollBar *sb = scroll->verticalScrollBar();
    sb->setValue(sb->value() + _scroll_step);
    _scroll_step = 0;
}

void BlockBuilder::add_from_palette(const Spec *spec)
{
    if (!spec)
        return;
    ContainerW *cont = target;
    std::optional<qsizetype> at;
    BlockW *selw = block_of(sel.get());
    if (selw) {   // into the picked block's mouth, or right under it
        if (selw->body && !why_not(selw->body_c(), spec)) {
            cont = selw->body_c();
        } else if (!why_not(selw->cont, spec)) {
            cont = selw->cont;
            at = idx(selw->cont->blocks, selw) + 1;
        }
    }
    const bool ok = cont && cont->isVisible();
    if (!ok || why_not(cont, spec)) {
        cont = nullptr;
        at.reset();
        for (QWidget *w : page->findChildren<QWidget *>())
            if (auto *c = dynamic_cast<ContainerW *>(w); c && !why_not(c, spec)) {
                cont = c;
                break;
            }
    }
    if (!cont) {
        say(u"No card of this %1 takes “%2”."_s.arg(kind.value_or(u"entry"_s), spec->key), true);
        return;
    }
    PPtr it = bc::to_ref(bc::new_item(*spec, env.get()));
    const bool was_mouth = selw && cont == selw->body;
    cont->insert(it, at);
    if (!was_mouth)
        select(it);   // (the next double-click goes right under this one)
    changed(u"%1 added"_s.arg(kstr(it->k)));
    say(u"Added to the %1."_s.arg(cont->ctx.name()));
}

void BlockBuilder::open_add(ContainerW *cont, QPushButton *button)
{
    // The ＋ of a container: the blocks that fit there — as the pieces they are — to pick from.
    target = cont;
    if (cont->ctx.is_list) {
        cont->insert(bc::pitem(u"50"_s, PList{}));
        changed(u"possibility added"_s);
        return;
    }
    auto *pop = new QFrame(this, Qt::Popup);
    pop->setObjectName(u"addPop"_s);
    pop->setStyleSheet(u"#addPop { background: %1; border: 2px solid %2; border-radius: 6px; }"_s.arg(theme::PANEL,
                                                                                                    theme::GOLD_DIM));
    auto *v = new QVBoxLayout(pop);
    v->setContentsMargins(8, 8, 8, 8);
    v->setSpacing(6);
    v->addWidget(widgets::label(u"Blocks that fit here (%1) — click one to put it in"_s.arg(cont->ctx.name()), u"sub"_s));
    auto *fnd = new QLineEdit(pop);
    fnd->setPlaceholderText(u"Type to find (prestige, flag, war, culture …) — Enter takes the first"_s);
    v->addWidget(fnd);
    std::vector<const Spec *> specs;
    for (const Spec &s : bc::OF_MODE(cont->ctx.mode))
        if (s.fits(cont->ctx.scope) && !s.hidden)
            specs.push_back(&s);
    QPointer<ContainerW> c(cont);
    auto take = [this, pop, c](const Spec *s) {   // (s: nullptr = "raw")
        pop->close();
        if (!c)
            return;
        PPtr fresh = s ? bc::to_ref(bc::new_item(*s, env.get())) : bc::pitem(u"key"_s, u"value"_s);
        c->insert(fresh);
        select(fresh);
        changed(u"%1 added"_s.arg(kstr(fresh->k)));
    };
    auto *lst = new BlockList(this, specs, take, {}, pop);
    lst->setAcceptDrops(false);
    lst->setFixedSize(600, 470);
    v->addWidget(lst);
    auto *raw = new QPushButton(u"Something else: a line typed by hand (key = value)"_s, pop);
    connect(raw, &QPushButton::clicked, pop, [take] { take(nullptr); });
    v->addWidget(raw);
    auto first = std::make_shared<const Spec *>(nullptr);
    connect(fnd, &QLineEdit::textChanged, pop, [first, lst](const QString &q) { *first = lst->apply_filter(q); });
    connect(fnd, &QLineEdit::returnPressed, pop, [first, take] {
        if (*first)
            take(*first);
    });
    pop->adjustSize();
    const QPoint pt = button->mapToGlobal(QPoint(0, button->height() + 2));
    QScreen *scr = QGuiApplication::screenAt(pt);
    if (!scr)
        scr = QGuiApplication::primaryScreen();
    const QRect geo = scr->availableGeometry();
    const int y = pt.y() + pop->height() < geo.bottom() - 4 ? pt.y()
                                                            : std::max(geo.top() + 4, geo.bottom() - pop->height() - 4);
    pop->move(std::min(std::max(geo.left() + 4, pt.x()), geo.right() - pop->width() - 4), y);
    pop->show();
    fnd->setFocus();
    _pop = pop;
}

// ------------------------------------------------------------------ the entry
void BlockBuilder::set_doc(document::Document *doc_, const refs::RefIndex *refs, events::EventDB *db)
{
    const bool fresh = doc_ != doc;
    doc = doc_;
    store = doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    if (fresh || !env || env->refs != refs || env->db != db)
        env = std::make_shared<bc::Env>(mod, doc, refs, db);
    if (lists.isEmpty())
        _fill_palette();
}

std::optional<script::Node> BlockBuilder::_find()
{
    // The entry's node in the file as it is now (or None).
    script::Nodes nodes;
    try {
        if (!store || !rel)
            return std::nullopt;
        nodes = script::parse(store->text(*rel));
    } catch (...) {
        return std::nullopt;
    }
    if (kind == OptStr(u"decision"_s)) {
        for (const script::Node &top : nodes)
            if (top.has_key && top.key == u"political_decisions" && top.is_block)
                for (const script::Node &n : top.kids)
                    if (n.has_key && n.key == ident && n.is_block)
                        return n;
        return std::nullopt;
    }
    for (const script::Node &n : nodes)
        if (n.has_key && eventtools::EVENT_KEYS.contains(n.key) && n.is_block &&
            std::any_of(n.kids.begin(), n.kids.end(), [&](const script::Node &c) {
                return !c.is_block && c.has_key && c.key == u"id" && py::strip(c.value) == ident;
            }))
            return n;
    return std::nullopt;
}

void BlockBuilder::open_entry(const QString &rel_, const QString &kind_, const QString &ident_)
{
    // kind: "event" / "decision"; ident: the event's id / the decision's key.
    rel = rel_;
    kind = kind_;
    ident = ident_;
    sel = nullptr;
    _load();
}

std::optional<std::vector<size_t>> BlockBuilder::_path(const PItem *it) const
{
    // Where an item stands in the entry: the places down from the top (it is found again after a reload).
    if (!root || !it)
        return std::nullopt;
    std::function<std::optional<std::vector<size_t>>(const PList &, std::vector<size_t>)> walk =
        [&](const PList &items, std::vector<size_t> path) -> std::optional<std::vector<size_t>> {
        for (size_t i = 0; i < items.size(); ++i) {
            const PItem *c = items[i].get();
            std::vector<size_t> p = path;
            p.push_back(i);
            if (c == it)
                return p;
            if (c->list)
                if (auto r = walk(c->items, p))
                    return r;
        }
        return std::nullopt;
    };
    return walk(root->items, {});
}

void BlockBuilder::_load()
{
    const std::optional<script::Node> n = _find();
    QString text;
    try {
        text = (rel && store) ? store->text(*rel) : QString();
    } catch (...) {
        text.clear();
    }
    _span_cache.reset();
    if (!n) {
        root = nullptr;
        sel = nullptr;
        _show_empty(u"Pick an event or a decision in Scripts and press “Blocks” — or make a new one with the buttons "
                    "above."_s);
        return;
    }
    const auto path = _path(sel.get());
    const OptStr key = sel ? sel->k : std::nullopt;
    root = nullptr;
    for (const bc::Item &it : bc::read_items(py::slice(text, n->kstart, n->vend)))
        if (it.list) {
            root = bc::to_ref(it);
            break;
        }
    if (!root) {   // (Python: StopIteration)
        _show_empty(QString());
        return;
    }
    lazy = lines_inside(root->items) > LAZY_FROM;   // a very large entry: opened folded
    open_lists = QSet<const void *>();
    _keep.clear();
    sel = nullptr;
    if (path && !path->empty()) {   // the picked block, found again (after an undo …)
        const PList *items = &root->items;
        PPtr it;
        for (size_t i : *path) {
            it = (items && i < items->size()) ? (*items)[i] : nullptr;
            if (!it)
                break;
            items = it->list ? &it->items : nullptr;
        }
        if (it && it->k == key)
            sel = it;
    }
    depth = kind == OptStr(u"decision"_s) ? 1 : 0;
    _written = pdump({root}, depth);
    _build();
}

void BlockBuilder::refresh(document::Document *doc_, const refs::RefIndex *refs, events::EventDB *db)
{
    // The document changed (an edit anywhere, an undo): follow — the cards are built again only when the entry's script
    // isn't what the blocks say.
    if (doc_ != doc) {
        set_doc(doc_, refs, db);
        _load();
        return;
    }
    if (refs) {
        env->refs = refs;
        env->db = db;
        for (const QString &k : {u"cflag"_s, u"gflag"_s, u"pflag"_s, u"var"_s, u"event"_s})
            env->forget(k);
    }
    if (!rel || _own)   // (its own change: the blocks ARE the script)
        return;
    const auto [text, a, b] = _where();
    if (!a) {
        _load();
        return;
    }
    PList first;
    for (const bc::Item &it : bc::read_items(py::slice(text, *a, *b)))
        if (it.list) {
            first.push_back(bc::to_ref(it));
            break;
        }
    if (OptStr(pdump(first, depth)) != _written)
        _load();
}

std::tuple<QString, std::optional<qsizetype>, std::optional<qsizetype>> BlockBuilder::_where()
{
    // (the file's text, start, end of the entry in it) — without reading the file again when it is still what the
    // builder wrote last.
    const QString text = store->text(*rel);
    if (_span_cache && std::get<0>(*_span_cache) == *rel && std::get<1>(*_span_cache) == text)
        return {text, std::get<2>(*_span_cache), std::get<3>(*_span_cache)};
    const auto n = _find();
    if (!n)
        return {text, std::nullopt, std::nullopt};
    _span_cache = std::make_tuple(*rel, text, n->kstart, n->vend);
    return {text, n->kstart, n->vend};
}

void BlockBuilder::changed(const QString &what)
{
    // The blocks changed: the script follows (one undo step of the main window).
    if (!root)
        return;
    const auto [text, a, b] = _where();
    if (!a) {
        say(u"This %1 isn't in the file any more — nothing written."_s.arg(kind.value_or(u"None"_s)), true);
        return;
    }
    _written = pdump({root}, depth);
    const QString new_block = lstrip_tabs(*_written);
    if (kind == OptStr(u"event"_s))
        ident = pval(root->items, u"id"_s, ident);
    _show_preview();
    _check();
    const QString new_text = py::head(text, *a) + new_block + py::tail(text, *b);
    _span_cache = std::make_tuple(*rel, new_text, *a, *a + new_block.size());
    _own = true;
    try {
        Q_EMIT scriptEdit(u"%1: %2"_s.arg(_name(), what), *rel, new_text);
    } catch (...) {
        _own = false;
        throw;
    }
    _own = false;
}

QString BlockBuilder::_name() const
{
    if (!root)
        return QString();
    if (kind == OptStr(u"decision"_s)) {
        const OptStr t = env->loc(ident + u"_title"_s);
        return (t && !t->isEmpty()) ? *t : ident;
    }
    const QString t = pval(root->items, u"title"_s, QString());
    const OptStr l = env->loc(t);
    return (l && !l->isEmpty()) ? *l : u"Event %1"_s.arg(ident);
}

void BlockBuilder::_to_editor()
{
    if (const auto n = _find())
        Q_EMIT openInEditor(*rel, int(n->kstart));
}

void BlockBuilder::_show_preview()
{
    _spans.clear();
    _marking = true;
    const int keep = preview->verticalScrollBar()->value();
    preview->setPlainText(root ? pdump({root}, 0, &_spans) : QString());
    preview->verticalScrollBar()->setValue(keep);
    _marking = false;
    _mark_script(sel != nullptr);
}

void BlockBuilder::_clear_page()
{
    clear_layout(page_lay);
    target = nullptr;
}

void BlockBuilder::_show_empty(const QString &text)
{
    _clear_page();
    title->setText(u"Block builder"_s);
    sub->setText(QString());
    auto *lb = new QLabel(text, page);
    lb->setWordWrap(true);
    lb->setAlignment(Qt::AlignCenter);
    lb->setObjectName(u"sub"_s);
    page_lay->addWidget(lb);
    page_lay->addStretch(1);
    preview->setPlainText(QString());
}

// ------------------------------------------------------------------ building the cards
void BlockBuilder::_build()
{
    const int keep = scroll->verticalScrollBar()->value();
    _clear_page();
    title->setText(_name());
    setWindowTitle(u"Block builder — %1"_s.arg(_name()));
    static const QHash<QString, QString> WHAT = {{u"country_event"_s, u"an event for a nation"_s},
                                                 {u"province_event"_s, u"an event for a province"_s}};
    sub->setText(u"  %1 · %2 · every change is written into the script at once (Ctrl+Z undoes it)"_s.arg(
        WHAT.value(kstr(root->k), u"a decision"_s), rel.value_or(u"None"_s)));
    warn = new QLabel(QString(), page);
    warn->setWordWrap(true);
    warn->setStyleSheet(u"color: #ff786e; font-weight: 600;"_s);
    page_lay->addWidget(warn);
    if (kind == OptStr(u"event"_s))
        _build_event(root);
    else
        _build_decision(root);
    page_lay->addStretch(1);
    _show_preview();
    _check();
    QTimer::singleShot(0, this, [this, keep] { scroll->verticalScrollBar()->setValue(keep); });
}

QWidget *BlockBuilder::_flag(QWidget *parent, PPtr owner, const QString &key, const QString &text, const QString &tip)
{
    auto *cb = new QCheckBox(text, parent);
    cb->setToolTip(tip);
    cb->setChecked(pval(owner->items, key, u"no"_s) == u"yes");
    connect(cb, &QCheckBox::toggled, this, [this, owner, key](bool on) {
        pset_value(owner->items, key, on ? OptStr(u"yes"_s) : std::nullopt);
        changed(u"%1 %2"_s.arg(key, on ? u"yes"_s : u"off"_s));
        if (key == u"is_triggered_only")
            QTimer::singleShot(0, this, [this] { _build(); });
    });
    return cb;
}

QWidget *BlockBuilder::_picture_box(QWidget *parent, PPtr owner)
{
    const QString cur = pval(owner->items, u"picture"_s, QString());
    auto *box = new LazyCombo(
        [this] {
            bc::Choices out;
            if (pictures)
                for (const QString &p : pictures())
                    out.push_back({p, p});
            return std::optional<bc::Choices>(out);
        },
        cur, true, {}, parent, 300);
    connect(box, &LazyCombo::picked, this, [this, owner](const QString &v) {
        pset_value(owner->items, u"picture"_s, v, true, std::min<qsizetype>(3, qsizetype(owner->items.size())));
        changed(u"picture %1"_s.arg(v));
    });
    return box;
}

void BlockBuilder::_build_event(PPtr owner)
{
    PList &items = owner->items;
    const QString scope = root->k == OptStr(u"province_event"_s) ? u"p"_s : u"c"_s;
    const QString who = bc::SCOPE_NAME.value(scope);
    // --- the event itself
    auto *c = new Card(page, u"THE EVENT"_s, QString(), u"raw"_s);
    auto *g = new QGridLayout();
    g->setHorizontalSpacing(10);
    g->setVerticalSpacing(6);
    g->setColumnStretch(1, 1);
    g->addWidget(new QLabel(u"Its number (id)"_s, c), 0, 0);
    auto *row = new QHBoxLayout();
    auto *ide = new QLineEdit(ident, c);
    ide->setFixedWidth(110);
    connect(ide, &QLineEdit::editingFinished, this, [this, ide] { _set_id(ide); });
    row->addWidget(ide);
    auto *kb = new QComboBox(c);
    kb->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    kb->setFixedWidth(250);
    kb->addItem(u"happens to a nation"_s, u"country_event"_s);
    kb->addItem(u"happens to a province"_s, u"province_event"_s);
    kb->setCurrentIndex(kb->findData(kstr(root->k)));
    connect(kb, &QComboBox::activated, this, [this, kb](int) {
        root->k = kb->currentData().toString();
        changed(kb->currentText());
        QTimer::singleShot(0, this, [this] { _build(); });
    });
    row->addWidget(kb);
    id_note = new QLabel(QString(), c);
    _id_note_made = true;
    id_note->setWordWrap(true);
    row->addWidget(id_note, 1);
    g->addLayout(row, 0, 1);
    g->addWidget(new QLabel(u"Title"_s, c), 1, 0, Qt::AlignTop);
    g->addWidget(_text(c, owner, u"title"_s, u"EVTNAME%1"_s.arg(ident)), 1, 1);
    g->addWidget(new QLabel(u"What it tells"_s, c), 2, 0, Qt::AlignTop);
    g->addWidget(_text(c, owner, u"desc"_s, u"EVTDESC%1"_s.arg(ident), 4), 2, 1);
    g->addWidget(new QLabel(u"Picture"_s, c), 3, 0);
    g->addWidget(_picture_box(c, owner), 3, 1, Qt::AlignLeft);
    c->v->addLayout(g);
    auto *fw = new QWidget(c);
    auto *fr = new widgets::WrapRow(fw, 16);
    fr->addWidget(_flag(fw, owner, u"is_triggered_only"_s, u"Only when something calls it"_s,
                        u"It never happens by itself: another event, a decision or the game fires it (is_triggered_only)"_s));
    fr->addWidget(_flag(fw, owner, u"fire_only_once"_s, u"Only once in a game"_s, u"fire_only_once"_s));
    fr->addWidget(_flag(fw, owner, u"major"_s, u"Everyone hears of it"_s, u"major: every nation gets the news"_s));
    fr->addWidget(_flag(fw, owner, u"allow_multiple_instances"_s, u"May run several times at once"_s,
                        u"allow_multiple_instances"_s));
    fr->addWidget(_flag(fw, owner, u"news"_s, u"In the newspaper"_s,
                        u"news = yes: the event shows in the newspaper (its news texts are among “Other lines”)"_s));
    fr->addWidget(_flag(fw, owner, u"election"_s, u"An election event"_s,
                        u"election = yes: it only happens during an election (with its issue_group line)"_s));
    c->v->addWidget(fw);
    page_lay->addWidget(c);
    const bool triggered = pval(items, u"is_triggered_only"_s, u"no"_s) == u"yes";
    // --- when
    c = new Card(page, u"WHEN can it happen?  (trigger)"_s,
                 u"All of these must be true for %1 — then the event may happen. %2"_s.arg(
                     who, triggered ? u"This event only happens when it is called, so it needs no conditions — the ones "
                                      "here are still checked when it is called."_s
                                    : u"With nothing here it can happen to every %1."_s.arg(py::split(who, u" "_s, 1).value(1))),
                 u"cond"_s);
    PPtr trig = pblock(items, u"trigger"_s);
    if (!trig && !triggered)
        trig = pblock(items, u"trigger"_s, true, _slot(items, u"trigger"_s));
    if (trig) {
        c->v->addWidget(new ContainerW(this, trig, Ctx{u"cond"_s, scope}, c));
    } else {
        auto *b = new QPushButton(u"＋ Give it conditions all the same"_s, c);
        connect(b, &QPushButton::clicked, this, [this, owner] {
            pblock(owner->items, u"trigger"_s, true, _slot(owner->items, u"trigger"_s));
            changed(u"trigger added"_s);
            _build();
        });
        c->v->addWidget(b, 0, Qt::AlignLeft);
    }
    page_lay->addWidget(c);
    // --- how often
    if (!triggered) {
        c = new Card(page, u"HOW OFTEN?  (mean time to happen)"_s,
                     u"Once the conditions are true, it takes about this long on average until it happens. The lines "
                     "below make it come sooner (a number under 1) or later (over 1) while their own conditions are "
                     "true."_s,
                     u"chance"_s);
        PPtr mt = pblock(items, u"mean_time_to_happen"_s, true, _slot(items, u"mean_time_to_happen"_s));
        if (!pchild(mt->items, u"months"_s) && !pchild(mt->items, u"days"_s) && !pchild(mt->items, u"years"_s))
            mt->items.insert(mt->items.begin(), bc::pitem(u"months"_s, u"12"_s));
        c->v->addWidget(new Factor(this, mt, scope, c, u"time"_s, u"It happens about every"_s, u"the time × "_s,
                                   u"(on average)"_s));
        page_lay->addWidget(c);
    }
    // --- right away
    c = new Card(page, u"WHAT happens RIGHT AWAY?  (immediate)"_s,
                 u"Done the moment the event happens — before the player picks a choice, and whatever is picked."_s,
                 u"eff"_s);
    PPtr im = pblock(items, u"immediate"_s);
    std::function<void()> first;
    if (!im) {   // (not in the script until something is put in)
        im = bc::pitem(u"immediate"_s, PList{});
        first = [this, owner, im] {
            owner->items.insert(owner->items.begin() + _slot(owner->items, u"immediate"_s), im);
        };
    }
    c->v->addWidget(new ContainerW(this, im, Ctx{u"eff"_s, scope}, c, {},
                                   u"Nothing happens before the choice — that is fine. Press ＋ to add something."_s, {},
                                   first));
    page_lay->addWidget(c);
    // --- the choices
    PList opts;
    for (const PPtr &it : items)
        if (it->k == OptStr(u"option"_s) && it->list)
            opts.push_back(it);
    for (size_t i = 0; i < opts.size(); ++i) {
        const PPtr op = opts[i];
        const QString letter = QString(u"ABCDEFGHIJ"_s.at(qsizetype(i % 10)));
        c = new Card(page, u"CHOICE %1  (option)"_s.arg(letter),
                     u"A button of the event: its words, what happens when it is pressed, and how gladly the computer "
                     "presses it."_s,
                     u"eff"_s);
        if (opts.size() > 1) {
            auto *x = new QPushButton(u"✕ Remove this choice"_s, c);
            connect(x, &QPushButton::clicked, this, [this, owner, op] {
                auto f = std::find(owner->items.begin(), owner->items.end(), op);
                if (f != owner->items.end())
                    owner->items.erase(f);
                changed(u"choice removed"_s);
                _build();
            });
            c->head->addWidget(x);
        }
        auto *gg = new QGridLayout();
        gg->setHorizontalSpacing(10);
        gg->setColumnStretch(1, 1);
        gg->addWidget(new QLabel(u"The button says"_s, c), 0, 0, Qt::AlignTop);
        gg->addWidget(_text(c, op, u"name"_s, u"EVTOPT%1%2"_s.arg(letter, ident)), 0, 1);
        c->v->addLayout(gg);
        c->v->addWidget(_h(c, u"When it is pressed:"_s));
        c->v->addWidget(new ContainerW(this, op, Ctx{u"eff"_s, scope}, c, {u"name"_s},
                                       u"Pressing it does nothing yet. Press ＋ to add what happens."_s, {u"ai_chance"_s}));
        PPtr ai = pblock(op->items, u"ai_chance"_s);
        if (!ai) {
            auto *mk = new QPushButton(u"＋ Say how gladly the computer picks this choice (without it: all choices alike)"_s, c);
            mk->setStyleSheet(u"QPushButton { padding: 3px 10px; color: %1; }"_s.arg(theme::GOLD));
            connect(mk, &QPushButton::clicked, this, [this, op] {
                op->items.push_back(bc::pitem(u"ai_chance"_s, PList{bc::pitem(u"factor"_s, u"1"_s)}));
                changed(u"ai_chance added"_s);
                _build();
            });
            c->v->addWidget(mk, 0, Qt::AlignLeft);
        } else {
            c->v->addWidget(_h(c, u"How gladly the computer picks it:"_s));
            if (!pchild(ai->items, u"factor"_s))
                ai->items.insert(ai->items.begin(), bc::pitem(u"factor"_s, u"1"_s));
            c->v->addWidget(new Factor(this, ai, scope, c, u"factor"_s, u"Its weight is"_s, u"the weight × "_s,
                                       u"(next to the other choices' weights)"_s));
        }
        page_lay->addWidget(c);
    }
    auto *add = new QPushButton(u"＋ Add a choice"_s, page);
    add->setObjectName(u"apply"_s);
    connect(add, &QPushButton::clicked, this, [this, owner] { _add_option(owner); });
    page_lay->addWidget(add, 0, Qt::AlignLeft);
    _others(items, HANDLED_EVENT);
}

void BlockBuilder::_build_decision(PPtr owner)
{
    PList &items = owner->items;
    auto *c = new Card(page, u"THE DECISION"_s, QString(), u"raw"_s);
    auto *g = new QGridLayout();
    g->setHorizontalSpacing(10);
    g->setVerticalSpacing(6);
    g->setColumnStretch(1, 1);
    g->addWidget(new QLabel(u"Its key"_s, c), 0, 0);
    auto *k = new QLabel(ident, c);
    k->setStyleSheet(u"font-family: %1;"_s.arg(theme::MONO));
    g->addWidget(k, 0, 1);
    g->addWidget(new QLabel(u"Title"_s, c), 1, 0, Qt::AlignTop);
    g->addWidget(new TextField(this, ident + u"_title"_s, c), 1, 1);
    g->addWidget(new QLabel(u"What it tells"_s, c), 2, 0, Qt::AlignTop);
    g->addWidget(new TextField(this, ident + u"_desc"_s, c, 4), 2, 1);
    g->addWidget(new QLabel(u"Picture"_s, c), 3, 0);
    g->addWidget(_picture_box(c, owner), 3, 1, Qt::AlignLeft);
    c->v->addLayout(g);
    page_lay->addWidget(c);
    const std::vector<std::tuple<QString, QString, QString, QString>> parts = {
        {u"potential"_s, u"WHO sees it?  (potential)"_s,
         u"Only nations for which all of this is true have the decision in their list at all."_s, u"cond"_s},
        {u"allow"_s, u"WHEN may it be taken?  (allow)"_s,
         u"The nation can press it once all of this is true — until then the list shows what is missing."_s, u"cond"_s},
        {u"effect"_s, u"WHAT does it do?  (effect)"_s, u"Done when the decision is taken."_s, u"eff"_s}};
    for (const auto &[key, t, about, mode] : parts) {
        c = new Card(page, t, about, mode);
        PPtr blk = pblock(items, key, true);
        c->v->addWidget(new ContainerW(this, blk, Ctx{mode, u"c"_s}, c));
        page_lay->addWidget(c);
    }
    c = new Card(page, u"Will the COMPUTER take it?  (ai_will_do)"_s,
                 u"1 = yes as soon as it may, 0 = never. The lines below change the number while their own conditions are "
                 "true."_s,
                 u"chance"_s);
    PPtr ai = pblock(items, u"ai_will_do"_s, true);
    if (!pchild(ai->items, u"factor"_s))
        ai->items.insert(ai->items.begin(), bc::pitem(u"factor"_s, u"1"_s));
    c->v->addWidget(new Factor(this, ai, u"c"_s, c, u"factor"_s, u"It takes it with the weight"_s, u"the weight × "_s, QString()));
    page_lay->addWidget(c);
    _others(items, HANDLED_DECISION);
}

QLabel *BlockBuilder::_h(QWidget *parent, const QString &text)
{
    auto *lb = new QLabel(text, parent);
    lb->setStyleSheet(u"font-weight: 700;"_s);
    return lb;
}

qsizetype BlockBuilder::_slot(const PList &items, const QString &key) const
{
    // Where a missing part belongs among the entry's lines (the usual order of an event).
    static const QStringList order = {u"id"_s, u"title"_s, u"desc"_s, u"picture"_s, u"major"_s, u"fire_only_once"_s,
                                      u"is_triggered_only"_s, u"allow_multiple_instances"_s, u"trigger"_s,
                                      u"mean_time_to_happen"_s, u"immediate"_s, u"option"_s};
    const QStringList after = order.mid(order.indexOf(key) + 1);
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i]->k && after.contains(*items[i]->k))
            return qsizetype(i);
    return qsizetype(items.size());
}

QWidget *BlockBuilder::_text(QWidget *parent, PPtr owner, const QString &key, const QString &default_key, int lines)
{
    // A title / description / button text: the words behind its localisation key (a key is made when the script has
    // none, or plain words instead of one).
    PList &items = owner->items;
    const QString cur = pval(items, key, QString());
    if (cur.isEmpty() || !LOC_KEY.match(cur).hasMatch()) {
        const QString words = cur;
        pset_value(items, key, default_key, true, key != u"name" ? _slot(items, key) : 0);
        auto *tf = new TextField(this, default_key, parent, lines);
        const OptStr had = env->loc(default_key);
        if (!words.isEmpty() && !(had && !had->isEmpty())) {
            QTimer::singleShot(0, this, [this, key, default_key, words] {
                changed(u"%1 key"_s.arg(key));
                Q_EMIT nameEdit(default_key, words);
            });
        } else if (cur.isEmpty()) {
            QTimer::singleShot(0, this, [this, key] { changed(u"%1 key"_s.arg(key)); });
        }
        return tf;
    }
    return new TextField(this, cur, parent, lines);
}

void BlockBuilder::_add_option(PPtr owner)
{
    PList &items = owner->items;
    const qsizetype n = std::count_if(items.begin(), items.end(), [](const PPtr &it) { return it->k == OptStr(u"option"_s); });
    const QString letter = QString(u"ABCDEFGHIJ"_s.at(n % 10));
    items.push_back(bc::pitem(u"option"_s, PList{bc::pitem(u"name"_s, u"EVTOPT%1%2"_s.arg(letter, ident), true),
                                                 bc::pitem(u"ai_chance"_s, PList{bc::pitem(u"factor"_s, u"1"_s)})}));
    changed(u"choice %1 added"_s.arg(letter));
    _build();
    QTimer::singleShot(60, this, [this] { scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum()); });
}

void BlockBuilder::_others(const PList &items, const QSet<QString> &handled)
{
    PList rest;
    for (const PPtr &it : items)
        if (!kin(it->k, handled))
            rest.push_back(it);
    if (rest.empty())
        return;
    if (std::all_of(rest.begin(), rest.end(), [](const PPtr &it) { return it->k == OptStr(bc::NOTE) && !it->list; })) {
        auto *c = new Card(page, u"Notes between the parts"_s,
                           u"Comments of the script that stand between its parts — kept where they are. (Notes inside a "
                           "part are among its blocks.)"_s,
                           u"raw"_s);
        for (const PPtr &it : rest) {
            auto *lb = new QLabel(u"#"_s + it->text, c);
            lb->setWordWrap(true);
            lb->setStyleSheet(u"font-family: %1; color: rgb(200,192,140);"_s.arg(theme::MONO));
            c->v->addWidget(lb);
        }
        page_lay->addWidget(c);
        return;
    }
    auto *c = new Card(page, u"Other lines"_s,
                       u"Lines of this %1 the cards above have no place for — kept as written."_s.arg(kind.value_or(u"None"_s)),
                       u"raw"_s);
    for (const PPtr &it : rest) {
        auto *lb = new QLabel(pdump({it}, 0).replace(u"\t"_s, u"    "_s), c);
        lb->setStyleSheet(u"font-family: %1; color: %2;"_s.arg(theme::MONO, theme::INK_DIM));
        lb->setWordWrap(true);
        c->v->addWidget(lb);
    }
    page_lay->addWidget(c);
}

void BlockBuilder::_set_id(QLineEdit *ed)
{
    const QString fresh = py::strip(ed->text());
    if (fresh == ident)
        return;
    if (!py::isdigit(fresh)) {
        ed->setStyleSheet(u"QLineEdit { border: 2px solid #ff786e; }"_s);
        if (id_note) {
            id_note->setText(u"An event's id is a whole number."_s);
            id_note->setStyleSheet(u"color: #ff786e;"_s);
        }
        return;
    }
    const eventtools::Ids used = eventtools::event_ids(mod, *store);
    if (used.contains(fresh)) {
        ed->setStyleSheet(u"QLineEdit { border: 2px solid #ff786e; }"_s);
        const QString rel0 = std::get<0>(used.get(fresh)->at(0));
        QStringList keys;
        for (const auto &kv : used)
            keys << kv.first;
        if (id_note) {
            id_note->setText(u"%1 is taken already (%2) — every event needs a number of its own. Free: %3"_s.arg(
                fresh, rel0, eventtools::free_id(keys, eventtools::ids_of_file(mod, *store, *rel))));
            id_note->setStyleSheet(u"color: #ff786e;"_s);
        }
        return;
    }
    ed->setStyleSheet(QString());
    const auto n = _find();
    if (!n)
        return;
    pset_value(root->items, u"id"_s, fresh);
    const QString text = store->text(*rel);
    _written = pdump({root}, depth);
    ident = fresh;
    Q_EMIT scriptEdit(u"Event id %1"_s.arg(fresh), *rel,
                      py::head(text, n->kstart) + lstrip_tabs(pdump({root}, depth)) + py::tail(text, n->vend));
    _show_preview();
    _check();
}

void BlockBuilder::_check()
{
    // What is wrong with the entry as a whole (shown in red above the cards).
    if (!root || !warn)
        return;
    QStringList notes;
    [&] {
        if (kind != OptStr(u"event"_s))
            return;
        const eventtools::Ids ids = eventtools::event_ids(mod, *store);
        const std::vector<eventtools::Place> used = ids.value(ident);
        if (used.size() > 1) {
            QStringList rels;
            for (const auto &p : used)
                if (!rels.contains(std::get<0>(p)))
                    rels << std::get<0>(p);
            rels.sort();
            notes << u"⚠ %1 events have the id %2 (%3): the game uses only one of them. Give this one another number."_s
                         .arg(used.size())
                         .arg(ident, rels.join(u", "_s));
        }
        if (_id_note_made && used.size() <= 1) {
            if (!id_note)   // (Python: the label is gone — RuntimeError, the rest is skipped)
                return;
            id_note->setText(u"no other event has this number ✓"_s);
            id_note->setStyleSheet(u"color: #7fd68a;"_s);
        }
        PList &items = root->items;
        PPtr trig = pblock(items, u"trigger"_s);
        if (pval(items, u"is_triggered_only"_s, u"no"_s) != u"yes" && (!trig || trig->items.empty()))
            notes << u"⚠ No conditions: this event will happen to every %1 again and again. Add conditions — or tick "
                     "“Only when something calls it”."_s.arg(root->k == OptStr(u"province_event"_s) ? u"province"_s
                                                                                                    : u"nation"_s);
        if (std::none_of(items.begin(), items.end(), [](const PPtr &it) { return it->k == OptStr(u"option"_s); }))
            notes << u"⚠ No choice: an event needs at least one button."_s;
    }();
    warn->setText(notes.join(u'\n'));
    warn->setVisible(!notes.isEmpty());
}

// ------------------------------------------------------------------ a new event / decision
void BlockBuilder::show_new(const QString &kind_)
{
    _clear_page();
    root = nullptr;
    rel.reset();
    preview->setPlainText(QString());
    title->setText(u"A new %1"_s.arg(kind_));
    sub->setText(QString());
    scriptstore::ScriptStore *st = store;
    const QString folder = kind_ == u"event" ? u"events"_s : u"decisions"_s;
    const QStringList files = bc::files_of(mod, st, folder);
    auto *c = new Card(page, u"A NEW %1"_s.arg(py::upper(kind_)),
                       u"Fill this in and press “Make it”: the %1 is written with every part ready — %2 — and opens here "
                       "as blocks."_s.arg(kind_, kind_ == u"event"
                                                     ? u"its number, title and text, when it can happen, how often, what "
                                                       "happens right away, its choices and how the computer picks"_s
                                                     : u"its texts, who sees it, when it may be taken, what it does and "
                                                       "whether the computer takes it"_s),
                       u"eff"_s);
    auto *g = new QGridLayout();
    g->setHorizontalSpacing(10);
    g->setVerticalSpacing(8);
    g->setColumnStretch(1, 1);
    int r = 0;
    g->addWidget(new QLabel(u"Into the file"_s, c), r, 0);
    auto *fb = new QComboBox(c);
    fb->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    fb->setFixedWidth(380);
    for (const QString &f : files)
        fb->addItem(f, f);
    fb->addItem(u"＋ a new file …"_s, QVariant());
    auto *row = new QHBoxLayout();
    row->addWidget(fb);
    auto *nf = new QLineEdit(c);
    nf->setPlaceholderText(u"the new file's name (without .txt)"_s);
    nf->setFixedWidth(280);
    row->addWidget(nf);
    row->addStretch(1);
    g->addLayout(row, r, 1);
    ++r;
    QLineEdit *ide = nullptr, *keyed = nullptr;
    QComboBox *kb = nullptr, *ob = nullptr;
    QCheckBox *trig = nullptr;
    QLabel *idn;
    if (kind_ == u"event") {
        g->addWidget(new QLabel(u"Its number (id)"_s, c), r, 0);
        row = new QHBoxLayout();
        ide = new QLineEdit(c);
        ide->setFixedWidth(110);
        row->addWidget(ide);
        idn = new QLabel(QString(), c);
        idn->setWordWrap(true);
        row->addWidget(idn, 1);
        g->addLayout(row, r, 1);
        ++r;
        g->addWidget(new QLabel(u"It happens to"_s, c), r, 0);
        kb = new QComboBox(c);
        kb->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        kb->setFixedWidth(250);
        kb->addItem(u"a nation"_s, u"country_event"_s);
        kb->addItem(u"a province"_s, u"province_event"_s);
        g->addWidget(kb, r, 1, Qt::AlignLeft);
        ++r;
    } else {
        g->addWidget(new QLabel(u"Its key"_s, c), r, 0);
        row = new QHBoxLayout();
        keyed = new QLineEdit(c);
        keyed->setPlaceholderText(u"letters, digits, _ — e.g. build_the_great_canal"_s);
        keyed->setFixedWidth(380);
        row->addWidget(keyed);
        idn = new QLabel(QString(), c);
        idn->setWordWrap(true);
        row->addWidget(idn, 1);
        g->addLayout(row, r, 1);
        ++r;
    }
    g->addWidget(new QLabel(u"Title"_s, c), r, 0);
    auto *te = new QLineEdit(c);
    te->setPlaceholderText(u"What the player reads at the top"_s);
    g->addWidget(te, r, 1);
    ++r;
    g->addWidget(new QLabel(u"What it tells"_s, c), r, 0, Qt::AlignTop);
    auto *de = new QPlainTextEdit(c);
    de->setFixedHeight(96);
    de->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    de->setPlaceholderText(u"The text of the %1"_s.arg(kind_));
    g->addWidget(de, r, 1);
    ++r;
    if (kind_ == u"event") {
        g->addWidget(new QLabel(u"Choices"_s, c), r, 0);
        ob = new QComboBox(c);
        ob->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        ob->setFixedWidth(250);
        for (int i = 1; i < 5; ++i)
            ob->addItem(u"%1 button%2"_s.arg(i).arg(i == 1 ? u""_s : u"s"_s), i);
        g->addWidget(ob, r, 1, Qt::AlignLeft);
        ++r;
        trig = new QCheckBox(u"It only happens when something calls it (another event, a decision)"_s, c);
        g->addWidget(trig, r, 1);
        ++r;
    }
    c->v->addLayout(g);
    auto *warn_ = new QLabel(QString(), c);
    warn_->setWordWrap(true);
    warn_->setStyleSheet(u"color: #ff786e; font-weight: 600;"_s);
    c->v->addWidget(warn_);
    auto *make = new QPushButton(u"Make it"_s, c);
    make->setObjectName(u"apply"_s);
    c->v->addWidget(make, 0, Qt::AlignLeft);
    page_lay->addWidget(c);
    page_lay->addStretch(1);
    // the ids / keys in use: {id: [(rel, …)]}
    auto used = std::make_shared<OrderedMap<QString, QStringList>>();
    if (kind_ == u"event") {
        for (const auto &[id, places] : eventtools::event_ids(mod, *st)) {
            QStringList rs;
            for (const auto &p : places)
                rs << std::get<0>(p);
            used->insert(id, rs);
        }
    } else {
        for (const auto &[key, places] : eventtools::decision_keys(mod, *st)) {
            QStringList rs;
            for (const auto &p : places)
                rs << p.first;
            used->insert(key, rs);
        }
    }
    auto file_rel = [fb, nf, folder]() -> OptStr {
        if (fb->currentData().isValid())
            return fb->currentData().toString();
        const OptStr fn = eventtools::safe_file(nf->text());
        return (fn && !fn->isEmpty()) ? OptStr(u"%1/%2"_s.arg(folder, *fn)) : std::nullopt;
    };
    auto check = [kind_, ide, keyed, idn, used]() -> bool {
        if (kind_ == u"event") {
            const QString t = py::strip(ide->text());
            const bool ok = py::isdigit(t) && !used->contains(t);
            idn->setText(ok ? u"free ✓"_s
                         : used->contains(t)
                             ? u"%1 is taken already (%2) — every event needs a number of its own."_s.arg(
                                   t, used->get(t)->value(0))
                             : u"a whole number"_s);
            idn->setStyleSheet(u"color: %1;"_s.arg(ok ? u"#7fd68a"_s : u"#ff786e"_s));
            return ok;
        }
        const QString t = py::strip(keyed->text());
        const bool ok = eventtools::key_ok(t) && !used->contains(t);
        idn->setText(t.isEmpty() ? QString()
                     : ok        ? u"free ✓"_s
                     : used->contains(t) ? u"there is a decision with this key already"_s
                                         : u"letters, digits and _ only"_s);
        idn->setStyleSheet(u"color: %1;"_s.arg(ok ? u"#7fd68a"_s : u"#ff786e"_s));
        return ok;
    };
    auto suggest = [this, kind_, fb, nf, ide, used, st, file_rel, check] {
        nf->setVisible(!fb->currentData().isValid());
        if (kind_ == u"event") {
            const OptStr rel_ = fb->currentData().isValid() ? OptStr(fb->currentData().toString()) : std::nullopt;
            QStringList near;
            if (rel_)
                for (const auto &[i, lst] : *used)
                    if (lst.contains(*rel_))
                        near << i;
            const OptStr got = reid::suggest(mod, *st, file_rel().value_or(QString()));
            QStringList keys;
            for (const auto &kv : *used)
                keys << kv.first;
            ide->setText(got ? *got : eventtools::free_id(keys, near));
            check();
        }
    };
    auto do_make = [this, kind_, file_rel, check, warn_, te, de, ide, ob, kb, trig, keyed, st] {
        const OptStr rel_ = file_rel();
        if (!rel_) {
            warn_->setText(u"Give the new file a name."_s);
            return;
        }
        if (!check()) {
            warn_->setText(u"The %1 isn't free yet — see above."_s.arg(kind_ == u"event" ? u"number"_s : u"key"_s));
            return;
        }
        const QString t = py::split(te->text()).join(u' ');
        const QString d = py::split(de->toPlainText()).join(u' ');
        if (t.isEmpty()) {
            warn_->setText(u"Give it a title — the player has to read something."_s);
            return;
        }
        warn_->setText(QString());
        QString old;
        try {
            old = st->text(*rel_);
        } catch (...) {
            old.clear();
        }
        QString text, id;
        std::vector<std::pair<QString, QString>> names;
        if (kind_ == u"event") {
            const QString eid = py::strip(ide->text());
            const int n_opt = ob->currentData().toInt();
            text = eventtools::add_event(old, eventtools::event_text(kb->currentData().toString(), eid, n_opt,
                                                                     trig->isChecked()));
            names = {{u"EVTNAME"_s + eid, t}, {u"EVTDESC"_s + eid, d.isEmpty() ? t : d}};
            for (int i = 0; i < n_opt; ++i)
                names.push_back({u"EVTOPT%1%2"_s.arg(QChar(u"ABCD"[i]), eid),
                                 n_opt == 1 ? u"OK"_s : u"Choice %1"_s.arg(QChar(u"ABCD"[i]))});
            id = eid;
        } else {
            id = py::strip(keyed->text());
            text = eventtools::add_decision(old, id);
            names = {{id + u"_title"_s, t}, {id + u"_desc"_s, d.isEmpty() ? t : d}};
        }
        Q_EMIT scriptEdit(u"New %1 %2"_s.arg(kind_, id), *rel_, text);
        for (const auto &[k, v] : names)
            Q_EMIT nameEdit(k, v);
        open_entry(*rel_, kind_, id);
        say(u"Made: %1 %2 in %3 — fill in its cards."_s.arg(kind_, id, *rel_));
    };
    connect(fb, &QComboBox::activated, this, [suggest](int) { suggest(); });
    if (ide)
        connect(ide, &QLineEdit::textChanged, this, [check](const QString &) { check(); });
    if (keyed)
        connect(keyed, &QLineEdit::textChanged, this, [check](const QString &) { check(); });
    connect(make, &QPushButton::clicked, this, [do_make] { do_make(); });
    if (rel_hint && files.contains(*rel_hint))
        fb->setCurrentIndex(fb->findData(*rel_hint));
    suggest();
}

}  // namespace blockbuilder
