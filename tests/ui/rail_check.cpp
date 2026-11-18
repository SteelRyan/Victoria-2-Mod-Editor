// railicons / rail against Python, the C++ side of tools/check_rail.py (the same gallery, the same order).
// Usage: med_rail --dump <out folder>
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "ui/rail.h"
#include "ui/railicons.h"
#include "ui/theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::L;
using pyform::v;
using namespace rail;

static const QStringList KEYS = {u"output"_s, u"shortcuts"_s, u"submods"_s, u"import"_s, u"library"_s, u"warfare"_s,
                                 u"rules"_s, u"pops"_s, u"provinces"_s, u"draw"_s, u"pos"_s, u"adj"_s, u"search"_s,
                                 u"states"_s, u"canal"_s, u"nations"_s, u"armies"_s, u"navies"_s, u"diplomacy"_s,
                                 u"politics"_s, u"technology"_s, u"production"_s, u"events"_s, u"errors"_s,
                                 u"changelog"_s, u"mapsize"_s, u"nothing"_s};

static QString b(bool x) { return x ? u"True"_s : u"False"_s; }

static int dump(const QString &out_dir)
{
    QDir().mkpath(out_dir);
    QStringList out;
    std::vector<std::pair<QString, QWidget *>> shots;
    // --- icons, drawn straight into pictures
    for (const auto &[size, col] : {std::pair{28, theme::GOLD}, {64, theme::INK_DIM}}) {
        QImage img(int(KEYS.size()) * (size + 8), size + 8, QImage::Format_ARGB32_Premultiplied);
        img.fill(QColor(theme::BG));
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        std::vector<bool> got;
        for (int i = 0; i < KEYS.size(); ++i)
            got.push_back(railicons::draw_icon(p, KEYS[i], QRectF(4 + i * (size + 8), 4, size, size + (i % 3)),
                                               QColor(col)));
        p.end();
        img.save(QDir(out_dir).filePath(u"icons_%1.png"_s.arg(size)));
        out << u"icons %1 %2"_s.arg(size).arg(v(got));
    }
    // --- entries
    const Page page = PAGES.value(u"production"_s);
    for (const auto &[name, slim, on, errs, pend] : std::vector<std::tuple<QString, bool, bool, long long, long long>>{
             {u"plain"_s, false, false, 0, 0}, {u"on"_s, false, true, 0, 0}, {u"errors"_s, false, false, 3, 0},
             {u"pending"_s, false, false, 0, 1}, {u"both"_s, false, true, 12, 1500}, {u"big"_s, false, false, 2500, 999},
             {u"slim_plain"_s, true, false, 0, 0}, {u"slim_on"_s, true, true, 0, 0}, {u"slim_both"_s, true, false, 4, 2},
             {u"slim_err"_s, true, true, 1, 0}}) {
        auto *it = new RailItem(page);
        it->slim = slim;
        it->setChecked(on);
        it->set_counts(pend, errs);
        it->resize(it->sizeHint());
        shots.emplace_back(u"item_"_s + name, it);
        out << u"item %1 hint %2 tip %3"_s.arg(name, L(it->sizeHint().width(), it->sizeHint().height()), v(it->toolTip()));
    }
    auto *lng = new RailItem({u"x"_s, u"A page with a very long name indeed"_s, u"X"_s, u"x"_s, std::nullopt, u"tip"_s});
    lng->set_counts(7, 0);
    lng->resize(164, 27);
    shots.emplace_back(u"item_long"_s, lng);
    for (bool slim : {false, true}) {
        auto *fb = new FoldButton();
        fb->slim = slim;
        fb->resize(!slim ? 164 : 46, 26);
        shots.emplace_back(u"fold_%1"_s.arg(slim ? u"slim"_s : u"wide"_s), fb);
    }
    // --- the whole rail
    QStringList seq;
    NavRail *nr = nullptr;
    for (bool slim : {false, true}) {
        nr = new NavRail(slim);
        QObject::connect(nr, &NavRail::folded, [&seq](bool s) { seq << v(s); });
        QObject::connect(nr, &NavRail::picked, [&seq](const QString &k) { seq << v(k); });
        nr->set_current(u"canal"_s);
        OrderedMap<QString, long long> pending, errors;
        for (auto [k, n] : {std::pair{u"output"_s, 3LL}, {u"production"_s, 2LL}, {u"pops"_s, 1LL}, {u"adj"_s, 0LL},
                            {u"canal"_s, 5LL}, {u"nowhere"_s, 9LL}})
            pending.insert(k, n);
        for (auto [k, n] : {std::pair{u"errors"_s, 4LL}, {u"canal"_s, 1LL}, {u"draw"_s, 2000LL}})
            errors.insert(k, n);
        nr->set_counts(pending, errors);
        nr->resize(nr->width(), 900);
        shots.emplace_back(u"rail_%1"_s.arg(slim ? u"slim"_s : u"wide"_s), nr);
        QStringList checked, counts;
        for (const auto &[k, it] : nr->items) {
            if (it->isChecked())
                checked << v(k);
            counts << L(k, it->pending, it->errors);
        }
        out << u"rail %1 width %2 checked %3 counts %4"_s.arg(b(slim)).arg(nr->width()).arg(pyform::list(checked),
                                                                                         pyform::list(counts));
    }
    nr->items.value(u"diplomacy"_s)->click();
    nr->fold->click();
    nr->fold->click();
    nr->set_slim(true, true);
    nr->set_current(u"pops"_s);
    QStringList checked;
    for (const auto &[k, it] : nr->items)
        if (it->isChecked())
            checked << v(k);
    out << u"signals %1 now %2 width %3 checked %4"_s.arg(pyform::list(seq), b(nr->slim)).arg(nr->width()).arg(
        pyform::list(checked));
    // --- tables
    QStringList po;
    for (const auto &[t, s] : std::vector<std::pair<QString, std::optional<QString>>>{
             {u"pops"_s, std::nullopt}, {u"map"_s, std::nullopt}, {u"map"_s, u"draw"_s}, {u"map"_s, u"pos"_s},
             {u"map"_s, u"adj"_s}, {u"map"_s, u"canal"_s}, {u"map"_s, u"zzz"_s}, {u"armies"_s, std::nullopt},
             {u"armies"_s, u"navies"_s}, {u"armies"_s, u"x"_s}, {u"econest"_s, std::nullopt}, {u"errors"_s, std::nullopt},
             {u"nope"_s, std::nullopt}, {u"search"_s, u"a"_s}})
        po << v(page_of(t, s));
    out << u"page_of "_s + pyform::list(po);
    QStringList pages, names;
    for (const auto &[k, p] : PAGES)
        pages << L(k, p.name, p.code, p.tab, p.sub);
    for (const auto &[k, n] : PAGE_NAMES)
        names << L(k, n);
    out << u"pages "_s + pyform::list(pages);
    out << u"names "_s + pyform::list(names);
    for (auto &[name, w] : shots)
        w->show();
    QApplication::processEvents();
    for (auto &[name, w] : shots)
        w->grab().save(QDir(out_dir).filePath(name + u".png"_s));
    QFile f(QDir(out_dir).filePath(u"lines.txt"_s));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 3;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    std::printf("%lld lines, %lld pictures\n", static_cast<long long>(out.size()),
                static_cast<long long>(shots.size()) + 2);
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
