// undokeys — see undokeys.h.
#include "ui/undokeys.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>

namespace undokeys {

static const char *MARK = "_undokeys_mark";          // the text when the cursor came in / Enter was last pressed
static const char *LOCAL_REDO = "_undokeys_redo";    // a Ctrl+Z went to the box itself: its Ctrl+Y may too

static bool is_undo(const QKeyEvent *k) { return k->matches(QKeySequence::Undo); }
static bool is_redo(const QKeyEvent *k)
{
    return k->matches(QKeySequence::Redo) || (k->key() == Qt::Key_Y && k->modifiers() == Qt::ControlModifier);
}

// The text box a widget is (or holds), as the object that keeps the mark; nullptr: none (or read-only)
struct Box {
    QObject *holder = nullptr;                       // where the mark is kept
    QLineEdit *line = nullptr;
    QPlainTextEdit *plain = nullptr;
    QTextEdit *rich = nullptr;
    QString text() const
    {
        return line ? line->text() : plain ? plain->toPlainText() : rich ? rich->toPlainText() : QString();
    }
    bool can_undo() const
    {
        return line ? line->isUndoAvailable() : plain ? plain->document()->isUndoAvailable()
                                                      : rich && rich->document()->isUndoAvailable();
    }
    bool can_redo() const
    {
        return line ? line->isRedoAvailable() : plain ? plain->document()->isRedoAvailable()
                                                      : rich && rich->document()->isRedoAvailable();
    }
};

static Box box_of(QObject *obj)
{
    Box b;
    if (!obj || obj->inherits("scriptedit::ScriptEditor"))   // (its own keys: the document's undo already)
        return b;
    if (auto *le = qobject_cast<QLineEdit *>(obj)) {
        if (!le->isReadOnly()) {
            b.line = le;
            // a number box's / list box's own line: the mark sits on the box the focus is on
            b.holder = le;
        }
        return b;
    }
    if (auto *sb = qobject_cast<QAbstractSpinBox *>(obj)) {
        if (!sb->isReadOnly())
            if (auto *le = sb->findChild<QLineEdit *>()) {
                b.line = le;
                b.holder = sb;
            }
        return b;
    }
    if (auto *cb = qobject_cast<QComboBox *>(obj)) {
        if (cb->isEditable() && cb->lineEdit()) {
            b.line = cb->lineEdit();
            b.holder = cb;
        }
        return b;
    }
    if (auto *pe = qobject_cast<QPlainTextEdit *>(obj)) {
        if (!pe->isReadOnly()) {
            b.plain = pe;
            b.holder = pe;
        }
        return b;
    }
    if (auto *te = qobject_cast<QTextEdit *>(obj)) {
        if (!te->isReadOnly()) {
            b.rich = te;
            b.holder = te;
        }
        return b;
    }
    return b;
}

static void set_mark(const Box &b)
{
    b.holder->setProperty(MARK, b.text());
    b.holder->setProperty(LOCAL_REDO, false);
}

Router::Router(QObject *parent) : QObject(parent) {}

bool Router::eventFilter(QObject *obj, QEvent *e)
{
    switch (e->type()) {
    case QEvent::FocusIn: {
        const Box b = box_of(obj);
        if (b.holder)
            set_mark(b);
        break;
    }
    case QEvent::KeyPress: {
        auto *k = static_cast<QKeyEvent *>(e);
        if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
            const Box b = box_of(obj);
            if (b.holder && (b.line)) {              // Enter hands the typing over: the mark moves after it
                QPointer<QObject> h(b.holder);
                QTimer::singleShot(0, h, [h, obj = QPointer<QObject>(obj)] {
                    if (h && obj) {
                        const Box bb = box_of(obj);
                        if (bb.holder)
                            set_mark(bb);
                    }
                });
            }
        }
        break;
    }
    case QEvent::ShortcutOverride: {
        auto *k = static_cast<QKeyEvent *>(e);
        const bool u = is_undo(k), r = !u && is_redo(k);
        if (!u && !r)
            break;
        const Box b = box_of(obj);
        if (!b.holder)
            break;
        const QVariant mark = b.holder->property(MARK);
        if (u) {
            // typing not handed over yet: the box takes it back itself (it is the newest change)
            if (b.can_undo() && (!mark.isValid() || b.text() != mark.toString())) {
                b.holder->setProperty(LOCAL_REDO, true);
                break;
            }
        } else if (b.can_redo() && b.holder->property(LOCAL_REDO).toBool()) {
            break;                                   // (its own Ctrl+Z came last: its Ctrl+Y too)
        }
        b.holder->setProperty(LOCAL_REDO, false);
        e->ignore();                                 // not the box's: the window's shortcut takes the keys
        return true;
    }
    default:
        break;
    }
    return QObject::eventFilter(obj, e);
}

void install()
{
    static Router *r = nullptr;
    if (r || !qApp)
        return;
    r = new Router(qApp);
    qApp->installEventFilter(r);
}

}  // namespace undokeys
