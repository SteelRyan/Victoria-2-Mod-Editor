// importpanel.py — The Import page: pick another mod, tick what to take over — by kind (events with their chains,
// decisions, nations, modifiers, governments, units …) — and see, before anything happens, everything that comes along
// with it (what the picked things need, their texts, pictures, flags, icons) and what to watch out for. One button
// brings it all in as one undoable step; Apply writes it.
// The ticks are a Python set of (kind, key): its order reaches the import (grid::PySet with CPython's tuple hash).
#pragma once
#include "core/ordered_map.h"
#include "grid/pyset.h"
#include "map/importer.h"

#include <QString>
#include <QWidget>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
namespace document { class Document; }
namespace mapview { class MapData; }
namespace scriptedit { class ScriptHighlighter; class EditorHost; }
namespace regionimport { class RegionImportPage; }

namespace importpanel {

struct Status {
    QString text, colour;
};
extern const std::map<QString, Status> STATUS;
extern const int FILE_LIMIT;

class ImportWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit ImportWorkspace(moddata::Mod &mod, QWidget *parent = nullptr);
    ~ImportWorkspace() override;
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    std::unique_ptr<importer::Source> src;
    std::unique_ptr<importer::Importer> imp;
    QString cat = QStringLiteral("events");
    grid::PySet<importer::Id> picked;
    std::set<importer::Id> left_out, also;
    std::optional<importer::Plan> plan;
    std::map<QString, std::pair<long long, long long>> _counts;
    QStringList _todo;
    OrderedMap<QString, std::vector<importer::ItemP>> _kids;   // heading of the list -> its things
    bool _filling = false;
    std::optional<std::pair<long long, const void *>> _undo_mark;
    QString mode = QStringLiteral("things");
    mapview::MapData *md = nullptr;              // this mod's map (set by the main window)
    std::map<QString, QPushButton *> mode_btn;
    QComboBox *src_box, *show_box, *nation_mode, *ev_mode;
    QLineEdit *path, *search;
    QLabel *src_info, *cat_title, *cat_about, *count, *preview_head, *result;
    QSplitter *things;
    regionimport::RegionImportPage *region;
    QTreeWidget *cats, *lst, *plan_tree;
    QPushButton *chain_btn, *go, *empty_btn;
    QPlainTextEdit *preview;
    QCheckBox *icons;
    QTimer *_fill_timer, *_plan_timer, *_count_timer;

    void set_mode(const QString &k);
    void _to_region();
    void refresh(document::Document *doc);
    void _browse();
    void _open(const QString &folder);
    void _restart();
    void _count_step();
    void _set_count(const QString &cid, const std::vector<importer::ItemP> &items);
    void _cat_picked(QTreeWidgetItem *it, QTreeWidgetItem *prev);
    void _fill_items();
    QTreeWidgetItem *_row(const importer::ItemP &it);
    void _expanded(QTreeWidgetItem *parent);
    void _item_changed(QTreeWidgetItem *row, int col);
    std::vector<QTreeWidgetItem *> _rows();
    void _tick_all(bool on);
    void _add_chains();
    void _show_preview(QTreeWidgetItem *row, QTreeWidgetItem *prev);
    void _replan();
    importer::Options _options() const;
    void _make_plan();
    void _plan_changed(QTreeWidgetItem *row, int col);
    void _clear();
    void _import();
    importer::ItemP item_of(QTreeWidgetItem *row) const;   // row.data(0, UserRole): its thing, or None
Q_SIGNALS:
    void message(const QString &text);
    void imported();                             // the main window rebuilds what depends on the mod's lists

private:
    QLabel *_bold(const QString &text);
    std::unique_ptr<scriptedit::EditorHost> _norefs;
    scriptedit::ScriptHighlighter *_hl = nullptr;
    std::map<quintptr, importer::ItemP> _held;   // (the things the rows stand for: kept alive while listed)
    // (replaced sources / importers stay alive: the map page may still point at them, as Python's references do)
    std::vector<std::unique_ptr<importer::Importer>> _old_imp;
    std::vector<std::unique_ptr<importer::Source>> _old_src;
    void _retire();
};

}  // namespace importpanel
