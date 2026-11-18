// Scripts → Texts & preview against Python, the C++ side of tools/check_locwin.py.
// Usage: med_locwin_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/events.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "map/mapview_data.h"
#include "ui/eventpanel.h"
#include "ui/locwin.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QTimer>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::v;

static constexpr int W = 1560, H = 980;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}

static QString combo(QComboBox *c)
{
    QStringList items;
    for (int i = 0; i < c->count(); ++i) {
        const QVariant d = c->itemData(i);
        items << u"%1/%2"_s.arg(c->itemText(i), d.isValid() ? d.toString() : u"None"_s);
    }
    return u"%1 %2 %3 %4"_s.arg(QString::number(c->count()), QString::number(c->currentIndex()), v(c->currentText()),
                                h(items.join(u'\n').toUtf8()));
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

    auto wait = [&] {
        QElapsedTimer t;
        t.start();
        while (ep->working() && t.elapsed() < 300000)
            QApplication::processEvents();
        QApplication::processEvents();
    };
    auto settle = [](int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms)
            QApplication::processEvents();
    };
    auto snap = [&](const QString &tag) {
        std::fprintf(stderr, "step %s\n", qPrintable(tag));   // (where a crash happens: the last step printed)
        std::fflush(stderr);
        locwin::TextsWindow *tw = win.text_win;
        out << u"%1 | title %2 | steps %3 | msg %4 | sel %5"_s.arg(
            tag, v(tw->windowTitle()), QString::number(d.undo_stack.size()), v(win.st_msg->text()),
            tw->entry ? tw->entry->uid.value_or(u"None"_s) : u"None"_s);
        out << u"%1 head %2 | sub %3"_s.arg(tag, v(tw->head->text()), v(tw->sub->text()));
        out << u"%1 nations %2"_s.arg(tag, combo(tw->nation_box));
        out << u"%1 pictures %2 | %3"_s.arg(tag, combo(tw->pic_box), v(tw->pic_note->text()));
        for (size_t i = 0; i < tw->fields.size(); ++i) {
            locwin::Field *f = tw->fields[i];
            out << u"%1 field %2 %3 | %4 | %5 | %6 | %7 %8 %9"_s.arg(
                       tag, QString::number(i), v(f->title->text()), v(f->key), v(f->key_lbl->text()),
                       v(f->edit->toPlainText()), v(f->edit->placeholderText()), QString::number(f->edit->height()),
                       QString::number(f->edit->isEnabled() ? 1 : 0)) +
                       u" | %1 | %2"_s.arg(f->note->isVisibleTo(tw) ? v(f->note->text()) : u"-"_s,
                                           QString::number(f->in_script ? 1 : 0));
        }
        QString pv = u"None"_s;
        if (tw->_img) {
            const QImage &img = *tw->_img;
            pv = u"%1x%2 %3"_s.arg(QString::number(img.width()), QString::number(img.height()),
                                   h(QByteArray(reinterpret_cast<const char *>(img.constBits()), img.sizeInBytes())));
        }
        out << u"%1 preview %2 | %3"_s.arg(tag, pv, v(tw->notes->text()));
        const QPixmap pm = tw->view->pixmap();
        out << u"%1 view %2"_s.arg(tag, pm.isNull() ? u"None"_s : u"%1x%2"_s.arg(pm.width()).arg(pm.height()));
    };
    auto grab = [&](QWidget *w, const QString &name) {
        settle(400);
        w->grab().save(QDir(out_dir).filePath(name));
    };

    win.set_tab(u"events"_s);
    QApplication::processEvents();
    wait();
    std::vector<events::Entry *> evs;
    for (const auto &x : ep->db->entries)
        if (x->is_event() && !x->province() && !x->major)
            evs.push_back(x.get());
    events::Entry *e = evs[0];
    for (events::Entry *x : evs)
        if (x->options.size() >= 2 && !x->picture.isEmpty()) {
            e = x;
            break;
        }
    out << u"event "_s + e->uid.value_or(u"None"_s);
    const events::Entry e_kept = *e;               // (Python keeps the old object alive past a re-read; here: a copy)
    ep->select(e);
    QApplication::processEvents();
    wait();
    win._script_tool(u"texts"_s);
    QApplication::processEvents();
    locwin::TextsWindow *tw = win.text_win;
    tw->resize(W, H);
    settle(500);
    tw->_fit();
    snap(u"opened"_s);
    grab(tw, u"texts_event.png"_s);
    {
        locwin::Field *f = tw->fields[0];
        f->edit->selectAll();
        f->edit->wrap(u"R"_s);
        settle(400);
        snap(u"wrapped"_s);
        tw->_commit(f->key, f->edit->value());
        QApplication::processEvents();
        settle(300);
        snap(u"committed"_s);
    }
    if (tw->fields.size() > 1) {
        locwin::Field *f = tw->fields[1];
        f->edit->setPlainText(u"Our $COUNTRY$ and $NOSUCHWORD$; §Ygold §Zbad\nnext @ENG flag"_s);
        settle(400);
        snap(u"odd text"_s);
        tw->_commit(f->key, f->edit->value());
        QApplication::processEvents();
        snap(u"odd committed"_s);
    }
    if (tw->nation_box->count() > 1) {
        tw->nation_box->setCurrentIndex(1);
        tw->_show();
        QApplication::processEvents();
        snap(u"nation 2"_s);
    }
    {
        std::vector<std::pair<QString, QString>> items;
        for (const auto &kv : tw->samples())
            items.emplace_back(kv.first, kv.second);
        std::sort(items.begin(), items.end());
        out << u"samples "_s + v(items);
    }
    for (const auto &[kind, typed] : {std::pair{u"§"_s, u""_s}, std::pair{u"\\"_s, u""_s}, std::pair{u"$"_s, u""_s},
                                      std::pair{u"$"_s, u"COU"_s}, std::pair{u"$"_s, u"prov"_s}, std::pair{u"@"_s, u""_s},
                                      std::pair{u"@"_s, u"E"_s}, std::pair{u"?"_s, u""_s}}) {
        const auto rows = tw->suggestions(kind, typed);
        QStringList p;
        for (size_t i = 0; i < rows.size() && i < 12; ++i)
            p << u"[%1, %2, %3]"_s.arg(v(rows[i].text), v(rows[i].put), rows[i].colour ? v(*rows[i].colour) : u"None"_s);
        out << u"suggest %1 %2 %3 [%4]"_s.arg(kind, typed, QString::number(rows.size()), p.join(u", "_s));
    }
    locwin::CodeEdit *ce = tw->fields[0]->edit;
    for (const QString &text : {u"abc $CO"_s, u"x §"_s, u"$A$ $B"_s, u"@EN"_s, u"@ENGL"_s, u"tail \\"_s, u"none"_s,
                                u"$COUNTRY$"_s}) {
        ce->setPlainText(text);
        QTextCursor c = ce->textCursor();
        c.movePosition(QTextCursor::End);
        ce->setTextCursor(c);
        const auto w = ce->_word();
        out << u"word %1 -> %2"_s.arg(v(text), w ? u"[%1, %2, %3]"_s.arg(v(w->kind), v(w->typed), QString::number(w->n))
                                                : u"None"_s);
    }
    tw->refresh(&d, &e_kept);
    settle(300);
    snap(u"refreshed entry"_s);
    if (tw->pic_box->count() > 2) {
        tw->pic_box->setCurrentIndex(2);
        tw->_pick_picture();
        QApplication::processEvents();
        wait();
        settle(300);
        snap(u"picture chosen"_s);
    }
    const std::vector<std::pair<QString, std::function<bool(const events::Entry &)>>> kinds = {
        {u"decision"_s, [](const events::Entry &x) { return !x.is_event(); }},
        {u"province event"_s, [](const events::Entry &x) { return x.province(); }},
        {u"major event"_s, [](const events::Entry &x) { return x.major; }}};
    for (const auto &[name, pick] : kinds) {
        events::Entry *x = nullptr;
        for (const auto &y : ep->db->entries)
            if (pick(*y)) {
                x = y.get();
                break;
            }
        if (!x) {
            out << u"no "_s + name;
            continue;
        }
        ep->select(x);
        QApplication::processEvents();
        wait();
        settle(300);
        snap(name);
        if (name != u"major event")
            grab(tw, u"texts_%1.png"_s.arg(name.split(u' ')[0]));
    }
    for (const QString &k : {u""_s, u"my_key"_s, u"Give Sanctions Menu"_s, u"a.b-c"_s, u"key\n"_s, u"ÄBC"_s})
        out << u"literal %1 %2"_s.arg(v(k), locwin::is_literal(k) ? u"True"_s : u"False"_s);
    const QString text = u"x = 1\nevent = {\n\ttitle = \"Old one\"\n\tdesc = \"Old one\"\n\toption = { name = \"A\" }\n"
                         "\toption = { name = \"A\" }\n}\n"_s;
    struct Case {
        QString what;
        std::optional<long long> nth;
        QString old, new_;
    };
    for (const Case &c : {Case{u"title"_s, std::nullopt, u"Old one"_s, u"New \"one\""_s},
                          Case{u"desc"_s, std::nullopt, u"Old one"_s, u"D"_s}, Case{u"name"_s, 1, u"A"_s, u"B"_s},
                          Case{u"name"_s, 5, u"A"_s, u"C"_s}, Case{u"name"_s, std::nullopt, u"Z"_s, u"C"_s}})
        out << u"in_script %1 %2 %3"_s.arg(c.what, c.nth ? QString::number(*c.nth) : u"None"_s,
                                            v(locwin::set_in_script(text, {6, text.size()}, c.what, c.nth, c.old, c.new_)));
    win._refresh_windows();
    settle(300);
    snap(u"main refresh"_s);
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
