// scriptsearch.py (see scriptsearch.h) — line by line.
#include "ui/scriptsearch.h"
#include "core/events.h"
#include "core/fs.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/refs.h"
#include "core/scriptstore.h"
#include "ui/colsizes.h"
#include "ui/relview.h"
#include "ui/scriptedit.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace scriptsearch {

const std::vector<std::pair<QString, QString>> SCOPES = {{u"file"_s, u"This file"_s},
                                                         {u"open"_s, u"Open files"_s},
                                                         {u"scripts"_s, u"All events + decisions"_s},
                                                         {u"all"_s, u"Every script file (+ common)"_s}};

QString expand(const QRegularExpressionMatch &m, const QString &tmpl)
{
    QString out;
    const qsizetype n = tmpl.size();
    for (qsizetype i = 0; i < n; ++i) {
        const QChar c = tmpl[i];
        if (c != u'\\' || i + 1 >= n) {
            out += c;
            continue;
        }
        const QChar d = tmpl[++i];
        if (d.isDigit() && d.unicode() < 128) {                 // \1 … \99 (two digits at most)
            QString num(d);
            if (i + 1 < n && tmpl[i + 1].isDigit() && tmpl[i + 1].unicode() < 128)
                num += tmpl[++i];
            out += m.captured(num.toInt());
        } else if (d == u'g' && i + 1 < n && tmpl[i + 1] == u'<') {
            const qsizetype close = tmpl.indexOf(u'>', i + 2);
            if (close < 0) {
                out += u"\\g"_s;
                continue;
            }
            const QString name = tmpl.mid(i + 2, close - i - 2);
            bool ok = false;
            const int k = name.toInt(&ok);
            out += ok ? m.captured(k) : m.captured(name);
            i = close;
        } else {
            static const QHash<QChar, QChar> ESC = {{u'n', u'\n'}, {u't', u'\t'}, {u'r', u'\r'}, {u'a', u'\a'},
                                                    {u'b', u'\b'}, {u'f', u'\f'}, {u'v', u'\v'}, {u'\\', u'\\'}};
            const auto it = ESC.find(d);
            if (it != ESC.end())
                out += *it;
            else {
                out += u'\\';
                out += d;
            }
        }
    }
    return out;
}

SearchPanel::SearchPanel(scriptedit::ScriptWorkspace *ws_) : QWidget(ws_), ws(ws_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 4, 6, 4);
    lay->setSpacing(4);
    auto *r1 = new QHBoxLayout();
    find = new QLineEdit();
    find->setPlaceholderText(u"Search the scripts — Enter: next match"_s);
    find->setClearButtonEnabled(true);
    connect(find, &QLineEdit::returnPressed, this, [this] { next(); });
    connect(find, &QLineEdit::textChanged, this, [this] { _timer->start(); });
    r1->addWidget(find, 1);
    const std::vector<std::tuple<QString, QString, QString>> opts = {
        {u"case"_s, u"Aa"_s, u"Match case"_s},
        {u"word"_s, u"W"_s, u"Whole words only"_s},
        {u"regex"_s, u".*"_s, u"Regular expression (Replace can use \\1 …)"_s}};
    for (const auto &[key, text, tip] : opts) {
        auto *b = new QPushButton(text);
        b->setCheckable(true);
        b->setToolTip(tip);
        b->setFixedWidth(40);
        connect(b, &QPushButton::toggled, this, [this] { run(); });
        r1->addWidget(b);
        opt.insert(key, b);
    }
    scope = new QComboBox();
    for (const auto &[k, t] : SCOPES)
        scope->addItem(t, k);
    scope->setCurrentIndex(2);
    connect(scope, &QComboBox::currentIndexChanged, this, [this] { run(); });
    r1->addWidget(scope);
    auto *x = new QPushButton(u"✕"_s);
    x->setToolTip(u"Close the search (Esc)"_s);
    x->setFixedWidth(34);
    connect(x, &QPushButton::clicked, this, [this] { close_panel(); });
    r1->addWidget(x);
    lay->addLayout(r1);
    auto *r2 = new QHBoxLayout();
    repl = new QLineEdit();
    repl->setPlaceholderText(u"Replace with …"_s);
    connect(repl, &QLineEdit::returnPressed, this, [this] { replace_one(); });
    r2->addWidget(repl, 1);
    auto *b = new QPushButton(u"Replace"_s);
    b->setToolTip(u"The match at the cursor, then on to the next"_s);
    connect(b, &QPushButton::clicked, this, [this] { replace_one(); });
    r2->addWidget(b);
    b = new QPushButton(u"Replace all"_s);
    b->setToolTip(u"Every match listed below — one step, Ctrl+Z undoes it"_s);
    connect(b, &QPushButton::clicked, this, [this] { replace_all(); });
    r2->addWidget(b);
    lay->addLayout(r2);
    info = new QLabel(QString());
    info->setObjectName(u"sub"_s);
    info->setWordWrap(true);
    lay->addWidget(info);
    tree = new QTreeWidget();
    tree->setColumnCount(2);
    tree->setHeaderLabels({u"Where"_s, u"Matches"_s});
    tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree->setTextElideMode(Qt::ElideRight);
    tree->setUniformRowHeights(true);
    colsizes::make_resizable(tree, u"searchTree"_s, {std::nullopt});
    tree->setMaximumHeight(260);
    connect(tree, &QTreeWidget::itemClicked, this, &SearchPanel::_clicked);
    _mono = QFont(tree->font());
    _mono.setFamily(u"Consolas"_s);
    lay->addWidget(tree);
    _timer = new QTimer(this);
    _timer->setSingleShot(true);
    _timer->setInterval(280);
    connect(_timer, &QTimer::timeout, this, [this] { run(); });
    setVisible(false);
}

// ------------------------------------------------------------------ what is searched
QStringList SearchPanel::files()
{
    const QString k = scope->currentData().toString();
    scriptedit::ScriptEditor *cur = ws->current();
    if (k == u"file")
        return cur ? QStringList{cur->rel} : QStringList{};
    QStringList out;
    if (k == u"open") {
        for (int i = 0; i < ws->tabs->count(); ++i)
            if (auto *w = qobject_cast<scriptedit::ScriptEditor *>(ws->tabs->widget(i)))
                if (!w->rel.isEmpty() && !out.contains(w->rel))
                    out << w->rel;
        return out;
    }
    if (ws->refs)
        out = ws->refs->db->files;
    if (k == u"all" && ws->store) {
        const QString d = ws->store->mod.p({u"common"_s});
        if (fs::isdir(d)) {                      // (glob "*.txt": no hidden files; sorted by the whole path)
            QStringList names;
            for (const QString &f : fs::listdir(d))
                if (!f.startsWith(u'.') && f.toLower().endsWith(u".txt"_s))
                    names << f;
            std::sort(names.begin(), names.end());
            for (const QString &f : names)
                out << u"common/"_s + f;
        }
    }
    return out;
}

std::optional<QRegularExpression> SearchPanel::pattern()
{
    const QString q = find->text();
    if (q.isEmpty())
        return std::nullopt;
    QString body = (*opt.get(u"regex"_s))->isChecked() ? q : QRegularExpression::escape(q);
    if ((*opt.get(u"word"_s))->isChecked())
        body = u"(?<![A-Za-z0-9_])(?:%1)(?![A-Za-z0-9_])"_s.arg(body);
    QRegularExpression::PatternOptions o = QRegularExpression::UseUnicodePropertiesOption;
    if (!(*opt.get(u"case"_s))->isChecked())
        o |= QRegularExpression::CaseInsensitiveOption;
    QRegularExpression rx(body, o);
    if (!rx.isValid()) {
        info->setText(u"<span style=\"color:#ff786e\">Not a valid expression: %1</span>"_s.arg(rx.errorString()));
        return std::nullopt;
    }
    return rx;
}

void SearchPanel::open_panel(const std::optional<QString> &sc, bool replace)
{
    setVisible(true);
    if (sc)
        for (int i = 0; i < static_cast<int>(SCOPES.size()); ++i)
            if (SCOPES[static_cast<size_t>(i)].first == *sc)
                scope->setCurrentIndex(i);
    scriptedit::ScriptEditor *ed = ws->current();
    if (ed && ed->textCursor().hasSelection()) {
        const QString t = ed->textCursor().selectedText();
        if (!t.contains(u' ') && t.size() < 200)
            find->setText(t);
    }
    QLineEdit *w = replace ? repl : find;
    w->setFocus();
    w->selectAll();
    run();
}

void SearchPanel::close_panel()
{
    setVisible(false);
    _rx.reset();
    ws->_decorate_current();
}

void SearchPanel::run()
{
    // Search again (the text changed, the options changed …).
    tree->clear();
    results.clear();
    ++_gen;
    const auto rx = pattern();
    _rx = rx;
    if (!rx || !ws->store) {
        if (find->text().isEmpty())
            info->setText(QString());
        ws->_decorate_current();
        return;
    }
    scriptstore::ScriptStore &store = *ws->store;
    int n_files = 0;
    for (const QString &rel : files()) {
        QString text;
        try {
            text = store.text(rel);
        } catch (const fs::OSError &) {
            continue;
        }
        std::vector<std::pair<qsizetype, qsizetype>> hits;
        for (auto it = rx->globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            if (m.capturedEnd(0) == m.capturedStart(0))
                continue;
            hits.emplace_back(m.capturedStart(0), m.capturedEnd(0));
            if (static_cast<long long>(results.size() + hits.size()) >= LIMIT)
                break;
        }
        if (hits.empty())
            continue;
        ++n_files;
        auto *fit = new QTreeWidgetItem(QStringList{rel.split(u'/').last(), QString::number(hits.size())});
        fit->setToolTip(0, rel);
        QFont f(tree->font());
        f.setBold(true);
        fit->setFont(0, f);
        fit->setData(0, Qt::UserRole,
                     QStringList{rel, QString::number(hits[0].first), QString::number(hits[0].second)});
        fit->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        tree->addTopLevelItem(fit);
        for (size_t i = 0; i < hits.size() && i < 400; ++i) {
            const auto [a, b] = hits[i];
            const long long ln = refs::line_of(text, a);
            auto *it = new QTreeWidgetItem(
                QStringList{u"%1   %2"_s.arg(ln, 6).arg(relview::line_snippet(text, a, 140)), QString()});
            it->setFont(0, _mono);
            it->setData(0, Qt::UserRole, QStringList{rel, QString::number(a), QString::number(b)});
            it->setToolTip(0, relview::line_snippet(text, a, 500));
            fit->addChild(it);
        }
        for (const auto &[a, b] : hits)
            results.emplace_back(rel, a, b);
        if (static_cast<long long>(results.size()) >= LIMIT)
            break;
    }
    if (n_files <= 4)
        tree->expandAll();
    const QString note = _note;
    _note.clear();
    const long long n = static_cast<long long>(results.size());
    info->setText(u"%1%2 match%3 in %4 file%5%6"_s.arg(!note.isEmpty() ? note + u" · now "_s : QString())
                      .arg(n)
                      .arg(n == 1 ? QString() : u"es"_s)
                      .arg(n_files)
                      .arg(n_files == 1 ? QString() : u"s"_s)
                      .arg(n >= LIMIT ? u" (the first %1)"_s.arg(LIMIT) : QString()));
    ws->_decorate_current();
}

void SearchPanel::_clicked(QTreeWidgetItem *it, int)
{
    const QStringList d = it->data(0, Qt::UserRole).toStringList();
    if (d.size() == 3)
        Q_EMIT goTo(d[0], d[1].toLongLong(), d[2].toLongLong());
}

void SearchPanel::next()
{
    // The next match after the cursor (in this file, then the next files).
    if (results.empty())
        run();
    if (results.empty())
        return;
    scriptedit::ScriptEditor *ed = ws->current();
    std::optional<std::tuple<QString, qsizetype, qsizetype>> pick;
    if (ed) {
        const QString hr = ed->rel;
        const qsizetype hp = ed->textCursor().selectionEnd();
        for (const auto &r : results)
            if (std::get<0>(r) == hr && std::get<1>(r) >= hp) {
                pick = r;
                break;
            }
        if (!pick) {
            qsizetype last = -1;
            for (size_t i = 0; i < results.size(); ++i)
                if (std::get<0>(results[i]) == hr)
                    last = static_cast<qsizetype>(i);
            if (last >= 0)
                pick = results[static_cast<size_t>((last + 1) % static_cast<qsizetype>(results.size()))];
        }
    }
    const auto &[rel, a, b] = pick ? *pick : results[0];
    Q_EMIT goTo(rel, a, b);
}

// ------------------------------------------------------------------ replacing
QString SearchPanel::_repl_for(const QRegularExpressionMatch &m) const
{
    const QString r = repl->text();
    return (*opt.get(u"regex"_s))->isChecked() ? expand(m, r) : r;
}

void SearchPanel::replace_one()
{
    const auto rx = _rx ? _rx : pattern();
    scriptedit::ScriptEditor *ed = ws->current();
    if (!rx || !ed)
        return;
    const QTextCursor c = ed->textCursor();
    const QString text = ws->store->text(ed->rel);
    const qsizetype a = c.selectionStart(), b = c.selectionEnd();
    std::optional<QRegularExpressionMatch> m;
    if (c.hasSelection()) {
        const auto mm = rx->match(text, a, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
        if (mm.hasMatch())
            m = mm;
    }
    if (!m || m->capturedEnd(0) != b) {
        next();                                  // not on a match yet: go to one first
        return;
    }
    const QString nw = text.left(a) + _repl_for(*m) + text.mid(b);
    OrderedMap<QString, QString> ch;
    ch.insert(ed->rel, nw);
    Q_EMIT replaceRequested(ch, u"Replace “%1” (%2 line %3)"_s.arg(find->text(), ed->rel.split(u'/').last())
                                    .arg(text.left(a).count(u'\n') + 1));
    QTimer::singleShot(0, this, [this] {
        run();
        next();
    });
}

void SearchPanel::replace_all()
{
    const auto rx = _rx ? _rx : pattern();
    if (!rx)
        return;
    OrderedMap<QString, QString> changes;
    long long n = 0;
    QStringList rels;                            // dict.fromkeys: each file once, in order
    for (const auto &r : results)
        if (!rels.contains(std::get<0>(r)))
            rels << std::get<0>(r);
    for (const QString &rel : rels) {
        const QString text = ws->store->text(rel);
        QString nw;
        qsizetype at = 0;
        long long k = 0;
        for (auto it = rx->globalMatch(text); it.hasNext();) {   // (rx.subn)
            const auto m = it.next();
            nw += text.mid(at, m.capturedStart(0) - at) + _repl_for(m);
            at = m.capturedEnd(0);
            ++k;
        }
        nw += text.mid(at);
        if (k) {
            changes.insert(rel, nw);
            n += k;
        }
    }
    if (changes.isEmpty())
        return;
    Q_EMIT replaceRequested(changes, u"Replace all “%1” → “%2” (%3 in %4 files)"_s.arg(find->text(), repl->text())
                                         .arg(n)
                                         .arg(changes.size()));
    _note = u"Replaced %1 in %2 file%3 — Ctrl+Z undoes it, Apply writes it"_s.arg(n).arg(changes.size()).arg(
        changes.size() == 1 ? QString() : u"s"_s);
    QTimer::singleShot(0, this, [this] { run(); });
}

std::vector<std::pair<qsizetype, qsizetype>> SearchPanel::matches_in(const QString &rel, qsizetype a, qsizetype b)
{
    // The matches of the current search inside [a, b) of a file (for lighting them in the editor).
    if (!_rx || !isVisible())
        return {};
    if (_by_gen != _gen) {
        _by_file.clear();
        for (const auto &[r, x, y] : results)
            _by_file[r].emplace_back(x, y);
        _by_gen = _gen;
    }
    const auto it = _by_file.constFind(rel);
    if (it == _by_file.constEnd())
        return {};
    const auto &lst = *it;
    const auto lo = std::lower_bound(lst.begin(), lst.end(), std::pair<qsizetype, qsizetype>{a, -1});
    const auto hi = std::lower_bound(lst.begin(), lst.end(), std::pair<qsizetype, qsizetype>{b, -1});
    return {lo, hi};
}

}  // namespace scriptsearch
