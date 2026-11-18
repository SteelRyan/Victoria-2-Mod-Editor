// colsizes.py — resizable, remembered columns (see colsizes.h).
#include "ui/colsizes.h"
#include "core/settings.h"

#include <QEvent>
#include <QHeaderView>
#include <QObject>
#include <QTimer>
#include <QTreeWidget>
#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

namespace colsizes {

namespace {

class Watch : public QObject {
public:
    Watch(std::function<void(int)> fn, QObject *parent) : QObject(parent), fn(std::move(fn)) {}
    std::function<void(int)> fn;
    bool eventFilter(QObject *, QEvent *e) override
    {
        if (e->type() == QEvent::Resize)
            fn(0);
        return false;
    }
};

std::optional<std::vector<int>> saved_widths(const QString &key)   // [int(x) for x in saved], None if unreadable
{
    const QVariant v = settings_::settings()->value(u"cols/"_s + key);
    if (!v.isValid() || v.isNull())
        return std::nullopt;
    std::vector<int> out;
    for (const QVariant &x : v.toList()) {
        bool ok = false;
        const int n = x.toString().trimmed().toInt(&ok);
        if (!ok)
            return std::nullopt;
        out.push_back(n);
    }
    if (out.empty())
        return std::nullopt;
    return out;
}

void store(const QString &key, QHeaderView *hd, int from, int to)
{
    QVariantList l;
    for (int c = from; c < to; ++c)
        l << hd->sectionSize(c);
    settings_::settings()->setValue(u"cols/"_s + key, l);
}

}  // namespace

std::function<void()> make_resizable(QTreeWidget *tree, const QString &key, const std::vector<std::optional<int>> &defaults,
                                     bool stretch_first)
{
    QHeaderView *hd = tree->header();
    const int n = tree->columnCount();
    if (stretch_first) {
        hd->setSectionsMovable(false);
        hd->setStretchLastSection(false);
        hd->setMinimumSectionSize(36);
        hd->setSectionResizeMode(0, QHeaderView::Stretch);
        std::vector<std::optional<int>> widths;
        if (auto s = saved_widths(key); s && int(s->size()) == n - 1)
            widths.assign(s->begin(), s->end());
        else
            for (int c = 1; c < n && c < int(defaults.size()); ++c)   // columns 1 … n-1
                widths.push_back(defaults[size_t(c)]);
        for (int c = 1; c < n; ++c) {
            hd->setSectionResizeMode(c, QHeaderView::Interactive);
            const size_t i = size_t(c - 1);
            hd->resizeSection(c, i < widths.size() && widths[i] && *widths[i] ? *widths[i] : 90);
        }
        auto *timer = new QTimer(tree);
        timer->setSingleShot(true);
        timer->setInterval(400);
        QObject::connect(timer, &QTimer::timeout, tree, [key, hd, n] { store(key, hd, 1, n); });
        QObject::connect(hd, &QHeaderView::sectionResized, tree, [timer](int c, int, int) {
            if (c > 0)
                timer->start();
        });
        return {};
    }
    hd->setSectionsMovable(false);
    hd->setStretchLastSection(true);
    hd->setMinimumSectionSize(36);
    for (int c = 0; c < n; ++c)
        hd->setSectionResizeMode(c, QHeaderView::Interactive);
    const std::optional<std::vector<int>> saved = saved_widths(key);
    auto busy = std::make_shared<bool>(false);

    // The fixed columns never take the last one's room (it keeps LAST_MIN): the one just dragged gives way.
    auto clamp = [tree, hd, n, busy](int col) {
        if (*busy)
            return;
        const int vw = tree->viewport()->width();
        int tot = 0;
        for (int c = 0; c < n - 1; ++c)
            tot += hd->sectionSize(c);
        int over = tot - (vw - LAST_MIN);
        if (over > 0) {
            // the one just dragged gives way first, then the widest others — never past the list's edge
            *busy = true;
            std::vector<int> others;
            for (int c = 0; c < n - 1; ++c)
                if (c != col)
                    others.push_back(c);
            std::stable_sort(others.begin(), others.end(),
                             [hd](int a, int b) { return -hd->sectionSize(a) < -hd->sectionSize(b); });
            std::vector<int> order{col < n - 1 ? col : 0};
            order.insert(order.end(), others.begin(), others.end());
            for (int c : order) {
                if (over <= 0)
                    break;
                const int take = std::min(over, hd->sectionSize(c) - 40);
                if (take > 0) {
                    hd->resizeSection(c, hd->sectionSize(c) - take);
                    over -= take;
                }
            }
            *busy = false;
        }
    };
    auto apply = std::make_shared<std::function<void()>>();
    *apply = [tree, hd, n, saved, defaults, busy, clamp, weak = std::weak_ptr(apply)] {
        const int vw = tree->viewport()->width();
        if (vw < 100) {
            if (auto a = weak.lock())
                QTimer::singleShot(200, tree, [a] { (*a)(); });
            return;
        }
        std::vector<std::optional<int>> widths;
        if (saved && int(saved->size()) == n - 1)
            widths.assign(saved->begin(), saved->end());
        else
            widths = defaults;
        int rest = 0;
        for (size_t i = 1; i < widths.size(); ++i)
            if (widths[i] && *widths[i])
                rest += *widths[i];
        if (!widths.empty() && !widths[0])
            widths[0] = std::max(120, vw - rest - LAST_MIN - 20);
        *busy = true;
        for (size_t c = 0; c < widths.size(); ++c)
            hd->resizeSection(int(c), widths[c].value_or(0));
        *busy = false;
        clamp(0);
    };
    auto *timer = new QTimer(tree);
    timer->setSingleShot(true);
    timer->setInterval(400);
    QObject::connect(timer, &QTimer::timeout, tree, [key, hd, n] { store(key, hd, 0, n - 1); });
    QObject::connect(hd, &QHeaderView::sectionResized, tree, [busy, n, clamp, timer](int col, int, int) {
        if (*busy || col >= n - 1)
            return;
        clamp(col);
        timer->start();
    });
    tree->viewport()->installEventFilter(new Watch(clamp, tree));
    // (the apply function lives as long as the list: kept by the timer's connection below and by the caller)
    QObject::connect(tree, &QObject::destroyed, [apply] {});
    QTimer::singleShot(0, tree, [apply] { (*apply)(); });
    return [apply] { (*apply)(); };
}

}  // namespace colsizes
