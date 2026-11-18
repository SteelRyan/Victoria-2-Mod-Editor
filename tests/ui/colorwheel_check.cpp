// colorwheel against Python, the C++ side of tools/check_colorwheel.py.
// Usage: med_colorwheel_check --dump <out folder> <work folder>
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "ui/colorwheel.h"
#include "ui/theme.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::L;
using pyform::v;
using namespace colorwheel;

static QString h(const void *p, size_t n)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(QByteArray::fromRawData(static_cast<const char *>(p), qsizetype(n)),
                                 QCryptographicHash::Sha1).toHex().left(16));
}

static int dump(const QString &out_dir)
{
    QDir().mkpath(out_dir);
    std::map<QString, QStringList> store;
    STORE = &store;
    QStringList out;
    for (int size : {230, 50, 7}) {
        const QImage img = _disc(size);
        out << u"disc %1 %2 %3"_s.arg(size)
                   .arg(img.format() == QImage::Format_RGBA8888 ? u"Format_RGBA8888"_s : QString::number(img.format()),
                        h(img.constBits(), size_t(img.sizeInBytes())));
    }
    Wheel w;
    w.show();
    QStringList sig;
    QObject::connect(&w, &Wheel::moved, [&] { sig << u"moved"_s; });
    for (const QString &c : {u"#3a7bd5"_s, u"#808080"_s, u"#000000"_s, u"#ff0000"_s, u"#12ab34"_s, u"#fefefe"_s,
                             u"#7f0040"_s}) {
        w.set_color(QColor(c));
        out << u"set %1 -> h %2 s %3 v %4 color %5"_s.arg(c, v(w.h), v(w.s), v(w.v), w.color().name());
    }
    int i = 0;
    for (const QString &c : {u"#3a7bd5"_s, u"#808080"_s, u"#7f0040"_s, u"#000000"_s}) {
        w.set_color(QColor(c));
        QApplication::processEvents();
        w.grab().save(QDir(out_dir).filePath(u"wheel_%1.png"_s.arg(i++)));
    }
    auto mouse = [&](QEvent::Type kind, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent ev(kind, pos, QPointF(w.mapToGlobal(pos.toPoint())), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(&w, &ev);
    };
    w.set_color(QColor(u"#3a7bd5"_s));
    using Pts = std::vector<std::pair<long long, long long>>;
    for (const Pts &pts : {Pts{{117, 117}, {150, 60}, {240, 300}, {10, 117}}, Pts{{117, 118}, {118, 119}},
                           Pts{{260, 40}, {262, 100}, {262, 233}, {262, -20}}, Pts{{240, 200}, {250, 20}}}) {
        auto P = [](const std::pair<long long, long long> &p) { return QPointF(double(p.first), double(p.second)); };
        mouse(QEvent::MouseButtonPress, P(pts[0]), Qt::LeftButton, Qt::LeftButton);
        for (size_t k = 1; k < pts.size(); ++k)
            mouse(QEvent::MouseMove, P(pts[k]), Qt::NoButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, P(pts.back()), Qt::LeftButton, Qt::NoButton);
        out << u"drag %1 -> h %2 s %3 v %4 color %5 drag %6"_s.arg(v(pts), v(w.h), v(w.s), v(w.v), w.color().name(),
                                                                 v(w._drag));
    }
    out << u"signals "_s + v(sig);
    // --- the popup
    ColorPopup pop(u"#3a7bd5"_s);
    QStringList ps;
    QObject::connect(&pop, &ColorPopup::preview, [&](const QString &c) { ps << u"preview "_s + c; });
    QObject::connect(&pop, &ColorPopup::chosen, [&](const QString &c) { ps << u"chosen "_s + c; });
    QObject::connect(&pop, &ColorPopup::cancelled, [&](const QString &c) { ps << u"cancelled "_s + c; });
    pop.show();
    QApplication::processEvents();
    pop.grab().save(QDir(out_dir).filePath(u"popup_empty.png"_s));
    auto fields = [&] {
        QStringList f{pop.hex->text()};
        for (auto *e : pop.rgb)
            f << e->text();
        f << pop.color() << pop.now->styleSheet();
        return v(f);
    };
    out << u"popup %1 note %2"_s.arg(fields(), v(pop.saved_note->text()));
    for (const QString &t : {u"12ab34"_s, u"#ABCDEF"_s, u"nonsense"_s, u" #0f0 "_s, u"#12345"_s}) {
        pop.hex->setText(t);
        pop._hex_done();
        out << u"hex %1 -> %2"_s.arg(v(t), fields());
    }
    for (const QStringList &vals : {QStringList{u"10"_s, u"20"_s, u"30"_s}, QStringList{u"300"_s, u"-5"_s, u" 7 "_s},
                                    QStringList{u"x"_s, u"1"_s, u"2"_s}, QStringList{u"1_0"_s, u"+4"_s, u"0"_s}}) {
        for (int k = 0; k < 3; ++k)
            pop.rgb[size_t(k)]->setText(vals[k]);
        pop._rgb_done();
        out << u"rgb %1 -> %2"_s.arg(v(vals), fields());
    }
    for (const QString &c : {u"#ff0000"_s, u"#00ff00"_s, u"#0000ff"_s, u"#ff0000"_s}) {
        pop.set_color(QColor(c));
        pop._save();
    }
    pop._unsave(u"#00ff00"_s);
    for (const QString &c : {u"#111111"_s, u"#222222"_s, u"#333333"_s, u"#444444"_s})
        push_recent(c);
    pop._fill_swatches();
    QApplication::processEvents();
    out << u"lists recent %1 saved %2 note %3"_s.arg(v(recent()), v(saved()), v(pop.saved_note->text()));
    for (int k = 0; k < 40; ++k)
        save_color(u"#%1"_s.arg(k * 40503, 6, 16, QChar(u'0')));
    out << u"saved max %1 first %2 last %3"_s.arg(saved().size()).arg(saved().first(), saved().last());
    for (int k = 0; k < 36; ++k)
        unsave_color(u"#%1"_s.arg(k * 40503, 6, 16, QChar(u'0')));
    pop._fill_swatches();
    QApplication::processEvents();
    pop.grab().save(QDir(out_dir).filePath(u"popup_swatches.png"_s));
    pop.wheel->set_color(QColor(u"#7f0040"_s));
    pop.close();
    QApplication::processEvents();
    out << u"closed %1 recent %2"_s.arg(v(ps), v(recent()));
    ps.clear();
    ColorPopup pop2(u"#7f0040"_s);
    QObject::connect(&pop2, &ColorPopup::preview, [&](const QString &c) { ps << u"preview "_s + c; });
    QObject::connect(&pop2, &ColorPopup::cancelled, [&](const QString &c) { ps << u"cancelled "_s + c; });
    QObject::connect(&pop2, &ColorPopup::chosen, [&](const QString &c) { ps << u"chosen "_s + c; });
    pop2.show();
    pop2.set_color(QColor(u"#010203"_s));
    pop2._cancel();
    QApplication::processEvents();
    out << u"cancel "_s + v(ps);
    // --- the button
    ColorButton b(u"#3a7bd5"_s);
    QStringList bs;
    QObject::connect(&b, &ColorButton::changed, [&](const QString &c) { bs << u"changed "_s + c; });
    QObject::connect(&b, &ColorButton::preview, [&](const QString &c) { bs << u"preview "_s + c; });
    b.show();
    QApplication::processEvents();
    b.grab().save(QDir(out_dir).filePath(u"button.png"_s));
    b.set(u"#zz"_s);
    b.set(u"#a1b2c3"_s, false);
    b.set(u"#c3b2a1"_s);
    b._open();
    b.popup->set_color(QColor(u"#445566"_s));
    out << u"button open tip %1 popup %2"_s.arg(
        v(b.toolTip()), L((long long)b.popup->pos().x(), (long long)b.popup->pos().y(), (long long)b.popup->width(),
                          (long long)b.popup->height()));
    b.popup->close();
    QApplication::processEvents();
    out << u"button %1 tip %2 signals %3"_s.arg(b.color, v(b.toolTip()), v(bs));
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
        if (a.size() >= 3 && a[1] == u"--dump")
            return dump(a[2]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
