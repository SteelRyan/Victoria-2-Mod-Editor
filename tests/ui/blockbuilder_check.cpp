// Scripts → 🧩 Blocks against Python, the C++ side of tools/check_blockbuilder.py.
// Usage: med_blockbuilder_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/blockcode.h"
#include "core/document.h"
#include "core/events.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "map/mapview_data.h"
#include "ui/blockbuilder.h"
#include "ui/eventpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QScrollBar>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::v;
using namespace blockbuilder;
namespace bc = blockcode;

static constexpr int W = 1760, H = 1020;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
static QString f(double x) { return v(x); }
static QString rect(const QRectF &r) { return u"%1,%2,%3,%4"_s.arg(f(r.x()), f(r.y()), f(r.width()), f(r.height())); }
[[maybe_unused]] static QString ns(const std::optional<QString> &s) { return s ? *s : u"None"_s; }
static QString vi(const std::vector<int> &xs)
{
    std::vector<long long> l(xs.begin(), xs.end());
    return v(l);
}
template <class T> static std::vector<T *> kids(QWidget *w)
{
    std::vector<T *> out;
    for (QWidget *c : w->findChildren<QWidget *>())
        if (auto *x = dynamic_cast<T *>(c))
            out.push_back(x);
    return out;
}

static QString wtext(QWidget *w)
{
    if (auto *c = qobject_cast<LazyCombo *>(w))
        return u"C:%1:%2:%3"_s.arg(c->currentText()).arg(c->width()).arg(c->value);
    if (auto *e = qobject_cast<ValueEdit *>(w))
        return u"V:%1:%2:%3:%4"_s.arg(e->text()).arg(e->width()).arg(e->vtype, e->styleSheet());
    if (auto *e = qobject_cast<QLineEdit *>(w))
        return u"E:%1:%2"_s.arg(e->text()).arg(e->width());
    if (auto *l = qobject_cast<QLabel *>(w))
        return u"L:"_s + l->text();
    return u"?"_s;
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
    win.resize(1900, 1000);
    win.show();
    QApplication::processEvents();
    win._check_timer->stop();
    QObject::disconnect(win._check_timer, &QTimer::timeout, nullptr, nullptr);
    document::Document &d = *win.doc;
    eventpanel::EventPanel *ep = win.event_panel;
    auto &store = d.map<scriptstore::ScriptStore>(u"scripts"_s);

    auto wait = [&] {
        QElapsedTimer t;
        t.start();
        while (ep->working() && t.elapsed() < 300000)
            QApplication::processEvents();
        QApplication::processEvents();
    };
    auto settle = [&](int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms)
            QApplication::processEvents();
        wait();
    };
    auto snap = [&](const QString &tag, BlockBuilder *B, bool all_blocks = true) {
        out << u"%1 | title %2 | sub %3 | win %4 | steps %5 | msg %6"_s.arg(
            tag, v(B->title->text()), v(B->sub->text()), v(B->windowTitle()), QString::number(d.undo_stack.size()),
            v(win.st_msg->text()));
        const QString wt = B->warn ? u"%1 %2"_s.arg(B->warn->isVisibleTo(B) ? 1 : 0).arg(v(B->warn->text())) : u"gone"_s;
        out << u"  warn %1 | status %2 %3 | help %4"_s.arg(wt, v(B->status->text()), v(B->status->styleSheet()),
                                                          h(B->help->text().toUtf8()));
        const QString tg = B->target ? B->target->ctx.name() : u"None"_s;
        const auto path = B->_path(B->sel.get());
        QString pv = u"None"_s;
        if (path) {
            std::vector<long long> l(path->begin(), path->end());
            pv = v(l);
        }
        out << u"  entry %1 %2 %3 | depth %4 | lazy %5 | sel %6 | target %7 | preview %8 | spans %9"_s.arg(
                   B->rel ? v(*B->rel) : u"None"_s, B->kind ? v(*B->kind) : u"None"_s, v(B->ident),
                   QString::number(B->depth), QString::number(B->lazy ? 1 : 0), pv, tg,
                   h(B->preview->toPlainText().toUtf8()), QString::number(B->_spans.size())) +
               u" | lit %1"_s.arg(B->preview->extraSelections().size());
        out << u"  text %1"_s.arg(B->rel ? h(store.text(*B->rel).toUtf8()) : u"None"_s);
        BlockList *lst = B->lists.value(B->_mode, nullptr);
        long long vis = 0;
        if (lst)
            for (auto &[s, pb] : lst->rows)
                vis += pb->isVisibleTo(lst) ? 1 : 0;
        out << u"  palette %1 | lists %2 | rows %3 visible %4 | chips %5 | find %6"_s.arg(
            B->_mode, QString::number(B->lists.size()), QString::number(lst ? qsizetype(lst->rows.size()) : -1),
            QString::number(lst ? vis : -1), QString::number(B->chip_row->count()), v(B->find->text()));
        const auto blocks = kids<BlockW>(B->page);
        QStringList lines;
        for (size_t i = 0; i < blocks.size(); ++i) {
            BlockW *bw = blocks[i];
            QStringList row;
            for (int j = 0; j < bw->row->count(); ++j)
                row << wtext(bw->row->itemAt(j)->widget());
            lines << u"%1 %2 | k %3 | spec %4 | wrong %5 | t %6 %7 | b %8 %9"_s.arg(
                         QString::number(i), bw->kind, v(bw->it->k), bw->spec ? bw->spec->key : u"None"_s,
                         QString::number(bw->wrong ? 1 : 0), vi(bw->tops), bw->top_mode, vi(bw->bottoms), bw->bottom_mode) +
                     u" | in %1 | %2 | head %3 | %4"_s.arg(
                         bw->inner ? vi(bw->inner->first) + u" "_s + bw->inner->second : u"None"_s,
                         rect(QRectF(bw->geometry())), rect(QRectF(bw->head->geometry())), row.join(u'/'));
        }
        const auto conts = kids<ContainerW>(B->page);
        QStringList clines;
        for (size_t i = 0; i < conts.size(); ++i) {
            ContainerW *c = conts[i];
            clines << u"%1 %2 %3 %4 | blocks %5 | items %6 | shown %7 | %8 | top %9"_s.arg(
                QString::number(i), c->ctx.mode, c->ctx.scope, QString::number(c->ctx.is_list ? 1 : 0),
                QString::number(c->blocks.size()), QString::number(c->items().size()), QString::number(c->shown().size()),
                rect(QRectF(c->geometry())), QString::number(c->top ? 1 : 0));
        }
        out << u"  blocks %1 %2 | stacks %3 %4 | page %5"_s.arg(
            QString::number(blocks.size()), h(lines.join(u'\n').toUtf8()), QString::number(conts.size()),
            h(clines.join(u'\n').toUtf8()), rect(QRectF(B->page->geometry())));
        for (int i = 0; i < lines.size() && (all_blocks || i < 30); ++i)
            out << u"   "_s + lines[i];
        for (int i = 0; i < clines.size() && (all_blocks || i < 20); ++i)
            out << u"   s"_s + clines[i];
    };
    auto grab = [&](QWidget *w, const QString &name) {
        settle(400);
        w->grab().save(QDir(out_dir).filePath(name));
    };
    auto lines_of = [&](const events::Entry &e) {
        return py::splitlines(py::slice(store.text(e.rel.value_or(QString())), e.span.first, e.span.second)).size();
    };

    win.set_tab(u"events"_s);
    QApplication::processEvents();
    wait();
    events::Entry *ev = nullptr;
    for (const auto &x : ep->db->entries)
        if (x->is_event() && x->options.size() >= 2 && x->kind == u"country_event" && lines_of(*x) < 80) {
            ev = x.get();
            break;
        }
    const QString ev_rel = ev->rel.value_or(QString()), ev_id = ev->id.value_or(u"None"_s);
    out << u"event "_s + ev->uid.value_or(u"None"_s);
    ep->select(ev);
    QApplication::processEvents();
    wait();
    win.open_blocks();
    BlockBuilder *B = win.block_win;
    B->resize(W, H);
    settle(500);
    snap(u"opened"_s, B);
    grab(B, u"blocks_event.png"_s);
    // what the help says
    {
        std::vector<const bc::Spec *> specs;
        auto take = [&](const std::vector<bc::Spec> &all, int n, const std::function<bool(const bc::Spec &)> &ok) {
            int k = 0;
            for (const bc::Spec &s : all)
                if (k < n && ok(s)) {
                    specs.push_back(&s);
                    ++k;
                }
        };
        auto any = [](const bc::Spec &) { return true; };
        take(bc::COND(), 3, any);
        take(bc::EFF(), 3, any);
        take(bc::EFF(), 2, [](const bc::Spec &s) { return s.vtype == std::optional<QString>(u"block"_s); });
        take(bc::GOAL(), 1, any);
        take(bc::EFF(), 2, [](const bc::Spec &s) { return s.fields && !s.fields->empty(); });
        take(bc::COND(), 1, [](const bc::Spec &s) { return s.keytype.has_value(); });
        for (const bc::Spec *s : specs) {
            B->explain(nullptr, s, nullptr);
            out << u"help %1 | %2"_s.arg(s->key, v(B->help->text()));
        }
        B->explain(nullptr, &WEIGHT(), nullptr);
        out << u"help weight | "_s + v(B->help->text());
        const PPtr odd = bc::pitem(u"odd_line"_s, u"1"_s);
        B->explain(odd.get(), nullptr, nullptr);
        out << u"help raw | "_s + v(B->help->text());
    }
    // the palette
    B->_set_mode(u"eff"_s);
    settle(200);
    snap(u"effects"_s, B, false);
    B->find->setText(u"prestige"_s);
    settle(200);
    snap(u"searched"_s, B, false);
    {
        BlockList *lst = B->lists.value(u"eff"_s);
        for (size_t i = 0; i < lst->rows.size() && i < 12; ++i) {
            const auto &[s, pb] = lst->rows[i];
            out << u"  pb %1 %2 %3 %4 | %5"_s.arg(s->key).arg(pb->width()).arg(pb->height()).arg(pb->hh).arg(
                v(pb->flow.chunks));
        }
        const bc::Spec *spec = lst->apply_filter(u"prestige"_s);
        out << u"first "_s + (spec ? spec->key : u"None"_s);
        B->add_from_palette(spec);
    }
    settle(300);
    snap(u"added"_s, B);
    {
        QKeyEvent ke(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QApplication::sendEvent(B, &ke);
    }
    settle(300);
    snap(u"deleted"_s, B);
    B->find->setText(QString());
    B->_set_mode(u"cond"_s);
    settle(200);
    // a note
    kids<ContainerW>(B->page)[0]->add_note();
    settle(200);
    {
        BlockW *nb = nullptr;
        for (BlockW *bw : kids<BlockW>(B->page))
            if (bw->kind == u"note") {
                nb = bw;
                break;
            }
        nb->note_edit->setText(u"hello — a “test” … ok"_s);
        Q_EMIT nb->note_edit->editingFinished();
    }
    settle(300);
    snap(u"note"_s, B);
    // a value typed
    {
        ValueEdit *ve = nullptr;
        for (BlockW *bw : kids<BlockW>(B->page)) {
            for (ValueEdit *w : bw->findChildren<ValueEdit *>())
                if (w->vtype == u"num" || w->vtype == u"int") {
                    ve = w;
                    break;
                }
            if (ve)
                break;
        }
        if (ve) {
            QPointer<ValueEdit> pv(ve);
            ve->setText(u"x"_s);
            Q_EMIT ve->editingFinished();
            settle(200);
            snap(u"value wrong"_s, B, false);
            if (pv) {
                pv->setText(u"3"_s);
                Q_EMIT pv->editingFinished();
            }
            settle(300);
            snap(u"value typed"_s, B);
        } else {
            out << u"no value"_s;
        }
    }
    // a block moved inside its stack
    {
        ContainerW *c = nullptr;
        for (ContainerW *x : kids<ContainerW>(B->page))
            if (x->blocks.size() >= 2) {
                c = x;
                break;
            }
        if (c) {
            const PPtr it = c->blocks.back()->it;
            const Dragging mv{u"move"_s, nullptr, it, c};
            B->dragging.reset();
            B->finish_drop(c, 0, nullptr, mv);
            settle(300);
            snap(u"moved"_s, B);
        } else {
            out << u"no stack of two"_s;
        }
    }
    // why_not
    {
        const auto conts = kids<ContainerW>(B->page);
        std::vector<const bc::Spec *> pick;
        auto first_of = [&](const std::vector<bc::Spec> &all, const std::function<bool(const bc::Spec &)> &ok) {
            for (const bc::Spec &s : all)
                if (ok(s)) {
                    pick.push_back(&s);
                    return;
                }
        };
        first_of(bc::EFF(), [](const bc::Spec &s) { return s.key == u"prestige"; });
        for (size_t i = 0; i < 2 && i < bc::COND().size(); ++i)
            pick.push_back(&bc::COND()[i]);
        first_of(bc::COND(), [](const bc::Spec &s) { return s.scopes == u"p"; });
        first_of(bc::EFF(), [](const bc::Spec &s) { return s.scopes == u"s"; });
        if (!bc::GOAL().empty())
            pick.push_back(&bc::GOAL()[0]);
        for (size_t i = 0; i < conts.size() && i < 10; ++i)
            for (const bc::Spec *s : pick) {
                const auto why = B->why_not(conts[i], s);
                out << u"why %1 %2 %3 | %4"_s.arg(QString::number(i), conts[i]->ctx.name(), s->key,
                                                   why ? v(*why) : u"None"_s);
            }
        B->dragging.reset();
        const auto why = B->why_not(conts[0]);
        out << u"why nothing | "_s + (why ? v(*why) : u"None"_s);
    }
    // how often: the unit
    {
        Factor *fac = nullptr;
        for (Factor *x : kids<Factor>(B->page))
            if (x->base_key == u"time") {
                fac = x;
                break;
            }
        if (fac) {
            QComboBox *ub = fac->findChildren<QComboBox *>()[0];
            ub->setCurrentIndex(ub->findData(u"days"_s));
            Q_EMIT ub->activated(ub->currentIndex());
            settle(300);
            snap(u"days"_s, B);
        }
    }
    // only when called
    auto trig_box = [&] {
        for (QCheckBox *x : B->page->findChildren<QCheckBox *>())
            if (x->text() == u"Only when something calls it")
                return x;
        return static_cast<QCheckBox *>(nullptr);
    };
    {
        QCheckBox *cb = trig_box();
        cb->setChecked(!cb->isChecked());
    }
    settle(400);
    snap(u"triggered"_s, B);
    {
        QCheckBox *cb = trig_box();
        cb->setChecked(!cb->isChecked());
    }
    settle(400);
    snap(u"untriggered"_s, B);
    // ＋ Add a choice
    B->_add_option(B->root);
    settle(400);
    // (its 60 ms "scroll to the end" races the new layout: Python's slower _build lands on the OLD end — timing, not
    //  the port; both checks put it at the new end)
    B->scroll->verticalScrollBar()->setValue(B->scroll->verticalScrollBar()->maximum());
    settle(100);
    snap(u"choice added"_s, B);
    // the id
    {
        QLineEdit *ide = nullptr;
        for (QLineEdit *x : B->page->findChildren<QLineEdit *>())
            if (x->text() == B->ident && x->width() == 110) {
                ide = x;
                break;
            }
        ide->setText(u"abc"_s);
        B->_set_id(ide);
        settle(200);
        snap(u"id not a number"_s, B, false);
        QString other;
        for (const auto &x : ep->db->entries)
            if (x->is_event() && x->id.value_or(u"None"_s) != B->ident) {
                other = x->id.value_or(u"None"_s);
                break;
            }
        ide->setText(other);
        B->_set_id(ide);
        settle(200);
        snap(u"id taken"_s, B, false);
        ide->setText(u"987654"_s);
        B->_set_id(ide);
    }
    settle(300);
    snap(u"id free"_s, B);
    win.undo();
    settle(300);
    win.undo();
    settle(400);
    snap(u"undone"_s, B);
    // a card's ＋ (the event opened again: after the undo its id is the old one)
    B->open_entry(ev_rel, u"event"_s, ev_id);
    settle(300);
    snap(u"reopened"_s, B, false);
    {
        ContainerW *c = kids<ContainerW>(B->page)[0];
        QPushButton *btn = nullptr;
        for (QPushButton *x : c->findChildren<QPushButton *>())
            if (x->text().startsWith(u"＋ "_s) && x->text() != u"＋ note") {
                btn = x;
                break;
            }
        B->open_add(c, btn);
        settle(200);
        QFrame *pop = B->_pop;
        BlockList *pl = kids<BlockList>(pop)[0];
        out << u"pop %1 %2 | rows %3"_s.arg(QString::number(pop->isVisible() ? 1 : 0), rect(QRectF(pop->geometry())),
                                            QString::number(pl->rows.size()));
        QLineEdit *fe = pop->findChildren<QLineEdit *>()[0];
        fe->setText(u"flag"_s);
        settle(100);
        long long vis = 0;
        for (auto &[s, pb] : pl->rows)
            vis += pb->isVisibleTo(pl) ? 1 : 0;
        out << u"pop searched "_s + QString::number(vis);
        Q_EMIT fe->returnPressed();
    }
    settle(300);
    snap(u"pop taken"_s, B);
    // a decision
    {
        events::Entry *dec = nullptr;
        for (const auto &x : ep->db->entries)
            if (!x->is_event()) {
                dec = x.get();
                break;
            }
        ep->select(dec);
    }
    settle(200);
    win.open_blocks();
    settle(400);
    snap(u"decision"_s, B);
    grab(B, u"blocks_decision.png"_s);
    // the largest event: folded
    {
        events::Entry *big = nullptr;
        for (const auto &x : ep->db->entries)
            if (x->is_event() && (!big || x->span.second - x->span.first > big->span.second - big->span.first))
                big = x.get();
        ep->select(big);
    }
    settle(200);
    win.open_blocks();
    settle(500);
    snap(u"large"_s, B, false);
    {
        std::vector<QPushButton *> stubs;
        for (QPushButton *b : B->page->findChildren<QPushButton *>())
            if (b->text().contains(u"inside — click to open"_s))
                stubs.push_back(b);
        QStringList st;
        for (size_t i = 0; i < stubs.size() && i < 6; ++i)
            st << stubs[i]->text();
        out << u"stubs %1 %2"_s.arg(QString::number(stubs.size()), v(st));
        if (!stubs.empty()) {
            QPushButton *best = nullptr;
            long long most = -1;
            for (QPushButton *b : stubs) {
                const long long n = py::split(b->text())[1].toLongLong();
                if (n > most) {
                    most = n;
                    best = b;
                }
            }
            best->click();
            settle(300);
            snap(u"large opened"_s, B, false);
            std::vector<QPushButton *> more;
            for (QPushButton *b : B->page->findChildren<QPushButton *>())
                if (b->text().startsWith(u"▸  the next"_s))
                    more.push_back(b);
            QStringList mt;
            for (size_t i = 0; i < more.size() && i < 4; ++i)
                mt << more[i]->text();
            out << u"more "_s + v(mt);
            if (!more.empty()) {
                more[0]->click();
                settle(300);
                snap(u"large more"_s, B, false);
            }
        }
    }
    // a new event, a new decision
    B->show_new(u"event"_s);
    settle(300);
    snap(u"new event form"_s, B, false);
    grab(B, u"blocks_new.png"_s);
    auto make_btn = [&] {
        for (QPushButton *x : B->page->findChildren<QPushButton *>())
            if (x->text() == u"Make it")
                return x;
        return static_cast<QPushButton *>(nullptr);
    };
    {
        const QList<QLineEdit *> les = B->page->findChildren<QLineEdit *>();
        QStringList texts;
        for (QLineEdit *x : les)
            texts << x->text();
        out << u"form "_s + v(texts);
        for (QLineEdit *x : les)
            if (x->placeholderText() == u"What the player reads at the top") {
                x->setText(u"My test event"_s);
                break;
            }
        B->page->findChildren<QPlainTextEdit *>()[0]->setPlainText(u"Some  words\nhere"_s);
        for (QComboBox *x : B->page->findChildren<QComboBox *>())
            if (x->itemText(0) == u"1 button") {
                x->setCurrentIndex(1);
                break;
            }
        make_btn()->click();
    }
    settle(500);
    snap(u"new event"_s, B);
    B->show_new(u"decision"_s);
    settle(300);
    {
        const QList<QLineEdit *> les = B->page->findChildren<QLineEdit *>();
        for (QLineEdit *x : les)
            if (x->placeholderText().startsWith(u"letters, digits"_s)) {
                x->setText(u"my_test_decision"_s);
                break;
            }
        for (QLineEdit *x : les)
            if (x->placeholderText() == u"What the player reads at the top") {
                x->setText(u"A decision"_s);
                break;
            }
        make_btn()->click();
    }
    settle(500);
    snap(u"new decision"_s, B);
    win._refresh_windows();
    settle(300);
    snap(u"main refresh"_s, B, false);
    QFile fo(QDir(out_dir).filePath(u"lines.txt"_s));
    if (!fo.open(QIODevice::WriteOnly))
        return 3;
    fo.write((out.join(u'\n') + u'\n').toUtf8());
    fo.close();
    std::printf("%d lines\n", int(out.size()));
    std::fflush(stdout);
    win._rec_timer->stop();
    std::_Exit(0);                                 // (no close: nothing to keep — and no recovery file)
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    QApplication::setCursorFlashTime(0);
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
