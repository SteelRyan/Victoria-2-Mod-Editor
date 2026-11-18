// recovery.py — unapplied edits are never lost (the tester, 2026-10-17; the user: "it should never ever be able to
// lose an edit like that").
// The PICTURE of everything pending (the same pictures the undo steps hold: Document._snapshot; the bitmaps give their
// changed pixels and their values instead) is kept in backups/recovery/<mod + scenario>; the same picture, cut down to
// some parts, gives per-file Apply / Revert and keeps the pending edits over an undo of a whole Apply.
// Parts ("units"): "pops", "states", "history", "kind:<map kind>", "file:<rel>" (the script texts, one per file).
// (Deviation, PROGRESS.md: Python pickles the picture into <key>.pkl; the port writes its own form into <key>.medrec,
// so neither editor ever reads or overwrites the other's file.)
#pragma once
#include "core/document.h"

#include <QByteArray>
#include <QSet>
#include <QString>
#include <optional>
#include <utility>
#include <vector>

namespace recovery {

extern const int VERSION;
extern const QString EXT;                       // ".medrec"

struct Pic {                                    // {"pops", "regs", "provs", "lists", "special"}
    document::Picture p;
    OrderedMap<QString, store::Snap> special;
};

QString path_for(const QString &backup_root, const QString &mod_root, const QString &scenario);
std::optional<Pic> picture(document::Document &doc);
std::vector<std::pair<QString, QString>> units(const std::optional<Pic> &pic);
extern const OrderedMap<QString, QString> KIND_WORDS;
std::optional<Pic> cut(const std::optional<Pic> &pic, const QSet<QString> &keep);
bool put_back(document::Document &doc, const std::optional<Pic> &pic,
              const QString &label = QStringLiteral("Unapplied edits put back"));

// ---- the file
struct Data {                                   // {"version", "mod", "scenario", "time", "count", "picture", "auto", "note"}
    long long version = 0;
    QString mod, scenario, time;
    long long count = 0;
    Pic picture;
    bool auto_ = false;
    QString note;
};
// Write the recovery file (or remove it when nothing is pending) -> what was written (None: nothing). Raises when it
// can't be written. (Every picture type must be registered: snapio::register_core / register_map.)
std::optional<Data> save(const QString &path, document::Document &doc, bool auto_ = false, const QString &note = {});
std::optional<Data> load(const QString &path);  // None: no file, not this editor's, another version, damaged
QByteArray pack(const Data &data);               // (the file's bytes; raises for a picture type not registered)
std::optional<Data> unpack(const QByteArray &bytes);   // (None: not this editor's / another version; raises if damaged)
void drop(const QString &path);
void keep_aside(const QString &path);

}  // namespace recovery
