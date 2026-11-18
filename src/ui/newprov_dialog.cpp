// newprov_dialog.h
#include "ui/newprov_dialog.h"
#include "core/moddata.h"
#include "ui/widgets.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace newprov {

NewProvinceDialog::NewProvinceDialog(const moddata::Mod &mod_, long long pid, const std::tuple<int, int, int> &rgb,
                                     const QString &where, std::optional<long long> tmpl, QWidget *parent)
    : QDialog(parent), mod(mod_)
{
    setWindowTitle(u"New province"_s);
    setMinimumWidth(460);
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(widgets::label(u"New province #%1"_s.arg(pid), u"title"_s));
    const auto [r, g, b] = rgb;
    lay->addWidget(widgets::label(u"Colour %1;%2;%3 (%4). It is added to definition.csv, default.map and the "
                                  u"localisation on Apply; paint it onto the map afterwards."_s.arg(r).arg(g).arg(b).arg(where),
                                  u"faint"_s));
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);
    grid->addWidget(widgets::label(u"Name"_s, u"sub"_s, false), 0, 0);
    name = new QLineEdit();
    name->setPlaceholderText(u"Name shown in the game"_s);
    grid->addWidget(name, 0, 1);
    grid->addWidget(widgets::label(u"Kind"_s, u"sub"_s, false), 1, 0);
    auto *row = new QHBoxLayout();
    row->setSpacing(0);
    kind_group = new QButtonGroup(this);
    for (const auto &[key, text] : {std::pair{u"land"_s, u"Land province"_s}, std::pair{u"sea"_s, u"Sea tile"_s}}) {
        auto *btn = new QPushButton(text);
        btn->setObjectName(u"seg"_s);
        btn->setCheckable(true);
        btn->setChecked(key == u"land");
        connect(btn, &QPushButton::toggled, this, [this](bool) { _update(); });
        kind_group->addButton(btn);
        kind_btns[key] = btn;
        row->addWidget(btn, 1);
    }
    grid->addLayout(row, 1, 1);
    tpl_lbl = widgets::label(u"Copy from"_s, u"sub"_s, false);
    grid->addWidget(tpl_lbl, 2, 0);
    auto *trow = new QHBoxLayout();
    tpl = new QSpinBox();
    tpl->setRange(0, 65535);
    tpl->setSpecialValueText(u"—"_s);
    tpl->setButtonSymbols(QSpinBox::NoButtons);
    tpl->setFixedWidth(80);
    tpl->setValue(int(tmpl.value_or(0)));
    connect(tpl, &QSpinBox::valueChanged, this, [this](int) { _update(); });
    tpl_name = widgets::label(QString(), u"faint"_s, false);
    tpl_name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    trow->addWidget(tpl);
    trow->addWidget(tpl_name, 1);
    grid->addLayout(trow, 2, 1);
    grid->setColumnStretch(1, 1);
    lay->addLayout(grid);
    note = widgets::label(QString(), u"sub"_s);
    lay->addWidget(note);
    warn = widgets::label(QString(), u"warn"_s);
    lay->addWidget(warn);
    buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(u"Create"_s);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);
    connect(name, &QLineEdit::textChanged, this, [this](const QString &) { _update(); });
    _update();
    name->setFocus();
}

bool NewProvinceDialog::is_sea() const
{
    return kind_btns.at(u"sea"_s)->isChecked();
}

NewProvinceDialog::Values NewProvinceDialog::values() const
{
    return {name->text().trimmed(), is_sea(), tpl->value() ? std::optional<long long>(tpl->value()) : std::nullopt};
}

void NewProvinceDialog::_update()
{
    const bool sea = is_sea();
    for (QWidget *w : {(QWidget *)tpl_lbl, (QWidget *)tpl, (QWidget *)tpl_name})
        w->setVisible(!sea);
    QString problem;
    const QString nm = name->text().trimmed();
    if (nm.isEmpty()) {
        problem = u"Enter a name."_s;
    } else {
        for (QChar c : nm)                           // (name.encode(ENC): latin-1)
            if (c.unicode() > 0xFF) {
                problem = u"The name has characters the game can't show (use Western European letters)."_s;
                break;
            }
        if (nm.contains(u';'))
            problem = u"The name can't contain ';'."_s;
    }
    if (sea) {
        note->setText(u"A sea tile: added to sea_starts in default.map. It gets no history, state, continent or "
                      u"climate. After painting, Finish adds its positions and coast crossings."_s);
    } else {
        const moddata::Province *pr = mod.provinces.get(tpl->value());
        if (!pr || pr->sea) {
            tpl_name->setText(u"enter a land province id"_s);
            if (problem.isEmpty())
                problem = u"Pick a land province to copy from (select one on the map before, or type its id)."_s;
            note->setText(QString());
        } else {
            tpl_name->setText(pr->name);
            note->setText(u"Copies the history of #%1 %2 (owner, cores, RGO, life rating, flags and its dated "
                          u"changes, without buildings) and puts the new province in the same state (%3), "
                          u"continent and climate. After painting, Finish places its positions, adds coast "
                          u"crossings and can move a share of the pops along."_s.arg(pr->id).arg(
                              pr->name, pr->region && !pr->region->isEmpty() ? *pr->region : u"no state"_s));
        }
    }
    warn->setText(problem);
    warn->setVisible(!problem.isEmpty());
    buttons->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
}

}  // namespace newprov
