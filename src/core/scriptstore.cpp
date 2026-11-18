// scriptstore.py — the script files as the editor holds them. Line by line.
#include "core/scriptstore.h"
#include "core/fs.h"
#include "core/pypath.h"
#include "core/pytext.h"

#include <QDateTime>
#include <QTextCursor>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace scriptstore {

using pypath::join;

const QString ScriptStore::GONE = QString(QChar(0)) + u"gone"_s;
std::function<void(QTextDocument *)> ScriptStore::prepare_doc;

QString _norm(const QString &t)
{
    QString s = t;
    s.replace(u"\r\n"_s, u"\n"_s);
    s.replace(u'\r', u'\n');
    return s;
}

// _TOK = r'"[^"\n]*"?|#[^\n]*|[{}=]|[^\s{}="#]+'  (findall: what matches none of them — blank space — is skipped)
static QStringList tokens(const QString &text)
{
    QStringList out;
    const qsizetype n = text.size();
    qsizetype i = 0;
    while (i < n) {
        const QChar c = text[i];
        if (c == u'"') {
            qsizetype j = i + 1;
            while (j < n && text[j] != u'"' && text[j] != u'\n')
                ++j;
            if (j < n && text[j] == u'"')
                ++j;
            out << text.mid(i, j - i);
            i = j;
        } else if (c == u'#') {
            qsizetype j = i + 1;
            while (j < n && text[j] != u'\n')
                ++j;
            out << text.mid(i, j - i);
            i = j;
        } else if (c == u'{' || c == u'}' || c == u'=') {
            out << QString(c);
            ++i;
        } else if (py::is_py_space(c.unicode())) {
            ++i;
        } else {
            qsizetype j = i;
            while (j < n && !py::is_py_space(text[j].unicode()) && text[j] != u'{' && text[j] != u'}' &&
                   text[j] != u'=' && text[j] != u'"' && text[j] != u'#')
                ++j;
            out << text.mid(i, j - i);
            i = j;
        }
    }
    return out;
}

// _NUM = r"^-?\d+\.\d+$"
static bool is_num(const QString &t)
{
    qsizetype i = t.startsWith(u'-') ? 1 : 0;
    const qsizetype d0 = i;
    while (i < t.size() && t[i].category() == QChar::Number_DecimalDigit)
        ++i;
    if (i == d0 || i >= t.size() || t[i] != u'.')
        return false;
    const qsizetype d1 = ++i;
    while (i < t.size() && t[i].category() == QChar::Number_DecimalDigit)
        ++i;
    return i > d1 && i == t.size();
}

QStringList canon(const QString &text)
{
    // What a script text says, without how it is laid out: its words, braces, quoted texts and comments in order
    // — blank space, line breaks and indentation don't count, and neither does how a number is spelled.
    QStringList out;
    for (QString t : tokens(text)) {
        if (is_num(t)) {
            while (t.endsWith(u'0'))
                t.chop(1);
            while (t.endsWith(u'.'))
                t.chop(1);
            if (t == u"-0" || t == u"-" || t.isEmpty())
                t = u"0"_s;
        } else if (t[0] == u'#') {
            t = py::rstrip(t);
        }
        out << t;
    }
    return out;
}

ScriptStore::~ScriptStore()
{
    for (auto &kv : docs)
        delete kv.second;
}

QString ScriptStore::path(const QString &rel) const { return mod.p(rel.split(u'/')); }

void ScriptStore::_load(const QString &rel)
{
    if (!orig.contains(rel)) {
        const QString p = path(rel);
        const QString raw = fs::isfile(p) ? script::read_text(p) : QString();
        nl.insert(rel, raw.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s);
        orig.insert(rel, _norm(raw));
    }
}

QString ScriptStore::text(const QString &rel)
{
    QTextDocument *const *d = docs.get(rel);
    if (!d) {
        _load(rel);
        return *orig.get(rel);
    }
    return text_c(rel);
}

QString ScriptStore::text_c(const QString &rel) const
{
    QTextDocument *d = *docs.get(rel);
    const long long rev = _rev.value(rel, 0);
    auto &cache = const_cast<OrderedMap<QString, std::pair<long long, QString>> &>(_cache);
    const auto *got = cache.get(rel);
    if (!got || got->first != rev) {
        cache.insert(rel, {rev, d->toPlainText()});
        got = cache.get(rel);
    }
    return got->second;
}

void ScriptStore::_change(const QString &rel, int pos, int removed, int added)
{
    // A change Qt reports for a text: the TEXT changed — or it was only coloured. Only a change of the text counts
    // (its own counter `_rev`: Qt's revision number goes up for colouring too).
    const auto *got = _cache.get(rel);
    if (removed == added && got && got->first == _rev.value(rel, 0)) {
        QTextDocument *d = *docs.get(rel);
        QTextCursor c(d);
        const int end = std::min(pos + added, std::max(0, d->characterCount() - 1));
        c.setPosition(std::min(pos, end));
        c.setPosition(end, QTextCursor::KeepAnchor);
        QString sel = c.selectedText();
        sel.replace(QChar(0x2029), u'\n');               // (Qt's paragraph separator in a selection)
        if (sel == got->second.mid(pos, end - pos))
            return;                                      // coloured only
    }
    _rev.insert(rel, _rev.value(rel, 0) + 1);
    _changed(rel);
}

QTextDocument *ScriptStore::document(const QString &rel)
{
    if (QTextDocument *const *d = docs.get(rel))
        return *d;
    _load(rel);
    auto *d = new QTextDocument();
    if (prepare_doc)
        prepare_doc(d);
    // a QTextDocument emits contentsChange only when it has a layout (Qt: QTextDocumentPrivate::finishEdit) — the
    // window part's plain-text one, or (no window: the checks) Qt's default, made here
    d->documentLayout();
    d->setUndoRedoEnabled(false);
    d->setPlainText(*orig.get(rel));
    // (the text's own undo stays on only for its revision number; Ctrl+Z / Ctrl+Y in a text go to the editor's one
    // undo list (Document.record_live) — the tester's report, 2026-10-14)
    d->setUndoRedoEnabled(true);
    d->setModified(false);
    QObject::connect(d, &QTextDocument::contentsChange, d,
                     [this, rel](int pos, int rem, int add) { _change(rel, pos, rem, add); });
    docs.insert(rel, d);
    return d;
}

void ScriptStore::_changed(const QString &rel)
{
    const auto fns = listeners;
    for (const auto &fn : fns)
        fn(rel);
}

void ScriptStore::set_text(const QString &rel, const QString &text0)
{
    // The file's text becomes `text` — one step of the editor's own undo, only the part that differs is replaced (so
    // cursors elsewhere stay put).
    const QString t = _norm(text0);
    gone.remove(rel);
    const QString old = text(rel);
    if (old == t)
        return;
    QTextDocument *d = document(rel);
    const qsizetype n = std::min(old.size(), t.size());
    qsizetype i = 0;
    while (i < n && old[i] == t[i])
        ++i;
    qsizetype j = 0;
    while (j < n - i && old[old.size() - 1 - j] == t[t.size() - 1 - j])
        ++j;
    QTextCursor c(d);
    c.beginEditBlock();
    c.setPosition(int(i));
    c.setPosition(int(old.size() - j), QTextCursor::KeepAnchor);
    c.insertText(t.mid(i, t.size() - j - i));
    c.endEditBlock();
}

void ScriptStore::remove(const QString &rel)
{
    // The file goes out of the mod on Apply (only a file of the mod's own can).
    gone.insert(rel);
    _changed(rel);
}

bool ScriptStore::_on_disk(const QString &rel) const { return fs::isfile(join(mod.root, rel.split(u'/'))); }

bool ScriptStore::same(const QString &rel)
{
    // The file says what it says on disk (it may be spaced differently: that is no change).
    const QString t = text(rel);
    const QString &o = *orig.get(rel);
    if (t == o)
        return true;
    if (!_canon_orig.contains(rel))
        _canon_orig.insert(rel, canon(o));
    const auto *got = _canon.get(rel);
    if (!got || got->first != t) {
        _canon.insert(rel, {t, canon(t)});
        got = _canon.get(rel);
    }
    return got->second == *_canon_orig.get(rel);
}

QStringList ScriptStore::dirty() const
{
    auto *self = const_cast<ScriptStore *>(this);
    QSet<QString> out;
    for (const auto &kv : docs)
        if (!gone.contains(kv.first) && !self->same(kv.first))
            out.insert(kv.first);
    for (const QString &rel : gone)
        if (_on_disk(rel))
            out.insert(rel);
    QStringList l(out.begin(), out.end());
    std::sort(l.begin(), l.end());
    return l;
}

QString ScriptStore::live_sig() const
{
    // Cheap: changes when a text changes, not when it is only coloured (Document.record_live asks first).
    QStringList parts;
    for (const auto &kv : docs)
        parts << kv.first + u'\x1f' + QString::number(_rev.value(kv.first, 0));
    QStringList g(gone.begin(), gone.end());
    std::sort(g.begin(), g.end());
    return parts.join(u'\x1e') + u'\x1d' + g.join(u'\x1e');
}

store::Snap ScriptStore::snapshot() const
{
    store::Items snap;
    for (const auto &kv : docs) {
        const QString &rel = kv.first;
        const long long rev = _rev.value(rel, 0);
        const QString o = orig.value(rel);
        const auto *got = _diff.get(rel);
        if (!got || std::get<0>(*got) != rev || std::get<2>(*got).constData() != o.constData()) {
            const QString t = text_c(rel);
            _diff.insert(rel, {rev, t != o, o});
            got = _diff.get(rel);
        }
        if (std::get<1>(*got))
            snap.insert(store::Key(rel), store::box(text_c(rel)));
    }
    if (!gone.isEmpty()) {
        QStringList g(gone.begin(), gone.end());
        std::sort(g.begin(), g.end());
        snap.insert(store::Key(GONE), store::box(g));
    }
    return std::make_shared<const store::ItemsSnap>(snap, false);
}

QSet<store::Key> ScriptStore::part_keys(const store::Items &a, const store::Items &b) const
{
    // For an undo that puts back only what its step changed: a file taken out of the mod (or put back) changed too.
    auto gone_of = [](const store::Items &x) {
        QSet<QString> s;
        if (const store::Snap *g = x.get(store::Key(GONE)))
            for (const QString &r : store::unbox<QStringList>(*g))
                s.insert(r);
        return s;
    };
    const QSet<QString> ga = gone_of(a), gb = gone_of(b);
    QSet<store::Key> out;
    for (const QString &r : (ga - gb) | (gb - ga))
        out.insert(store::Key(r));
    return out;
}

void ScriptStore::restore(const store::Snap &s)
{
    const store::Items &snap = dynamic_cast<const store::ItemsSnap &>(*s).items;
    QSet<QString> g;
    if (const store::Snap *gs = snap.get(store::Key(GONE)))
        for (const QString &r : store::unbox<QStringList>(*gs))
            g.insert(r);
    QStringList rels;
    for (const auto &kv : docs)
        rels << kv.first;
    for (const auto &kv : snap)
        if (!kv.first.is_int() && kv.first.text() != GONE && !rels.contains(kv.first.text()))
            rels << kv.first.text();
    for (const QString &rel : rels) {
        const store::Snap *t = snap.get(store::Key(rel));
        set_text(rel, t ? store::unbox<QString>(*t) : orig.value(rel));
    }
    gone = g;
}

// --- undo past an Apply: a step's picture of the texts says nothing about a file that was "as on disk" then
store::Snap ScriptStore::rebase_begin()
{
    OrderedMap<QString, QString> tok;
    for (const QString &rel : dirty())
        tok.insert(rel, orig.value(rel));
    return store::box(tok);
}

store::Snap ScriptStore::rebase(const store::Snap &snap, const store::Snap &old)
{
    store::Items items = dynamic_cast<const store::ItemsSnap &>(*snap).items;
    for (const auto &[rel, t] : store::unbox<OrderedMap<QString, QString>>(old))
        if (!items.contains(store::Key(rel)))
            items.insert(store::Key(rel), store::box(t));
    return std::make_shared<const store::ItemsSnap>(items, false);
}

std::optional<QStringList> ScriptStore::save(const QString &backup_root)
{
    const QStringList rels = dirty();
    if (rels.isEmpty())
        return std::nullopt;
    const QString stamp = QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s);
    QStringList written;
    for (const QString &rel : rels) {
        const QString dst = join(mod.root, rel.split(u'/'));
        const QString reldir = pypath::dirname(rel);
        if (gone.contains(rel)) {                    // taken out of the mod: a backup, then the file goes
            const QString bdir = join(backup_root, {u"scripts"_s, stamp, reldir});
            fs::makedirs(bdir);
            fs::copy2(dst, join(bdir, pypath::basename(rel)));
            fs::remove(dst);
            orig.insert(rel, QString());             // (undone after this, its text counts as a change again)
            _canon_orig.remove(rel);
            written << dst;
            continue;
        }
        if (fs::exists(dst)) {
            const QString bdir = join(backup_root, {u"scripts"_s, stamp, reldir});
            fs::makedirs(bdir);
            fs::copy2(dst, join(bdir, pypath::basename(rel)));
        }
        fs::makedirs(pypath::dirname(dst));
        const QString t = text(rel);
        QString out = t;
        if (nl.value(rel) == u"\r\n")
            out.replace(u"\n"_s, u"\r\n"_s);
        script::write_text(dst, out);
        orig.insert(rel, t);
        _canon_orig.remove(rel);
        (*docs.get(rel))->setModified(false);
        written << dst;
    }
    return written;
}

}  // namespace scriptstore
