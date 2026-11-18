// modpanel.py — Event modifiers (common/event_modifiers.txt): what each one does, which events / decisions add, remove
// or check it — and an editor for its effects, icon and name. ModifierForm is that editor (the Modifiers list uses it,
// and the Events tab shows it next to the text when the cursor is on a modifier); ModifierView is the list with it.
#pragma once
#include "core/moddata.h"
#include "core/modifiers.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QTreeWidgetItem>
#include <QWidget>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
namespace document { class Document; }
namespace events { struct Entry; class EventDB; }

namespace modpanel {

QString attr_name(const moddata::Mod &mod, const QString &key);   // an effect's name as the game writes it, else readable
QStringList effects_text(const moddata::Mod &mod, const std::vector<std::pair<QString, QString>> &attrs);

using ModFn = std::function<void(modifiers::ModifierEdits &)>;
using Used = modifiers::ModifierDB::Uses;    // {modifier: [(entry, how)]}

class SortItem : public QTreeWidgetItem {    // a row sorted by the values kept in its UserRole + 1 (numbers as numbers)
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &other) const override;
};

class ModifierForm : public QWidget {
    // One modifier: its name and description in the game, icon, effects (values edited in place, ✕ takes one off, a
    // box adds one), who uses it, delete. Every change is one undoable edit of the file's text.
    Q_OBJECT
public:
    explicit ModifierForm(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    modifiers::ModifierEdits *edits = nullptr;
    events::EventDB *db = nullptr;
    const Used *used = nullptr;
    std::optional<QString> cur;
    QLabel *title, *meta, *eff_title, *add_warn, *used_lbl;
    QLineEdit *name_edit, *desc_edit, *add_val;
    QComboBox *icon_box, *add_key;
    QWidget *edit_host, *rows_host, *add_row;
    QGridLayout *rows;
    QPushButton *del_btn;
    void refresh(document::Document *doc, events::EventDB *db, const Used *used);
    QString nm(const QString &name) const;
    void show_mod(const std::optional<QString> &name);
    void fill();
Q_SIGNALS:
    void modEdit(const QString &label, const modpanel::ModFn &fn);   // undo label, fn(ModifierEdits)
    void nameEdit(const QString &key, const QString &text);           // localisation key, text
    void entryPicked(events::Entry *e);                               // an event / decision to go to
    void deleted();

private:
    std::optional<std::pair<QString, QStringList>> _rows_sig;
    QHash<QString, QLineEdit *> _val_edits;
    bool _armed = false;
    std::vector<std::pair<const events::Entry *, QString>> _used_of(const QString &name) const;
    void _set_del();
    void _val_done(const QString &key, QLineEdit *ed);
    void _del_attr(const QString &key);
    void _add_done();
    void _icon_done(int);
    void _name_done(QLineEdit *ed, const std::optional<QString> &key);
    void _delete();
    void _used_link(const QString &href);
};

class ModifierView : public QWidget {
    // The "Modifiers" button of the Events tab: every modifier of common/event_modifiers.txt, and the editor.
    Q_OBJECT
public:
    explicit ModifierView(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    modifiers::ModifierEdits *edits = nullptr;
    events::EventDB *db = nullptr;
    Used used;
    std::optional<QString> cur;
    QLineEdit *search, *new_edit;
    QLabel *count, *new_warn;
    QWidget *new_row;
    QTreeWidget *lst;
    ModifierForm *form;
    void refresh(document::Document *doc, events::EventDB *db);
    QString nm(const QString &name) const;
    bool exists(const QString &name) const;
    void select(const QString &name);
    void _fill_list();
Q_SIGNALS:
    void modEdit(const QString &label, const modpanel::ModFn &fn);
    void nameEdit(const QString &key, const QString &text);
    void entryPicked(events::Entry *e);
    void modPicked(const QString &name);     // a modifier picked in the list (the editor shows its text)

private:
    const events::EventDB *_used_src = nullptr;
    void _picked(QTreeWidgetItem *cur, QTreeWidgetItem *prev);
    void _new_open();
    void _new_done();
};

}  // namespace modpanel
