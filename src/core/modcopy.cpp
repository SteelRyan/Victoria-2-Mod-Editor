// modcopy — see modcopy.h.
#include "core/modcopy.h"

#include "core/fs.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/submods.h"

#include <QDir>
#include <QHash>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace modcopy {

static QString norm(const QString &p)
{
    QString s = QDir::cleanPath(QDir::fromNativeSeparators(pypath::abspath(p))).toLower();
    while (s.endsWith(u'/') && s.size() > 3)
        s.chop(1);
    return s;
}

bool inside(const QString &path, const QString &folder)
{
    const QString p = norm(path), f = norm(folder);
    return p == f || p.startsWith(f + u'/');
}

QString copy_folder_name(const QString &folder_name, const QDateTime &when)
{
    return u"%1 (original %2)"_s.arg(folder_name, when.toString(u"yyyy-MM-dd HHmm"_s));
}

QString copy_mod_name(const QString &name, const QDateTime &when)
{
    return u"%1 (original %2)"_s.arg(name, when.toString(u"yyyy-MM-dd HH:mm"_s));
}

const QString SAFE_DIR = u"ModEditor safeguards"_s;

bool is_safe_dir(const QString &name) { return name.compare(SAFE_DIR, Qt::CaseInsensitive) == 0; }

QString safeguard_owner(const QString &folder)
{
    const QString f = pypath::abspath(folder);
    const QString home = pypath::dirname(f);
    return is_safe_dir(pypath::basename(home)) ? pypath::dirname(home) : QString();
}

QString safeguard_home(const QString &folder)
{
    const QString f = pypath::abspath(folder);
    return safeguard_owner(f).isEmpty() ? pypath::join(f, SAFE_DIR) : pypath::dirname(f);
}

QStringList safeguards(const QString &mod_folder)
{
    QStringList out;
    const QString home = pypath::join(pypath::abspath(mod_folder), SAFE_DIR);
    if (!fs::isdir(home))
        return out;
    try {
        for (const fs::DirEntry &e : fs::scandir(home))
            if (e.is_dir)
                out << e.path;
    } catch (const fs::OSError &) {
    }
    std::stable_sort(out.begin(), out.end(), [](const QString &a, const QString &b) {   // (the date in the name)
        return pypath::basename(a).toLower() > pypath::basename(b).toLower();
    });
    return out;
}

// every file below `src`, copied to the same place below `dst` (folders made as needed); counts what was copied. The
// safeguards folder at the top of the mod stays out (no copy holds the copies before it).
static void copy_tree(const QString &src, const QString &dst, Result &r, bool top = true)
{
    fs::makedirs(dst, false);
    for (const fs::DirEntry &e : fs::scandir(src)) {
        const QString to = pypath::join(dst, e.name);
        if (e.is_dir) {
            if (top && is_safe_dir(e.name))
                continue;
            copy_tree(e.path, to, r, false);
        } else if (e.is_file) {
            fs::copy2(e.path, to);
            ++r.files;
            r.bytes += e.size;
        }
    }
}

Result keep_original(const QString &folder0, const QDateTime &when)
{
    Result r;
    const QString folder = pypath::abspath(folder0);
    const QString home = safeguard_home(folder);
    const QString owner = pypath::dirname(home);           // the mod itself (a safeguard: the mod it belongs to)
    const QString mods_dir = pypath::dirname(owner);        // the game's mod folder: the .mod files lie there
    const QString base = pypath::basename(folder);
    if (!fs::isdir(folder)) {
        r.error = u"the mod folder %1 is not there"_s.arg(folder);
        return r;
    }
    // the mod's own .mod file: its name, dependencies, replace_path, user_dir carry over
    const auto [mods, _base] = moddata::discover_mods(mods_dir);
    const moddata::ModEntry *me = nullptr;
    for (const moddata::ModEntry &m : mods)
        if (m.folder && norm(*m.folder) == norm(folder)) {
            me = &m;
            break;
        }
    const QString name = me ? me->name : base;
    // a free name: the minute, else with the seconds, else a number
    QString fname = copy_folder_name(base, when), mname = copy_mod_name(name, when);
    auto taken = [&](const QString &f) {
        return fs::exists(pypath::join(home, f)) || fs::exists(pypath::join(mods_dir, f + u".mod"_s));
    };
    if (taken(fname)) {
        fname = u"%1 (original %2)"_s.arg(base, when.toString(u"yyyy-MM-dd HHmmss"_s));
        mname = u"%1 (original %2)"_s.arg(name, when.toString(u"yyyy-MM-dd HH:mm:ss"_s));
    }
    for (int k = 2; taken(fname); ++k) {
        fname = u"%1 (original %2 %3)"_s.arg(base, when.toString(u"yyyy-MM-dd HHmmss"_s)).arg(k);
        mname = u"%1 (original %2 %3)"_s.arg(name, when.toString(u"yyyy-MM-dd HH:mm:ss"_s)).arg(k);
    }
    r.folder = pypath::join(home, fname);
    r.modfile = pypath::join(mods_dir, fname + u".mod"_s);
    r.name = mname;
    try {
        copy_tree(folder, r.folder, r);
        std::optional<QString> user_dir;
        if (me && me->user_dir && !me->user_dir->isEmpty())
            user_dir = *me->user_dir + u"_original"_s;   // (its own saves / caches: the two mods differ from now on)
        // path = "mod/<mod>/ModEditor safeguards/<copy>" (below the game's mod folder)
        const QString rel = pypath::relpath(r.folder, mods_dir).replace(u'\\', u'/');
        submods::write_modfile(r.modfile, mname, rel, me ? me->deps : QStringList{}, user_dir,
                               me ? me->replace : QStringList{});
    } catch (const std::exception &e) {
        r.error = QString::fromUtf8(e.what());
        return r;
    }
    r.ok = true;
    return r;
}

// ------------------------------------------------------------------ the guard
namespace {
struct Armed {
    QString folder, modfile;
    std::function<bool()> on;
    std::function<void(const Result &)> done;
};
std::optional<Armed> g_armed;
int g_id = 0;
QHash<QString, QString> g_copied;            // normalised folder -> the copy ("" = not needed)
bool g_busy = false;
}  // namespace

bool copied(const QString &folder) { return g_copied.contains(norm(folder)); }
void mark_copied(const QString &folder) { g_copied.insert(norm(folder), QString()); }
QString copy_of(const QString &folder) { return g_copied.value(norm(folder)); }

static void guard(const QString &path)
{
    if (!g_armed || g_busy || copied(g_armed->folder))
        return;
    if (!inside(path, g_armed->folder) && norm(path) != norm(g_armed->modfile))
        return;
    if (g_armed->on && !g_armed->on())
        return;
    if (qEnvironmentVariable("MODEDITOR_KEEP_COPY") == u"0")
        return;
    g_busy = true;                               // (the copy's own writes pass the guard too)
    const Result r = keep_original(g_armed->folder);
    g_busy = false;
    if (r.ok)
        g_copied.insert(norm(g_armed->folder), r.folder);
    if (g_armed->done)
        g_armed->done(r);
    if (!r.ok)                                   // nothing is written into a mod whose copy could not be made
        throw fs::OSError(("the copy of the original mod could not be made (" + r.error.toStdString() +
                           ") — nothing was written. Untick \"Keep a copy of the original mod\" to write anyway.")
                              .c_str(),
                          13);
}

int arm(const QString &folder, const QString &modfile, std::function<bool()> on,
        std::function<void(const Result &)> done)
{
    QString mf = modfile;
    if (mf.isEmpty()) {                          // the mod's own .mod file (its name line is a write into the mod too)
        const auto [mods, _base] = moddata::discover_mods(pypath::dirname(pypath::abspath(folder)));
        for (const moddata::ModEntry &m : mods)
            if (m.folder && m.modfile && norm(*m.folder) == norm(folder)) {
                mf = *m.modfile;
                break;
            }
    }
    g_armed = Armed{folder, mf, std::move(on), std::move(done)};
    fs::set_guard(guard);
    return ++g_id;
}

void disarm(int id)
{
    if (id != g_id)
        return;
    g_armed.reset();
    fs::set_guard(nullptr);
}

}  // namespace modcopy
