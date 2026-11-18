// adjacency.py, its data half — map/adjacencies.csv: sea crossings (straits, Meme-river crossings, coast crossings),
// canals and impassable borders. (The geometry half — AdjGeometry, detect_rivers, the checks — uses arrays and
// comes with the map in phase 2: map/adjacency_geo.)
// Row format: From;To;Type;Through;Data;Comment. Saving keeps every untouched line byte-for-byte; changed rows are
// rewritten in place, deleted rows are cut, new rows are inserted after the last row of the same kind.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"

#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>
#include <tuple>
#include <vector>

namespace adjacency {

using Rgb = std::tuple<int, int, int>;
using OptUid = std::optional<long long>;

struct Row {                               // namedtuple("Row", "uid a b type through data comment")
    long long uid = 0, a = 0, b = 0;
    QString type;
    long long through = 0, data = 0;
    QString comment;
    bool operator==(const Row &o) const = default;
};
struct Mark {                              // a Meme river: its name and sea tile(s)
    long long uid = 0;
    QString name;
    std::vector<long long> tiles;
    bool operator==(const Mark &o) const = default;
};
struct RowFields {                         // parse_row's tuple: (a, b, type, through, data, comment)
    long long a, b;
    QString type;
    long long through, data;
    QString comment;
};

extern const QRegularExpression MARK_RE;
extern const QString NC_HEAD;
extern const QRegularExpression NC_HEAD_RE;
extern const QRegularExpression NC_RE;
extern const QStringList TYPES;
extern const QRegularExpression RIVER_RE;
struct Category {
    QString key, label;
    Rgb color;
};
extern const std::vector<Category> CATEGORIES;
extern const OrderedMap<QString, QString> CAT_HELP;
extern const OrderedMap<QString, Rgb> CAT_COLOR;
extern const Rgb MISSING_COLOR, STALE_COLOR, PREVIEW_COLOR;

std::optional<RowFields> parse_row(const QString &line);
std::optional<std::pair<QString, std::vector<long long>>> parse_mark(const QString &line);
QString fmt_mark(const Mark &m);
QString fmt_row(const Row &r);

using Line = std::pair<QString, OptUid>;   // (text, uid or None)

class AdjacencyFile {
public:
    static constexpr const char *kind = "adjacency";
    static constexpr bool undo_after_apply = true;
    explicit AdjacencyFile(moddata::Mod &mod);
    moddata::Mod &mod;
    QString path;
    QSet<long long> sea;
    std::vector<Line> lines;               // as on disk
    std::vector<Row> rows;                 // current rows, file order (new ones at the end)
    OrderedMap<long long, Row> orig;       // uid -> Row as on disk
    std::vector<Mark> marks;               // current Meme rivers
    OrderedMap<long long, Mark> orig_marks;
    QSet<long long> no_coast, orig_no_coast;
    QSet<long long> _nc_lines;             // line indexes of the no-coast block
    long long next_uid = 1;
    OrderedMap<long long, OptUid> was_after;   // uid of a row Apply took out -> uid of the line it stood under
    std::optional<QString> hash;
    QString nl = QStringLiteral("\r\n");
    bool trailing_nl = true;

    void _load(const QString &text);
    const Row *get(long long uid) const;
    QString category(const Row &r) const;
    const OrderedMap<std::pair<long long, long long>, std::vector<Row>> &pair_index();
    QString coast_comment() const;
    long long river_names() const;
    OrderedMap<long long, Mark> river_tiles() const;
    const Mark *mark_by_name(const QString &name) const;
    QSet<long long> skip_coast() const;
    std::vector<long long> canal_numbers() const;
    QStringList dirty() const;
    std::vector<long long> add(const std::vector<RowFields> &specs);
    void update(long long uid, const std::function<void(Row &)> &change);
    void del(const QList<long long> &uids);          // (Python: delete)
    void set_no_coast(const QList<long long> &tiles, bool on = true);
    void set_mark(const QString &name, const QList<long long> &tiles);
    void rename_mark(const QString &old, const QString &fresh);
    void unmark(const QList<long long> &tiles);
    struct Snap {
        std::vector<Row> rows;
        std::vector<Mark> marks;
        QSet<long long> no_coast;
        long long next_uid;
        bool operator==(const Snap &o) const = default;
    };
    Snap snapshot() const { return {rows, marks, no_coast, next_uid}; }
    void restore(const Snap &s)
    {
        rows = s.rows;
        marks = s.marks;
        no_coast = s.no_coast;
        next_uid = s.next_uid;
        rows_id += 1;                       // (list(snap[0]): a new list)
    }
    std::optional<qsizetype> _block_start(const QString &name) const;
    std::vector<Line> _no_coast_block() const;
    std::optional<QString> save(const QString &backup_root);

    // Python's rows list identity: a new list (load, delete, restore, rename) counts up; append / item change don't
    long long rows_id = 0;

private:
    std::optional<std::tuple<long long, size_t, OrderedMap<std::pair<long long, long long>, std::vector<Row>>>> _pairs;
};

}  // namespace adjacency

