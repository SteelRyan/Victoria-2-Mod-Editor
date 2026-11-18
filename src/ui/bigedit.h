// bigedit.py — An entry of a list page (a pop type, a war goal, a rebel type …) in a large window of its own: left an
// overview of its parts (what it promotes to, where it migrates, its ideology and issue leanings, its needs …) — a
// click goes there; right its text, edited directly, coloured like the Scripts page: every word the mod gives a meaning
// (a pop type, an ideology, a reform, a good, a nation …) in the colour of its kind, and hovering a word says what it is
// or what the line does. The text is the editor's own (doc.maps["scripts"]): what is typed here is what Apply writes.
#pragma once
#include "core/ordered_map.h"
#include "core/refs.h"
#include "core/script.h"
#include "ui/scriptedit.h"

#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <functional>
#include <optional>
#include <tuple>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
namespace document { class Document; }
namespace moddata { class Mod; }
namespace scriptstore { class ScriptStore; }

namespace bigedit {

// the parts of a pop type file: (name, what it is)
extern const OrderedMap<QString, std::pair<QString, QString>> PARTS;
// single words: the notation aid (hovering says what the line does)
extern const OrderedMap<QString, QString> GLOSSARY;

QHash<QString, QString> _names(moddata::Mod &mod, scriptstore::ScriptStore &store);   // {word: kind}

class LocalRefs : public refs::RefIndex {
    // What ScriptHighlighter / ScriptEditor ask of a reference index, for the one file of this window: the words that
    // are names, by position.
public:
    LocalRefs(moddata::Mod &mod, QHash<QString, QString> names);
    QHash<QString, QString> names;
    std::vector<refs::Ref> list;                 // Python: spans (of the one file)
    std::vector<qsizetype> starts;
    void build(const QString &text);
    const refs::Ref *at(const QString &rel, qsizetype pos) const override;
    std::vector<refs::Ref> in_range(const QString &rel, qsizetype a, qsizetype b) const override;
    std::optional<std::pair<QString, qsizetype>> definition(const QString &kind, const QString &target) const override;
};

class BigEditor;

class _Ws : public scriptedit::EditorHost {      // what a ScriptEditor asks of the workspace it lives in
public:
    _Ws(BigEditor *win, scriptstore::ScriptStore *store);
    BigEditor *win;
    scriptedit::ScriptEditor *current() const override;
    void _cursor_moved(scriptedit::ScriptEditor *ed) override;
    void _decorate(scriptedit::ScriptEditor *ed) override;
    void zoom(int step) override;
    void back() override;
    void forward() override {}
    void follow(const refs::Ref &r, bool new_tab = false) override;
    QString hover_html(const QString &rel, const refs::Ref &r) const override;
};

class _Editor : public scriptedit::ScriptEditor {
public:
    using ScriptEditor::ScriptEditor;

protected:
    bool viewportEvent(QEvent *e) override;
};

class BigEditor : public QDialog {
    // `changed()` is called a moment after the text was typed in (the main window counts the pending edits).
    Q_OBJECT
public:
    BigEditor(moddata::Mod &mod, document::Document *doc, std::function<void()> changed = {}, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc;
    std::function<void()> changed;
    scriptstore::ScriptStore *store;
    _Ws ws;
    LocalRefs *refs;
    std::optional<QString> rel;
    QStringList path;
    QString noun = QStringLiteral("entry");
    QPointer<scriptedit::ScriptEditor> ed;
    std::vector<std::tuple<QString, QStringList, QString>> _trail;   // (rel, path, noun) shown before (◀ Back)
    QHash<QString, QPointer<scriptedit::ScriptHighlighter>> _hl;     // rel -> ScriptHighlighter
    QPushButton *back_btn;
    QLabel *title, *sub, *found, *about, *where, *legend;
    QLineEdit *find;
    QTreeWidget *tree;
    QWidget *host;
    QVBoxLayout *host_lay;
    std::vector<std::pair<qsizetype, qsizetype>> _hits;

    void open_entry(const QString &rel, const QStringList &path, const QString &noun = QStringLiteral("entry"),
                    bool record = true);
    void back();
    std::optional<qsizetype> _node_start();      // _node().kstart (or None)
    QString _summary(const script::Node &n);
    QString _nm(const QString &key);
    void _fill_outline();
    void _outline_clicked(QTreeWidgetItem *it, int col);
    void _go(qsizetype pos, bool flash = true);
    void _cursor_moved();
    void _decorate();
    QString ref_html(const refs::Ref &r);
    std::optional<QString> _pop_rel(const QString &t);
    void follow(const refs::Ref &r);
    void _fold(bool on);
    void _find(int step);
    void _store_changed(const QString &rel);
    void _text_settled();
    void refresh(document::Document *doc);   // the main window's document changed (an undo, a revert, Apply)

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    QTimer *_timer;
    std::optional<std::pair<QString, QStringList>> _outline_of;
};

}  // namespace bigedit
