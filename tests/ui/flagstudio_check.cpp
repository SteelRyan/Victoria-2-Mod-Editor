// The flag studio opened from the Nation page against Python, the C++ side of tools/check_flagstudio.py.
// Usage: med_flagstudio_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/countryhist.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "map/mapview_data.h"
#include "map/nationextras_data.h"
#include "ui/colorwheel.h"
#include "ui/dialogs.h"
#include "ui/flagstudio.h"
#include "ui/mapview.h"
#include "ui/nationextras.h"
#include "ui/nationpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QTreeWidget>
#include <algorithm>
#include <cstdio>
#include <map>

using namespace Qt::StringLiterals;
using pyform::v;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}

static constexpr int MAP_W = 1000, MAP_H = 700, SIDE_W = 470;

static QString img_h(const QImage &img0)
{
    if (img0.isNull())
        return u"none"_s;
    const QImage img = img0.convertToFormat(QImage::Format_ARGB32);
    return u"%1x%2:%3"_s.arg(img.width())
        .arg(img.height())
        .arg(h(QByteArray(reinterpret_cast<const char *>(img.constBits()), img.sizeInBytes())));
}

static QString num(const flagstudio::Num &n)
{
    return n.is_int ? QString::number(static_cast<long long>(n.v)) : v(n.v);
}

static QString shp(const flagstudio::Shape &s)
{
    return u"%1:%2:%3:%4:%5:%6:%7:%8:%9"_s.arg(s.kind, v(s.x), v(s.y), num(s.w), num(s.h), v(s.rot), s.color, v(s.lock),
                                               s.t ? v(*s.t) : u"-"_s);
}

static QString des(const flagstudio::Design &dz)
{
    QStringList ss;
    for (const flagstudio::Shape &s : dz.shapes)
        ss << shp(s);
    return u"%1 base %2 | %3"_s.arg(dz.bg, dz.base ? img_h(*dz.base) : u"none"_s, ss.join(u';'));
}

static int run(const QString &root, const QString &backups, const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    flagstudio::app_dir_override = pypath::join(pypath::dirname(out_dir), {u"app"_s});
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

    auto flags = [&] {
        auto &fe = d.map<nationextras::FlagEdits>(u"flags"_s);
        QStringList parts;
        for (const auto &[fn, img] : fe.fresh)
            parts << fn + u"="_s + img_h(img);
        QStringList gone(fe.gone.begin(), fe.gone.end());
        std::sort(gone.begin(), gone.end());
        return u"%1 | gone %2"_s.arg(parts.join(u';'), gone.join(u','));
    };
    auto common = [&](const QString &tag) {
        out << u"%1 | mode %2 | tab %3 | nation %4 | msg %5 | %6 | steps %7 | flags %8"_s.arg(
            tag, win.mode, win.tab, win.nation_tag.value_or(u"None"_s), v(win.st_msg->text()), win.st_pending->text(),
            QString::number(d.undo_stack.size()), flags());
    };
    auto combo = [](const QComboBox *cb) {
        return u"%1/%2 %3"_s.arg(cb->currentIndex()).arg(cb->count()).arg(v(cb->currentText()));
    };
    auto items = [](const QComboBox *cb) {
        QStringList r;
        for (int i = 0; i < cb->count(); ++i)
            r << cb->itemText(i);
        return r.join(u';');
    };
    auto guides = [](const flagstudio::Canvas *c) {
        QStringList g;
        for (const auto &[k, x] : c->_guides)
            g << u"%1 %2"_s.arg(k, v(x));
        return g.join(u';');
    };
    flagstudio::FlagStudio *s = nullptr;
    auto ssnap = [&](const QString &tag) {
        common(tag);
        flagstudio::Canvas *c = s->canvas;
        out << u"%1 S %2 | %3 | %4 %5 | %6 | %7 | vis %8 min %9"_s.arg(
            tag, v(s->head->text()), v(s->windowTitle()), v(s->editing->text()), v(s->editing->styleSheet()),
            v(s->info->text()), v(s->info->styleSheet()), v(s->isVisible()), v(s->isMinimized()));
        QStringList fs;
        for (const auto &[k, e] : s->fields)
            fs << u"%1 %2 %3"_s.arg(k, v(e->text()), v(e->isEnabled()));
        out << u"%1 F %2 | %3 | %4 | rot %5 %6 | thick %7 %8 | lock %9 %10 | undo %11 redo %12 | clear %13"_s.arg(
                   tag, v(s->sh_title->text()), v(s->thick_label->text()), fs.join(u';'), v(s->rot->value()),
                   v(s->rot->isEnabled()), v(s->thick->value()), v(s->thick->isEnabled()), v(s->lock->isChecked()))
                   .arg(v(s->lock->isEnabled()), v(s->undo_btn->isEnabled()), v(s->redo_btn->isEnabled()),
                        v(s->clear_base->isEnabled()));
        out << u"%1 B %2 | set %3 | warn %4 | pick %5 | base %6 [%7] | %8 [%9] | preset %10 | asym %11 %12 | snap %13"_s
                   .arg(tag, s->bg_btn->color, v(s->set_btn->text()), v(s->warn->text()), combo(s->nation_pick),
                        combo(s->base_tag), items(s->base_tag), combo(s->base_var), items(s->base_var))
                   .arg(combo(s->preset_box), v(s->asym_box->isChecked()), combo(s->asym_mode),
                        v(s->snap_box->isChecked()));
        QStringList sl;
        for (int i : c->sels)
            sl << QString::number(i);
        out << u"%1 D %2 | sels %3 | clip %4 | guides %5 | share %6 | asym %7"_s.arg(
            tag, des(c->design), sl.join(u','), QString::number(s->clip.size()), guides(c), v(c->asym_share),
            c->asym ? (c->_asym_img ? img_h(*c->_asym_img) : u"none"_s) : u"-"_s);
        QStringList rows;
        for (int i = 0; i < s->lst->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = s->lst->topLevelItem(i);
            rows << u"%1/%2/%3"_s.arg(it->text(0), it->text(1), v(it->isSelected()));
        }
        QTreeWidgetItem *cur = s->lst->currentItem();
        out << u"%1 L %2 | cur %3 | small %4"_s.arg(tag, rows.join(u';'))
                   .arg(cur ? s->lst->indexOfTopLevelItem(cur) : -1)
                   .arg(img_h(s->small->pixmap().toImage()));
        QStringList cells;
        for (const auto &[sfx, name] : s->variants) {
            const flagstudio::FlagStudio::Cell *ce = s->cells.get(sfx);
            if (!ce)
                continue;
            cells << u"%1 %2 %3 %4 | tick %5 %6 | edit %7 | rm %8 %9 | pic %10 %11 %12 | %13"_s.arg(
                sfx, v(ce->cap->text()), v(ce->state->text()), v(ce->state->styleSheet()),
                v(ce->tick->isVisibleTo(ce->cell)), v(ce->tick->isChecked()), v(ce->edit->isEnabled()),
                v(ce->rm->isEnabled()), v(ce->rm->toolTip()))
                         .arg(v(ce->pic->text()), img_h(ce->pic->pixmap().toImage()), v(ce->pic->toolTip()),
                              v(ce->cell->styleSheet()));
        }
        out << u"%1 C %2 | %3"_s.arg(tag, v(s->foot_title->text()), cells.join(u" ;; "_s));
        QStringList works;
        for (const auto &[t, wk] : s->works)
            for (const auto &[sfx, e] : wk)
                works << u"%1%2 %3 %4 %5 u%6 r%7 | %8"_s.arg(t, sfx, v(e.dirty), e.how,
                                                            e.crc ? QString::number(*e.crc) : u"None"_s)
                             .arg(e.undo->size())
                             .arg(e.redo->size())
                             .arg(des(e.design));
        out << u"%1 W cur %2 | u%3 r%4 | %5"_s.arg(tag, v(s->cur))
                   .arg(s->_undo->size())
                   .arg(s->_redo->size())
                   .arg(works.join(u" ;; "_s));
    };
    const Qt::KeyboardModifiers nomod = Qt::NoModifier;
    auto mouse = [&](QWidget *w, QEvent::Type kind, const QPointF &pos, Qt::MouseButton button,
                     Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) {
        QMouseEvent ev(kind, pos, w->mapToGlobal(pos), button, buttons, mods);
        QApplication::sendEvent(w, &ev);
    };
    auto drag = [&](const QPointF &a, const QPointF &b, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        flagstudio::Canvas *c = s->canvas;
        mouse(c, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton, mods);
        const QPointF mid((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0);
        mouse(c, QEvent::MouseMove, mid, Qt::NoButton, Qt::LeftButton, mods);
        mouse(c, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton, mods);
        out << u"guides "_s + guides(c);
        mouse(c, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton, mods);
        QApplication::processEvents();
    };
    auto key = [&](QWidget *w, int k, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QKeyEvent ev(QEvent::KeyPress, k, mods);
        QApplication::sendEvent(w, &ev);
    };
    auto btn = [&](const QString &text) -> QPushButton * {
        for (QPushButton *b : s->findChildren<QPushButton *>())
            if (b->text() == text)
                return b;
        throw std::runtime_error(("no button " + text).toStdString());
    };
    auto handle = [&](const flagstudio::Shape &sh, const QString &name) {
        for (const auto &[k, p] : s->canvas->_handles(sh))
            if (k == name)
                return p;
        throw std::runtime_error("no handle");
    };
    auto field = [&](const QString &k) -> QLineEdit * {
        for (const auto &[n, e] : s->fields)
            if (n == k)
                return e;
        throw std::runtime_error("no field");
    };

    countryhist::NationData &data = *m->nation_data;
    countryhist::CountryHistory &hist = *m->country_hist;
    QStringList tags;
    for (const auto &[t, _c] : m->countries)
        tags << t;
    std::sort(tags.begin(), tags.end());
    std::map<QString, std::vector<long long>> owned;
    for (const QString &t : tags)
        owned[t] = nationpanel::stats_owned(d, *m, t);
    QStringList cands;
    for (const QString &t : tags)
        if (hist.files.contains(t) && owned[t].size() >= 3 && data.parties(t).size() >= 2 &&
            !m->countries.get(t)->file.isEmpty())
            cands << t;
    const QString tag = cands[0], tag2 = cands[1];
    out << u"nation %1 other %2"_s.arg(tag, tag2);
    win.set_tab(u"nations"_s);
    win.show_nation(tag);
    QApplication::processEvents();
    win.nations_panel->flags_box->open_btn->click();
    QApplication::processEvents();
    s = win.flag_win;
    ssnap(u"open"_s);
    s->grab().save(QDir(out_dir).filePath(u"studio.png"_s));
    flagstudio::Canvas *c = s->canvas;
    // --- a layout, a shape added and handled with the mouse
    s->preset_box->setCurrentIndex(s->preset_box->findData(u"v3"_s));
    btn(u"Use this layout (replaces the shapes)"_s)->click();
    ssnap(u"layout"_s);
    btn(u"★ Star"_s)->click();
    ssnap(u"star"_s);
    const int star = static_cast<int>(c->design.shapes.size()) - 1;
    {
        const flagstudio::Shape &sh = c->design.shapes[star];
        const QPointF p0 = c->_c(QPointF(sh.x, sh.y));
        drag(p0, QPointF(p0.x() + 37, p0.y() + 22));
    }
    ssnap(u"moved"_s);
    drag(handle(c->design.shapes[star], u"br"_s), handle(c->design.shapes[star], u"br"_s) + QPointF(30, 18));
    ssnap(u"resized"_s);
    drag(handle(c->design.shapes[star], u"rot"_s), handle(c->design.shapes[star], u"rot"_s) + QPointF(60, 30));
    ssnap(u"turned"_s);
    drag(handle(c->design.shapes[star], u"br"_s), handle(c->design.shapes[star], u"br"_s) + QPointF(11, 7));
    ssnap(u"resized turned"_s);
    {
        const flagstudio::Shape other = c->design.shapes[0];
        const QPointF q = c->_c(QPointF(other.x, other.y + 20));
        mouse(c, QEvent::MouseButtonPress, q, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
        mouse(c, QEvent::MouseButtonRelease, q, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    }
    ssnap(u"ctrl click"_s);
    {
        const QPointF kb = *c->_group_knob();
        drag(kb, kb + QPointF(80, 40));
    }
    ssnap(u"group turned"_s);
    key(c, Qt::Key_Right);
    key(c, Qt::Key_Down, Qt::ShiftModifier);
    ssnap(u"nudged"_s);
    // --- the fields
    for (const auto &[k, text] : {std::pair{u"x"_s, u"40,5"_s}, std::pair{u"y"_s, u"abc"_s}, std::pair{u"w"_s, u"0"_s}}) {
        QLineEdit *e = field(k);
        e->setText(text);
        Q_EMIT e->editingFinished();
        ssnap(u"typed %1 %2"_s.arg(k, text));
    }
    s->rot->setValue(30);
    Q_EMIT s->rot->valueChanged(s->rot->value());
    ssnap(u"rot 30"_s);
    s->lst->setCurrentItem(s->lst->topLevelItem(star));
    QApplication::processEvents();
    ssnap(u"list pick"_s);
    s->thick->setValue(55);
    Q_EMIT s->thick->valueChanged(s->thick->value());
    ssnap(u"thick"_s);
    Q_EMIT s->col_btn->changed(u"#00aa44"_s);
    ssnap(u"colour"_s);
    s->lock->click();
    ssnap(u"locked"_s);
    s->lock->click();
    for (const QString &t : {u"Corner ╲"_s, u"Centre ↔"_s, u"Sideways"_s}) {
        btn(t)->click();
        ssnap(u"place "_s + t);
    }
    // --- copies, order, delete
    for (const QString &t : {u"Mirror ↔"_s, u"Flip ↕"_s, u"▲"_s, u"▼"_s, u"Copy"_s, u"Paste"_s}) {
        btn(t)->click();
        ssnap(u"tool "_s + t);
    }
    s->_dup();
    ssnap(u"dup"_s);
    btn(u"✕ Delete"_s)->click();
    ssnap(u"deleted"_s);
    s->undo_btn->click();
    s->undo_btn->click();
    s->redo_btn->click();
    ssnap(u"undo redo"_s);
    // --- asymmetries, baseline, background
    s->asym_box->setChecked(true);
    ssnap(u"asym"_s);
    s->asym_mode->setCurrentIndex(2);
    Q_EMIT s->asym_mode->activated(2);
    ssnap(u"asym both"_s);
    s->asym_box->setChecked(false);
    btn(u"Take it as the baseline"_s)->click();
    ssnap(u"no baseline yet"_s);
    Q_EMIT s->bg_btn->changed(u"#202020"_s);
    ssnap(u"background"_s);
    c->grab().save(QDir(out_dir).filePath(u"canvas.png"_s));
    // --- set it, handed on to a ticked one
    QString tick;
    for (const auto &[x, _n] : s->variants)
        if (!x.isEmpty()) {
            tick = x;
            break;
        }
    s->cells.get(tick)->tick->setChecked(true);
    ssnap(u"ticked"_s);
    s->set_btn->click();
    QApplication::processEvents();
    ssnap(u"set"_s);
    s->base_tag->setCurrentIndex(std::max(0, s->base_tag->findData(tag)));
    Q_EMIT s->base_tag->activated(s->base_tag->currentIndex());
    s->base_var->setCurrentIndex(s->base_var->count() - 1);
    btn(u"Take it as the baseline"_s)->click();
    ssnap(u"baseline"_s);
    btn(u"No baseline"_s)->click();
    ssnap(u"baseline dropped"_s);
    QString other_sfx;
    for (const auto &[x, _n] : s->variants)
        if (!x.isEmpty() && x != tick) {
            other_sfx = x;
            break;
        }
    s->cells.get(other_sfx)->edit->click();
    ssnap(u"other flag"_s);
    btn(u"● Disc"_s)->click();
    ssnap(u"disc"_s);
    {
        QLabel *pic = s->cells.get(QString())->pic;
        const QPointF mid(pic->width() / 2.0, pic->height() / 2.0);
        mouse(pic, QEvent::MouseButtonDblClick, mid, Qt::LeftButton, Qt::LeftButton, nomod);
    }
    ssnap(u"double click"_s);
    s->cells.get(QString())->rm->click();
    QApplication::processEvents();
    ssnap(u"flag deleted"_s);
    // --- another nation, on the map, Esc
    s->nation_pick->setCurrentIndex(s->nation_pick->findData(tag2));
    Q_EMIT s->nation_pick->activated(s->nation_pick->currentIndex());
    QApplication::processEvents();
    ssnap(u"nation 2"_s);
    s->nation_pick->setCurrentIndex(s->nation_pick->findData(tag));
    Q_EMIT s->nation_pick->activated(s->nation_pick->currentIndex());
    QApplication::processEvents();
    ssnap(u"nation back"_s);
    btn(u"Pick on the map ▸"_s)->click();
    ssnap(u"pick"_s);
    long long sea = -1;
    for (const auto &[p, pr] : m->provinces)
        if (pr.sea) {
            sea = p;
            break;
        }
    win.on_click(sea, nomod);
    ssnap(u"pick sea"_s);
    win.on_click(owned[tag2][0], nomod);
    QApplication::processEvents();
    ssnap(u"picked"_s);
    btn(u"Pick on the map ▸"_s)->click();
    win.escape();
    ssnap(u"pick esc"_s);
    win.open_flag_studio(std::nullopt, tag);
    QApplication::processEvents();
    ssnap(u"reopened"_s);
    // --- the main window's undo
    out << u"undo "_s + v(win.undo());
    QApplication::processEvents();
    ssnap(u"undone"_s);
    s->grab().save(QDir(out_dir).filePath(u"studio2.png"_s));
    // --- G4
    {
        const bool ok = win.save(false);
        out << u"apply %1 | msg %2 | %3"_s.arg(v(ok), v(win.st_msg->text()), win.st_pending->text());
    }
    QApplication::processEvents();
    ssnap(u"applied"_s);
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
