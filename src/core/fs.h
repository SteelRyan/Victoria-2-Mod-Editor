// Python's file operations, as the editor uses them (PLAN.md §4.4) — same results, not Qt's.
#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fs {

// OSError: the port throws this where Python would raise (caught where the Python catches)
struct OSError : std::runtime_error {
    int err = 0;                                       // Python's errno (2 = not found, 13 = permission, …)
    explicit OSError(const std::string &what, int errno_ = 0) : std::runtime_error(what), err(errno_) {}
    QString text() const { return QString::fromStdString(what()); }   // str(e)
};

// The Apply journal's watch (applyjournal): every write, replace or removal of a file of the port passes watched()
// first (Python: builtins.open / os.replace / os.remove hooked while an Apply runs). It may answer another path (a base
// game file: the mod's copy) or refuse (throws). No watch set: the path as it is.
using Watch = std::function<QString(const QString &path, bool remove)>;
void set_watch(Watch w);
QString watched(const QString &path, bool remove = false);
// C++ only: a guard every write / replacement / removal (and copy2's target) passes before the watch — modcopy's "keep
// a copy of the original mod" (it may throw OSError: the write does not happen)
using Guard = std::function<void(const QString &path)>;
void set_guard(Guard g);

// shutil.copy2(src, dst): content + modification time + read-only flag; overwrites dst; dst may be a folder
QString copy2(const QString &src, const QString &dst);
// os.replace(src, dst): rename, replacing dst if it exists (also across folders on one drive)
void replace(const QString &src, const QString &dst);
// os.makedirs(path, exist_ok=True) — exist_ok=false as Python's default (FileExistsError: err 183)
void makedirs(const QString &path, bool exist_ok = true);
// os.listdir(path): the names as Windows gives them (FindFirstFile order), no "." / ".."
QStringList listdir(const QString &path);
// os.remove(path)
void remove(const QString &path);
// os.rmdir(path): an empty folder only
void rmdir(const QString &path);
// os.scandir(path): one entry per name (FindFirstFile order, no "." / ".."); is_dir / is_file as
// DirEntry.is_dir(follow_symlinks=False) / is_file(follow_symlinks=False); size and mtime as DirEntry.stat()
struct DirEntry {
    QString name, path;    // path = os.path.join(the folder, name)
    bool is_dir = false, is_file = false;
    long long size = 0, mtime_ns = 0;
};
std::vector<DirEntry> scandir(const QString &path);
// a whole file's bytes through Windows itself (no Qt file engine: thousands of small files read faster); nullopt when it
// can't be opened or read
std::optional<QByteArray> read_bytes(const QString &path);
bool isfile(const QString &path);
bool isdir(const QString &path);
bool exists(const QString &path);
// os.path.getmtime(path) as a float (seconds since 1970)
double getmtime(const QString &path);
// (st_mtime_ns, st_size) of os.stat — as a signature that changes when the file does; nullopt: OSError
std::optional<std::pair<long long, long long>> stat_sig(const QString &path);

}  // namespace fs
