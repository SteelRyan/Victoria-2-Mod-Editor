// rail.py — The navigation rail on the window's left edge (instead of a row of tabs): every page of the editor
// under the group it belongs to — Map, Nations, Rules, Tools. Each entry shows its pending edits (gold) and its
// errors (red). The rail folds to a slim strip of icons (the « button at its foot); the choice is remembered.
#pragma once
#include "core/ordered_map.h"

#include <QAbstractButton>
#include <QString>
#include <QWidget>
#include <optional>
#include <vector>

class QFrame;
class QLabel;

namespace rail {

struct Page {                                 // (page, name, short code, the tab it is, the tab's sub-page, tooltip)
    QString key, name, code, tab;
    std::optional<QString> sub;
    QString tip;
};
struct Group {
    QString name;
    std::vector<Page> pages;
};
extern const std::vector<Group> RAIL;
// pages that are reached from inside another page: no entry of their own, the rail marks the page they belong to
extern const std::vector<Page> EXTRA;
extern const OrderedMap<QString, QString> ALIAS;
extern const OrderedMap<QString, Page> PAGES;
extern const OrderedMap<QString, QString> PAGE_NAMES;
extern const int WIDE, SLIM;

std::optional<QString> page_of(const QString &tab, const std::optional<QString> &sub = std::nullopt);

class RailItem : public QAbstractButton {
    Q_OBJECT
public:
    explicit RailItem(const Page &page, QWidget *parent = nullptr);
    QString key, name, code, tip;
    long long pending = 0, errors = 0;
    bool slim = false;
    QSize sizeHint() const override;
    void set_counts(long long pending, long long errors);

protected:
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void paintEvent(QPaintEvent *e) override;

private:
    void _tip();
};

class FoldButton : public QAbstractButton {   // « / » at the rail's foot: fold it to the slim strip, or open it again
    Q_OBJECT
public:
    explicit FoldButton(QWidget *parent = nullptr);
    bool slim = false;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;
};

class NavRail : public QWidget {
    Q_OBJECT
public:
    explicit NavRail(bool slim = false, QWidget *parent = nullptr);
    OrderedMap<QString, RailItem *> items;
    std::vector<QLabel *> heads;
    std::vector<QFrame *> lines;
    FoldButton *fold;
    bool slim = false;
    void set_slim(bool slim, bool quiet = false);
    void set_current(const QString &key);
    void set_counts(const OrderedMap<QString, long long> &pending, const OrderedMap<QString, long long> &errors);
Q_SIGNALS:
    void picked(const QString &key);         // a page was clicked
    void folded(bool slim);
};

}  // namespace rail
