// mapdefs.py — Edit the definitions in map/climate.txt and map/continent.txt: each one's modifiers, new ones, and
// removing the ones the mod added (vanilla ones are locked).
//
// Which provinces belong where is still set with the Climate / Continent boxes in the Provinces panel; this dialog
// only edits what a climate / continent *does*. Nothing is written until Apply in the main window, and the whole
// dialog is one undo step.
#pragma once
#include "core/mapfiles.h"
#include "core/ordered_map.h"

#include <QDialog>
#include <QRegularExpression>
#include <QWidget>
#include <functional>
#include <optional>
#include <tuple>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QVBoxLayout;

namespace moddata {
class Mod;
}

namespace mapdefs {

extern const QRegularExpression KEY_RE;
extern const QStringList REF_DIRS;
// VALUE_RE.match(v): ^[^\s{}=#"]+$ — Python's \s (str.isspace, \x1c-\x1f too), so a loop instead of a Qt pattern
bool value_ok(const QString &v);

QString pretty(const QString &name);
QStringList find_references(const moddata::Mod &mod, const QString &name, int limit = 50);

// Confirm removing a climate / continent; its provinces must go to another one.
class RemoveDialog : public QDialog {
    Q_OBJECT
public:
    RemoveDialog(const QString &kind, const QString &name, long long count, const QStringList &targets,
                 const QStringList &refs, QWidget *parent = nullptr);
    QComboBox *target = nullptr;
    std::optional<QString> chosen() const;
};

class ModifierRow : public QWidget {
    Q_OBJECT
public:
    ModifierRow(const QString &key, const QString &value, const QStringList &keys,
                const std::function<void(ModifierRow *)> &on_remove);
    QLineEdit *key, *value;
    std::pair<QString, QString> pair() const;
};

// Left: the climates / continents with their province counts. Right: the selected one's modifiers.
class MapDefsDialog : public QDialog {
    Q_OBJECT
public:
    explicit MapDefsDialog(mapfiles::MembershipFile &mf, const std::optional<QString> &start = std::nullopt,
                           QWidget *parent = nullptr);
    mapfiles::MembershipFile &mf;
    QString kind;
    OrderedMap<QString, mapfiles::Mods> work;
    QStringList names, new_names;
    QStringList removed;                             // in the order they were removed
    OrderedMap<QString, std::optional<QString>> moves;   // removed name -> where its provinces go
    std::optional<QString> current;
    std::vector<ModifierRow *> rows;
    QStringList keys;
    OrderedMap<QString, QString> renames;            // key -> new name in the game (continents: shown in the game)
    // (Python: QInputDialog.getText / RemoveDialog.exec — a check answers them without a window)
    std::function<std::pair<QString, bool>(QWidget *, const QString &, const QString &)> get_text;
    std::function<bool(RemoveDialog &)> run_remove;

    QListWidget *list;
    QPushButton *new_btn, *del_btn, *add_btn;
    QLabel *head, *info, *err;
    QWidget *name_row, *rows_box;
    QLineEdit *name_field;
    QVBoxLayout *rows_lay;

    // --- list
    long long _count(const QString &name) const;
    std::optional<QString> _final_target(const QString &r) const;
    void _fill_list();
    void _on_row(int row);
    QString _shown(const QString &n) const;
    void _name_edited();
    // --- modifier rows
    void _set_rows(const mapfiles::Mods &pairs);
    void _add_row(const QString &key, const QString &value, bool focus = false);
    void _remove_row(ModifierRow *row);
    mapfiles::Mods _rows() const;
    void _commit();
    // --- new / remove
    void _new();
    void _remove();
    // --- result
    void _ok();
    void _bad(const QString &name, const QString &msg);
    void show_error(const QString &text);
    // ({name: [(k, v)]} for changed modifiers, [new names], [removed names], {removed: target} — "" for None)
    std::tuple<OrderedMap<QString, mapfiles::Mods>, QStringList, QStringList, OrderedMap<QString, QString>>
    result_changes() const;
};

}  // namespace mapdefs
