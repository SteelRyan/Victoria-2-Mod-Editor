// scriptstore.py — doc.maps["scripts"]: the mod's script files (events, decisions, common/event_modifiers.txt …) as
// the editor holds them. Every change to these files — typing in the editor, a category, a modifier's effect — goes
// into the same text, so nothing can overwrite anything else; Apply writes the changed files (with a backup).
// (The texts are QTextDocuments — QtGui; the plain-text layout the script editor needs is QtWidgets, so the window
// part installs it through `prepare_doc`.)
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/store.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextDocument>
#include <functional>
#include <memory>
#include <optional>

namespace scriptstore {

QString _norm(const QString &t);
QStringList canon(const QString &text);        // (Python: hash(tuple(tokens)) — here the tokens themselves)

class ScriptStore : public store::Store {
public:
    static constexpr const char *kind = "scripts";
    static const QString GONE;                 // "\0gone"
    explicit ScriptStore(moddata::Mod &mod) : mod(mod) {}
    ~ScriptStore() override;
    moddata::Mod &mod;
    OrderedMap<QString, QTextDocument *> docs; // rel -> its text (only files that were opened / changed)
    OrderedMap<QString, QString> orig;          // rel -> the text on disk ("\n" newlines)
    OrderedMap<QString, QString> nl;            // rel -> the file's own newline
    std::vector<std::function<void(const QString &)>> listeners;   // fn(rel) after every change of a file's text
    std::optional<std::pair<std::function<void()>, std::function<void()>>> undo_hook;
    QSet<QString> gone;                         // rels taken out of the mod (Apply deletes the file, after a backup)
    QStringList drop_names;                     // localisation keys the next change takes with it
    static std::function<void(QTextDocument *)> prepare_doc;   // (the window part: the plain-text layout)

    QString path(const QString &rel) const;
    void _load(const QString &rel);
    QString text(const QString &rel);
    void _change(const QString &rel, int pos, int removed, int added);
    bool is_open(const QString &rel) const { return docs.contains(rel); }
    QTextDocument *document(const QString &rel);
    void _changed(const QString &rel);
    void set_text(const QString &rel, const QString &text);
    void remove(const QString &rel);
    bool _on_disk(const QString &rel) const;
    bool same(const QString &rel);

    store::Snap snapshot() const override;
    void restore(const store::Snap &snap) override;
    QStringList dirty() const override;
    bool partial_undo() const override { return true; }
    bool has_part_keys() const override { return true; }
    QSet<store::Key> part_keys(const store::Items &a, const store::Items &b) const override;
    bool has_rebase() const override { return true; }
    store::Snap rebase_begin() override;
    store::Snap rebase(const store::Snap &snap, const store::Snap &old) override;
    bool has_live_sig() const override { return true; }
    QString live_sig() const override;
    std::optional<QStringList> save(const QString &backup_root) override;

private:
    OrderedMap<QString, std::pair<long long, QString>> _cache;   // rel -> (revision, text)
    OrderedMap<QString, std::pair<QString, QStringList>> _canon; // rel -> (the text it was made of, its canon)
    OrderedMap<QString, QStringList> _canon_orig;                // rel -> canon of the text on disk
    mutable OrderedMap<QString, std::tuple<long long, bool, QString>> _diff;   // rel -> (rev, differs, disk text)
    OrderedMap<QString, long long> _rev;        // rel -> how often its TEXT changed (not its colours)
    QString text_c(const QString &rel) const;   // (text() for const callers: no new document is made)
};

}  // namespace scriptstore
