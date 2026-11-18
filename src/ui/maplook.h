// maplook.py, its window half: LookPopup — the colours of a map mode (the ◐ button next to the map modes): smooth
// (the shared scale or one of its own) or ranges; for State size / Province size the size filters. The data half
// (the scales, ranges, load / save) is core/maplook.
#pragma once
#include "core/maplook.h"

#include <QFrame>
#include <QWidget>
#include <functional>
#include <map>
#include <utility>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace maplook {

class _Bar : public QWidget {                    // the smooth scale drawn as one bar, the stops as dark lines
public:
    explicit _Bar(std::function<const Stops &()> stops, QWidget *parent = nullptr);
    std::function<const Stops &()> stops;

protected:
    void paintEvent(QPaintEvent *e) override;
};

class LookPopup : public QFrame {
    Q_OBJECT
public:
    // `values` (optional): the mode's values on the map now, for "ranges from the map"
    LookPopup(const QString &mode, QWidget *parent = nullptr, std::function<std::vector<double>()> values = nullptr);
    QString mode;
    std::function<std::vector<double>()> values;
    QLabel *msg;
    bool size_mode;
    std::map<QString, QPushButton *> how;        // (Python's dict: keys "ranges" / "smooth")
    QVBoxLayout *body = nullptr;
    // the size filters
    QString kind;
    std::vector<std::pair<QString, QPushButton *>> by;
    QVBoxLayout *ranges = nullptr;
    QPushButton *add_range = nullptr;
    QLabel *legend = nullptr;
    std::vector<std::pair<QLineEdit *, QLineEdit *>> range_edits;

    Scale &_s();
    void _fill();
    void _fill_body();
    void _own(bool on);
    void _how(const QString &how);
    void _smooth_editor(std::function<Stops &()> target, bool shared);
    void _set_stops(const std::function<Stops &()> &target, const Stops &stops);
    void _stop_colour(const std::function<Stops &()> &target, int n, const QString &c, _Bar *bar);
    void _stop_place(const std::function<Stops &()> &target, int n, QLineEdit *ed);
    void _add_stop(const std::function<Stops &()> &target);
    void _ranges_editor();
    void _reset_ranges(const Ranges &rs);
    void _add_mode_range();
    void _range_rows(QVBoxLayout *lay, const std::function<Ranges &()> &rg, int dec, std::function<void()> refill);
    void _range(const std::function<Ranges &()> &rg, int n, QLineEdit *a, QLineEdit *b, int dec,
                const std::function<void()> &refill);
    void _range_colour(const std::function<Ranges &()> &rg, int n, const QString &c);
    void _range_del(const std::function<Ranges &()> &rg, int n, const std::function<void()> &refill);
    void _build_size(QVBoxLayout *lay);
    void _fill_size();
    void _by(const QString &k);
    void _add_range();

Q_SIGNALS:
    void changed();
};

}  // namespace maplook
