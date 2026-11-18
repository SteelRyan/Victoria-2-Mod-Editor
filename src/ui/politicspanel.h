// politicspanel.py — Politics tab: the politics of the mod, for all nations — the reforms (political and social, with
// the party issues), the unciv reforms on their own page, the government forms and the ideologies. Which of them a
// nation starts with is set on the Nation page. (National values, national focuses and rebels: Game rules. Culture
// groups: Population.)
#pragma once
#include "core/document.h"
#include "core/moddata.h"
#include "core/ordered_map.h"

#include <QString>
#include <QStringList>
#include <QWidget>
#include <map>
#include <utility>
#include <vector>

class QStackedWidget;
namespace widgets {
class BadgeButton;
}

namespace politicspanel {

extern const std::vector<std::pair<QString, QString>> PAGES;

class PoliticsWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit PoliticsWorkspace(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    QString page = QStringLiteral("reforms");
    std::map<QString, widgets::BadgeButton *> seg;
    QStackedWidget *stack;
    OrderedMap<QString, QWidget *> views;
    void set_counts(const std::map<QString, std::pair<long long, long long>> &counts);   // {page: (errors, warnings)}
    void set_page(const QString &key);
    void refresh(document::Document *doc);
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);   // undo label, rel file, new text
    void nameEdit(const QString &key, const QString &text);                           // localisation key, text
    void fileRemove(const QString &label, const QString &rel);
    void bigRequested(const QString &rel, const QStringList &path, const QString &noun);

private:
    void _refresh_view(const QString &key);
};

}  // namespace politicspanel
