// culturewin.py — Population → Edit cultures…: ONE window for common/cultures.txt — the cultures and their groups (drag
// between groups, new ones, names) and, behind a switch, a group's own lines (the nation it can unite into, leader
// pictures, unit graphics). Before: two windows for the same file.
#pragma once
#include <QDialog>
#include <QString>
#include <vector>

class QPushButton;
class QStackedWidget;
namespace dialogs { class CultureManager; }
namespace document { class Document; }
namespace entryview { class EntryView; }
namespace moddata { class Mod; }
namespace mainwindow { class MainWindow; }

namespace culturewin {

class CulturesWindow : public QDialog {
    Q_OBJECT
public:
    CulturesWindow(mainwindow::MainWindow *win, moddata::Mod &mod, document::Document &doc, const QString &backup_root,
                   int mode = 0);
    mainwindow::MainWindow *win;
    document::Document &doc;
    std::vector<QPushButton *> btns;
    QStackedWidget *stack;
    dialogs::CultureManager *mgr;
    entryview::EntryView *groups;
    void set_mode(int n);
    void reject() override;                          // Esc / ✕: changes not taken over yet are asked about there
};

}  // namespace culturewin
