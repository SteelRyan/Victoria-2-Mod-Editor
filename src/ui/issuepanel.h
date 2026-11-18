// issuepanel.py — Reforms and party issues (common/issues.txt), shown and edited: every group (political / social
// reforms, party issues, the unciv economic / military reforms) → its issues → their options. An option's effects (the
// numbers it changes) are edited in place, ✕ takes one off, a box adds one; its other blocks (rules, allow,
// on_execute …) are edited as text; names and descriptions in the game; which nations have it at the start. The text
// is the editor's (doc.maps["scripts"]): every change is one undoable step, written on Apply.
// (reindent, the data half, is in core/issuepanel_data.)
#pragma once
#include "core/document.h"
#include "core/issuepanel_data.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/script.h"
#include "core/scriptstore.h"

#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QComboBox;

namespace locwin {
class GameLook;
}
namespace reformwin {
class GameReformView;
}
namespace widgets {
class Section;
}

namespace issuepanel {

using OptStr = std::optional<QString>;
extern const QString REL;
extern const OrderedMap<QString, QString> GROUP_NAMES;
QString strip_colours(const QString &s);              // COLOR_RE.sub("", s)

using Sel = std::tuple<QString, QString, OptStr>;    // (group, issue, option or None)
using Tree = std::vector<std::pair<QString, std::vector<std::pair<QString, QStringList>>>>;

class IssueView : public QWidget {
    Q_OBJECT
public:
    IssueView(moddata::Mod &mod, const QStringList &groups, const QString &intro, bool wide = false,
              QWidget *parent = nullptr);
    ~IssueView() override;
    moddata::Mod &mod;
    QStringList groups;
    document::Document *doc = nullptr;
    scriptstore::ScriptStore *store = nullptr;
    std::optional<Sel> sel;
    QString part;
    bool as_window = false;
    reformwin::GameReformView *win_view = nullptr;
    std::map<QString, QPushButton *> view_btn;
    QLineEdit *search, *name_edit, *desc_edit;
    QTreeWidget *lst;
    widgets::Section *list_sec, *detail;
    QLabel *head, *title, *sub, *warn;
    QWidget *names_host, *rows_host;
    QVBoxLayout *rows;

    QString text();
    void refresh(document::Document *doc);
    void _set_view(const QString &k);
    void _show_view();
    void _fill_window();
    void _picked_in_window(const QString &iss, const QString &opt = QString());
    Tree tree();                                      // [(group, [(issue, [option])])] of the groups shown
    OptStr loc(const QString &key);
    QString nm(const QString &key);
    OptStr _key() const;
    const std::map<std::pair<QString, QString>, QStringList> &holders();   // {(issue, option): [tags]}
    void _fill_list();
    void _picked(QTreeWidgetItem *it, QTreeWidgetItem *prev);
    void _clear();
    void _fill_detail();
    void _issue_rows(const QString &grp, const QString &iss, const std::vector<script::Node> &kids);
    void _go_option(const QString &grp, const QString &iss, const QString &opt);
    void _sync_window();
    void _option_rows(const QString &grp, const QString &iss, const QString &opt, const std::vector<script::Node> &kids);
    QWidget *_block_box(const QStringList &path, const QString &key);
    void _emit_change(const QString &what, const std::function<QString(const QString &)> &fn);
    void _set_num(const QStringList &path, const QString &k, QLineEdit *ed);
    void _add_num(const QStringList &path, QComboBox *box, QLineEdit *val);
    void _loc_done(QLineEdit *ed, const OptStr &key);
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);   // undo label, rel file, new text
    void nameEdit(const QString &key, const QString &text);                           // localisation key, text

private:
    using Sig = std::pair<QStringList, std::vector<std::pair<QString, bool>>>;
    std::optional<Sig> _sig;
    OrderedMap<QString, QLineEdit *> _edits;
    std::optional<std::map<std::pair<QString, QString>, QStringList>> _holders;
    QSplitter *_split = nullptr;
    std::optional<bool> _shown_as;
    std::unique_ptr<locwin::GameLook> _look;
};

}  // namespace issuepanel
