// The Economy tab against Python, the C++ side of tools/check_prod.py.
// Usage: med_prod_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/production.h"
#include "core/pypath.h"
#include "map/mapview_data.h"
#include "ui/mapview.h"
#include "ui/prodpanel.h"
#include "ui/techpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;
using pyform::v;
using prodtree::Cid;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}

static constexpr int MAP_W = 1000, MAP_H = 700, SIDE_W = 470;

static QString key(const std::optional<QString> &k) { return k.value_or(u"None"_s); }
static QString nid(const Cid &n) { return n.kind + u'|' + n.key + (n.n ? u'|' + QString::number(n.n) : QString()); }
static QString nid(const std::optional<Cid> &n) { return n ? nid(*n) : u"None"_s; }
static QString rc(const QRectF &r) { return u"%1,%2,%3,%4"_s.arg(v(r.x()), v(r.y()), v(r.width()), v(r.height())); }

// Python's repr of a tuple of texts: ('prod', 'key')
static QString py_tuple(const QStringList &l)
{
    QStringList parts;
    for (const QString &s : l)
        parts << (s.contains(u'\'') && !s.contains(u'"') ? u'"' + s + u'"' : u'\'' + QString(s).replace(u"\\"_s, u"\\\\"_s).replace(u"'"_s, u"\\'"_s) + u'\'');
    return u'(' + parts.join(u", "_s) + (l.size() == 1 ? u",)"_s : u")"_s);
}

static QString data_str(const QVariant &d)
{
    if (!d.isValid())
        return u"None"_s;
    if (d.metaType().id() == QMetaType::QStringList)
        return py_tuple(d.toStringList());
    return d.toString();
}

static QList<QWidget *> kids_of(QWidget *w)
{
    QList<QWidget *> got;
    if (auto *sp = qobject_cast<QSplitter *>(w)) {
        for (int i = 0; i < sp->count(); ++i)
            got << sp->widget(i);
        return got;
    }
    if (auto *sa = qobject_cast<QScrollArea *>(w)) {
        if (sa->widget())
            got << sa->widget();
        return got;
    }
    if (auto *st = qobject_cast<QStackedWidget *>(w)) {
        if (st->currentWidget())
            got << st->currentWidget();
        return got;
    }
    std::function<void(QLayout *)> lay_walk = [&](QLayout *lay) {
        for (int i = 0; i < lay->count(); ++i) {
            QLayoutItem *it = lay->itemAt(i);
            if (it->widget())
                got << it->widget();
            else if (it->layout())
                lay_walk(it->layout());
        }
    };
    if (w->layout())
        lay_walk(w->layout());
    return got;
}

static QList<QWidget *> widgets_in(QWidget *w, QWidget *root)
{
    QList<QWidget *> res;
    for (QWidget *c : kids_of(w))
        if (c->isVisibleTo(root)) {
            res << c;
            res << widgets_in(c, root);
        }
    return res;
}

static QString btext(const QPushButton *b)      // (a WrapButton keeps its own text)
{
    if (const auto *wb = qobject_cast<const widgets::WrapButton *>(b))
        return wb->text();
    return b->text();
}

static std::optional<QString> desc(QWidget *w)
{
    const QString en = w->isEnabled() ? QString() : u" off"_s;
    if (auto *c = qobject_cast<QCheckBox *>(w))
        return u"K %1 %2%3"_s.arg(v(c->text()), v(c->isChecked()), en);
    if (auto *c = qobject_cast<QRadioButton *>(w))
        return u"R %1 %2%3"_s.arg(v(c->text()), v(c->isChecked()), en);
    if (auto *b = qobject_cast<QPushButton *>(w))
        return u"B %1 %2%3 | %4 | %5"_s.arg(v(btext(b)), v(b->isChecked()), en, v(b->toolTip()), v(b->styleSheet()));
    if (auto *l = qobject_cast<QLabel *>(w))
        return u"L %1 | %2 | %3"_s.arg(v(l->text()), v(l->styleSheet()), v(l->toolTip()));
    if (auto *e = qobject_cast<QLineEdit *>(w))
        return u"E %1 | %2 | %3 | %4%5"_s.arg(v(e->text()), v(e->placeholderText()), v(e->styleSheet()), v(e->toolTip()), en);
    if (auto *c = qobject_cast<QComboBox *>(w)) {
        QStringList items;
        for (int i = 0; i < c->count(); ++i)
            items << u"%1/%2"_s.arg(c->itemText(i), data_str(c->itemData(i)));
        return u"C %1 %2 %3 %4%5"_s.arg(QString::number(c->count()), QString::number(c->currentIndex()),
                                        v(c->currentText()), h(items.join(u'\n').toUtf8()), en);
    }
    if (auto *p = qobject_cast<QPlainTextEdit *>(w))
        return u"P %1 %2"_s.arg(h(p->toPlainText().toUtf8())).arg(p->height());
    if (auto *l = qobject_cast<QListWidget *>(w)) {
        QStringList items;
        for (int i = 0; i < l->count(); ++i)
            items << u"%1/%2"_s.arg(l->item(i)->text(), data_str(l->item(i)->data(Qt::UserRole)));
        return u"Q %1 %2 %3 %4"_s.arg(QString::number(l->count()), QString::number(l->currentRow()),
                                      h(items.join(u'\n').toUtf8()), QString::number(l->height()));
    }
    if (auto *s = qobject_cast<QSplitter *>(w)) {
        QStringList sz;
        for (int x : s->sizes())
            sz << QString::number(x);
        return u"S "_s + sz.join(u',');
    }
    return std::nullopt;
}

static int run(const QString &root, const QString &backups, const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    QStringList out;
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    auto m = std::make_shared<moddata::Mod>(root, BASE, std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    m->load();
    auto md = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mainwindow::MainWindow win(m, md, m->scenarios()[0].name, backups);
    win._check_timer->stop();
    win._rec_timer->stop();
    win.cl_ws->dir = QDir::toNativeSeparators(QDir(out_dir).filePath(u"changelog"_s));
    win.resize(1500, 900);
    win.show();
    win.side_scroll->setFixedWidth(SIDE_W);
    win.map->setFixedSize(MAP_W, MAP_H);
    QApplication::processEvents();
    win._check_timer->stop();
    win.map->fit();
    QApplication::processEvents();
    document::Document &d = *win.doc;
    auto &prod = d.map<production::ProdEdits>(u"production"_s);
    prodpanel::ProductionWorkspace *pw = win.prod_ws;
    prodpanel::ProdChart *ch = pw->chart;

    auto common = [&](const QString &tag) {
        QStringList a, b, c, e;
        for (const QString &k : prod.keys())
            a << u"%1=%2"_s.arg(k, prod.text(k));
        for (const QString &k : prod.buildings())
            b << u"%1=%2"_s.arg(k, prod.bld_text(k));
        for (const QString &k : prod.poptypes())
            c << u"%1=%2"_s.arg(k, prod.pop_text(k));
        for (const auto &[k, x] : prod.costs())
            e << u"%1=%2"_s.arg(k, v(x));
        out << u"%1 | tab %2 | msg %3 | %4 | steps %5 | prods %6 | blds %7 | pops %8 | costs %9"_s.arg(
            tag, win.tab, v(win.st_msg->text()), win.st_pending->text(), QString::number(d.undo_stack.size()),
            h(a.join(u'\n').toUtf8()), h(b.join(u'\n').toUtf8()), h(c.join(u'\n').toUtf8()), h(e.join(u';').toUtf8()));
    };
    auto sorted_ids = [](const auto &coll, auto f) {
        QStringList l;
        for (const auto &x : coll)
            l << f(x);
        std::sort(l.begin(), l.end());
        return l.join(u',');
    };
    auto pk_id = [](const std::pair<QString, QString> &p) { return p.first + u'|' + p.second; };
    auto chart_lines = [&](const QString &tag) {
        out << u"%1 H zoom %2 off %3,%4 | sel %5 | multi %6 | nodes %7 | meta %8 | parked %9"_s.arg(
                   tag, v(ch->zoom), v(ch->off.x()), v(ch->off.y()), nid(ch->sel),
                   sorted_ids(ch->multi, [](const Cid &x) { return nid(x); }), QString::number(ch->nodes.size()),
                   QString::number(ch->meta.size()), QString::number(ch->parked_at.size())) +
                   u" | area "_s + (ch->park_area ? rc(*ch->park_area) : u"None"_s);
        for (const auto &[n, r] : ch->nodes) {
            const prodpanel::NodeInfo *dd = ch->info.get(n);
            static const prodpanel::NodeInfo E;
            const prodpanel::NodeInfo &D = dd ? *dd : E;
            out << u"%1 N %2 %3 | %4 | %5 | %6 %7 %8 | %9"_s.arg(tag, nid(n), rc(r), v(D.name), h(D.tip.toUtf8()), D.look,
                                                                D.plus, v(D.open),
                                                                D.path ? D.path->join(u'/').replace(QChar(0x1f), u'|') : QString());
        }
        for (const auto &[n, r] : ch->parked_at)
            out << u"%1 A %2 %3"_s.arg(tag, nid(n), rc(r));
        for (size_t i = 0; i < ch->meta.size(); ++i) {
            const prodpanel::Meta &mm = ch->meta[i];
            const auto [r_, g_, b_] = mm.rgb;
            out << u"%1 M %2 %3 %4 %5 | %6 | %7 %8"_s.arg(
                tag, QString::number(i), key(mm.good), mm.trunk, u"%1,%2,%3"_s.arg(r_).arg(g_).arg(b_),
                h(mm.title.toUtf8()), QString::number(mm.members.size()), QString::number(mm.tgts.size()));
        }
        if (ch->lay) {
            QStringList ts, ps;
            for (const prodtree::Text &t : ch->lay->texts)
                ts << u"%1,%2,%3"_s.arg(v(t.x), v(t.y), t.text);
            for (const auto &[x, y, t] : ch->lay->pills)
                ps << u"%1,%2,%3"_s.arg(v(x), v(y), t);
            out << u"%1 T %2 | pills %3"_s.arg(tag, h(ts.join(u';').toUtf8()), h(ps.join(u';').toUtf8()));
        }
    };
    auto snap = [&](const QString &tag) {
        common(tag);
        QStringList trail, wi;
        for (const Cid &x : pw->ex_trail)
            trail << nid(x);
        for (const auto &[g, x] : pw->whatif)
            wi << u"%1=%2"_s.arg(g, v(x));
        out << u"%1 W sel %2 | trail %3 | parked %4 | whatif %5 | multi %6 | centre %7 | armed %8"_s.arg(
            tag, nid(pw->sel), trail.join(u','), sorted_ids(pw->parked, pk_id), wi.join(u';'),
            sorted_ids(pw->multi, pk_id), nid(pw->_centre_cid),
            pw->_eff_armed ? pw->_eff_armed->first.first + u'/' + pw->_eff_armed->first.second : u"None"_s);
        chart_lines(tag);
        for (QWidget *w : widgets_in(pw, pw)) {
            if (qobject_cast<prodpanel::ProdChart *>(w))
                continue;
            if (const auto dd = desc(w))
                out << tag + u' ' + *dd;
        }
    };
    auto send = [](QWidget *w, QEvent::Type kind, const QPointF &pos, Qt::MouseButton button, Qt::MouseButtons buttons,
                   Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QMouseEvent e(kind, pos, pos, button, buttons, mods);
        QApplication::sendEvent(w, &e);
    };
    auto click_at = [&](QWidget *w, const QPointF &p, Qt::MouseButton button = Qt::LeftButton,
                        Qt::KeyboardModifiers mods = Qt::NoModifier) {
        send(w, QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton);
        send(w, QEvent::MouseButtonPress, p, button, button, mods);
        send(w, QEvent::MouseButtonRelease, p, button, Qt::NoButton, mods);
        QApplication::processEvents();
    };
    auto screen = [&](const QRectF &r) {
        const QPointF c = r.center();
        return QPointF(c.x() * ch->zoom + ch->off.x(), c.y() * ch->zoom + ch->off.y());
    };
    auto grab = [&](const QString &name) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 400)
            QApplication::processEvents();
        pw->grab().save(QDir(out_dir).filePath(name));
    };
    auto found = [&](auto *cls_tag, const std::function<bool(QWidget *)> &pred) {
        using T = std::remove_pointer_t<decltype(cls_tag)>;
        QList<T *> res;
        for (QWidget *w : widgets_in(pw->side, pw->side))
            if (auto *x = qobject_cast<T *>(w))
                if (!pred || pred(w))
                    res << x;
        return res;
    };
    auto pe = static_cast<QPushButton *>(nullptr);
    auto le = static_cast<QLineEdit *>(nullptr);
    auto co = static_cast<QComboBox *>(nullptr);
    auto edit = [](QLineEdit *w, const QString &text) {
        w->setText(text);
        Q_EMIT w->editingFinished();
        QApplication::processEvents();
    };
    auto first_ph = [&](const QString &ph) -> QLineEdit * {
        const auto es = found(le, [ph](QWidget *w) { return static_cast<QLineEdit *>(w)->placeholderText() == ph; });
        return es.isEmpty() ? nullptr : es[0];
    };
    auto by_tip = [&](const std::function<bool(const QString &)> &f) -> QLineEdit * {
        const auto fs = found(static_cast<techpanel::ValueField *>(nullptr), [f](QWidget *w) { return f(w->toolTip()); });
        return fs.isEmpty() ? nullptr : static_cast<QLineEdit *>(fs[0]->w);   // (a ValueField by its tooltip: its line edit)
    };
    auto pick_box = [&](const QString &prefix, int i) {
        const auto boxes = found(co, [prefix](QWidget *w) {
            auto *c = static_cast<QComboBox *>(w);
            return c->lineEdit() && c->lineEdit()->placeholderText().startsWith(prefix);
        });
        if (!boxes.isEmpty() && boxes[0]->count()) {
            QComboBox *b = boxes[0];
            b->setCurrentIndex(std::min(i, b->count() - 1));
            Q_EMIT b->activated(b->currentIndex());
            QApplication::processEvents();
        }
    };
    auto button = [&](const std::function<bool(const QString &)> &f) {
        const auto bs = found(pe, [f](QWidget *w) { return f(btext(static_cast<QPushButton *>(w))); });
        if (!bs.isEmpty()) {
            bs[0]->click();
            QApplication::processEvents();
        }
        return !bs.isEmpty();
    };
    auto node_of = [&](const QString &kind, const std::optional<QString> &k) -> std::optional<Cid> {
        for (const auto &[n, _r] : ch->nodes)
            if (n.kind == kind && (!k || n.key == *k))
                return n;
        return std::nullopt;
    };

    // --- ⌂ Everything
    win.set_tab(u"production"_s);
    QApplication::processEvents();
    snap(u"economy"_s);
    grab(u"prod_all.png"_s);
    QStringList keys = prod.keys();
    std::sort(keys.begin(), keys.end());
    QHash<QString, production::Desc> info;
    for (const QString &k : keys)
        info.insert(k, prod.info(k));
    QString fac;
    for (const QString &k : keys)
        if (info[k].kind == u"factory" && info[k].inputs.size() >= 2 && !info[k].bonuses.empty()) {
            fac = k;
            break;
        }
    if (fac.isEmpty()) {
        fac = keys.value(0);
        for (const QString &k : keys)
            if (info[k].kind == u"factory") {
                fac = k;
                break;
            }
    }
    if (const auto n = node_of(u"prod"_s, fac))
        click_at(ch, screen(*ch->nodes.get(*n)));
    snap(u"card clicked"_s);
    // --- a factory explored, a ＋ unfolded
    pw->explore({u"prod"_s, fac, 0});
    QApplication::processEvents();
    snap(u"explore"_s);
    grab(u"prod_factory.png"_s);
    for (const auto &[n, r] : ch->nodes) {
        const prodpanel::NodeInfo *dd = ch->info.get(n);
        if (dd && !dd->plus.isEmpty()) {
            if (const auto b = ch->_plus_box(n, r))
                click_at(ch, screen(*b));
            break;
        }
    }
    snap(u"unfolded"_s);
    // --- its good: the real price
    const QString good = info[fac].output && !info[fac].output->isEmpty() ? *info[fac].output : m->goods.begin()->first;
    pw->explore({u"good"_s, good, 0});
    QApplication::processEvents();
    snap(u"good"_s);
    grab(u"prod_good.png"_s);
    if (QLineEdit *e = by_tip([](const QString &t) { return t == u"The real price in goods.txt (written on Apply)"; }))
        edit(e, u"3.5"_s);
    snap(u"price"_s);
    // --- back to the factory: what-if, amounts, inputs, boosts
    pw->explore({u"prod"_s, fac, 0});
    QApplication::processEvents();
    if (QLineEdit *e = by_tip([](const QString &t) { return t.startsWith(u"What-if price of"_s); }))
        edit(e, u"9"_s);
    snap(u"whatif"_s);
    button([](const QString &t) { return t == u"Reset"; });
    snap(u"reset"_s);
    if (QLineEdit *e = first_ph(u"e.g. 1"_s))
        edit(e, u"2.5"_s);
    snap(u"amount"_s);
    pick_box(u"＋ Add an input good"_s, 2);
    snap(u"input added"_s);
    if (const auto xs = found(pe, [](QWidget *w) { return static_cast<QPushButton *>(w)->text() == u"✕"; }); !xs.isEmpty()) {
        xs.back()->click();
        QApplication::processEvents();
    }
    snap(u"input off"_s);
    button([](const QString &t) { return t == u"＋ Add a boost"; });
    snap(u"boost added"_s);
    if (const auto es = found(le, [](QWidget *w) { return static_cast<QLineEdit *>(w)->placeholderText() == u"e.g. 0.25"; });
        !es.isEmpty())
        edit(es.back(), u"0.4"_s);
    snap(u"boost value"_s);
    // --- its efficiency goods
    for (int i = 0; i < 2; ++i) {
        button([&](const QString &t) {
            return t == u"Give only this one its own list" || t.startsWith(u"Click again: %1 gets"_s.arg(fac));
        });
        snap(u"own list"_s);
    }
    for (int i = 0; i < 2; ++i) {
        button([](const QString &t) {
            return t == u"Back to the template's list" || t.startsWith(u"Click again: drop"_s);
        });
        snap(u"back to template"_s);
    }
    for (int i = 0; i < 2; ++i) {
        button([](const QString &t) { return t.startsWith(u"Edit the template "_s) || t.startsWith(u"Click again: edit "_s); });
        snap(u"template"_s);
    }
    grab(u"prod_template.png"_s);
    if (QLineEdit *e = first_ph(u"e.g. 1"_s))
        edit(e, u"0.75"_s);
    snap(u"template amount"_s);
    // --- its building
    if (const auto bld = prod.building_of(fac); bld && !bld->isEmpty()) {
        pw->explore({u"bld"_s, *bld, 0});
        QApplication::processEvents();
    }
    snap(u"building"_s);
    if (QLineEdit *e = first_ph(u"e.g. 365"_s))
        edit(e, u"400"_s);
    snap(u"build time"_s);
    // --- a pop type's needs
    std::optional<QString> pt;
    for (const QString &p : prod.poptypes()) {
        bool any = false;
        for (const auto &[_k, g] : prod.needs(p))
            any = any || !g.empty();
        if (any) {
            pt = p;
            break;
        }
    }
    if (pt) {
        pw->explore({u"need"_s, *pt, 0});
        QApplication::processEvents();
    }
    snap(u"needs"_s);
    grab(u"prod_needs.png"_s);
    if (QLineEdit *e = first_ph(u"e.g. 1"_s))
        edit(e, u"1.5"_s);
    snap(u"need amount"_s);
    pick_box(u"＋ Add a good"_s, 1);
    snap(u"need added"_s);
    // --- park one, put it back
    pw->explore({u"prod"_s, fac, 0});
    QApplication::processEvents();
    for (const auto &[n, r] : ch->nodes)
        if (n.kind == u"prod" && n.key != fac) {
            click_at(ch, screen(r), Qt::RightButton);
            break;
        }
    snap(u"parked"_s);
    grab(u"prod_parked.png"_s);
    if (!ch->parked_at.isEmpty())
        click_at(ch, screen(ch->parked_at.begin()->second));
    snap(u"unparked"_s);
    // --- the search
    {
        const QString t = u"Good: "_s + pw->gname(good);
        pw->search->setText(t);
        pw->_search_pick(t);
        QApplication::processEvents();
        QApplication::processEvents();
    }
    snap(u"searched"_s);
    // --- Ctrl+click two cards
    pw->explore({u"prod"_s, fac, 0});
    QApplication::processEvents();
    {
        std::vector<QRectF> two;
        for (const auto &[n, r] : ch->nodes)
            if (n.kind == u"prod" && two.size() < 2)
                two.push_back(r);
        for (const QRectF &r : two)
            click_at(ch, screen(r), Qt::LeftButton, Qt::ControlModifier);
    }
    snap(u"multi"_s);
    // --- the filters
    (*pw->show_kind.get(u"rgo"_s))->click();
    QApplication::processEvents();
    snap(u"no rgo"_s);
    (*pw->show_kind.get(u"rgo"_s))->click();
    pw->money->click();
    QApplication::processEvents();
    snap(u"no money"_s);
    pw->money->click();
    pw->goods_between->click();
    QApplication::processEvents();
    snap(u"no goods between"_s);
    pw->goods_between->click();
    QApplication::processEvents();
    // --- a new production
    pw->new_btn->click();
    QApplication::processEvents();
    snap(u"new form"_s);
    pw->new_key->setText(u"bad key!"_s);
    button([](const QString &t) { return t == u"Create"; });
    snap(u"new wrong"_s);
    {
        const int i = pw->new_out->findData(good);
        pw->new_out->setCurrentIndex(i >= 0 ? i : 0);
        QApplication::processEvents();
    }
    button([](const QString &t) { return t == u"Create"; });
    snap(u"created"_s);
    // --- deleted
    button([](const QString &t) { return t == u"Delete this production"; });
    snap(u"deleted"_s);
    // --- the main window's undo
    for (int i = 0; i < 3; ++i) {
        out << u"undo "_s + v(win.undo());
        QApplication::processEvents();
    }
    snap(u"undone"_s);
    // --- G4
    {
        const bool ok = win.save(false);
        out << u"apply %1 | msg %2 | %3"_s.arg(v(ok), v(win.st_msg->text()), win.st_pending->text());
    }
    QApplication::processEvents();
    snap(u"applied"_s);
    QFile f(QDir(out_dir).filePath(u"lines.txt"_s));
    if (!f.open(QIODevice::WriteOnly))
        return 3;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    f.close();
    std::printf("%d lines\n", int(out.size()));
    std::fflush(stdout);
    win._rec_timer->stop();
    std::_Exit(0);                                 // (no close: nothing to keep — and no recovery file)
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 6 && a[1] == u"--run")
            return run(a[2], a[3], a[4], a[5]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
