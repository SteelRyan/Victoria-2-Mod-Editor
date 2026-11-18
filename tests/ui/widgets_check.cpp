// theme / textselect / widgets against Python, the C++ side of tools/check_widgets.py (the same gallery, the same
// order).  Usage: med_widgets --dump <out folder>
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QSlider>
#include <QTableWidget>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::L;
using pyform::v;
using namespace widgets;

static std::vector<Segment> segs(bool outer = false, size_t n = 4)
{
    std::vector<Segment> s = outer ? std::vector<Segment>{{u"Poor"_s, 70.0, false, {184, 101, 79}},
                                                          {u"Middle"_s, 20.0, false, {110, 140, 200}},
                                                          {u"Rich"_s, 10.0, false, {214, 190, 120}}}
                                   : std::vector<Segment>{{u"Farmers"_s, 42.5, false, {201, 161, 90}},
                                                          {u"Labourers"_s, 30.25, true, {95, 179, 179}},
                                                          {u"Clerks"_s, 12.0, false, {176, 122, 161}},
                                                          {u"Soldiers"_s, 15.25, false, {122, 155, 118}}};
    s.resize(std::min(n, s.size()));
    return s;
}
static QString b(bool x) { return x ? u"True"_s : u"False"_s; }
static QString num(double x, bool whole) { return whole ? QString::number(static_cast<long long>(x)) : v(x); }

static int dump(const QString &out_dir)
{
    QDir().mkpath(out_dir);
    QStringList out;
    std::vector<std::pair<QString, QWidget *>> shots;
    auto shot = [&](const QString &name, QWidget *w, QSize size) {
        w->resize(size);
        shots.emplace_back(name, w);
    };
    // --- chips
    for (const auto &[name, on, nar] : std::vector<std::tuple<QString, bool, std::optional<long long>>>{
             {u"chip_on"_s, true, std::nullopt}, {u"chip_off"_s, false, std::nullopt},
             {u"chip_narrowed"_s, true, 24035}, {u"chip_long"_s, false, 7}}) {
        auto *c = new Chip(name == u"chip_long" ? u"Farmers of the long plains"_s : u"Farmers"_s, {120, 200, 80},
                           name == u"chip_long" ? std::nullopt : std::optional<long long>(66785));
        c->setChecked(on);
        if (nar)
            c->set_narrowed(nar);
        shot(name, c, {175, 26});
        out << u"%1 tip %2"_s.arg(name, v(c->toolTip()));
    }
    auto *g = new ChipGroup(u"Pop types"_s);
    g->unchecked = {u"b"_s};
    g->set_items({{u"a"_s, u"Farmers"_s, {201, 161, 90}, 1200345}, {u"b"_s, u"Labourers"_s, {95, 179, 179}, 98765},
                  {u"c"_s, u"Clerks"_s, {176, 122, 161}, 4321}, {u"d"_s, u"Soldiers"_s, {122, 155, 118}, 0},
                  {u"e"_s, u"Aristocrats"_s, {110, 140, 200}, 15}});
    g->set_narrowed(OrderedMap<QString, long long>{{u"a"_s, 100}, {u"c"_s, 3}});
    shot(u"chipgroup"_s, g, {420, 110});
    auto sorted = [](const QSet<QString> &s) {
        QStringList l(s.begin(), s.end());
        l.sort();
        return l;
    };
    out << u"chipgroup checked "_s + v(sorted(g->checked()));
    g->invert();
    out << u"chipgroup inverted %1 unchecked %2"_s.arg(v(sorted(g->checked())), v(sorted(g->unchecked)));
    g->invert();
    // --- bars and pie
    auto *bar = new SplitBar();
    bar->set_segments(segs());
    shot(u"splitbar"_s, bar, {380, 54});
    shot(u"splitbar_empty"_s, new SplitBar(), {380, 54});
    auto *e2 = new SplitBar();
    e2->empty_text = u"Nothing to split"_s;
    shot(u"splitbar_text"_s, e2, {380, 54});
    auto *z = new ZoomBar();
    z->set_segments(segs());
    z->set_range(35, 60);
    shot(u"zoombar"_s, z, {380, 54});
    out << u"zoombar tip "_s + v(z->toolTip());
    auto *p = new PieChart();
    p->set_segments(segs());
    p->set_outer(segs(true));
    shot(u"pie"_s, p, {320, 240});
    auto *p2 = new PieChart();
    p2->set_segments(segs(false, 2));
    shot(u"pie_plain"_s, p2, {300, 240});
    shot(u"pie_empty"_s, new PieChart(), {300, 240});
    // --- buttons, dots, steppers
    for (const auto &[name, errs, warns] : std::vector<std::tuple<QString, long long, long long>>{
             {u"badge"_s, 3, 12}, {u"badge_k"_s, 1500, 0}, {u"badge_none"_s, 0, 0}}) {
        auto *bb = new BadgeButton(u"Provinces"_s);
        bb->setToolTip(u"The provinces page"_s);
        bb->set_counts(errs, warns);
        shot(name, bb, {190, 34});
        out << u"%1 tip %2"_s.arg(name, v(bb->toolTip()));
    }
    auto *h = new HelpDot(u"A <b>note</b> about it"_s);
    shot(u"helpdot"_s, h, {22, 22});
    out << u"helpdot tip "_s + v(h->toolTip());
    auto *st = new Stepper(0, 2000, 1836);
    shot(u"stepper"_s, st, {120, 30});
    std::vector<long long> seq;
    QObject::connect(st, &Stepper::valueChanged, [&seq](int x) { seq.push_back(x); });
    st->plus->click();
    st->minus->click();
    st->setValue(5000);
    st->setValue(-3);
    st->setChoices({1836, 1861, 1900, 1836, 1870});
    st->plus->click();
    st->plus->click();
    st->minus->click();
    st->setValue(1880);
    st->setValue(1865);
    st->edit->setText(u"abc"_s);
    Q_EMIT st->edit->editingFinished();
    st->edit->setText(u" 1901 "_s);
    Q_EMIT st->edit->editingFinished();
    out << u"stepper %1 now %2 minus %3 plus %4 edit %5"_s.arg(v(seq), v(st->value()), b(st->minus->isEnabled()),
                                                               b(st->plus->isEnabled()), v(st->edit->text()));
    auto *ss = new StepSlider({u"none"_s, u"a little"_s, u"some"_s, u"a lot"_s, u"all"_s}, 2);
    shot(u"stepslider"_s, ss, {420, 74});
    auto *ss2 = new StepSlider({u"off"_s, u"on"_s, u"both"_s}, 0);
    ss2->setEnabled(false);
    shot(u"stepslider_off"_s, ss2, {300, 74});
    auto *sf = new SliderField(0, 100, 1000, 25, u"px"_s, true);
    shot(u"sliderfield"_s, sf, {320, 30});
    QStringList vals;
    QObject::connect(sf, &SliderField::valueChanged, [&vals](double x) { vals << num(x, true); });
    for (int pos : {0, 1, 333, 500, 999, 1000})
        sf->slider->setValue(pos);
    for (const QString t : {u"750"_s, u"1,5e9"_s, u"12,5"_s}) {
        sf->edit->setText(t);
        Q_EMIT sf->edit->editingFinished();
    }
    out << u"sliderfield %1 now %2 pos %3 edit %4"_s.arg(pyform::list(vals), num(sf->value(), true))
               .arg(sf->slider->value())
               .arg(v(sf->edit->text()));
    auto *sd = new SliderField(0.1, 2.0, 10.0, 0.75, QString(), false, 2);
    sd->set_snaps({1.0, 1.5});
    QStringList vals2;
    QObject::connect(sd, &SliderField::valueChanged, [&vals2](double x) { vals2 << v(x); });
    for (int pos : {0, 470, 480, 700, 1000})
        sd->slider->setValue(pos);
    sd->setValue(7.777);
    out << u"sliderfield2 %1 now %2 pos %3 edit %4"_s.arg(pyform::list(vals2), v(sd->value()))
               .arg(sd->slider->value())
               .arg(v(sd->edit->text()));
    shot(u"sliderfield2"_s, sd, {320, 30});
    auto *wb = new WrapButton(u"Fix this one and then go on to the next one of the same group ▸ next"_s);
    shot(u"wrapbutton"_s, wb, {200, wb->heightForWidth(200)});
    out << u"wrapbutton hfw "_s + L(static_cast<long long>(wb->heightForWidth(120)), static_cast<long long>(wb->heightForWidth(200)),
                                   static_cast<long long>(wb->heightForWidth(400)));
    auto *el = new ElideLabel(u"A rather long label text that will not fit into its narrow box at all"_s);
    shot(u"elidelabel"_s, el, {140, 22});
    out << u"elidelabel tip "_s + v(el->toolTip());
    auto *sec = new Section();
    for (const auto &[text, obj] : std::vector<std::pair<QString, QString>>{
             {u"Title"_s, u"title"_s}, {u"A sub line"_s, u"sub"_s}, {u"label"_s, u"label"_s}, {u"faint"_s, u"faint"_s},
             {u"A warning in red"_s, u"warn"_s}, {u"group head"_s, u"groupHead"_s}})
        sec->lay->addWidget(label(text, obj));
    shot(u"section"_s, sec, {260, 200});
    // --- lists and headers
    auto *lw = new QListWidget();
    lw->setItemDelegate(heading_delegate(lw));
    for (const auto &[t, head] : std::vector<std::pair<QString, bool>>{
             {u"Europe"_s, true}, {u"France"_s, false}, {u"Prussia"_s, false}, {u"Asia"_s, true}, {u"Japan"_s, false}}) {
        auto *it = new QListWidgetItem(t);
        if (head)
            it->setData(HEAD_ROLE, true);
        lw->addItem(it);
    }
    lw->setCurrentRow(2);
    shot(u"headings"_s, lw, {240, 200});
    auto *tw = new QTableWidget(3, 3);
    tw->setHorizontalHeader(new TintHeader(QColor(u"#c9a15a"_s), {1}));
    tw->setHorizontalHeaderLabels({u"Unit"_s, u"Attack"_s, u"Defence"_s});
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            tw->setItem(r, c, new QTableWidgetItem(u"%1-%2"_s.arg(r).arg(c)));
    shot(u"tintheader"_s, tw, {320, 130});
    // --- the theme on plain widgets
    auto *plain = new QWidget();
    auto *lay = new QVBoxLayout(plain);
    for (const QString name : {u""_s, u"apply"_s, u"link"_s, u"seg"_s, u"primary"_s}) {
        auto *bt = new QPushButton(u"Button %1"_s.arg(name.isEmpty() ? u"plain"_s : name));
        if (!name.isEmpty())
            bt->setObjectName(name);
        lay->addWidget(bt);
    }
    auto *ck = new QPushButton(u"Checked"_s);
    ck->setCheckable(true);
    ck->setChecked(true);
    lay->addWidget(ck);
    lay->addWidget(new QLineEdit(u"some text"_s));
    auto *cb = new QComboBox();
    cb->addItems({u"one"_s, u"two"_s});
    lay->addWidget(cb);
    auto *c1 = new QCheckBox(u"A tick"_s);
    c1->setChecked(true);
    lay->addWidget(c1);
    shot(u"themed"_s, plain, {260, 330});
    // --- layouts
    auto *host = new QWidget();
    auto *gf = new GridFlow(host, 5);
    std::vector<QLabel *> labs;
    for (int i = 0; i < 7; ++i) {
        labs.push_back(new QLabel(u"cell %1"_s.arg(i)));
        gf->addWidget(labs.back());
    }
    for (int w : {100, 180, 360, 361, 700}) {
        gf->setGeometry(QRect(3, 4, w, 300));
        QStringList g_;
        for (QLabel *l : labs)
            g_ << L(static_cast<long long>(l->x()), static_cast<long long>(l->y()), static_cast<long long>(l->width()),
                    static_cast<long long>(l->height()));
        out << u"gridflow %1 hfw %2 %3"_s.arg(w).arg(gf->heightForWidth(w)).arg(pyform::list(g_));
    }
    auto *host2 = new QWidget();
    auto *wr = new WrapRow(host2, 4, 3);
    std::vector<QPushButton *> btns;
    for (int i = 0; i < 6; ++i) {
        btns.push_back(new QPushButton(u"b%1"_s.arg(i)));
        btns.back()->setFixedSize(40 + 10 * i, 24 + (i % 2) * 6);
        wr->addWidget(btns.back());
    }
    for (int w : {60, 150, 260, 1000}) {
        wr->setGeometry(QRect(0, 0, w, 300));
        QStringList g_;
        for (QPushButton *bt : btns)
            g_ << L(static_cast<long long>(bt->x()), static_cast<long long>(bt->y()));
        out << u"wraprow %1 hfw %2 %3"_s.arg(w).arg(wr->heightForWidth(w)).arg(pyform::list(g_));
    }
    out << u"wraprow hint "_s + L(static_cast<long long>(wr->sizeHint().width()), static_cast<long long>(wr->sizeHint().height()));
    // --- push_edge
    for (const auto &[i, pos] : std::vector<std::pair<long long, double>>{
             {0, 50.0}, {0, 80.0}, {1, 5.0}, {2, 101.0}, {1, -3.0}, {2, 64.5}}) {
        auto s = segs();
        push_edge(s, i, pos);
        std::vector<double> pcts;
        for (const Segment &x : s)
            pcts.push_back(x.pct);
        out << u"push %1 %2 %3"_s.arg(i).arg(v(pos), v(pcts));
    }
    // --- theme helpers
    QStringList inks;
    for (const auto &[r, g_, b_] : std::vector<std::tuple<int, int, int>>{
             {0, 0, 0}, {255, 255, 255}, {201, 161, 90}, {90, 90, 90}, {128, 128, 128}, {118, 118, 118}})
        inks << theme::ink_on(r, g_, b_);
    out << u"ink_on "_s + v(inks);
    QStringList fm;
    for (long long x : {0LL, 5LL, 999LL, 1000LL, 1234567LL, -1234LL, 1000000000LL})
        fm << fmt(x);
    out << u"fmt "_s + v(fm);
    // --- show and picture everything
    for (auto &[name, w] : shots)
        w->show();
    QApplication::processEvents();
    for (auto &[name, w] : shots)
        w->grab().save(QDir(out_dir).filePath(name + u".png"_s));
    QStringList sel;
    for (auto &[name, w] : shots)
        for (QLabel *lab : w->findChildren<QLabel *>())
            sel << u"%1:%2:%3"_s.arg(name, lab->text().left(12)).arg(int(lab->textInteractionFlags()));
    out << u"selectable "_s + v(sel);
    QFile f(QDir(out_dir).filePath(u"lines.txt"_s));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 3;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    std::printf("%lld lines, %lld pictures\n", static_cast<long long>(out.size()), static_cast<long long>(shots.size()));
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
