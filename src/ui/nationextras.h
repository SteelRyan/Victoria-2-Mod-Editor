// nationextras.py — More of a nation, on its page: its parties (common/countries/<file>: name, ideology, the years it
// exists, its stance on every party issue), its colour on the map, its graphical culture, and its flags (the ones the
// game can ask for, small; the flag studio opens from here). The data half (FlagEdits) is map\nationextras_data.
#pragma once
#include "core/document.h"
#include "core/moddata.h"
#include "core/script.h"
#include "map/nationextras_data.h"
#include "ui/widgets.h"

#include <QHash>
#include <QLabel>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace colorwheel {
class ColorButton;
}
namespace scriptstore {
class ScriptStore;
}

namespace nationextras {

using OptStr = std::optional<QString>;

extern const std::vector<std::pair<QString, QString>> VARIANTS;   // (suffix, name): normal, monarchy … fascist

std::map<QString, QString> government_flags(document::Document *doc);   // {government: its flagType}
OptStr start_flag(const moddata::Mod &mod, document::Document *doc, const QString &tag,
                  const std::map<QString, QString> *gov_flags = nullptr);   // the flag it flies at the start (a path)
std::vector<std::pair<QString, QString>> flag_variants(document::Document *doc);
std::vector<const script::Node *> party_nodes(const script::Nodes &nodes);   // party_nodes(text): of parse(text)
QString _unq(const script::Node &c);                                         // _unq(c.value)
QHash<QString, QString> fields(const script::Node &n);                       // a block's plain key = value lines
QStringList ideologies(scriptstore::ScriptStore *store);           // defpanel.ideologies: in the file's order

class _Thumb : public QLabel {
    Q_OBJECT
public:
    explicit _Thumb(QWidget *parent = nullptr);
Q_SIGNALS:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *e) override;
};

class FlagsBox : public widgets::Section {
    // The nation's flags, small; a click on one (or the button) opens the flag studio — a window of its own.
    Q_OBJECT
public:
    explicit FlagsBox(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    OptStr tag;
    std::vector<std::pair<QString, QString>> variants;
    std::vector<std::pair<QString, _Thumb *>> thumbs;
    QWidget *thumb_host;
    QGridLayout *thumb_grid;
    QLabel *note;
    QPushButton *open_btn;
    void _build_variants();
    void refresh(document::Document *doc, const OptStr &tag);
Q_SIGNALS:
    void studioRequested(const std::optional<QString> &sfx);   // the flag to edit ("" = the normal one …) or None
};

class PartiesBox : public widgets::Section {
    // full=False (the Nation page): the map colour and how many parties, with the button to the parties window;
    // full=True (inside that window): the list of parties and the one picked, to edit.
    Q_OBJECT
public:
    PartiesBox(moddata::Mod &mod, QWidget *parent = nullptr, bool full = true, bool colour = true);
    moddata::Mod &mod;
    bool full;
    document::Document *doc = nullptr;
    scriptstore::ScriptStore *store = nullptr;
    OptStr tag, rel;
    long long cur = 0;
    std::optional<std::tuple<OptStr, long long, QString, qsizetype, OptStr>> _sig;
    std::optional<QStringList> _gc_list;
    std::optional<QHash<QString, int>> _key_use;

    QLabel *file_note, *color_label, *color_note, *gc_label, *summary, *new_note, *warn;
    colorwheel::ColorButton *color_btn;
    QComboBox *gc_box;
    QCheckBox *live;
    QPushButton *win_btn, *new_btn;
    QWidget *new_host, *form_host, *form = nullptr;
    QLineEdit *new_name;
    QTreeWidget *lst;
    QVBoxLayout *form_lay;

    void liveToggled();
    bool is_live() const;
    void _color_preview(const QString &col);
    void select(int i);
    OptStr loc(const QString &key) const;
    std::vector<std::pair<QString, QStringList>> issue_groups() const;
    void refresh(document::Document *doc, const OptStr &tag);
    void _picked(QTreeWidgetItem *it);
    void _fill_form(const QString &text, const std::vector<const script::Node *> &ps);
    QStringList gc_names() const;
    void _show_gc(const QString &text);
    void _gc();
    void _emit(const QString &what, const QString &fresh);
    void _field(const QString &key, const QString &value, QLineEdit *ed = nullptr, bool ok = true);
    int _key_users(const QString &key);
    void _own_key(const QString &key);
    void _new_named();
    bool _new(const QString &key);
    void _remove();
    void _color(const QString &col);
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);
    void nameEdit(const QString &key, const QString &text);
    void partyPicked(long long i);                // (the window's timeline follows)
    void windowRequested();
    void colorPreview(const std::optional<widgets::Rgb> &rgb);   // the colour the wheel is on right now, or None
};

}  // namespace nationextras
