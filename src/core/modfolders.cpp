// modfolders — see modfolders.h.
#include "core/modfolders.h"

#include "core/fs.h"
#include "core/modcopy.h"
#include "core/pypath.h"
#include "core/submods.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace modfolders {

static QString norm(const QString &p)
{
    QString s = QDir::cleanPath(QDir::fromNativeSeparators(pypath::abspath(p))).toLower();
    while (s.endsWith(u'/') && s.size() > 3)
        s.chop(1);
    return s;
}

QString free_name(const QString &dir, const QString &base, const QString &modfile_dir, const QString &own_modfile)
{
    auto taken = [&](const QString &n) {   // (the mod's own .mod file, which goes along, is no other mod's)
        const QString mf = pypath::join(modfile_dir, n + u".mod"_s);
        return fs::exists(pypath::join(dir, n)) ||
               (fs::exists(mf) && (own_modfile.isEmpty() || norm(mf) != norm(own_modfile)));
    };
    if (!taken(base))
        return base;
    for (int k = 2;; ++k)
        if (const QString n = u"%1 (%2)"_s.arg(base).arg(k); !taken(n))
            return n;
}

QString with_path(const QString &text, const QString &rel, const QString &name)
{
    QString t = text;
    const QString nl = t.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    auto set = [&](const QString &key, const QString &value) {
        const QRegularExpression re(uR"((?m)^([ \t]*))"_s + key + uR"([ \t]*=[^\r\n]*)"_s);
        const QString line = key + u" = \""_s + value + u'"';
        if (const auto mt = re.match(t); mt.hasMatch())
            t.replace(mt.capturedStart(), mt.capturedLength(), mt.captured(1) + line);
        else
            t = (t.isEmpty() || t.endsWith(u'\n') ? t : t + nl) + line + nl;
    };
    set(u"path"_s, u"mod/"_s + rel);
    if (!name.isEmpty())
        set(u"name"_s, name);
    return t;
}

static QString read_latin1(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw fs::OSError("open: " + path.toStdString(), 2);
    return QString::fromLatin1(f.readAll());
}

static void write_latin1(const QString &path, const QString &text)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        throw fs::OSError("open: " + path.toStdString(), 13);
    f.write(text.toLatin1());
}

// every file below `src` to the same place below `dst`; the safeguards folder at the top stays out when `skip_safe`
static void copy_tree(const QString &src, const QString &dst, bool skip_safe)
{
    fs::makedirs(dst, true);
    for (const fs::DirEntry &e : fs::scandir(src)) {
        const QString to = pypath::join(dst, e.name);
        if (e.is_dir) {
            if (skip_safe && modcopy::is_safe_dir(e.name))
                continue;
            copy_tree(e.path, to, false);
        } else if (e.is_file) {
            fs::copy2(e.path, to);
        }
    }
}

Result move_mod(const moddata::ModEntry &m, const QString &to0, bool copy, const std::vector<moddata::ModEntry> &all)
{
    Result r;
    if (!m.folder || !fs::isdir(*m.folder)) {
        r.error = u"the folder of “%1” is not there"_s.arg(m.name);
        return r;
    }
    const QString src = pypath::abspath(*m.folder), to = pypath::abspath(to0);
    const bool into_safe = modcopy::is_safe_dir(pypath::basename(to));
    const QString owner = into_safe ? pypath::dirname(to) : QString();
    if (modcopy::inside(to, src)) {
        if (into_safe && norm(owner) == norm(src) && copy) {   // Ctrl+drag a mod onto its own safeguards: a safeguard now
            const modcopy::Result k = modcopy::keep_original(src);
            r.ok = k.ok;
            r.error = k.error;
            r.folder = k.folder;
            r.modfile = k.modfile;
            r.name = k.name;
            return r;
        }
        r.error = into_safe ? u"“%1” can't go into its own safeguards — Ctrl+drag makes a safeguard (a copy) of it"_s.arg(m.name)
                            : u"“%1” can't go into a folder inside itself"_s.arg(m.name);
        return r;
    }
    if (norm(pypath::dirname(src)) == norm(to)) {
        r.error = u"“%1” is in that folder already"_s.arg(m.name);
        return r;
    }
    const QString mdir = into_safe ? pypath::dirname(owner) : to;   // where the .mod file goes
    const QString base = pypath::basename(src);
    const QString fname = free_name(to, base, mdir, copy ? QString() : m.modfile.value_or(QString()));
    const QString dst = pypath::join(to, fname);
    const QString rel = into_safe ? pypath::relpath(dst, mdir).replace(u'\\', u'/') : fname;
    const QString name = fname == base ? m.name : m.name + fname.mid(base.size());
    r.folder = dst;
    r.modfile = pypath::join(mdir, fname + u".mod"_s);
    r.name = name;
    try {
        fs::makedirs(to, true);
        // the mod's safeguards that have a .mod file (they follow a move)
        std::vector<const moddata::ModEntry *> safe;
        if (!copy)
            for (const moddata::ModEntry &x : all)
                if (x.folder && x.modfile && norm(modcopy::safeguard_owner(*x.folder)) == norm(src) && fs::isfile(*x.modfile))
                    safe.push_back(&x);
        if (copy) {
            copy_tree(src, dst, true);
        } else if (!QDir().rename(src, dst)) {   // (another drive: copied, the old folder to the Recycle Bin)
            copy_tree(src, dst, false);
            if (!QFile::moveToTrash(src))
                r.error = u"copied to %1, but the old folder could not go to the Recycle Bin: %2"_s.arg(dst, src);
        }
        const bool had = m.modfile && fs::isfile(*m.modfile);
        if (had) {
            const QString text = with_path(read_latin1(*m.modfile), rel, fname == base ? QString() : name);
            if (!copy && norm(*m.modfile) != norm(r.modfile) && !QFile::rename(*m.modfile, r.modfile))
                throw fs::OSError("rename: " + m.modfile->toStdString(), 13);
            write_latin1(r.modfile, text);
        } else {
            submods::write_modfile(r.modfile, name, rel, m.deps, m.user_dir, m.replace);
        }
        for (const moddata::ModEntry *x : safe) {   // the moved mod's safeguards: their .mod files next to it
            const QString sg = pypath::join(dst, {modcopy::SAFE_DIR, pypath::basename(*x->folder)});
            const QString smdir = pypath::dirname(dst);
            const QString sfile = pypath::join(smdir, pypath::basename(*x->modfile));
            const QString srel = pypath::relpath(sg, smdir).replace(u'\\', u'/');
            const QString text = with_path(read_latin1(*x->modfile), srel);
            if (norm(*x->modfile) != norm(sfile)) {
                if (fs::exists(sfile))
                    continue;                    // (a .mod of that name is there: this one stays where it was)
                if (!QFile::rename(*x->modfile, sfile))
                    continue;
            }
            write_latin1(sfile, text);
        }
    } catch (const std::exception &e) {
        r.error = QString::fromUtf8(e.what());
        return r;
    }
    r.ok = r.error.isEmpty();
    return r;
}

}  // namespace modfolders
