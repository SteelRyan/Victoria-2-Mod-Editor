// mapdefs against Python, the C++ side of tools/check_mapdefs.py.
// Usage: med_mapdefs_check --dump <out folder> <work folder>   (MODEDITOR_SETTINGS names the settings file)
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "core/mapfiles.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "ui/mapdefs.h"
#include "ui/theme.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <cstdio>
#include <deque>

using namespace Qt::StringLiterals;
using pyform::v;
using namespace mapdefs;

static QString opt(const std::optional<QString> &s) { return s ? *s : u"None"_s; }
static QString mods_text(const mapfiles::Mods &lst)
{
    QStringList p;
    for (const auto &[k, val] : lst)
        p << k + u'=' + val;
    return p.join(u","_s);
}

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    moddata::Mod m(pypath::join(work, u"Ferrum_Mare 1.2.000"_s), BASE, std::nullopt,
                   QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s});
    m.load();
    QStringList out;
    std::deque<std::pair<QString, bool>> asks;               // QInputDialog.getText's answers: (text, ok)
    std::deque<std::pair<bool, std::optional<int>>> removes; // RemoveDialog.exec's answers: (accept, combo row)
    std::deque<QString> shots;                               // a picture of the next remove dialog under this name

    auto labels = [](QWidget *w) {
        QStringList p;
        for (QLabel *l : w->findChildren<QLabel *>())
            p << l->objectName() + u": "_s + l->text();
        return p.join(u" / "_s);
    };
    auto get_text = [&](QWidget *, const QString &title, const QString &label) {
        out << u"ask %1 | %2"_s.arg(title, label);
        const auto a = asks.front();
        asks.pop_front();
        return a;
    };
    auto run_remove = [&](RemoveDialog &d) {
        out << u"remove dialog %1 | %2"_s.arg(d.windowTitle(), labels(&d));
        if (d.target) {
            QStringList t;
            for (int i = 0; i < d.target->count(); ++i)
                t << d.target->itemText(i) + u'=' + d.target->itemData(i).toString();
            out << u"  targets "_s + t.join(u", "_s);
        }
        if (!shots.empty()) {
            d.show();
            QApplication::processEvents();
            d.grab().save(QDir(out_dir).filePath(shots.front()));
            shots.pop_front();
            d.hide();
        }
        const auto [acc, idx] = removes.front();
        removes.pop_front();
        if (idx && d.target)
            d.target->setCurrentIndex(*idx);
        out << u"  -> %1 chosen %2"_s.arg(v(acc), opt(d.chosen()));
        return acc;
    };
    auto hook = [&](MapDefsDialog &d) {
        d.get_text = get_text;
        d.run_remove = run_remove;
    };

    auto st = [&](MapDefsDialog &d, const QString &what) {
        out << u"== "_s + what;
        for (int i = 0; i < d.list->count(); ++i) {
            QListWidgetItem *it = d.list->item(i);
            const QBrush fg = it->foreground();
            out << u"item %1 %2 | tip %3 | data %4 | fg %5"_s.arg(i).arg(
                it->text(), it->toolTip(), it->data(Qt::UserRole).toString(),
                fg.style() == Qt::NoBrush ? u"none"_s : fg.color().name());
        }
        out << u"row %1 current %2 head %3"_s.arg(d.list->currentRow()).arg(opt(d.current), d.head->text());
        out << u"info "_s + d.info->text();
        out << u"name %1 | ph %2 | vis %3"_s.arg(d.name_field->text(), d.name_field->placeholderText(),
                                                 v(d.name_row->isVisibleTo(&d)));
        out << u"del %1 | %2"_s.arg(v(d.del_btn->isEnabled()), d.del_btn->toolTip());
        QStringList rows;
        for (ModifierRow *r : d.rows)
            rows << r->key->text() + u'=' + r->value->text();
        out << u"rows "_s + rows.join(u"; "_s);
        out << u"err %1 %2"_s.arg(v(d.err->isVisibleTo(&d)), d.err->text());
        out << u"names %1 | new %2 | removed %3"_s.arg(d.names.join(u','), d.new_names.join(u','),
                                                       d.removed.join(u','));
        QStringList mv, rn, wk;
        for (const auto &[r, t] : d.moves)
            mv << r + u'>' + opt(t);
        for (const auto &[k, val] : d.renames)
            rn << k + u'>' + val;
        for (const auto &[n, lst] : d.work)
            wk << n + u':' + mods_text(lst);
        out << u"moves %1 | renames %2"_s.arg(mv.join(u", "_s), rn.join(u", "_s));
        out << u"work "_s + wk.join(u" / "_s);
    };
    auto result = [&](MapDefsDialog &d) {
        const auto [mods, nw, rem, mv] = d.result_changes();
        QStringList ms, ts;
        for (const auto &[n, lst] : mods)
            ms << n + u':' + mods_text(lst);
        for (const auto &[r, t] : mv)
            ts << r + u'>' + (t.isEmpty() ? u"None"_s : t);
        out << u"result %1 | mods %2 | new %3 | removed %4 | moves %5"_s.arg(d.result()).arg(
            ms.join(u" / "_s), nw.join(u','), rem.join(u','), ts.join(u", "_s));
    };
    auto grab = [&](QWidget &d, const QString &name) {
        d.show();
        QApplication::processEvents();
        d.grab().save(QDir(out_dir).filePath(name));
    };
    auto x_button = [](ModifierRow *row) {
        for (QPushButton *b : row->findChildren<QPushButton *>())
            if (b->text() == u"✕"_s)
                return b;
        return static_cast<QPushButton *>(nullptr);
    };
    auto rp = [](const QString &s) { return py::repr_str(s); };
    auto emit_done = [](MapDefsDialog &d, const QString &text) {
        d.name_field->setText(text);
        Q_EMIT d.name_field->editingFinished();
    };

    // --- helpers
    for (const QString &n : {u"Mild_climate"_s, u"north_america"_s, u"a_b_c"_s, QString(), u"x"_s})
        out << u"pretty %1 %2"_s.arg(rp(n), pretty(n));
    for (const QString &x : {u"0.1"_s, u"-5"_s, u"a b"_s, u"1{"_s, u"x="_s, u"#"_s, u"\""_s, u"\x1c"_s, u" "_s,
                             QString(), u"abc"_s})
        out << u"value %1 %2"_s.arg(rp(x), v(value_ok(x)));
    for (const QString &k : {u"farm_rgo_eff"_s, u"_a1"_s, u"1a"_s, u"a-b"_s, u"Ab_9"_s, QString(), u"ä"_s})
        out << u"key %1 %2"_s.arg(rp(k), v(KEY_RE.match(k).hasMatch()));
    for (const auto &[n, lim] : std::vector<std::pair<QString, int>>{
             {u"pillar"_s, 50}, {u"islands"_s, 50}, {u"trade_port_scope"_s, 50}, {u"europe"_s, 3},
             {u"leftover"_s, 50}, {u"no_such_name_x"_s, 50}, {u"Pillar"_s, 50}})
        out << u"refs %1 %2 %3"_s.arg(n).arg(lim).arg(find_references(m, n, lim).join(u", "_s));

    // --- Ferrum Mare's continents
    mapfiles::MembershipFile mf(m, u"continent"_s);
    MapDefsDialog dlg(mf, u"troida"_s);
    hook(dlg);
    out << u"title %1 | keys %2 %3"_s.arg(dlg.windowTitle()).arg(dlg.keys.size()).arg(dlg.keys.join(u','));
    out << u"labels "_s + labels(&dlg);
    st(dlg, u"start at troida"_s);
    grab(dlg, u"continent_start.png"_s);
    dlg.rows[0]->value->setText(u" 0.25 "_s);
    dlg._add_row(u"farm_rgo_eff"_s, u"-0.1"_s);
    st(dlg, u"troida: typed + a row"_s);
    x_button(dlg.rows[1])->click();
    st(dlg, u"troida: second row removed"_s);
    dlg.add_btn->click();
    out << u"completer %1"_s.arg(dlg.rows.back()->key->completer()->completionCount());
    dlg.rows.back()->key->completer()->popup()->hide();
    dlg.rows.back()->key->setText(u"max_attrition"_s);
    st(dlg, u"troida: added by the button"_s);
    dlg.list->setCurrentRow(0);
    st(dlg, u"pillar"_s);
    emit_done(dlg, u"  Great   Pillars "_s);
    st(dlg, u"pillar renamed"_s);
    emit_done(dlg, u"Pillars"_s);
    st(dlg, u"pillar back to its name"_s);
    emit_done(dlg, u"   "_s);
    dlg.list->setCurrentRow(6);
    emit_done(dlg, u"The Isles"_s);
    st(dlg, u"islands named"_s);
    dlg.list->setCurrentRow(0);
    for (const auto &a : std::vector<std::pair<QString, bool>>{
             {QString(), false}, {u"bad key!"_s, true}, {u"pillar"_s, true}, {u" new_land "_s, true}}) {
        asks.push_back(a);
        dlg._new();
        st(dlg, u"new (%1, %2)"_s.arg(rp(a.first), v(a.second)));
    }
    asks.push_back({u"second_land"_s, true});
    dlg._new();
    asks.push_back({u"other_land"_s, true});
    dlg._new();
    st(dlg, u"two more"_s);
    dlg._remove();
    st(dlg, u"other_land removed (made here, no provinces)"_s);
    dlg.list->setCurrentRow(int(dlg.names.indexOf(u"leftover"_s)));
    removes.push_back({false, std::nullopt});
    dlg._remove();
    st(dlg, u"leftover: cancelled"_s);
    removes.push_back({true, 2});
    shots.push_back(u"remove_leftover.png"_s);
    dlg._remove();
    st(dlg, u"leftover moved"_s);
    dlg.list->setCurrentRow(int(dlg.names.indexOf(u"troida"_s)));
    removes.push_back({true, int(dlg.names.indexOf(u"new_land"_s)) - 1});
    dlg._remove();
    st(dlg, u"troida into new_land"_s);
    dlg.list->setCurrentRow(int(dlg.names.indexOf(u"new_land"_s)));
    removes.push_back({true, 0});
    dlg._remove();
    st(dlg, u"new_land removed, troida sent on"_s);
    dlg.list->setCurrentRow(int(dlg.names.indexOf(u"trade_port_scope"_s)));
    removes.push_back({true, std::nullopt});
    dlg._remove();
    st(dlg, u"trade_port_scope removed"_s);
    dlg.list->setCurrentRow(int(dlg.names.indexOf(u"europe"_s)));
    dlg._remove();
    asks.push_back({u"leftover"_s, true});
    dlg._new();
    st(dlg, u"europe (vanilla), leftover again"_s);
    // --- OK's checks
    dlg.list->setCurrentRow(1);
    dlg.rows[0]->key->setText(u"bad key"_s);
    dlg.list->setCurrentRow(0);
    dlg._ok();
    st(dlg, u"ok: bad key"_s);
    dlg.rows[0]->key->setText(u"good_key"_s);
    dlg.rows[0]->value->setText(QString());
    dlg._ok();
    st(dlg, u"ok: no value"_s);
    dlg.rows[0]->value->setText(u"1 2"_s);
    dlg._ok();
    st(dlg, u"ok: a space"_s);
    dlg.rows[0]->value->setText(u"2"_s);
    dlg.rows[1]->key->setText(u"good_key"_s);
    dlg._ok();
    st(dlg, u"ok: twice"_s);
    dlg.rows[1]->key->setText(QString());
    dlg.rows[1]->value->setText(QString());
    dlg._ok();
    st(dlg, u"ok"_s);
    result(dlg);
    dlg.hide();

    // --- Ferrum Mare's climates
    mapfiles::MembershipFile mc(m, u"climate"_s);
    MapDefsDialog dc(mc, u"no_such"_s);
    hook(dc);
    out << u"title %1 | keys %2"_s.arg(dc.windowTitle()).arg(dc.keys.size());
    st(dc, u"climates"_s);
    grab(dc, u"climate_start.png"_s);
    dc._remove();
    asks.push_back({u"cold_climate"_s, true});
    dc._new();
    dc.rows[0]->value->setText(u"-0.5"_s);
    st(dc, u"climate: new"_s);
    dc._remove();
    st(dc, u"climate: new removed"_s);
    dc.reject();
    result(dc);
    dc.hide();

    // --- Modus: the base game's continent.txt
    moddata::Mod mm(pypath::join(work, {u"study mods"_s, u"Modus Omnino Demens 1.7"_s}), BASE);
    mm.load();
    mapfiles::MembershipFile mo(mm, u"continent"_s);
    MapDefsDialog dm(mo);
    hook(dm);
    out << u"modus labels "_s + labels(&dm);
    st(dm, u"modus continents"_s);
    grab(dm, u"modus_start.png"_s);
    dm.list->setCurrentRow(3);
    removes.push_back({true, 0});
    dm._remove();
    st(dm, u"modus: vanilla not removed"_s);
    dm.hide();

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
