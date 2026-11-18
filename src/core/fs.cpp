// Python's file operations on Windows, through the same Win32 calls CPython makes.
#include "core/fs.h"
#include "core/pypath.h"
#include "core/pytext.h"

#include <QDir>
#include <QFileInfo>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using namespace Qt::StringLiterals;

namespace fs {

static std::wstring w(const QString &p) { return QDir::toNativeSeparators(p).toStdWString(); }

// str() of the OSError CPython raises for a failed Win32 call: "[WinError 5] Access is denied: 'C:\\x'" (the system's
// text without its trailing dots / line ends, the file name as repr() shows it; a second name: " -> 'y'")
static std::string winerror_text(DWORD err, const QString &p, const QString *p2 = nullptr)
{
    wchar_t *buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                       FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                   reinterpret_cast<wchar_t *>(&buf), 0, nullptr);
    QString msg;
    if (n && buf) {
        msg = QString::fromWCharArray(buf, int(n));
        while (!msg.isEmpty() && (msg.back() <= u' ' || msg.back() == u'.'))
            msg.chop(1);
    } else
        msg = u"Windows Error 0x%1"_s.arg(qulonglong(err), 0, 16);
    if (buf)
        LocalFree(buf);
    QString s = u"[WinError %1] %2: %3"_s.arg(qulonglong(err)).arg(msg, py::repr_str(p));
    if (p2)
        s += u" -> "_s + py::repr_str(*p2);
    return s.toStdString();
}

[[noreturn]] static void fail(const char *, const QString &p, const QString *p2 = nullptr)
{
    const DWORD err = GetLastError();
    throw OSError(winerror_text(err, p, p2), int(err));
}

static Watch &the_watch()
{
    static Watch w;
    return w;
}

void set_watch(Watch w) { the_watch() = std::move(w); }

static Guard &the_guard()
{
    static Guard g;
    return g;
}

void set_guard(Guard g) { the_guard() = std::move(g); }

QString watched(const QString &path, bool remove)
{
    if (the_guard())
        the_guard()(path);                        // (C++ only: modcopy keeps the original before the first write)
    return the_watch() ? the_watch()(path, remove) : path;
}

QString copy2(const QString &src, const QString &dst0)
{
    QString dst = dst0;
    if (isdir(dst))                                   // copy2 into a folder: same name inside it
        dst = QDir(dst).filePath(QFileInfo(src).fileName());
    if (the_guard())                                  // (the guard does see it: a copy into the mod is a write)
        the_guard()(dst);
    // (NOT watched: CPython 3.14's copy2 on Windows copies with the system's CopyFile2, past the builtins.open hook
    // applyjournal installs — measured 2026-10-20: a copy2 into the mod is not recorded. Kept as the oracle has it.)
    // CPython: shutil.copyfile (the bytes) then copystat (times, the read-only bit). CopyFileW does both in one
    // call and, like copyfile, replaces an existing file unless it is read-only.
    if (!CopyFileW(w(src).c_str(), w(dst).c_str(), FALSE))
        fail("copy2", src);
    // copystat also sets the access time to the source's; CopyFileW keeps the write time and attributes
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (GetFileAttributesExW(w(src).c_str(), GetFileExInfoStandard, &a)) {
        HANDLE h = CreateFileW(w(dst).c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            SetFileTime(h, nullptr, &a.ftLastAccessTime, &a.ftLastWriteTime);
            CloseHandle(h);
        }
    }
    return dst;
}

void replace(const QString &src, const QString &dst)
{
    // os.replace on Windows = MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)
    const QString s = watched(src), d = watched(dst);
    if (!MoveFileExW(w(s).c_str(), w(d).c_str(), MOVEFILE_REPLACE_EXISTING))
        fail("replace", src, &dst);
}

void makedirs(const QString &path, bool exist_ok)
{
    // os.makedirs: the missing parents first (one that appears meanwhile is fine), then os.mkdir(path) — its error
    // names the first folder that could not be made, as Python's does
    QString head = pypath::dirname(path), tail = pypath::basename(path);
    if (tail.isEmpty()) {
        tail = pypath::basename(head);
        head = pypath::dirname(head);
    }
    if (!head.isEmpty() && !tail.isEmpty() && !exists(head)) {
        try {
            makedirs(head, exist_ok);
        } catch (const OSError &e) {
            if (e.err != ERROR_ALREADY_EXISTS)          // (FileExistsError: pass)
                throw;
        }
    }
    if (tail == u".")
        return;
    if (!CreateDirectoryW(w(path).c_str(), nullptr)) {
        const DWORD err = GetLastError();
        if (!exist_ok || !isdir(path))
            throw OSError(winerror_text(err, path), int(err));
    }
}

QStringList listdir(const QString &path)
{
    QStringList out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((w(path) + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        fail("listdir", path);
    do {
        const QString n = QString::fromWCharArray(fd.cFileName);
        if (n != u"." && n != u"..")
            out << n;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

void remove(const QString &path0)
{
    const QString path = watched(path0, true);
    if (!DeleteFileW(w(path).c_str()))
        fail("remove", path);
}

void rmdir(const QString &path)
{
    if (!RemoveDirectoryW(w(path).c_str()))
        fail("rmdir", path);
}

std::vector<DirEntry> scandir(const QString &path)
{
    // CPython's os.scandir on Windows: FindFirstFileW(path\*), the find data cached in each entry
    std::vector<DirEntry> out;
    WIN32_FIND_DATAW fd;
    QString base = path;
    HANDLE h = FindFirstFileW((w(path) + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        fail("scandir", path);
    const bool sep = !base.isEmpty() && (base.endsWith(u'\\') || base.endsWith(u'/') || base.endsWith(u':'));
    do {
        const QString name = QString::fromWCharArray(fd.cFileName);
        if (name == u"." || name == u"..")
            continue;
        DirEntry e;
        e.name = name;
        e.path = sep ? base + name : base + u'\\' + name;
        const bool link = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && fd.dwReserved0 == IO_REPARSE_TAG_SYMLINK;
        const bool dir = fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        e.is_dir = dir && !link;
        e.is_file = !dir && !link;
        ULARGE_INTEGER t, n;
        t.LowPart = fd.ftLastWriteTime.dwLowDateTime;
        t.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        n.LowPart = fd.nFileSizeLow;
        n.HighPart = fd.nFileSizeHigh;
        e.size = static_cast<long long>(n.QuadPart);
        e.mtime_ns = static_cast<long long>((t.QuadPart - 116444736000000000ULL) * 100);
        out.push_back(e);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

std::optional<QByteArray> read_bytes(const QString &path)
{
    HANDLE h = CreateFileW(w(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return std::nullopt;
    LARGE_INTEGER sz{};
    std::optional<QByteArray> out;
    if (GetFileSizeEx(h, &sz) && sz.QuadPart < (1LL << 31)) {
        QByteArray b(qsizetype(sz.QuadPart), Qt::Uninitialized);
        DWORD got = 0;
        if (b.isEmpty() || (ReadFile(h, b.data(), DWORD(b.size()), &got, nullptr) && got == DWORD(b.size())))
            out = std::move(b);
    }
    CloseHandle(h);
    return out;
}
bool isfile(const QString &path)
{
    const DWORD a = GetFileAttributesW(w(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool isdir(const QString &path)
{
    const DWORD a = GetFileAttributesW(w(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool exists(const QString &path) { return GetFileAttributesW(w(path).c_str()) != INVALID_FILE_ATTRIBUTES; }

double getmtime(const QString &path)
{
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(w(path).c_str(), GetFileExInfoStandard, &a))
        fail("getmtime", path);
    ULARGE_INTEGER t;
    t.LowPart = a.ftLastWriteTime.dwLowDateTime;
    t.HighPart = a.ftLastWriteTime.dwHighDateTime;
    // 100-ns ticks since 1601 -> seconds since 1970, as CPython computes it
    return double(t.QuadPart - 116444736000000000ULL) / 1e7;
}

std::optional<std::pair<long long, long long>> stat_sig(const QString &path)
{
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(w(path).c_str(), GetFileExInfoStandard, &a))
        return std::nullopt;
    ULARGE_INTEGER t, n;
    t.LowPart = a.ftLastWriteTime.dwLowDateTime;
    t.HighPart = a.ftLastWriteTime.dwHighDateTime;
    n.LowPart = a.nFileSizeLow;
    n.HighPart = a.nFileSizeHigh;
    return std::make_pair(static_cast<long long>((t.QuadPart - 116444736000000000ULL) * 100),
                          static_cast<long long>(n.QuadPart));
}

}  // namespace fs
