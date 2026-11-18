// dialogs.py — Dialogs: add pop, paste pops, change culture/religion/type, new culture / religion, culture and
// religion managers.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"

#include <QComboBox>
#include <QDialog>
#include <QFrame>
#include <QSet>
#include <QStringList>
#include <QTreeWidget>
#include <QWidget>
#include <functional>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

class QButtonGroup;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;

namespace document {
class Document;
}

namespace dialogs {

using OptStr = std::optional<QString>;
using Rgb = std::tuple<int, int, int>;

QLabel *_lab(const QString &text, const QString &obj = QStringLiteral("sub"));
// the problem of a dialog said where it opened from (Python: getattr(parent, "show_error", None) or
// getattr(parent, "message", None)) — the Q_INVOKABLE show_error / message of that window
void report(QWidget *parent, const QString &text);

struct Result {                                   // result_data(): a new culture / religion
    QString key, name;
    Rgb color;
    std::optional<long long> icon;                // (religions)
    QString group;
    OptStr new_group;
};

// ---------------------------------------------------------------------------------------------- new culture
class NewCultureDialog : public QDialog {
    // Asks for key, display name, colour and group (existing, or a new group).
    Q_OBJECT
public:
    NewCultureDialog(moddata::Mod &mod, const QStringList &groups, const QSet<QString> &taken,
                     const OptStr &default_group = std::nullopt, QWidget *parent = nullptr);
    QSet<QString> taken;
    Rgb color;
    QLineEdit *key_edit, *name_edit, *new_group;
    QPushButton *color_btn;
    QComboBox *group_box;
    QLabel *err;
    Q_INVOKABLE void show_error(const QString &text);
    void _suggest_name(const QString &t);
    void _show_color();
    void _pick_color();
    void _ok();
    Result result_data() const;
};

OptStr create_culture(QWidget *parent, moddata::Mod &mod, const QString &backup_root,
                      const OptStr &default_group = std::nullopt);
void reload_cultures(moddata::Mod &mod, const OrderedMap<QString, QString> &names = {});

// ---------------------------------------------------------------------------------------------- the managers
class GroupTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit GroupTree(const QString &item_title = QStringLiteral("Culture"), QWidget *parent = nullptr);
Q_SIGNALS:
    void moved(const QStringList &keys, const QString &gid);   // item keys (cultures / religions), target group

protected:
    void dropEvent(QDropEvent *ev) override;
};

struct ItemInfo {                                 // a culture / religion as the manager reads it
    QString group, name;
    Rgb color;
    std::optional<long long> icon;
};
struct NewItem {                                  // (key, gid, color, name, template or icon)
    QString key, gid;
    Rgb color;
    QString name;
    std::optional<long long> icon;
};

class GroupManager : public QDialog {
    // Folder view of groups (culture groups / religion groups): drag items between groups, add items and groups.
    // Subclasses say which file and data they edit.
    Q_OBJECT
public:
    GroupManager(moddata::Mod &mod, document::Document &doc, const QString &backup_root, QWidget *parent = nullptr,
                 const QString &kind = QStringLiteral("culture"), const QString &kinds = QStringLiteral("cultures"),
                 const QString &file = QStringLiteral("common/cultures.txt"));
    void build();                                 // the rest of Python's __init__: the subclass's constructor calls it
                                                  // (its overrides only work once it is made)
    const QString KIND, KINDS, FILE;
    moddata::Mod &mod;
    document::Document &doc;
    QString backup_root;
    bool changed_file = false;
    QStringList groups;
    OrderedMap<QString, QStringList> members;     // (Python: defaultdict(list))
    std::vector<std::pair<QString, OptStr>> new_groups;   // (gid, template)
    std::vector<NewItem> new_items;
    OrderedMap<QString, QString> moves;           // key -> gid
    OrderedMap<QString, QString> renames;         // key (item or group) -> new name in the game
    OrderedMap<QString, QString> orig_group;
    QLineEdit *search, *name_field;
    GroupTree *tree;
    QPushButton *rename_btn, *save_btn, *close_btn;
    QLabel *pending_label, *err;
    OptStr _armed;                                // "save" / "close": the button was clicked once already

    // --- what is edited (overridden by the religion manager)
    virtual OrderedMap<QString, ItemInfo> _items() const;
    virtual QStringList _group_order() const;
    virtual QDialog *_new_dialog();
    virtual NewItem _new_entry(const Result &d) const;
    virtual QString _apply();
    virtual void _reload();
    virtual void _decorate(QTreeWidgetItem *ci, const QString &key, const ItemInfo *item, const NewItem *nw) const;
    virtual Result _result_of(QDialog *dlg) const;

    QString _save_text() const;
    void _reset_pending();
    long long pending() const;
    QString _name_of(const QString &key) const;
    void _show_name();
    void rename();
    OrderedMap<OptStr, long long> _pop_sizes() const;
    void rebuild();
    void move(const QStringList &keys, const QString &gid);
    void move_to();
    QSet<QString> _taken() const;
    OptStr _selected_group() const;
    void new_group();
    void new_item();
    Q_INVOKABLE void show_error(const QString &text);
    void _arm(const QString &what, QPushButton *btn, const QString &text);
    void _disarm();
    void save();
    void close_dialog();
};

class CultureManager : public GroupManager {
    // Culture groups (common/cultures.txt).
    Q_OBJECT
public:
    CultureManager(moddata::Mod &mod, document::Document &doc, const QString &backup_root, QWidget *parent = nullptr);
};

class ReligionManager : public GroupManager {
    // Religion groups (common/religion.txt): same as cultures, with the religion icons.
    Q_OBJECT
public:
    ReligionManager(moddata::Mod &mod, document::Document &doc, const QString &backup_root, QWidget *parent = nullptr);
    OrderedMap<QString, ItemInfo> _items() const override;
    QStringList _group_order() const override;
    QDialog *_new_dialog() override;
    NewItem _new_entry(const Result &d) const override;
    QString _apply() override;
    void _reload() override;
    void _decorate(QTreeWidgetItem *ci, const QString &key, const ItemInfo *item, const NewItem *nw) const override;
    Result _result_of(QDialog *dlg) const override;
};

void reload_religions(moddata::Mod &mod, const OrderedMap<QString, QString> &names = {});

// ---------------------------------------------------------------------------------------------- new religion
class NewReligionDialog : public QDialog {
    // Key, display name, colour, icon (one of the game's religion icons) and group (existing or new).
    Q_OBJECT
public:
    NewReligionDialog(moddata::Mod &mod, const QStringList &groups, const QSet<QString> &taken,
                      const OptStr &default_group = std::nullopt, QWidget *parent = nullptr);
    moddata::Mod &mod;
    QSet<QString> taken;
    Rgb color;
    QLineEdit *key_edit, *name_edit, *new_group;
    QPushButton *color_btn;
    QComboBox *group_box;
    long long icon = 1;
    QButtonGroup *icon_group;
    QSpinBox *icon_spin = nullptr;
    QLabel *icon_note, *err;
    Q_INVOKABLE void show_error(const QString &text);
    void _set_icon(long long n);
    void _suggest_name(const QString &t);
    void _show_color();
    void _pick_color();
    void _ok();
    Result result_data() const;
};

OptStr create_religion(QWidget *parent, moddata::Mod &mod, const QString &backup_root,
                       const OptStr &default_group = std::nullopt);

// ---------------------------------------------------------------------------------------------- pickers
extern const QString HEADER, NEW;
// a combo entry's data: a key, None, HEADER or NEW (Python: the item data, any object)
using Data = std::optional<QString>;
struct PickItem {                                 // (key, text, rgb, group_label or None)
    QString key, text;
    std::optional<Rgb> rgb;
    OptStr group;
};
using Lead = std::vector<std::pair<Data, QString>>;   // [(data, text)]

class PickerPopup : public QFrame {
    // Large searchable picker used instead of the long combo drop-down list.
    // Search box (focused, type to filter by name / key / group), options grouped under their group heading, special
    // entries (keep / new) pinned on top. Click or Enter picks; Esc closes.
    Q_OBJECT
public:
    PickerPopup(const Lead &lead, const std::vector<PickItem> &items, const Data &current, QWidget *parent);
    QLineEdit *search;
    QTreeWidget *tree;
    bool eventFilter(QObject *obj, QEvent *ev) override;
    std::vector<QTreeWidgetItem *> _visible_items() const;
    void _filter(const QString &text);
    void _pick_first();
    void _clicked(QTreeWidgetItem *it, int col = 0);
Q_SIGNALS:
    void picked(const dialogs::Data &data);
};

class PickCombo : public QComboBox {
    // Drop-down picking from an existing pool, optionally with group headings.
    // items: [(key, text, rgb, group_label or None)]. `lead` = [(data, text)] entries shown first (e.g. "(keep
    // culture)" -> None, "＋ New culture…" -> NEW). Clicking opens a large searchable PickerPopup instead of the plain
    // list.
    Q_OBJECT
public:
    explicit PickCombo(const std::vector<PickItem> &items, const Lead &lead = {}, const Data &default_ = std::nullopt,
                       QWidget *parent = nullptr);
    std::vector<PickItem> _items;
    Lead _lead_items;
    int _last = 0;
    void showPopup() override;
    void hidePopup() override {}
    void _picked(const Data &data);
    void fill(const std::vector<PickItem> &items, const Lead &lead = {}, const Data &default_ = std::nullopt);
    Data data_at(int i) const;                    // itemData(i) as the Python object it stands for
    int find(const Data &d) const;                // findData
    Data current() const { return data_at(currentIndex()); }   // currentData()
    OptStr key() const;
};

std::vector<PickItem> grouped_cultures(const moddata::Mod &mod);
std::vector<PickItem> grouped_religions(const moddata::Mod &mod);
std::vector<PickItem> all_poptypes(const moddata::Mod &mod);   // vanilla pop types (always) + the mod's, two headings

class SizeField : public QWidget {
    // Number field without spinner arrows, with ±1k … ±100k step buttons underneath.
    Q_OBJECT
public:
    explicit SizeField(long long value = 10000, long long minimum = 1, const QString &suffix = QString(),
                       QWidget *parent = nullptr);
    static const std::vector<long long> STEPS;
    QSpinBox *spin;
    long long value() const;
};

class CultureCombo : public PickCombo {
    // Culture drop-down whose first entry creates a new culture (like '＋ New group…').
    Q_OBJECT
public:
    CultureCombo(moddata::Mod &mod, const QString &backup_root, const OptStr &keep_text = std::nullopt,
                 const Data &default_ = std::nullopt, QWidget *parent = nullptr);
    moddata::Mod &mod;
    QString backup_root;
    OptStr keep_text;
    Lead _lead() const;
    void _activated(int i);
};

class ReligionCombo : public PickCombo {
    // Religion drop-down whose first entry creates a new religion (like '＋ New culture…').
    Q_OBJECT
public:
    ReligionCombo(moddata::Mod &mod, const QString &backup_root, const OptStr &keep_text = std::nullopt,
                  const Data &default_ = std::nullopt, QWidget *parent = nullptr);
    moddata::Mod &mod;
    QString backup_root;
    OptStr keep_text;
    Lead _lead() const;
    void _activated(int i);
};

class AddPopDialog : public QDialog {
    Q_OBJECT
public:
    AddPopDialog(moddata::Mod &mod, document::Document &doc, long long n_provinces, const QString &backup_root,
                 QWidget *parent = nullptr);
    moddata::Mod &mod;
    QString backup_root;
    PickCombo *type_box;
    CultureCombo *cult;
    ReligionCombo *rel;
    SizeField *size;
    QLabel *err;
    Q_INVOKABLE void show_error(const QString &text);
    void _ok();
    std::tuple<OptStr, OptStr, OptStr, long long> values() const;
};

class PasteDialog : public QDialog {
    Q_OBJECT
public:
    PasteDialog(const QString &clip_desc, long long clip_total, long long n_provinces, QWidget *parent = nullptr);
    QRadioButton *add_rb, *rep_rb, *pct_rb, *tot_rb;
    QDoubleSpinBox *pct;
    SizeField *total;
    std::tuple<bool, double, std::optional<long long>> values() const;
};

}  // namespace dialogs
