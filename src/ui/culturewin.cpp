// culturewin.py — Population → Edit cultures… (see culturewin.h).
#include "ui/culturewin.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "ui/dialogs.h"
#include "ui/entryview.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace culturewin {

CulturesWindow::CulturesWindow(mainwindow::MainWindow *win_, moddata::Mod &mod, document::Document &doc_,
                               const QString &backup_root, int mode)
    : QDialog(win_), win(win_), doc(doc_)
{
    static const std::pair<QString, QString> MODES[] = {
        {u"Cultures && groups"_s, u"Move cultures between groups, make new cultures and groups, name them"_s},
        {u"Group settings"_s,
         u"A group's own lines: the nation it can unite into (union), its leader pictures and unit graphics"_s}};
    setWindowTitle(u"Cultures — "_s + mod.name());
    resize(1320, 900);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 8, 0, 0);
    lay->setSpacing(6);
    auto *bar = new QHBoxLayout();
    bar->setContentsMargins(12, 0, 12, 0);
    bar->setSpacing(0);
    auto *grp = new QButtonGroup(this);
    for (int i = 0; i < 2; ++i) {
        auto *b = new QPushButton(MODES[i].first);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setToolTip(MODES[i].second);
        connect(b, &QPushButton::clicked, this, [this, i] { set_mode(i); });
        grp->addButton(b);
        bar->addWidget(b);
        btns.push_back(b);
    }
    bar->addStretch(1);
    lay->addLayout(bar);
    stack = new QStackedWidget();
    lay->addWidget(stack, 1);
    mgr = new dialogs::CultureManager(mod, doc, backup_root, this);
    mgr->setWindowFlags(Qt::Widget);
    connect(mgr, &QDialog::finished, this, [this](int) { accept(); });   // its Close button closes this window
    stack->addWidget(mgr);
    groups = new entryview::EntryView(mod, entryview::CULTURE_GROUPS);
    connect(groups, &entryview::EntryView::scriptEdit, win,
            [this](const QString &lb, const QString &rel, const QString &text) { win->on_script_text(lb, rel, text); });
    connect(groups, &entryview::EntryView::nameEdit, win,
            [this](const QString &key, const QString &text) { win->on_name(key, text, std::nullopt); });
    connect(groups, &entryview::EntryView::fileRemove, win,
            [this](const QString &lb, const QString &rel) { win->on_script_remove(lb, rel); });
    connect(groups, &entryview::EntryView::bigRequested, win,
            [this](const QString &rel, const QStringList &path, const QString &noun) { win->open_big(rel, path, noun); });
    stack->addWidget(groups);
    set_mode(mode);
}

void CulturesWindow::set_mode(int n)
{
    btns[size_t(n)]->setChecked(true);
    stack->setCurrentIndex(n);
    if (n == 1) {
        groups->refresh(win->doc.get());
    } else if (!mgr->pending()) {                  // (the group settings may have changed the file meanwhile)
        mgr->_reset_pending();
        mgr->rebuild();
    }
}

void CulturesWindow::reject()
{
    if (mgr->pending() && mgr->_armed != u"close"_s) {
        set_mode(0);
        mgr->close_dialog();
        return;
    }
    QDialog::reject();
}

}  // namespace culturewin
