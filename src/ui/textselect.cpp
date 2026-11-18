// textselect.py — line by line.
#include "ui/textselect.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QTabBar>

namespace textselect {

const char *const TAKES_MOUSE = "takesMouse";

static bool skip(const QWidget *w)
{
    return qobject_cast<const QAbstractButton *>(w) || qobject_cast<const QAbstractItemView *>(w) ||
           qobject_cast<const QMenu *>(w) || qobject_cast<const QComboBox *>(w) ||
           qobject_cast<const QAbstractSpinBox *>(w) || qobject_cast<const QLineEdit *>(w) ||
           qobject_cast<const QTabBar *>(w);
}
static bool takes_mouse(const QWidget *w) { return w->property(TAKES_MOUSE).toBool(); }

bool selectable_ok(const QLabel *lab)
{
    if (lab->objectName() == QLatin1String("qtooltip_label") || lab->property("noSelect").toBool())
        return false;
    if (!lab->pixmap().isNull() && lab->text().isEmpty())
        return false;
    if (takes_mouse(lab))
        return false;
    for (const QWidget *w = lab->parentWidget(); w; w = w->parentWidget()) {
        if (skip(w) || takes_mouse(w))
            return false;
        if (w->isWindow())
            break;
    }
    return true;
}

namespace {
class Selectable : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (ev->type() == QEvent::Polish || ev->type() == QEvent::Show) {
            auto *lab = qobject_cast<QLabel *>(obj);
            if (lab && !lab->property("_selDone").toBool()) {
                lab->setProperty("_selDone", true);
                if (selectable_ok(lab)) {
                    lab->setTextInteractionFlags(lab->textInteractionFlags() | Qt::TextSelectableByMouse |
                                                 Qt::LinksAccessibleByMouse);
                    if (lab->focusPolicy() == Qt::NoFocus)
                        lab->setFocusPolicy(Qt::ClickFocus);   // (Ctrl+C after marking)
                }
            }
        }
        return false;
    }
};
Selectable *the_filter = nullptr;
}  // namespace

void install(QApplication &app)
{
    if (!the_filter) {
        the_filter = new Selectable(&app);
        app.installEventFilter(the_filter);
    }
}

}  // namespace textselect
