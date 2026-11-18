// The Provinces & states page in the main window against Python, the C++ side of tools/check_provpage.py.
// Usage: med_provpage_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/localisation.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "map/mapview_data.h"
#include "ui/dialogs.h"
#include "ui/mapview.h"
#include "ui/provpanel.h"
#include "ui/statepanel.h"
#include "ui/theme.h"
#include "ui/utilpanel.h"
#include "ui/widgets.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QTreeWidget>
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

static QString key(const std::optional<QString> &k) { return k ? *k : u"None"_s; }

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
    provpanel::ProvincePanel *pp = win.prov_panel;
    utilpanel::UtilityPanel *up = win.util_panel;
    statepanel::StatePanel *sp = win.state_panel;
    const Qt::KeyboardModifiers nomod = Qt::NoModifier, ctrl = Qt::ControlModifier;

    auto hist = [&] {
        std::vector<long long> ids;
        for (const auto &[pid, _s] : d.prov_want)
            ids.push_back(pid);
        std::sort(ids.begin(), ids.end());
        QStringList lines;
        for (long long pid : ids) {
            const history::ProvState &st = *d.prov_want.get(pid);
            QStringList fs;
            for (const auto &f : st.factories)
                fs << u"%1:%2:%3"_s.arg(key(f.building)).arg(f.level).arg(v(f.upgrade));
            lines << u"%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12"_s.arg(pid)
                         .arg(key(st.owner), key(st.controller), st.cores.join(u','),
                              st.life_rating ? QString::number(*st.life_rating) : u"None"_s, key(st.trade_goods))
                         .arg(st.colonial)
                         .arg(st.fort)
                         .arg(st.naval_base)
                         .arg(st.railroad)
                         .arg(fs.join(u';'), st.flags.join(u','));
        }
        return h(lines.join(u'\n').toUtf8());
    };
    auto regions = [](const OrderedMap<QString, moddata::Region> &rs) {
        QStringList ks;
        for (const auto &[rid, _r] : rs)
            ks << rid;
        std::sort(ks.begin(), ks.end());
        QStringList parts;
        for (const QString &rid : ks) {
            QStringList ps;
            for (long long p : rs.get(rid)->provinces)
                ps << QString::number(p);
            parts << rid + u":"_s + ps.join(u',');
        }
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
    auto page = [&] {
        const QWidget *w = win.prov_stack->currentWidget();
        return w == pp ? u"prov"_s : w == up ? u"util"_s : w == sp ? u"states"_s : u"other"_s;
    };
    auto common = [&](const QString &tag) {
        out << u"%1 | mode %2 | page %3 %4 | msg %5 | %6 | steps %7 | hist %8 | reg %9 | util %10 | loc %11 | hl %12"_s
                   .arg(tag, win.mode, page(), v(win.stack->currentWidget() == win.prov_host), v(win.st_msg->text()),
                        win.st_pending->text(), QString::number(d.undo_stack.size()), hist(), regions(m->regions))
                   .arg(regions(m->utility), names(), h(win.map->hl.data(), win.map->hl.size()));
    };
    auto combo = [](const QComboBox *cb) {
        return u"%1/%2 %3"_s.arg(cb->currentIndex()).arg(cb->count()).arg(v(cb->currentText()));
    };
    auto texts = [](const QWidget *host) {
        QStringList r;
        for (const QPushButton *b : host->findChildren<QPushButton *>())
            r << b->text() + (b->isEnabled() ? QString() : u"(off)"_s);
        return r.join(u';');
    };
    auto psnap = [&](const QString &tag) {
        common(tag);
        out << u"%1 head %2 | %3 | %4"_s.arg(tag, v(pp->title->text()), v(pp->sub->text()), v(pp->note->text()));
        out << u"%1 names %2 %3 | %4 %5 | %6 %7"_s.arg(tag, v(pp->name_edit->text()), v(pp->name_edit->isEnabled()),
                                                     v(pp->state_name_edit->text()), v(pp->state_name_edit->isEnabled()),
                                                     v(pp->name_where->text()), v(pp->name_where->isVisibleTo(pp)));
        out << u"%1 state %2 | cap %3 %4 %5 | warn %6 %7"_s.arg(
            tag, v(pp->state_line->text()), v(pp->cap_btn->isVisibleTo(pp)), v(pp->cap_btn->isEnabled()),
            v(pp->cap_pops->isVisibleTo(pp)), v(pp->state_warn->text()), v(pp->state_warn->isVisibleTo(pp)));
        out << u"%1 own %2 | %3 | cores %4 | add %5"_s.arg(tag, combo(pp->owner), combo(pp->controller),
                                                         texts(pp->cores_host), combo(pp->add_core));
        QStringList mb;
        for (const auto &[k, cb] : pp->member_boxes)
            mb << u"%1 %2 %3"_s.arg(k, combo(cb), v(cb->isEnabled()));
        out << u"%1 land %2 | %3 | %4 %5 | %6"_s.arg(tag, v(pp->lr_value->text()), combo(pp->rgo), combo(pp->colonial),
                                                    v(pp->status_note->text()), mb.join(u';'));
        out << u"%1 mwarn %2 %3 | island %4 %5 %6"_s.arg(tag, v(pp->member_warn->text()),
                                                        v(pp->member_warn->isVisibleTo(pp)))
                   .arg(int(pp->island_cb->checkState()))
                   .arg(v(pp->island_cb->isEnabled()), v(pp->island_note->text()));
        QStringList lv;
        for (const auto &[k, r] : pp->level_rows)
            lv << u"%1 %2 %3 %4 %5"_s.arg(k, v(r.val->text()), v(r.why->text()), v(r.minus->isEnabled()),
                                          v(r.plus->isEnabled()));
        out << u"%1 levels %2"_s.arg(tag, lv.join(u';'));
        QStringList rows;
        for (int i = 0; i < pp->fact_lay->count(); ++i) {
            QWidget *w = pp->fact_lay->itemAt(i)->widget();
            if (!w)
                continue;
            QString t;
            if (auto *l = qobject_cast<QLabel *>(w))
                t = l->text();
            else if (auto *b = qobject_cast<QAbstractButton *>(w))
                t = b->text();
            rows << t + (w->isEnabled() ? QString() : u"(off)"_s);
        }
        out << u"%1 facts %2 %3 | %4 | %5 | add %6 %7 | warn %8"_s.arg(
            tag, v(pp->fact_section->isVisibleTo(pp)), v(pp->fact_title->text()), v(pp->fact_title->styleSheet()),
            rows.join(u';'), combo(pp->add_fact), v(pp->add_fact->isEnabled()), v(pp->fact_warn->text()));
        out << u"%1 later %2 %3"_s.arg(tag, v(pp->later_section->isVisibleTo(pp)), v(pp->later->text()));
    };
    auto usnap = [&](const QString &tag) {
        common(tag);
        QStringList items;
        for (int i = 0; i < up->lst->count(); ++i)
            items << up->lst->item(i)->text() + (up->lst->item(i)->isSelected() ? u"*"_s : QString());
        out << u"%1 list %2 | %3 | none %4 | %5"_s.arg(tag, v(up->list_title->text()), items.join(u';'),
                                                       v(up->none_note->isVisibleTo(up)))
                   .arg(up->lst->height());
        out << u"%1 view %2 | %3 | %4 | %5 %6 | edit %7 %8 | %9 %10 %11"_s.arg(
            tag, v(up->view_section->isVisibleTo(up)), v(up->v_title->text()), v(up->v_sub->text()),
            v(up->v_warn->text()), v(up->v_warn->isVisibleTo(up)), v(up->edit_btn->isChecked()),
            v(up->edit_hint->isVisibleTo(up)), v(up->add_sel->text()))
                   .arg(v(up->add_sel->isEnabled()), v(up->rm_sel->text()));
        QStringList sg;
        for (int i = 0; i < up->sugg->count(); ++i)
            sg << up->sugg->item(i)->text();
        out << u"%1 sugg %2 %3 | %4 | all %5 | pick %6"_s.arg(tag, v(up->sugg_section->isVisibleTo(up)),
                                                              v(up->sugg_title->text()), sg.join(u';'),
                                                              v(up->sugg_all->isVisibleTo(up)), combo(up->pick));
    };
    auto tree_rows = [](const QTreeWidget *t, int n = -1) {
        QStringList rows;
        const int k = n < 0 ? t->topLevelItemCount() : std::min(n, t->topLevelItemCount());
        for (int i = 0; i < k; ++i) {
            const QTreeWidgetItem *it = t->topLevelItem(i);
            QStringList cols;
            for (int c = 0; c < t->columnCount(); ++c)
                cols << it->text(c);
            rows << cols.join(u'|') + u"|"_s + it->foreground(0).color().name();
        }
        return rows.join(u';');
    };
    auto ssnap = [&](const QString &tag) {
        common(tag);
        const QTreeWidgetItem *cur = sp->tree->currentItem();
        out << u"%1 src %2 | count %3 | rows %4 | cur %5 | %6"_s.arg(tag, v(sp->src->text()), v(sp->count->text()))
                   .arg(sp->tree->topLevelItemCount())
                   .arg(cur ? v(cur->text(0)) : u"None"_s, tree_rows(sp->tree, 6));
        out << u"%1 ed %2 %3 | %4 | name %5 | %6"_s.arg(tag, v(sp->ed->isEnabled()), v(sp->ed_title->text()),
                                                       v(sp->ed_note->text()), v(sp->name->text()),
                                                       v(sp->name_where->text()));
        out << u"%1 members %2 | cap %3 | add %4"_s.arg(tag, tree_rows(sp->members), v(sp->cap_btn->isEnabled()),
                                                      v(sp->add_btn->isChecked()));
        out << u"%1 del %2 %3 | target %4 %5 | refs %6 %7 | fix %8 | warn %9"_s.arg(
            tag, v(sp->del_sec->isEnabled()), v(sp->del_info->text()), combo(sp->target), v(sp->target->isVisibleTo(sp)),
            v(sp->refs_info->text()), QString::number(sp->refs->topLevelItemCount()), v(sp->fix_refs->isVisibleTo(sp)))
                   .arg(v(sp->del_warn->text()));
    };
    auto accept_next = [] {
        QTimer::singleShot(0, [] {
            if (auto *dlg = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dlg->accept();
        });
    };
    auto pick = [](dialogs::PickCombo *cb, const dialogs::Data &data) {
        cb->setCurrentIndex(cb->find(data));
        Q_EMIT cb->activated(cb->currentIndex());
    };
    auto first_key = [](const dialogs::PickCombo *cb) {
        for (int i = 0; i < cb->count(); ++i) {
            const dialogs::Data dd = cb->data_at(i);
            if (dd && *dd != dialogs::HEADER && *dd != dialogs::NEW)
                return i;
        }
        return -1;
    };
    auto btn = [](const QWidget *host, const QString &text) -> QPushButton * {
        for (QPushButton *b : host->findChildren<QPushButton *>())
            if (b->text() == text)
                return b;
        return nullptr;
    };
    auto owner = [&](long long p) { return d.prov_state(p).owner; };
    auto has_owner = [&](long long p) {
        const auto o = owner(p);
        return o && !o->isEmpty();
    };

    std::vector<long long> land;
    for (const auto &[p, pr] : m->provinces)
        if (!pr.sea && p < md->n && md->area[size_t(p)] > 0)
            land.push_back(p);
    std::sort(land.begin(), land.end());
    auto region_of = [&](long long p) { return m->provinces.get(p)->region.value_or(QString()); };
    std::vector<long long> cands;
    for (long long p : land) {
        const QString r = region_of(p);
        if (!r.isEmpty() && m->regions.get(r)->provinces.size() >= 3 && d.hist->main_file(p) && has_owner(p))
            cands.push_back(p);
    }
    std::stable_sort(cands.begin(), cands.end(), [&](long long a, long long b) {
        const auto na = -static_cast<long long>(d.state_factories(region_of(a)).size());
        const auto nb = -static_cast<long long>(d.state_factories(region_of(b)).size());
        return std::make_pair(na, a) < std::make_pair(nb, b);
    });
    const long long src = cands[0];
    const QString rid = region_of(src);
    std::vector<long long> mates;
    for (long long q : m->regions.get(rid)->provinces)
        if (q != src)
            mates.push_back(q);
    long long other = 0;
    for (long long p : land) {
        const QString r = region_of(p);
        if (!r.isEmpty() && r != rid && has_owner(p) && m->regions.get(r)->provinces.size() >= 2) {
            other = p;
            break;
        }
    }
    const QString orid = region_of(other);
    QStringList tags;
    {
        QStringList all;
        for (const auto &[t, _c] : m->countries)
            all << t;
        std::sort(all.begin(), all.end());
        for (const QString &t : all)
            if (!m->countries.get(t)->dynamic && OptStr(t) != owner(src))
                tags << t;
    }
    out << u"source %1 region %2 mates %3 other %4 %5 tags %6"_s.arg(src).arg(rid, v(mates)).arg(other).arg(
        orid, v(tags.mid(0, 3)));
    win.set_tab(u"provinces"_s);
    win.set_prov_sub(u"provinces"_s);
    QApplication::processEvents();
    psnap(u"start"_s);
    // --- one province
    win.on_click(src, nomod);
    QApplication::processEvents();
    psnap(u"one"_s);
    pp->grab().save(QDir(out_dir).filePath(u"side_prov.png"_s));
    // --- ownership
    pick(pp->owner, tags[0]);
    psnap(u"owner"_s);
    pick(pp->controller, tags[1]);
    psnap(u"controller"_s);
    pick(pp->add_core, tags[2]);
    psnap(u"core added"_s);
    pp->cores_host->findChildren<QPushButton *>()[0]->click();
    psnap(u"core removed"_s);
    btn(pp, u"Add owner as core"_s)->click();
    psnap(u"owner core"_s);
    // --- land
    for (const QString &t : {u"+5"_s, u"-1"_s}) {
        btn(pp, t)->click();
        psnap(u"lr "_s + t);
    }
    {
        const OptStr cur = d.prov_state(src).trade_goods;
        for (const auto &it : provpanel::goods_items(*m))
            if (OptStr(it.key) != cur) {
                pick(pp->rgo, it.key);
                break;
            }
    }
    psnap(u"rgo"_s);
    pp->colonial->setCurrentIndex(1);
    Q_EMIT pp->colonial->activated(1);
    psnap(u"colony"_s);
    {
        QComboBox *cb = pp->member_boxes[0].second;
        for (int i = 0; i < cb->count(); ++i)
            if (i != cb->currentIndex()) {
                cb->setCurrentIndex(i);
                break;
            }
        Q_EMIT cb->activated(cb->currentIndex());
    }
    psnap(u"climate"_s);
    // --- buildings
    auto row = [&](const QString &k) -> provpanel::ProvincePanel::LevelRow & {
        for (auto &[kk, r] : pp->level_rows)
            if (kk == k)
                return r;
        return pp->level_rows[0].second;
    };
    for (const QString &k : {u"fort"_s, u"railroad"_s, u"naval_base"_s}) {
        row(k).plus->click();
        psnap(k + u" +"_s);
    }
    row(u"fort"_s).minus->click();
    psnap(u"fort -"_s);
    // --- factories
    pick(pp->add_fact, pp->add_fact->data_at(first_key(pp->add_fact)));
    psnap(u"factory added"_s);
    for (QPushButton *b : pp->fact_host->findChildren<QPushButton *>())
        if (b->objectName() == u"stepPlus") {
            b->click();
            break;
        }
    psnap(u"factory raised"_s);
    {
        QPushButton *last = nullptr;
        for (QPushButton *b : pp->fact_host->findChildren<QPushButton *>())
            if (b->objectName() == u"icon")
                last = b;
        last->click();
    }
    psnap(u"factory removed"_s);
    // --- island, names
    pp->island_cb->click();
    psnap(u"island"_s);
    pp->name_edit->setText(u"Testhaven"_s);
    Q_EMIT pp->name_edit->editingFinished();
    psnap(u"named"_s);
    pp->state_name_edit->setText(u"Teststate"_s);
    Q_EMIT pp->state_name_edit->editingFinished();
    psnap(u"state named"_s);
    // --- two provinces
    win.on_click(mates[0], ctrl);
    psnap(u"two"_s);
    win.on_click(other, ctrl);
    psnap(u"three"_s);
    // --- the state capital
    win.on_click(mates.back(), nomod);
    psnap(u"mate"_s);
    pp->cap_btn->click();
    psnap(u"capital"_s);
    // --- move to state
    win.on_click(src, nomod);
    win.on_state_move(u"map"_s);
    common(u"move pick"_s);
    win.on_click(other, nomod);
    psnap(u"moved"_s);
    win.on_click(src, nomod);
    accept_next();
    win.on_state_move(u"new"_s);
    psnap(u"new state"_s);
    win.on_click(mates[0], nomod);
    win.on_state_move(u"list"_s);
    {
        dialogs::PickerPopup *pop = win.findChildren<dialogs::PickerPopup *>().last();
        out << u"popup %1"_s.arg(pop->tree->topLevelItemCount());
        Q_EMIT pop->picked(m->provinces.get(src)->region);
        pop->close();
        QApplication::processEvents();
    }
    psnap(u"from list"_s);
    win.on_click(src, nomod);
    win.on_state_move(u"delete_map"_s);
    common(u"delete pick"_s);
    win.on_click(other, nomod);
    psnap(u"deleted"_s);
    win.on_state_move(u"map"_s);
    win.escape();
    psnap(u"move cancelled"_s);
    // --- utility states
    win.set_prov_sub(u"utility"_s);
    QApplication::processEvents();
    usnap(u"utility"_s);
    QString urid;
    {
        QStringList rs;
        for (const auto &[r, reg] : m->regions)
            if (r != rid && r != orid && !reg.provinces.empty())
                rs << r;
        std::sort(rs.begin(), rs.end());
        urid = rs.last();
    }
    up->pick->setCurrentIndex(up->pick->find(urid));
    btn(up, u"Mark"_s)->click();
    usnap(u"marked"_s);
    up->grab().save(QDir(out_dir).filePath(u"side_util.png"_s));
    win.map->grab().save(QDir(out_dir).filePath(u"map_util.png"_s));
    up->edit_btn->click();
    usnap(u"editing"_s);
    win.on_click(other, nomod);
    usnap(u"clicked in"_s);
    win.on_click(other, Qt::ShiftModifier);
    usnap(u"state clicked"_s);
    win.escape();
    usnap(u"esc 1"_s);
    win.escape();
    usnap(u"esc 2"_s);
    Q_EMIT up->lst->itemClicked(up->lst->item(0));
    usnap(u"shown again"_s);
    up->unmark_btn->click();
    usnap(u"unmarked"_s);
    // --- states
    win.set_prov_sub(u"states"_s);
    QApplication::processEvents();
    ssnap(u"states"_s);
    sp->filter->setText(u"a"_s);
    ssnap(u"filter a"_s);
    sp->filter->setText(QString());
    sp->only_odd->setChecked(true);
    ssnap(u"odd"_s);
    sp->only_odd->setChecked(false);
    for (int i = 0; i < sp->tree->topLevelItemCount(); ++i)
        if (sp->tree->topLevelItem(i)->data(0, Qt::UserRole).toString() == orid) {
            sp->tree->setCurrentItem(sp->tree->topLevelItem(i));
            break;
        }
    QApplication::processEvents();
    ssnap(u"picked"_s);
    sp->members->setCurrentItem(sp->members->topLevelItem(1));
    ssnap(u"member"_s);
    sp->cap_btn->click();
    ssnap(u"member capital"_s);
    sp->name->setText(u"  Other   state "_s);
    Q_EMIT sp->name->editingFinished();
    ssnap(u"named"_s);
    sp->add_btn->click();
    common(u"adding"_s);
    out << u"handler "_s + v(win.map->mouse_handler == &sp->handler);
    win.map->mouse_handler->press(md->cx[size_t(mates[1])] + 0.5, md->cy[size_t(mates[1])] + 0.5, 1.0, nomod);
    ssnap(u"added"_s);
    win.escape();
    ssnap(u"stopped"_s);
    sp->target->setCurrentIndex(first_key(sp->target));
    sp->del_btn->click();
    ssnap(u"deleted"_s);
    sp->grab().save(QDir(out_dir).filePath(u"side_states.png"_s));
    // --- undo
    for (int i = 0; i < 3; ++i)
        out << u"undo "_s + v(win.undo());
    ssnap(u"undone"_s);
    win.set_prov_sub(u"provinces"_s);
    psnap(u"back"_s);
    // --- G4
    {
        const bool ok = win.save(false);
        out << u"apply %1 | msg %2 | %3"_s.arg(v(ok), v(win.st_msg->text()), win.st_pending->text());
    }
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
