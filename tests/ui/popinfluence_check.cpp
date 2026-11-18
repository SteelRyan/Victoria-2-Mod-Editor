// Game rules → Pop influences against Python, the C++ side of tools/check_popinfluence.py.
// Usage: med_popinfluence_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "../core/nopopup.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "map/mapview_data.h"
#include "ui/popinfluence.h"
#include "ui/regionimport.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QFontMetrics>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;
using namespace popinfluence;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
static QString h8(const QString &s) { return h(s.toUtf8()); }
static QString r(double x) { return py::repr_float(x); }
static QString rect(const QRectF &q) { return u"%1,%2,%3,%4"_s.arg(r(q.x()), r(q.y()), r(q.width()), r(q.height())); }
static QString opt(const std::optional<QString> &x) { return x ? *x : u"None"_s; }
static QString opt(const std::optional<long long> &x) { return x ? QString::number(*x) : u"None"_s; }
static QString tf(bool b) { return b ? u"True"_s : u"False"_s; }
static QString fo(const std::optional<Focus> &f) { return f ? u"%1/%2/%3"_s.arg(f->key, f->pop, r(f->value)) : u"None"_s; }

static QString net_s(const std::optional<Net> &n)
{
    if (!n)
        return u"None"_s;
    QStringList per;
    for (const auto &[nm, o] : n->per)
        per << nm + u": "_s + net_s(o);
    return u"pos %1 neg %2 net %3 how %4 note %5 per [%6]"_s.arg(r(n->pos), r(n->neg), r(n->net), n->how, n->note,
                                                                 per.join(u"; "_s));
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
    PopInfluencePage page(*m);
    page.md = md.get();
    page.resize(1600, 900);
    page.show();
    QObject::connect(&page, &PopInfluencePage::scriptEdit, [&](const QString &l, const QString &rel, const QString &t) {
        out << u"EDIT %1 | %2 | %3"_s.arg(l, rel, h8(t));
    });
    QObject::connect(&page, &PopInfluencePage::scriptsEdit, [&](const OrderedMap<QString, QString> &d, const QString &l) {
        QStringList ps;
        for (const auto &[rel, t] : d)
            ps << rel + u':' + h8(t);
        out << u"EDITS %1 | %2"_s.arg(l, ps.join(u' '));
    });
    QApplication::processEvents();
    page.refresh(win.doc.get());
    QApplication::processEvents();

    auto vis = [&](QWidget *w) { return w->isVisibleTo(&page) ? u"1"_s : u"0"_s; };
    // a combo box: its items (text | data), the current one, enabled; `focus_of`: the data is a place in a focus list
    auto combo = [&](QComboBox *c, const std::vector<Focus> *focus_of = nullptr) {
        QStringList items;
        for (int i = 0; i < c->count(); ++i) {
            const QVariant d = c->itemData(i);
            QString ds;
            if (d.isValid())
                ds = focus_of ? fo((*focus_of)[static_cast<size_t>(d.toLongLong())]) : d.toString();
            items << c->itemText(i) + u'|' + ds;
        }
        return u"%1 [%2] cur %3 en %4"_s.arg(c->count()).arg(items.join(u" ¦ "_s)).arg(c->currentIndex())
            .arg(c->isEnabled() ? u"1"_s : u"0"_s);
    };

    auto snap = [&](const QString &tag) {
        if (qEnvironmentVariableIsSet("CHECK_TRACE"))
            std::fprintf(stderr, "snap %s\n", tag.toUtf8().constData());
        QApplication::processEvents();
        Chart *ch = page.chart;
        out << u"%1 | pop %2 head %3 dir %4 source %5 target %6 sel %7 class %8 src %9"_s.arg(
                   tag, opt(page.pop), opt(page.head), page.dir, opt(page.source), page.target, opt(page.sel),
                   page.view_class, page.src) +
                   u" focus %1 months %2 size %3"_s.arg(fo(page.focus)).arg(page.months).arg(page.size);
        QStringList pl;
        for (int i = 0; i < page.pops->count(); ++i)
            pl << u"%1|%2|%3"_s.arg(page.pops->item(i)->text(), page.pops->item(i)->data(Qt::UserRole).toString(),
                                    !page.pops->item(i)->icon().isNull() ? u"i"_s : u"-"_s);
        out << u"%1 pops %2 cur %3 %4"_s.arg(tag).arg(page.pops->count()).arg(page.pops->currentRow()).arg(h8(pl.join(u'\n')));
        QStringList hs;
        for (const auto &[k, b] : page.head_btn)
            hs << u"%1:%2%3%4"_s.arg(k, b->text(), b->isChecked() ? u"x"_s : QString(),
                                     b->isVisibleTo(&page) ? QString() : u"~"_s);
        out << tag + u" heads "_s + hs.join(u' ');
        QStringList ds, cl, tm;
        for (const QString k : {u"in"_s, u"out"_s}) {
            QPushButton *b = page.dir_btn.at(k);
            ds << u"%1:%2:%3%4%5"_s.arg(k, b->text(), b->isChecked() ? u"x"_s : QString(), vis(b),
                                        b->isEnabled() ? u"e"_s : QString());
        }
        for (const QString k : {u"poor"_s, u"middle"_s, u"rich"_s}) {   // (Python's dict order)
            QPushButton *b = page.class_btn.at(k);
            cl << u"%1:%2%3"_s.arg(k, b->isChecked() ? u"x"_s : QString(), vis(b));
        }
        for (const auto &[k, b] : page.time_btn)
            tm << u"%1:%2"_s.arg(k).arg(b->isChecked() ? u"x"_s : QString());
        out << u"%1 dirs %2 | class %3 | times %4"_s.arg(tag, ds.join(u' '), cl.join(u' '), tm.join(u' '));
        out << u"%1 targets %2 %3 | %4 %5"_s.arg(tag, page.target_lbl->text(), vis(page.target_lbl), combo(page.targets),
                                                 vis(page.targets));
        const std::vector<Focus> tr = page.tiers();
        out << tag + u" focus_box "_s + combo(page.focus_box, &tr);
        CasePanel *cp = page.case_panel;
        out << u"%1 case nv %2 %3 | focus %4 %5 | more %6 | facts %7"_s.arg(
            tag, vis(cp->nv), combo(cp->nv), cp->focus_lbl->text(), combo(cp->focus, &cp->_tiers),
            cp->more_btn->isChecked() ? u"x"_s : QString(), vis(cp->facts_lbl));
        int row = 0;
        for (const auto &[k, sv] : cp->sliders) {
            const bool bold = cp->grid->itemAtPosition(row, 0)->widget()->font().bold();
            out << u"%1   slider %2 %3 %4..%5 %6 %7"_s.arg(tag, k)
                       .arg(sv.first->value())
                       .arg(sv.first->minimum())
                       .arg(sv.first->maximum())
                       .arg(sv.second->text(), bold ? u"B"_s : QString());
            ++row;
        }
        for (const auto &[k, b] : cp->fact_btn)
            out << u"%1   fact %2 | %3 %4 | %5"_s.arg(tag, k, b->text(), b->isChecked() ? u"x"_s : QString(), b->toolTip());
        QStringList vs, fs;
        for (const auto &[k, v] : page.cs.vals)
            vs << k + u'=' + r(v);
        for (const QString &f : std::as_const(page.cs.facts))
            fs << f;
        std::sort(fs.begin(), fs.end());
        out << u"%1 case vals %2 | facts %3"_s.arg(tag, vs.join(u' '), fs.join(u" ¦ "_s));
        const std::pair<const char *, QLabel *> labels[] = {
            {"d_title", page.d_title}, {"d_file", page.d_file}, {"result", page.result}, {"fact", page.fact},
            {"place_lbl", page.place_lbl}, {"c_head", page.c_head}, {"c_where", page.c_where},
            {"base_lbl", page.base_lbl}, {"msg", page.msg}};
        for (const auto &[nm, w] : labels)
            out << u"%1 %2 %3 %4"_s.arg(tag, QString::fromLatin1(nm), vis(w), w->text());
        out << u"%1 edits f %2 %3 | c %4 | base %5 %6 | share %7 | size %8 | rows %9"_s.arg(
                   tag, page.f_edit->text(), page.f_edit->isEnabled() ? u"e"_s : QString(), page.c_edit->toPlainText(),
                   page.base_edit->text(), page.base_edit->isEnabled() ? u"e"_s : QString(), page.share_edit->text(),
                   page.size_edit->text(), vis(page.size_row)) +
                   u" %1 | card %2 | add %3 | del %4 %5"_s.arg(vis(page.share_row), vis(page.card_sec), page.add_btn->text(),
                                                             page.del_btn->text(), page.del_btn->styleSheet());
        QStringList tk, op;
        std::vector<long long> tks(ch->ticked.begin(), ch->ticked.end());
        std::sort(tks.begin(), tks.end());
        for (long long i : tks)
            tk << QString::number(i);
        for (long long i : ch->open)
            op << QString::number(i);
        out << u"%1 chart %2 | %3 | base %4 | result %5 | sel %6 | ticked %7 | open %8 | min %9"_s.arg(
                   tag, ch->title, ch->sub, ch->base, ch->result, opt(ch->sel), tk.join(u','), op.join(u','),
                   QString::number(ch->minimumHeight())) +
                   u" | mid "_s + rect(ch->_mid);
        out << tag + u" net "_s + net_s(ch->net);
        QStringList tys = ch->_ty.keys();
        std::sort(tys.begin(), tys.end());
        QStringList tyl, nts;
        for (const QString &k : tys)
            tyl << u"%1:%2/%3"_s.arg(k, r(ch->_ty.value(k).first), r(ch->_ty.value(k).second));
        for (const auto &[a, b] : ch->_ty_notes)
            nts << r(a) + u'/' + r(b);
        out << u"%1 ty %2 | notes %3"_s.arg(tag, tyl.join(u' '), nts.join(u' '));
        if (ch->sample) {
            const Sample &s = *ch->sample;
            out << u"%1 sample %2 | %3 | mark %4"_s.arg(tag, s.head, s.headline, opt(s.mark));
            for (const SampleRow &rw : s.rows)
                out << u"%1   row %2 (%3, %4, %5) %6 %7"_s.arg(tag, rw.name)
                           .arg(std::get<0>(rw.rgb))
                           .arg(std::get<1>(rw.rgb))
                           .arg(std::get<2>(rw.rgb))
                           .arg(r(rw.before), r(rw.after));
            for (const auto &[t, kind] : s.notes)
                out << u"%1   note %2 | %3"_s.arg(tag, kind, t);
        } else {
            out << tag + u" sample None"_s;
        }
        QString fcs = u"None"_s;
        if (ch->focus_card) {
            const FocusCard &fc = *ch->focus_card;
            QStringList ts;
            for (const FocusTier &t : fc.tiers)
                ts << u"%1/%2/%3"_s.arg(t.key, t.name, r(t.value));
            fcs = u"%1 | %2 | pick %3 | units %4"_s.arg(fc.name, ts.join(u" ¦ "_s), opt(fc.pick),
                                                       fc.units ? r(*fc.units) : u"None"_s);
        }
        QString fr;
        if (ch->_focus_rect) {
            QStringList rows;
            for (const auto &[q, k] : ch->_focus_rows)
                rows << rect(q) + u':' + QString::number(k);
            fr = rect(*ch->_focus_rect) + u" "_s + rows.join(u' ');
        }
        out << u"%1 focus card %2 open %3 %4"_s.arg(tag, fcs, tf(ch->focus_open), fr);
        for (const Ladder &L : ch->_lads) {
            QStringList ix, th;
            for (long long i : L.idx)
                ix << QString::number(i);
            if (L.thr)
                for (double v : *L.thr)
                    th << r(v);
            out << u"%1   lad %2 %3 %4 %5 %6 %7 %8 %9"_s.arg(tag, L.kind, L.key, tf(L.neg), L.rest, L.tmpl.value_or(QString()),
                                                          ix.join(u','), L.thr ? th.join(u',') : u"None"_s, tf(L.grouped));
        }
        for (const Chart::LadBox &lb : ch->_lad_boxes) {
            QStringList rows;
            for (const auto &[a, b] : lb.rows)
                rows << rect(a) + u':' + QString::number(b);
            out << u"%1   ladbox %2 %3 %4 %5 | %6"_s.arg(tag, rect(lb.r)).arg(lb.n).arg(lb.col).arg(
                rows.join(u' '), ch->tip_at(QPointF(lb.r.left() + 20, lb.r.top() + 20)));
        }
        const auto cur = page.cur();
        for (size_t i = 0; i < ch->mods.size(); ++i) {
            const Card &c = ch->mods[i];
            QStringList at;
            if (cur)
                for (const auto &[b, k] : page.at_of(*cur, static_cast<long long>(i)))
                    at << b->rel + u':' + QString::number(k);
            out << u"%1   card %2 %3 | %4 | g %5 | %6 | rate %7 | warn %8 | at %9"_s.arg(
                tag, QString::number(i), c.factor, c.cond, opt(c.group), ch->names(c.cond), ch->rate(c), ch->warn(c),
                at.join(u' '));
        }
        const QFontMetrics fm_(ch->_font(13));
        for (const Chart::Box &b : ch->_boxes) {
            out << u"%1   box %2 %3 %4 | %5"_s.arg(tag, rect(b.r)).arg(b.i).arg(b.col).arg(ch->tip_at(b.r.center()));
            const Card &md_ = ch->mods[static_cast<size_t>(b.i)];   // (the card's words as painted: elided)
            QString words = ch->names(md_.cond);
            if (words.isEmpty())
                words = u"(no condition: always)"_s;
            const double bw = b.r.right() - (b.r.left() + 3 + 66) - 44;   // (TAB_W = 66)
            if (fm_.horizontalAdvance(words) > bw * 2.9)
                words = fm_.elidedText(words, Qt::ElideRight, static_cast<int>(bw * 2.9));
            if (qEnvironmentVariableIsSet("CHECK_WORDS")) {   // (debug: the whole text, the font, the wrapped height)
                const QRectF wb(b.r.left() + 3 + 66 + 8, b.r.top() + 4, bw, b.r.height() - 8);
                const QRect br = fm_.boundingRect(wb.toRect(), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap, words);
                fprintf(stderr, "%s\n", qUtf8Printable(u"%1 words [%2] font %3 br %4 %5 %6 %7"_s.arg(
                    tag, words, ch->_font(13).toString()).arg(br.x()).arg(br.y()).arg(br.width()).arg(br.height())));
            }
            const QList<uint> wu = words.toUcs4();
            const qsizetype n = wu.size();
            out << u"%1   box words %2 %3"_s.arg(tag).arg(n).arg(QString::fromUcs4(
                       reinterpret_cast<const char32_t *>(wu.constData()) + std::max<qsizetype>(0, n - 12),
                       std::min<qsizetype>(12, n)));
        }
        if (ch->_focus_rect)
            out << u"%1   focus tip %2"_s.arg(tag, ch->tip_at(QPointF(ch->_focus_rect->left() + 20, ch->_focus_rect->top() + 20)));
        if (ch->_ty.contains(u"netbig"_s))
            out << u"%1   net tip %2"_s.arg(tag, ch->tip_at(QPointF(ch->_mid.center().x(), ch->_ty.value(u"netbig"_s).first + 5)));
        const QImage img = ch->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        out << u"%1 picture %2x%3 %4"_s.arg(tag)
                   .arg(img.width())
                   .arg(img.height())
                   .arg(h(QByteArray(reinterpret_cast<const char *>(img.constBits()), img.sizeInBytes())));
        if (tag == u"start" || tag == u"pop 1" || tag == u"best")   // (G5: a few of them as files)
            img.save(QDir(out_dir).filePath(u"chart_%1.png"_s.arg(QString(tag).replace(u' ', u'_'))));
        regionimport::SourceMap *pm = page.pmap;
        QStringList pk;
        for (long long p : pm->picked)
            pk << QString::number(p);
        QByteArray lb;
        for (const auto &c : pm->lut)
            lb.append(reinterpret_cast<const char *>(c.data()), 3);
        out << u"%1 pmap %2 picked %3 view %4 %5 %6 lut %7 tip %8"_s.arg(
            tag, vis(pm), pk.join(u','), py::fmt_f(pm->scale, 3), py::fmt_f(pm->ox, 3), py::fmt_f(pm->oy, 3),
            !pm->lut.empty() ? h(lb) : u"-"_s, pm->toolTip());
    };

    snap(u"start"_s);
    for (int k = 0; k < page.pops->count(); ++k) {
        page.pops->setCurrentRow(k);
        snap(u"pop %1"_s.arg(k));
        const std::optional<QString> first = page.head;
        QStringList hs;
        for (const auto &[hd, _b] : page.head_btn)
            hs << hd;
        for (qsizetype i = 1; i < hs.size(); ++i) {
            page._set_head(hs[i]);
            snap(u"pop %1 head %2"_s.arg(k).arg(hs[i]));
        }
        page._set_head(first.value_or(QString()));
    }
    const QStringList tys = page.types();
    std::optional<QString> best;
    size_t bn = 0;
    for (const QString &t : tys)
        if (!best || page.into_blocks(t).size() > bn) {
            best = t;
            bn = page.into_blocks(t).size();
        }
    if (best) {
        page._set_pop(*best);
        snap(u"best"_s);
        for (int i : {1, 2})
            if (i < page.targets->count()) {
                page._set_target(page.targets->itemData(i).toString());
                snap(u"source %1"_s.arg(i));
            }
        page._set_target(std::nullopt);
        if (!page.chart->_boxes.empty()) {
            const long long i = page.chart->_boxes[0].i;
            page._card_clicked(i);
            snap(u"toggle"_s);
            page._card_clicked(i);
            page._pick(i);
            snap(u"picked card"_s);
            page._pick(-1);
            snap(u"middle picked"_s);
        }
        if (!page.chart->_lads.empty()) {
            const Ladder L = page.chart->_lads[0];
            page._ladder(L.idx, ladder_ticks(L, std::min<long long>(1, static_cast<long long>(L.idx.size()) - 1)));
            snap(u"ladder"_s);
            page.chart->open.insert(0);
            page.chart->_layout();
            snap(u"ladder open"_s);
            page._ladder(L.idx, {});
            snap(u"ladder cleared"_s);
        }
        if (!page.tiers().empty()) {
            page._focus_tier(0);
            snap(u"focus 0"_s);
            page.chart->focus_open = true;
            page.chart->_layout();
            snap(u"focus open"_s);
            page._focus_tier(-1);
        }
        page._set_months(300);
        snap(u"25 years"_s);
        page._set_months(12);
        page.size_edit->setText(u"5,000"_s);
        page._size_done();
        page.share_edit->setText(u"50"_s);
        page._share_done();
        snap(u"size share"_s);
        page.case_panel->more_btn->setChecked(true);
        page._show();
        snap(u"every setting"_s);
        if (!page.case_panel->sliders.isEmpty()) {
            const QString k = page.case_panel->sliders.begin()->first;
            QSlider *sl = page.case_panel->sliders.begin()->second.first;
            sl->setValue(sl->maximum());
            page._timer->stop();
            page._show();
            snap(u"slider "_s + k);
        }
        if (!page.case_panel->fact_btn.isEmpty()) {
            page.case_panel->fact_btn.begin()->second->click();
            page._timer->stop();
            page._show();
            snap(u"fact"_s);
        }
        if (page.case_panel->nv->count() > 1) {
            page.case_panel->nv->setCurrentIndex(1);
            page.case_panel->_nv_picked(1);
            page._timer->stop();
            page._show();
            snap(u"national value"_s);
        }
        page.case_panel->_reset();
        page._timer->stop();
        page.case_panel->more_btn->setChecked(false);
        page._show();
        snap(u"reset"_s);
        page._set_dir(u"out"_s);
        snap(u"out"_s);
        if (page.targets->count() > 1) {
            page._set_target(page.targets->itemData(1).toString());
            snap(u"out target 1"_s);
        }
        // the edits (recorded, not applied)
        page._set_dir(u"in"_s);
        page._set_target(std::nullopt);
        if (!page.chart->_boxes.empty()) {
            const long long i = page.chart->_boxes[0].i;
            page._pick(i);
            page.f_edit->setText(u"0.75"_s);
            page._factor_done();
            page._pick(i);
            page.c_edit->setPlainText(u"literacy = 0.4\nmilitancy = 2"_s);
            page._cond_done();
            page._pick(i);
            page._remove();
            snap(u"armed"_s);
            page._remove();
        }
        page.base_edit->setText(u"0.02"_s);
        page._base_done();
        page.a_factor->setText(u"x"_s);
        page._add();
        snap(u"bad factor"_s);
        page.a_factor->setText(u"1.5"_s);
        page.quick->setCurrentIndex(1);
        page._quick(1);
        page._add();
        snap(u"added"_s);
        if (page.targets->count() > 1) {
            page._set_target(page.targets->itemData(1).toString());
            if (!page.chart->_boxes.empty()) {
                page._pick(page.chart->_boxes[0].i);
                page.f_edit->setText(u"-3"_s);
                page._factor_done();
            }
            snap(u"one source edit"_s);
            page._set_target(std::nullopt);
        }
    }
    page._set_pop(u"pop_types"_s);
    for (const QString c : {u"middle"_s, u"rich"_s, u"poor"_s}) {
        page._set_class(c);
        snap(u"every pop "_s + c);
    }
    // a real place
    page._set_source(u"province"_s);
    snap(u"province source"_s);
    std::optional<long long> pid;
    std::vector<long long> ps;
    for (const auto &[p, _v] : win.doc->pops)
        ps.push_back(p);
    std::sort(ps.begin(), ps.end());
    for (long long p : ps) {
        const moddata::Province *pr = m->provinces.get(p);
        long long n = 0;
        for (const moddata::Pop &x : *win.doc->pops.get(p))
            n += x.size;
        if (pr && !pr->sea && n > 0 && win.doc->prov_state(p).owner && !win.doc->prov_state(p).owner->isEmpty()) {
            pid = p;
            break;
        }
    }
    if (pid) {
        page.pmap->picked = {*pid};
        page._place_picked();
        snap(u"place %1"_s.arg(*pid));
        for (const QString hd : {u"assimilation_chance"_s, u"conversion_chance"_s, u"promotion_chance"_s})
            if (page.head_btn.contains(hd)) {
                page._set_head(hd);
                snap(u"place "_s + hd);
            }
        if (best) {
            page._set_pop(*best);
            snap(u"place best"_s);
            page._set_months(300);
            snap(u"place 25 years"_s);
            page._set_months(12);
        }
        page._set_source(u"state"_s);
        snap(u"state source"_s);
        page._place_hover(static_cast<int>(*pid));
        out << u"hover "_s + page.pmap->toolTip();
    }
    page._set_source(u"made"_s);
    snap(u"made again"_s);
    page.show_place(best.value_or(u"pop_types"_s), u"promote_to"_s, QString(), u"in"_s);
    snap(u"show_place"_s);
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
