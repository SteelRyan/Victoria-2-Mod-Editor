// applyjournal.py — line by line.
#include "core/applyjournal.h"
#include "core/fs.h"
#include "core/modcopy.h"
#include "core/pypath.h"
#include "core/pytext.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>
#include <algorithm>
#include <chrono>

using namespace Qt::StringLiterals;
using pypath::join;

namespace applyjournal {

static const QString SEP = u"\\"_s;                // os.sep

QString _norm(const QString &p) { return pypath::normcase(pypath::abspath(p)); }

static QString rel_slash(const QString &p, const QString &root) { return pypath::relpath(p, root).replace(u'\\', u'/'); }

Scan _scan(const QString &root)
{
    // {rel: (size, mtime_ns)} of every file under the mod, and the set of its folders.
    Scan out;
    if (!fs::isdir(root))
        return out;
    std::function<void(const QString &)> walk = [&](const QString &d) {
        const QString rd = pypath::relpath(d, root);
        out.dirs.insert(rd == u"." ? QString() : QString(rd).replace(u'\\', u'/'));
        QStringList subs;
        for (const fs::DirEntry &e : fs::scandir(d)) {
            if (e.is_dir) {
                if (d == root && modcopy::is_safe_dir(e.name))   // (C++ only: the mod's safeguards are not the mod)
                    continue;
                subs << e.path;
                continue;
            }
            out.files[rel_slash(e.path, root)] = {e.size, e.mtime_ns};
        }
        for (const QString &s : subs)
            walk(s);
    };
    walk(root);
    return out;
}

bool _same(const QString &a, const QString &b)
{
    QFile fa(a), fb(b);
    if (!fa.exists() || !fb.exists() || QFileInfo(a).size() != QFileInfo(b).size())
        return false;
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly))
        return false;
    while (true) {
        const QByteArray x = fa.read(1 << 20), y = fb.read(1 << 20);
        if (x != y)
            return false;
        if (x.isEmpty())
            return true;
    }
}

static QString now_text() { return QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH:mm:ss"_s); }

// ---------------------------------------------------------------------------------------------- the recorder
Recorder::Recorder(ApplyJournal &journal, const QString &folder_, const QString &label_, const QString &scenario_)
    : j(journal), folder(folder_), root(journal.mod_root), nroot(_norm(journal.mod_root) + SEP), label(label_),
      scenario(scenario_)
{
    pre = _scan(root);
}

QString Recorder::touch(const QString &path0, bool remove)
{
    // Watch a write; -> the path to write to (a base game file: its copy in the mod — copy-on-write).
    if (_in)
        return path0;
    QString path = path0, p = path0;
    QString n = _norm(p);
    std::optional<QString> to;
    _in = true;
    try {
        to = j.guard(p, n, remove);
    } catch (...) {
        _in = false;
        throw;
    }
    _in = false;
    std::optional<QString> copy_from;
    if (to && _norm(*to) != n) {
        copy_from = p;                               // (the base game's file: the mod's copy starts as it)
        p = *to;
        n = _norm(*to);
        path = *to;
    }
    if (!n.startsWith(nroot))
        return path;
    const QString rel = rel_slash(pypath::abspath(p), pypath::abspath(root));   // (its own case)
    const QString low = py::lower(rel);
    for (const auto &kv : files)
        if (py::lower(kv.first) == low)
            return path;
    _in = true;
    try {
        const QString src = join(root, rel.split(u'/'));
        const bool existed = fs::isfile(src);
        if (existed) {
            const QString dst = join(join(folder, u"before"_s), rel.split(u'/'));
            fs::makedirs(pypath::dirname(dst));
            fs::copy2(src, dst);
        }
        pyjson::Object rec;
        rec.insert(u"before"_s, pyjson::Value(existed));   // (a new copy: undoing the Apply takes it out again)
        files.insert(rel, pyjson::Value(rec));
        if (copy_from && !existed) {
            fs::makedirs(pypath::dirname(src));           // (a folder the mod hasn't got yet)
            if (fs::isfile(*copy_from))
                fs::copy2(*copy_from, src);
        }
    } catch (...) {
        _in = false;
        throw;
    }
    _in = false;
    return path;
}

void Recorder::install()
{
    fs::set_watch([this](const QString &path, bool remove) { return touch(path, remove); });
}

void Recorder::uninstall() { fs::set_watch(nullptr); }

std::optional<pyjson::Value> Recorder::finish(const std::optional<QString> &error)
{
    for (auto &[rel, rec] : files) {
        const QString src = join(root, rel.split(u'/'));
        const bool after = fs::isfile(src);
        rec.obj().insert(u"after"_s, pyjson::Value(after));
        if (after) {
            const QString dst = join(join(folder, u"after"_s), rel.split(u'/'));
            fs::makedirs(pypath::dirname(dst));
            fs::copy2(src, dst);
        }
    }
    const Scan post = _scan(root);
    std::set<QString> mine;
    for (const auto &kv : files)
        mine.insert(py::lower(kv.first));
    std::set<QString> all;
    for (const auto &kv : pre.files)
        all.insert(kv.first);
    for (const auto &kv : post.files)
        all.insert(kv.first);
    untracked.clear();
    for (const QString &r : all) {
        const auto a = pre.files.find(r);
        const auto b = post.files.find(r);
        const bool differ = (a == pre.files.end()) != (b == post.files.end()) ||
                            (a != pre.files.end() && a->second != b->second);
        if (differ && !mine.count(py::lower(r)))
            untracked << r;
    }
    pyjson::Object changed;
    for (const auto &[r, v] : files) {
        const bool before = v.get(u"before"_s)->truthy(), after = v.get(u"after"_s)->truthy();
        const bool same = before && after &&
                          _same(join(join(folder, u"before"_s), r.split(u'/')), join(join(folder, u"after"_s), r.split(u'/')));
        if (!same && (before || after))
            changed.insert(r, v);
    }
    files = changed;
    if (changed.isEmpty() && untracked.isEmpty()) {
        QDir(folder).removeRecursively();           // (shutil.rmtree(ignore_errors=True): the Apply's own folder)
        manifest.reset();
        return std::nullopt;
    }
    pyjson::Object m;
    m.insert(u"mod"_s, pyjson::Value(_norm(root)));
    m.insert(u"stamp"_s, pyjson::Value(pypath::basename(folder)));
    m.insert(u"label"_s, pyjson::Value(label));
    m.insert(u"scenario"_s, pyjson::Value(scenario));
    m.insert(u"when"_s, pyjson::Value(now_text()));
    m.insert(u"files"_s, pyjson::Value(changed));
    pyjson::Array ut;
    for (const QString &u : untracked)
        ut.emplace_back(u);
    m.insert(u"untracked"_s, pyjson::Value(ut));
    m.insert(u"state"_s, pyjson::Value(u"done"_s));
    pyjson::Array dirs;
    for (const QString &d : pre.dirs)               // (std::set: sorted, as sorted(self.pre_dirs))
        dirs.emplace_back(d);
    m.insert(u"dirs"_s, pyjson::Value(dirs));
    m.insert(u"error"_s, error ? pyjson::Value(*error) : pyjson::Value());
    m.insert(u"log"_s, pyjson::Value(pyjson::Array{}));
    manifest = pyjson::Value(m);
    j._write(folder, *manifest);
    return manifest;
}

// ---------------------------------------------------------------------------------------------- the journal
ApplyJournal::ApplyJournal(const QString &backup_root, const QString &mod_root_, const std::optional<QString> &base_root_)
    : dir(join(backup_root, u"applies"_s)), mod_root(mod_root_), base_root(base_root_)
{
}

std::optional<QString> ApplyJournal::guard(const QString &path, const QString &n0, bool remove) const
{
    // While Apply writes, every write is checked: is this already the mod's own file, or does it need a copy?
    // -> the path to write to (None: as it is). A write into the base game's own files goes into the MOD's copy of
    // that file instead (copy-on-write); removing a base game file is refused. A write into a `mod` folder at the top
    // of this mod is stopped.
    const QString n = n0.isEmpty() ? _norm(path) : n0;
    if (n.startsWith(_norm(pypath::dirname(dir)) + SEP))
        return std::nullopt;                         // the editor's own backups (wherever the editor is kept)
    const QString root = _norm(mod_root);
    if (n.startsWith(root + SEP)) {
        const QString first = n.mid(root.size() + 1).section(SEP, 0, 0);
        if (first == u"mod")
            throw WriteRefused((u"stopped a write into a 'mod' folder inside the mod (%1) — the game never reads it; "
                                u"nothing was written there"_s.arg(path)).toStdString());
        return std::nullopt;
    }
    if (base_root && !base_root->isEmpty()) {
        const QString base = _norm(*base_root);
        if (n.startsWith(base + SEP) && n.mid(base.size() + 1).section(SEP, 0, 0) != u"mod") {
            if (remove)
                throw WriteRefused((u"stopped removing a base game file (%1) — the base game is never changed"_s.arg(path))
                                       .toStdString());
            const QString rel = pypath::relpath(pypath::abspath(path), pypath::abspath(*base_root));
            return join(mod_root, rel);              // (touch copies the game's file there first)
        }
    }
    return std::nullopt;
}

void ApplyJournal::_write(const QString &folder, const pyjson::Value &man) const
{
    // (Python: open(…, "w", encoding="utf-8") — text mode: every "\n" written as "\r\n"; not watched)
    QFile f(join(folder, u"manifest.json"_s));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        throw fs::OSError(("cannot write " + f.fileName()).toStdString());
    f.write(pyjson::dumps(man, 1, false).replace(u"\n"_s, u"\r\n"_s).toUtf8());
}

std::vector<ApplyJournal::Entry> ApplyJournal::entries() const
{
    // This mod's Applies, oldest first: [(folder, manifest)].
    std::vector<Entry> out;
    if (!fs::isdir(dir))
        return out;
    const QString me = _norm(mod_root);
    QStringList names = fs::listdir(dir);
    std::sort(names.begin(), names.end());
    for (const QString &name : names) {
        QFile f(join(join(dir, name), u"manifest.json"_s));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray raw = f.readAll();
        QStringDecoder dec(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
        const QString text = dec(raw);
        if (dec.hasError())
            continue;
        const auto man = pyjson::loads(QString(text).replace(u"\r\n"_s, u"\n"_s));
        if (!man || !man->is_obj())
            continue;
        if (const pyjson::Value *m = man->get(u"mod"_s); m && m->str() == me)
            out.emplace_back(join(dir, name), *man);
    }
    return out;
}

void ApplyJournal::record(const QString &label, const QString &scenario, const std::function<void(Recorder &)> &fn,
                          std::optional<pyjson::Value> *manifest)
{
    using namespace std::chrono;
    const auto now = system_clock::now();
    const long long us = duration_cast<microseconds>(now.time_since_epoch()).count() % 1000000;
    const QString stamp = QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s) + u"_%1"_s.arg(us, 6, 10, QChar(u'0'));
    const QString folder = join(dir, stamp);
    fs::makedirs(folder);
    Recorder rec(*this, folder, label, scenario);
    rec.install();
    try {
        fn(rec);
    } catch (const std::exception &e) {
        rec.uninstall();
        rec.finish(QString::fromUtf8(e.what()));
        if (manifest)
            *manifest = rec.manifest;
        throw;
    }
    rec.uninstall();
    rec.finish(std::nullopt);
    if (manifest)
        *manifest = rec.manifest;
}

std::optional<ApplyJournal::Entry> ApplyJournal::undoable() const
{
    // The newest Apply still in the files (only it can be undone: the ones after it are built on it).
    std::optional<Entry> last;
    for (const Entry &e : entries())
        if (const pyjson::Value *s = e.second.get(u"state"_s); s && s->str() == u"done")
            last = e;
    return last;
}

std::optional<ApplyJournal::Entry> ApplyJournal::redoable() const
{
    // The Apply undone last — as long as no Apply came after it.
    const std::vector<Entry> es = entries();
    auto state = [](const Entry &e) {
        const pyjson::Value *s = e.second.get(u"state"_s);
        return s ? s->str() : QString();
    };
    auto stamp = [](const Entry &e) {
        const pyjson::Value *s = e.second.get(u"stamp"_s);
        return s ? s->str() : QString();
    };
    for (auto it = es.rbegin(); it != es.rend(); ++it)
        if (state(*it) == u"done") {
            // a newer Apply in the files than any undone one: nothing to redo
            for (const Entry &x : es)
                if (state(x) == u"undone" && stamp(x) > stamp(*it))
                    return x;
            return std::nullopt;
        }
    for (const Entry &x : es)
        if (state(x) == u"undone")
            return x;
    return std::nullopt;
}

std::pair<long long, QStringList> ApplyJournal::_put(const QString &folder, const pyjson::Value &man, const QString &side) const
{
    // Puts the files of one side ("before" / "after") back into the mod; what is there now is kept in replaced-<n>/
    // first. -> (restored, changed since: [rel] that differed from the other side).
    long long n = 1;
    for (const QString &d : fs::listdir(folder))
        if (d.startsWith(u"replaced-"))
            ++n;
    const QString keep = join(folder, u"replaced-%1"_s.arg(n));
    const QString other = side == u"before" ? u"after"_s : u"before"_s;
    QStringList changed_since;
    long long done = 0;
    std::set<QString> dirs;
    if (const pyjson::Value *ds = man.get(u"dirs"_s); ds && ds->is_arr())
        for (const pyjson::Value &x : ds->arr())
            dirs.insert(x.str());
    std::vector<std::pair<QString, pyjson::Value>> items;
    if (const pyjson::Value *fv = man.get(u"files"_s); fv && fv->is_obj())
        items.assign(fv->obj().begin(), fv->obj().end());
    std::sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    auto flag = [](const pyjson::Value &rec, const QString &k) {
        const pyjson::Value *x = rec.get(k);
        return x && x->truthy();
    };
    for (const auto &[rel, rec] : items) {
        const QString cur = join(mod_root, rel.split(u'/'));
        if (fs::isfile(cur)) {
            const QString dst = join(keep, rel.split(u'/'));
            fs::makedirs(pypath::dirname(dst));
            fs::copy2(cur, dst);
            const QString theirs = join(join(folder, other), rel.split(u'/'));
            if (!(flag(rec, other) && _same(cur, theirs)))
                changed_since << rel;
        } else if (flag(rec, other)) {
            changed_since << rel;
        }
        if (flag(rec, side)) {
            fs::makedirs(pypath::dirname(cur));
            fs::copy2(join(join(folder, side), rel.split(u'/')), cur);
        } else if (fs::isfile(cur)) {
            fs::remove(cur);
            QString d = pypath::dirname(cur);       // folders the Apply made: gone again when empty
            while (_norm(d) != _norm(mod_root)) {
                const QString rd = pypath::relpath(d, mod_root).replace(u'\\', u'/');
                if (dirs.count(rd) || !fs::listdir(d).isEmpty())
                    break;
                fs::rmdir(d);
                d = pypath::dirname(d);
            }
        }
        ++done;
    }
    return {done, changed_since};
}

static std::pair<long long, QStringList> put_and_log(ApplyJournal &j, ApplyJournal::Entry &entry, const QString &side,
                                                     const QString &state, const QString &what)
{
    auto &[folder, man] = entry;
    const auto res = j._put(folder, man, side);
    man.obj().insert(u"state"_s, pyjson::Value(state));
    pyjson::Object log;
    log.insert(what, pyjson::Value(now_text()));
    pyjson::Array since;
    for (const QString &r : res.second)
        since.emplace_back(r);
    log.insert(u"changed_since"_s, pyjson::Value(since));
    if (pyjson::Value *l = man.obj().get(u"log"_s); l && l->is_arr())
        l->arr().emplace_back(log);
    j._write(folder, man);
    return res;
}

std::pair<long long, QStringList> ApplyJournal::undo(Entry &entry) { return put_and_log(*this, entry, u"before"_s, u"undone"_s, u"undone"_s); }
std::pair<long long, QStringList> ApplyJournal::redo(Entry &entry) { return put_and_log(*this, entry, u"after"_s, u"done"_s, u"redone"_s); }

QString describe(const pyjson::Value &man, long long n)
{
    // '3 files: events/a.txt, map/region.txt, …'
    QStringList rels;
    if (const pyjson::Value *f = man.get(u"files"_s); f && f->is_obj())
        for (const auto &kv : f->obj())
            rels << kv.first;
    std::sort(rels.begin(), rels.end());
    return u"%1 file%2: %3%4"_s.arg(QString::number(rels.size()), rels.size() == 1 ? QString() : u"s"_s,
                                    rels.mid(0, n).join(u", "_s), rels.size() > n ? u", …"_s : QString());
}

}  // namespace applyjournal
