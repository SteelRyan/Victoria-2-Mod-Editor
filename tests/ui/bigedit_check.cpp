// The large window of a list page's entry against Python, the C++ side of tools/check_bigedit.py.
// Usage: med_bigedit_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/fs.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "core/settings.h"
#include "map/mapview_data.h"
#include "ui/bigedit.h"
#include "ui/scriptedit.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <algorithm>
#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;
using pyform::v;

static constexpr int W = 1560, H = 940;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}

static QList<QWidget *> kids_of(QWidget *w)
{
    QList<QWidget *> got;
    if (auto *sp = qobject_cast<QSplitter *>(w)) {
        for (int i = 0; i < sp->count(); ++i)
            got << sp->widget(i);
        return got;
    }
    if (auto *sa = qobject_cast<QScrollArea *>(w)) {
        if (sa->widget())
            got << sa->widget();
        return got;
    }
    if (auto *st = qobject_cast<QStackedWidget *>(w)) {
        if (st->currentWidget())
            got << st->currentWidget();
        return got;
    }
    std::function<void(QLayout *)> lay_walk = [&](QLayout *lay) {
        for (int i = 0; i < lay->count(); ++i) {
            QLayoutItem *it = lay->itemAt(i);
            if (it->widget())
                got << it->widget();
            else if (it->layout())
                lay_walk(it->layout());
        }
    };
    if (w->layout())
        lay_walk(w->layout());
    return got;
}

static QList<QWidget *> widgets_in(QWidget *w, QWidget *root)
{
    QList<QWidget *> res;
    for (QWidget *c : kids_of(w))
        if (c->isVisibleTo(root)) {
            res << c;
            res << widgets_in(c, root);
        }
    return res;
}

static QString dat(const QVariant &d)
{
    if (!d.isValid())
        return u"None"_s;
    return d.toString().replace(QChar(0), u"\\0"_s);
}

static void item_rows(QTreeWidgetItem *it, int depth, QStringList &rows)
{
    rows << u"%1 %2/%3/%4/%5/%6/%7/%8/%9/"_s.arg(QString::number(depth), it->text(0), it->text(1), it->toolTip(0),
                                                it->toolTip(1), it->foreground(0).color().name(),
                                                it->foreground(1).color().name(), dat(it->data(0, Qt::UserRole)),
                                                dat(it->data(0, Qt::UserRole + 1))) +
                u"%1/%2/%3\t%4"_s.arg(dat(it->data(0, Qt::UserRole + 2)), QString::number(it->font(0).bold() ? 1 : 0),
                                      QString::number(it->font(1).bold() ? 1 : 0),
                                      QString::number(it->isExpanded() ? 1 : 0));
    for (int c = 0; c < it->childCount(); ++c)
        item_rows(it->child(c), depth + 1, rows);
}

static QString btext(const QPushButton *b)      // (a WrapButton keeps its own text)
{
    if (const auto *wb = qobject_cast<const widgets::WrapButton *>(b))
        return wb->text();
    return b->text();
}

static std::optional<QString> desc(QWidget *w)
{
    const QString en = w->isEnabled() ? QString() : u" off"_s;
    if (auto *c = qobject_cast<QCheckBox *>(w))
        return u"K %1 %2%3"_s.arg(v(c->text()), v(c->isChecked()), en);
    if (auto *b = qobject_cast<QPushButton *>(w))
        return u"B %1 %2%3 | %4"_s.arg(v(btext(b)), v(b->isChecked()), en, v(b->toolTip()));
    if (auto *l = qobject_cast<QLabel *>(w))
        return u"L %1 | %2"_s.arg(v(l->text()), v(l->styleSheet()));
    if (auto *e = qobject_cast<QLineEdit *>(w))
        return u"E %1 | %2%3"_s.arg(v(e->text()), v(e->placeholderText()), en);
    if (auto *t = qobject_cast<QTreeWidget *>(w)) {
        QStringList rows;
        for (int r = 0; r < t->topLevelItemCount(); ++r)
            item_rows(t->topLevelItem(r), 0, rows);
        return u"T %1 %2"_s.arg(QString::number(t->topLevelItemCount()),
                                qEnvironmentVariableIsSet("ROWS") ? rows.join(u'\n') : h(rows.join(u'\n').toUtf8()));
    }
    if (auto *ed = qobject_cast<scriptedit::ScriptEditor *>(w)) {
        const QTextCursor c = ed->textCursor();
        QStringList sels;
        for (const auto &s : ed->extraSelections())
            sels << u"%1-%2 %3 %4"_s.arg(QString::number(s.cursor.selectionStart()),
                                         QString::number(s.cursor.selectionEnd()),
                                         s.format.background().color().name(QColor::HexArgb),
                                         QString::number(s.format.boolProperty(QTextFormat::FullWidthSelection) ? 1 : 0));
        const auto fl = ed->folded_lines();
        std::vector<long long> fs(fl.begin(), fl.end());
        std::sort(fs.begin(), fs.end());
        QStringList fss;
        for (long long x : fs)
            fss << QString::number(x);
        return u"X %1 | %2-%3 | blocks %4 | pt %5 | folded %6 | flash %7 | sels %8 %9"_s.arg(
            ed->rel, QString::number(c.anchor()), QString::number(c.position()),
            QString::number(ed->document()->blockCount()), QString::number(ed->font().pointSize()), fss.join(u','),
            ed->flash ? u"%1-%2"_s.arg(ed->flash->first).arg(ed->flash->second) : u"None"_s,
            QString::number(sels.size()), qEnvironmentVariableIsSet("ROWS") ? sels.join(u';') : h(sels.join(u';').toUtf8()));
    }
    return std::nullopt;
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
    auto &store = d.map<scriptstore::ScriptStore>(u"scripts"_s);

    auto settle = [](int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms)
            QApplication::processEvents();
    };
    auto snap = [&](const QString &tag) {
        bigedit::BigEditor *bw = win.big_win;
        out << u"%1 | title %2 | %3 %4 %5 | trail %6 | font %7 | steps %8 | hits %9"_s.arg(
            tag, v(bw->windowTitle()), bw->rel.value_or(u"None"_s), bw->path.join(u'/'), bw->noun,
            QString::number(bw->_trail.size()), QString::number(bw->ws.font_size), QString::number(d.undo_stack.size()),
            QString::number(bw->_hits.size()));
        QStringList spans;
        for (const auto &r : bw->refs->list)
            spans << u"%1,%2,%3,%4"_s.arg(QString::number(r.pos), QString::number(r.length), r.kind, r.target);
        out << u"%1 names %2 %3"_s.arg(tag, QString::number(bw->refs->list.size()), h(spans.join(u'\n').toUtf8()));
        for (QWidget *w : widgets_in(bw, bw))
            if (const auto dd = desc(w))
                out << tag + u' ' + *dd;
    };
    auto grab = [&](QWidget *w, const QString &name) {
        settle(400);
        w->grab().save(QDir(out_dir).filePath(name));
    };
    auto key = [](QWidget *w, int k, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QKeyEvent e(QEvent::KeyPress, k, mods);
        QApplication::sendEvent(w, &e);
        QApplication::processEvents();
    };
    auto hover = [&](bigedit::BigEditor *bw, const QString &word) -> QString {
        // The hover text over the first place the word stands (whole word) in the text.
        scriptedit::ScriptEditor *ed = bw->ed;
        const QString text = store.text(*bw->rel);
        const QRegularExpression re(u"(?<![\\w.])%1(?![\\w.])"_s.arg(QRegularExpression::escape(word)),
                                    QRegularExpression::UseUnicodePropertiesOption);
        const auto mm = re.match(text);
        if (!mm.hasMatch())
            return u"no "_s + word;
        QToolTip::hideText();
        QApplication::processEvents();
        QTextCursor c = ed->textCursor();
        c.setPosition(int(mm.capturedStart(0) + 1));
        ed->reveal(mm.capturedStart(0));
        ed->setTextCursor(c);
        ed->centerCursor();
        QApplication::processEvents();
        const QRect r = ed->cursorRect(c);
        const QPoint pt(r.center().x(), r.center().y());
        QHelpEvent he(QEvent::ToolTip, pt, ed->viewport()->mapToGlobal(pt));
        QApplication::sendEvent(ed->viewport(), &he);
        QApplication::processEvents();
        return word + u' ' + (QToolTip::isVisible() ? v(QToolTip::text()) : u"(none)"_s);
    };

    // --- a pop type
    QStringList pops;
    for (const auto &kv : m->poptypes) {
        const QString rel = u"poptypes/%1.txt"_s.arg(kv.first);
        if (store.docs.contains(rel) || fs::isfile(m->p({u"poptypes"_s, kv.first + u".txt"_s})))
            pops << kv.first;
    }
    QString best = pops[0];
    for (const QString &p : pops)
        if (store.text(u"poptypes/%1.txt"_s.arg(p)).contains(u"promote_to"_s)) {
            best = p;
            break;
        }
    out << u"pop "_s + best;
    win.open_big(u"poptypes/%1.txt"_s.arg(best), {}, u"pop type"_s);
    QApplication::processEvents();
    bigedit::BigEditor *bw = win.big_win;
    bw->resize(W, H);
    settle(500);
    snap(u"pop opened"_s);
    grab(bw, u"big_pop.png"_s);
    for (int i = 0; i < std::min(5, bw->tree->topLevelItemCount()); ++i) {
        QTreeWidgetItem *it = bw->tree->topLevelItem(i);
        bw->_outline_clicked(it, 0);
        QApplication::processEvents();
        snap(u"outline %1"_s.arg(i));
        if (it->childCount()) {
            bw->_outline_clicked(it->child(it->childCount() - 1), 0);
            QApplication::processEvents();
            snap(u"outline %1 child"_s.arg(i));
        }
    }
    settle(1500);
    snap(u"flash over"_s);
    bw->find->setText(u"factor"_s);
    QApplication::processEvents();
    snap(u"find typed"_s);
    key(bw->find, Qt::Key_Return);
    key(bw->find, Qt::Key_Return);
    snap(u"find next"_s);
    key(bw->find, Qt::Key_Return, Qt::ShiftModifier);
    snap(u"find before"_s);
    bw->find->setText(u"Modifier"_s);
    QApplication::processEvents();
    snap(u"find case"_s);
    bw->find->setText(u"zz_port_nothing"_s);
    QApplication::processEvents();
    snap(u"find nothing"_s);
    key(bw, Qt::Key_Escape);
    snap(u"find esc"_s);
    for (int step : {1, 1, -1, 0, -1}) {
        bw->ws.zoom(step);
        QApplication::processEvents();
        out << u"zoom %1 -> %2 setting %3"_s.arg(QString::number(step), QString::number(bw->ws.font_size),
                                                 settings_::settings()->value(u"bigFontSize"_s).toString());
    }
    snap(u"zoomed"_s);
    bw->ws.zoom(0);
    bw->_fold(true);
    QApplication::processEvents();
    snap(u"folded"_s);
    grab(bw, u"big_folded.png"_s);
    bw->_fold(false);
    QApplication::processEvents();
    snap(u"unfolded"_s);
    for (const QString &word : {u"factor"_s, u"strata"_s, u"modifier"_s})
        out << u"hover "_s + hover(bw, word);
    std::vector<refs::Ref> others;
    for (const auto &r : bw->refs->list)
        if (r.kind == u"poptype" && r.target != best && bw->_pop_rel(r.target))
            others.push_back(r);
    {
        std::vector<refs::Ref> some(bw->refs->list.begin(),
                                    bw->refs->list.begin() + std::min<size_t>(4, bw->refs->list.size()));
        if (!others.empty())
            some.push_back(others[0]);
        for (const auto &r : some)
            out << u"ref_html "_s + v(bw->ref_html(r));
    }
    if (!bw->refs->list.empty())
        out << u"hover "_s + hover(bw, bw->refs->list[0].target);
    snap(u"hovered"_s);
    if (!others.empty()) {
        bw->ws.follow(others[0], false);
        QApplication::processEvents();
        settle(300);
        snap(u"followed"_s);
        grab(bw, u"big_followed.png"_s);
        bw->back_btn->click();
        QApplication::processEvents();
        snap(u"back"_s);
    }
    {
        scriptedit::ScriptEditor *ed = bw->ed;
        QTextCursor c = ed->textCursor();
        const QString text = store.text(*bw->rel);
        const qsizetype at = text.indexOf(u'\n');
        c.setPosition(int(at >= 0 ? at : text.size()));
        ed->setTextCursor(c);
        c.insertText(u"\n\tport_check = yes\n\tpromote_to = { %1 = { factor = 2 } }"_s.arg(best));
        QApplication::processEvents();
        snap(u"typed now"_s);
        settle(800);
        snap(u"typed settled"_s);
    }
    for (const auto &[rel, noun] : {std::pair{u"common/rebel_types.txt"_s, u"rebel type"_s},
                                    std::pair{u"common/cb_types.txt"_s, u"war goal"_s}}) {
        QStringList keys;
        for (const auto &n : script::parse(store.text(rel)))
            if (n.has_key && !n.key.isEmpty() && n.is_block)
                keys << n.key;
        if (!keys.isEmpty()) {
            win.open_big(rel, {keys[0]}, noun);
            QApplication::processEvents();
            settle(300);
            snap(u"opened "_s + noun);
            if (noun == u"rebel type") {
                grab(bw, u"big_rebel.png"_s);
                bw->_outline_clicked(bw->tree->topLevelItem(bw->tree->topLevelItemCount() - 1), 0);
                QApplication::processEvents();
                snap(u"rebel outline"_s);
            }
        }
    }
    win._refresh_windows();
    settle(600);
    snap(u"refreshed"_s);
    bw->back_btn->click();
    QApplication::processEvents();
    snap(u"back again"_s);
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
