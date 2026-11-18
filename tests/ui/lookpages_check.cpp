// lookpages (Library → Map look, Menu & loading) against Python, the C++ side of tools/check_lookpages.py.
// Usage: med_lookpages_check --run <mod copy> <backups> <out folder> <inputs> <work folder>   (MODEDITOR_SETTINGS)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "ui/lookpages.h"
#include "ui/mapview.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <cstdio>
#include <stdexcept>

using namespace Qt::StringLiterals;
using pyform::v;
using namespace lookpages;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
static QString ah(const fourk::Pic &a)
{
    return u"%1x%2x%3 %4"_s.arg(a.h).arg(a.w).arg(a.ch).arg(
        h(QByteArray(reinterpret_cast<const char *>(a.d.data()), qsizetype(a.d.size()))));
}
static QString img_h(const QImage &img)
{
    const QImage im = img.convertToFormat(QImage::Format_RGBA8888);
    QByteArray rows;
    for (int y = 0; y < im.height(); ++y)
        rows.append(reinterpret_cast<const char *>(im.constScanLine(y)), im.width() * 4);
    return u"%1x%2 %3"_s.arg(im.width()).arg(im.height()).arg(h(rows));
}

struct Combo {
    int bright, contrast, colour;
    std::optional<Rgb> tint;
    int strength;
};
static const std::vector<Combo> COMBOS = {
    {30, 0, 0, std::nullopt, 0}, {0, -40, 0, std::nullopt, 0}, {0, 0, 55, std::nullopt, 0},
    {0, 0, 0, Rgb{176, 140, 92}, 35}, {-20, 35, -60, Rgb{84, 128, 190}, 80}, {100, 100, 100, Rgb{201, 161, 90}, 100},
    {-100, -100, -100, Rgb{140, 140, 140}, 1}};
static const Combo FULL = {15, -25, 30, Rgb{176, 140, 92}, 40};
static fourk::Pic adj(const fourk::Pic &a, const Combo &c)
{
    return adjust(a, c.bright, c.contrast, c.colour, c.tint, c.strength);
}

static int run(const QString &root, const QString &backups, const QString &out_dir, const QString &inputs,
               const QString &work)
{
    QDir().mkpath(out_dir);
    QStringList out;

    // --- adjust on made-up pictures
    std::vector<long long> widths;
    for (long long w = 1; w <= 20; ++w)
        widths.push_back(w);
    widths.push_back(101);
    widths.push_back(977);
    for (long long w : widths) {
        fourk::Pic a(3, w, 4);
        for (long long y = 0; y < 3; ++y)
            for (long long x = 0; x < w; ++x) {
                a.at(y, x, 0) = std::uint8_t((y * 131 + x * 71 + (x * x) % 17) % 256);
                a.at(y, x, 1) = std::uint8_t((y * 37 + x * 113 + 29 + (x * y) % 23) % 256);
                a.at(y, x, 2) = std::uint8_t((y * 11 + x * 5 + 200 + (x * x * x) % 31) % 256);
                a.at(y, x, 3) = std::uint8_t((x * 3 + y * 7) % 256);
            }
        QStringList hs;
        for (const Combo &c : COMBOS)
            hs << ah(adj(a, c)).split(u' ')[1];
        out << u"adjust w %1: %2"_s.arg(w).arg(hs.join(u' '));
    }

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
    // (no changelog in the port yet: Python's is pointed into its side's own folder and not compared)
    document::Document *doc = win.doc.get();
    auto add = [&win](const QString &l, const OrderedMap<QString, moreviews::Asset> &f) { win.on_asset_edit(l, f); };

    auto settle = [](LookPage *P) {
        P->_tick->stop();
        P->_show();
    };
    auto state = [&](LookPage *P, const QString &what, bool icons = false) {
        QApplication::processEvents();
        out << u"== "_s + what;
        QStringList items;
        for (int i = 0; i < P->lst->count(); ++i) {
            QListWidgetItem *it = P->lst->item(i);
            QString s = u"%1 [%2]"_s.arg(it->text(), it->foreground().color().name());
            if (icons)
                s += u" icon "_s + (it->icon().isNull() ? u"none"_s : img_h(it->icon().pixmap(120, 68).toImage()));
            items << s;
        }
        out << u"items %1: %2 | current %3"_s.arg(P->lst->count()).arg(items.join(u" ; "_s)).arg(P->lst->currentRow());
        out << u"rel %1 | head %2 | info %3"_s.arg(v(P->rel), P->head->text(), P->info->text());
        QStringList sl;
        for (const auto &[k, s] : P->sliders)
            sl << u"%1=%2"_s.arg(k).arg(s->value());
        QString tb;
        for (QPushButton *b : P->tint_btns)
            tb += b->isChecked() ? u'x' : u'.';
        out << u"note %1 %2 | use %3 | sliders %4 | tint %5 %6"_s.arg(
            v(P->note->isVisibleTo(P)), P->note->text(), v(P->use_btn->isEnabled()), sl.join(u','),
            P->_tint ? u"%1,%2,%3"_s.arg(std::get<0>(*P->_tint)).arg(std::get<1>(*P->_tint)).arg(std::get<2>(*P->_tint))
                     : u"none"_s,
            tb);
        const QPixmap pm = P->view->pixmap();
        out << u"base %1 | view %2"_s.arg(P->_base ? ah(*P->_base) : u"None"_s,
                                          pm.isNull() ? u"none"_s : img_h(pm.toImage()));
        if (auto *M = qobject_cast<MenuLookPage *>(P))
            out << u"music %1 extra %2"_s.arg(M->music_lbl->text(), v(M->extra->isVisibleTo(M)));
        out << u"msg %1 | %2 | steps %3"_s.arg(v(win.st_msg->text()), win.st_pending->text())
                   .arg(doc->undo_stack.size());
    };
    auto grab = [&](QWidget *P, const QString &name) {
        QApplication::processEvents();
        P->grab().save(QDir(out_dir).filePath(name));
        out << u"grab "_s + name;
    };
    auto row_of = [](LookPage *P, const QString &end) {
        for (int i = 0; i < P->lst->count(); ++i)
            if (P->lst->item(i)->data(Qt::UserRole).toStringList()[0].endsWith(end))
                return i;
        throw std::runtime_error("no such row");
    };
    auto in = [&](const QString &n) { return QDir(inputs).filePath(n); };

    // --- Map look
    auto *P = new MapLookPage(*m, add);
    P->resize(1500, 900);
    P->show();
    QApplication::processEvents();
    P->refresh(doc);
    state(P, u"map start"_s, true);
    grab(P, u"map_start.png"_s);
    for (int i = 0; i < P->lst->count(); ++i) {
        P->lst->setCurrentRow(i);
        out << u"slot %1 %2 | %3 | base %4 | view %5 | full %6"_s.arg(i).arg(*P->rel, P->info->text(), ah(*P->_base),
                                                                         img_h(P->view->pixmap().toImage()),
                                                                         P->_work ? ah(adj(*P->_work, FULL)) : u"None"_s);
    }
    P->lst->setCurrentRow(0);
    P->sliders.value(u"bright"_s)->setValue(25);
    settle(P);
    state(P, u"bright 25"_s);
    P->tint_btns[2]->click();
    settle(P);
    state(P, u"tint blue"_s);
    P->sliders.value(u"contrast"_s)->setValue(-30);
    P->sliders.value(u"colour"_s)->setValue(40);
    settle(P);
    state(P, u"contrast -30, colour 40"_s);
    grab(P, u"map_tinted.png"_s);
    P->reset_btn->click();
    state(P, u"sliders back to 0 (no settle)"_s);
    settle(P);
    state(P, u"sliders back to 0"_s);
    P->sliders.value(u"bright"_s)->setValue(-40);
    P->tint_btns[5]->click();
    P->sliders.value(u"strength"_s)->setValue(70);
    settle(P);
    P->use_btn->click();
    state(P, u"used"_s);
    P->lst->setCurrentRow(1);
    P->lst->setCurrentRow(0);
    state(P, u"back to the changed one"_s);
    grab(P, u"map_changed.png"_s);
    P->sliders.value(u"bright"_s)->setValue(10);
    settle(P);
    P->use_btn->click();
    state(P, u"used again on top"_s);
    P->take_file(in(u"picture.bmp"_s));
    state(P, u"replaced (stretched)"_s);
    P->take_file(in(u"broken.png"_s));
    state(P, u"no picture"_s);
    P->lst->setCurrentRow(row_of(P, u"texturesheet.tga"_s));
    P->take_file(in(u"picture.bmp"_s));
    state(P, u"texture sheet replaced (24 bit)"_s);
    P->lst->setCurrentRow(row_of(P, u"borders.dds"_s));
    P->tint_btns[0]->click();
    settle(P);
    P->use_btn->click();
    state(P, u"borders tinted"_s);
    out << u"undo "_s + v(win.undo());
    P->refresh(doc);
    state(P, u"undone once"_s);

    // --- Menu & loading
    P->hide();
    auto *M = new MenuLookPage(*m, add);
    M->resize(1500, 900);
    M->show();
    QApplication::processEvents();
    M->refresh(doc);
    state(M, u"menu start"_s, true);
    grab(M, u"menu_start.png"_s);
    for (int i = 0; i < M->lst->count(); ++i) {
        M->lst->setCurrentRow(i);
        out << u"slot %1 %2 | %3 | base %4 | view %5"_s.arg(i).arg(*M->rel, M->info->text(), ah(*M->_base),
                                                             img_h(M->view->pixmap().toImage()));
    }
    M->lst->setCurrentRow(0);
    M->take_file(in(u"picture.bmp"_s));
    state(M, u"menu background replaced (cut to the shape)"_s);
    M->lst->setCurrentRow(2);
    M->sliders.value(u"colour"_s)->setValue(-100);
    M->sliders.value(u"contrast"_s)->setValue(20);
    settle(M);
    M->use_btn->click();
    state(M, u"loading picture greyed"_s);
    M->lst->setCurrentRow(M->lst->count() - 1);
    M->tint_btns[4]->click();
    settle(M);
    M->use_btn->click();
    state(M, u"last loading picture tinted"_s);
    M->take_music(in(u"title.mp3"_s));
    state(M, u"title music"_s);
    grab(M, u"menu_changed.png"_s);

    // --- Apply
    {
        const bool ok = win.save(false);
        out << u"apply %1 | msg %2 | %3"_s.arg(v(ok), v(win.st_msg->text()), win.st_pending->text());
    }
    M->refresh(doc);
    M->lst->setCurrentRow(0);
    state(M, u"menu after Apply"_s);
    M->hide();
    P->show();
    P->refresh(doc);
    P->lst->setCurrentRow(0);
    state(P, u"map after Apply"_s);
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
        if (a.size() >= 7 && a[1] == u"--run")
            return run(a[2], a[3], a[4], a[5], a[6]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
