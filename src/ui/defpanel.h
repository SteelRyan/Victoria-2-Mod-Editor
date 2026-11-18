// defpanel.py — Lists of things the game defines as plain blocks, shown and edited the same way: government forms
// (common/governments.txt), national values (common/nationalvalues.txt) and technology schools (the "schools" block of
// common/technology.txt). The list left, the one picked right: its name in the game, what it does (numbers: edit, ✕,
// add — or for a government form which parties may rule, elections, the flag), which nations start with it; new ones
// are made as a copy of an existing one, ones no nation starts with can be removed. The text is the editor's
// (doc.maps["scripts"]): every change is one undoable step, written on Apply.
// (The icon-strip warning of the national values — spec.strip — comes with iconstrips, on the Game rules page.)
#pragma once
#include "core/document.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/script.h"
#include "core/scriptstore.h"

#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
namespace widgets {
class Section;
}

namespace defpanel {

using OptStr = std::optional<QString>;

struct Spec {
    QString rel;
    QStringList base;
    QString kind, noun, title, state, cols;
    std::vector<std::pair<QString, QString>> names;   // (suffix of the localisation key, what it is)
    OptStr example, prefix, strip;
    QString intro;
    QStringList common;
};
extern const std::vector<std::pair<QString, QString>> GOV_FLAGS;
extern const Spec GOVERNMENTS, NATIONAL_VALUES, SCHOOLS;

QStringList ideologies(scriptstore::ScriptStore &store);   // [ideology] in the file's order

class DefView : public QWidget {
    Q_OBJECT
public:
    DefView(moddata::Mod &mod, const Spec &spec, QWidget *parent = nullptr);
    moddata::Mod &mod;
    Spec spec;
    QString rel;
    QStringList base;
    document::Document *doc = nullptr;
    scriptstore::ScriptStore *store = nullptr;
    OptStr cur;
    QLabel *strip_warn, *new_warn, *head, *title, *sub, *warn, *dur_label = nullptr;
    QLabel *pic_note = nullptr;                       // its picture, in words (C++ only, 2026-11-16)
    QString fresh;                                     // the one made last here: the picture note is a reminder
    QLineEdit *search, *new_name, *new_key, *dur_edit = nullptr;
    QTreeWidget *lst;
    QComboBox *new_from;
    widgets::Section *detail;
    OrderedMap<QString, QLineEdit *> name_edits;
    QWidget *names_host, *rows_host;
    QVBoxLayout *rows;

    QString text();
    void refresh(document::Document *doc);
    QStringList keys();
    OptStr loc(const QString &key);
    QString nm(const QString &key);
    const OrderedMap<QString, QStringList> &holders();   // {key: [tags]} — the nations (with land) that start with it
    QString summary(const QString &key);
    void _fill_list();
    void _picked(QTreeWidgetItem *it, QTreeWidgetItem *prev);
    void _clear();
    void _fill_detail();
    QLabel *_bold(const QString &text);
    void _num_rows(const QString &key, const std::vector<script::Node> &kids);
    void _gov_rows(const QString &key, const std::vector<script::Node> &kids);
    void _sync_gov(const OrderedMap<QString, QString> &vals);
    void _change(const QString &what, const std::function<QString(const QString &)> &fn);
    void _set_num(const QStringList &path, const QString &k, QLineEdit *ed);
    void _add_num(const QStringList &path, QComboBox *box, QLineEdit *val);
    void _flag(const QStringList &path, const QString &k, bool on, bool remove = false);
    void _duration(const QStringList &path, QLineEdit *ed);
    void _flag_type(const QStringList &path, const QString &v);
    void _loc_done(QLineEdit *ed, const QString &suffix);
    void _remove(const QString &key);
    void _name_typed(const QString &text);
    void _make_new();
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);   // undo label, rel file, new text
    void nameEdit(const QString &key, const QString &text);                           // localisation key, text

private:
    struct Sig {
        QString key;
        std::vector<std::pair<QString, bool>> kids;
        qsizetype holders = 0;
        std::optional<QStringList> ideos;
        bool operator==(const Sig &) const = default;
    };
    std::optional<Sig> _sig;
    OrderedMap<QString, QWidget *> _edits;
    std::optional<OrderedMap<QString, QStringList>> _holders;
    bool _key_typed = false;
};

}  // namespace defpanel
