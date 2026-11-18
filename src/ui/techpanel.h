// techpanel.py — Technology tab (full screen, no map): every technology of a folder laid out like the game's tech
// screen; the selected technology's name, year, cost, column and effects; the inventions that come with it.
//
// - A column is always in the order of the years: that's the order the game researches it (each technology needs the
//   one above it) and the order the file gets on Apply. Same-year technologies are marked ⇅ — pick which comes first.
// - New technologies only come as a whole new row (one in every column: the game needs them all equally long).
// - Effects are added in two steps: pick the effect (type to search), then fill in the field made for it (a building,
//   a unit, a number …). Every row shows what it writes; "Show as in the file" shows the whole block live.
#pragma once
#include "core/ordered_map.h"
#include "core/techdata.h"

#include <QFrame>
#include <QPlainTextEdit>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSplitter;
class QVBoxLayout;
namespace document { class Document; }
namespace moddata { class Mod; }
namespace widgets { class Section; class Stepper; }
namespace defpanel { class DefView; }

namespace techpanel {

using OptStr = std::optional<QString>;
using IPath = std::vector<qsizetype>;

void searchable(QComboBox *box, int visible = 20);   // an editable combo box that lists everything on click, filters
QString fmt(const QString &n);
QString short_(const QString &n);                     // 3600 -> 3.6k (research costs on the cards)
QString ordinal(long long n);

class NoteEdit : public QPlainTextEdit {              // a multi-line field that commits when it loses focus
    Q_OBJECT
public:
    using QPlainTextEdit::QPlainTextEdit;
Q_SIGNALS:
    void done(const QString &text);

protected:
    void focusOutEvent(QFocusEvent *ev) override;
};

// ---------------------------------------------------------------------------------------------- the grid
struct Fx {                                          // a line of a card: sign +1 / -1 colours the number
    int sign;
    QString num, words;
};
struct Col {                                         // (area, its title, its keys, the place in it)
    OptStr a;
    QString at;
    QStringList ks;
    long long j = 0;
};
struct Info {                                        // a technology's card
    QString name, year, cost;
    QStringList unlocks;
    long long inv = 0;
    bool same = false, isnew = false;
    Col col;
    std::vector<Fx> fx;
};
struct Area {
    QString a, title;
    QStringList keys;
};
struct Folder {
    QString key, title;
    std::vector<Area> areas;
};

std::pair<QString, int> signed_(const QString &v);   // 0.05 -> (+5 %, 1), 1 -> (+1, 1), -0.25 -> (−25 %, -1)
std::vector<Fx> effect_lines(const QString &text, const std::function<QString(const QString &)> &name_of);

class TechGrid : public QWidget {                    // a folder's areas as columns of cards, like the game's tech screen
    Q_OBJECT
public:
    static constexpr int GAP = 18, HEAD_H = 50, PAD = 16, RAIL = 62, CARD_MIN = 118, CARD_MAX = 156;
    explicit TechGrid(QWidget *parent = nullptr);
    std::vector<Area> areas;
    OrderedMap<QString, Info> info;
    OptStr sel, flash;                               // flash: a card to highlight (search result)
    std::optional<std::set<QString>> match;
    int CARD_H = 128;
    int _view_h = 0;
    std::vector<std::pair<QRectF, QString>> _cells;
    std::optional<std::pair<QString, QPointF>> _press, _drag;   // _drag: (key, mouse position) while dragging a card
    QString row_years(long long j) const;
    long long rows() const;
    void set_view_height(int h);
    void _fit();
    void set_folder(const std::vector<Area> &areas, const OrderedMap<QString, Info> &info);
    QSize sizeHint() const override;
    double _col_w() const;
    bool _can_drop(const QString &key, const QString &target) const;
    OptStr _hit(const QPointF &pos) const;
Q_SIGNALS:
    void picked(const QString &key);
    void dropped(const QString &key, const QString &target);   // dragged key, the card it was dropped on

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
};

// ---------------------------------------------------------------------------------------------- effects
// EffectSchema or ConditionSchema (Python: duck typed — only ConditionSchema has a name)
struct Schema {
    techdata::EffectSchema *e = nullptr;
    techdata::ConditionSchema *c = nullptr;
    explicit operator bool() const { return e || c; }
    bool operator==(const Schema &o) const { return e == o.e && c == o.c; }
    OptStr name(const QString &key) const { return c ? OptStr(c->name(key)) : std::nullopt; }
    techdata::Kind kind(const QString &key, const OptStr &parent = std::nullopt) const
    {
        return e ? e->kind(key, parent) : c->kind(key, parent);
    }
    QString example(const QString &key, const OptStr &parent = std::nullopt) const
    {
        return e ? e->example(key, parent) : c->example(key, parent);
    }
    QStringList keys() const { return e ? e->keys() : c->keys(); }
};

using Options = std::optional<std::vector<std::pair<QString, QString>>>;

class ValueField : public QWidget {                  // a list, a tick box or a number box with an example
    Q_OBJECT
public:
    ValueField(const QString &kind, const Options &options = std::nullopt, const QString &value = QString(),
               const QString &example = QStringLiteral("0.1"), QWidget *parent = nullptr);
    QString kind;
    QWidget *w;
    QString value() const;
    void focus();
Q_SIGNALS:
    void committed(const QString &value);
};

class EffectEditor : public QWidget {                // the lines of a block as rows; sub-blocks as headings
    Q_OBJECT
public:
    explicit EffectEditor(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    Schema schema;
    QString text;
    IPath ipath;
    std::set<QString> skip;
    QVBoxLayout *outer;
    QWidget *body = nullptr;                         // the rows: built off-screen, then swapped in (fast)
    QVBoxLayout *lay = nullptr;
    struct Pending {                                 // (parent ipath, parent key, key) of the row being added
        IPath ipath;
        OptStr parent;
        QString key;
        bool operator==(const Pending &) const = default;
    };
    std::optional<Pending> pending;
    ValueField *_pending_field = nullptr;
    std::function<void()> _pending_commit;
    QString add_text = QStringLiteral("Add an effect");
    void set_block(const QString &text, const IPath &ipath, const std::set<QString> &skip, const Schema &schema);
    QString _name(const QString &key) const;
Q_SIGNALS:
    void changed(const QString &text, const QString &label);   // new block text, undo label

private:
    struct Sig {
        IPath ipath;
        std::set<QString> skip;
        std::optional<Pending> pending;
        std::vector<std::pair<qsizetype, QString>> kids;
        bool operator==(const Sig &) const = default;
    };
    std::optional<Sig> _sig;
    long long _add_rows(const script::Node &c, const IPath &ipath, int depth, const OptStr &parent);
    QPushButton *_x(const IPath &ipath, const QString &tip);
    std::vector<std::pair<QString, QString>> _options(const OptStr &parent) const;
    QWidget *_adder(const IPath &ipath, const OptStr &parent, int depth);
    QComboBox *_open_adder(QWidget *w, QHBoxLayout *h, QPushButton *opener, const QString &hint, const IPath &ipath,
                           const OptStr &parent);
    QFrame *_pending_row(const IPath &ipath, const OptStr &parent, const QString &key, int depth);
};

class ChanceEditor : public QWidget {                // the factors of a chance block, each as a card
    Q_OBJECT
public:
    explicit ChanceEditor(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    QVBoxLayout *outer;
    QWidget *body = nullptr;
    QVBoxLayout *lay = nullptr;
    QString text;
    IPath ipath;
    std::vector<EffectEditor *> editors;
    void set_block(const QString &text, const IPath &ipath, const Schema &schema);
Q_SIGNALS:
    void changed(const QString &text, const QString &label);

private:
    std::optional<std::pair<IPath, QString>> _sig;
};

class RawBox : public QWidget {                      // Edit as text: the whole block, checked before it's taken
    Q_OBJECT
public:
    explicit RawBox(QWidget *parent = nullptr);
    QPlainTextEdit *ed;
    QLabel *err;
    QPushButton *cancel, *ok;
    OptStr key;
    void open(const QString &key, const QString &text);
Q_SIGNALS:
    void saved(const QString &text);

private:
    void _ok();
};

class FilePreview : public QWidget {                 // "Show as in the file": the block as Apply will write it
    Q_OBJECT
public:
    explicit FilePreview(QWidget *parent = nullptr);
    QCheckBox *box;
    QPlainTextEdit *view;
    void set_text(const QString &text);
};

// ---------------------------------------------------------------------------------------------- the workspace
// interface/country_technology.gui with room for `rows` rows of technologies; None when not laid out like the game's
OptStr fit_tech_window(QString text, long long rows);

struct NewTech {                                     // (key, file, text, name)
    QString key, file, text, name;
};

class TechWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit TechWorkspace(moddata::Mod &mod, QWidget *parent = nullptr);
    ~TechWorkspace() override;
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    techdata::TechEdits *edits = nullptr;
    std::unique_ptr<techdata::EffectSchema> schema;
    std::unique_ptr<techdata::ConditionSchema> cond_schema;
    long long folder = 0;
    OptStr sel, sel_inv, _inv_armed;
    std::vector<Folder> _folders_cache;
    OrderedMap<QString, Info> _info_cache;
    OrderedMap<QString, QStringList> _inv_of;
    std::map<QString, QString> _fx_names;
    QHBoxLayout *bar_layout;
    std::vector<QPushButton *> folder_btns;
    QButtonGroup *folder_group;
    QPushButton *schools_btn = nullptr;
    QLineEdit *search;
    QLabel *search_note, *rows_warn;
    QPushButton *row_btn, *row_rm_btn, *fit_btn;
    long long _rows_now = 6;
    QSplitter *split;
    TechGrid *grid;
    QWidget *detail;
    QLabel *t_title, *t_sub, *t_order, *same_note, *t_ai;
    QLineEdit *t_name, *t_cost;
    widgets::Stepper *t_year;
    QComboBox *t_area;
    QWidget *same_row;
    QPushButton *up_btn, *dn_btn, *t_raw_btn;
    QCheckBox *t_unciv;
    EffectEditor *t_effects;
    ChanceEditor *t_chance;
    FilePreview *t_file;
    RawBox *t_raw;
    QWidget *inv;
    QLabel *i_head, *i_new_msg, *i_title, *i_sub, *i_chance_note, *i_need_note, *i_mods_title;
    QListWidget *i_list;
    QLineEdit *i_new_name, *i_name;
    QPushButton *i_new_btn, *i_del, *i_raw_btn;
    widgets::Section *i_section;
    NoteEdit *i_desc;
    QComboBox *i_needs;
    widgets::Stepper *i_chance;
    ChanceEditor *i_chance_ed;
    QCheckBox *i_news;
    EffectEditor *i_effects, *i_mods;
    FilePreview *i_file;
    RawBox *i_raw;
    defpanel::DefView *schools;
    bool unciv_on = false;                           // (the schools page is shown instead of the technologies)
    std::optional<qsizetype> _search_sig;
    std::map<QString, std::pair<QString, OptStr>> _search_map;   // (sorted: Python sorts its keys where it walks them)

    void set_unciv(bool on);
    QString name(const OptStr &key) const;
    QString _fx_name(const QString &key);
    OrderedMap<QString, Info> &_compute();
    void refresh(document::Document *doc);
    void _fill_grid(bool compute = true);
    void _set_folder(long long i);
    void select(const QString &key);
    void _fill_tech();
    void _fill_inv();
    void _search_items(const OrderedMap<QString, Info> &info);
    void _search_pick(const QString &text);
    void _search_jump();
    void _dropped(const QString &key, const QString &target);
    void _set_text(const OptStr &key, const QString &text, const QString &what);
    void _name(const OptStr &key, const QString &text);
    void _field(const QString &key, const OptStr &value);
    void _inv_row(int row);
    void _inv_needs();
    void _inv_chance(long long v);
    void _inv_news(bool on);
    void _inv_new();
    void _inv_delete();
    std::vector<NewTech> new_row_plan() const;
Q_SIGNALS:
    void guiPatchRequested(long long rows);          // make the game's technology window fit this many rows
    void edit(const QString &label, const QString &key, const QString &text);   // undo label, key, new text
    void nameRequested(const QString &key, const QString &text, const QString &section);
    void orderRequested(const OptStr &area, const QString &key, long long step);   // -1 / +1 (same-year order)
    void rowRequested();                             // one new technology in every column
    void rowRemoveRequested();                       // the last row (7th and up) goes
    void dropRequested(const OptStr &area, const QString &key, const QString &target);
    void inventionRequested(const QString &key, const QString &file, const QString &text, const QString &name);
    void inventionRemoveRequested(const QString &key);
    void message(const QString &text, int ms);       // (Python: self.window().message)
};

}  // namespace techpanel
