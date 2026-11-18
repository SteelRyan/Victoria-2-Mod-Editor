// checksum.py — see checksum.h.
#include "map/checksum.h"
#include "core/fs.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"
#include "core/submods.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>
#include <algorithm>
#include <chrono>
#include <set>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>

using namespace Qt::StringLiterals;

namespace checksum {

const QString PROBE = u"ModEditor checksum probe"_s;
const QString LETTERS = u"ABCDEFGHIJKLMNOPQRSTUVWXYZ"_s;
const QString SETTINGS =
    u"gui=\r\n{\r\nlanguage=l_english\r\n}\r\ngraphics=\r\n{\r\nsize=\r\n{\r\n\tx=1024\r\n\ty=768\r\n}\r\n\r\n"
    u"refreshRate=60\r\nfullScreen=no\r\nborderless=no\r\nshadows=no\r\nshadowSize=2048\r\nmulti_sampling=0\r\n"
    u"anisotropic_filtering=0\r\ngamma=50.000000\r\n}\r\nsound_fx_volume=0.000000\r\nmusic_volume=0.000000\r\n"
    u"master_volume=0.000000\r\n"_s;
const double TIMEOUT = 300.0;

QString documents_dir()
{
    wchar_t buf[260] = {};
    if (SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, buf) == 0 && buf[0])
        return pypath::join(QString::fromWCharArray(buf), {u"Paradox Interactive"_s, u"Victoria II"_s});
    // os.path.expanduser("~") on Windows: USERPROFILE, else HOMEDRIVE + HOMEPATH
    QString home = qEnvironmentVariable("USERPROFILE");
    if (home.isEmpty())
        home = qEnvironmentVariable("HOMEDRIVE") + qEnvironmentVariable("HOMEPATH");
    return pypath::join(home, {u"Documents"_s, u"Paradox Interactive"_s, u"Victoria II"_s});
}

// --------------------------------------------------------------------------------- the game's own checksum (C++ only)
// The same number the game works out — checked against the checksums the game itself wrote for the base game and
// four mods (tests/map/gamesum_check.cpp).
namespace {

struct Sums {                                        // the game's per-file value, fed piece by piece
    quint32 a = 0, b = 0;
    void add(const char *data, qsizetype n)
    {
        const auto *p = reinterpret_cast<const unsigned char *>(data);
        for (qsizetype i = 0; i < n; ++i) {
            a += p[i];
            if (a > 0xFFFF)
                a -= 0xFFFF;
            b += a;
            if (b > 0xFFFF)
                b -= 0xFFFF;
        }
    }
    quint32 value() const { return (a << 16) | b; }
};

bool ascii(const QString &s)
{
    return std::all_of(s.begin(), s.end(), [](QChar c) { return c.unicode() < 128; });
}

// the game's file system (PhysicsFS): every mod in front of the game, the first folder holding a file wins
struct Vfs {
    QStringList roots;                               // in front first: the last ticked mod … the first, the game
    qsizetype mods = 0;                              // how many of the roots are mods
    std::vector<std::pair<qsizetype, QString>> replace;   // (root of the mod that says it, replace_path)
    std::map<QString, quint32> values;
    std::set<QString> twice;                         // counted files that more than one ticked mod holds
    long long files = 0;

    static bool safe(const QString &rel)             // PhysicsFS refuses ".", "..", ":" and "\" in a path
    {
        if (rel.contains(u':') || rel.contains(u'\\'))
            return false;
        const QStringList parts = rel.split(u'/');
        return std::none_of(parts.begin(), parts.end(), [](const QString &x) { return x == u"." || x == u".."; });
    }
    std::optional<std::pair<qsizetype, QString>> find(const QString &rel, bool counted = false)
    {
        if (rel.isEmpty() || !safe(rel))
            return std::nullopt;
        for (qsizetype i = 0; i < roots.size(); ++i) {
            const QString p = roots[i] + u'/' + rel;
            if (!QFileInfo(p).isFile())
                continue;
            if (counted && i < mods)
                for (qsizetype j = i + 1; j < mods; ++j)
                    if (QFileInfo(roots[j] + u'/' + rel).isFile())
                        twice.insert(rel);
            return std::make_pair(i, p);
        }
        return std::nullopt;
    }
    quint32 value(const QString &path)
    {
        const auto it = values.find(path);
        if (it != values.end())
            return it->second;
        const quint32 v = file_value(path);
        values.emplace(path, v);
        return v;
    }
    std::set<QString> names(const QString &dir, QDir::Filters kind) const
    {
        std::set<QString> out;
        for (const QString &r : roots)
            for (const QString &n : QDir(r + u'/' + dir).entryList(kind | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
                out.insert(n);
        return out;
    }
    quint32 folder(const QString &dir, const QString &ext)      // addFolder(dir, ext, recursive = 1)
    {
        std::optional<qsizetype> limit;
        for (const auto &[i, r] : replace)
            if (r == dir && (!limit || i < *limit))
                limit = i;
        quint32 sum = 0;
        for (const QString &n : names(dir, QDir::Files)) {
            if (!n.endsWith(ext) || !ascii(n))      // ("Invalid filename found")
                continue;
            const auto f = find(dir + u'/' + n, true);
            if (!f || (limit && f->first > *limit))
                continue;
            sum += value(f->second);
            ++files;
        }
        for (const QString &n : names(dir, QDir::Dirs))
            if (ascii(n) && n != u".svn")
                sum += folder(dir + u'/' + n, ext);
        return sum;
    }
    quint32 file(const QString &rel)                 // addFile: 0 when the file system has no such file
    {
        const auto f = find(rel, true);
        if (!f)
            return 0;
        ++files;
        return value(f->second);
    }
};

QByteArray read_all(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}  // namespace

quint32 bytes_value(const QByteArray &data)
{
    Sums s;
    s.add(data.constData(), data.size());
    return s.value();
}

quint32 file_value(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    Sums s;
    QByteArray buf(0x40000, Qt::Uninitialized);
    for (;;) {
        const qint64 n = f.read(buf.data(), buf.size());
        if (n <= 0)
            break;
        s.add(buf.constData(), n);
    }
    return s.value();
}

QString letters(qint32 sum)
{
    QString out;
    for (int i = 0; i < 4; ++i) {
        const qint32 x = sum >> (4 * i);             // (keeps the sign, as the game's sar)
        out += QChar(u'A' + std::abs(x % 26));       // (C's % truncates toward 0, as the game's idiv)
    }
    return out;
}

ModIn mod_in(const QString &game, const QString &modfile)
{
    ModIn m;
    m.modfile = modfile;
    script::Nodes nodes;
    try {
        nodes = script::parse(script::read_text(modfile));
    } catch (const fs::OSError &) {
        return m;
    }
    for (const script::Node &n : nodes) {
        if (!n.has_key || n.is_block)
            continue;
        if (n.key == u"path" && m.dir.isEmpty())
            m.dir = QDir::cleanPath(pypath::join(game, n.value));
        else if (n.key == u"user_dir" && m.user_dir.isEmpty())
            m.user_dir = n.value;
        else if (n.key == u"replace_path")
            m.replace << QString(n.value).replace(u'\\', u'/');
        else if (n.key == u"replace" || n.key == u"extend")
            m.odd_keys << n.key + u" = \""_s + n.value + u'"';
    }
    return m;
}

Prediction predict(const QString &game, const std::vector<ModIn> &mods, const QString &docs_)
{
    Prediction p;
    for (const ModIn &m : mods)
        if (!m.odd_keys.isEmpty()) {
            p.why = pypath::basename(m.modfile) + u" uses “"_s + m.odd_keys.join(u"”, “"_s) +
                    u"” — lines no mod uses; how the game counts those was read from its code but never tried, so "
                    u"the editor does not promise letters for it."_s;
            return p;
        }
    Vfs v;
    for (auto it = mods.rbegin(); it != mods.rend(); ++it)
        if (!it->dir.isEmpty())
            v.roots << it->dir;
    v.mods = v.roots.size();
    v.roots << QDir::cleanPath(game);
    for (const ModIn &m : mods)
        if (!m.dir.isEmpty())
            for (const QString &r : m.replace)
                v.replace.emplace_back(v.roots.indexOf(m.dir), r);
    // the game's order (0x6377A0)
    quint32 sum = v.folder(u"common"_s, u".txt"_s) + v.folder(u"common/countries"_s, u".txt"_s) +
                  v.folder(u"common"_s, u".lua"_s) + v.folder(u"news"_s, u".txt"_s);
    for (const QString &d : {u"poptypes"_s, u"events"_s, u"missions"_s, u"units"_s, u"inventions"_s})
        sum += v.folder(d, u".txt"_s);
    sum += v.folder(u"script"_s, u".lua"_s);
    for (const QString &d : {u"technologies"_s, u"decisions"_s})
        sum += v.folder(d, u".txt"_s);
    for (const QString &d : {u"countries"_s, u"diplomacy"_s, u"provinces"_s, u"wars"_s, u"leaders"_s, u"units"_s})
        sum += v.folder(u"history/"_s + d, u".txt"_s);
    const auto dm = v.find(u"map/default.map"_s);
    sum += v.file(u"map/default.map"_s);
    // (map/cache/time comes here in the game: below)
    sum += v.folder(u"battleplans"_s, u".txt"_s) + v.folder(u"battleplans"_s, u".pln"_s);
    const auto exe = v.find(u"v2game.exe"_s);
    sum += v.file(u"v2game.exe"_s);
    p.files = v.files;

    // the map cache's key: default.map + the six files it names (0x4A0FF0)
    quint32 key = 0;
    if (dm) {
        key += v.value(dm->second);
        script::Nodes nodes;
        try {
            nodes = script::parse(script::read_text(dm->second));
        } catch (const fs::OSError &) {
        }
        for (const QString &k : {u"definitions"_s, u"provinces"_s, u"positions"_s, u"terrain"_s,
                                 u"terrain_definition"_s, u"adjacencies"_s}) {
            const script::Node *last = nullptr;
            for (const script::Node &n : nodes)
                if (n.has_key && n.key == k && !n.is_block)
                    last = &n;
            if (last)
                if (const auto f = v.find(u"map/"_s + last->value))
                    key += v.value(f->second);
        }
    }
    p.map_key = key;
    QByteArray key_bytes(4, '\0');
    for (int i = 0; i < 4; ++i)
        key_bytes[i] = char((key >> (8 * i)) & 0xFF);

    // which settings folder: the ticked mods' user_dir values joined with "_", parent first
    const QString docs = !docs_.isEmpty() ? docs_ : documents_dir();
    QStringList uds;
    for (const ModIn &m : mods)
        if (!m.user_dir.isEmpty())
            uds << m.user_dir;
    p.settings = uds.isEmpty() ? docs : pypath::join(docs, uds.join(u'_'));
    const QString mine = pypath::join(p.settings, {u"map"_s, u"cache"_s, u"time"_s});
    const auto own = v.find(u"map/cache/time"_s);   // a new settings folder: the game folder's (or a mod's) own
    const quint32 fresh_time = own ? bytes_value(read_all(own->second)) : 0;
    const bool has_mine = QFileInfo(mine).isFile();
    const quint32 next_time = has_mine ? bytes_value(read_all(mine)) : fresh_time;
    p.steady = qint32(sum + bytes_value(key_bytes));
    p.fresh = qint32(sum + fresh_time);
    p.next = qint32(sum + next_time);
    if (p.next != p.steady)
        p.next_why = has_mine ? u"the map cache in that settings folder is of another map (an older version?) — the "
                                u"game makes it anew on that start"_s
                              : u"that settings folder has no map cache yet — the game makes it on that start"_s;
    if (!dm)
        p.notes << u"No map/default.map found."_s;
    if (!exe)
        p.notes << u"No v2game.exe in the game folder."_s;
    else
        p.notes << u"Worked out with this game's own v2game.exe: a player with another one (another version, or a "
                   u"patched exe) gets other letters."_s;
    if (!v.twice.empty()) {
        const QStringList t(v.twice.begin(), v.twice.end());
        p.notes << u"%1 counted file%2 in more than one ticked mod (%3) — taken from the mod ticked later (a submod "
                   u"before its parent): assumed, not measured."_s.arg(QString::number(t.size()), t.size() == 1 ? u" is"_s : u"s are"_s,
                                                                      t.mid(0, 4).join(u", "_s) + (t.size() > 4 ? u" …"_s : QString()));
    }
    p.ok = true;
    return p;
}

// ---------------------------------------------------------------------------------------------- measured in the game
namespace {

struct Found {
    DWORD pid;
    std::vector<HWND> out;
};

BOOL CALLBACK each_window(HWND h, LPARAM l)
{
    auto *f = reinterpret_cast<Found *>(l);
    DWORD p = 0;
    GetWindowThreadProcessId(h, &p);
    if (p == f->pid && IsWindowVisible(h))
        f->out.push_back(h);
    return TRUE;
}

std::vector<HWND> windows_of(qint64 pid)
{
    Found f{DWORD(pid), {}};
    EnumWindows(each_window, reinterpret_cast<LPARAM>(&f));
    return f.out;
}

void minimise(HWND h, const std::optional<std::pair<int, int>> &place)
{
    if (place)                                     // (tests: first onto a side screen)
        SetWindowPos(h, nullptr, place->first, place->second, 0, 0, 0x0001 | 0x0004 | 0x0010);
    ShowWindow(h, 7);                              // SW_SHOWMINNOACTIVE
}

void to_bin(const QString &path)                   // into the Recycle Bin (the probe's own leftovers)
{
    if (QFileInfo::exists(path))
        QFile::moveToTrash(path);
}

}  // namespace

std::optional<std::pair<int, int>> Probe::place;
std::function<void(const QString &)> Probe::dispose = to_bin;

double monotonic()
{
    // (Windows: the performance counter since the computer started, as Python's time.monotonic)
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Probe::Probe(const QString &game_, const QStringList &modfiles_, const QStringList &user_dirs_, const QString &docs_,
             const QString &exe_)
    : game(game_), docs(!docs_.isEmpty() ? docs_ : documents_dir()), exe(exe_), modfiles(modfiles_)
{
    for (const QString &u : user_dirs_)
        if (!u.isEmpty())
            user_dirs << u;
    const QString mods = pypath::join(game, u"mod"_s);
    probe_dir = pypath::join(mods, PROBE);
    probe_mod = pypath::join(mods, PROBE + u".mod"_s);
    folders << pypath::join(docs, (user_dirs + QStringList{PROBE}).join(u'_'))
            << pypath::join(docs, (QStringList{PROBE} + user_dirs).join(u'_'));
}

Probe::~Probe()
{
    if (proc && proc->state() != QProcess::NotRunning) {   // (a probe left running: ended, as finish() does)
        proc->kill();
        proc->waitForFinished(10000);
    }
}

void Probe::start()
{
    fs::makedirs(probe_dir);
    submods::write_modfile(probe_mod, PROBE, PROBE, {}, PROBE);
    for (const QString &f : folders) {
        dispose(f);                                // a fresh folder: an old map cache once changed the letters
        fs::makedirs(f);
        QFile fh(pypath::join(f, u"settings.txt"_s));
        if (!fh.open(QIODevice::WriteOnly))
            throw fs::OSError("[Errno 13] Permission denied: '" + fh.fileName().toStdString() + "'", 13);
        fh.write(SETTINGS.toLatin1());
    }
    // one plain command line: the game wants the parts WITHOUT quotes (with quotes no mod is loaded)
    QStringList parts;
    for (const QString &m : modfiles + QStringList{PROBE + u".mod"_s})
        parts << u"-mod=mod/"_s + m;
    proc = std::make_unique<QProcess>();
    proc->setProgram(pypath::join(game, exe));
    proc->setNativeArguments(parts.join(u' '));
    proc->setWorkingDirectory(game);
    proc->setProcessChannelMode(QProcess::ForwardedChannels);
    proc->start();
    if (!proc->waitForStarted(30000))
        throw fs::OSError("[WinError 2] The system cannot find the file specified", 2);
    t0 = monotonic();
}

std::optional<Result> Probe::poll()
{
    if (result)
        return result;
    if (!minimised) {
        const std::vector<HWND> hs = windows_of(proc->processId());
        if (!hs.empty()) {
            for (HWND h : hs)
                minimise(h, place);
            minimised = true;
        }
    }
    static const QRegularExpression SUM_RE(u"Checksum is [^(\\r\\n]*\\((\\w+)\\)"_s,
                                           QRegularExpression::UseUnicodePropertiesOption);
    for (const QString &f : folders) {
        QFile log(pypath::join(f, {u"logs"_s, u"setup.log"_s}));
        if (!log.open(QIODevice::ReadOnly))
            continue;
        const QRegularExpressionMatch m = SUM_RE.match(QString::fromLatin1(log.readAll()));
        if (m.hasMatch())
            return finish({u"ok"_s, m.captured(1)});
    }
    if (proc->state() == QProcess::NotRunning)
        return finish({u"error"_s, u"the game ended before it said its checksum (it may not start on this computer: "
                                   u"start it once from the launcher)"_s});
    if (monotonic() - t0 > TIMEOUT)
        return finish({u"error"_s, u"the game did not say its checksum within %1 s"_s.arg(TIMEOUT)});
    return std::nullopt;
}

Result Probe::finish(const Result &r)
{
    if (proc && proc->state() != QProcess::NotRunning) {
        proc->kill();                              // (only the game this probe started, by its own handle)
        proc->waitForFinished(10000);
    }
    QThread::msleep(500);
    for (const QString &p : QStringList{probe_mod, probe_dir} + folders) {
        try {
            dispose(p);
        } catch (...) {
        }
    }
    result = r;
    return r;
}

QString stamp() { return QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH:mm"_s); }

}  // namespace checksum
