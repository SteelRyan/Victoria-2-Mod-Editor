// The Nation page in the main window against Python, the C++ side of tools/check_nationpage.py.
// Usage: med_nationpage_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/countryhist.h"
#include "core/document.h"
#include "core/localisation.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/scriptstore.h"
#include "map/mapview_data.h"
#include "ui/colorwheel.h"
#include "ui/dialogs.h"
#include "ui/mapview.h"
#include "ui/nationextras.h"
#include "ui/nationpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::v;
using OptStr = std::optional<QString>;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
static QString h(const void *p, size_t n)
{
    return h(QByteArray::fromRawData(static_cast<const char *>(p), qsizetype(n)));
}

static constexpr int MAP_W = 1000, MAP_H = 700, SIDE_W = 470;

static QString key(const OptStr &k) { return k ? *k : u"None"_s; }

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
    // (no changelog in the port yet: Python's is pointed into its side's own folder and not compared)
    win.resize(1500, 900);
    win.show();
    win.side_scroll->setFixedWidth(SIDE_W);
    win.map->setFixedSize(MAP_W, MAP_H);
    QApplication::processEvents();
    win._check_timer->stop();
    win.map->fit();
    QApplication::processEvents();
    document::Document &d = *win.doc;
    nationpanel::NationPanel *np = win.nations_panel;
    nationextras::PartiesBox *pb = np->parties_box;
    nationextras::FlagsBox *fb = np->flags_box;
    const Qt::KeyboardModifiers nomod = Qt::NoModifier;
    auto &nat = d.map<countryhist::NationEdits>(u"nations"_s);

    auto nations = [&] {
        QStringList ts;
        for (const auto &[t, _s] : nat.want)
            ts << t;
        std::sort(ts.begin(), ts.end());
        QStringList lines;
        for (const QString &t : ts) {
            const countryhist::NationState &st = *nat.want.get(t);
            QStringList sc, rf, uhs;
            for (const QString &k : countryhist::SCALARS)
                sc << k + u"="_s + key(st.at(k));
            std::vector<std::pair<QString, QString>> r(st.reforms.begin(), st.reforms.end()),
                u(st.upper_house.begin(), st.upper_house.end());
            std::sort(r.begin(), r.end());
            std::sort(u.begin(), u.end());
            QSet<QString> unciv;                       // (the unciv reforms: C++ only, not in the Python editor's states)
            for (const countryhist::Reform &x : nat.hist.data.unciv)
                unciv.insert(x.issue);
            for (const auto &[a, b] : r)
                if (!unciv.contains(a))
                    rf << a + u"="_s + b;
            for (const auto &[a, b] : u)
                uhs << a + u"="_s + b;
            QStringList te(st.techs.begin(), st.techs.end()), in(st.inventions.begin(), st.inventions.end());
            std::sort(te.begin(), te.end());
            std::sort(in.begin(), in.end());
            lines << u"%1 %2 | %3 | %4 | %5 | %6 | %7"_s.arg(t, sc.join(u';'), st.cultures.join(u','), rf.join(u';'),
                                                         te.join(u','), in.join(u','), uhs.join(u';'));
        }
        return h(lines.join(u'\n').toUtf8());
    };
    auto scripts = [&] {
        auto &store = d.map<scriptstore::ScriptStore>(u"scripts"_s);
        QStringList rs = store.dirty();
        std::sort(rs.begin(), rs.end());
        QStringList parts;
        for (const QString &r : rs)
            parts << r + u":"_s + h(store.text(r).toUtf8());
        return h(parts.join(u';').toUtf8());
    };
    auto names = [&] {
        auto &loc = d.map<localisation::LocEdits>(u"loc"_s);
        QStringList ks;
        for (const auto &[k, _t] : loc.fresh)
            ks << k;
        std::sort(ks.begin(), ks.end());
        QStringList parts;
        for (const QString &k : ks)
            parts << k + u"="_s + loc.fresh.get(k)->first;
        return h(parts.join(u';').toUtf8());
    };
    auto fresh = [&] {
        auto &nn = d.map<countryhist::NewNations>(u"new_nations"_s);
        QStringList ts;
        for (const auto &[t, _a] : nn.added)
            ts << t;
        std::sort(ts.begin(), ts.end());
        QStringList parts;
        for (const QString &t : ts) {
            const countryhist::NewNation &a = *nn.added.get(t);
            parts << u"%1:%2:%3:%4,%5,%6:%7"_s.arg(t, a.name, a.adj)
                         .arg(std::get<0>(a.color))
                         .arg(std::get<1>(a.color))
                         .arg(std::get<2>(a.color))
                         .arg(a.template_);
        }
        return parts.join(u';');
    };
    auto common = [&](const QString &tag) {
        out << u"%1 | mode %2 | tab %3 %4 | nation %5 | msg %6 | %7 | steps %8 | nat %9 | scr %10 | loc %11 | new %12"
               u" | hl %13"_s.arg(tag, win.mode, win.tab, v(win.stack->currentWidget() == np), key(win.nation_tag),
                                  v(win.st_msg->text()), win.st_pending->text(), QString::number(d.undo_stack.size()),
                                  nations())
                   .arg(scripts(), names(), fresh(), h(win.map->hl.data(), win.map->hl.size()));
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
    auto slider = [](const widgets::SliderField *sf) { return u"%1 %2"_s.arg(v(sf->value()), v(sf->isEnabled())); };
    auto pix = [](const QPixmap &p) { return !p.isNull() ? u"%1x%2"_s.arg(p.width()).arg(p.height()) : u"none"_s; };
    auto nsnap = [&](const QString &tag) {
        common(tag);
        out << u"%1 head %2 | %3 | %4 | %5 | new %6"_s.arg(tag, combo(np->picker), v(np->title->text()), v(np->sub->text()),
                                                          v(np->note->text()), v(np->new_btn->isVisibleTo(np)));
        QStringList ne;
        for (const auto &[k, el] : np->name_edits)
            ne << u"%1 %2 %3 %4"_s.arg(k, v(el.first->text()), v(el.first->isEnabled()), v(el.first->placeholderText()));
        out << u"%1 names %2 | gov %3 %4 [%5] | %6"_s.arg(tag, ne.join(u';'), combo(np->gov_pick),
                                                         v(np->gov_pick->isEnabled()), items(np->gov_pick),
                                                         v(np->name_where->text()));
        out << u"%1 shown %2 %3 %4 | %5"_s.arg(tag, v(np->start_section->isVisibleTo(np)),
                                               v(np->reform_section->isVisibleTo(np)),
                                               v(np->tech_section->isVisibleTo(np)), v(np->start_title->text()));
        QStringList chips;
        for (int i = 0; i < np->accepted_row->count(); ++i) {
            QWidget *w = np->accepted_row->itemAt(i)->widget();
            if (auto *b = qobject_cast<QAbstractButton *>(w))
                chips << b->text();
            else if (auto *l = qobject_cast<QLabel *>(w))
                chips << l->text();
        }
        out << u"%1 start cap %2 | %3 | prim %4 | acc %5 | add %6 | rel %7 | gov %8"_s.arg(
            tag, combo(np->capital), v(np->capital->isEnabled()), combo(np->primary), chips.join(u';'),
            combo(np->add_culture), combo(np->religion), combo(np->government));
        out << u"%1 nv %2 | pic %3 | school %4 | %5 | party %6 [%7] | elec %8 | civ %9 %10"_s.arg(
            tag, combo(np->nv), pix(np->nv_pic->pixmap()), combo(np->school), v(np->school_note->text()),
            combo(np->party), items(np->party), v(np->election->text()), v(np->civilized->isChecked()))
                   .arg(v(np->civilized->isEnabled()));
        QStringList sl, uhs;
        for (const auto &[k, sf] : np->sliders)
            sl << k + u" "_s + slider(sf);
        for (const auto &[k, sf] : np->uh)
            uhs << k + u" "_s + slider(sf);
        out << u"%1 sliders %2 | uh %3 | %4"_s.arg(tag, sl.join(u';'), uhs.join(u';'), v(np->uh_total->text()));
        QStringList rf;
        for (const auto &[k, cb] : np->reform_boxes)
            rf << k + u" "_s + combo(cb);
        out << u"%1 reforms %2"_s.arg(tag, rf.join(u';'));
        QStringList own(np->tree->owned.begin(), np->tree->owned.end());
        std::sort(own.begin(), own.end());
        out << u"%1 techs %2 | folder %3 | owned %4 %5 | edit %6"_s.arg(tag, v(np->tech_title->text()))
                   .arg(np->tree->folder)
                   .arg(np->tree->owned.size())
                   .arg(h(own.join(u',').toUtf8()), v(np->tree->enabled_edit));
        out << u"%1 parties %2 | %3 | %4 | gc %5 | %6 | win %7 | live %8"_s.arg(
            tag, v(pb->isVisibleTo(np)), v(pb->file_note->text()), pb->color_btn->color, combo(pb->gc_box),
            v(pb->summary->text()), v(pb->win_btn->isVisibleTo(np)), v(pb->live->isChecked()));
        QStringList th;
        for (size_t i = 0; i < fb->variants.size(); ++i) {
            const nationextras::_Thumb *t = fb->thumbs[i].second;
            th << u"%1 %2 %3 %4"_s.arg(fb->variants[i].first, v(t->text()), pix(t->pixmap()), v(t->toolTip()));
        }
        out << u"%1 flags %2 | %3 | %4 | open %5"_s.arg(tag, v(fb->isVisibleTo(np)), th.join(u';'), v(fb->note->text()),
                                                       v(fb->open_btn->isEnabled()));
    };
    auto data_of = [](const QComboBox *cb, int i) -> dialogs::Data {
        if (auto *pc = qobject_cast<const dialogs::PickCombo *>(cb))
            return pc->data_at(i);
        const QVariant d = cb->itemData(i);
        return d.isValid() ? dialogs::Data(d.toString()) : std::nullopt;
    };
    auto real = [](const dialogs::Data &dd) { return dd && *dd != dialogs::HEADER && *dd != dialogs::NEW; };
    auto choose = [&](QComboBox *cb) {
        for (int i = 0; i < cb->count(); ++i)
            if (i != cb->currentIndex() && real(data_of(cb, i))) {
                cb->setCurrentIndex(i);
                break;
            }
        Q_EMIT cb->activated(cb->currentIndex());
    };
    auto pick = [](dialogs::PickCombo *cb, const dialogs::Data &data) {
        cb->setCurrentIndex(cb->find(data));
        Q_EMIT cb->activated(cb->currentIndex());
    };
    auto flush = [&] {
        np->_timer->stop();
        np->_flush();
    };
    auto name_ed = [&](const QString &k) -> QLineEdit * {
        for (const auto &[kk, el] : np->name_edits)
            if (kk == k)
                return el.first;
        return nullptr;
    };

    countryhist::CountryHistory &hist = *m->country_hist;
    countryhist::NationData &data = *m->nation_data;
    QStringList tags;
    for (const auto &[t, _c] : m->countries)
        tags << t;
    std::sort(tags.begin(), tags.end());
    std::map<QString, std::vector<long long>> owned_of;
    for (const QString &t : tags)
        owned_of[t] = nationpanel::stats_owned(d, *m, t);
    QStringList cands;
    for (const QString &t : tags)
        if (hist.files.contains(t) && owned_of[t].size() >= 3 && data.parties(t).size() >= 2 &&
            !m->countries.get(t)->file.isEmpty())
            cands << t;
    const QString tag = cands[0], tag2 = cands[1];
    const std::vector<long long> own = owned_of[tag];
    out << u"nation %1 (%2 provinces) other %3"_s.arg(tag).arg(own.size()).arg(tag2);
    win.set_tab(u"nations"_s);
    QApplication::processEvents();
    nsnap(u"start"_s);
    // --- a nation clicked on the map
    win.on_click(own[0], nomod);
    QApplication::processEvents();
    nsnap(u"one"_s);
    np->grab().save(QDir(out_dir).filePath(u"side_nation.png"_s));
    // --- another nation picked in the list, and back
    pick(np->picker, tag2);
    nsnap(u"picked"_s);
    win.on_click(own.back(), nomod);
    nsnap(u"back"_s);
    // --- names
    for (const auto &[k, text] : {std::pair{u"name"_s, u"Testland"_s}, std::pair{u"adj"_s, u"Testish"_s}}) {
        QLineEdit *ed = name_ed(k);
        ed->setText(text);
        Q_EMIT ed->editingFinished();
        nsnap(u"named "_s + k);
    }
    choose(np->gov_pick);
    nsnap(u"gov view"_s);
    {
        QLineEdit *ed = name_ed(u"gov"_s);
        ed->setText(u"Testland Commune"_s);
        Q_EMIT ed->editingFinished();
    }
    nsnap(u"gov named"_s);
    // --- the capital
    choose(np->capital);
    nsnap(u"capital list"_s);
    np->cap_pick->click();
    common(u"cap pick"_s);
    win.on_click(owned_of[tag2][0], nomod);
    nsnap(u"cap refused"_s);
    np->cap_pick->click();
    win.on_click(own[1], nomod);
    nsnap(u"cap map"_s);
    np->cap_pick->click();
    win.escape();
    nsnap(u"cap esc"_s);
    // --- the start
    choose(np->primary);
    nsnap(u"primary"_s);
    {
        const QStringList have = nat.state(tag).cultures;
        for (int i = 0; i < np->add_culture->count(); ++i) {
            const dialogs::Data k = np->add_culture->data_at(i);
            if (real(k) && !have.contains(*k)) {
                pick(np->add_culture, k);
                break;
            }
        }
    }
    nsnap(u"accepted"_s);
    for (int i = 0; i < np->accepted_row->count(); ++i)
        if (auto *b = qobject_cast<QPushButton *>(np->accepted_row->itemAt(i)->widget())) {
            b->click();
            break;
        }
    nsnap(u"unaccepted"_s);
    for (const auto &[name, cb] : std::vector<std::pair<QString, QComboBox *>>{{u"religion"_s, np->religion},
                                                                              {u"government"_s, np->government},
                                                                              {u"nv"_s, np->nv},
                                                                              {u"school"_s, np->school},
                                                                              {u"party"_s, np->party}}) {
        choose(cb);
        nsnap(name);
    }
    np->election->setText(u"1830.4.2"_s);
    Q_EMIT np->election->editingFinished();
    nsnap(u"election"_s);
    np->civilized->click();
    nsnap(u"civilized"_s);
    // --- sliders: one step once settled
    {
        widgets::SliderField *sf = np->slider(u"prestige"_s);
        sf->setValue(55);
        Q_EMIT sf->valueChanged(sf->value());
        flush();
    }
    nsnap(u"prestige"_s);
    {
        widgets::SliderField *sf = np->uh_slider(u"liberal"_s);
        sf->setValue(60);
        Q_EMIT sf->valueChanged(sf->value());
        flush();
    }
    nsnap(u"upper house"_s);
    // --- a reform
    choose(np->reform_boxes.front().second);
    nsnap(u"reform"_s);
    // --- technologies
    {
        const QStringList line = data.area_techs.value(data.folders[0].second[0]);
        Q_EMIT np->tree->clicked(line[2]);
        nsnap(u"techs given"_s);
        const QSet<QString> has = nat.state(tag).techs;
        qsizetype last = -1;
        for (qsizetype i = 0; i < line.size(); ++i)
            if (has.contains(line[i]))
                last = i;
        Q_EMIT np->tree->clicked(line[last]);
        nsnap(u"tech taken"_s);
    }
    np->tree->folder = 1;
    np->tree->update();
    QApplication::processEvents();
    np->tree->grab().save(QDir(out_dir).filePath(u"techtree.png"_s));
    // --- parties box: map colour, graphical culture
    Q_EMIT pb->color_btn->changed(u"#3c6e91"_s);
    nsnap(u"colour"_s);
    pb->gc_box->setCurrentIndex(pb->gc_box->count() > 2 ? 2 : 0);
    Q_EMIT pb->gc_box->activated(pb->gc_box->currentIndex());
    nsnap(u"graphical culture"_s);
    // --- a new nation from this one
    QTimer::singleShot(0, [] {
        QWidget *dlg = QApplication::activeModalWidget();
        if (!dlg)
            return;
        const auto eds = dlg->findChildren<QLineEdit *>();
        const QStringList texts{u"zq1"_s, u"Zequa"_s, u"Zequan"_s};
        for (qsizetype i = 0; i < eds.size() && i < texts.size(); ++i)
            eds[i]->setText(texts[i]);
        dlg->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    np->new_btn->click();
    QApplication::processEvents();
    nsnap(u"new nation"_s);
    // --- undo
    for (int i = 0; i < 3; ++i)
        out << u"undo "_s + v(win.undo());
    win.show_nation(tag);
    nsnap(u"undone"_s);
    // --- G4
    {
        const bool ok = win.save(false);
        out << u"apply %1 | msg %2 | %3"_s.arg(v(ok), v(win.st_msg->text()), win.st_pending->text());
    }
    nsnap(u"applied"_s);
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
