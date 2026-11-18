// politicspanel.py — the Politics tab (see politicspanel.h).
#include "ui/politicspanel.h"
#include "ui/defpanel.h"
#include "ui/entryview.h"
#include "ui/issuepanel.h"
#include "ui/widgets.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;
using widgets::BadgeButton;
using widgets::GridFlow;
using widgets::label;
using widgets::Section;

namespace politicspanel {

const std::vector<std::pair<QString, QString>> PAGES = {{u"reforms"_s, u"Reforms && party issues"_s},
                                                        {u"unciv"_s, u"Unciv reforms"_s},
                                                        {u"governments"_s, u"Government forms"_s},
                                                        {u"ideologies"_s, u"Ideologies"_s}};

PoliticsWorkspace::PoliticsWorkspace(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *bar = new QWidget();
    bar->setObjectName(u"side"_s);
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(16, 10, 16, 8);
    bl->setSpacing(0);
    auto *grp = new QButtonGroup(this);
    for (const auto &[key, text] : PAGES) {
        auto *b = new BadgeButton(text);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(key == page);
        connect(b, &QPushButton::clicked, this, [this, k = key] { set_page(k); });
        grp->addButton(b);
        bl->addWidget(b, 1);
        seg[key] = b;
    }
    lay->addWidget(bar);
    stack = new QStackedWidget();
    lay->addWidget(stack, 1);
    auto *reforms = new issuepanel::IssueView(
        mod, {u"political_reforms"_s, u"social_reforms"_s, u"party_issues"_s},
        u"The political and social reforms and the party issues (common/issues.txt): what each option does, for every "
        u"nation. Which one a nation starts with is set on its page on the Nation page."_s,
        true);
    auto *unciv = new issuepanel::IssueView(
        mod, {u"economic_reforms"_s, u"military_reforms"_s},
        u"The unciv reforms (common/issues.txt): an uncivilised nation buys them with research points "
        u"(technology_cost); each gives it technologies or other effects. Change what they cost and what they do."_s,
        true);
    auto *govs = new defpanel::DefView(mod, defpanel::GOVERNMENTS);
    auto *ideos = new entryview::EntryView(mod, entryview::IDEOLOGIES);
    views.insert(u"reforms"_s, reforms);
    views.insert(u"unciv"_s, unciv);
    views.insert(u"governments"_s, govs);
    views.insert(u"ideologies"_s, ideos);
    for (issuepanel::IssueView *v : {reforms, unciv}) {
        connect(v, &issuepanel::IssueView::scriptEdit, this, &PoliticsWorkspace::scriptEdit);
        connect(v, &issuepanel::IssueView::nameEdit, this, &PoliticsWorkspace::nameEdit);
    }
    connect(govs, &defpanel::DefView::scriptEdit, this, &PoliticsWorkspace::scriptEdit);
    connect(govs, &defpanel::DefView::nameEdit, this, &PoliticsWorkspace::nameEdit);
    connect(ideos, &entryview::EntryView::scriptEdit, this, &PoliticsWorkspace::scriptEdit);
    connect(ideos, &entryview::EntryView::nameEdit, this, &PoliticsWorkspace::nameEdit);
    connect(ideos, &entryview::EntryView::fileRemove, this, &PoliticsWorkspace::fileRemove);
    connect(ideos, &entryview::EntryView::bigRequested, this, &PoliticsWorkspace::bigRequested);
    for (const auto &[key, _t] : PAGES)
        stack->addWidget(*views.get(key));
}

void PoliticsWorkspace::set_counts(const std::map<QString, std::pair<long long, long long>> &counts)
{
    for (const auto &[k, b] : seg) {
        const auto it = counts.find(k);
        const auto [e, w] = it != counts.end() ? it->second : std::pair<long long, long long>{0, 0};
        b->set_counts(e, w);
    }
}

void PoliticsWorkspace::_refresh_view(const QString &key)
{
    QWidget *v = *views.get(key);
    if (auto *iv = qobject_cast<issuepanel::IssueView *>(v))
        iv->refresh(doc);
    else if (auto *dv = qobject_cast<defpanel::DefView *>(v))
        dv->refresh(doc);
    else if (auto *ev = qobject_cast<entryview::EntryView *>(v))
        ev->refresh(doc);
}

void PoliticsWorkspace::set_page(const QString &key)
{
    page = key;
    for (const auto &[k, b] : seg)
        b->setChecked(k == key);
    stack->setCurrentWidget(*views.get(key));
    if (doc)
        _refresh_view(key);
}

void PoliticsWorkspace::refresh(document::Document *doc_)
{
    doc = doc_;
    _refresh_view(page);
}

}  // namespace politicspanel
