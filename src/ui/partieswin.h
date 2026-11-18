// partieswin.py — A nation's parties in a window of their own: a timeline — every party a thick line from the year
// it appears to the year it goes, in the colour of its ideology, parties of one ideology in the same band so one sees
// which follows which and which exist side by side. Hovering a line shows the party's policies; the filters leave
// only the parties of one ideology or with one policy; a line's ends can be dragged to other years. Beside it the
// party picked, with everything about it to edit.
#pragma once
#include "core/document.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "ui/widgets.h"

#include <QDialog>
#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QWidget>
#include <functional>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

class QComboBox;
class QLabel;

namespace scriptstore {
class ScriptStore;
}
namespace nationextras {
class PartiesBox;
}

namespace partieswin {

using OptStr = std::optional<QString>;
using widgets::Rgb;

inline constexpr int ROW = 40;     // a lane: the name above, the line below
inline constexpr int LEFT = 150;   // room for the ideology names
inline constexpr int TOP = 34;     // the years

double year_of(const OptStr &date, double fallback);
OrderedMap<QString, Rgb> ideology_colors(scriptstore::ScriptStore *store);   // in the order of common/ideologies.txt

struct Party {
    long long i = 0;                               // its index in the file
    QString key, name, ideology;
    double a = 0, b = 0;                           // the years
    QHash<QString, QString> f;                     // its fields
    std::vector<std::pair<QString, QString>> policies;
};

class Timeline : public QWidget {
    Q_OBJECT
public:
    explicit Timeline(QWidget *parent = nullptr);
    std::vector<Party> parties;
    OrderedMap<QString, Rgb> colors;
    QHash<QString, QString> names;                 // ideology -> its name
    long long sel = -1;
    std::pair<double, double> game{1836.0, 1936.0};
    std::function<bool(const Party &)> filter;     // (empty: None)
    std::vector<std::pair<size_t, int>> _lanes;    // [(party, lane)]
    std::vector<std::tuple<QString, int, int>> _heads;   // [(ideology, first lane, lanes)]
    double _lo = 1800.0, _hi = 1950.0;
    std::optional<QPointF> _mouse;
    struct Drag {
        size_t q;
        QString which;                             // "a" / "b"
        double yr;
    };
    std::optional<Drag> _drag;
    std::function<QString(const QString &)> loc;

    void set_data(std::vector<Party> parties, OrderedMap<QString, Rgb> colors, QHash<QString, QString> names,
                  std::pair<double, double> game);
    void relayout();
    double _x(double year) const;
    double _year(double x) const;
    QRectF _line(const Party &p, int lane) const;
    std::tuple<std::optional<size_t>, int, QString> _at(const QPointF &pos) const;   // (party, lane, "a"/"b"/"")
    std::vector<size_t> active(double year) const;
    QString tip(const Party &q) const;
    Rgb color(const QString &ideo) const;
Q_SIGNALS:
    void picked(int i);                            // a party's index in the file
    void dateDragged(int i, const QString &key, const QString &date);   // "start_date" / "end_date", the new date
    void hovered(const QString &text);             // the line under the timeline: the year and who is active

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
};

class PartiesWindow : public QDialog {
    Q_OBJECT
public:
    explicit PartiesWindow(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    OptStr tag;
    QLabel *title, *status;
    QComboBox *f_ideo, *f_issue, *f_opt;
    Timeline *timeline;
    nationextras::PartiesBox *box;
    std::optional<std::pair<QStringList, std::vector<std::pair<QString, QStringList>>>> _f_sig;
    OrderedMap<QString, QStringList> _issues;

    QString _loc(const QString &key) const;
    void open_for(document::Document *doc, const OptStr &tag);
    void refresh(document::Document *doc, const OptStr &tag = std::nullopt);
    void _fill_opts(const QVariant &keep = QVariant());
    std::function<bool(const Party &)> _filter_fn() const;
    void _filter();
    void _count();
    void _timeline_picked(int i);
    void _box_picked(long long i);
    void _date_dragged(int i, const QString &key, const QString &date);
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);
    void nameEdit(const QString &key, const QString &text);

protected:
    void keyPressEvent(QKeyEvent *e) override;
};

}  // namespace partieswin
