// applyjournal.py — every Apply, kept: the files it changed as they were before and as it wrote them, so an Apply can
// be undone and redone as a whole — nothing an Apply overwrites can be lost (the tester's report, 2026-10-14).
// While an Apply runs every way the port writes, replaces or removes a file passes fs::watched() (Python: open() for
// writing, os.replace / rename / remove, QImage.save hooked): the first time a file is touched it is copied into the
// Apply's folder ("before/"); when the Apply is done what it wrote is copied too ("after/"). A scan of the mod's files
// before and after finds anything changed some other way ("untracked" — reported, never silent).
// backups/applies/<stamp>/manifest.json, before/<rel>, after/<rel>, replaced-<n>/<rel>.
#pragma once
#include "core/pyjson.h"

#include <QString>
#include <QStringList>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace applyjournal {

QString _norm(const QString &p);
struct Scan {
    std::map<QString, std::pair<long long, long long>> files;   // rel -> (size, mtime_ns)
    std::set<QString> dirs;
};
Scan _scan(const QString &root);
bool _same(const QString &a, const QString &b);

struct WriteRefused : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class ApplyJournal;
class Recorder {
public:
    Recorder(ApplyJournal &journal, const QString &folder, const QString &label, const QString &scenario);
    ApplyJournal &j;
    QString folder, root, nroot, label, scenario;
    pyjson::Object files;                        // rel -> {"before": existed, "after": …}
    Scan pre;
    QStringList untracked;
    std::optional<pyjson::Value> manifest;
    QString touch(const QString &path, bool remove = false);
    void install();
    void uninstall();
    std::optional<pyjson::Value> finish(const std::optional<QString> &error);

private:
    bool _in = false;
};

class ApplyJournal {
public:
    ApplyJournal(const QString &backup_root, const QString &mod_root, const std::optional<QString> &base_root = std::nullopt);
    QString dir, mod_root;
    std::optional<QString> base_root;
    std::optional<QString> guard(const QString &path, const QString &n = {}, bool remove = false) const;
    void _write(const QString &folder, const pyjson::Value &man) const;
    using Entry = std::pair<QString, pyjson::Value>;   // (folder, manifest)
    std::vector<Entry> entries() const;
    // with journal.record(label, scenario): fn() runs watched; the Recorder is finished whatever happens
    // (manifest: rec.manifest afterwards — also when fn throws)
    void record(const QString &label, const QString &scenario, const std::function<void(Recorder &)> &fn,
                std::optional<pyjson::Value> *manifest = nullptr);
    std::optional<Entry> undoable() const;
    std::optional<Entry> redoable() const;
    std::pair<long long, QStringList> _put(const QString &folder, const pyjson::Value &man, const QString &side) const;
    std::pair<long long, QStringList> undo(Entry &entry);
    std::pair<long long, QStringList> redo(Entry &entry);
};

QString describe(const pyjson::Value &man, long long n = 6);

}  // namespace applyjournal
