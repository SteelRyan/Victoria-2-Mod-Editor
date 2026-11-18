// checksumpage — see checksumpage.h
#include "ui/checksumpage.h"

#include "core/moddata.h"
#include "map/checksum.h"
#include "ui/submodpage.h"
#include "ui/theme.h"

#include <QLabel>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace checksumpage {

ChecksumWorkspace::ChecksumWorkspace(submodpage::SubmodWorkspace &sub_, QWidget *parent) : QWidget(parent), sub(sub_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(40, 32, 40, 32);
    lay->setSpacing(10);
    what = new QLabel();
    what->setStyleSheet(u"font-size:15px; color:%1;"_s.arg(theme::INK_DIM));
    what->setWordWrap(true);
    lay->addWidget(what);
    first = new QLabel();
    first->setStyleSheet(u"font-size:15px; color:%1;"_s.arg(theme::INK_DIM));
    first->setWordWrap(true);
    lay->addWidget(first);
    lay->addSpacing(18);
    auto *head = new QLabel(u"Checksum"_s);
    head->setStyleSheet(u"font-size:22px; font-weight:700; color:%1;"_s.arg(theme::INK));
    lay->addWidget(head);
    big = new QLabel();
    big->setStyleSheet(u"font-family:%1; font-size:96px; font-weight:800; color:%2; letter-spacing:6px;"_s.arg(
        theme::MONO, theme::GOLD));
    big->setWordWrap(true);
    lay->addWidget(big);
    lay->addStretch(1);
}

void ChecksumWorkspace::refresh()
{
    // the mod as opened: a mod alone, or a submod with its parents (what the players tick in the launcher)
    const auto sets = sub.ck_sets();
    const size_t i = sub.mod.submod ? 1 : 0;
    if (sets.size() <= i) {
        what->setText(u"The mods of this chain were not found in the game's mod folder."_s);
        first->clear();
        big->setText(u"—"_s);
        return;
    }
    const checksum::Prediction p = sub._code_of(sets[i].second);
    QStringList names;
    for (const auto &e : sets[i].second)
        names << std::get<0>(e);
    what->setText(names.join(u" + "_s));
    if (!p.ok) {
        first->setText(u"<span style='color:%1'>%2</span>"_s.arg(theme::WARN, p.why.toHtmlEscaped()));
        big->setText(u"—"_s);
        return;
    }
    first->setText(u"First start (no settings folder in Documents yet): <b style='font-family:%1'>%2</b>"_s.arg(
        theme::MONO, checksum::letters(p.fresh)));
    big->setText(checksum::letters(p.steady));
}

}  // namespace checksumpage
