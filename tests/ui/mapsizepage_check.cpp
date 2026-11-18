// mapsize's page half against Python, the C++ side of tools/check_mapsizepage.py.
// Usage: med_mapsizepage_check --dump <out folder> <work folder>   (MODEDITOR_SETTINGS names the settings file)
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "map/moreviews_data.h"
#include "ui/mapsize.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <cstdio>
#include <stdexcept>

using namespace Qt::StringLiterals;
using pyform::v;
using namespace mapsize;

static QString hs(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
static QString img_h(const QImage &img)
{
    const QImage im = img.convertToFormat(QImage::Format_RGBA8888);
    QByteArray rows;
    for (int y = 0; y < im.height(); ++y)
        rows.append(reinterpret_cast<const char *>(im.constScanLine(y)), im.width() * 4);
    return u"%1x%2 %3"_s.arg(im.width()).arg(im.height()).arg(hs(rows));
}

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    QStringList out;
    auto m = std::make_shared<moddata::Mod>(pypath::join(work, u"Ferrum_Mare 1.2.000"_s), BASE, std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    m->load();
    auto mdp = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mapview::MapData &md = *mdp;
    mainwindow::MainWindow win(m, mdp, m->scenarios()[0].name, pypath::join(out_dir, u"backups"_s));
    win._check_timer->stop();
    win._rec_timer->stop();
    document::Document *doc = win.doc.get();

    auto P = std::make_unique<MapSizeWorkspace>(*m, md);
    QStringList msgs, edits;
    QString how = u"false"_s;
    QObject::connect(P.get(), &MapSizeWorkspace::message, [&](const QString &t) { msgs << t; });
    QObject::connect(P.get(), &MapSizeWorkspace::reloadRequested, [&] { msgs << u"RELOAD"_s; });
    P->backup_root = QDir(out_dir).filePath(u"never"_s);
    P->before_cut = [&](const std::vector<long long> &pids) -> bool {
        QStringList f;
        for (size_t i = 0; i < pids.size() && i < 12; ++i)
            f << QString::number(pids[i]);
        edits << u"before_cut %1 %2"_s.arg(pids.size()).arg(f.join(u','));
        if (how == u"raise")
            throw std::runtime_error("the delete went wrong");
        return false;                                // (the map is never cut here)
    };
    P->asset_edit = [&](const QString &lbl, const OrderedMap<QString, moreviews::Asset> &files) {
        for (const auto &[rel, a] : files)
            edits << u"asset_edit %1 | %2 %3 %4"_s.arg(lbl, rel, a.how, img_h(a.img));
        auto &assets = doc->map<moreviews::AssetEdits>(u"assets"_s);
        doc->edit(lbl, {}, [&] {
            for (const auto &[rel, a] : files)
                assets.fresh.insert(rel, a);
        }, {}, {}, {u"assets"_s});
    };
    P->resize(1500, 900);
    P->show();
    QApplication::processEvents();
    CutPreview *pv = P->preview;
    QList<QPushButton *> btns = P->findChildren<QPushButton *>();

    auto btn = [&](const QString &text, int i = 0) {
        for (QPushButton *b : btns)
            if (b->text() == text && i-- == 0)
                return b;
        throw std::runtime_error("no button");
    };
    auto st = [](widgets::Stepper *s) {
        return u"%1(%2%3)"_s.arg(s->value()).arg(s->minus->isEnabled() ? u"-"_s : QString(),
                                                 s->plus->isEnabled() ? u"+"_s : QString());
    };
    auto state = [&](const QString &what) {
        MapSizeWorkspace &p = *P;
        out << u"== "_s + what;
        out << u"size %1 | hint %2"_s.arg(p.size_lbl->text(), p.hint->text());
        out << u"rule "_s + p.rule->text();
        QStringList db;
        for (const auto &[k, b] : p.dir_btns)
            db << u"%1=%2"_s.arg(k, v(b->isChecked()));
        out << u"dir %1 btns %2 | w_box %3 h_box %4 | steps %5 left %6 h_steps %7 top %8 | slider %9/%10 h_slider "
               u"%11/%12"_s.arg(p.dir, db.join(u','), v(p.w_box->isVisibleTo(&p)), v(p.h_box->isVisibleTo(&p)),
                                st(p.steps), st(p.left), st(p.h_steps), st(p.top))
                   .arg(p.slider->value())
                   .arg(p.slider->maximum())
                   .arg(p.h_slider->value())
                   .arg(p.h_slider->maximum());
        out << u"labels %1 | %2 | %3 | %4"_s.arg(p.total_lbl->text(), p.right_lbl->text(), p.h_lbl->text(),
                                                 p.bottom_lbl->text());
        out << u"result "_s + p.result->text();
        out << u"warn %1 %2 | cut %3 %4 | armed %5 | del %6 mm %7/%8"_s.arg(
            v(p.warn->isVisibleTo(&p)), p.warn->text(), v(p.cut_btn->isEnabled()), p.cut_btn->text(), v(p._armed),
            v(p.del_box->isChecked()), v(p.mm_box->isChecked()), v(p.mm_box->isVisibleTo(&p)));
        out << u"minimap %1 | fit btn %2"_s.arg(p.mm_note->text(), v(p.mm_fit_btn->isVisibleTo(&p)));
        const auto [x0, y0, dw, dh] = pv->_place();
        out << u"preview %1 %2 %3 %4 vertical %5 max %6 | %7x%8 img %9x%10 | place %11 %12 %13 %14"_s.arg(pv->left)
                   .arg(pv->right)
                   .arg(pv->top)
                   .arg(pv->bottom)
                   .arg(v(pv->vertical))
                   .arg(pv->max_cut)
                   .arg(pv->w)
                   .arg(pv->h)
                   .arg(pv->img.width())
                   .arg(pv->img.height())
                   .arg(v(x0), v(y0), v(dw), v(dh));
        out << u"msgs %1 | edits %2 | pending %3"_s.arg(msgs.join(u" / "_s), edits.join(u" / "_s))
                   .arg(doc ? doc->pending_count() : 0);
        msgs.clear();
        edits.clear();
    };
    auto grab = [&](const QString &name) {
        QApplication::processEvents();
        P->grab().save(QDir(out_dir).filePath(name));
        out << u"grab "_s + name;
    };
    auto send = [&](QEvent::Type kind, double x, double y, Qt::MouseButton button = Qt::LeftButton,
                    std::optional<Qt::MouseButtons> buttons = std::nullopt) {
        const QPointF pos(x, y);
        const Qt::MouseButtons bs = buttons ? *buttons
                                            : (kind != QEvent::MouseButtonRelease ? Qt::MouseButtons(button)
                                                                                  : Qt::MouseButtons(Qt::NoButton));
        QMouseEvent e(kind, pos, pos, kind != QEvent::MouseMove ? button : Qt::NoButton, bs, Qt::NoModifier);
        if (kind == QEvent::MouseButtonPress)
            pv->mousePressEvent(&e);
        else if (kind == QEvent::MouseMove)
            pv->mouseMoveEvent(&e);
        else
            pv->mouseReleaseEvent(&e);
    };
    auto drag = [&](double x0, double y0, double x1, double y1, const QString &what) {
        send(QEvent::MouseButtonPress, x0, y0);
        out << u"pressed at %1,%2: drag %3 cursor %4"_s.arg(v(x0), v(y0), v(bool(pv->_drag)))
                   .arg(int(pv->cursor().shape()));
        send(QEvent::MouseMove, (x0 + x1) / 2.0, (y0 + y1) / 2.0);
        out << u"half way: preview %1 %2 %3 %4 | %5 | %6"_s.arg(pv->left)
                   .arg(pv->right)
                   .arg(pv->top)
                   .arg(pv->bottom)
                   .arg(P->right_lbl->text(), P->bottom_lbl->text());
        send(QEvent::MouseMove, x1, y1);
        send(QEvent::MouseButtonRelease, x1, y1);
        state(what);
    };
    auto hover = [&](double x, double y) {
        send(QEvent::MouseMove, x, y, Qt::NoButton, Qt::MouseButtons(Qt::NoButton));
        const QString s = pv->_side_at(QPointF(x, y));
        return u"%1:%2"_s.arg(s.isEmpty() ? u"None"_s : s).arg(int(pv->cursor().shape()));
    };

    P->refresh(doc);
    P->_check_minimap();
    state(u"start"_s);
    grab(u"page_start.png"_s);
    // the amount
    P->steps->plus->click();
    state(u"＋ once"_s);
    P->steps->plus->click();
    P->steps->plus->click();
    state(u"＋ three times"_s);
    P->steps->minus->click();
    state(u"− once"_s);
    P->steps->edit->setText(u"100"_s);
    Q_EMIT P->steps->edit->editingFinished();
    state(u"typed 100"_s);
    P->slider->setValue(5);
    state(u"slider at 5"_s);
    for (const QString &t : {u"All from the left"_s, u"Half and half"_s, u"All from the right"_s}) {
        btn(t)->click();
        state(t);
    }
    btn(u"◀ 8 px more from the left"_s)->click();
    btn(u"◀ 8 px more from the left"_s)->click();
    state(u"16 px more from the left"_s);
    btn(u"8 px more from the right ▶"_s)->click();
    state(u"8 px more from the right"_s);
    // the borders in the picture
    {
        const auto [x0_, y0, dw_, dh] = pv->_place();
        const double ym = y0 + dh / 2.0;
        const QString h1 = hover(pv->edge_x(u"left"_s), ym);
        const QString h2 = hover(pv->edge_x(u"right"_s), ym);
        const QString h3 = hover((pv->edge_x(u"left"_s) + pv->edge_x(u"right"_s)) / 2.0, ym);
        const QString h4 = hover(pv->edge_x(u"left"_s), y0 - 40);
        const QString h5 = hover(3.0, 3.0);
        out << u"hover %1 %2 %3 %4 %5"_s.arg(h1, h2, h3, h4, h5);
        drag(pv->edge_x(u"left"_s), ym, pv->edge_x(u"left"_s) + 40.0, ym, u"the left border dragged 40 px right"_s);
        drag(pv->edge_x(u"right"_s), ym, pv->edge_x(u"right"_s) - 1000.0, ym, u"the right border dragged far left"_s);
        const double mid = (pv->edge_x(u"left"_s) + pv->edge_x(u"right"_s)) / 2.0;
        drag(mid, ym, mid - 25.0, ym, u"the kept part dragged 25 px left"_s);
        send(QEvent::MouseButtonPress, pv->edge_x(u"left"_s), ym, Qt::RightButton);
        out << u"right button: drag "_s + v(bool(pv->_drag));
    }
    P->slider->setValue(P->slider->maximum());
    btn(u"Half and half"_s)->click();
    state(u"the most, half and half"_s);
    grab(u"page_width.png"_s);
    P->del_box->setChecked(false);
    state(u"provinces kept"_s);
    P->del_box->setChecked(true);
    P->mm_box->setChecked(false);
    state(u"minimap left"_s);
    P->mm_box->setChecked(true);
    // the cut: armed, then stopped by the provinces' delete (never cut)
    {
        const long long l = std::min(P->left->value(), P->total());
        const auto gone = affected(md, l, P->total() - l).first;
        out << u"gone %1"_s.arg(gone.size());
        P->_cut();
        state(u"cut once: armed"_s);
        if (!gone.empty()) {
            P->_cut();
            state(u"cut twice: the delete said no"_s);
            how = u"raise"_s;
            P->_cut();
            P->_cut();
            state(u"cut twice: the delete raised"_s);
        }
    }
    // the height
    P->set_dir(u"h"_s);
    state(u"the height chosen"_s);
    P->h_steps->plus->click();
    P->h_steps->plus->click();
    state(u"height ＋ twice"_s);
    for (const QString &t : {u"All from the top"_s, u"Half and half"_s, u"All from the bottom"_s}) {
        btn(t, t == u"Half and half" ? 1 : 0)->click();
        state(t);
    }
    btn(u"▲ 8 px more from the top"_s)->click();
    state(u"8 px more from the top"_s);
    btn(u"8 px more from the bottom ▼"_s)->click();
    state(u"8 px more from the bottom"_s);
    P->h_slider->setValue(std::min(6, P->h_slider->maximum()));
    btn(u"Half and half"_s, 1)->click();
    state(u"height slider at 6, half and half"_s);
    {
        const auto [x0, y0_, dw, dh_] = pv->_place();
        const double xm = x0 + dw / 2.0;
        const QString h1 = hover(xm, pv->edge_y(u"top"_s));
        const QString h2 = hover(xm, pv->edge_y(u"bottom"_s));
        const QString h3 = hover(xm, (pv->edge_y(u"top"_s) + pv->edge_y(u"bottom"_s)) / 2.0);
        const QString h4 = hover(x0 - 30, 300.0);
        out << u"hover %1 %2 %3 %4"_s.arg(h1, h2, h3, h4);
        drag(xm, pv->edge_y(u"top"_s), xm, pv->edge_y(u"top"_s) + 12.0, u"the top border dragged down"_s);
        const double mid = (pv->edge_y(u"top"_s) + pv->edge_y(u"bottom"_s)) / 2.0;
        drag(xm, mid, xm, mid + 9.0, u"the kept part dragged down"_s);
        drag(xm, pv->edge_y(u"bottom"_s), xm, pv->edge_y(u"bottom"_s) + 500.0, u"the bottom border dragged off"_s);
    }
    grab(u"page_height.png"_s);
    P->_cut();
    state(u"height cut armed"_s);
    // a width amount while the height is chosen: the width takes over
    P->steps->setValue(P->_cuts[2]);
    state(u"width set from outside"_s);
    P->h_steps->blockSignals(true);                  // both at once (only from outside): refused
    P->h_steps->setValue(P->_hcuts[1]);
    P->h_steps->blockSignals(false);
    P->top->blockSignals(true);
    P->top->setRange(0, P->h_steps->value());
    P->top->setValue(8);
    P->top->blockSignals(false);
    P->_changed();
    P->_cut();
    state(u"both at once"_s);
    P->set_dir(u"w"_s);
    state(u"the width again"_s);
    // the minimap fitted: a pending picture
    if (P->mm_fit_btn->isVisibleTo(P.get())) {
        P->mm_fit_btn->click();
        state(u"minimap fitted"_s);
        P->_changed();
        state(u"pending: the cut waits"_s);
        grab(u"page_pending.png"_s);
        P->_cut();
        state(u"cut while pending"_s);
        const auto u = doc->undo();
        out << u"undo "_s + (u ? *u : u"None"_s);
        P->_check_minimap();
        P->_changed();
        state(u"after undo"_s);
    }
    P->refresh(doc);
    state(u"refreshed"_s);
    P->steps->setValue(0);
    state(u"nothing cut"_s);
    P->_cut();
    state(u"cut with nothing"_s);

    // --- vanilla, no document
    auto vm = std::make_shared<moddata::Mod>(BASE, std::nullopt, std::nullopt, QStringList{});
    vm->load();
    auto vmd = std::make_shared<mapview::MapData>(*vm, vm->p({u"map"_s, vm->provinces_bmp}));
    P->hide();
    auto P2 = std::make_unique<MapSizeWorkspace>(*vm, *vmd);
    std::swap(P, P2);
    pv = P->preview;
    P->resize(1500, 900);
    P->show();
    QApplication::processEvents();
    doc = nullptr;                                   // (for the state lines only)
    P->refresh(nullptr);
    P->_check_minimap();
    state(u"vanilla"_s);
    P->steps->plus->click();
    state(u"vanilla ＋"_s);
    grab(u"page_vanilla.png"_s);
    P->_cut();
    state(u"vanilla cut: no document"_s);

    QFile f(QDir(out_dir).filePath(u"lines.txt"_s));
    if (!f.open(QIODevice::WriteOnly))
        return 3;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    std::printf("%d lines\n", int(out.size()));
    return 0;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
