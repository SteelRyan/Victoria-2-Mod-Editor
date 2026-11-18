// theme.py — line by line (the style sheet's text is generated from Python's: tools/gen_theme.py).
#include "ui/theme.h"
#include "ui/textselect.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QWidget>
#include <cmath>

using namespace Qt::StringLiterals;

namespace theme {

const QString BG = u"#12151a"_s;
const QString PANEL = u"#181c23"_s;
const QString PANEL_ALT = u"#1e232b"_s;
const QString LINE = u"#2a303a"_s;
const QString LINE_SOFT = u"#232830"_s;
const QString INK = u"#e7e3d8"_s;
const QString INK_DIM = u"#9aa1ad"_s;
const QString INK_FAINT = u"#7d8591"_s;      // (was #5c636e: below a readable contrast on the panels — the user's rule)
const QString GOLD = u"#c9a15a"_s;
const QString GOLD_DIM = u"#8a7346"_s;
const QString DANGER = u"#b8654f"_s;
const QString OK = u"#7a9b76"_s;
const QString ERROR_ = u"#ff4d4d"_s;         // bright red: problems the user has to see (no pop-ups)
const QString WARN = u"#f0aa3c"_s;           // amber: warnings (red is for errors only)
const QString MONO = u"Consolas"_s;

#include "ui/theme_qss.inc"

static double lum(int r, int g, int b)
{
    auto c = [](int v8) {
        const double v = v8 / 255.0;
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * c(r) + 0.7152 * c(g) + 0.0722 * c(b);
}

QString ink_on(int r, int g, int b)
{
    const double L = lum(r, g, b);
    return (L + 0.05) / 0.05 >= 1.05 / (L + 0.05) ? u"#111111"_s : u"#f4f4f4"_s;
}

void apply(QApplication &app)
{
    app.setStyle(u"Fusion"_s);
    QPalette pal;
    const std::pair<QPalette::ColorRole, QString> roles[] = {
        {QPalette::Window, BG},           {QPalette::WindowText, INK},     {QPalette::Base, PANEL_ALT},
        {QPalette::AlternateBase, PANEL}, {QPalette::Text, INK},           {QPalette::Button, PANEL_ALT},
        {QPalette::ButtonText, INK},      {QPalette::ToolTipBase, PANEL_ALT}, {QPalette::ToolTipText, INK},
        {QPalette::Highlight, GOLD_DIM},  {QPalette::HighlightedText, INK}, {QPalette::PlaceholderText, INK_FAINT},
        {QPalette::BrightText, u"#ffffff"_s}, {QPalette::Light, LINE},     {QPalette::Midlight, LINE},
        {QPalette::Mid, LINE_SOFT},       {QPalette::Dark, BG},            {QPalette::Shadow, u"#000000"_s},
        {QPalette::Link, GOLD}};
    for (const auto &[role, col] : roles)
        pal.setColor(role, QColor(col));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        pal.setColor(QPalette::Disabled, role, QColor(INK_FAINT));
    app.setPalette(pal);
    app.setStyleSheet(QSS);
    textselect::install(app);                 // every text can be marked and copied (the user, 2026-10-16)
}

void no_hscroll(QWidget *root)
{
    for (QAbstractScrollArea *w : root->findChildren<QAbstractScrollArea *>())
        w->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

}  // namespace theme
