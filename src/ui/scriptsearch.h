// scriptsearch.py — Search and replace in the script files (the editor's texts): in this file, the open ones, all
// events and decisions, or every script file of the mod (events, decisions, common). Results grouped by file, a click
// goes there; Replace does the match at the cursor, Replace all every match — one undoable step (Ctrl+Z).
// (The pattern is Python's re in the original, PCRE2 here: a plain search, whole words and the usual expressions
// match alike; the message of a broken expression is PCRE2's own.)
#pragma once
#include "core/ordered_map.h"

#include <QFont>
#include <QHash>
#include <QRegularExpression>
#include <QWidget>
#include <optional>
#include <tuple>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace scriptedit { class ScriptWorkspace; }

namespace scriptsearch {

extern const std::vector<std::pair<QString, QString>> SCOPES;
constexpr int LIMIT = 5000;

// Python's Match.expand(template): \1 … \99, \g<n>, \g<name>, \\ \n \t \r \a \b \f \v
QString expand(const QRegularExpressionMatch &m, const QString &tmpl);

class SearchPanel : public QWidget {
    Q_OBJECT
public:
    explicit SearchPanel(scriptedit::ScriptWorkspace *ws);
    scriptedit::ScriptWorkspace *ws;
    std::vector<std::tuple<QString, qsizetype, qsizetype>> results;   // [(rel, start, end)]
    std::optional<QRegularExpression> _rx;
    QLineEdit *find, *repl;
    OrderedMap<QString, QPushButton *> opt;
    QComboBox *scope;
    QLabel *info;
    QTreeWidget *tree;
    QTimer *_timer;
    QString _note;
    QStringList files();
    std::optional<QRegularExpression> pattern();
    void open_panel(const std::optional<QString> &scope = std::nullopt, bool replace = false);
    void close_panel();
    void run();
    void next();
    void replace_one();
    void replace_all();
    std::vector<std::pair<qsizetype, qsizetype>> matches_in(const QString &rel, qsizetype a, qsizetype b);
Q_SIGNALS:
    void goTo(const QString &rel, long long start, long long end);
    void replaceRequested(const OrderedMap<QString, QString> &changes, const QString &label);   // {rel: new text}

private:
    QFont _mono;
    long long _gen = 0, _by_gen = -1;            // (Python: _by_file is kept for the one results list it was made of)
    QHash<QString, std::vector<std::pair<qsizetype, qsizetype>>> _by_file;
    QString _repl_for(const QRegularExpressionMatch &m) const;
    void _clicked(QTreeWidgetItem *it, int col);
};

}  // namespace scriptsearch
