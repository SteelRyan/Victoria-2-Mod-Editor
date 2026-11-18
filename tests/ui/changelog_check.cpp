// The Changelog tab against Python, the C++ side of tools/check_changelog.py.
// Usage: med_changelog_check --run <mod copy> <backups> <out folder> <edits.json> <work folder>
//        (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/localisation.h"
#include "core/moddata.h"
#include "core/pyjson.h"
#include "core/pypath.h"
#include "core/pyre.h"
#include "core/scriptstore.h"
#include "map/mapview_data.h"
#include "ui/moreviews.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeWidget>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::v;
using changelog::Value;

static const char *OLD_LOG = R"({"entries": [{"id": 1, "text": "Fixed an old thing", "section": "Bugfixes", "src": "own", "used": false},
 {"id": 2, "text": "Event 5 “Old” edited", "section": "Events", "src": "file", "rel": "events/x.txt",
  "when": "2026-01-02 10:11", "scenario": "1836", "used": true},
 {"id": 3, "text": "Typed in common/defines.lua", "section": "Game settings", "src": "edit",
  "script": true, "when": "2026-01-02 10:11", "scenario": "1836", "used": false}],
 "draft": {"version": "3.1", "title": "Ferrum Mare v3.1",
  "body": "**Gameplay**\n- one\n  more lines\n- two\n\n**Bugfixes**\n- a fix\nstray\n- second"},
 "extra": [1, 2.5, null, "ä"]})";

static QStringList g_paths;

static QString norm(QString s)
{
    for (const QString &p : g_paths)
        s.replace(p, u"<P>"_s).replace(QString(p).replace(u"\\"_s, u"\\\\"_s), u"<P>"_s);
    s.replace(py::rx(u"\\d{4}-\\d\\d-\\d\\d[ _]\\d\\d[:-]\\d\\d(?:[:-]\\d\\d)?"_s), u"STAMP"_s);
    return s.replace(py::rx(u"\\d{8}_\\d{6}_\\d{6}"_s), u"STAMP"_s);
}
static QString j(const Value &x) { return norm(pyjson::dumps(x, std::nullopt, false)); }
static Value parse(const QString &json) { return *pyjson::loads(json); }

static int run(const QString &root, const QString &backups, const QString &out_dir, const QString &edits_path,
               const QString &work)
{
    QDir().mkpath(out_dir);
    QStringList out;
    moreviews::app_dir_override = out_dir;           // (the changelog folder: in the out folder)
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    auto m = std::make_shared<moddata::Mod>(root, BASE, std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    m->load();
    const QString mname = !m->name().isEmpty() ? m->name() : u"mod"_s;
    const QString cdir = pypath::join(out_dir, {u"changelogs"_s, QString(mname).replace(py::rx(u"[^\\w .-]"_s), u"_"_s)});
    QDir().mkpath(cdir);
    {
        QFile f(pypath::join(cdir, u"log.json"_s));
        (void)f.open(QIODevice::WriteOnly);
        f.write(pyjson::dumps(parse(QString::fromUtf8(OLD_LOG)), 1, false).replace(u"\n"_s, u"\r\n"_s).toUtf8());
    }
    auto md = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mainwindow::MainWindow win(m, md, m->scenarios()[0].name, backups);
    win._check_timer->stop();
    win._rec_timer->stop();
    win.resize(1900, 1000);
    win.show();
    QApplication::processEvents();
    win._check_timer->stop();
    QObject::disconnect(win._check_timer, &QTimer::timeout, nullptr, nullptr);
    document::Document &d = *win.doc;
    changelog::ChangelogWorkspace *cl = win.cl_ws;
    changelog::NotesBoard *board = cl->board;
    auto &store = d.map<scriptstore::ScriptStore>(u"scripts"_s);
    g_paths = {out_dir, root, backups};

    auto settle = [](int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms)
            QApplication::processEvents();
    };
    auto snap = [&](const QString &tag) {
        out << u"%1 | version %2 | title %3 | status %4 | msg %5 | notice %6"_s.arg(
            tag, v(cl->version->text()), v(cl->title->text()), v(norm(cl->status->text())), v(norm(win.st_msg->text())),
            win.notice->isVisibleTo(&win) ? v(norm(win.notice->text())) : u"-"_s);
        out << u"%1 preview %2"_s.arg(tag, v(cl->preview->toPlainText()));
        out << u"%1 note %2 | ticks %3 %4 | move_to %5 | own %6 %7"_s.arg(
            tag, v(norm(cl->folder_note->text())), QString::number(cl->hide_used->isChecked() ? 1 : 0),
            QString::number(cl->steps_box->isChecked() ? 1 : 0), QString::number(cl->move_to->currentIndex()),
            QString::number(cl->own_sec->currentIndex()), v(cl->own->text()));
        for (int i = 0; i < cl->lst->topLevelItemCount(); ++i) {
            QTreeWidgetItem *g = cl->lst->topLevelItem(i);
            out << u"%1 group %2 %3 | %4 | %5"_s.arg(tag, QString::number(i), v(norm(g->text(0))),
                                                    QString::number(g->flags().toInt()), g->foreground(0).color().name());
            for (int k = 0; k < g->childCount(); ++k) {
                QTreeWidgetItem *it = g->child(k);
                out << u"%1   row %2 | %3 | %4 | %5 %6 | %7 | %8 | %9"_s.arg(
                    tag, v(norm(it->text(0))), v(it->text(1)), v(norm(it->toolTip(0))), it->foreground(0).color().name(),
                    it->foreground(1).color().name(), QString::number(it->flags().toInt()),
                    QString::number(it->font(0).italic() ? 1 : 0), QString::number(it->isSelected() ? 1 : 0));
            }
        }
        for (const auto &r : board->_rows) {
            if (!r.bi) {
                QStringList labels;
                for (QLabel *x : r.w->findChildren<QLabel *>())
                    labels << x->text();
                out << u"%1 head %2 %3 | %4"_s.arg(tag, QString::number(r.si),
                                                  v(r.w->findChildren<QComboBox *>()[0]->currentText()), v(labels));
            } else {
                auto *c = dynamic_cast<changelog::BlockCard *>(r.w);
                out << u"%1 card %2 %3 %4 | %5 | %6 | %7"_s.arg(
                    tag, QString::number(r.si), QString::number(*r.bi), QString::number(c->bid),
                    c->editor ? u"open"_s : u"closed"_s, v(c->editor ? c->editor->toPlainText() : c->text->text()),
                    v(c->cat->currentText()));
            }
        }
        out << u"%1 board open %2 next %3 | md %4"_s.arg(
            tag, board->open_id ? QString::number(*board->open_id) : u"None"_s, QString::number(board->next_id),
            v(cl->markdown()));
        out << u"%1 data %2"_s.arg(tag, j(cl->data));
    };
    auto grab = [&](const QString &name) {
        for (Value &e : cl->entries())               // (the times of the Applies and the folder: the same both sides)
            if (e.obj().contains(u"when"_s))
                e.obj().insert(u"when"_s, Value(u"2026-11-04 12:00"_s));
        cl->_fill();
        cl->folder_note->setText(norm(cl->folder_note->text()));
        settle(300);
        cl->grab().save(QDir(out_dir).filePath(name));
    };
    auto files = [&](const QString &tag) {
        QStringList dirs{cdir};
        for (qsizetype i = 0; i < dirs.size(); ++i) {
            QStringList subs = QDir(dirs[i]).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            std::sort(subs.begin(), subs.end());
            for (const QString &s : subs)
                dirs << pypath::join(dirs[i], s);
        }
        std::sort(dirs.begin(), dirs.end());
        for (const QString &dp : dirs) {
            QStringList fns = QDir(dp).entryList(QDir::Files);
            std::sort(fns.begin(), fns.end());
            for (const QString &fn : fns) {
                const QString p = pypath::join(dp, fn);
                QFile f(p);
                (void)f.open(QIODevice::ReadOnly);
                const QByteArray b = f.readAll();
                out << u"%1 file %2 | %3 crlf | %4"_s.arg(tag, v(norm(pypath::relpath(p, cdir))),
                                                          QString::number(b.count("\r\n")), v(norm(QString::fromUtf8(b))));
            }
        }
    };

    // ---- the pure parts
    const std::vector<std::pair<QString, QString>> cats = {
        {u"events/a.txt"_s, u""_s}, {u"decisions/B.txt"_s, u""_s}, {u"units/x.txt"_s, u""_s},
        {u"common/issues.txt"_s, u"party_issues"_s}, {u"common/issues.txt"_s, u"economic_reforms"_s},
        {u"common/issues.txt"_s, u"political_reforms"_s}, {u"common/cb_types.txt"_s, u""_s},
        {u"common/defines.lua"_s, u""_s}, {u"map/terrain.txt"_s, u""_s}, {u"news/x.txt"_s, u""_s},
        {u"interface/a.gui"_s, u""_s}, {u"poptypes/a.txt"_s, u""_s}, {u"history/wars/a.txt"_s, u""_s},
        {u"common/governments.txt"_s, u""_s}, {u"common/nationalvalues.txt"_s, u""_s},
        {u"common/technology.txt"_s, u"schools"_s}, {u"common/technology.txt"_s, u""_s},
        {u"technologies/army.txt"_s, u""_s}, {u"common/event_modifiers.txt"_s, u""_s}, {u"common/goods.txt"_s, u""_s},
        {u"localisation/a.csv"_s, u""_s}, {u"history/units/a.txt"_s, u""_s}, {u"history/diplomacy/a.txt"_s, u""_s},
        {u"history/pops/1836.1.1/a.txt"_s, u""_s}, {u"history/provinces/a/1.txt"_s, u""_s},
        {u"history/countries/ENG.txt"_s, u""_s}, {u"common/countries/E.txt"_s, u""_s}, {u"map/default.map"_s, u""_s},
        {u"gfx/x.dds"_s, u""_s}, {u"COMMON/CULTURES.TXT"_s, u""_s}};
    for (const auto &[rel, grp] : cats)
        out << u"category %1 %2 %3"_s.arg(rel, grp, changelog::file_category(rel, grp));
    {
        QStringList cs;
        for (const QString &c : {u"Events"_s, u"Other"_s, u"Apply"_s, u"My own"_s, u"ÄÖ"_s, u""_s})
            cs << changelog::cat_color(c);
        out << u"colours "_s + v(cs);
    }
    for (const QString &ver : {u"1.0.0"_s, u"v2.9"_s, u"1.09"_s, u"abc"_s, u""_s, u"x99"_s, u"007"_s, u"9"_s, u"1.2.3a"_s})
        out << u"bump %1 %2"_s.arg(v(ver), v(changelog::bump(ver)));
    const Value secs = parse(uR"([{"name": "Gameplay", "blocks": [{"text": "one\n\n  two  \n"}, {"text": "   "}]},
        {"name": "Empty", "blocks": []}, {"name": "Bugfixes", "blocks": [{"text": "fix"}]}])"_s);
    out << u"markdown "_s + v(changelog::notes_markdown(secs.arr()));
    out << u"markdown empty "_s + v(changelog::notes_markdown({}));
    out << u"body "_s + j(Value(changelog::parse_body(u"stray\n- first\n  cont\n**Head**\n- a\n**Head 2**  \nloose\n- b\n- "_s)));
    out << u"steps "_s + j(Value(changelog::edit_steps({{u"Name X: a → b"_s, u"pops"_s, false}, {u"Size = 3"_s, u"map"_s, true},
                                                        {u"Size = 4"_s, u"map"_s, true}, {u"Other"_s, u"nosuchtab"_s, false},
                                                        {u"Other"_s, u"pops"_s, false}, {u"Name X: a → b"_s, u"pops"_s, true}})));

    // ---- script edits
    QFile ef(edits_path);
    (void)ef.open(QIODevice::ReadOnly);
    const Value edits = parse(QString::fromUtf8(ef.readAll()));
    win.set_tab(u"events"_s);
    QApplication::processEvents();
    for (const Value &e : edits.arr()) {
        if (e.arr()[0].str() == u"set")
            store.set_text(e.arr()[1].str(), e.arr()[2].str());
        else
            store.remove(e.arr()[1].str());
        QApplication::processEvents();
    }
    d.flush_live();
    win._cl_note();
    localisation::LocEdits *lc = d.maps.contains(u"loc"_s) ? &d.map<localisation::LocEdits>(u"loc"_s) : nullptr;
    changelog::Loc loc;
    if (lc)
        loc = [lc](const QString &k) { return lc->text(k); };
    else
        loc = [&m](const QString &k) -> std::optional<QString> {
            const QString *t = m->loc.get(k);
            return t ? std::optional<QString>(*t) : std::nullopt;
        };
    for (const Value &c : changelog::script_changes(store, loc))
        out << u"change "_s + j(c);
    for (const Value &c : win._changelog_pending())
        out << u"pending "_s + j(c);
    out << u"undone "_s + v(win._applied_undone());
    win.set_tab(u"changelog"_s);
    QApplication::processEvents();
    settle(300);
    snap(u"shown"_s);
    // ---- Apply
    out << u"save "_s + v(win.save());
    QApplication::processEvents();
    settle(700);
    snap(u"applied"_s);
    files(u"applied"_s);
    // ---- blocks
    QTreeWidget *lst = cl->lst;
    QTreeWidgetItem *g = lst->topLevelItem(0);
    for (int k = 0; k < std::min(3, g->childCount()); ++k)
        g->child(k)->setSelected(true);
    cl->_add_items(lst->selectedItems());
    QApplication::processEvents();
    snap(u"added 3"_s);
    cl->_add_all();
    QApplication::processEvents();
    settle(200);
    snap(u"added all"_s);
    grab(u"changelog_blocks.png"_s);
    std::vector<long long> ids;
    for (const Value &s : board->secs())
        for (const Value &b : s.get(u"blocks"_s)->arr())
            ids.push_back(*std::get_if<long long>(&b.get(u"id"_s)->v));
    auto block_id = [&](size_t si, size_t bi) {
        return *std::get_if<long long>(&board->secs()[si].get(u"blocks"_s)->arr()[bi].get(u"id"_s)->v);
    };
    board->move_block(ids[0], 1);
    board->move_block(ids[0], -1);
    board->move_block(ids[0], -1);                   // (the first of all: nothing)
    const long long first_of_2nd = board->secs().size() > 1 ? block_id(1, 0) : ids.back();
    board->move_block(first_of_2nd, -1);             // over the edge: the end of the category before
    QApplication::processEvents();
    snap(u"moved"_s);
    for (int y : {-5, 0, 30, 80, 200, 5000}) {
        const auto t = board->_target(y);
        out << u"target %1 %2"_s.arg(y).arg(t ? u"[%1, %2, %3]"_s.arg(std::get<0>(*t)).arg(std::get<1>(*t)).arg(std::get<2>(*t))
                                             : u"None"_s);
    }
    board->_drop = std::tuple{ids[1], qsizetype(0), qsizetype(0)};
    board->apply_drop();
    QApplication::processEvents();
    snap(u"dropped"_s);
    board->set_category(ids[2], u"  Balance  "_s);
    board->set_category(ids[2], u"balance"_s);       // (the same: nothing)
    QApplication::processEvents();
    snap(u"category"_s);
    board->rename_section(u"Balance"_s, u"Gameplay"_s);   // (there already: one)
    board->rename_section(board->secs()[0].get(u"name"_s)->str(), u"My own heading"_s);
    board->move_section(u"My own heading"_s, 1);
    QApplication::processEvents();
    snap(u"renamed"_s);
    board->open_block(ids[3]);
    QApplication::processEvents();
    snap(u"opened"_s);
    auto card_of = [&](long long id) -> changelog::BlockCard * {
        for (const auto &r : board->_rows)
            if (r.bi)
                if (auto *c = dynamic_cast<changelog::BlockCard *>(r.w); c && c->bid == id)
                    return c;
        return nullptr;
    };
    card_of(ids[3])->editor->setPlainText(u"  rewritten\nin two lines  "_s);
    board->open_block(ids[4]);                       // (the text of the open one is kept)
    QApplication::processEvents();
    snap(u"opened another"_s);
    changelog::BlockCard *card = card_of(ids[4]);
    card->editor->setPlainText(u"closed text"_s);
    card->_close();
    QApplication::processEvents();
    snap(u"closed"_s);
    board->send_back(ids[5]);
    QApplication::processEvents();
    settle(200);
    snap(u"sent back"_s);
    cl->own->setText(u"  My   own   block "_s);
    cl->own_sec->setCurrentIndex(3);
    cl->_add_own();
    cl->own->setText(u"   "_s);
    cl->_add_own();
    QApplication::processEvents();
    snap(u"own"_s);
    cl->move_to->setCurrentIndex(5);
    g = lst->topLevelItem(lst->topLevelItemCount() - 1);
    if (g->childCount())
        g->child(0)->setSelected(true);
    cl->_add_items(lst->selectedItems());
    cl->move_to->setCurrentIndex(0);
    QApplication::processEvents();
    snap(u"added under"_s);
    lst->clearSelection();
    for (int i = 0; i < lst->topLevelItemCount(); ++i) {
        QTreeWidgetItem *gi = lst->topLevelItem(i);
        if (gi->childCount())
            gi->child(gi->childCount() - 1)->setSelected(true);
    }
    cl->_forget();
    QApplication::processEvents();
    snap(u"forgot"_s);
    cl->hide_used->setChecked(false);
    cl->steps_box->setChecked(true);
    QApplication::processEvents();
    snap(u"ticks"_s);
    cl->hide_used->setChecked(true);
    cl->steps_box->setChecked(false);
    cl->version->setText(u"3.2.0"_s);
    cl->_version_edited(u"3.2.0"_s);
    QApplication::processEvents();
    snap(u"version"_s);
    cl->_copy_title();
    out << u"clip "_s + v(QGuiApplication::clipboard()->text());
    cl->_copy_notes();
    out << u"clip "_s + v(QGuiApplication::clipboard()->text());
    out << u"md "_s + v(cl->_save_md());
    cl->_write();
    snap(u"saved"_s);
    files(u"saved"_s);
    cl->_next();
    QApplication::processEvents();
    snap(u"next"_s);
    files(u"next"_s);
    // ---- undo after Apply, an Apply that takes back applied changes
    out << u"undo "_s + v(win.undo());
    QApplication::processEvents();
    snap(u"undone"_s);
    for (const Value &c : win._changelog_pending())
        out << u"pending after undo "_s + j(c);
    for (int i = 0; i < 4; ++i) {
        win.undo();
        QApplication::processEvents();
    }
    out << u"undone "_s + v(win._applied_undone());
    out << u"save "_s + v(win.save());
    QApplication::processEvents();
    snap(u"asked"_s);
    out << u"save "_s + v(win.save());
    QApplication::processEvents();
    settle(500);
    snap(u"written"_s);
    files(u"end"_s);
    win.set_tab(u"changelog"_s);
    QApplication::processEvents();
    settle(300);
    grab(u"changelog_end.png"_s);
    QDir(cdir).removeRecursively();                  // (its files are in the lines; the pictures are compared)
    QFile f(QDir(out_dir).filePath(u"lines.txt"_s));
    if (!f.open(QIODevice::WriteOnly))
        return 3;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    f.close();
    std::printf("%d lines\n", int(out.size()));
    std::fflush(stdout);
    win._rec_timer->stop();
    std::_Exit(0);                                   // (no close: nothing to keep — and no recovery file)
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    QApplication::setCursorFlashTime(0);
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
