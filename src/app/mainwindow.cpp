// mainwindow.py — line by line, in stages (see mainwindow.h).
#include "app/mainwindow.h"
#include "app/version.h"
#include "core/applyjournal.h"
#include "core/countryhist.h"
#include "core/fs.h"
#include "core/diplomacy.h"
#include "core/document.h"
#include "core/events.h"
#include "core/eventtools.h"
#include "core/history.h"
#include "core/localisation.h"
#include "core/provrenumber.h"
#include "core/mapfiles.h"
#include "core/maplook.h"
#include "core/mapmodes.h"
#include "core/modifiers.h"
#include "core/modrename.h"
#include "core/oob.h"
#include "core/positions.h"
#include "core/production.h"
#include "core/refs.h"
#include "core/popcheck.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pyre.h"
#include "core/pytext.h"
#include "core/recovery.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "core/settings.h"
#include "core/submods.h"
#include "ui/checksumpage.h"
#include "core/techdata.h"
#include "map/adjacency_geo.h"
#include "map/importer.h"
#include "map/layerbmp.h"
#include "map/mapview_data.h"
#include "map/moreviews_data.h"
#include "map/nationextras_data.h"
#include "map/newprov.h"
#include "map/mismatch.h"
#include "map/pospanel_data.h"
#include "map/provdelete.h"
#include "map/provbmp.h"
#include "map/provpanel_data.h"
#include "map/snapio_map.h"
#include "ui/adjpanel.h"
#include "ui/canalpanel.h"
#include "ui/cbpanel.h"
#include "ui/checkpanel.h"
#include "ui/colorwheel.h"
#include "ui/colormappanel.h"
#include "ui/eventpanel.h"
#include "ui/modpanel.h"
#include "ui/scriptedit.h"
#include "ui/scriptpage.h"
#include "ui/drawpanel.h"
#include "ui/mapdefs.h"
#include "ui/mapsize.h"
#include "ui/entryview.h"
#include "ui/defpanel.h"
#include "ui/iconboard.h"
#include "ui/iconstrips.h"
#include "ui/importpanel.h"
#include "ui/lookpages.h"
#include "ui/mapmarkspage.h"
#include "ui/shortcuts.h"
#include "ui/submodpage.h"
#include "ui/econest.h"
#include "ui/popinfluence.h"
#include "ui/culturewin.h"
#include "ui/politicspanel.h"
#include "ui/techpanel.h"
#include "ui/prodpanel.h"
#include "ui/moreviews.h"
#include "ui/unitpanel.h"
#include "ui/unittimeline.h"
#include "ui/mismatchbox.h"
#include "ui/maplook.h"
#include "ui/mapview.h"
#include "ui/modepanel.h"
#include "ui/nationextras.h"
#include "ui/nationpanel.h"
#include "ui/partieswin.h"
#include "ui/eventnet.h"
#include "ui/bigedit.h"
#include "ui/locwin.h"
#include "ui/nodegraph.h"
#include "ui/blockbuilder.h"
#include "ui/changelog.h"
#include "ui/reidwin.h"
#include "ui/armypanel.h"
#include "ui/leaderlist.h"
#include "ui/traitodds.h"
#include "ui/diplopanel.h"
#include "ui/diploweb.h"
#include "ui/flagstudio.h"
#include "ui/dialogs.h"
#include "ui/palette.h"
#include "ui/panels.h"
#include "ui/popfixbox.h"
#include "ui/provpanel.h"
#include "ui/rail.h"
#include "ui/stockpilepage.h"
#include "ui/pospanel.h"
#include "ui/riverpanel.h"
#include "ui/statepanel.h"
#include "ui/buildtypes.h"
#include "core/provbuild.h"
#include "ui/terrainpanel.h"
#include "ui/theme.h"
#include "ui/undokeys.h"
#include "ui/utilpanel.h"
#include "ui/widgets.h"
#include "map/mapcheck.h"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCompleter>
#include <QCryptographicHash>
#include <QCursor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QAction>
#include <QPainter>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iterator>
#include <set>
#include <tuple>

using namespace Qt::StringLiterals;

namespace mainwindow {

// tabs without the map: their workspace uses the whole window
static const QSet<QString> FULL_SCREEN = {u"technology"_s, u"production"_s, u"changelog"_s, u"politics"_s, u"warfare"_s,
                                          u"rules"_s, u"library"_s, u"import"_s, u"mapsize"_s, u"shortcuts"_s,
                                          u"submods"_s, u"checksum"_s, u"econest"_s, u"popinf"_s};
using Modes = std::vector<std::pair<QString, QString>>;
static const Modes OWNER_ONLY = {{u"political"_s, u"Owner"_s}};
const std::map<QString, Modes> COLOR_MODES_BY_TAB = {
    {u"pops"_s, {{u"political"_s, u"Owner"_s}, {u"states"_s, u"States"_s}, {u"statesize"_s, u"State size"_s},
                 {u"provsize"_s, u"Province size"_s}, {u"population"_s, u"Population"_s},
                 {u"culture"_s, u"Culture majority"_s}, {u"culture_mix"_s, u"Culture mix"_s},
                 {u"religion"_s, u"Religion majority"_s}, {u"religion_mix"_s, u"Religion mix"_s},
                 {u"filter"_s, u"Filtered pops"_s}}},
    {u"provinces"_s, {{u"political"_s, u"Owner"_s}, {u"controller"_s, u"Controller"_s}, {u"cores"_s, u"Cores"_s},
                      {u"liferating"_s, u"Life rating"_s}, {u"rgo"_s, u"RGO"_s}, {u"buildings"_s, u"Buildings (all)"_s},
                      {u"fort"_s, u"Forts"_s}, {u"naval_base"_s, u"Naval bases"_s}, {u"railroad"_s, u"Railroads"_s},
                      {u"factories"_s, u"Factories"_s}, {u"colonial"_s, u"Colonial status"_s},
                      {u"climate"_s, u"Climate"_s}, {u"continent"_s, u"Continent"_s}, {u"states"_s, u"States"_s},
                      {u"statesize"_s, u"State size"_s}, {u"provsize"_s, u"Province size"_s},
                      {u"mixedowners"_s, u"Mixed owners"_s}, {u"splitstates"_s, u"Split states"_s}}},
    {u"map"_s, {{u"plain"_s, u"Land / sea"_s}, {u"political"_s, u"Owner"_s}, {u"states"_s, u"States"_s},
                {u"statesize"_s, u"State size"_s}, {u"continent"_s, u"Continent"_s}, {u"climate"_s, u"Climate"_s}}},
    {u"nations"_s, {{u"political"_s, u"Owner"_s}, {u"states"_s, u"States"_s}, {u"statesize"_s, u"State size"_s}}},
    {u"armies"_s, {{u"military"_s, u"The picked nation (its units, others dimmed)"_s},
                   {u"soldiers"_s, u"Soldiers per state"_s}, {u"political"_s, u"Owner"_s}}},
    {u"diplomacy"_s, {{u"relvalues"_s, u"Relations with the main nation"_s},
                      {u"status"_s, u"What each nation is to the main nation"_s},
                      {u"spheres"_s, u"Spheres of influence"_s}, {u"subjects"_s, u"Puppets & substates"_s},
                      {u"alliances"_s, u"Alliances"_s}, {u"political"_s, u"Owner"_s}}},
    {u"technology"_s, OWNER_ONLY}, {u"production"_s, OWNER_ONLY},
    {u"events"_s, {{u"events"_s, u"Chance within 10 years"_s}, {u"political"_s, u"Owner"_s}}},
    {u"changelog"_s, OWNER_ONLY}, {u"politics"_s, OWNER_ONLY}, {u"warfare"_s, OWNER_ONLY}, {u"library"_s, OWNER_ONLY},
    {u"import"_s, OWNER_ONLY}, {u"mapsize"_s, OWNER_ONLY}, {u"shortcuts"_s, OWNER_ONLY}, {u"submods"_s, OWNER_ONLY}, {u"checksum"_s, OWNER_ONLY},
    {u"econest"_s, OWNER_ONLY}, {u"popinf"_s, OWNER_ONLY}, {u"rules"_s, OWNER_ONLY},
    {u"errors"_s, {{u"errors"_s, u"Errors — everything"_s}, {u"plain"_s, u"Land / sea"_s}, {u"political"_s, u"Owner"_s},
                   {u"states"_s, u"States"_s}, {u"continent"_s, u"Continent"_s}}},
};
const QStringList MAP_PAGES = {u"pops"_s, u"provinces"_s, u"map"_s, u"nations"_s, u"armies"_s, u"diplomacy"_s};
const QSet<QString> PAGE_BOUND_MODES = {u"errors"_s, u"filter"_s, u"events"_s, u"military"_s, u"status"_s, u"relvalues"_s};
static const std::map<QString, QString> NEEDS_NATION = {
    {u"military"_s, u"Armies: click a nation on the map — its units stand out, the others are dimmed"_s},
    {u"status"_s, u"Diplomacy: pick the main nation (click it on the map) — every other nation is then coloured by "
                  u"what it is to it: ally, puppet, in its sphere …"_s},
    {u"relvalues"_s, u"Diplomacy: pick the main nation (click it on the map) — every other nation is then coloured by "
                     u"its relations with it (red hostile … green friendly)"_s}};
const std::map<QString, QString> START_MODE = {{u"provinces"_s, u"political"_s}, {u"errors"_s, u"errors"_s},
                                               {u"events"_s, u"events"_s}};
// Errors tab double-click: where each kind of problem is fixed ("provinces" tab, or Drawing → layer)
const std::map<QString, QString> JUMP = {
    {u"event_ids"_s, u"evid"_s}, {u"script_prov"_s, u"script"_s}, {u"wars"_s, u"wars"_s}, {u"utility_suggest"_s, u"utility"_s},
    {u"nation_birth"_s, u"nations"_s}, {u"nations"_s, u"nations"_s}, {u"oob"_s, u"armies"_s},
    {u"diplo"_s, u"diplomacy"_s}, {u"names"_s, u"provinces"_s}, {u"pos_crash"_s, u"pos"_s}, {u"pos_naval"_s, u"pos"_s},
    {u"pos_place"_s, u"pos"_s}, {u"pos_missing"_s, u"pos"_s}, {u"zero_small"_s, u"prov"_s},
    {u"states_split"_s, u"provinces"_s}, {u"states_owners"_s, u"provinces"_s}, {u"adj_rivers"_s, u"adj"_s},
    {u"adj_missing"_s, u"adj"_s}, {u"adj_stale"_s, u"adj"_s}, {u"adj_dup"_s, u"adj"_s}, {u"adj_useless"_s, u"adj"_s},
    {u"adj_other"_s, u"adj"_s}, {u"adj_banks"_s, u"adj"_s}, {u"cb_broken"_s, u"script"_s},
    {u"ref_modifier"_s, u"script"_s}, {u"ref_cb"_s, u"script"_s}, {u"brackets"_s, u"script"_s},
    {u"equals"_s, u"script"_s}, {u"build_window"_s, u"buildtypes"_s}, {u"build_window_look"_s, u"buildtypes"_s}, {u"cb_lines"_s, u"cb"_s}, {u"cb_order"_s, u"cb"_s}, {u"cb_texts"_s, u"cb"_s},
    {u"rivers"_s, u"rivers"_s}, {u"landsea_rivers"_s, u"rivers"_s}, {u"landsea_terrain"_s, u"terrain"_s},
    {u"stray"_s, u"prov"_s}, {u"unknown"_s, u"prov"_s}, {u"offmap"_s, u"prov"_s}, {u"offmap_sea"_s, u"prov"_s},
    {u"south_edge"_s, u"prov"_s},
    {u"unused_rows"_s, u"prov"_s}, {u"tiny"_s, u"prov"_s}, {u"newprov"_s, u"prov"_s}, {u"parts"_s, u"prov"_s},
    {u"islands"_s, u"prov"_s}, {u"naval"_s, u"provinces"_s}, {u"factories"_s, u"provinces"_s},
    {u"states"_s, u"provinces"_s}, {u"continent"_s, u"provinces"_s}, {u"climate"_s, u"provinces"_s},
    {u"nohist"_s, u"provinces"_s}, {u"histsea"_s, u"provinces"_s}};
static const QSet<QString> PROVINCE_MODES = {u"political"_s, u"controller"_s, u"cores"_s, u"liferating"_s, u"rgo"_s,
                                             u"buildings"_s, u"fort"_s, u"naval_base"_s, u"railroad"_s, u"factories"_s,
                                             u"colonial"_s, u"mixedowners"_s, u"splitstates"_s, u"climate"_s,
                                             u"continent"_s};
const Rgb NO_PROVINCE_RGB{0, 0, 0};
const std::vector<Rgb> CLIMATE_COLORS = {{240, 210, 90}, {80, 160, 80},   {200, 120, 50}, {205, 205, 215},
                                         {90, 150, 220}, {200, 100, 170}, {120, 210, 200}, {150, 110, 70}};

QString script_page(const QString &rel)
{
    const QString r = rel.toLower();
    auto ends = [&r](std::initializer_list<const char16_t *> xs) {
        for (const char16_t *x : xs)
            if (r.endsWith(QString::fromUtf16(x)))
                return true;
        return false;
    };
    auto starts = [&r](std::initializer_list<const char16_t *> xs) {
        for (const char16_t *x : xs)
            if (r.startsWith(QString::fromUtf16(x)))
                return true;
        return false;
    };
    if (r.startsWith(u"units/"_s))
        return u"warfare"_s;
    if (r.endsWith(u"cb_types.txt"_s))
        return u"events"_s;
    if (ends({u"issues.txt", u"governments.txt", u"ideologies.txt"}))
        return u"politics"_s;
    if (r.startsWith(u"interface/"_s) && r.endsWith(u".gfx"_s))
        return u"library"_s;
    if (r.startsWith(u"interface/"_s) && r.endsWith(u".gui"_s) && !r.endsWith(u"country_technology.gui"_s))
        return u"shortcuts"_s;
    if (r.endsWith(u"common/technology.txt"_s) || starts({u"interface/", u"inventions/", u"technologies/"}))
        return u"technology"_s;
    if (ends({u"cultures.txt", u"religion.txt"}))
        return u"pops"_s;
    if (r.endsWith(u"country_colors.txt"_s))
        return u"warfare"_s;
    if (r.startsWith(u"news/"_s) || r.endsWith(u"on_actions.txt"_s))
        return u"rules"_s;
    if (ends({u"traits.txt", u"map/terrain.txt"}) || r.startsWith(u"history/wars/"_s))
        return u"warfare"_s;
    if (ends({u"defines.lua", u"static_modifiers.txt", u"nationalvalues.txt", u"national_focus.txt",
              u"rebel_types.txt", u"triggered_modifiers.txt", u"crime.txt", u"bookmarks.txt"}) ||
        r.startsWith(u"poptypes/"_s))
        return u"rules"_s;
    if (r.startsWith(u"common/countries/"_s))
        return u"nations"_s;
    return u"events"_s;
}

static const std::map<QString, std::vector<std::pair<QString, QString>>> SCRIPT_PARTS = {
    {u"events"_s, {{u"events/"_s, u"event"_s}, {u"decisions/"_s, u"decision"_s}, {u"event_modifiers.txt"_s, u"mods"_s},
                   {u"cb_types.txt"_s, u"cbs"_s}}},
    {u"politics"_s, {{u"ideologies.txt"_s, u"ideologies"_s}, {u"governments.txt"_s, u"governments"_s}}},
    {u"warfare"_s, {{u"units/"_s, u"units"_s}, {u"traits.txt"_s, u"traits"_s}, {u"map/terrain.txt"_s, u"terrain"_s},
                    {u"history/wars/"_s, u"wars"_s}, {u"country_colors.txt"_s, u"colours"_s}}},
    {u"rules"_s, {{u"defines.lua"_s, u"settings"_s}, {u"bookmarks.txt"_s, u"bookmarks"_s},
                  {u"poptypes/"_s, u"poptypes"_s}, {u"nationalvalues.txt"_s, u"values"_s},
                  {u"national_focus.txt"_s, u"focus"_s}, {u"rebel_types.txt"_s, u"rebels"_s},
                  {u"static_modifiers.txt"_s, u"static"_s}, {u"triggered_modifiers.txt"_s, u"triggered"_s},
                  {u"crime.txt"_s, u"crime"_s}, {u"news/"_s, u"news"_s}, {u"on_actions.txt"_s, u"timed"_s}}},
};

std::pair<QString, OptStr> script_place(const QString &rel)
{
    const QString page = script_page(rel);
    const QString r = rel.toLower();
    const auto it = SCRIPT_PARTS.find(page);
    if (it != SCRIPT_PARTS.end())
        for (const auto &[mark, part] : it->second)
            if (r.startsWith(mark) || r.endsWith(mark))
                return {page, part};
    return {page, std::nullopt};
}

QStringList mix_slots(const std::vector<std::pair<QString, double>> &shares_, int q, double min_share, int max_groups)
{
    // [(key, share)] -> q keys: the biggest groups (at most max_groups, each at least min_share) as many slots as their
    // share of the groups shown (largest first, contiguous) — smaller groups' shares are split among them in proportion
    std::vector<std::pair<QString, double>> shares;
    for (const auto &kv : shares_)
        if (kv.second > 0)
            shares.push_back(kv);
    std::stable_sort(shares.begin(), shares.end(), [](const auto &a, const auto &b) { return -a.second < -b.second; });
    if (shares.empty())
        return {};
    std::vector<std::pair<QString, double>> keep;
    for (const auto &kv : shares)
        if (kv.second >= min_share - 1e-9 && int(keep.size()) < max_groups)
            keep.push_back(kv);
    if (keep.empty())
        keep.push_back(shares[0]);
    std::vector<double> w;
    for (const auto &kv : keep)
        w.push_back(kv.second);
    std::vector<long long> n = document::distribute(q, w);
    for (size_t i = 0; i < n.size(); ++i)
        while (n[i] == 0) {                            // every group shown gets a stripe
            size_t j = 0;
            for (size_t x = 1; x < n.size(); ++x)
                if (n[x] > n[j])
                    j = x;
            n[j] -= 1;
            n[i] += 1;
        }
    QStringList out;
    for (size_t i = 0; i < keep.size(); ++i)
        for (long long c = 0; c < n[i]; ++c)
            out << keep[i].first;
    return out;
}

Rgb hashed_color(const QString &key, double s, double v)
{
    // (Python: md5 of the key → a hue, colorsys.hsv_to_rgb; remembered per key)
    static std::map<std::tuple<QString, double, double>, Rgb> cache;
    const auto ck = std::make_tuple(key, s, v);
    if (const auto it = cache.find(ck); it != cache.end())
        return it->second;
    const QByteArray hex = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5).toHex().left(6);
    const double h = double(hex.toLongLong(nullptr, 16)) / double(0xFFFFFF);
    double r, g, b;
    if (s == 0.0) {
        r = g = b = v;
    } else {
        long long i = static_cast<long long>(h * 6.0);
        const double f = (h * 6.0) - double(i);
        const double p = v * (1.0 - s), qq = v * (1.0 - s * f), t = v * (1.0 - s * (1.0 - f));
        i = i % 6;
        switch (i) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = qq, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = qq, b = v; break;
        case 4: r = t, g = p, b = v; break;
        default: r = v, g = p, b = qq; break;
        }
    }
    const Rgb out{int(r * 255), int(g * 255), int(b * 255)};
    cache[ck] = out;
    return out;
}

MainWindow::MainWindow(std::shared_ptr<moddata::Mod> mod_, std::shared_ptr<mapview::MapData> mapdata_,
                       const OptStr &scenario_, const QString &backup_root_)
    : mod(std::move(mod_)), mapdata(std::move(mapdata_)), backup_root(backup_root_), settings(settings_::settings()),
      scenario(scenario_)
{
    static bool registered = false;               // (the recovery file's picture types: once)
    if (!registered) {
        snapio::register_map();
        registered = true;
    }
    setWindowTitle(u"ModEditor " MODEDITOR_VERSION_TEXT " — %1"_s.arg(mod->name()));
    resize(1500, 900);

    auto *central = new QWidget();
    auto *outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(_build_toolbar());
    // a red bar across the window for what must not be missed (no pop-ups): a close held back, …
    notice = new QLabel(u""_s);
    notice->setWordWrap(true);
    notice->setAlignment(Qt::AlignCenter);
    notice->setStyleSheet(u"background: #b3261e; color: #ffffff; font-weight: 700; font-size: 14px; "
                          u"padding: 9px 16px;"_s);
    notice->hide();
    _notice_timer = new QTimer(this);
    _notice_timer->setSingleShot(true);
    connect(_notice_timer, &QTimer::timeout, notice, &QWidget::hide);
    outer->addWidget(notice);
    // unapplied edits from the last session (or kept over a re-read of the mod): offered back (recovery.py)
    rec_bar = new QWidget();
    rec_bar->setStyleSheet(u"QWidget#recBar { background: %1; border-bottom: 2px solid %2; }"_s.arg(theme::PANEL_ALT,
                                                                                                    theme::GOLD));
    rec_bar->setObjectName(u"recBar"_s);
    rec_bar->setAttribute(Qt::WA_StyledBackground, true);
    auto *rbl = new QHBoxLayout(rec_bar);
    rbl->setContentsMargins(14, 6, 14, 6);
    rec_lbl = new QLabel(u""_s);
    rec_lbl->setWordWrap(true);
    rec_lbl->setStyleSheet(u"color: %1; font-weight: 600;"_s.arg(theme::GOLD));
    rbl->addWidget(rec_lbl, 1);
    rec_restore_btn = new QPushButton(u"Restore them ▸"_s);
    rec_restore_btn->setObjectName(u"primary"_s);
    connect(rec_restore_btn, &QPushButton::clicked, this, [this] { _recovery_restore(); });
    rbl->addWidget(rec_restore_btn);
    rec_discard_btn = new QPushButton(u"Discard"_s);
    rec_discard_btn->setToolTip(u"Puts the recovery file aside (backups/recovery/…discarded-…pkl) — never "
                                u"deleted outright"_s);
    connect(rec_discard_btn, &QPushButton::clicked, this, [this] { _recovery_discard(); });
    rbl->addWidget(rec_discard_btn);
    rec_bar->hide();
    outer->addWidget(rec_bar);
    // files added to / taken from the mod while the editor runs (a copy put in by hand …): read them in
    scan_bar = new QWidget();
    scan_bar->setObjectName(u"scanBar"_s);
    scan_bar->setAttribute(Qt::WA_StyledBackground, true);
    scan_bar->setStyleSheet(u"QWidget#scanBar { background: %1; border-bottom: 2px solid %2; }"_s.arg(theme::PANEL_ALT,
                                                                                                      theme::WARN));
    auto *sbl = new QHBoxLayout(scan_bar);
    sbl->setContentsMargins(14, 6, 14, 6);
    scan_lbl = new QLabel(u""_s);
    scan_lbl->setWordWrap(true);
    scan_lbl->setStyleSheet(u"color: %1; font-weight: 600;"_s.arg(theme::WARN));
    sbl->addWidget(scan_lbl, 1);
    auto *b = new QPushButton(u"⟳ Read them in ▸"_s);
    b->setObjectName(u"primary"_s);
    connect(b, &QPushButton::clicked, this, [this] { rescan_mod(); });
    sbl->addWidget(b);
    b = new QPushButton(u"Not now"_s);
    connect(b, &QPushButton::clicked, this, [this] {
        _listing = _mod_listing();
        scan_bar->hide();
    });
    sbl->addWidget(b);
    scan_bar->hide();
    outer->addWidget(scan_bar);
    // Pops: a new total / mix waits for "Apply changes" while the page shows something else (C++ only, the user
    // 2026-11-13: "when you click away … it should warn you, hey, you haven't applied this change yet")
    pend_bar = new QWidget();
    pend_bar->setObjectName(u"pendBar"_s);
    pend_bar->setAttribute(Qt::WA_StyledBackground, true);
    pend_bar->setStyleSheet(u"QWidget#pendBar { background: %1; border-bottom: 2px solid %2; }"_s.arg(theme::PANEL_ALT,
                                                                                                      theme::WARN));
    {
        auto *l = new QHBoxLayout(pend_bar);
        l->setContentsMargins(14, 6, 14, 6);
        pend_lbl = new QLabel(u""_s);
        pend_lbl->setWordWrap(true);
        pend_lbl->setStyleSheet(u"color: %1; font-weight: 600;"_s.arg(theme::WARN));
        l->addWidget(pend_lbl, 1);
        auto *pb = new QPushButton(u"Apply changes ▸"_s);
        pb->setObjectName(u"primary"_s);
        connect(pb, &QPushButton::clicked, this, [this] { edit_panel->apply_pending(); });
        l->addWidget(pb);
        pb = new QPushButton(u"Discard"_s);
        pb->setToolTip(u"Throw away the new total / mix that was not applied"_s);
        connect(pb, &QPushButton::clicked, this, [this] { edit_panel->discard_pending(); });
        l->addWidget(pb);
        pb = new QPushButton(u"Go back to them"_s);
        pb->setToolTip(u"The Pops page with the same provinces and filter, the change shown again"_s);
        connect(pb, &QPushButton::clicked, this, [this] { _pend_go_back(); });
        l->addWidget(pb);
    }
    pend_bar->hide();
    outer->addWidget(pend_bar);
    _listing.reset();
    _scan_timer = new QTimer(this);
    _scan_timer->setInterval(8000);
    connect(_scan_timer, &QTimer::timeout, this, [this] { _scan_tick(); });
    _scan_timer->start();
    sub_strip = new QLabel(u""_s);                 // a submod is open: what that means, always in sight
    sub_strip->setWordWrap(true);
    sub_strip->setTextFormat(Qt::RichText);
    sub_strip->setStyleSheet(u"background: %1; color: %2; font-size: 13px; padding: 5px 14px; "
                             u"border-bottom: 1px solid %3;"_s.arg(theme::PANEL_ALT, theme::INK, theme::GOLD_DIM));
    sub_strip->setVisible(bool(mod->submod));
    outer->addWidget(sub_strip);
    prob_strip = new QWidget();
    prob_strip->setObjectName(u"probStrip"_s);
    prob_strip->setAttribute(Qt::WA_StyledBackground, true);
    auto *pl = new QHBoxLayout(prob_strip);
    pl->setContentsMargins(16, 6, 10, 6);
    pl->setSpacing(8);
    prob_text = new QLabel(u""_s);
    prob_text->setWordWrap(true);
    prob_text->setTextFormat(Qt::PlainText);
    pl->addWidget(prob_text, 1);
    for (const auto &[text, tip] : {std::pair{u"Next ▸"_s, u"The next problem of the same page"_s},
                                    {u"Fix it… (Errors)"_s, u"This problem in the Errors tab, where its fix buttons are"_s},
                                    {u"✕"_s, u"Close this line"_s}}) {
        auto *pb = new QPushButton(text);
        pb->setToolTip(tip);
        pb->setCursor(Qt::PointingHandCursor);
        if (text == u"Next ▸"_s)
            connect(pb, &QPushButton::clicked, this,
                    [this] { next_problem(_prob_cur ? OptStr(_prob_cur->page) : std::nullopt); });
        else if (text == u"Fix it… (Errors)"_s)
            connect(pb, &QPushButton::clicked, this, [this] { _problem_to_errors(); });
        else
            connect(pb, &QPushButton::clicked, prob_strip, &QWidget::hide);
        pl->addWidget(pb, 0, Qt::AlignTop);
    }
    prob_strip->hide();
    outer->addWidget(prob_strip);
    auto *body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(_build_tabs());                // the navigation rail (instead of a row of tabs)
    auto *content = new QWidget();
    auto *root = new QVBoxLayout(content);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    body->addWidget(content, 1);
    outer->addLayout(body, 1);

    auto *split = new QSplitter(Qt::Horizontal);
    split->setHandleWidth(1);
    map = new mapview::MapView();
    map->set_data(mapdata.get());
    connect(map, &mapview::MapView::provinceClicked, this, [this](int pid, Qt::KeyboardModifiers m) { on_click(pid, m); });
    connect(map, &mapview::MapView::provinceHovered, this, [this](int pid, int x, int y) { on_hover(pid, x, y); });
    connect(map, &mapview::MapView::provinceRightClicked, this,
            [this](int pid, Qt::KeyboardModifiers m) { on_right_click(pid, m); });
    connect(map, &mapview::MapView::legendToggled, this, [this](const QString &k) { _bld_toggle(k); });
    // the Events tab: the script editor in the middle, the map under it (unfolded with its 🗺 Map button)
    script_ws = new scriptedit::ScriptWorkspace();
    script_ws->hide();
    left_split = new QSplitter(Qt::Vertical);
    left_split->setHandleWidth(6);
    left_split->addWidget(script_ws);
    left_split->addWidget(map);
    left_split->setStretchFactor(0, 3);
    left_split->setStretchFactor(1, 2);
    split->addWidget(left_split);

    stack = new widgets::PageStack();
    QSet<long long> sea_ids;
    for (const auto &[p, pr] : mod->provinces)
        if (pr.sea)
            sea_ids.insert(p);
    geo = std::make_unique<adjacency::AdjGeometry>(*mapdata, sea_ids);
    auto sel_req = [this](const QList<long long> &land, const QList<long long> &sea) {
        set_selection(std::vector<long long>(land.begin(), land.end()), true,
                      std::vector<long long>(sea.begin(), sea.end()));
    };
    map_panel = new adjpanel::MapPanel(*mod, *geo);
    connect(map_panel, &adjpanel::MapPanel::overlayChanged, this, [this] { update_overlay(map_panel); });
    connect(map_panel, &adjpanel::MapPanel::pickRequested, this, [this](const QString &f) { start_adj_pick(f); });
    connect(map_panel, &adjpanel::MapPanel::selectRequested, this, sel_req);
    connect(map_panel, &adjpanel::MapPanel::zoomRequested, this, [this](double x, double y) { zoom_to_point(x, y); });
    connect(map_panel, &adjpanel::MapPanel::message, this, [this](const QString &m) { message(m, 8000); });
    connect(map_panel, &adjpanel::MapPanel::editModeChanged, this, [this](bool) { _sync_map_tool(); });
    map_panel->view = map;
    canal_panel = new canalpanel::CanalPanel(*mod, *mapdata, *geo);
    canal_panel->view = map;
    connect(canal_panel, &canalpanel::CanalPanel::overlayChanged, this, [this] { update_overlay(canal_panel); });
    connect(canal_panel, &canalpanel::CanalPanel::selectRequested, this, sel_req);
    connect(canal_panel, &canalpanel::CanalPanel::zoomRequested, this,
            [this](double x, double y) { zoom_to_point(x, y, 14.0); });   // (a model: ~20 px)
    connect(canal_panel, &canalpanel::CanalPanel::message, this, [this](const QString &m) { message(m, 8000); });
    connect(map_panel, &adjpanel::MapPanel::canalsRequested, this, [this] { set_map_sub(u"canal"_s); });
    pos_panel = new pospanel::PositionsPanel(*mod, *mapdata);
    pos_panel->view = map;
    connect(pos_panel, &pospanel::PositionsPanel::overlayChanged, this, [this] { update_overlay(pos_panel); });
    connect(pos_panel, &pospanel::PositionsPanel::selectRequested, this, sel_req);
    connect(pos_panel, &pospanel::PositionsPanel::zoomRequested, this, [this](double x, double y) { zoom_to_point(x, y); });
    connect(pos_panel, &pospanel::PositionsPanel::message, this, [this](const QString &m) { message(m, 8000); });
    connect(pos_panel, &pospanel::PositionsPanel::editModeChanged, this, [this](bool) { _sync_map_tool(); });
    chk_panel = new checkpanel::MapCheckPanel(*mod, *mapdata);
    chk_panel->loc_tab = &_loc_tab;
    connect(chk_panel, &checkpanel::MapCheckPanel::overlayChanged, this, [this] { update_overlay(chk_panel); });
    connect(chk_panel, &checkpanel::MapCheckPanel::selectRequested, this,
            [this](const QList<long long> &land, const QList<long long> &sea) {
                set_selection(std::vector<long long>(land.begin(), land.end()), true,
                              std::vector<long long>(sea.begin(), sea.end()));
            });
    connect(chk_panel, &checkpanel::MapCheckPanel::zoomRequested, this,
            [this](double x, double y, double z) { zoom_to_point(x, y, z); });
    connect(chk_panel, &checkpanel::MapCheckPanel::message, this, [this](const QString &m) { message(m, 10000); });
    connect(chk_panel, &checkpanel::MapCheckPanel::bannerChanged, this,
            [this](const QString &t, std::optional<Rgb> c) { _set_chk_banner(t, c); });
    connect(chk_panel, &checkpanel::MapCheckPanel::checked, this, [this] { _update_errors(); });
    connect(chk_panel, &checkpanel::MapCheckPanel::jumpRequested, this, [this](const QString &key, int ii) {
        std::optional<mapcheck::Item> it;            // (the item, or None: -1) — a copy: a page switch may check again
        if (ii >= 0)
            for (const auto &c : chk_panel->cats)
                if (c.key == key && ii < int(c.items.size())) {
                    it = c.items[size_t(ii)];
                    break;
                }
        jump_to_problem(key, it ? &*it : nullptr);
    });
    connect(chk_panel, &checkpanel::MapCheckPanel::renumberRequested, this, [this] {
        close_number_gaps();
    });
    connect(chk_panel, &checkpanel::MapCheckPanel::errorMapChanged, this, [this] { _error_map_changed(); });
    chk_panel->adj_geo = geo.get();
    // the checks behind the error badge run once editing pauses
    _check_timer = new QTimer(this);
    _check_timer->setSingleShot(true);
    _check_timer->setInterval(700);
    connect(_check_timer, &QTimer::timeout, this, [this] { _checks_due(); });
    _lazy_panel = new QTimer(this);              // the page behind a tool window follows its edits in a pause
    _lazy_panel->setSingleShot(true);
    _lazy_panel->setInterval(500);
    connect(_lazy_panel, &QTimer::timeout, this, [this] { _lazy_refresh(); });
    draw_panel = new drawpanel::DrawPanel(*mod, *mapdata);
    draw_panel->view = map;
    connect(draw_panel, &drawpanel::DrawPanel::overlayChanged, this, [this] { update_overlay(draw_panel); });
    connect(draw_panel, &drawpanel::DrawPanel::message, this, [this](const QString &m) { message(m, 8000); });
    connect(draw_panel, &drawpanel::DrawPanel::editModeChanged, this, [this](bool) { _sync_map_tool(); });
    connect(draw_panel, &drawpanel::DrawPanel::undoRequested, this, [this] { undo(); });
    connect(draw_panel, &drawpanel::DrawPanel::layerChanged, this, [this](const QString &) { _draw_layer_changed(); });
    connect(draw_panel->mm_box, &mismatchbox::MismatchBox::changed, this, [this] { render_map(); });
    connect(draw_panel->river_box, &riverpanel::RiverBox::bannerChanged, this, [this] { _show_banner(); });
    connect(draw_panel, &drawpanel::DrawPanel::zoomRequested, this,
            [this](double x, double y) { zoom_to_point(x, y, 4.0); });
    connect(draw_panel, &drawpanel::DrawPanel::islandRequested, this,
            [this](bool on, const QList<long long> &pids) { on_island(on, pids); });
    draw_panel->pos_panel = pos_panel;
    chk_panel->draw_panel = draw_panel;
    chk_panel->pos_panel = pos_panel;
    draw_panel->geo = geo.get();
    // the Map tab: Adjacencies | Canals | Positions | Draw (the map check lives in the Errors tab)
    const QString ms = settings->value(u"mapSub"_s, u"adj"_s).toString();
    map_sub = (ms == u"adj" || ms == u"canal" || ms == u"pos" || ms == u"draw") ? ms : u"adj"_s;
    map_tab = new QWidget();
    auto *mv = new QVBoxLayout(map_tab);
    mv->setContentsMargins(0, 0, 0, 0);
    mv->setSpacing(0);
    auto *segw = new QWidget();
    segw->setObjectName(u"side"_s);
    auto *sl = new QHBoxLayout(segw);
    sl->setContentsMargins(16, 10, 16, 4);
    sl->setSpacing(0);
    map_sub_group = new QButtonGroup(this);
    for (const auto &[key, text] : std::vector<std::pair<QString, QString>>{
             {u"adj"_s, u"Adjacencies"_s}, {u"canal"_s, u"Canals"_s}, {u"pos"_s, u"Positions"_s}, {u"draw"_s, u"Draw"_s}}) {
        auto *sb = new QPushButton(text);
        sb->setObjectName(u"seg"_s);
        sb->setCheckable(true);
        sb->setChecked(key == map_sub);
        connect(sb, &QPushButton::clicked, this, [this, k = key] { set_map_sub(k); });
        map_sub_group->addButton(sb);
        map_sub_btns[key] = sb;
        sl->addWidget(sb, 1);
    }
    mv->addWidget(segw);
    map_seg_bar = segw;
    // Adjacencies has two sub-tabs: its sea crossings / borders, and the Canals (the user, 2026-10-16: the canal tools
    // in Adjacencies, "neat … distinct from the rest and not just mushed in there")
    auto *adjw = new QWidget();
    adjw->setObjectName(u"side"_s);
    auto *al = new QHBoxLayout(adjw);
    al->setContentsMargins(16, 10, 16, 4);
    al->setSpacing(0);
    auto *grp = new QButtonGroup(this);
    for (const auto &[key, text, tip] : std::vector<std::tuple<QString, QString, QString>>{
             {u"adj"_s, u"Sea crossings && borders"_s,
              u"The adjacencies: sea crossings, impassable borders, the check of every row"_s},
             {u"canal"_s, u"⚓  Canals"_s,
              u"Every canal: its model on the map (Kiel / Panama / Suez, moved pixel by pixel or dragged), how it "
              u"opens, a new one by clicking its province"_s}}) {
        auto *sb = new QPushButton(text);
        sb->setObjectName(u"seg"_s);
        sb->setCheckable(true);
        sb->setToolTip(tip);
        sb->setChecked(key == map_sub);
        connect(sb, &QPushButton::clicked, this, [this, k = key] { set_map_sub(k); });
        grp->addButton(sb);
        adj_sub_btns[key] = sb;
        al->addWidget(sb, 1);
    }
    mv->addWidget(adjw);
    adj_seg_bar = adjw;
    adjw->setVisible(map_sub == u"adj" || map_sub == u"canal");
    map_stack = new widgets::PageStack();
    map_stack->addWidget(map_panel);
    map_stack->addWidget(canal_panel);
    map_stack->addWidget(pos_panel);
    map_stack->addWidget(draw_panel);
    mv->addWidget(map_stack, 1);
    map->click_hook = [this](double x, double y, double tol, Qt::KeyboardModifiers m) {
        return _map_click_hook(x, y, tol, m);
    };
    const QString ps = settings->value(u"provSub"_s, u"provinces"_s).toString();
    prov_sub = (ps == u"provinces" || ps == u"utility" || ps == u"states") ? ps : u"provinces"_s;
    // Population
    edit_panel = new panels::EditPanel(*mod, backup_root);
    pop_fix = new popfixbox::PopFixBox(*mod, [this] { return doc.get(); }, [this] { refresh_panel(); });
    connect(pop_fix, &popfixbox::PopFixBox::message, this, [this](const QString &m) { message(m, 15000); });
    connect(pop_fix, &popfixbox::PopFixBox::selectRequested, this,
            [this](const QList<long long> &pids) { set_selection(std::vector<long long>(pids.begin(), pids.end())); });
    static_cast<QVBoxLayout *>(edit_panel->layout())->insertWidget(0, pop_fix);
    split_panel = new panels::SplitPanel(*mod);
    split_panel->mod_area = &mapdata->area;
    {
        panels::EditPanel *ep = edit_panel;
        connect(ep, &panels::EditPanel::scaleRequested, this, [this](double p) { do_scale(p); });
        connect(ep, &panels::EditPanel::totalRequested, this, [this](long long t) { do_set_total(t); });
        connect(ep, &panels::EditPanel::filterChanged, this, [this] { on_filter_changed(); });
        connect(ep, &panels::EditPanel::splitRequested, this, [this] { start_split(); });
        connect(ep, &panels::EditPanel::consolidateRequested, this, [this] { do_consolidate(); });
        connect(ep, &panels::EditPanel::changeCapitalRequested, this, [this] { start_capital_pick(); });
        connect(ep, &panels::EditPanel::eliteTypesChanged, this,
                [this](const QStringList &t) { settings->setValue(u"eliteTypes"_s, t); });
        connect(ep, &panels::EditPanel::toolRequested, this, [this](const QString &k) { on_tool(k); });
        connect(ep, &panels::EditPanel::mixRequested, this,
                [this](const QString &f, const panels::Mix &t) { do_mix(f, t); });
        connect(ep, &panels::EditPanel::changesRequested, this,
                [this](const QList<long long> &pids, const document::PopFilter &flt, std::optional<long long> total,
                       const QString &field, const std::vector<std::pair<OptStr, double>> &shares) {
                    do_pop_changes(pids, flt, total, field, shares);
                });
        connect(ep, &panels::EditPanel::pendingChanged, this, [this] { _update_pend_bar(); });
        connect(ep, &panels::EditPanel::pendingRefused, this, [this] {
            _update_pend_bar();
            message(u"⚠ Apply or discard the pop changes that wait (the bar at the top) before starting new ones."_s,
                    8000);
        });
        connect(ep, &panels::EditPanel::culturesRequested, this, [this] { open_cultures(); });
        connect(ep, &panels::EditPanel::religionsRequested, this, [this] { open_religions(); });
        // (Python: a list, or one text when one type was kept, or nothing)
        ep->set_elite_types(settings->value(u"eliteTypes"_s, panels::DEFAULT_ELITE).toStringList());
        panels::SplitPanel *sp = split_panel;
        connect(sp, &panels::SplitPanel::applyRequested, this, [this] { apply_split(); });
        connect(sp, &panels::SplitPanel::cancelRequested, this, [this] { end_split(); });
        connect(sp, &panels::SplitPanel::targetsChanged, this, [this] { update_highlight(); });
    }
    // Provinces & states: Provinces | States | Utility states
    prov_panel = new provpanel::ProvincePanel(*mod);
    prov_panel->mapdata = mapdata.get();
    {
        provpanel::ProvincePanel *pp = prov_panel;
        connect(pp, &provpanel::ProvincePanel::edit, this,
                [this](const QString &l, const provpanel::ProvFn &fn) { on_prov_edit(l, fn); });
        connect(pp, &provpanel::ProvincePanel::editFor, this,
                [this](const QString &l, const provpanel::ProvFn &fn, const QList<long long> &pids) {
                    on_prov_edit_for(l, fn, pids);
                });
        connect(pp, &provpanel::ProvincePanel::membershipRequested, this,
                [this](const QString &k, const QString &n) { on_membership(k, n); });
        connect(pp, &provpanel::ProvincePanel::definitionsRequested, this,
                [this](const QString &k) { on_edit_definitions(k); });
        connect(pp, &provpanel::ProvincePanel::stateMoveRequested, this, [this](const QString &k) { on_state_move(k); });
        connect(pp, &provpanel::ProvincePanel::capitalRequested, this, [this] { on_make_capital(); });
        connect(pp, &provpanel::ProvincePanel::islandRequested, this,
                [this](bool on, const QList<long long> &pids) { on_island(on, pids); });
        connect(pp, &provpanel::ProvincePanel::nameRequested, this,
                [this](const QString &k, const QString &t, const QString &s) { on_name(k, t, s); });
        pp->pieces_of = [this](long long pid) { return piece_sizes(pid); };
    }
    bt_panel = new buildtypes::BuildTypesPanel(*mod);
    connect(bt_panel, &buildtypes::BuildTypesPanel::bundleEdit, this,
            [this](const QString &lb, const OrderedMap<QString, QString> &t, const OrderedMap<QString, moreviews::Asset> &f,
                   const OrderedMap<QString, QString> &n) { on_bundle_names(lb, t, f, n); });
    connect(bt_panel, &buildtypes::BuildTypesPanel::nameEdit, this,
            [this](const QString &k, const QString &t) { on_name(k, t, std::nullopt); });
    state_panel = new statepanel::StatePanel(*mod, mapdata.get());
    state_panel->view = map;
    {
        statepanel::StatePanel *sp = state_panel;
        connect(sp, &statepanel::StatePanel::selectRequested, this,
                [this](const QList<long long> &pids) { set_selection(std::vector<long long>(pids.begin(), pids.end())); });
        connect(sp, &statepanel::StatePanel::zoomRequested, this, [this](long long pid) {
            zoom_to_point(double(mapdata->cx[size_t(pid)]) + 0.5, double(mapdata->cy[size_t(pid)]) + 0.5, 4.0);
        });
        connect(sp, &statepanel::StatePanel::nameRequested, this,
                [this](const QString &k, const QString &t, const QString &s) { on_name(k, t, s); });
        connect(sp, &statepanel::StatePanel::scriptRequested, this,
                [this](const QString &rel, long long pos) { open_script_place(rel, pos); });
        connect(sp, &statepanel::StatePanel::message, this, [this](const QString &m) { message(m, 12000); });
        connect(sp, &statepanel::StatePanel::addModeChanged, this, [this](bool) { _sync_map_tool(); });
    }
    util_panel = new utilpanel::UtilityPanel(*mod);
    {
        utilpanel::UtilityPanel *up = util_panel;
        connect(up, &utilpanel::UtilityPanel::viewRequested, this, [this](const QString &r) { _util_view(r); });
        connect(up, &utilpanel::UtilityPanel::markRequested, this,
                [this](const QStringList &r, bool on) { _util_mark(r, on); });
        connect(up, &utilpanel::UtilityPanel::editToggled, this, [this](bool on) { _util_edit_toggled(on); });
        connect(up, &utilpanel::UtilityPanel::selectedRequested, this, [this](bool add) { _util_selected(add); });
    }
    nations_panel = new nationpanel::NationPanel(*mod);
    {
        nationpanel::NationPanel *np = nations_panel;
        connect(np, &nationpanel::NationPanel::edit, this,
                [this](const QString &l, const QString &t, const nationpanel::NationFn &fn) { on_nation_edit(l, t, fn); });
        connect(np, &nationpanel::NationPanel::nameRequested, this,
                [this](const QString &k, const QString &t, const QString &s) { on_name(k, t, s); });
        connect(np->parties_box, &nationextras::PartiesBox::scriptEdit, this,
                [this](const QString &l, const QString &rel, const QString &text) { on_script_text(l, rel, text); });
        connect(np->parties_box, &nationextras::PartiesBox::nameEdit, this,
                [this](const QString &k, const QString &t) { on_name(k, t, std::nullopt); });
        connect(np->flags_box, &nationextras::FlagsBox::studioRequested, this,
                [this](const OptStr &sfx) { open_flag_studio(sfx); });
        connect(np->parties_box, &nationextras::PartiesBox::windowRequested, this, [this] { open_parties_window(); });
        connect(np->parties_box, &nationextras::PartiesBox::colorPreview, this,
                [this](const std::optional<Rgb> &rgb) { _nation_color_preview(rgb); });
        connect(np, &nationpanel::NationPanel::nationPicked, this, [this](const QString &t) { show_nation(t, true); });
        connect(np, &nationpanel::NationPanel::newNationRequested, this, [this](const QString &t) { new_nation(t); });
        connect(np, &nationpanel::NationPanel::capitalPickRequested, this, [this] { start_nation_capital_pick(); });
        connect(np, &nationpanel::NationPanel::placeRequested, this,
                [this](const QString &k, const QString &key) { open_place(k, key); });
    }
    army_panel = new armypanel::ArmyPanel(*mod, *mapdata);
    {
        armypanel::ArmyPanel *ap = army_panel;
        connect(ap, &armypanel::ArmyPanel::edit, this,
                [this](const QString &l, const QString &t, const armypanel::EditFn &fn) { on_oob_edit(l, t, fn); });
        connect(ap, &armypanel::ArmyPanel::nationPicked, this, [this](const QString &t) { show_nation(t, true); });
        connect(ap, &armypanel::ArmyPanel::moveRequested, this, [this](const QString &k) { start_unit_move(k); });
        connect(ap, &armypanel::ArmyPanel::overlayChanged, this, [this, ap] { update_overlay(ap); });
        connect(ap, &armypanel::ArmyPanel::message, this, [this](const QString &s) { message(s, 8000); });
        connect(ap->leaders, &leaderlist::LeaderList::traitsRequested, this, [this] {   // → Warfare → Leader traits
            go_page(u"warfare"_s);
            war_ws->set_page(u"traits"_s);
        });
        connect(ap->leaders, &leaderlist::LeaderList::scopeChanged, this, [this] {
            if (color_mode == u"military")
                render_map();
            else
                update_highlight();                  // (the red outline of the picked nations)
        });
    }
    diplo_panel = new diplopanel::DiploPanel(*mod, *mapdata);
    {
        diplopanel::DiploPanel *dp = diplo_panel;
        connect(dp, &diplopanel::DiploPanel::edit, this,
                [this](const QString &l, const diplopanel::EditFn &fn) { on_diplo_edit(l, fn); });
        connect(dp, &diplopanel::DiploPanel::nationPicked, this,
                [this](const QString &t) { show_nation(t.isEmpty() ? OptStr() : OptStr(t), false); });
        connect(dp, &diplopanel::DiploPanel::otherPicked, this, [this](const QString &t) { show_other(t); });
        connect(dp, &diplopanel::DiploPanel::overlayChanged, this, [this, dp] { update_overlay(dp); });
        connect(dp, &diplopanel::DiploPanel::viewChanged, this, [this](const QString &k) { _diplo_view(k); });
        connect(dp, &diplopanel::DiploPanel::warRequested, this, [this](const QString &rel) { open_war(rel); });
    }
    dip_view = new diploweb::DiploWebView(*mod);   // the relations instead of the map (Diplomacy)
    dip_view->hide();
    left_split->addWidget(dip_view);
    connect(dip_view, &diploweb::DiploWebView::picked, this,
            [this](const QString &t) { show_nation(t.isEmpty() ? OptStr() : OptStr(t), false); });
    connect(dip_view, &diploweb::DiploWebView::edit, this,
            [this](const QString &l, const diplopanel::EditFn &fn) { on_diplo_edit(l, fn); });
    connect(dip_view, &diploweb::DiploWebView::other, this, [this](const QString &t) { show_other(t); });
    connect(dip_view, &diploweb::DiploWebView::message, this, [this](const QString &m) { message(m, 9000); });
    diplo_panel->web_view = dip_view;
    diplo_panel->show_web = [this] {
        dip_view->related = [this](const QString &t) { return diplo_panel->_related(t); };
        dip_view->show_for(doc.get(), diplo_panel->a, [this] { return diplo_panel->wars(); });
    };
    connect(diplo_panel, &diplopanel::DiploPanel::shownChanged, this, [this](const QString &) { _place_map(); });
    event_panel = new eventpanel::EventPanel(*mod);
    connect(event_panel, &eventpanel::EventPanel::changed, this, [this] { _events_changed(); });
    connect(event_panel, &eventpanel::EventPanel::catEdit, this,
            [this](const eventpanel::CatChanges &ch, const QString &l) { on_event_cats(ch, l); });
    connect(event_panel, &eventpanel::EventPanel::litEdit, this,
            [this](const QString &rel, const QString &uid, bool on) { on_event_literacy(rel, uid, on); });
    connect(event_panel, &eventpanel::EventPanel::changed, this, [this] {
        if (color_mode == u"literacy" && (tab == u"pops" || tab == u"nations"))
            _render_timer->start(300);
    });
    connect(event_panel, &eventpanel::EventPanel::provincePicked, this, [this](long long pid) { on_event_province(pid); });
    connect(event_panel, &eventpanel::EventPanel::focusChanged, this, [this] {
        if (tab == u"events" && map->isVisible())
            _events_focus();
    });
    event_panel->attach(script_ws);
    connect(event_panel, &eventpanel::EventPanel::toolRequested, this, [this](const QString &k) { _script_tool(k); });
    connect(event_panel, &eventpanel::EventPanel::changed, this, [this] { _refresh_script_windows(); });
    event_panel->hold = [this] { return _tool_busy(); };
    connect(script_ws, &scriptedit::ScriptWorkspace::mapToggled, this, [this](bool on) { _events_map_toggled(on); });
    connect(script_ws, &scriptedit::ScriptWorkspace::replaceRequested, this,
            [this](const OrderedMap<QString, QString> &ch, const QString &l) { on_script_replace(ch, l); });
    connect(event_panel->mod_view, &modpanel::ModifierView::modEdit, this,
            [this](const QString &label, const modpanel::ModFn &fn) {
                auto &mods = doc->map<modifiers::ModifierEdits>(u"event_mods"_s);
                doc->edit(label, {}, [&mods, fn] { fn(mods); }, {}, {}, {u"scripts"_s});
                message(u"%1 — written to common/event_modifiers.txt on Apply (Ctrl+Z undoes it)."_s.arg(label), 8000);
            });
    connect(event_panel->mod_view, &modpanel::ModifierView::nameEdit, this,
            [this](const QString &key, const QString &text) { on_name(key, text, std::nullopt); });
    connect(event_panel->cb_view, &cbpanel::WarGoalView::scriptEdit, this,
            [this](const QString &lb, const QString &rel, const QString &text) { on_script_text(lb, rel, text); });
    connect(event_panel->cb_view, &cbpanel::WarGoalView::nameEdit, this,
            [this](const QString &key, const QString &text) { on_name(key, text, std::nullopt); });
    prov_host = new QWidget();
    {
        auto *pv = new QVBoxLayout(prov_host);
        pv->setContentsMargins(0, 0, 0, 0);
        pv->setSpacing(0);
        auto *pseg = new QWidget();
        pseg->setObjectName(u"side"_s);
        auto *psl = new QHBoxLayout(pseg);
        psl->setContentsMargins(16, 10, 16, 4);
        psl->setSpacing(0);
        auto *pgrp = new QButtonGroup(this);
        for (const auto &[key, text] : std::vector<std::pair<QString, QString>>{
                 {u"provinces"_s, u"Provinces"_s}, {u"states"_s, u"States"_s}, {u"utility"_s, u"Utility states"_s}}) {
            auto *pb = new QPushButton(text);
            pb->setObjectName(u"seg"_s);
            pb->setCheckable(true);
            pb->setChecked(key == prov_sub);
            connect(pb, &QPushButton::clicked, this, [this, k = key] { set_prov_sub(k); });
            pgrp->addButton(pb);
            prov_sub_btns[key] = pb;
            psl->addWidget(pb, 1);
        }
        pv->addWidget(pseg);
        prov_seg_bar = pseg;
        prov_stack = new widgets::PageStack();
        prov_stack->addWidget(prov_panel);
        prov_stack->addWidget(util_panel);
        prov_stack->addWidget(state_panel);
        pv->addWidget(prov_stack, 1);
    }
    stack->addWidget(edit_panel);
    stack->addWidget(split_panel);
    stack->addWidget(prov_host);
    stack->addWidget(map_tab);
    stack->addWidget(nations_panel);
    stack->addWidget(army_panel);
    stack->addWidget(diplo_panel);
    stack->addWidget(event_panel);
    stack->addWidget(chk_panel);
    for (widgets::PageStack *st : {stack, map_stack, prov_stack})
        connect(st, &widgets::PageStack::currentChanged, this, [this](int) { _page_changed(); });
    side_scroll = new QScrollArea();
    side_scroll->setObjectName(u"sideScroll"_s);
    side_scroll->setWidgetResizable(true);
    side_scroll->setWidget(stack);
    // pages wrap to the panel's width — never a horizontal scrollbar (the user's rule)
    side_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    side_scroll->setMinimumWidth(450);             // the widest page (Pops) needs ~435 px: never cut off
    _stick_timer = new QTimer(this);
    _stick_timer->setSingleShot(true);
    _stick_timer->setInterval(500);
    connect(_stick_timer, &QTimer::timeout, this, [this] { _stick_bottom = false; });
    connect(side_scroll->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int hi) {
        if (_stick_bottom)
            side_scroll->verticalScrollBar()->setValue(hi);
    });
    split->addWidget(side_scroll);
    script_ws->set_side_host(side_scroll, event_panel->mini_host);   // the mini window can be dropped there
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setSizes({1100, 470});
    root->addWidget(split, 1);
    main_split = split;
    size_ws = new mapsize::MapSizeWorkspace(*mod, *mapdata);
    size_ws->backup_root = backup_root;
    connect(size_ws, &mapsize::MapSizeWorkspace::message, this, [this](const QString &s) { message(s, 15000); });
    connect(size_ws, &mapsize::MapSizeWorkspace::reloadRequested, this, [this] { _reload_mod(); });
    size_ws->before_cut = [this](const std::vector<long long> &pids) { return _delete_before_cut(pids); };
    size_ws->asset_edit = [this](const QString &l, const OrderedMap<QString, moreviews::Asset> &f) { on_asset_edit(l, f); };
    size_ws->hide();
    root->addWidget(size_ws, 1);
    keys_ws = new shortcuts::ShortcutsWorkspace(*mod);
    connect(keys_ws, &shortcuts::ShortcutsWorkspace::edit, this,
            [this](const QString &lb, const QString &rel, const QString &text) { on_script_text(lb, rel, text); });
    connect(keys_ws, &shortcuts::ShortcutsWorkspace::message, this, [this](const QString &s) { message(s, 12000); });
    keys_ws->hide();
    root->addWidget(keys_ws, 1);
    sub_ws = new submodpage::SubmodWorkspace(*mod);
    sub_ws->md = mapdata.get();                      // (C++ only: the 4K screen draws the map)
    connect(sub_ws, &submodpage::SubmodWorkspace::message, this, [this](const QString &s) { message(s, 20000); });
    sub_ws->hide();
    root->addWidget(sub_ws, 1);
    ck_ws = new checksumpage::ChecksumWorkspace(*sub_ws);   // (C++ only, 2026-11-18: Tools → Checksum)
    ck_ws->hide();
    root->addWidget(ck_ws, 1);
    econ_ws = new econest::EconWorkspace(*mod);
    econ_ws->md = mapdata.get();
    connect(econ_ws, &econest::EconWorkspace::selectRequested, this,
            [this](const QList<long long> &pids, const QString &level) { _econ_show(pids, level); });
    connect(econ_ws, &econest::EconWorkspace::chainsRequested, this, [this] { go_page(u"production"_s); });
    econ_ws->hide();
    root->addWidget(econ_ws, 1);
    popinf_ws = new popinfluence::PopInfluencePage(*mod);
    popinf_ws->md = mapdata.get();                   // (its sample can be a province / state picked on a map)
    connect(popinf_ws, &popinfluence::PopInfluencePage::scriptEdit, this,
            [this](const QString &lb, const QString &rel, const QString &text) { on_script_text(lb, rel, text); });
    connect(popinf_ws, &popinfluence::PopInfluencePage::scriptsEdit, this,
            [this](const OrderedMap<QString, QString> &ch, const QString &l) { on_script_replace(ch, l); });
    connect(popinf_ws, &popinfluence::PopInfluencePage::nameEdit, this,
            [this](const QString &key, const QString &text) { on_name(key, text, std::nullopt); });
    popinf_ws->hide();
    root->addWidget(popinf_ws, 1);
    pol_ws =new politicspanel::PoliticsWorkspace(*mod);
    pol_ws->hide();
    root->addWidget(pol_ws, 1);
    // edited as script (Python: for iv in (self.pol_ws, …))
    connect(pol_ws, &politicspanel::PoliticsWorkspace::scriptEdit, this,
            [this](const QString &lb, const QString &rel, const QString &text) { on_script_text(lb, rel, text); });
    connect(pol_ws, &politicspanel::PoliticsWorkspace::nameEdit, this,
            [this](const QString &key, const QString &text) { on_name(key, text, std::nullopt); });
    connect(pol_ws, &politicspanel::PoliticsWorkspace::fileRemove, this,
            [this](const QString &lb, const QString &rel) { on_script_remove(lb, rel); });
    connect(pol_ws, &politicspanel::PoliticsWorkspace::bigRequested, this,
            [this](const QString &rel, const QStringList &path, const QString &noun) { open_big(rel, path, noun); });
    tech_ws = new techpanel::TechWorkspace(*mod);
    connect(tech_ws, &techpanel::TechWorkspace::edit, this,
            [this](const QString &lb, const QString &key, const QString &text) { on_tech_edit(lb, key, text); });
    connect(tech_ws, &techpanel::TechWorkspace::nameRequested, this,
            [this](const QString &key, const QString &text, const QString &section) { on_name(key, text, section); });
    connect(tech_ws, &techpanel::TechWorkspace::guiPatchRequested, this, [this](long long rows) { on_tech_gui(rows); });
    connect(tech_ws->schools, &defpanel::DefView::scriptEdit, this,      // edited as script
            [this](const QString &lb, const QString &rel, const QString &text) { on_script_text(lb, rel, text); });
    connect(tech_ws->schools, &defpanel::DefView::nameEdit, this,
            [this](const QString &key, const QString &text) { on_name(key, text, std::nullopt); });
    connect(tech_ws, &techpanel::TechWorkspace::orderRequested, this,
            [this](const OptStr &area, const QString &key, long long step) { on_tech_order(area, key, step); });
    connect(tech_ws, &techpanel::TechWorkspace::inventionRequested, this,
            [this](const QString &key, const QString &file, const QString &text, const QString &name) {
                on_invention_new(key, file, text, name);
            });
    connect(tech_ws, &techpanel::TechWorkspace::inventionRemoveRequested, this,
            [this](const QString &key) { on_invention_remove(key); });
    connect(tech_ws, &techpanel::TechWorkspace::rowRequested, this, [this] { on_tech_row(); });
    connect(tech_ws, &techpanel::TechWorkspace::rowRemoveRequested, this, [this] { on_tech_row_remove(); });
    connect(tech_ws, &techpanel::TechWorkspace::dropRequested, this,
            [this](const OptStr &area, const QString &key, const QString &target) { on_tech_drop(area, key, target); });
    connect(tech_ws, &techpanel::TechWorkspace::message, this, [this](const QString &s, int ms) { message(s, ms); });
    tech_ws->hide();
    root->addWidget(tech_ws, 1);
    prod_ws = new prodpanel::ProductionWorkspace(*mod);
    connect(prod_ws, &prodpanel::ProductionWorkspace::outputRequested, this, [this] { go_page(u"output"_s); });
    connect(prod_ws, &prodpanel::ProductionWorkspace::edit, this,
            [this](const QString &lb, const QString &key, const QString &text) { on_prod_edit(lb, key, text); });
    connect(prod_ws, &prodpanel::ProductionWorkspace::other, this,
            [this](const QString &lb, const QString &what, const QString &key, const QString &v) {
                on_prod_other(lb, what, key, v);
            });
    connect(prod_ws, &prodpanel::ProductionWorkspace::addRequested, this,
            [this](const QString &key, const QString &text, const std::optional<std::pair<QString, QString>> &b,
                   const QString &bn) { on_prod_add(key, text, b, bn); });
    connect(prod_ws, &prodpanel::ProductionWorkspace::removeRequested, this, [this](const QString &k) { on_prod_remove(k); });
    prod_ws->hide();
    root->addWidget(prod_ws, 1);
    cl_ws = new changelog::ChangelogWorkspace(*mod);
    connect(cl_ws, &changelog::ChangelogWorkspace::message, this, [this](const QString &s) { message(s, 10000); });
    cl_ws->hide();
    root->addWidget(cl_ws, 1);
    {
        auto entry = [this](const entryview::Spec &spec) { return [this, &spec]() -> QWidget * {
            return new entryview::EntryView(*mod, spec); }; };
        auto entry_refresh = [](QWidget *w, document::Document *d) { static_cast<entryview::EntryView *>(w)->refresh(d); };
        war_ws = new entryview::PagesWorkspace(*mod, {
            {u"units"_s, u"Unit stats"_s, [this]() -> QWidget * {
                 auto *v = new unitpanel::UnitStatsView(*mod);
                 v->tech_db = tech_db.get();
                 return v; },
             [this](QWidget *w, document::Document *d) {
                 auto *v = static_cast<unitpanel::UnitStatsView *>(w);
                 v->tech_db = tech_db.get();
                 v->refresh(d); }},
            {u"timeline"_s, u"Unit timeline"_s, [this]() -> QWidget * {
                 return new unittimeline::UnitTimelineView(*mod, [this](const QString &l, const QString &k, const QString &t) {
                     on_tech_edit(l, k, t); }); },
             [](QWidget *w, document::Document *d) { static_cast<unittimeline::UnitTimelineView *>(w)->refresh(d); }},
            // (C++ only, 2026-11-17) the odds of a new leader + mass edit, and the one-trait entry view
            {u"traits"_s, u"Leader traits"_s, [this]() -> QWidget * {
                 auto *v = new traitodds::TraitsPage(*mod);
                 connect(v, &traitodds::TraitsPage::message, this, [this](const QString &s) { message(s, 10000); });
                 return v; },
             [](QWidget *w, document::Document *d) { static_cast<traitodds::TraitsPage *>(w)->refresh(d); }},
            {u"terrain"_s, u"Terrain types"_s, entry(entryview::TERRAIN), entry_refresh},
            {u"wars"_s, u"Wars at the start"_s, [this]() -> QWidget * { return new moreviews::WarsView(*mod); },
             [](QWidget *w, document::Document *d) { static_cast<moreviews::WarsView *>(w)->refresh(d); }},
            {u"colours"_s, u"Unit colours"_s, entry(entryview::UNIT_COLOURS), entry_refresh},
            // (the user 2026-11-17: the almanac is the last page of Warfare, no longer in Library)
            {u"almanacs"_s, u"Almanacs"_s, [this]() -> QWidget * { return new moreviews::AlmanacPage(*mod); },
             [](QWidget *w, document::Document *d) { static_cast<moreviews::AlmanacPage *>(w)->refresh(d); }}});
    }
    connect(war_ws, &entryview::PagesWorkspace::scriptEdit, this,      // edited as script
            [this](const QString &lb, const QString &rel, const QString &text) { on_script_text(lb, rel, text); });
    connect(war_ws, &entryview::PagesWorkspace::nameEdit, this,
            [this](const QString &key, const QString &text) { on_name(key, text, std::nullopt); });
    connect(war_ws, &entryview::PagesWorkspace::fileRemove, this,
            [this](const QString &lb, const QString &rel) { on_script_remove(lb, rel); });
    connect(war_ws, &entryview::PagesWorkspace::bigRequested, this,
            [this](const QString &rel, const QStringList &path, const QString &noun) { open_big(rel, path, noun); });
    war_ws->hide();
    root->addWidget(war_ws, 1);
    {
        auto entry = [this](const entryview::Spec &spec) { return [this, &spec]() -> QWidget * {
            return new entryview::EntryView(*mod, spec); }; };
        auto entry_refresh = [](QWidget *w, document::Document *d) { static_cast<entryview::EntryView *>(w)->refresh(d); };
        rules_ws = new entryview::PagesWorkspace(*mod, {
            {u"settings"_s, u"Game settings"_s, [this]() -> QWidget * { return new entryview::GameSettingsView(*mod); },
             [](QWidget *w, document::Document *d) { static_cast<entryview::GameSettingsView *>(w)->refresh(d); }},
            {u"bookmarks"_s, u"Start dates"_s, [this]() -> QWidget * { return new moreviews::StartDatesView(*mod); },
             [](QWidget *w, document::Document *d) { static_cast<moreviews::StartDatesView *>(w)->refresh(d); }},
            {u"poptypes"_s, u"Pop types"_s, [this]() -> QWidget * { return _script_page(scriptpage::POP_TYPES); },
             [](QWidget *w, document::Document *d) { static_cast<scriptpage::ScriptListPage *>(w)->refresh(d); }},
            {u"values"_s, u"National values"_s,
             [this]() -> QWidget * {
                 auto *old = new defpanel::DefView(*mod, defpanel::NATIONAL_VALUES);
                 return new iconboard::BoardWithList(*mod, u"nv"_s, old, [old](document::Document *d) { old->refresh(d); },
                                                     [this](const QString &lb, const iconstrips::StripFiles &f,
                                                            const OrderedMap<QString, QString> &t) { on_strip_change(lb, f, t); });
             },
             [](QWidget *w, document::Document *d) { static_cast<iconboard::BoardWithList *>(w)->refresh(d); }},
            {u"focus"_s, u"National focuses"_s,
             [this]() -> QWidget * {
                 auto *old = new entryview::EntryView(*mod, entryview::FOCUSES);
                 return new iconboard::BoardWithList(*mod, u"focus"_s, old, [old](document::Document *d) { old->refresh(d); },
                                                     [this](const QString &lb, const iconstrips::StripFiles &f,
                                                            const OrderedMap<QString, QString> &t) { on_strip_change(lb, f, t); });
             },
             [](QWidget *w, document::Document *d) { static_cast<iconboard::BoardWithList *>(w)->refresh(d); }},
            // (the user 2026-11-17: Building types belongs here, after the national focuses — a page of types like
            // them; before it was a sub-tab of Provinces & states)
            // (in a scroll area of its own, as it had in the side panel: the page is long, and it wraps to the width —
            // never a horizontal scroll bar)
            {u"buildings"_s, u"Building types"_s,
             [this]() -> QWidget * {
                 auto *sc = new QScrollArea();
                 sc->setWidgetResizable(true);
                 sc->setFrameShape(QFrame::NoFrame);
                 sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                 sc->setWidget(bt_panel);
                 return sc;
             },
             [this](QWidget *, document::Document *d) {
                 if (d)
                     bt_panel->refresh(*d, {});
             }},
            {u"rebels"_s, u"Rebels"_s, [this]() -> QWidget * { return _script_page(scriptpage::REBELS); },
             [](QWidget *w, document::Document *d) { static_cast<scriptpage::ScriptListPage *>(w)->refresh(d); }},
            {u"static"_s, u"Standing modifiers"_s, entry(entryview::STATIC_MODS), entry_refresh},
            {u"triggered"_s, u"Triggered modifiers"_s, entry(entryview::TRIGGERED_MODS), entry_refresh},
            {u"crime"_s, u"Crimes"_s, entry(entryview::CRIMES), entry_refresh},
            {u"stockpile"_s, u"Stockpile slider"_s, [this]() -> QWidget * { return new stockpilepage::StockpilePage(*mod); },
             [](QWidget *w, document::Document *d) { static_cast<stockpilepage::StockpilePage *>(w)->refresh(d); }},
            {u"news"_s, u"News"_s, entry(entryview::NEWS), entry_refresh},
            {u"timed"_s, u"Timed events"_s,
             [this]() -> QWidget * { return new moreviews::TimedEventsView(*mod, [this](const QString &e) { return _event_name(e); }); },
             [](QWidget *w, document::Document *d) { static_cast<moreviews::TimedEventsView *>(w)->refresh(d); }},
            {u"index"_s, u"Flags && variables"_s,
             [this]() -> QWidget * {
                 return new moreviews::IndexView(*mod, [this] { return _script_refs(); },
                                                 [this](const QString &rel, long long pos) { _script_jump(rel, pos); }); },
             [](QWidget *w, document::Document *d) { static_cast<moreviews::IndexView *>(w)->refresh(d); }}});
    }
    connect(rules_ws, &entryview::PagesWorkspace::scriptEdit, this,    // edited as script
            [this](const QString &lb, const QString &rel, const QString &text) { on_script_text(lb, rel, text); });
    connect(rules_ws, &entryview::PagesWorkspace::nameEdit, this,
            [this](const QString &key, const QString &text) { on_name(key, text, std::nullopt); });
    connect(rules_ws, &entryview::PagesWorkspace::fileRemove, this,
            [this](const QString &lb, const QString &rel) { on_script_remove(lb, rel); });
    connect(rules_ws, &entryview::PagesWorkspace::bigRequested, this,
            [this](const QString &rel, const QStringList &path, const QString &noun) { open_big(rel, path, noun); });
    rules_ws->hide();
    root->addWidget(rules_ws, 1);
    {
        auto add = [this](const QString &l, const OrderedMap<QString, moreviews::Asset> &f) { on_asset_edit(l, f); };
        auto look = [this, add](bool menu) -> QWidget * {
            if (menu)
                return new lookpages::MenuLookPage(*mod, add);
            return new lookpages::MapLookPage(*mod, add);
        };
        auto look_refresh = [](QWidget *w, document::Document *d) { static_cast<lookpages::LookPage *>(w)->refresh(d); };
        auto lib_refresh = [](QWidget *w, document::Document *d) { static_cast<moreviews::LibraryPage *>(w)->refresh(d); };
        lib_ws = new entryview::PagesWorkspace(*mod, {
            {u"strips"_s, u"Icon strips"_s,
             [this]() -> QWidget * {
                 return new iconstrips::StripsPage(*mod, [this](const QString &lb, const iconstrips::StripFiles &f,
                                                                const OrderedMap<QString, QString> &tx) {
                     on_strip_change(lb, f, tx); });
             },
             [](QWidget *w, document::Document *d) { static_cast<iconstrips::StripsPage *>(w)->refresh(d); }},
            {u"boards"_s, u"Picture boards"_s, [this]() -> QWidget * { return _boards_page(); },
             [](QWidget *w, document::Document *d) { static_cast<iconboard::AllBoardsPage *>(w)->refresh(d); }},
            {u"pictures"_s, u"Pictures"_s,
             [this, add]() -> QWidget * { return new moreviews::PicturesPage(*mod, add, [this] { return _used_pictures(); }); },
             lib_refresh},
            {u"music"_s, u"Music"_s, [this, add]() -> QWidget * { return new moreviews::MusicPage(*mod, add); }, lib_refresh},
            {u"menu"_s, u"Menu && loading"_s, [look]() -> QWidget * { return look(true); }, look_refresh},
            {u"maplook"_s, u"Map look"_s, [look]() -> QWidget * { return look(false); }, look_refresh},
            {u"mapmarks"_s, u"Zoom && map marks"_s, [this]() -> QWidget * { return _mapmarks_page(); },
             [](QWidget *w, document::Document *d) { static_cast<mapmarkspage::MapMarksPage *>(w)->refresh(d); }}});
    }
    connect(lib_ws, &entryview::PagesWorkspace::scriptEdit, this,     // edited as script
            [this](const QString &lb, const QString &rel, const QString &text) { on_script_text(lb, rel, text); });
    connect(lib_ws, &entryview::PagesWorkspace::nameEdit, this,
            [this](const QString &key, const QString &text) { on_name(key, text, std::nullopt); });
    connect(lib_ws, &entryview::PagesWorkspace::fileRemove, this,
            [this](const QString &lb, const QString &rel) { on_script_remove(lb, rel); });
    connect(lib_ws, &entryview::PagesWorkspace::bigRequested, this,
            [this](const QString &rel, const QStringList &path, const QString &noun) { open_big(rel, path, noun); });
    lib_ws->hide();
    root->addWidget(lib_ws, 1);
    imp_ws = new importpanel::ImportWorkspace(*mod);
    imp_ws->md = mapdata.get();
    connect(imp_ws, &importpanel::ImportWorkspace::message, this, [this](const QString &s) { message(s, 15000); });
    connect(imp_ws, &importpanel::ImportWorkspace::imported, this, [this] { _after_import(); });
    imp_ws->hide();
    root->addWidget(imp_ws, 1);
    event_panel->script_edit = [this](const QString &lb, const QString &rel, const QString &text) {
        on_script_text(lb, rel, text);
    };
    outer->addWidget(_build_statusbar());
    setCentralWidget(central);
    _arm_keep_copy();
    // the rail has a page for each of these: their own buttons in the side panel are not needed any more
    map_seg_bar->hide();
    // Provinces & states keeps its own sub-tabs: Provinces | States | Utility states (the user, 2026-10-15: States
    // is a sub-tab of this page, not a page of its own in the rail)
    prov_switch = new QPushButton();
    prov_switch->setCursor(Qt::PointingHandCursor);
    prov_switch->setFocusPolicy(Qt::NoFocus);
    prov_switch->setStyleSheet(u"QPushButton { border: none; background: transparent; color: %1; padding: 7px 16px 3px "
                               u"16px; text-align: right; } QPushButton:hover { color: %2; }"_s.arg(theme::GOLD, theme::INK));
    connect(prov_switch, &QPushButton::clicked, this,
            [this] { set_prov_sub(prov_sub == u"utility" ? u"provinces"_s : u"utility"_s); });
    static_cast<QVBoxLayout *>(prov_host->layout())->insertWidget(0, prov_switch);
    prov_switch->hide();                           // (the sub-tabs above say it now)
    _prov_switch_text();
    // (the army panel's own row stays: the rail has Armies and Navies, the row their sub-tabs Armies | Generals and
    // Navies | Admirals — the user 2026-11-18)
    army_panel->after_set_sub = [this] { _sync_rail(); };   // (Python: set_sub wrapped)
    _sync_rail();

    _render_timer = new QTimer(this);
    _render_timer->setSingleShot(true);
    connect(_render_timer, &QTimer::timeout, this, [this] { render_map(); });
    _rec_timer = new QTimer(this);
    _rec_timer->setInterval(4000);
    connect(_rec_timer, &QTimer::timeout, this, [this] { _recovery_tick(); });
    _rec_timer->start();

    undokeys::install();                           // Ctrl+Z / Ctrl+Y reach the undo from text boxes too
    const std::vector<std::pair<QKeySequence, std::function<void()>>> keys = {
        {QKeySequence::Undo, [this] { undo(); }},
        // (QKeySequence::Redo is Ctrl+Y and Ctrl+Shift+Z on Windows: a second Ctrl+Y shortcut made the key ambiguous —
        //  Qt then runs neither, so Ctrl+Y did nothing; 2026-11-13)
        {QKeySequence::Redo, [this] { redo(); }},
        {QKeySequence::Save, [this] { save(); }},
        {QKeySequence(u"Escape"_s), [this] { escape(); }},
        {QKeySequence::Find, [this] { search->setFocus(); }},
        {QKeySequence(u"D"_s), [this] { toggle_draw(); }},
        {QKeySequence(u"Ctrl+K"_s), [this] { open_palette(); }}};
    for (const auto &[seq, fn] : keys) {
        auto *sc = new QShortcut(seq, this);
        connect(sc, &QShortcut::activated, this, fn);
    }
    // Map → Drawing → Provinces: cut / paste provinces (provclip). Keys of the MAP only, so texts keep their own
    // Ctrl+X / C / V; the arrows nudge pasted provinces while they float, else scroll the map as before.
    auto drawing = [this] {
        return tab == u"map" && map_sub == u"draw" && draw_panel->edit_btn->isChecked() && draw_panel->layer() == u"prov";
    };
    const std::vector<std::pair<QString, std::function<QString()>>> clip_keys = {
        {u"Ctrl+X"_s, [this] { return draw_panel->clip.cut(); }},
        {u"Ctrl+C"_s, [this] { return draw_panel->clip.copy(); }},
        {u"Ctrl+V"_s, [this] { return draw_panel->clip.paste(); }}};
    for (const auto &[seq, fn] : clip_keys)
        for (QWidget *where : {static_cast<QWidget *>(map), static_cast<QWidget *>(draw_panel)}) {
            // (the map, or the Drawing panel: provinces may be picked in its list; its text boxes keep their own keys)
            auto *sc = new QShortcut(QKeySequence(seq), where);
            sc->setContext(Qt::WidgetWithChildrenShortcut);
            connect(sc, &QShortcut::activated, this, [this, drawing, fn] {
                if (drawing()) {
                    message(fn(), 12000);
                    draw_panel->_clip_changed();
                } else {
                    message(u"Ctrl+X / Ctrl+C / Ctrl+V cut, copy and paste provinces in Map → Drawing (drawing on, the "
                            "Provinces layer)."_s, 6000);
                }
            });
        }
    // (C++ only, the user's wish 2026-11-09) while drawing: 1 / 2 / 3 the layer drawn on, G the pixel grid — the
    // drawing bar at the map's top left shows them too
    const std::vector<std::pair<QString, std::function<void()>>> draw_keys = {
        {u"1"_s, [this] { draw_panel->set_layer(u"prov"_s); }},
        {u"2"_s, [this] { draw_panel->set_layer(u"terrain"_s); }},
        {u"3"_s, [this] { draw_panel->set_layer(u"rivers"_s); }},
        {u"4"_s, [this] { draw_panel->set_layer(u"colours"_s); }},
        {u"G"_s, [this] { draw_panel->toggle_grid(); }}};
    for (const auto &[seq, fn] : draw_keys)
        for (QWidget *where : {static_cast<QWidget *>(map), static_cast<QWidget *>(draw_panel)}) {
            auto *sc = new QShortcut(QKeySequence(seq), where);
            sc->setContext(Qt::WidgetWithChildrenShortcut);
            connect(sc, &QShortcut::activated, this, [this, fn] {
                if (tab == u"map" && map_sub == u"draw" && draw_panel->edit_btn->isChecked())
                    fn();
            });
        }
    for (const auto &[seq, dx, dy] : std::vector<std::tuple<QString, int, int>>{
             {u"Left"_s, -1, 0}, {u"Right"_s, 1, 0}, {u"Up"_s, 0, -1}, {u"Down"_s, 0, 1},
             {u"Shift+Left"_s, -10, 0}, {u"Shift+Right"_s, 10, 0}, {u"Shift+Up"_s, 0, -10}, {u"Shift+Down"_s, 0, 10}}) {
        auto *sc = new QShortcut(QKeySequence(seq), map);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, [this, dx, dy] {
            if (draw_panel->clip.floating()) {
                draw_panel->clip.nudge(dx, dy);
                return;
            }
            QScrollBar *bar = dx ? map->horizontalScrollBar() : map->verticalScrollBar();
            bar->triggerAction((dx < 0 || dy < 0) ? QAbstractSlider::SliderSingleStepSub : QAbstractSlider::SliderSingleStepAdd);
        });
    }

    load_scenario(scenario.value_or(QString()));
    theme::no_hscroll(this);                       // never a horizontal scrollbar (the user's rule)
    // (C++ only, 2026-11-18, a tester: "de-fullscreening itself during use sometimes") left alone, the window's minimum
    // size is its layout's and changes with the page shown (measured: 1619 – 1897 px wide, 948 – 1032 px high). Where
    // that is more than the screen gives a maximised window (a 1080p screen at 125 %, a laptop), Windows resizes the
    // window to that minimum as soon as such a page opens, and it leaves the maximised state. A minimum of its own keeps
    // the layout from setting one (QLayout::SetDefaultConstraint): the pages get less room instead.
    setMinimumSize(1024, 600);
    QTimer::singleShot(0, map, [this] { map->fit(); });
}

MainWindow::~MainWindow()
{
    modcopy::disarm(_keep_id);
}

void MainWindow::_arm_keep_copy()
{
    // the folder the editor protects: the mod's own (a submod: its real folder, not the working copy)
    const QString folder = mod->submod ? mod->submod->root : mod->root;
    _keep_id = modcopy::arm(folder, QString(),
                            [this] {
                                const bool on = keep_cb && keep_cb->isChecked();
                                if (on) {         // (asked right before the copy: a big mod takes some seconds)
                                    message(u"Keeping a copy of the original mod first (once) …"_s);
                                    st_msg->repaint();
                                }
                                return on;
                            },
                 [this](const modcopy::Result &r) { _keep_copy_done(r); });
    if (modcopy::copied(folder) && !modcopy::copy_of(folder).isEmpty())
        _keep_copy_done({true, QString(), modcopy::copy_of(folder), QString(), QString(), 0, 0});
}

void MainWindow::_keep_copy_done(const modcopy::Result &r)
{
    if (!keep_cb)
        return;
    if (r.ok) {
        keep_cb->setText(u"Original kept ✓"_s);
        keep_cb->setToolTip(u"The untouched mod was copied before the first write of this run:\n%1%2"_s.arg(
            r.folder, r.name.isEmpty() ? QString() : u"\nIn the launcher: %1"_s.arg(r.name)));
        if (r.files)
            message(u"Kept the original mod first: %1 files copied to \"ModEditor safeguards\\%2\" — in the launcher "
                    u"as \"%3\"."_s
                        .arg(r.files)
                        .arg(pypath::basename(r.folder), r.name),
                    15000);
    } else {
        message(u"⚠ The copy of the original mod could not be made (%1) — nothing was written."_s.arg(r.error), 15000);
    }
}

// ---------------------------------------------------------------------------------------------- chrome
QWidget *MainWindow::_build_toolbar()
{
    auto *bar = new QWidget();
    bar->setObjectName(u"toolbar"_s);
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(14, 8, 14, 8);
    lay->setSpacing(10);

    const auto &sub = mod->submod;
    // (C++ only, the user 2026-11-17) the bar starts with a plain way out — back to the opening window and its list of
    // mods — then the mod's name as a text box (double-click or ✎ to rename it), then the scenario, then the search
    mod_btn = new QPushButton(u"⟵  Mod list"_s);
    mod_btn->setObjectName(u"exitMod"_s);
    mod_btn->setCursor(Qt::PointingHandCursor);
    mod_btn->setStyleSheet(u"QPushButton#exitMod { font-weight: 600; padding: 4px 12px; }"_s);
    mod_btn->setToolTip(u"Leave this mod and go back to the opening window, with the list of mods (edits not yet "
                        u"applied are asked about first)"_s);
    connect(mod_btn, &QPushButton::clicked, this, [this] {   // (Python: switch_mod)
        if (confirm_discard(u"mod"_s)) {
            switch_requested = true;
            close();
        }
    });
    lay->addWidget(mod_btn);
    mod_name_edit = new QLineEdit();
    mod_name_edit->setReadOnly(true);
    mod_name_edit->setMinimumWidth(180);
    mod_name_edit->setCursor(Qt::IBeamCursor);
    mod_name_edit->setToolTip(u"The mod's name, as the game's launcher shows it — double-click (or ✎) to rename it, "
                              u"e.g. to give it a new version number. Enter or a click elsewhere: rename; Esc: keep "
                              u"the old name"_s +
                              (sub ? u"\nA submod of %1: Apply writes only what differs from the parent into\n%2"_s.arg(
                                         sub->parents.join(u", "_s), sub->root)
                                   : u""_s));
    {
        QPixmap pen(18, 18);                         // (the ✎ glyph drawn: a text box anyone can see is editable)
        pen.fill(Qt::transparent);
        QPainter pp(&pen);
        QFont pf = pp.font();
        pf.setPixelSize(15);
        pp.setFont(pf);
        pp.setPen(QColor(theme::GOLD));
        pp.drawText(QRect(0, 0, 18, 18), Qt::AlignCenter, u"✎"_s);
        pp.end();
        QAction *ren = mod_name_edit->addAction(QIcon(pen), QLineEdit::TrailingPosition);
        ren->setToolTip(u"Rename the mod"_s);
        connect(ren, &QAction::triggered, this, [this] { begin_rename(); });
    }
    mod_name_edit->installEventFilter(this);
    connect(mod_name_edit, &QLineEdit::textEdited, this, [this](const QString &t) {
        const QString why = modrename::check_name(t);
        mod_name_note->setText(why.isEmpty() ? QString() : u"⚠ "_s + why);
        mod_name_note->setVisible(!why.isEmpty());
    });
    connect(mod_name_edit, &QLineEdit::editingFinished, this, [this] { end_rename(true); });
    lay->addWidget(mod_name_edit);
    mod_name_note = new QLabel();
    mod_name_note->setStyleSheet(u"color: #ff5050; font-weight: bold;"_s);
    mod_name_note->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mod_name_note->hide();
    lay->addWidget(mod_name_note);
    _show_mod_name();
    lay->addWidget(new QLabel(u"Scenario"_s));
    scenario_box = new QComboBox();
    scenario_box->setMinimumWidth(170);
    connect(scenario_box, &QComboBox::activated, this, [this](int idx) {   // (Python: on_scenario_chosen)
        const QString name = scenario_box->itemData(idx).toString();
        if (name == u"__new__") {
            refresh_scenarios();
            bool ok = false;
            const QString nw = QInputDialog::getText(this, u"New scenario"_s,
                                                     u"Name of the new start-date folder (e.g. 1861.1.1).\n"
                                                     u"It will be a copy of '%1'."_s.arg(doc->scenario),
                                                     QLineEdit::Normal, QString(), &ok);
            if (ok && !py::strip(nw).isEmpty()) {
                try {
                    mod->create_scenario(py::strip(nw), doc->scenario);
                } catch (const std::exception &e) {
                    message(u"⚠ New scenario: %1"_s.arg(QString::fromUtf8(e.what())), 12000);
                    return;
                }
                refresh_scenarios();
                if (confirm_discard(u"scenario"_s))
                    load_scenario(py::strip(nw));
            }
            return;
        }
        if (doc && name == doc->scenario)
            return;
        if (!confirm_discard(u"scenario"_s)) {
            refresh_scenarios();
            return;
        }
        load_scenario(name);
    });
    lay->addWidget(scenario_box);
    auto *pal_btn = new QPushButton(u"⌕ Ctrl+K"_s);
    pal_btn->setToolTip(u"Every function of the editor in one list: type a few letters, Enter — the editor goes "
                        u"there and frames the place"_s);
    connect(pal_btn, &QPushButton::clicked, this, [this] { open_palette(); });
    lay->addWidget(pal_btn);

    auto *seg = new QWidget();
    auto *sl = new QHBoxLayout(seg);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(0);
    view_group = new QButtonGroup(this);
    for (const auto &[key, text] : {std::pair{u"province"_s, u"Province view"_s}, {u"state"_s, u"State view"_s}}) {
        auto *b = new QPushButton(text);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(key == u"province"_s);
        const QString k = key;
        connect(b, &QPushButton::clicked, this, [this, k] { set_view_mode(k); });
        view_group->addButton(b);
        sl->addWidget(b);
    }
    // map mode, joined to the view buttons as one control (no separate "Colour" label)
    color_box = new modepanel::ModeCombo();        // (a click opens the wide panel of every map mode — modepanel)
    color_box->setObjectName(u"segCombo"_s);
    color_box->setToolTip(u"Map mode — a click shows every map mode, by category"_s);
    color_box->swatch = [this](const QString &m) { return _mode_swatch(m); };
    color_box->custom = [this](const QString &m) { return _mode_custom(m); };
    sl->addWidget(color_box);
    maplook::load(*settings);
    look_btn = new QPushButton(u"◐"_s);
    look_btn->setObjectName(u"seg"_s);
    look_btn->setFixedWidth(34);
    look_btn->setText(u""_s);                      // (the drawn mark: the ◐ glyph is tiny in this font)
    look_btn->setIcon(QIcon(modepanel::mark_pixmap(20)));
    look_btn->setToolTip(u"The colours of the map modes that show an amount (population, life rating, "
                         u"literacy …) — and the ranges of the map modes State size / Province size"_s);
    connect(look_btn, &QPushButton::clicked, this, [this] { open_map_look(); });
    sl->addWidget(look_btn);
    lay->addWidget(seg);

    more_modes = std::make_unique<mapmodes::MoreModes>(
        *mod, mapdata->n, [this](long long p) { return mapdata->neighbours(p); },
        [this](bool ensure) -> events::EventDB * {   // (the event panel's: read once, when a literacy event is
            if (!event_panel)                         //  assigned)
                return nullptr;
            if (ensure && doc)
                event_panel->ensure(doc.get());
            return event_panel->db.get();
        },
        [](double t) { return maplook::ramp(t); });
    fill_color_modes();                            // (the first page's modes — and the other pages', grouped)
    connect(color_box, &QComboBox::currentIndexChanged, this, [this](int i) { on_color_mode(i); });

    search = new QLineEdit();
    search->setPlaceholderText(u"Search province, state or country…  (Ctrl+F)"_s);
    search->setMinimumWidth(280);
    completer = new QCompleter(this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setMaxVisibleItems(15);
    search->setCompleter(completer);
    connect(completer, qOverload<const QString &>(&QCompleter::activated), this,
            [this](const QString &t) { run_search(t); });
    connect(search, &QLineEdit::returnPressed, this, [this] { run_search(search->text()); });
    lay->addWidget(search);
    // the problems of the page that is open: how many, and a click steps through them one by one
    here_btn = new QPushButton(u""_s);
    here_btn->setCursor(Qt::PointingHandCursor);
    here_btn->setToolTip(u"The problems the checks found on this page — click to go through them one by one "
                         u"(each is named in a line under this bar)"_s);
    here_btn->hide();
    connect(here_btn, &QPushButton::clicked, this, [this] { next_problem(); });
    lay->addWidget(here_btn);
    lay->addStretch(1);

    for (const auto &[text, tip] : {std::pair{u"↶"_s, u"Undo (Ctrl+Z)"_s}, {u"↷"_s, u"Redo (Ctrl+Y)"_s},
                                    {u"–"_s, u"Zoom out"_s}, {u"+"_s, u"Zoom in"_s}, {u"⤢"_s, u"Show whole map"_s}}) {
        auto *b = new QPushButton(text);
        b->setObjectName(u"icon"_s);
        b->setToolTip(tip);
        lay->addWidget(b);
        if (text == u"↶"_s) {             // (their tooltips say what the next undo / redo takes back — worked out when shown)
            _undo_btn = b;
            b->installEventFilter(this);
            connect(b, &QPushButton::clicked, this, [this] { undo(); });
        } else if (text == u"↷"_s) {
            _redo_btn = b;
            b->installEventFilter(this);
            connect(b, &QPushButton::clicked, this, [this] { redo(); });
        } else if (text == u"–"_s) {
            connect(b, &QPushButton::clicked, this, [this] { map->zoom_by(1 / 1.4, false); });
        } else if (text == u"+"_s) {
            connect(b, &QPushButton::clicked, this, [this] { map->zoom_by(1.4, false); });
        } else {
            connect(b, &QPushButton::clicked, this, [this] { map->fit(); });
        }
    }
    return bar;
}

QWidget *MainWindow::_build_tabs()                 // the navigation rail: every page under its group, see rail.h
{
    const QString s = settings->value(u"railSlim"_s, u"false"_s).toString().toLower();
    rail = new ::rail::NavRail(s == u"true"_s || s == u"1"_s);
    connect(rail, &::rail::NavRail::picked, this, [this](const QString &k) { go_page(k); });
    connect(rail, &::rail::NavRail::folded, this,
            [this](bool on) { settings->setValue(u"railSlim"_s, on ? u"true"_s : u"false"_s); });
    return rail;
}

std::optional<QString> MainWindow::rail_page() const   // the rail page open now (a tab and its sub-page)
{
    std::optional<QString> sub;
    if (tab == u"map"_s)
        sub = map_sub;
    else if (tab == u"provinces"_s)
        sub = prov_sub;
    else if (tab == u"armies"_s && army_panel)
        sub = army_panel->sub;
    return ::rail::page_of(tab, sub);
}

void MainWindow::_sync_rail()
{
    if (rail) {
        if (const auto p = rail_page())
            rail->set_current(*p);
        else
            rail->set_current(QString());
        _update_here();
    }
}

QWidget *MainWindow::_shown_page() const          // the page the side panel shows (the sub-page, where a page has several)
{
    QWidget *w = stack->currentWidget();
    for (widgets::PageStack *st : {map_stack, prov_stack})
        if (st->parentWidget() == w || (w && w->isAncestorOf(st)))
            return st->currentWidget();
    return w;
}

// Another page in the side panel: it opens where it was left (a page seen for the first time: at its top) — never
// scrolled past its end (a page is only as tall as its own content).
void MainWindow::_page_changed()
{
    QScrollBar *vb = side_scroll->verticalScrollBar();
    if (_page_shown)
        _page_scroll[_page_shown] = vb->value();
    QWidget *page = _shown_page();
    _page_shown = page;
    const auto it = _page_scroll.find(page);
    const int want = it == _page_scroll.end() ? 0 : it->second;
    _stick_bottom = false;
    vb->setValue(std::min(want, vb->maximum()));
    QTimer::singleShot(0, this, [this, page, want, vb] {
        if (page == _page_shown)
            vb->setValue(std::min(want, vb->maximum()));
    });
}

void MainWindow::open_war(const QString &rel)
{
    // Diplomacy → a war of the start: its page in Warfare.
    go_page(u"warfare"_s);
    war_ws->set_page(u"wars"_s);
    auto *v = static_cast<moreviews::WarsView *>(*war_ws->views.get(u"wars"_s));
    if (!rel.isEmpty()) {
        v->cur = rel;
        v->refresh(doc.get());
    }
    message(u"Warfare → Wars at the start."_s, 8000);
}

void MainWindow::go_page(const QString &key)       // a page of the rail was clicked: its tab, and the sub-page
{
    if (key == u"search"_s) {                      // the rail's last entry: the command palette (Ctrl+K), not a page
        open_palette();
        _sync_rail();
        return;
    }
    if (!::rail::PAGES.contains(key))
        return;
    const ::rail::Page page = ::rail::PAGES.value(key);
    const QString t = page.tab;
    const auto &sub = page.sub;
    if (t != tab) {                                // the sub-page first (quietly): the tab opens on it at once
        if (t == u"map"_s && sub) {
            map_sub = *sub;
            settings->setValue(u"mapSub"_s, *sub);
            for (auto &[k, b] : adj_sub_btns)
                b->setChecked(k == *sub);
            adj_seg_bar->setVisible(*sub == u"adj"_s || *sub == u"canal"_s);
        } else if (t == u"provinces"_s) {         // the page opens on the provinces (utility states: its link)
            prov_sub = sub.value_or(u"provinces"_s);
            settings->setValue(u"provSub"_s, prov_sub);
            util_edit = false;
            _prov_switch_text();
        } else if (t == u"armies"_s && sub && army_panel->sub != *sub) {
            army_panel->set_sub(*sub, false);
        }
        set_tab(t);
    } else if (t == u"map"_s && sub) {
        set_map_sub(*sub);
    } else if (t == u"provinces"_s && prov_sub != sub.value_or(u"provinces"_s)) {
        set_prov_sub(sub.value_or(u"provinces"_s));
    } else if (t == u"armies"_s && sub && army_panel->sub != *sub) {
        army_panel->set_sub(*sub);
    }
    _sync_rail();
}

void MainWindow::set_tab(const QString &t)
{
    if (t == tab)
        return;
    _jump_mark.reset();
    static const QSet<QString> PICKS = {u"split"_s, u"capital"_s, u"adjpick"_s, u"statepick"_s, u"armymove"_s,
                                        u"navymove"_s, u"nationcap"_s};
    if (PICKS.contains(mode))
        escape();
    if (tab != u"events" && map->isVisible() && _map_where == u"main" && t == u"events")
        _map_cams[u"main"_s] = _map_cam();         // the main view's camera
    const QString was = tab;
    tab = t;
    _sync_rail();
    _update_pend_bar();                            // (Pops: a change not applied yet, left behind)
    const bool full = FULL_SCREEN.contains(t);
    main_split->setVisible(!full);
    const bool ev_tab = t == u"events";
    script_ws->setVisible(ev_tab);
    script_ws->tab_shown(ev_tab);
    _place_map();
    if (!ev_tab)
        map->set_focus_layer({});
    if (t == u"events") {                          // its table needs room: the side panel at least 780 px
        const QList<int> sz = main_split->sizes();
        const int tot = sz.size() == 2 ? sz[0] + sz[1] : 0;
        if (sz.size() == 2 && sz[1] < 780 && tot > 1200) {
            _side_before = sz[1];
            main_split->setSizes({tot - 780, 780});
        }
    } else if (was == u"events" && _side_before && *_side_before) {
        const QList<int> sz = main_split->sizes();  // … and the other pages get the map's room back
        const int tot = sz.size() == 2 ? sz[0] + sz[1] : 0;
        if (sz.size() == 2 && tot > *_side_before)
            main_split->setSizes({tot - *_side_before, *_side_before});
        _side_before.reset();
    }
    tech_ws->setVisible(t == u"technology");
    prod_ws->setVisible(t == u"production");
    cl_ws->setVisible(t == u"changelog");
    pol_ws->setVisible(t == u"politics");
    war_ws->setVisible(t == u"warfare");
    rules_ws->setVisible(t == u"rules");
    lib_ws->setVisible(t == u"library");
    imp_ws->setVisible(t == u"import");
    size_ws->setVisible(t == u"mapsize");
    keys_ws->setVisible(t == u"shortcuts");
    sub_ws->setVisible(t == u"submods");
    ck_ws->setVisible(t == u"checksum");
    econ_ws->setVisible(t == u"econest");
    popinf_ws->setVisible(t == u"popinf");
    color_box->setEnabled(!full);
    fill_color_modes();
    map->shape_layer->setVisible(t == u"map" || t == u"errors" || t == u"armies" || t == u"diplomacy" || t == u"events");
    map->name_layer->setVisible(t == u"map" && map_sub == u"pos");
    _sync_map_tool();
    _show_banner();
    refresh_panel();
    render_map();
    update_overlay();
}

void MainWindow::fill_color_modes()
{
    Modes modes = COLOR_MODES_BY_TAB.at(tab);
    for (const auto &m : mapmodes::extra_modes(*mod, tab))
        modes.push_back(m);
    QStringList keys;
    for (const auto &[k, _t] : modes)
        keys << k;
    QString want = _tab_modes.contains(tab) && !_tab_modes[tab].isEmpty()
                       ? _tab_modes[tab]
                       : (keys.contains(u"political"_s) ? u"political"_s : modes[0].first);
    if (START_MODE.contains(tab))
        want = START_MODE.at(tab);                 // this tab always opens in the same map mode
    color_box->blockSignals(true);
    color_box->clear();
    // every group under a heading of its own — this page's too: the headings are drawn apart by heading_delegate
    static QStyledItemDelegate *delegate = nullptr;
    if (!color_box->property("hasDelegate").toBool()) {
        delegate = widgets::heading_delegate(color_box);
        color_box->setItemDelegate(delegate);
        color_box->setProperty("hasDelegate", true);
    }
    auto *model = qobject_cast<QStandardItemModel *>(color_box->model());
    auto heading = [&](const QString &text, const QString &t) {
        color_box->addItem(text);
        QStandardItem *head = model->item(color_box->count() - 1);
        head->setEnabled(false);                   // (a heading: not a mode)
        QString page;                              // its page (the panel's icon and colour of the category)
        if (t == tab && rail)
            page = rail_page().value_or(QString());
        else
            page = ::rail::page_of(t, t == u"map" ? std::optional<QString>(u"draw"_s) : std::nullopt).value_or(t);
        head->setData(page, widgets::HEAD_ROLE);
    };
    auto page_name = [](const QString &t) {
        const QString k = t == u"map" ? u"draw"_s : t;
        return ::rail::PAGE_NAMES.value(k, t);
    };
    const QString here = rail ? ::rail::PAGE_NAMES.value(rail_page().value_or(QString()), page_name(tab)) : page_name(tab);
    heading(u"This page — %1"_s.arg(here), tab);
    for (const auto &[key, text] : modes)
        color_box->addItem(text, key);             // (indented under its heading by the delegate)
    // the map modes of the other pages too (the user: not only where they belong), each page's under its name
    QSet<QString> seen(keys.begin(), keys.end());
    for (const QString &other : MAP_PAGES) {
        if (other == tab)
            continue;
        Modes more;
        Modes all = COLOR_MODES_BY_TAB.count(other) ? COLOR_MODES_BY_TAB.at(other) : Modes();
        for (const auto &m : mapmodes::extra_modes(*mod, other))
            all.push_back(m);
        for (const auto &[k, t] : all)
            if (!seen.contains(k) && !PAGE_BOUND_MODES.contains(k))
                more.emplace_back(k, t);
        if (more.empty())
            continue;
        heading(u"From %1"_s.arg(page_name(other)), other);
        for (const auto &[k, t] : more) {
            color_box->addItem(t, k);
            seen.insert(k);
        }
    }
    const int i = color_box->findData(want);
    color_box->setCurrentIndex(i >= 0 ? i : 1);   // (row 0 is the heading)
    color_mode = color_box->currentData().toString();
    color_box->blockSignals(false);
}

QWidget *MainWindow::_build_statusbar()
{
    auto *bar = new QWidget();
    bar->setObjectName(u"statusbar"_s);
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(14, 5, 14, 5);
    lay->setSpacing(20);
    st_cursor = new QLabel(u"Cursor: —"_s);
    // long texts are cut with "…" (whole text as tooltip) — a long message must never widen the window
    st_hover = new widgets::ElideLabel(u"Hovered: —"_s);
    st_msg = new widgets::ElideLabel(u""_s);
    for (const auto &[w, stretch] : {std::pair<QLabel *, int>{st_cursor, 0}, {st_hover, 2}, {st_msg, 3}}) {
        w->setObjectName(u"status"_s);
        lay->addWidget(w, stretch);
    }
    st_hover->setMinimumWidth(420);
    st_pending = new QLabel(u"Pending edits: 0"_s);
    st_pending->setObjectName(u"pending"_s);
    lay->addWidget(st_pending);
    err_badge = new QPushButton(u""_s);
    err_badge->setObjectName(u"errBadge"_s);
    err_badge->setCursor(Qt::PointingHandCursor);
    connect(err_badge, &QPushButton::clicked, this, [this] { set_tab(u"errors"_s); });
    err_badge->hide();
    // an Apply as a whole: undone / redone from the copies kept of the files it changed (applyjournal.py)
    undo_apply_btn = new QPushButton(u"↶ Apply"_s);
    connect(undo_apply_btn, &QPushButton::clicked, this, [this] { undo_apply(); });
    undo_apply_btn->hide();
    lay->addWidget(undo_apply_btn);
    redo_apply_btn = new QPushButton(u"↷ Apply"_s);
    connect(redo_apply_btn, &QPushButton::clicked, this, [this] { redo_apply(); });
    redo_apply_btn->hide();
    lay->addWidget(redo_apply_btn);
    revert_btn = new QPushButton(u"Revert all"_s);
    connect(revert_btn, &QPushButton::clicked, this, [this] { revert(); });
    lay->addWidget(revert_btn);
    by_file_btn = new QPushButton(u"By file ▾"_s);
    by_file_btn->setToolTip(u"The pending edits file by file: Apply or revert each on its own"_s);
    connect(by_file_btn, &QPushButton::clicked, this, [this] { open_pending_panel(); });
    lay->addWidget(by_file_btn);
    lay->addWidget(err_badge);                     // right next to Apply
    // C++ only (the user 2026-11-13: "I don't ever want to risk someone breaking their mod on accident"): before the
    // first write of this run the untouched mod is copied next to it, with its own launcher entry (modcopy)
    keep_cb = new QCheckBox(u"Keep a copy of the original mod"_s);
    keep_cb->setChecked(settings->value(u"keepOriginal"_s, true).toBool());
    keep_cb->setToolTip(u"On: before the editor writes into this mod for the first time (this run), the whole mod "
                        u"folder is copied INTO it, to \"ModEditor safeguards\\<folder> (original <date>)\" (the game "
                        u"never reads that folder), with its own .mod file, so the game's launcher lists the untouched "
                        u"mod beside the edited one. The opening window lists the copies under the mod (Safeguards). "
                        u"The edited mod keeps its name and folder.\nOff: the edits are written into the mod with no "
                        u"copy."_s);
    connect(keep_cb, &QCheckBox::toggled, this, [this](bool on) {
        settings->setValue(u"keepOriginal"_s, on);
        if (!on)
            message(u"⚠ No copy of the original mod will be kept — Apply writes straight into the mod."_s, 10000);
    });
    lay->addWidget(keep_cb);
    // C++ only (the user 2026-11-15: "with every mod comes an almanac if the box is checked"): on by default and
    // remembered. (A check's fresh settings file — MODEDITOR_SETTINGS — starts with it off: the checks' mods get none.)
    almanac_cb = new QCheckBox(u"Almanac with every Apply"_s);
    almanac_cb->setChecked(
        settings->value(u"almanacOnApply"_s, qEnvironmentVariable("MODEDITOR_SETTINGS").isEmpty()).toBool());
    almanac_cb->setToolTip(u"On: every Apply also writes \"<mod name> almanac.html\" into the mod's own folder — the "
                           u"stats of every army and navy unit, the war goals, the government forms, the national "
                           u"values and the technology schools as tables, to open in any browser. The game does not "
                           u"read it and it does not change the checksum.\nOff: no almanac. (Warfare → Almanacs shows"
                           u"it and writes it without an Apply.)"_s);
    connect(almanac_cb, &QCheckBox::toggled, this, [this](bool on) { settings->setValue(u"almanacOnApply"_s, on); });
    lay->addWidget(almanac_cb);
    apply_btn = new QPushButton(u"Apply  (Ctrl+S)"_s);
    apply_btn->setObjectName(u"apply"_s);
    connect(apply_btn, &QPushButton::clicked, this, [this] { save(); });
    apply_btn->installEventFilter(this);           // its tooltip: what Apply will write (worked out on hover)
    lay->addWidget(apply_btn);
    return bar;
}

void MainWindow::message(const QString &text, int ms, bool error)
{
    const bool err = error || py::lstrip(text).startsWith(u"⚠"_s);
    const QString obj = err ? u"statusErr"_s : u"status"_s;
    if (st_msg->objectName() != obj) {
        st_msg->setObjectName(obj);
        st_msg->style()->unpolish(st_msg);
        st_msg->style()->polish(st_msg);
    }
    st_msg->setText(text);
    if (ms)
        QTimer::singleShot(ms, this, [this, text] {
            if (st_msg->text() == text)
                st_msg->setText(u""_s);
        });
}

// ---------------------------------------------------------------------------------------------- the scenario
void MainWindow::refresh_scenarios()
{
    scenario_box->blockSignals(true);
    scenario_box->clear();
    for (const auto &s : mod->scenarios()) {
        const QString extra = s.bookmark && !s.bookmark->isEmpty() ? u" — %1"_s.arg(*s.bookmark) : u""_s;
        scenario_box->addItem(u"%1  (%2 files)%3"_s.arg(s.name).arg(s.files).arg(extra), s.name);
    }
    scenario_box->addItem(u"＋ New scenario (copy of current)…"_s, u"__new__"_s);
    if (doc) {
        const int i = scenario_box->findData(doc->scenario);
        scenario_box->setCurrentIndex(std::max(0, i));
    }
    scenario_box->blockSignals(false);
}

void MainWindow::_discard_shared(document::Document &d)
{
    // Unsaved edits that live in data shared between scenarios are put back before another document is made:
    // painted pixels, terrain / rivers, new and deleted provinces and state membership.
    newprov::ProvinceDefs *defs = d.maps.contains(u"definitions"_s) ? &d.map<newprov::ProvinceDefs>(u"definitions"_s)
                                                                    : nullptr;
    if (defs && !defs->gone.isEmpty())              // provinces deleted but not written: back (before the pixels)
        defs->restore(store::box(newprov::DefsSnap{defs->added, {}}));
    for (const QString k : {u"terrain_bmp"_s, u"rivers_bmp"_s})
        if (d.maps.contains(k))
            d.map<layerbmp::IndexedBitmap>(k).revert_unsaved();
    if (d.maps.contains(u"provinces_bmp"_s)) {
        auto &old = d.map<provbmp::ProvinceBitmap>(u"provinces_bmp"_s);
        if (old.version != old.saved) {
            // unsaved painting is discarded: put the pixels back to how the file has them
            if (old.saved >= 0)
                old.restore_to(old.saved);
            else
                old.reload_file();
        }
    }
    for (const auto &[rid, order] : d.region_orig) {
        moddata::Region *r = mod->regions.get(rid);
        if (!r)
            r = mod->utility.get(rid);
        if (r)
            r->provinces = order;
    }
    if (defs && !defs->added.isEmpty())
        defs->restore(store::box(newprov::DefsSnap{}));
    if (d.maps.contains(u"hist_new"_s)) {
        auto &nh = d.map<history::NewHistFiles>(u"hist_new"_s);
        if (!nh.made.isEmpty())
            nh.restore({});                       // empty history files not written yet
    }
    const auto empty_items = [] { return std::make_shared<const store::ItemsSnap>(store::Items{}, true); };
    if (d.maps.contains(u"loc"_s)) {
        auto &loc = d.map<localisation::LocEdits>(u"loc"_s);
        if (!loc.fresh.isEmpty())
            loc.restore(empty_items());           // names not written yet: back to the old ones
    }
    if (d.maps.contains(u"new_nations"_s)) {
        auto &nn = d.map<countryhist::NewNations>(u"new_nations"_s);
        if (!nn.added.isEmpty())
            nn.restore(empty_items());            // nations made in the editor but not written
    }
    if (d.maps.contains(u"utility"_s)) {
        auto &um = d.map<document::UtilityMarks>(u"utility"_s);
        if (!um.dirty().isEmpty())
            um.restore(store::box(um.saved));     // utility marks that weren't applied
    }
    d._sync_regions();
}

void MainWindow::load_scenario(QString scen)   // (by value: a caller may pass doc->scenario, freed inside)
{
    if (doc)
        _discard_shared(*doc);
    chk_panel->doc = nullptr;
    more_modes->doc = nullptr;
    if (event_panel)
        event_panel->refresh(nullptr);            // (C++: the panel lets go of the old document first)
    landsea.reset();
    doc.reset();
    doc = std::make_shared<document::Document>(*mod, scen);
    document::Document &d = *doc;
    d.coastal = std::vector<bool>(mapdata->coastal.begin(), mapdata->coastal.end());
    d.maps.insert(u"positions"_s, std::make_shared<positions::PositionsFile>(*mod, mapdata->h));
    auto pb = std::make_shared<provbmp::ProvinceBitmap>(*mod, *mapdata);
    pb->listeners.push_back([this](const provbmp::Box &box, const provbmp::Ids &ids) { _pixels_changed(box, ids); });
    d.maps.insert(u"provinces_bmp"_s, pb);
    auto defs = std::make_shared<newprov::ProvinceDefs>(*mod, *mapdata, *d.hist);
    defs->listeners.push_back([this] { _defs_changed(); });
    d.maps.insert(u"definitions"_s, defs);
    auto tb = std::make_shared<layerbmp::IndexedBitmap>(*mod, *mapdata, u"terrain.bmp"_s, u"terrain_bmp"_s);
    auto rb = std::make_shared<layerbmp::IndexedBitmap>(*mod, *mapdata, u"rivers.bmp"_s, u"rivers_bmp"_s);
    d.maps.insert(u"terrain_bmp"_s, tb);
    d.maps.insert(u"rivers_bmp"_s, rb);
    d.maps.insert(u"river_plan"_s, std::make_shared<riverpanel::RiverPlan>());
    d.maps.insert(u"islands"_s, std::make_shared<store::StoreOf<history::IslandFlags>>(*d.hist));
    d.maps.insert(u"hist_new"_s, std::make_shared<store::StoreOf<history::NewHistFiles>>(*d.hist));
    d.maps.insert(u"loc"_s, std::make_shared<localisation::LocEdits>(*mod));
    d.maps.insert(u"flags"_s, std::make_shared<nationextras::FlagEdits>(*mod));
    d.maps.insert(u"assets"_s, std::make_shared<moreviews::AssetEdits>(*mod));   // pictures and music added
    // nations: the game data and the history files are shared by every scenario of the mod
    if (!mod->nation_data) {
        mod->nation_data = std::make_shared<countryhist::NationData>(*mod);
        mod->country_hist = std::make_shared<countryhist::CountryHistory>(*mod, *mod->nation_data);
    }
    auto nn = std::make_shared<countryhist::NewNations>(*mod, *mod->country_hist);
    auto ne = std::make_shared<countryhist::NationEdits>(*mod->country_hist, d.date);
    nn->nations = ne.get();
    d.maps.insert(u"new_nations"_s, nn);
    d.maps.insert(u"nations"_s, ne);
    d.maps.insert(u"oob"_s, std::make_shared<oob::OOBEdits>(*mod, *mod->country_hist, d.date));
    d.maps.insert(u"diplomacy"_s, std::make_shared<diplomacy::DiploEdits>(*mod, d.date));
    auto store = std::make_shared<scriptstore::ScriptStore>(*mod);   // the script files as the editor has them
    d.maps.insert(u"scripts"_s, store);
    d.maps.insert(u"event_cats"_s, std::make_shared<events::EventCats>(*mod, *store));
    d.maps.insert(u"event_mods"_s, std::make_shared<modifiers::ModifierEdits>(*mod, *store));
    if (!tech_db)
        tech_db = std::make_unique<techdata::TechDB>(*mod);
    d.maps.insert(u"techs"_s, std::make_shared<techdata::TechEdits>(*mod, *tech_db));
    if (!prod_db)
        prod_db = std::make_unique<production::ProdDB>(*mod);
    auto prod = std::make_shared<production::ProdEdits>(*mod, *prod_db);
    prod->store = store.get();                    // (the pop type files: one writer)
    d.maps.insert(u"production"_s, prod);
    d.maps.insert(u"utility"_s, std::make_shared<document::UtilityMarks>(*mod, d));
    pending_script = std::make_unique<document::PendingScripts>(d);
    pending_script->on_sync = [this] {
        localisation::LocEdits *loc = doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
        importer::sync_cultures(*mod, doc->map<scriptstore::ScriptStore>(u"scripts"_s), loc);
    };
    mod->pending_script = pending_script.get();   // new cultures / religions wait for Apply too
    d.maps.insert(u"owners_top"_s, std::make_shared<history::OwnersOnTop>(d));
    d.maps.insert(u"start_blocks"_s, std::make_shared<history::StartBlocks>(d, *mod));
    tb->listeners.push_back([this](const layerbmp::Box &box) { _layer_pixels_changed(u"terrain"_s, box); });
    rb->listeners.push_back([this](const layerbmp::Box &box) { _layer_pixels_changed(u"rivers"_s, box); });
    _layer_views.clear();
    landsea = std::make_unique<layerbmp::LandSea>(*mod, *mapdata, *tb, *rb);
    chk_panel->landsea = landsea.get();
    draw_panel->landsea = landsea.get();
    d.listeners.push_back([this](const QSet<long long> &) { on_doc_changed(); });
    _cl_doc = nullptr;                            // (a new document — it may sit where the old one did)
    _cl_edits();                                  // (the changelog watches this document's edits from the start)
    // typing in the script texts: each burst an undo step of its own; Ctrl+Z in a text is the editor's one undo
    store->undo_hook = std::make_pair(std::function<void()>([this] { undo(); }),
                                      std::function<void()>([this] { redo(); }));
    store->listeners.push_back([this](const QString &rel) { _script_typed(rel); });
    store->listeners.push_back([this](const QString &rel) {    // (C++ only: new province buildings)
        if (rel == buildtypes::BUILDINGS)
            QTimer::singleShot(0, this, [this] { _buildings_changed(); });
    });
    d.track_live(u"scripts"_s);
    journal = std::make_unique<applyjournal::ApplyJournal>(backup_root, mod->root, mod->base);   // every Apply kept whole
    chk_panel->backup_root = backup_root;         // (the Errors tab's stray-folder fix keeps it there)
    more_modes->doc = &d;
    _undo_armed = nullptr;
    _refresh_apply_undo();
    // unapplied edits are never lost (recovery.py): a file of them, written as they change; one left from the last
    // session is offered back — set aside first, so that the edits made now can't overwrite it before it is answered
    _rec_path = recovery::path_for(backup_root, mod->root, scen);
    _rec_sig.reset();
    _rec_offer.reset();
    const QString offered = _rec_path.left(_rec_path.lastIndexOf(u'.')) + u".offered"_s +
                            _rec_path.mid(_rec_path.lastIndexOf(u'.'));
    if (_rec_internal) {
        // (per-file Apply / Revert reverts inside: no offer)
    } else if (QFileInfo::exists(_rec_path)) {
        if (QFile::exists(offered))
            QFile::remove(offered);
        if (QFile::rename(_rec_path, offered))
            _rec_offer = offered;
    } else if (QFileInfo::exists(offered)) {
        _rec_offer = offered;
    }
    QTimer::singleShot(0, this, [this] { _offer_recovery(); });
    refresh_scenarios();
    setWindowTitle(u"ModEditor " MODEDITOR_VERSION_TEXT " — %1 — %2"_s.arg(mod->name(), scen));
    std::vector<long long> keep;
    for (long long p : selection)
        if (mod->provinces.contains(p))
            keep.push_back(p);
    selection = keep;
    _build_search_index();
    std::vector<long long> caps;
    for (const auto &[rid, _r] : mod->regions)
        if (const auto c = d.capital(rid))
            caps.push_back(*c);
    map->set_capitals(caps);
    map->set_show_stars(view_mode == u"state");
    render_map();
    refresh_panel();
    update_pending();
    schedule_checks();
    if (!d.problems.isEmpty())
        message(u"%1 problem(s) while reading pop files, first: %2"_s.arg(d.problems.size()).arg(d.problems[0]), 15000);
}

// ---------------------------------------------------------------------------------------------- search
void MainWindow::_build_search_index()
{
    search_map = {};
    QStringList items;
    std::vector<long long> pids;
    for (const auto &[pid, _pr] : mod->provinces)
        pids.push_back(pid);
    std::sort(pids.begin(), pids.end());
    for (long long pid : pids) {
        const moddata::Province &pr = *mod->provinces.get(pid);
        if (pr.sea)
            continue;
        QString s = u"%1 — %2"_s.arg(pid).arg(pr.name);
        if (pr.region && !pr.region->isEmpty())
            s += u"  [%1]"_s.arg(*pr.region);
        items << s;
        search_map.insert(s, SearchHit{u'p', pid, QString()});
    }
    std::vector<QString> rids;
    for (const auto &[rid, _r] : mod->regions)
        rids.push_back(rid);
    std::sort(rids.begin(), rids.end());
    for (const QString &rid : rids) {
        const moddata::Region &r = *mod->regions.get(rid);
        const QString s = u"%1 — state%2 (%3 provinces)"_s.arg(rid, r.name == rid ? QString() : u" "_s + r.name)
                              .arg(r.provinces.size());
        items << s;
        search_map.insert(s, SearchHit{u'r', 0, rid});
    }
    std::map<QString, long long> owners;           // (sorted by tag; a count is all that is shown)
    for (const auto &[pid, pr] : mod->provinces)
        if (pr.owner && !pr.owner->isEmpty())
            ++owners[*pr.owner];
    for (const auto &[tag, n] : owners) {
        const moddata::Country *c = mod->countries.get(tag);
        const QString s = u"%1 — %2 (country, %3 provinces)"_s.arg(tag, c ? c->name : tag).arg(n);
        items << s;
        search_map.insert(s, SearchHit{u'c', 0, tag});
    }
    completer->setModel(new QStringListModel(items, completer));
}

void MainWindow::run_search(const QString &text_)
{
    const QString text = py::strip(text_);
    if (text.isEmpty())
        return;
    std::optional<SearchHit> hit;
    if (const SearchHit *h = search_map.get(text))
        hit = *h;
    if (!hit) {
        bool ok = false;
        const long long n = text.toLongLong(&ok);
        if (py::isdigit(text) && ok && mod->provinces.contains(n))
            hit = SearchHit{u'p', n, QString()};
        else if (mod->regions.contains(text))
            hit = SearchHit{u'r', 0, text};
        else if (mod->countries.contains(text.toUpper()))
            hit = SearchHit{u'c', 0, text.toUpper()};
        else {
            const QString low = text.toLower();
            for (const auto &[k, v] : search_map)
                if (k.toLower().contains(low)) {
                    hit = v;
                    break;
                }
        }
    }
    if (!hit) {
        message(u"Nothing found for '%1'"_s.arg(text));
        return;
    }
    std::vector<long long> pids;
    if (hit->kind == u'p')
        pids = {hit->pid};
    else if (hit->kind == u'r')
        pids = mod->regions.get(hit->key)->provinces;
    else
        for (const auto &[pid, pr] : mod->provinces)
            if (pr.owner == hit->key)
                pids.push_back(pid);
    if (mode == u"split") {
        for (long long pid : pids)
            if (pid != split_panel->source)
                split_panel->toggle_target(pid);
    } else {
        set_selection(pids);
    }
    if (const auto box = mapdata->bbox(pids)) {
        const auto &b = *box;
        map->fitInView(double(b[0] - 40), double(b[1] - 40), double(b[2] - b[0] + 80), double(b[3] - b[1] + 80),
                       Qt::KeepAspectRatio);
        if (map->transform().m11() > 6)
            map->zoom_by(6 / map->transform().m11(), false);
        map->_zoomed();
    }
    search->clear();
}

// ---------------------------------------------------------------------------------------------- view / colours
void MainWindow::set_view_mode(const QString &m)
{
    view_mode = m;
    for (QAbstractButton *b : view_group->buttons())
        b->setChecked(b->text().toLower().startsWith(m));
    map->set_show_stars(m == u"state");
    if (m == u"state" && !selection.empty() && mode == u"edit") {
        std::set<QString> rids;
        for (long long p : selection)
            if (const auto *pr = mod->provinces.get(p); pr && pr->region && !pr->region->isEmpty())
                rids.insert(*pr->region);
        std::vector<long long> ps;
        for (const QString &rid : rids)
            for (long long p : mod->regions.get(rid)->provinces)
                ps.push_back(p);
        set_selection(ps, false);
    }
    render_map();
    refresh_panel();
}

std::optional<QString> MainWindow::_mode_custom(const QString &m) const
{
    // "ranges" / "own" when a map mode has colours of its own (◐), else None — the map-mode panel marks it
    if (m == u"statesize" || m == u"provsize")
        return u"ranges"_s;
    if (maplook::uses_ranges(m))
        return u"ranges"_s;
    const maplook::Scale *s = maplook::SCALES.get(m);
    if (s && s->own && !s->own->empty())
        return u"own"_s;
    return std::nullopt;
}

std::optional<modepanel::Swatch> MainWindow::_mode_swatch(const QString &m)
{
    // what a map mode will look like, small: a gradient or a few of its colours (the map-mode panel)
    modepanel::Swatch sw;
    auto colours = [](const maplook::Ranges &rs) {
        modepanel::Swatch s;
        for (const auto &r : rs)
            s.colours.push_back(r.rgb);
        return s;
    };
    if (m == u"statesize" || m == u"provsize") {
        const QString k = m == u"provsize" ? u"prov_people"_s : maplook::state_kind();
        return colours(maplook::SIZE.kinds[k]);
    }
    if (maplook::has_scale(m)) {
        if (maplook::uses_ranges(m))
            return colours(maplook::SCALES.get(m)->ranges.value_or(maplook::Ranges()));
        const maplook::Scale *s = maplook::SCALES.get(m);
        sw.grad = true;
        if (s && s->own && !s->own->empty())
            sw.stops = *s->own;
        else if (m == u"relvalues")
            sw.stops = {{0.0, {214, 58, 48}}, {0.5, {120, 122, 128}}, {1.0, {64, 190, 88}}};
        else
            sw.stops = maplook::STOPS;
        return sw;
    }
    static std::map<QString, std::optional<modepanel::Swatch>> cache;
    if (const auto it = cache.find(m); it != cache.end())
        return it->second;
    auto first = [](const auto &table, int n = 5) {
        modepanel::Swatch s;
        for (const auto &[_k, v] : table) {
            if (std::find(s.colours.begin(), s.colours.end(), v.color) == s.colours.end())
                s.colours.push_back(v.color);
            if (int(s.colours.size()) >= n)
                break;
        }
        return s;
    };
    auto list = [](std::vector<Rgb> cs) {
        modepanel::Swatch s;
        s.colours = std::move(cs);
        return s;
    };
    std::vector<std::pair<QString, long long>> owned;
    for (const auto &[k, _c] : mod->countries) {
        long long n = 0;
        for (const auto &[_p, pr] : mod->provinces)
            if (pr.owner == OptStr(k))
                n += 1;
        owned.emplace_back(k, n);
    }
    std::stable_sort(owned.begin(), owned.end(), [](const auto &a, const auto &b) { return -a.second < -b.second; });
    std::vector<Rgb> nations;
    for (size_t i = 0; i < std::min<size_t>(owned.size(), 5); ++i)
        nations.push_back(mod->countries.get(owned[i].first)->color);
    std::vector<Rgb> states, conts;
    for (const auto &[rid, _r] : mod->regions) {
        if (states.size() >= 5)
            break;
        states.push_back(hashed_color(rid));
    }
    for (const QString &n : {u"a"_s, u"b"_s, u"c"_s, u"d"_s})
        conts.push_back(hashed_color(n, 0.45, 0.7));
    std::optional<modepanel::Swatch> out;
    if (m == u"political" || m == u"controller" || m == u"mixedowners" || m == u"military")
        out = list(nations);
    else if (m == u"culture" || m == u"culture_mix")
        out = first(mod->cultures);
    else if (m == u"religion" || m == u"religion_mix")
        out = first(mod->religions);
    else if (m == u"rgo")
        out = first(mod->goods);
    else if (m == u"states")
        out = list(states);
    else if (m == u"climate")
        out = list(std::vector<Rgb>(CLIMATE_COLORS.begin(), CLIMATE_COLORS.begin() + 5));
    else if (m == u"continent")
        out = list(conts);
    else if (m == u"colonial")
        out = list({{60, 70, 62}, {190, 150, 80}, {215, 110, 60}});
    else if (m == u"cores")
        out = list({{201, 161, 90}, {160, 135, 95}, {70, 58, 58}});
    else if (m == u"splitstates")
        out = list({{120, 88, 50}, {245, 150, 40}});
    else if (m == u"plain")
        out = list({{86, 90, 96}, mapview::SEA_RGB, {70, 140, 235}});
    else if (m == u"buildings")
        out = list({{201, 161, 90}, {214, 84, 72}, {80, 130, 220}, {165, 100, 200}});
    else if (m == u"status")
        out = list({{201, 161, 90}, {225, 200, 70}, {50, 170, 160}, {80, 130, 220}, {165, 100, 200}});
    else if (m == u"spheres")
        out = list({{50, 170, 160}, {225, 200, 70}, {120, 122, 128}});
    else if (m == u"subjects")
        out = list({{225, 200, 70}, {165, 100, 200}, {120, 122, 128}});
    else if (m == u"alliances")
        out = list({{80, 130, 220}, {120, 122, 128}});
    else if (m == u"errors")
        out = list({{255, 77, 77}, {240, 170, 60}, {120, 122, 128}});
    cache[m] = out;
    return out;
}

OrderedMap<QString, double> MainWindow::state_sizes()
{
    // {state: its size as the State size map mode counts it (provinces or people at the start)}
    OrderedMap<QString, double> out;
    for (const auto &[rid, r] : mod->regions) {
        std::vector<long long> ps;
        for (long long p : r.provinces)
            if (const auto *pr = mod->provinces.get(p); pr && !pr->sea)
                ps.push_back(p);
        if (ps.empty())
            continue;
        if (maplook::SIZE.by == u"provinces") {
            out.insert(rid, double(ps.size()));
        } else {
            long long s = 0;
            for (long long p : ps)
                if (const auto *pops = doc->pops.get(p))
                    for (const auto &pp : *pops)
                        s += pp.size;
            out.insert(rid, double(s));
        }
    }
    return out;
}

void MainWindow::open_map_look()
{
    // the colour scale of the map modes / the State size settings, under the ◐ button
    const QString cm = color_mode;
    auto *pop = look_pop = new maplook::LookPopup(cm, this, [this, cm] { return mode_values(cm); });
    connect(pop, &maplook::LookPopup::changed, this, [this] { _map_look_changed(); });
    pop->adjustSize();
    const QPoint g = look_btn->mapToGlobal(look_btn->rect().bottomLeft());
    pop->move(g.x() - 200, g.y() + 4);
    pop->show();
}

void MainWindow::_map_look_changed()
{
    maplook::save(*settings);
    render_map();
    _size_legend();
}

std::vector<double> MainWindow::mode_values(const QString &m)
{
    // the values a map mode shows on the map now, in its own unit (◐: "Ranges from the map")
    if (mapmodes::handles(m)) {
        std::vector<long long> land;
        for (const auto &[p, pr] : mod->provinces)
            if (!pr.sea && p < mapdata->n)
                land.push_back(p);
        return more_modes->values(m, land);
    }
    if (!_mode_vals.contains(m))
        color_table();
    const auto it = _mode_vals.find(m);
    return it == _mode_vals.end() ? std::vector<double>{} : it->second;
}

void MainWindow::_size_legend()
{
    if (color_mode == u"statesize" || color_mode == u"provsize")
        message(maplook::size_legend(color_mode == u"provsize" ? u"prov_people"_s : maplook::state_kind()) +
                    u"   (◐ sets the ranges)"_s,
                15000);
    else if (maplook::uses_ranges(color_mode))
        message(maplook::legend(color_mode) + u"   (◐ sets the colours)"_s, 15000);
}

void MainWindow::on_color_mode(int idx)
{
    color_mode = color_box->itemData(idx).toString();
    _tab_modes[tab] = color_mode;                 // (for this session only: the editor opens in Owner)
    render_map();
    _size_legend();
    _show_banner();
    if (tab == u"diplomacy") {
        diplo_panel->sync_mode(color_mode);
        update_overlay();
    }
}

void MainWindow::_diplo_view(const QString &key)
{
    // Diplomacy: Relations | Status in the panel picks the map mode that goes with it
    const int i = color_box->findData(diplopanel::DiploPanel::MODE.value(key));
    if (i >= 0 && i != color_box->currentIndex())
        color_box->setCurrentIndex(i);              // → on_color_mode
    else
        update_overlay();
}

void MainWindow::_place_map()
{
    // The map: in the Events tab's small window over the text, docked under the text, or (other tabs) the main view.
    scriptedit::ScriptWorkspace *ws = script_ws;
    if (tab == u"events" && !ws->map_visible()) {
        map->setVisible(false);                    // not shown on the Events tab: it stays where it is
        dip_view->setVisible(false);
        return;
    }
    const bool mini = tab == u"events" && ws->map_visible() && ws->map_mode == u"mini";
    const QString where = mini ? u"mini"_s : (tab == u"events" ? u"dock"_s : u"main"_s);
    if (_map_where != where) {
        // each place keeps its own camera: the small window's zoom never leaks into the other tabs
        const QString old = _map_where;
        if (old != u"main" && map->isVisible())   // leaving the small window / the dock: kept for next time
            _map_cams[old] = _map_cam();
        _map_where = where;
        if (mini)
            ws->mini->put_map(map);
        else
            left_split->insertWidget(1, map);
        const auto cam = _map_cams.find(where);
        if (cam != _map_cams.end()) {
            const auto c = cam->second;
            QTimer::singleShot(0, this, [this, c] {
                map->setTransform(c.first);
                map->centerOn(c.second);
                map->_zoomed();
            });
        } else {                                   // the first time here: the whole map, fitted to the view
            QTimer::singleShot(60, this, [this] {
                map->fit();
                map->_zoomed();
            });
        }
    } else if (!mini && map->parent() != left_split) {
        left_split->insertWidget(1, map);
        QTimer::singleShot(60, this, [this] {
            map->fit();
            map->_zoomed();
        });
    }
    const bool web = tab == u"diplomacy" && diplo_panel->shown == u"web";   // Diplomacy: the relations instead
    map->setVisible((tab != u"events" || ws->map_visible()) && !web);
    dip_view->setVisible(web);
    if (web) {
        dip_view->related = [this](const QString &t) { return diplo_panel->_related(t); };
        dip_view->show_for(doc.get(), diplo_panel->a, [this] { return diplo_panel->wars(); });
    }
}

std::pair<QTransform, QPointF> MainWindow::_map_cam() const
{
    return {map->transform(), map->mapToScene(map->viewport()->rect().center())};
}

std::vector<mapview::Shape> MainWindow::_jump_shapes() const
{
    // The yellow ring on the spot an Errors-tab double-click jumped to (until the next edit / tab change).
    if (!_jump_mark || (tab != u"map" && tab != u"events"))
        return {};
    mapview::Shape s;
    s.kind = u"circle"_s;
    s.pts = {_jump_mark->pt};
    s.radius = _jump_mark->r;
    s.width = 3.5;
    s.color = _jump_mark->rgb;
    return {s};
}

std::optional<QString> MainWindow::draw_layer() const
{
    // "terrain" / "rivers" while Drawing shows one of those bitmaps (or the Errors tab shows a river problem)
    if (tab == u"map" && map_sub == u"draw") {
        const QString lay = draw_panel->layer();
        return lay != u"prov" ? std::optional<QString>(lay) : std::nullopt;
    }
    if (tab == u"errors") {
        // a pixel problem of terrain.bmp / rivers.bmp is shown on that bitmap
        const auto k = chk_panel->current_key();
        if (k == u"rivers" || k == u"landsea_rivers")
            return u"rivers"_s;
        if (k == u"landsea_terrain")
            return u"terrain"_s;
    }
    return std::nullopt;
}

std::shared_ptr<mapview::PixelLayer> MainWindow::layer_view()
{
    const auto lay = draw_layer();
    if (!lay)
        return nullptr;
    if (*lay == u"colours") {                     // (C++ only: the colour maps, read through the Colours box)
        auto it = _layer_views.find(*lay);
        if (it == _layer_views.end())
            it = _layer_views.insert_or_assign(*lay, std::make_pair(nullptr, std::make_shared<colormappanel::ColormapView>(
                                                                                  *mapdata, draw_panel->colour_box))).first;
        return it->second.second;
    }
    auto &bmp = doc->map<layerbmp::IndexedBitmap>(*lay == u"terrain" ? u"terrain_bmp"_s : u"rivers_bmp"_s);
    if (!bmp.load()) {
        message(u"⚠ %1"_s.arg(bmp.error.value_or(QString())), 12000);
        return nullptr;
    }
    auto it = _layer_views.find(*lay);
    if (it == _layer_views.end() || it->second.first != &bmp) {
        std::shared_ptr<mapview::PixelLayer> v;
        if (*lay == u"terrain")
            v = std::make_shared<terrainpanel::TerrainView>(*mapdata, bmp);
        else
            v = std::make_shared<riverpanel::RiverView>(*mapdata, bmp, draw_panel->river_box);
        it = _layer_views.insert_or_assign(*lay, std::make_pair(&bmp, v)).first;
    }
    return it->second.second;
}

void MainWindow::_layer_pixels_changed(const QString &lay, const std::array<long long, 4> &box)
{
    if (draw_layer() == lay)
        map->redraw_box(box);
}

void MainWindow::_draw_layer_changed()
{
    _jump_mark.reset();
    _sync_map_tool();
    render_map();
    update_highlight();
    update_overlay();
    _show_banner();
}

void MainWindow::_error_map_changed()
{
    if (tab == u"errors" && color_mode == u"errors" && !draw_layer())
        render_map();
}

bool MainWindow::_prov_mismatch_shown() const
{
    return tab == u"map" && map_sub == u"draw" && draw_panel->layer() == u"prov" && draw_panel->mm_box->show_cb->isChecked();
}

mapview::ColorTable MainWindow::color_table()
{
    mapview::ColorTable t = _color_table();
    if (_prov_mismatch_shown() && !t.layer) {
        if (mismatch::Mismatch *mm = draw_panel->mismatch()) {   // (the mismatches over the provinces)
            mapview::ColorTable v;
            v.layer = std::make_shared<mismatchbox::ProvMismatchView>(*mapdata, t, *mm);
            return v;
        }
    }
    return t;
}

mapview::ColorTable MainWindow::_color_table()
{
    if (auto view = layer_view()) {                // Drawing → Terrain / Rivers: the bitmap itself
        mapview::ColorTable v;
        v.layer = view;
        return v;
    }
    if (tab == u"errors" && color_mode == u"errors" && !chk_panel->fill_lut.empty())
        return chk_panel->error_view.table();     // the error map: every problem at once
    const moddata::Mod &m = *mod;
    document::Document &d = *doc;
    const long long n = mapdata->n;
    std::vector<Rgb> t(size_t(n), Rgb{64, 66, 70});
    const QString cm = color_mode;
    auto finish = [&](std::vector<Rgb> &tt) {
        mapview::ColorTable ct;
        ct.rgb.resize(size_t(n) * 3);
        for (long long i = 0; i < n; ++i) {
            ct.rgb[size_t(i) * 3] = std::uint8_t(std::get<0>(tt[size_t(i)]));
            ct.rgb[size_t(i) * 3 + 1] = std::uint8_t(std::get<1>(tt[size_t(i)]));
            ct.rgb[size_t(i) * 3 + 2] = std::uint8_t(std::get<2>(tt[size_t(i)]));
        }
        return ct;
    };
    auto sea_and_zero = [&](std::vector<Rgb> &tt) {
        for (long long i = 0; i < n; ++i)
            if (mapdata->sea[size_t(i)])
                tt[size_t(i)] = mapview::SEA_RGB;
        if (n)
            tt[0] = NO_PROVINCE_RGB;              // pixels whose colour no province uses: never mistaken for sea
    };
    auto country_colour = [&](const QString &tag) {
        const moddata::Country *c = m.countries.get(tag);
        return c ? c->color : hashed_color(tag);
    };
    if (tab == u"provinces" && prov_sub == u"utility") {
        // Utility states: the viewed one in gold over the nations (dimmed); none viewed: every province in some
        // utility state in a soft gold (_utility_colors)
        const moddata::Region *view = util_view ? m.utility.get(*util_view) : nullptr;
        std::set<long long> inside;
        if (view)
            inside.insert(view->provinces.begin(), view->provinces.end());
        else
            for (const auto &[_k, r] : m.utility)
                inside.insert(r.provinces.begin(), r.provinces.end());
        for (const auto &[pid, pr] : m.provinces) {
            if (pr.sea || pid >= n)
                continue;
            if (inside.contains(pid)) {
                t[size_t(pid)] = view ? Rgb{242, 202, 92} : Rgb{178, 156, 104};
            } else {
                const OptStr &o = d.prov_state(pid).owner;
                Rgb c{70, 72, 78};
                if (o && !o->isEmpty()) {
                    const moddata::Country *cn = m.countries.get(*o);
                    c = cn ? cn->color : Rgb{90, 90, 96};
                }
                auto dim = [](int x) { return int(double(x) * 0.28 + 52 * 0.72); };
                t[size_t(pid)] = Rgb{dim(std::get<0>(c)), dim(std::get<1>(c)), dim(std::get<2>(c))};
            }
        }
        sea_and_zero(t);
        return finish(t);
    }
    std::vector<long long> land;
    for (const auto &[pid, pr] : m.provinces)
        if (!pr.sea && pid < n)
            land.push_back(pid);
    if (cm == u"political" || cm == u"controller") {
        for (long long pid : land) {
            const auto &st = d.prov_state(pid);
            const OptStr tag = cm == u"political" ? st.owner : st.controller;
            if (tag && !tag->isEmpty())
                t[size_t(pid)] = country_colour(*tag);
        }
    } else if (cm == u"cores") {
        const std::optional<long long> focus =
            last_clicked && m.provinces.contains(*last_clicked) ? last_clicked : std::nullopt;
        const OptStr tag = focus ? d.prov_state(*focus).owner : std::nullopt;
        const bool has = tag && !tag->isEmpty();
        const Rgb col = has ? (m.countries.get(*tag) ? m.countries.get(*tag)->color : Rgb{201, 161, 90}) : Rgb{201, 161, 90};
        for (long long pid : land) {
            const auto &st = d.prov_state(pid);
            if (has && st.cores.contains(*tag))
                t[size_t(pid)] = st.owner == tag ? col
                                                 : Rgb{int(std::get<0>(col) * 0.55 + 90 * 0.45),
                                                       int(std::get<1>(col) * 0.55 + 90 * 0.45),
                                                       int(std::get<2>(col) * 0.55 + 90 * 0.45)};
            else if (has && st.owner == tag)
                t[size_t(pid)] = {70, 58, 58};
            else
                t[size_t(pid)] = {44, 46, 52};
        }
    } else if (cm == u"liferating") {
        std::vector<double> vals;
        for (long long pid : land) {
            const auto lr = d.prov_state(pid).life_rating;
            if (lr)
                vals.push_back(double(*lr));
            t[size_t(pid)] = lr ? maplook::colour(cm, double(*lr), std::max(0.0, std::min(1.0, double(*lr) / 50.0)))
                                : Rgb{44, 46, 52};
        }
        _mode_vals[cm] = vals;
    } else if (cm == u"rgo") {
        for (long long pid : land) {
            const OptStr g = d.prov_state(pid).trade_goods;
            if (g && !g->isEmpty()) {
                const moddata::Good *gd = m.goods.get(*g);
                t[size_t(pid)] = gd ? gd->color : hashed_color(*g);
            }
        }
    } else if (cm == u"fort" || cm == u"naval_base" || cm == u"railroad") {
        const moddata::Building *b = m.buildings.get(cm);   // (provpanel's _max_level)
        if (cm == u"railroad" && !b)
            for (const auto &[_k, x] : m.buildings)
                if (x.type == u"infrastructure") {
                    b = &x;
                    break;
                }
        long long mx = b ? b->max_level : 6;
        if (!mx)
            mx = 6;
        std::vector<double> vals;
        for (long long pid : land) {
            const long long v = d.prov_state(pid).level_of(cm);
            vals.push_back(double(v));
            t[size_t(pid)] = v > 0 || maplook::uses_ranges(cm)
                                 ? maplook::colour(cm, double(v), 0.25 + 0.75 * double(v) / double(mx))
                                 : Rgb{44, 46, 52};
        }
        _mode_vals[cm] = vals;
    } else if (cm == u"buildings") {
        _building_colors(t, land);
    } else if (cm == u"factories") {
        std::vector<double> vals;
        for (const auto &[rid, r] : m.regions) {
            const long long cnt = static_cast<long long>(d.state_factories(rid).size());
            vals.push_back(double(cnt));
            Rgb c;
            if (maplook::uses_ranges(cm))
                c = cnt > 0 ? maplook::colour(cm, double(cnt), 0) : Rgb{44, 46, 52};
            else
                c = cnt == 0 ? Rgb{44, 46, 52}
                    : cnt > FACTORY_CAP ? Rgb{200, 60, 50}
                                        : maplook::colour(cm, double(cnt), 0.2 + 0.8 * double(cnt) / double(FACTORY_CAP));
            for (long long pid : r.provinces)
                if (pid < n)
                    t[size_t(pid)] = c;
        }
        _mode_vals[cm] = vals;
    } else if (cm == u"climate" || cm == u"continent") {
        // debugging view: every land province should be in exactly one list
        auto &mf = d.map<mapfiles::MembershipFile>(cm);
        const auto mem = mf.membership();
        for (qsizetype i = 0; i < mf.names.size(); ++i) {
            const QString &name = mf.names[i];
            const Rgb c = cm == u"climate" ? CLIMATE_COLORS[size_t(i) % CLIMATE_COLORS.size()] : hashed_color(name, 0.45, 0.7);
            if (const auto *l = mf.lists.get(name))
                for (long long pid : *l)
                    if (pid < n)
                        t[size_t(pid)] = c;
        }
        for (long long pid : land) {
            const QStringList *k = mem.get(pid);
            if (!k || k->isEmpty())
                t[size_t(pid)] = {110, 30, 30};   // in none: dark red
            else if (k->size() > 1)
                t[size_t(pid)] = {240, 40, 40};   // in several: bright red
        }
    } else if (cm == u"events") {
        return _event_colors(t, land);
    } else if (mapmodes::handles(cm)) {
        more_modes->table(cm, t, land);
    } else if (cm == u"plain") {
        std::fill(t.begin(), t.end(), Rgb{86, 90, 96});
        for (long long i = 0; i < n; ++i)
            if (mapdata->sea[size_t(i)])
                t[size_t(i)] = mapview::SEA_RGB;
        for (long long pid : geo->thin_tiles().items())
            t[size_t(pid)] = {52, 72, 96};        // thin sea tiles (possible Meme rivers)
        auto &adjf = d.map<adjacency::AdjacencyFile>(u"adjacency"_s);
        for (long long pid : adjf.no_coast)
            if (pid < n)
                t[size_t(pid)] = {50, 120, 120};  // no-coast sea tiles: teal
        for (const auto &[pid, _mk] : adjf.river_tiles())
            if (pid < n)
                t[size_t(pid)] = {70, 140, 235};  // Meme rivers: bright blue
        if (n)
            t[0] = NO_PROVINCE_RGB;
        return finish(t);
    } else if (cm == u"mixedowners") {
        std::fill(t.begin(), t.end(), Rgb{48, 52, 58});
        for (const auto &[rid, r] : m.regions) {
            QSet<OptStr> owners;
            for (long long p : r.provinces)
                owners.insert(d.prov_state(p).owner);
            if (!r.provinces.empty() && owners.size() > 1)
                for (long long pid : r.provinces) {
                    const OptStr tag = d.prov_state(pid).owner;
                    if (pid < n)
                        t[size_t(pid)] = tag && !tag->isEmpty() ? country_colour(*tag) : Rgb{90, 90, 90};
                }
        }
    } else if (cm == u"splitstates") {
        std::fill(t.begin(), t.end(), Rgb{48, 52, 58});
        // (a meme river through a state is no split: C++ only, the user 2026-11-13)
        const provpanel::Links slinks = provpanel::state_links(&d.map<adjacency::AdjacencyFile>(u"adjacency"_s));
        for (const auto &[rid, r] : m.regions)
            if (r.provinces.size() > 1) {
                const auto parts = provpanel::state_components(m, mapdata.get(), rid, &slinks);
                if (parts.size() > 1) {
                    size_t main = 0;
                    for (size_t i = 1; i < parts.size(); ++i)
                        if (parts[i].size() > parts[main].size())
                            main = i;
                    for (size_t i = 0; i < parts.size(); ++i)
                        for (long long pid : parts[i])
                            if (pid < n)
                                t[size_t(pid)] = i == main ? Rgb{120, 88, 50} : Rgb{245, 150, 40};
                }
            }
    } else if (cm == u"colonial") {
        for (long long pid : land) {
            const long long v = d.prov_state(pid).colonial;
            t[size_t(pid)] = v == 0 ? Rgb{60, 70, 62} : v == 1 ? Rgb{190, 150, 80} : Rgb{215, 110, 60};
        }
    } else if (cm == u"states") {
        for (const auto &[rid, r] : m.regions) {
            const Rgb c = hashed_color(rid);
            for (long long pid : r.provinces)
                if (pid < n)
                    t[size_t(pid)] = c;
        }
    } else if (cm == u"statesize") {
        const QString kind = maplook::state_kind();
        for (long long pid : land)
            t[size_t(pid)] = maplook::OUTSIDE;
        for (const auto &[rid, v] : state_sizes())
            if (const auto c = maplook::size_colour(kind, v))
                for (long long pid : m.regions.get(rid)->provinces)
                    if (pid < n)
                        t[size_t(pid)] = *c;
    } else if (cm == u"provsize") {
        for (long long pid : land) {
            long long s = 0;
            if (const auto *pops = d.pops.get(pid))
                for (const auto &p : *pops)
                    s += p.size;
            const auto c = maplook::size_colour(u"prov_people"_s, double(s));
            t[size_t(pid)] = c ? *c : maplook::OUTSIDE;
        }
    } else if (cm == u"population" || cm == u"filter") {
        OrderedMap<long long, long long> vals;
        const std::optional<document::PopFilter> flt =
            cm == u"filter" ? std::optional<document::PopFilter>(edit_panel->current_filter()) : std::nullopt;
        for (const auto &[pid, pops] : d.pops) {
            long long s = 0;
            for (const auto &p : pops)
                if (!flt || flt->match(p))
                    s += p.size;
            vals.insert(pid, s);
        }
        // colour by rank so the whole ramp is used even with a few huge provinces
        std::vector<std::pair<long long, long long>> ranked;
        for (const auto &[pid, v] : vals)
            if (pid < n && m.provinces.contains(pid) && !m.provinces.get(pid)->sea && v > 0)
                ranked.emplace_back(v, pid);
        std::sort(ranked.begin(), ranked.end());
        std::vector<double> mv;
        for (size_t i = 0; i < ranked.size(); ++i) {
            t[size_t(ranked[i].second)] =
                maplook::colour(cm, double(ranked[i].first), double(i + 1) / double(ranked.size()));
            mv.push_back(double(ranked[i].first));
        }
        _mode_vals[cm] = mv;
        for (const auto &[pid, v] : vals)
            if (pid < n && v <= 0)
                t[size_t(pid)] = {44, 46, 52};
    } else if (cm == u"culture" || cm == u"religion") {
        const bool cul = cm == u"culture";
        for (const auto &[pid, pops] : d.pops) {
            OrderedMap<OptStr, long long> agg;
            for (const auto &p : pops)
                agg[cul ? p.culture : p.religion] += p.size;
            if (!agg.isEmpty() && pid < n) {
                OptStr k;
                long long best = 0;
                bool firstk = true;
                for (const auto &[kk, v] : agg)
                    if (firstk || v > best) {
                        k = kk;
                        best = v;
                        firstk = false;
                    }
                const QString ks = k.value_or(u"None"_s);
                Rgb c = hashed_color(ks);
                if (k) {
                    if (cul) {
                        if (const auto *x = m.cultures.get(*k))
                            c = x->color;
                    } else if (const auto *x = m.religions.get(*k)) {
                        c = x->color;
                    }
                }
                t[size_t(pid)] = c;
            }
        }
    } else if (cm == u"culture_mix" || cm == u"religion_mix") {
        // stripes: every culture (religion) of a province covers its share of the province
        const bool cul = cm == u"culture_mix";
        mapview::ColorTable ct;
        ct.Q = MIX_Q;
        ct.rgb.resize(size_t(n) * MIX_Q * 3);
        auto put = [&](long long pid, int q, const Rgb &c) {
            const size_t o = (size_t(pid) * MIX_Q + size_t(q)) * 3;
            ct.rgb[o] = std::uint8_t(std::get<0>(c));
            ct.rgb[o + 1] = std::uint8_t(std::get<1>(c));
            ct.rgb[o + 2] = std::uint8_t(std::get<2>(c));
        };
        for (long long pid = 0; pid < n; ++pid)
            for (int q = 0; q < MIX_Q; ++q)
                put(pid, q, t[size_t(pid)]);
        for (const auto &[pid, pops] : d.pops) {
            if (pid >= n)
                continue;
            OrderedMap<OptStr, long long> agg;
            for (const auto &p : pops)
                agg[cul ? p.culture : p.religion] += p.size;
            long long tot = 0;
            for (const auto &[_k, v] : agg)
                tot += v;
            if (tot <= 0)
                continue;
            std::vector<std::pair<QString, double>> shares;
            std::map<QString, OptStr> back;
            for (const auto &[k, v] : agg) {
                const QString ks = k ? *k : u"\x01None"_s;   // (None stays apart from a culture named "None")
                shares.emplace_back(ks, double(v) / double(tot));
                back[ks] = k;
            }
            const QStringList slots_ = mix_slots(shares);
            for (int q = 0; q < int(slots_.size()); ++q) {
                const OptStr k = back[slots_[q]];
                Rgb c = hashed_color(k.value_or(u"None"_s));
                if (k) {
                    if (cul) {
                        if (const auto *x = m.cultures.get(*k))
                            c = x->color;
                    } else if (const auto *x = m.religions.get(*k)) {
                        c = x->color;
                    }
                }
                put(pid, q, c);
            }
        }
        for (long long pid = 0; pid < n; ++pid)
            if (mapdata->sea[size_t(pid)] || pid == 0)
                for (int q = 0; q < MIX_Q; ++q)
                    put(pid, q, pid == 0 ? NO_PROVINCE_RGB : mapview::SEA_RGB);
        return ct;
    }
    if (cm == u"military" || cm == u"soldiers" || cm == u"status" || cm == u"relvalues" || cm == u"spheres" ||
        cm == u"subjects" || cm == u"alliances") {
        // Armies / Diplomacy map modes (Python: _unit_modes)
        const OptStr a = nation_tag;
        QSet<QString> lit;                          // (C++ only) the nations whose generals / admirals are listed
        if (army_panel && (army_panel->sub == u"generals" || army_panel->sub == u"admirals"))
            for (const QString &x : army_panel->leaders->tags_shown())
                lit.insert(x);
        std::vector<std::pair<long long, OptStr>> owner;
        for (long long pid : land)
            owner.emplace_back(pid, d.prov_state(pid).owner);
        std::fill(t.begin(), t.end(), Rgb{52, 54, 60});
        if (cm == u"military") {
            for (const auto &[pid, o] : owner) {
                if (!o || o->isEmpty())
                    continue;
                const Rgb c = country_colour(*o);
                if (o == a || lit.contains(*o))
                    t[size_t(pid)] = Rgb{int(std::get<0>(c) * 0.55 + 118 * 0.45), int(std::get<1>(c) * 0.55 + 118 * 0.45),
                                         int(std::get<2>(c) * 0.55 + 118 * 0.45)};
                else
                    t[size_t(pid)] = Rgb{int(std::get<0>(c) * 0.45 + 60 * 0.55), int(std::get<1>(c) * 0.45 + 60 * 0.55),
                                         int(std::get<2>(c) * 0.45 + 60 * 0.55)};   // like the Diplomacy map
            }
        } else if (cm == u"soldiers") {
            std::map<long long, long long> sold;
            for (long long pid : land) {
                long long s = 0;
                if (const auto *ps = d.pops.get(pid))
                    for (const auto &p : *ps)
                        if (p.type == u"soldiers")
                            s += p.size;
                sold[pid] = s;
            }
            std::map<std::pair<QString, QString>, long long> per_state;
            for (const auto &[pid, o] : owner) {
                const OptStr &rid = m.provinces.get(pid)->region;
                if (rid && !rid->isEmpty() && o && !o->isEmpty())
                    per_state[{*o, *rid}] += sold[pid];
            }
            std::vector<double> vals;
            for (const auto &[_k, v] : per_state)
                if (v > 0)
                    vals.push_back(double(v));
            std::sort(vals.begin(), vals.end());
            for (const auto &[pid, o] : owner) {
                const OptStr &rid = m.provinces.get(pid)->region;
                long long v = 0;
                if (rid && !rid->isEmpty() && o && !o->isEmpty())
                    if (const auto it = per_state.find({*o, *rid}); it != per_state.end())
                        v = it->second;
                if (v > 0) {
                    const auto idx = std::lower_bound(vals.begin(), vals.end(), double(v)) - vals.begin();
                    t[size_t(pid)] = maplook::colour(cm, double(v), 0.15 + 0.85 * double(idx + 1) / double(vals.size()));
                } else {
                    t[size_t(pid)] = {44, 46, 52};
                }
            }
            _mode_vals[cm] = vals;
        } else if (cm == u"status" || cm == u"relvalues") {
            auto dimmed = [](const Rgb &c) {
                return Rgb{int(std::get<0>(c) * 0.45 + 60 * 0.55), int(std::get<1>(c) * 0.45 + 60 * 0.55),
                           int(std::get<2>(c) * 0.45 + 60 * 0.55)};
            };
            if (!a || a->isEmpty()) {
                for (const auto &[pid, o] : owner)
                    if (o && !o->isEmpty())
                        t[size_t(pid)] = dimmed(country_colour(*o));
            } else if (cm == u"relvalues") {
                const auto &ob = d.map<oob::OOBEdits>(u"oob"_s);
                std::map<QString, Rgb> cache;
                std::vector<double> rels;
                for (const auto &[pid, o] : owner) {
                    if (!o || o->isEmpty())
                        continue;
                    auto it = cache.find(*o);
                    if (it == cache.end()) {
                        const bool has = ob.state(*a).relations.contains(*o) || ob.state(*o).relations.contains(*a);
                        const std::optional<long long> rv =
                            has ? std::optional<long long>(diplomacy::relation_value(ob, *a, *o)) : std::nullopt;
                        if (rv)
                            rels.push_back(double(*rv));
                        Rgb c = diplopanel::COL_OTHER;
                        if (*o == *a)
                            c = country_colour(*a);
                        else if (rv)
                            c = maplook::colour(cm, double(*rv), double(*rv + 200) / 400.0,
                                                [](double x) { return diplopanel::relation_rgb(x * 400 - 200); });
                        it = cache.emplace(*o, c).first;
                    }
                    t[size_t(pid)] = it->second;
                }
                _mode_vals[cm] = rels;
            } else {
                // the status: A in its own colour, the others by what they are to A — striped when several
                const auto st = diplopanel::statuses(d.map<oob::OOBEdits>(u"oob"_s),
                                                     d.map<diplomacy::DiploEdits>(u"diplomacy"_s), *a);
                for (const auto &[pid, o] : owner)
                    if (o && !o->isEmpty())
                        t[size_t(pid)] = *o == *a ? country_colour(*a) : diplopanel::COL_OTHER;
                mapview::ColorTable ct;
                ct.Q = MIX_Q;
                ct.rgb.resize(size_t(n) * MIX_Q * 3);
                auto put = [&](long long pid, int q, const Rgb &c) {
                    const size_t o = (size_t(pid) * MIX_Q + size_t(q)) * 3;
                    ct.rgb[o] = std::uint8_t(std::get<0>(c));
                    ct.rgb[o + 1] = std::uint8_t(std::get<1>(c));
                    ct.rgb[o + 2] = std::uint8_t(std::get<2>(c));
                };
                for (long long pid = 0; pid < n; ++pid)
                    for (int q = 0; q < MIX_Q; ++q)
                        put(pid, q, t[size_t(pid)]);
                for (const auto &[pid, o] : owner) {
                    const QStringList *ks = o && !o->isEmpty() ? st.get(*o) : nullptr;
                    if (ks && !ks->isEmpty())
                        for (int q = 0; q < MIX_Q; ++q)
                            put(pid, q, *diplopanel::STATUS_RGB.get((*ks)[q * ks->size() / MIX_Q]));
                }
                for (long long pid = 0; pid < n; ++pid)   // (the relations have their own view: no outline here)
                    if (mapdata->sea[size_t(pid)] || pid == 0)
                        for (int q = 0; q < MIX_Q; ++q)
                            put(pid, q, pid == 0 ? NO_PROVINCE_RGB : mapview::SEA_RGB);
                return ct;
            }
        } else if (cm == u"spheres") {
            const auto &ob = d.map<oob::OOBEdits>(u"oob"_s);
            const QStringList gpl = diplomacy::great_powers(m, d.map<countryhist::NationEdits>(u"nations"_s));
            const QSet<QString> gps(gpl.begin(), gpl.end());
            OrderedMap<QString, QStringList> lords;
            for (const auto &[tg, ls] : diplomacy::sphere_lords(ob, gpl)) {
                QStringList keep;
                for (const QString &l : ls)
                    if (gps.contains(l))
                        keep << l;
                if (!keep.isEmpty())
                    lords.insert(tg, keep);
            }
            QSet<QString> gp;
            for (const auto &[_t, ls] : lords)
                for (const QString &l : ls)
                    gp.insert(l);
            for (const auto &[pid, o] : owner) {
                if (!o)
                    continue;
                if (gp.contains(*o)) {
                    t[size_t(pid)] = country_colour(*o);
                } else if (const QStringList *ls = lords.get(*o)) {
                    const Rgb c = country_colour(ls->front());
                    t[size_t(pid)] = ls->size() == 1 ? Rgb{int(std::get<0>(c) * 0.62 + 40 * 0.38),
                                                           int(std::get<1>(c) * 0.62 + 40 * 0.38),
                                                           int(std::get<2>(c) * 0.62 + 40 * 0.38)}
                                                      : Rgb{236, 70, 70};
                }
            }
        } else if (cm == u"subjects") {
            std::map<QString, QString> over;
            for (const diplomacy::Record &r : d.map<diplomacy::DiploEdits>(u"diplomacy"_s).at_start())
                if (r.kind == u"vassal" || r.kind == u"substate")
                    over[r.second] = r.first;
            QSet<QString> lords;
            for (const auto &[_s, l] : over)
                lords.insert(l);
            for (const auto &[pid, o] : owner) {
                if (!o)
                    continue;
                if (lords.contains(*o)) {
                    t[size_t(pid)] = country_colour(*o);
                } else if (const auto it = over.find(*o); it != over.end()) {
                    const Rgb c = country_colour(it->second);
                    t[size_t(pid)] = Rgb{int(std::get<0>(c) * 0.62 + 40 * 0.38), int(std::get<1>(c) * 0.62 + 40 * 0.38),
                                         int(std::get<2>(c) * 0.62 + 40 * 0.38)};
                }
            }
        } else {                                   // alliances
            std::map<QString, std::set<QString>> groups;
            for (const diplomacy::Record &r : d.map<diplomacy::DiploEdits>(u"diplomacy"_s).at_start()) {
                if (r.kind != u"alliance")
                    continue;
                const auto g1 = groups.contains(r.first) ? groups[r.first] : std::set<QString>{r.first};
                const auto g2 = groups.contains(r.second) ? groups[r.second] : std::set<QString>{r.second};
                std::set<QString> g = g1;
                g.insert(g2.begin(), g2.end());
                for (const QString &x : g)
                    groups[x] = g;
            }
            for (const auto &[pid, o] : owner)
                if (o)
                    if (const auto it = groups.find(*o); it != groups.end())
                        t[size_t(pid)] = hashed_color(*it->second.begin(), 0.6, 0.8);
        }
    }
    sea_and_zero(t);
    return finish(t);
}

void MainWindow::render_map()
{
    map->render_base(color_table(), view_mode);
    update_highlight();
    if (mapmodes::handles(color_mode))             // (its banner names numbers that may have changed)
        _show_banner();
}

void MainWindow::schedule_render()
{
    static const QSet<QString> MODES = {u"population"_s, u"culture"_s, u"religion"_s, u"culture_mix"_s,
                                        u"religion_mix"_s, u"filter"_s, u"military"_s, u"soldiers"_s, u"status"_s,
                                        u"relvalues"_s, u"spheres"_s, u"subjects"_s, u"alliances"_s};
    if (MODES.contains(color_mode) || PROVINCE_MODES.contains(color_mode) || mapmodes::handles(color_mode))
        _render_timer->start(200);
}

// ---------------------------------------------------------------------------------------------- selection
OrderedMap<OptStr, long long> MainWindow::mesh_index(const QString &rid, const OptStr &own)
{
    // stable legend index per other owner of a state (same order as the panel's legend)
    std::set<std::pair<bool, QString>> others;
    std::map<std::pair<bool, QString>, OptStr> back;
    for (long long q : mod->regions.get(rid)->provinces) {
        const OptStr o = doc->prov_state(q).owner;
        if (o == own)
            continue;
        const std::pair<bool, QString> k{!o.has_value(), o.value_or(u"None"_s)};
        others.insert(k);
        back[k] = o;
    }
    OrderedMap<OptStr, long long> out;
    long long i = 0;
    for (const auto &k : others)
        out.insert(back[k], i++);
    return out;
}

OrderedMap<long long, long long> MainWindow::_context_codes(const std::vector<long long> &pids)
{
    OrderedMap<long long, long long> codes;
    for (long long pid : pids) {
        const moddata::Province *pr = mod->provinces.get(pid);
        if (!pr || !pr->region || pr->region->isEmpty())
            continue;
        const QString rid = *pr->region;
        const OptStr own = doc->prov_state(pid).owner;
        const auto idx = mesh_index(rid, own);
        for (long long q : mod->regions.get(rid)->provinces) {
            const long long *c = codes.get(q);
            if (!c || *c != mapview::CTX) {
                const OptStr o = doc->prov_state(q).owner;
                codes.insert(q, o == own ? mapview::CTX : mapview::CTX_FOREIGN + *idx.get(o));
            }
        }
    }
    return codes;
}

// Armies → Generals / Navies → Admirals: the picked nations outlined red on the map (the user 2026-11-18: "like with
// the province selection … so I can see all of this area is selected"); set again only when the picked nations change
void MainWindow::_leaders_outline()
{
    QString key;
    std::vector<mapview::FocusLayer> layers;
    if (tab == u"armies" && army_panel && (army_panel->sub == u"generals" || army_panel->sub == u"admirals") &&
        army_panel->leaders->scope != u"world") {
        const QStringList tags = army_panel->leaders->tags_shown();
        key = tags.join(u',') + u'|' + QString::number(doc->undo_stack.size());
        const QSet<QString> want(tags.begin(), tags.end());
        mapview::FocusLayer l{u"outline"_s, Rgb{235, 40, 40}, {}};
        for (long long pid = 1; pid < mapdata->n; ++pid)
            if (!mapdata->sea[size_t(pid)])
                if (const OptStr o = doc->prov_state(pid).owner; o && want.contains(*o))
                    l.pids.insert(pid, 1.0);
        if (l.pids.size() > 0)
            layers.push_back(std::move(l));
    }
    if (key == _lead_outline_key)
        return;
    _lead_outline_key = key;
    if (tab != u"events")
        map->set_focus_layer(layers);
}

void MainWindow::update_highlight()
{
    OrderedMap<long long, long long> codes = _context_codes(selection);
    for (long long pid : selection)
        codes.insert(pid, 1);
    if (mode == u"split" && split_panel->source) {
        const long long src = *split_panel->source;
        codes = _context_codes({src});
        codes.insert(src, 3);
        for (long long pid : split_panel->target_pids())
            codes.insert(pid, 2);
    } else if (mode == u"capital" && capital_region) {
        codes.clear();
        if (const moddata::Region *r = mod->regions.get(*capital_region))
            for (long long pid : r->provinces)
                codes.insert(pid, 4);
    }
    map->set_highlight(codes);
    const std::vector<long long> focus =
        mode == u"split" && split_panel->source ? std::vector<long long>{*split_panel->source} : selection;
    // the river map has no use for the province selection, and its red hatching would hide the rivers
    std::vector<long long> shown;
    if (draw_layer() != u"rivers") {
        shown = focus;
        shown.insert(shown.end(), sea_selection.begin(), sea_selection.end());
    }
    map->set_selection_layer(shown);
    _leaders_outline();
    std::set<QString> rids;
    for (long long p : focus)
        if (const auto *pr = mod->provinces.get(p); pr && pr->region)
            rids.insert(*pr->region);
    std::set<long long> caps;
    for (const QString &r : rids)
        if (!r.isEmpty())
            if (const auto c = doc->capital(r))
                caps.insert(*c);
    map->set_focus_capitals(std::vector<long long>(caps.begin(), caps.end()));
}

void MainWindow::set_selection(const std::vector<long long> &pids, bool render, const std::vector<long long> &sea)
{
    std::set<long long> seen;
    selection.clear();
    for (long long p : pids) {
        const moddata::Province *pr = mod->provinces.get(p);
        if (pr && !pr->sea && seen.insert(p).second)
            selection.push_back(p);
    }
    // sea tiles: selectable and highlighted, but no editing functions (yet)
    sea_selection = sea;
    if (render)
        update_highlight();
    refresh_panel();
}

// ---------------------------------------------------------------------------------------------- the Errors tab
void MainWindow::_set_chk_banner(const QString &text, std::optional<Rgb> rgb)
{
    _chk_banner = {text, rgb};
    _show_banner();
    // a river problem is shown on the river map (red pixels), everything else on the normal map
    const auto want = draw_layer();
    if (!_err_layer || *_err_layer != want) {
        const bool first = !_err_layer;
        _err_layer = want;
        if (!first || want) {
            render_map();
            update_highlight();
        }
    }
}

void MainWindow::_show_banner()
{
    // the problem selected in the Errors tab stays named at the top of the map while it's selected; in Drawing →
    // Rivers the river guide says what to click next
    const auto &[text, rgb] = _chk_banner;
    if (tab == u"errors" && !text.isEmpty()) {
        map->set_banner(text, rgb);
    } else if (tab == u"map" && map_sub == u"draw" && draw_panel->layer() == u"rivers" &&
               !draw_panel->river_box->banner().isEmpty()) {
        map->set_banner(draw_panel->river_box->banner(), Rgb{232, 196, 120});
    } else if (NEEDS_NATION.contains(color_mode) && (!nation_tag || nation_tag->isEmpty())) {   // (no nation picked yet)
        map->set_banner(NEEDS_NATION.at(color_mode), Rgb{232, 196, 120});
    } else if (mapmodes::handles(color_mode) && (tab == u"pops" || tab == u"nations") && !draw_layer()) {
        // what the colours mean (and, for literacy, which scripts they come from) — it stays at the top
        const QString note = more_modes->caption(color_mode);
        map->set_banner(note, note.contains(u"⚠"_s) ? Rgb{240, 170, 60} : Rgb{232, 196, 120});
    } else {
        map->set_banner(u""_s, std::nullopt);
    }
    _mode_legend();
}

namespace {
Rgb bld_col(const QString &k)                       // (Python's BLD_COL)
{
    if (k == u"factories")
        return {215, 175, 60};
    if (k == u"fort")
        return {215, 70, 55};
    if (k == u"naval_base")
        return {70, 135, 235};
    if (k == u"both")
        return {175, 90, 225};
    return {90, 185, 95};                           // railroad
}
} // namespace

void MainWindow::_bld_toggle(const QString &key)
{
    if (color_mode != u"buildings" || !_bld_shown.contains(key))
        return;
    _bld_shown[key] = !_bld_shown[key];
    render_map();
    _mode_legend();
}

void MainWindow::_building_colors(std::vector<Rgb> &t, const std::vector<long long> &land)
{
    const moddata::Mod &m = *mod;
    document::Document &d = *doc;
    const long long n = mapdata->n;
    const auto &on = _bld_shown;
    const Rgb empty{44, 46, 52};
    std::fill(t.begin(), t.end(), Rgb{64, 66, 70});
    auto shade = [&](const Rgb &rgb, double f) {   // f 0..1: dark (level 1) .. full (the highest level)
        const double k = 0.40 + 0.60 * std::max(0.0, std::min(1.0, f));
        auto one = [&](int c, int e) { return static_cast<int>(double(c) * k + double(e) * (1 - k)); };
        return Rgb{one(std::get<0>(rgb), std::get<0>(empty)), one(std::get<1>(rgb), std::get<1>(empty)),
                   one(std::get<2>(rgb), std::get<2>(empty))};
    };
    for (long long pid : land)
        t[size_t(pid)] = empty;
    if (on.at(u"factories"_s)) {
        for (const auto &[rid, r] : m.regions) {
            const auto cnt = static_cast<long long>(d.state_factories(rid).size());
            if (cnt) {
                const Rgb c = shade(bld_col(u"factories"_s), double(cnt) / double(FACTORY_CAP));
                for (long long pid : r.provinces) {
                    const moddata::Province *pr = m.provinces.get(pid);
                    if (pid < n && pr && !pr->sea)
                        t[size_t(pid)] = c;
                }
            }
        }
    }
    std::map<QString, long long> mx;
    for (const QString k : {u"fort"_s, u"naval_base"_s, u"railroad"_s}) {
        const long long v = prov_panel->_max_level(k);
        mx[k] = v ? v : 6;
    }
    for (long long pid : land) {
        const auto &st = d.prov_state(pid);
        const long long f = on.at(u"fort"_s) ? st.fort : 0;
        const long long nb = on.at(u"naval_base"_s) ? st.naval_base : 0;
        if (f && nb)
            t[size_t(pid)] = shade(bld_col(u"both"_s), std::max(double(f) / double(mx[u"fort"_s]),
                                                              double(nb) / double(mx[u"naval_base"_s])));
        else if (f)
            t[size_t(pid)] = shade(bld_col(u"fort"_s), double(f) / double(mx[u"fort"_s]));
        else if (nb)
            t[size_t(pid)] = shade(bld_col(u"naval_base"_s), double(nb) / double(mx[u"naval_base"_s]));
        else if (on.at(u"railroad"_s) && t[size_t(pid)] == empty) {
            const long long rr = st.railroad;
            if (rr)
                t[size_t(pid)] = shade(bld_col(u"railroad"_s), double(rr) / double(mx[u"railroad"_s]));
        }
    }
}

void MainWindow::_building_legend()
{
    const moddata::Mod &m = *mod;
    document::Document &d = *doc;
    const auto &on = _bld_shown;
    long long nfort = 0, nnaval = 0, nrail = 0, both = 0;
    for (const auto &[p, pr] : m.provinces) {
        if (pr.sea)
            continue;
        const auto &st = d.prov_state(p);
        nfort += st.fort > 0;
        nnaval += st.naval_base > 0;
        nrail += st.railroad > 0;
        both += st.fort > 0 && st.naval_base > 0;
    }
    long long nstates = 0, nf = 0;
    for (const auto &[rid, _r] : m.regions) {
        const auto k = static_cast<long long>(d.state_factories(rid).size());
        if (k) {
            nstates += 1;
            nf += k;
        }
    }
    std::vector<mapview::LegendRow> rows;
    rows.push_back({bld_col(u"factories"_s),
                    u"Factories (a state's): %1 in %2 states — the whole state, brighter = more (of %3)"_s.arg(nf).arg(
                        nstates).arg(FACTORY_CAP),
                    u"factories"_s, on.at(u"factories"_s)});
    rows.push_back({bld_col(u"fort"_s), u"Forts: %1 provinces — brighter = higher level"_s.arg(nfort), u"fort"_s,
                    on.at(u"fort"_s)});
    rows.push_back({bld_col(u"naval_base"_s), u"Naval bases: %1 provinces"_s.arg(nnaval), u"naval_base"_s,
                    on.at(u"naval_base"_s)});
    rows.push_back({bld_col(u"both"_s), u"Fort AND naval base: %1 provinces"_s.arg(both)});
    rows.push_back({bld_col(u"railroad"_s), u"Railroads: %1 provinces (where nothing else is drawn)"_s.arg(nrail),
                    u"railroad"_s, on.at(u"railroad"_s)});
    map->set_legend(u"Buildings — click a line to show / hide it; the levels are in the hover line"_s, rows);
}

void MainWindow::_mode_legend()
{
    // the legend of the Climate / Continent map modes: each one in its colour, its provinces, what it gives
    const QString m = color_mode;
    if (m == u"buildings" && !draw_layer() && doc) {
        _building_legend();
        return;
    }
    if ((m != u"climate" && m != u"continent") || draw_layer() || !doc || !doc->maps.contains(m)) {
        map->set_legend(u""_s, {});
        return;
    }
    auto &mf = doc->map<mapfiles::MembershipFile>(m);
    const auto mem = mf.membership();
    std::vector<long long> land;
    for (const auto &[p, pr] : mod->provinces)
        if (!pr.sea)
            land.push_back(p);
    std::vector<mapview::LegendRow> rows;
    auto *lc = doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    for (qsizetype i = 0; i < mf.names.size(); ++i) {
        const QString &name = mf.names[i];
        const Rgb c = m == u"climate" ? CLIMATE_COLORS[size_t(i) % CLIMATE_COLORS.size()] : hashed_color(name, 0.45, 0.7);
        long long k = 0;
        if (const auto *l = mf.lists.get(name))
            for (long long p : *l)
                if (const auto *pr = mod->provinces.get(p); pr && !pr->sea)
                    k += 1;
        QString nm;
        if (lc)
            nm = lc->text(name).value_or(QString());
        if (nm.isEmpty())
            nm = mod->loc.value(name);
        if (nm.isEmpty())
            nm = QString(name).replace(u'_', u' ');
        QStringList mods;
        if (const auto *ms = mf.mods.get(name))
            for (const auto &[key, v] : *ms) {
                bool ok = false;
                const double f = py::strip(v).toDouble(&ok);
                if (!ok || f != 0)
                    mods << u"%1 %2"_s.arg(QString(key).replace(u'_', u' '), v);
            }
        rows.push_back({c, u"%1 — %2 province%3%4"_s.arg(nm).arg(k).arg(k == 1 ? u""_s : u"s"_s,
                                                                      mods.isEmpty() ? u""_s : u" · "_s + mods.join(u", "_s))});
    }
    long long none = 0, many = 0;
    for (long long p : land) {
        const QStringList *x = mem.get(p);
        if (!x || x->isEmpty())
            none += 1;
        else if (x->size() > 1)
            many += 1;
    }
    if (none)
        rows.push_back({{110, 30, 30}, u"in no %1: %2 land province%3"_s.arg(m).arg(none).arg(none == 1 ? u""_s : u"s"_s)});
    if (many)
        rows.push_back({{240, 40, 40}, u"in several %1s: %2"_s.arg(m).arg(many)});
    map->set_legend(m == u"climate" ? u"Climate"_s : u"Continent"_s, rows);
}

void MainWindow::_update_errors()
{
    // red badge left of Apply (number of errors) and red markers on the tabs the errors belong to
    const auto [per_tab, total] = chk_panel->error_counts();
    err_badge->setVisible(total > 0);
    err_badge->setText(u"⚠ %1"_s.arg(total));
    err_badge->setToolTip(u"%1 error%2 — click to see them (Errors tab)"_s.arg(total).arg(total == 1 ? u""_s : u"s"_s));
    std::map<QString, std::vector<PageProblem>> places;
    QStringList order;                              // (the dict's order of first appearance)
    for (const auto &c : chk_panel->cats) {
        if ((c.severity != mapcheck::SEV_ERROR && c.severity != mapcheck::SEV_WARN) || c.items.empty())
            continue;
        for (const auto &it : c.items) {
            const auto [page, part] = problem_place(c.key, c.tab, &it);
            if (page && !page->isEmpty()) {
                if (!places.contains(*page))
                    order << *page;
                places[*page].push_back({c.severity, c.key, &it, part});
            }
        }
    }
    for (auto &[_p, lst] : places)                 // errors first (the order within stays)
        std::stable_sort(lst.begin(), lst.end(), [](const PageProblem &a, const PageProblem &b) {
            return (a.sev != mapcheck::SEV_ERROR) < (b.sev != mapcheck::SEV_ERROR);
        });
    _page_problems = places;
    OrderedMap<QString, long long> pages;
    if (total)
        pages.insert(u"errors"_s, total);
    for (const QString &page : order) {
        long long k = 0;
        for (const auto &x : places[page])
            k += x.sev == mapcheck::SEV_ERROR;
        if (k)
            pages.insert(page, k);
    }
    _err_pages = pages;
    rail->set_counts(_pend_pages, _err_pages);
    auto parts = [&places](const QString &page) {
        std::map<QString, std::pair<long long, long long>> out;
        const auto it = places.find(page);
        if (it != places.end())
            for (const PageProblem &x : it->second)
                if (x.part && !x.part->isEmpty()) {
                    auto &[e, wn] = out[*x.part];
                    e += x.sev == mapcheck::SEV_ERROR;
                    wn += x.sev == mapcheck::SEV_WARN;
                }
        return out;
    };
    pol_ws->set_counts(parts(u"politics"_s));
    war_ws->set_counts(parts(u"warfare"_s));
    rules_ws->set_counts(parts(u"rules"_s));
    scriptstore::ScriptStore *store = doc && doc->maps.contains(u"scripts"_s)
                                          ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    // the place of a script problem in its file (None: it is about the file as a whole)
    auto where = [store](const QString &key, const mapcheck::Val &d) -> std::optional<qsizetype> {
        const auto &l = d.list();
        if (l.size() > 2) {
            if (const auto *i = std::get_if<long long>(&l[2].v))
                return qsizetype(*i);
            if (const auto *b = std::get_if<bool>(&l[2].v))   // (Python: a bool is an int)
                return qsizetype(*b);
        }
        if (key == u"brackets" && store) {        // which bracket: worked out from the text
            try {
                return scriptedit::bracket_place(store->text(std::get<QString>(l[1].v)));
            } catch (...) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    };
    auto script_rel = [](const mapcheck::Val &d) -> const QString * {
        const auto *l = std::get_if<std::vector<mapcheck::Val>>(&d.v);
        if (!l || l->size() <= 1)
            return nullptr;
        const auto *s = std::get_if<QString>(&(*l)[1].v);
        return s && s->contains(u'/') ? s : nullptr;
    };
    scriptedit::ProblemsByRel every;               // every script problem by its file (the borrowed editors' pages)
    for (const QString &pg : order)                // (the dict's order)
        for (const PageProblem &x : places[pg])
            if (const QString *rel = script_rel(x.it->data))
                every[*rel].push_back({where(x.key, x.it->data), x.sev, x.it->text});
    _script_problems = every;
    for (scriptpage::ScriptListPage *pg : script_pages)
        pg->set_problems(every);
    script_ws->set_problems(every);                // (the marker over the lines, in every editor)
    for (scriptpage::ScriptListPage *pg : script_pages)
        pg->ws->set_problems(every);
    scriptedit::ProblemsByRel by_rel;
    if (const auto it = places.find(u"events"_s); it != places.end())
        for (const PageProblem &x : it->second)
            if (const QString *rel = script_rel(x.it->data))
                by_rel[*rel].push_back({where(x.key, x.it->data), x.sev, x.it->text});
    OrderedMap<QString, std::pair<long long, long long>> ev_parts;
    for (const auto &[k, v] : parts(u"events"_s))
        ev_parts.insert(k, v);
    event_panel->set_problems(ev_parts, by_rel);
    if (prob_strip->isVisible() && _prob_cur) {    // the one shown: still there?
        const ProbCur cur = *_prob_cur;
        std::vector<PageProblem> lst;
        if (const auto it = places.find(cur.page); it != places.end())
            lst = it->second;
        std::optional<size_t> i;
        for (size_t j = 0; j < lst.size(); ++j)
            if (lst[j].key == cur.key && lst[j].it->text == cur.it->text) {
                i = j;
                break;
            }
        if (!i)
            prob_strip->hide();
        else {
            _prob_at[cur.page] = static_cast<long long>(*i);
            _prob_cur = ProbCur{cur.page, cur.key, std::make_shared<const mapcheck::Item>(*lst[*i].it)};
        }
    }
    _update_here();
}

std::pair<OptStr, OptStr> MainWindow::problem_place(const QString &key, const OptStr &t, const mapcheck::Item *item) const
{
    // (rail page, part of the page or None) where a problem of the checks is put right
    const mapcheck::Val d = item ? item->data : mapcheck::Val();
    const bool is_list = std::holds_alternative<std::vector<mapcheck::Val>>(d.v);
    OptStr dest;
    if (JUMP.contains(key))
        dest = JUMP.at(key);
    else if (key.startsWith(u"popcap_"_s))
        dest = u"pops"_s;
    if (dest == u"script") {
        OptStr rel;
        if (is_list && d.list().size() > 1)
            if (const auto *s = std::get_if<QString>(&d.list()[1].v))
                rel = *s;
        if (rel && !rel->isEmpty()) {
            const auto [pg, part] = script_place(*rel);
            return {pg, part};
        }
        return {u"events"_s, std::nullopt};
    }
    if (dest == u"cb")
        return {u"events"_s, u"cbs"_s};
    if (dest == u"buildtypes")
        return {u"rules"_s, u"buildings"_s};
    if (dest == u"evid")
        return {u"events"_s, u"event"_s};
    if (dest == u"wars")
        return {u"warfare"_s, u"wars"_s};
    if (dest == u"armies") {
        bool navy = false;
        if (is_list && !d.list().empty())
            if (const auto *s = std::get_if<QString>(&d.list()[0].v))
                navy = *s == u"navy_loc" || *s == u"ship_type";
        return {navy ? u"navies"_s : u"armies"_s, std::nullopt};
    }
    if (dest) {
        static const std::map<QString, QString> M = {{u"prov"_s, u"draw"_s}, {u"rivers"_s, u"draw"_s},
                                                     {u"terrain"_s, u"draw"_s}, {u"utility"_s, u"provinces"_s}};
        const auto it = M.find(*dest);
        return {it != M.end() ? it->second : *dest, std::nullopt};
    }
    if (t == u"map")
        return {u"draw"_s, std::nullopt};
    return {t, std::nullopt};
}

void MainWindow::_update_here()
{
    // the "N here ▸" button of the top bar: the problems of the page that is open
    if (!here_btn)
        return;
    const auto page = rail_page();
    std::vector<PageProblem> lst;
    if (page && *page != u"errors" && _page_problems.contains(*page))
        lst = _page_problems.at(*page);
    long long e = 0;
    for (const auto &x : lst)
        e += x.sev == mapcheck::SEV_ERROR;
    const long long wn = static_cast<long long>(lst.size()) - e;
    here_btn->setVisible(!lst.empty());
    if (!lst.empty()) {
        QStringList words;
        if (e)
            words << u"%1 error%2"_s.arg(e).arg(e == 1 ? u""_s : u"s"_s);
        if (wn)
            words << u"%1 warning%2"_s.arg(wn).arg(wn == 1 ? u""_s : u"s"_s);
        here_btn->setText(u"⚠ %1 on this page  ▸"_s.arg(words.join(u" · "_s)));
        const QString bg = e ? theme::ERROR_ : theme::WARN, fg = e ? u"#ffffff"_s : u"#14100a"_s,
                      hov = e ? u"#ff7070"_s : u"#ffc266"_s;
        here_btn->setStyleSheet(u"QPushButton { background: %1; color: %2; border: none; border-radius: 11px; "
                                u"font-weight: 700; padding: 4px 12px; } QPushButton:hover { background: %3; }"_s
                                    .arg(bg, fg, hov));
    }
    if (prob_strip->isVisible() && !_prob_jumping && _prob_page != page)
        prob_strip->hide();                        // (another page: its line goes)
}

void MainWindow::next_problem(const OptStr &page_)
{
    // Step to the next problem of a page (the one that is open): go where it is put right, and name it.
    const OptStr page = page_ && !page_->isEmpty() ? page_ : rail_page();
    if (!page || !_page_problems.contains(*page) || _page_problems.at(*page).empty())
        return;
    const std::vector<PageProblem> lst = _page_problems.at(*page);   // (a copy: a jump may check again)
    const long long n = static_cast<long long>(lst.size());
    const long long i = ((_prob_at.contains(*page) ? _prob_at.at(*page) : -1) + 1) % n;
    _prob_at[*page] = i;
    const PageProblem &pp = lst[size_t(i)];
    const QString key = pp.key;
    const auto it = std::make_shared<const mapcheck::Item>(*pp.it);
    _prob_cur = ProbCur{*page, key, it};
    long long e = 0;
    for (const auto &x : lst)
        e += x.sev == mapcheck::SEV_ERROR;
    const bool err = pp.sev == mapcheck::SEV_ERROR;
    prob_text->setText(u"%1 %2 of %3 · %4 — %5"_s.arg(err ? u"Error"_s : u"Warning"_s)
                           .arg(err ? i + 1 : i + 1 - e)
                           .arg(err ? e : n - e)
                           .arg(::rail::PAGE_NAMES.value(*page, *page), it->text));
    const QString dark = err ? u"#4a1512"_s : u"#4a3510"_s, edge = err ? theme::ERROR_ : theme::WARN;
    prob_strip->setStyleSheet(
        u"QWidget#probStrip { background: %1; border-bottom: 2px solid %2; border-left: 6px solid %2; } "
        u"QWidget#probStrip QLabel { color: #ffffff; font-size: 14px; font-weight: 600; background: transparent; }"_s
            .arg(dark, edge));
    prob_strip->show();
    _prob_jumping = true;
    struct Reset {                                 // (try / finally)
        bool &b;
        ~Reset() { b = false; }
    } reset{_prob_jumping};
    {
        if (!it->pids.empty())
            set_selection(it->pids);
        if (it->point)
            zoom_to_point(it->point->first, it->point->second);
        if (JUMP.contains(key) || key.startsWith(u"popcap_"_s))
            jump_to_problem(key, it.get());
    }
    _prob_jumping = false;
    _prob_page = rail_page();                      // (a step may lead to the page next door)
    _update_here();
}

void MainWindow::_problem_to_errors()
{
    if (!_prob_cur)
        return;
    const ProbCur cur = *_prob_cur;
    set_tab(u"errors"_s);
    for (const auto &c : chk_panel->cats)
        if (c.key == cur.key) {
            for (size_t k = 0; k < c.items.size(); ++k)
                if (c.items[k].text == cur.it->text) {
                    chk_panel->show_item(cur.key, int(k));
                    return;
                }
            return;
        }
}

namespace {
// The parts of an Errors-tab item's data tuple (Python indexes item.data; a missing / other-typed part = None).
OptStr dstr(const mapcheck::Val &d, size_t i)
{
    if (!std::holds_alternative<std::vector<mapcheck::Val>>(d.v) || i >= d.list().size())
        return std::nullopt;
    if (const auto *s = std::get_if<QString>(&d.list()[i].v))
        return *s;
    return std::nullopt;
}
std::optional<long long> dint(const mapcheck::Val &d, size_t i)
{
    if (!std::holds_alternative<std::vector<mapcheck::Val>>(d.v) || i >= d.list().size())
        return std::nullopt;
    if (const auto *n = std::get_if<long long>(&d.list()[i].v))
        return *n;
    return std::nullopt;
}
const mapcheck::Val *dpart(const mapcheck::Val &d, size_t i)
{
    if (!std::holds_alternative<std::vector<mapcheck::Val>>(d.v) || i >= d.list().size())
        return nullptr;
    return &d.list()[i];
}
}   // namespace

void MainWindow::jump_to_problem(const QString &key, const mapcheck::Item *item)
{
    // Errors tab double-click: open the tab where this problem is fixed — the camera stays exactly where it was
    // (rivers: Drawing → Rivers; pixel problems: Drawing; province data: Provinces).
    OptStr dest_;
    if (JUMP.contains(key))
        dest_ = JUMP.at(key);
    else if (key.startsWith(u"popcap_"_s))
        dest_ = u"pops"_s;
    if (!dest_) {
        message(u"This one isn't fixed on the map or in a province — use the fix here in the Errors tab, or edit the "
                u"file."_s, 8000);
        return;
    }
    const QString dest = *dest_;
    const mapcheck::Val none;
    const mapcheck::Val &d = item ? item->data : none;
    const bool has = item && d.truthy();
    auto *v = map;
    const QTransform keep_t = v->transform();
    const QPointF keep_c = v->mapToScene(v->viewport()->rect().center());
    QString where;
    if (dest == u"provinces") {
        set_tab(u"provinces"_s);
        where = u"Provinces"_s;
    } else if (dest == u"buildtypes") {
        go_page(u"rules"_s);
        rules_ws->set_page(u"buildings"_s);
        where = u"Game rules → Building types"_s;
    } else if (dest == u"pops") {
        set_tab(u"pops"_s);
        where = u"Population"_s;
    } else if (dest == u"adj") {
        set_tab(u"map"_s);
        set_map_sub(u"adj"_s);
        where = u"Adjacencies"_s;
        _jump_adj(item);
    } else if (dest == u"nations") {
        set_tab(u"nations"_s);
        where = u"Nation"_s;
        if (has)
            show_nation(dstr(d, 1));
    } else if (dest == u"armies") {
        set_tab(u"armies"_s);
        where = u"Armies"_s;
        if (has) {
            show_nation(dstr(d, 1));
            const OptStr k0 = dstr(d, 0);
            if (k0 == u"navy_loc" || k0 == u"ship_type") {
                army_panel->set_sub(u"navies"_s);
                if (const auto n = dint(d, 2))
                    army_panel->select_navy(*n);
            } else if (k0 == u"army_loc" || k0 == u"reg_type" || k0 == u"home") {
                army_panel->set_sub(u"armies"_s);
                if (const auto n = dint(d, 2))
                    army_panel->select_army(*n);
            } else if (k0 == u"over") {
                const mapcheck::Val *over = dpart(d, 3);
                if (over && over->truthy()) {
                    army_panel->set_sub(u"armies"_s);
                    if (const mapcheck::Val *first = dpart(*over, 0))
                        if (const auto n = dint(*first, 0))
                            army_panel->select_army(*n);
                }
            }
        }
    } else if (dest == u"diplomacy") {
        set_tab(u"diplomacy"_s);
        where = u"Diplomacy"_s;
        const OptStr k0 = has ? dstr(d, 0) : std::nullopt;
        if (k0 == u"treaty_remove" && dstr(d, 2) && mod->countries.contains(*dstr(d, 2))) {
            show_nation(dstr(d, 2));
            if (const OptStr b = dstr(d, 3); b && mod->countries.contains(*b))
                show_other(*b);
        } else if (k0 == u"sphere") {
            if (const mapcheck::Val *gps = dpart(d, 2))
                show_nation(dstr(*gps, 0));
            if (const OptStr o = dstr(d, 1))
                show_other(*o);
        } else if (k0 == u"nongp") {
            show_nation(dstr(d, 1));
            if (const OptStr o = dstr(d, 2); o && mod->countries.contains(*o))
                show_other(*o);
        }
    } else if (dest == u"evid") {
        if (has) {
            _script_refs();
            if (const OptStr rel = dstr(d, 1))
                _script_jump(*rel, dint(d, 2).value_or(0));
        }
        message(u"Scripts: the event with the doubled id."_s, 8000);
        return;
    } else if (dest == u"script") {
        OptStr rel = has ? dstr(d, 1) : std::nullopt;
        std::optional<long long> pos = has ? dint(d, 2) : std::nullopt;
        if (has && dstr(d, 0) == u"close" && rel)    // (a file that lacks its last }: its end)
            pos = doc->map<scriptstore::ScriptStore>(u"scripts"_s).text(*rel).size();
        if (has && pos && rel && _own_script_page(*rel, *pos))
            return;                                // (a rebel type / a pop type: on its own page)
        if (has && pos && rel) {
            _script_refs();
            _script_jump(*rel, *pos);
        }
        message(u"Scripts: the line is where the cursor stands."_s, 8000);
        return;
    } else if (dest == u"cb") {
        OptStr goal;
        if (has) {
            const OptStr k0 = dstr(d, 0);
            goal = k0 == u"order" ? OptStr(u"peace_order"_s) : k0 == u"name" ? dstr(d, 4) : dstr(d, 1);
        }
        go_page(u"events"_s);
        _script_refs();
        event_panel->show_cb(goal);
        message(u"Scripts → War goals%1."_s.arg(goal ? u": "_s + QString(*goal).replace(u'_', u' ') : QString()), 8000);
        return;
    } else if (dest == u"wars") {
        go_page(u"warfare"_s);
        war_ws->set_page(u"wars"_s);
        auto *wv = static_cast<moreviews::WarsView *>(*war_ws->views.get(u"wars"_s));
        if (has) {
            wv->cur = dstr(d, 1);
            wv->refresh(doc.get());
        }
        message(u"Warfare → Wars at the start."_s, 8000);
        return;
    } else if (dest == u"pos") {
        set_tab(u"map"_s);
        set_map_sub(u"pos"_s);
        where = u"Positions"_s;
    } else if (dest == u"utility") {
        set_tab(u"provinces"_s);
        set_prov_sub(u"utility"_s);
        if (has)
            if (const OptStr r = dstr(d, 1); r && mod->utility.contains(*r))
                _util_view(*r);
        message(u"Utility states: mark it in the suggestions (or use the fix in the Errors tab)."_s, 10000);
        return;
    } else {
        set_tab(u"map"_s);
        set_map_sub(u"draw"_s);
        if (!draw_panel->layer_btns.at(dest)->isChecked())
            draw_panel->layer_btns.at(dest)->setChecked(true);
        static const std::map<QString, QString> LAYER = {
            {u"prov"_s, u"Provinces"_s}, {u"rivers"_s, u"Rivers"_s}, {u"terrain"_s, u"Terrain"_s}, {u"colours"_s, u"Colours"_s}};
        where =u"Drawing → "_s + LAYER.at(dest);
    }
    std::vector<long long> pids;
    if (item)
        for (long long p : item->pids)
            if (mod->provinces.contains(p))
                pids.push_back(p);
    const bool split = (key == u"stray" || key == u"parts" || key == u"islands") && dest == u"prov" && !pids.empty();
    if (split)                                     // the province with the separate parts is the one to paint with —
        draw_panel->set_paint(pids[0]);            // its "Has islands" box is right there
    v->setTransform(keep_t);
    v->centerOn(keep_c);
    v->_zoomed();
    // white around the yellow terrain pixels, yellow elsewhere
    static const std::map<QString, double> JUMP_RING = {{u"rivers"_s, 4.5},         {u"stray"_s, 7.0},
                                                        {u"unknown"_s, 7.0},        {u"parts"_s, 7.0},
                                                        {u"landsea_rivers"_s, 7.0}, {u"landsea_terrain"_s, 7.0},
                                                        {u"south_edge"_s, 7.0}};
    if (item && item->point && JUMP_RING.contains(key))
        _jump_mark = JumpMark{QPointF(item->point->first, item->point->second), JUMP_RING.at(key),
                              key == u"landsea_terrain" ? Rgb{255, 255, 255} : Rgb{255, 230, 70}};
    else
        _jump_mark.reset();
    _apply_overlay();
    if (dest == u"adj") {
        message(u"%1: %2. (Edit crossings on the map is at the top of the panel.)"_s.arg(
                    where, has && dstr(d, 0) == u"row" ? u"the row is selected in the list — edit or delete it there"_s
                                                       : u"the missing crossing is highlighted"_s),
                12000);
        return;
    }
    if (dest == u"armies" || dest == u"diplomacy") {
        message(u"%1: %2."_s.arg(where, dest == u"armies" ? u"the army / navy is selected"_s
                                                          : u"the two nations are picked"_s),
                10000);
        return;
    }
    QString what = dest == u"rivers"
                       ? u"the broken river pixel is in the yellow ring — use the pen / eraser, or select the river and "
                         u"regenerate or delete it"_s
                   : dest == u"prov" ? u"paint the pixels in the yellow ring with the brush (right-click picks a province)"_s
                                     : u"the province is selected"_s;
    if (key.startsWith(u"landsea_"_s))
        what = u"the ringed pixels say the wrong land / sea — %1, or use the fix in the Errors tab"_s.arg(
            dest == u"terrain" ? u"they show bright yellow; paint them with the right terrain (right-click picks a colour)"_s
                               : u"they show red; the eraser makes land plain land again"_s);
    if (split) {
        const QString nm = py::strip(mod->provinces.get(pids[0])->name);   // (pids: only provinces of the mod)
        what = u"painting with #%1 %2%3 — tick \"Has islands\" if its separate parts are meant to be, or right-click the "
               u"province around it and paint over them"_s.arg(pids[0])
                   .arg(!nm.isEmpty() ? nm : u"(unnamed)"_s,
                        JUMP_RING.contains(key) ? u" (its separate part is in the yellow ring)"_s : QString());
    }
    message(dest != u"provinces" && dest != u"pops" ? u"%1: %2. (D switches drawing on / off.)"_s.arg(where, what)
                                                    : u"%1: %2."_s.arg(where, what),
            12000);
}

void MainWindow::_jump_adj(const mapcheck::Item *item)
{
    // Select an Errors-tab adjacency problem in the Adjacencies panel: its row, or the missing crossing.
    if (!item || !std::holds_alternative<std::vector<mapcheck::Val>>(item->data.v) || item->data.list().empty())
        return;
    const mapcheck::Val &d = item->data;
    const OptStr k0 = dstr(d, 0);
    if (k0 == u"row") {
        map_panel->select_row(dint(d, 1));
    } else if (k0 == u"pair") {
        map_panel->sel_uid.reset();
        if (const auto a = dint(d, 1), b = dint(d, 2); a && b)
            map_panel->sel_missing = std::make_pair(*a, *b);
        Q_EMIT map_panel->overlayChanged();
    } else if (k0 == u"river") {
        std::vector<long long> sea;                // the Meme-river tools work on the selected sea tiles
        if (const mapcheck::Val *s = dpart(d, 2); s && std::holds_alternative<std::vector<mapcheck::Val>>(s->v))
            for (const auto &x : s->list())
                if (const auto *n = std::get_if<long long>(&x.v))
                    sea.push_back(*n);
        set_selection({}, true, sea);
    }
}

void MainWindow::schedule_checks()
{
    // (the workspaces without the map don't change what the map checks look at: there the checks wait for a pause)
    // (C++ only, 2026-11-14 — the user: "drawing lags": while drawing, a pause between two strokes is no reason to
    // check the whole map — 1.5 s of rest)
    _check_timer->start(FULL_SCREEN.contains(tab) ? 2500 : draw_layer() ? 1500 : 700);
}

void MainWindow::_checks_due()
{
    if (reload_requested)
        return;
    if (_tool_window()) {                          // (blocks don't change what the map checks look at)
        _check_timer->start(3000);
        return;
    }
    // (C++ only, 2026-11-10, the performance round: never in the middle of a stroke or a drag — the whole-map checks
    // would stop the brush for a moment; they run once the button is let go)
    if (QApplication::mouseButtons() != Qt::NoButton) {
        _check_timer->start(draw_layer() ? 1000 : 300);   // (drawing: not the moment the stroke ends — the next may follow)
        return;
    }
    chk_panel->refresh(doc.get());
}

QWidget *MainWindow::active_map_panel() const
{
    if (map_sub == u"pos")
        return pos_panel;
    if (map_sub == u"draw")
        return draw_panel;
    if (map_sub == u"canal")
        return canal_panel;
    return map_panel;
}

// The panel whose markers the map shows: the Errors tab's list, or the Map tab's current mode.
QWidget *MainWindow::overlay_panel() const
{
    if (tab == u"errors")
        return chk_panel;
    if (tab == u"armies")
        return army_panel;
    if (tab == u"diplomacy")
        return diplo_panel;
    return tab == u"map" ? active_map_panel() : nullptr;
}

bool MainWindow::_cancel_handler(QWidget *p)
{
    if (p == map_panel)
        return map_panel->handler.cancel();
    if (p == pos_panel)
        return pos_panel->handler.cancel();
    if (p == canal_panel)
        return canal_panel->handler.cancel();
    if (p == draw_panel) {                         // (its handler is the current layer's)
        const QString l = draw_panel->layer();
        if (l == u"terrain")
            return draw_panel->terrain_box->handler.cancel();
        if (l == u"rivers")
            return draw_panel->river_box->handler.cancel();
        return draw_panel->prov_handler.cancel();
    }
    return false;
}

void MainWindow::set_map_sub(const QString &key)
{
    if (key == map_sub)
        return;
    _jump_mark.reset();
    map_panel->handler.cancel();
    pos_panel->handler.cancel();
    canal_panel->handler.cancel();
    if (mode == u"adjpick")
        mode = u"edit"_s;
    const bool was_layer = bool(draw_layer());
    map_sub = key;
    settings->setValue(u"mapSub"_s, key);
    if (was_layer || draw_layer())
        render_map();
    for (const auto &[k, b] : map_sub_btns)
        b->setChecked(k == key);
    for (const auto &[k, b] : adj_sub_btns)
        b->setChecked(k == key);
    adj_seg_bar->setVisible(key == u"adj" || key == u"canal");
    _sync_rail();
    map->name_layer->setVisible(tab == u"map" && key == u"pos");
    _sync_map_tool();
    _show_banner();
    refresh_panel();
}

void MainWindow::_sync_map_tool()
{
    QWidget *p = active_map_panel();
    const bool always = p == canal_panel;          // (CanalPanel.always_handler)
    const QPushButton *eb = p == map_panel ? map_panel->edit_btn : p == pos_panel ? pos_panel->edit_btn
                          : p == draw_panel ? draw_panel->edit_btn : nullptr;
    const bool on = tab == u"map" && (always || (eb && eb->isChecked()));
    for (QWidget *q : {static_cast<QWidget *>(map_panel), static_cast<QWidget *>(pos_panel),
                       static_cast<QWidget *>(draw_panel), static_cast<QWidget *>(canal_panel)})
        if (q != p || !on)
            _cancel_handler(q);
    mapview::MouseHandler *h = nullptr;
    if (on) {
        if (p == map_panel)
            h = &map_panel->handler;
        else if (p == pos_panel)
            h = &pos_panel->handler;
        else if (p == canal_panel)
            h = &canal_panel->handler;
        else
            h = draw_panel->handler();
    }
    map->mouse_handler = h;
    if (state_panel && tab == u"provinces" && prov_sub == u"states" && state_panel->add_btn->isChecked())
        map->mouse_handler = &state_panel->handler;   // States: "Add provinces — click them on the map"
    _update_draw_pill();
}

// While drawing, a gold "DRAW MODE" label sits at the top of the map (a click on it stops drawing).
void MainWindow::_update_draw_pill()
{
    if (!draw_panel || !map)
        return;
    const bool on = tab == u"map" && map_sub == u"draw" && draw_panel->edit_btn->isChecked();
    static const std::map<QString, QString> NAMES = {
        {u"prov"_s, u"Provinces"_s}, {u"terrain"_s, u"Terrain"_s}, {u"rivers"_s, u"Rivers"_s}, {u"colours"_s, u"Colours"_s}};
    const QString text = on ?u"✎  DRAW MODE · %1   (D)"_s.arg(NAMES.at(draw_panel->layer())) : QString();
    map->set_mode_pill(text, [this] { toggle_draw(); });
    if (map->_pill)
        map->_pill->setToolTip(u"Drawing on the map — click here or press D to stop"_s);
}

// Shortcut D: switch drawing on the map on / off — from another tab it opens Drawing, drawing.
void MainWindow::toggle_draw()
{
    if (tab != u"map" || map_sub != u"draw") {
        set_tab(u"map"_s);
        set_map_sub(u"draw"_s);
        draw_panel->edit_btn->setChecked(true);
    } else {
        draw_panel->edit_btn->setChecked(!draw_panel->edit_btn->isChecked());
    }
    _update_draw_pill();
}

void MainWindow::update_overlay(QObject *sender)
{
    QWidget *p = overlay_panel();
    if (!p)
        return;
    if (sender && sender != p)                     // (a panel the map doesn't show)
        return;
    _apply_overlay(p);
}

// Put the markers of panel p (default: the one the map shows) on the map, whoever asks.
void MainWindow::_apply_overlay(QWidget *p)
{
    if (!p)
        p = overlay_panel();
    if (!p)
        return;
    std::vector<mapview::Shape> shapes;
    if (p == chk_panel)
        shapes = chk_panel->shapes();
    else if (p == map_panel)
        shapes = map_panel->shapes();
    else if (p == canal_panel)
        shapes = canal_panel->shapes();
    else if (p == pos_panel)
        shapes = pos_panel->shapes();
    else if (p == draw_panel)
        shapes = draw_panel->shapes();
    else if (p == army_panel)
        shapes = army_panel->shapes();
    else if (p == diplo_panel)
        shapes = diplo_panel->shapes();
    for (auto &s : _jump_shapes())
        shapes.push_back(std::move(s));
    map->shape_layer->set_shapes(shapes);
    map->name_layer->set_items(p == pos_panel ? pos_panel->name_items() : std::vector<mapview::NameItem>{});
}

bool MainWindow::_map_click_hook(double x, double y, double tol, Qt::KeyboardModifiers mods)
{
    QWidget *p = overlay_panel();
    if (!p || mode != u"edit" || (mods & (Qt::ControlModifier | Qt::ShiftModifier)))
        return false;
    const auto tag = map->shape_layer->hit(x, y, tol);
    if (!tag && p == chk_panel) {
        if (const auto hit = chk_panel->problem_at(x, y)) {   // a problem pixel / a province with a problem
            chk_panel->select_problem(hit->first, hit->second);
            return true;
        }
    }
    if (!tag || (!tag->isEmpty() && (*tag)[0].toString() == u"preview"))
        return false;
    if (p == chk_panel)
        chk_panel->map_clicked(*tag);
    else if (p == map_panel)
        map_panel->map_clicked(*tag);
    else if (p == canal_panel)
        canal_panel->map_clicked(*tag);
    else if (p == pos_panel)
        pos_panel->map_clicked(*tag);
    else if (p == draw_panel)
        draw_panel->map_clicked(*tag);
    else if (p == army_panel)
        army_panel->map_clicked(*tag);
    else if (p == diplo_panel)
        diplo_panel->map_clicked(*tag);
    return true;
}

void MainWindow::start_adj_pick(const QString &field)
{
    mode = u"adjpick"_s;
    _adj_field = field;
    static const std::map<QString, QString> F = {{u"a"_s, u"From"_s}, {u"b"_s, u"To"_s}, {u"through"_s, u"Through"_s}};
    message(u"Click the province or sea tile for '%1' (Esc to cancel)"_s.arg(F.at(field)), 0);
}
void MainWindow::zoom_to_point(double x, double y, double z)
{
    const double cur = map->transform().m11();
    if (cur < z)
        map->zoom_by(z / cur, false);
    map->centerOn(QPointF(x, y));
    map->_zoomed();
}

void MainWindow::on_click(long long pid, Qt::KeyboardModifiers mods)
{
    const moddata::Province *pr = mod->provinces.get(pid);
    bool multi = bool(mods & (Qt::ControlModifier | Qt::ShiftModifier));
    if (mode == u"flagpick") {                     // the flag studio waits: this province's owner's flags
        const OptStr owner = pr && !pr->sea ? doc->prov_state(pid).owner : std::nullopt;
        if (!owner || owner->isEmpty()) {
            message(u"No owner there — click a nation's province (Esc: back to the studio as it was)."_s, 6000);
            return;
        }
        mode = u"edit"_s;
        open_flag_studio(std::nullopt, owner);
        return;
    }
    if (mode == u"adjpick") {
        mode = u"edit"_s;
        static const std::map<QString, QString> F = {{u"a"_s, u"From"_s}, {u"b"_s, u"To"_s}, {u"through"_s, u"Through"_s}};
        if (pr) {
            map_panel->set_pick(_adj_field, pid);
            const QString nm = py::strip(pr->name);
            message(u"%1: #%2 %3"_s.arg(F.at(_adj_field)).arg(pid).arg(nm.isEmpty() ? u"(unnamed)"_s : nm));
        } else {
            message(u"No province there — pick cancelled"_s);
        }
        return;
    }
    if (mode == u"split") {
        if (pr && !pr->sea)
            split_panel->toggle_target(pid);
        return;
    }
    if (mode == u"statepick") {
        mode = u"edit"_s;
        const OptStr target = pr && !pr->sea ? pr->region : std::nullopt;
        if (target && !target->isEmpty() && _state_del && !_state_del->isEmpty())
            _do_state_delete(target);
        else if (target && !target->isEmpty())
            _do_state_move(*target);
        else
            message(u"No state there — move cancelled"_s);
        return;
    }
    if (mode == u"armymove" || mode == u"navymove") {
        const QString kind = mode;
        mode = u"edit"_s;
        if (!pr) {
            message(u"No province there — move cancelled"_s);
        } else if (kind == u"armymove" && pr->sea) {
            message(u"⚠ Armies start on land — move cancelled"_s);
        } else {
            army_panel->set_location(pid);
            const OptStr own = !pr->sea ? doc->prov_state(pid).owner : std::nullopt;
            message(u"→ #%1 %2%3 (written on Apply, Ctrl+Z undoes it)."_s.arg(pid).arg(
                pr->name, pr->sea || own == nation_tag ? QString()
                                                       : u"  ⚠ not %1's province"_s.arg(nation_tag.value_or(u"None"_s))));
        }
        return;
    }
    if (mode == u"nationcap") {
        mode = u"edit"_s;
        const QString t = nation_tag.value_or(u"None"_s);
        if (pr && !pr->sea && doc->prov_state(pid).owner == nation_tag) {
            nations_panel->set_capital(pid);
            message(u"Capital of %1 → #%2 %3 (written on Apply, Ctrl+Z undoes it)."_s.arg(t).arg(pid).arg(pr->name));
        } else {
            message(u"⚠ That isn't one of %1's provinces — capital unchanged."_s.arg(t));
        }
        return;
    }
    if (mode == u"capital") {
        const moddata::Region *r = capital_region ? mod->regions.get(*capital_region) : nullptr;
        if (r && std::find(r->provinces.begin(), r->provinces.end(), pid) != r->provinces.end()) {
            doc->set_capital(*capital_region, pid);
            std::vector<long long> caps;
            for (const auto &[rid, _r] : mod->regions)
                if (const auto c = doc->capital(rid))
                    caps.push_back(*c);
            map->set_capitals(caps);
            message(u"State capital of %1 is now %2 (#%3)"_s.arg(*capital_region, pr->name).arg(pid));
        } else {
            message(u"That province is not in state %1 — state capital unchanged"_s.arg(capital_region.value_or(u"None"_s)));
        }
        mode = u"edit"_s;
        capital_region.reset();
        update_highlight();
        refresh_panel();
        return;
    }
    if (tab == u"armies") {
        const OptStr owner = pr && !pr->sea ? doc->prov_state(pid).owner : std::nullopt;
        // (C++ only) Generals / Admirals: Ctrl+click selects several nations
        if ((army_panel->sub == u"generals" || army_panel->sub == u"admirals") && owner && !owner->isEmpty()) {
            if (multi) {
                army_panel->leaders->toggle_pick(*owner);
                return;
            }
            army_panel->leaders->pick_only(*owner);   // (a plain click: this nation alone — then the page follows)
        }
        if (owner && !owner->isEmpty() && owner != nation_tag)
            show_nation(owner);
        else if (pr && !pr->sea && (!owner || owner->isEmpty()))
            message(u"#%1 %2 has no owner at the start."_s.arg(pid).arg(pr->name));
        else
            army_panel->deselect();                // the nation / the sea: back to the whole nation
        return;
    }
    if (tab == u"diplomacy" && (!pr || pr->sea))
        return;
    if (tab == u"events") {                        // a nation's own chances
        const OptStr owner = pr && !pr->sea ? doc->prov_state(pid).owner : std::nullopt;
        if (owner && !owner->isEmpty())
            event_panel->set_nation(owner == event_panel->nation && !multi ? OptStr() : owner);
        else if (pr && !pr->sea)
            message(u"#%1 %2 has no owner at the start."_s.arg(QString::number(pid), pr->name));
        return;
    }
    if (!pr) {
        if (!multi)
            set_selection({});
        return;
    }
    if (pr->sea) {
        std::vector<long long> cur = sea_selection;
        if (multi) {
            const auto it = std::find(cur.begin(), cur.end(), pid);
            if (it != cur.end())
                cur.erase(it);
            else
                cur.push_back(pid);
            set_selection(selection, true, cur);
        } else {
            set_selection({}, true, {pid});
        }
        return;
    }
    if (tab == u"nations" && !multi) {
        const OptStr owner = doc->prov_state(pid).owner;
        if (owner && !owner->isEmpty())
            show_nation(owner);
        else
            message(u"#%1 %2 has no owner at the start."_s.arg(pid).arg(pr->name));
        return;
    }
    if (tab == u"diplomacy") {
        const OptStr owner = doc->prov_state(pid).owner;
        if (!owner || owner->isEmpty())
            message(u"#%1 %2 has no owner at the start."_s.arg(pid).arg(pr->name));
        else if (!nation_tag || (mods & Qt::ShiftModifier) || owner == nation_tag)
            show_nation(owner);
        else
            show_other(*owner);
        return;
    }
    if (tab == u"provinces" && prov_sub == u"utility" && util_view && util_edit) {
        const moddata::Region *r = mod->utility.get(*util_view);
        QList<long long> group{pid};
        if ((mods & Qt::ShiftModifier) && pr->region && !pr->region->isEmpty() && mod->regions.contains(*pr->region)) {
            const auto &ps = mod->regions.get(*pr->region)->provinces;
            group = QList<long long>(ps.begin(), ps.end());
        }
        const bool add = std::find(r->provinces.begin(), r->provinces.end(), pid) == r->provinces.end();
        const auto [ok, msg] = doc->utility_members(*util_view, group, add);
        message((ok ? QString() : u"⚠ "_s) + msg + (ok ? u" Ctrl+Z undoes it."_s : QString()), 8000);
        render_map();
        return;
    }
    last_clicked = pid;
    // Shift+click: the whole state · Alt+click: everything of that owner · Alt+Shift+click: the continent
    const bool shift = bool(mods & Qt::ShiftModifier), alt = bool(mods & Qt::AltModifier);
    multi = bool(mods & Qt::ControlModifier);
    std::vector<long long> group;
    if (alt && shift) {
        auto &cf = doc->map<mapfiles::MembershipFile>(u"continent"_s);
        std::set<long long> g;
        const auto mem = cf.membership();
        for (const QString &nm : mem.value(pid))
            if (const auto *l = cf.lists.get(nm))
                for (long long p : *l)
                    if (const auto *q = mod->provinces.get(p); q && !q->sea)
                        g.insert(p);
        group.assign(g.begin(), g.end());
        if (group.empty())
            group = {pid};
    } else if (alt) {
        const OptStr owner = doc->prov_state(pid).owner;
        for (const auto &[p, q] : mod->provinces)
            if (!q.sea && doc->prov_state(p).owner == owner)
                group.push_back(p);
    } else if ((view_mode == u"state" || shift) && pr->region && !pr->region->isEmpty()) {
        group = mod->regions.get(*pr->region)->provinces;
    } else {
        group = {pid};
    }
    if (multi) {
        const std::set<long long> sel(selection.begin(), selection.end());
        bool all = true;
        for (long long p : group)
            all = all && sel.contains(p);
        if (all) {
            const std::set<long long> g(group.begin(), group.end());
            std::vector<long long> keep;
            for (long long p : selection)
                if (!g.contains(p))
                    keep.push_back(p);
            set_selection(keep);
        } else {
            std::vector<long long> s = selection;
            s.insert(s.end(), group.begin(), group.end());
            set_selection(s);
        }
    } else {
        set_selection(group);
    }
}

void MainWindow::on_hover(long long pid, int x, int y)
{
    st_cursor->setText(x >= 0 ? u"Cursor: %1, %2"_s.arg(x).arg(y) : u"Cursor: —"_s);
    if (tab == u"errors" && x >= 0) {
        const double m11 = map->transform().m11();
        const auto tag = map->shape_layer->hit(x + 0.5, y + 0.5, 7.0 / (m11 ? m11 : 1.0));
        const QString t = tag ? chk_panel->describe(*tag) : chk_panel->hover_text(x, y);
        if (!t.isEmpty()) {
            st_hover->setText(t);
            return;
        }
    }
    const moddata::Province *pr = mod->provinces.get(pid);
    if (!pr) {
        st_hover->setText(u"Hovered: —"_s);
        return;
    }
    if ((draw_layer() || _prov_mismatch_shown()) && x >= 0) {
        const QString info = draw_panel->pixel_info(x, y);
        if (!info.isEmpty()) {
            st_hover->setText(info);
            return;
        }
    }
    if (QWidget *op = overlay_panel(); op && x >= 0) {
        const double m11 = map->transform().m11();
        if (const auto tag = map->shape_layer->hit(x + 0.5, y + 0.5, 7.0 / (m11 ? m11 : 1.0))) {
            QString t;
            if (op == chk_panel)
                t = chk_panel->describe(*tag);
            else if (op == map_panel)
                t = map_panel->describe(*tag);
            else if (op == canal_panel)
                t = canal_panel->describe(*tag);
            else if (op == pos_panel)
                t = pos_panel->describe(*tag);
            else if (op == draw_panel)
                t = draw_panel->describe(*tag);
            else if (op == army_panel)
                t = army_panel->describe(*tag);
            else if (op == diplo_panel)
                t = diplo_panel->describe(*tag);
            st_hover->setText(t);
            return;
        }
    }
    if (pr->sea) {
        st_hover->setText(u"Hovered: %1 (#%2, sea)"_s.arg(pr->name.trimmed().isEmpty() ? u"unnamed"_s : pr->name.trimmed())
                              .arg(pid));
        return;
    }
    const long long tot = doc->total(pid);
    const auto &st = doc->prov_state(pid);
    QString extra;
    if (color_mode == u"culture_mix" || color_mode == u"religion_mix" || color_mode == u"culture" ||
        color_mode == u"religion") {
        const bool cul = color_mode.startsWith(u"culture"_s);
        OrderedMap<OptStr, long long> agg;
        for (const auto &p : doc->province_pops(pid))
            agg[cul ? p.culture : p.religion] += p.size;
        if (tot) {
            std::vector<std::pair<OptStr, long long>> top(agg.begin(), agg.end());
            std::stable_sort(top.begin(), top.end(), [](const auto &a, const auto &b) { return -a.second < -b.second; });
            QStringList parts;
            for (size_t i = 0; i < std::min<size_t>(top.size(), 5); ++i) {
                const OptStr &k = top[i].first;
                QString name = k.value_or(u"None"_s);
                if (k) {
                    if (cul) {
                        if (const auto *cu = mod->cultures.get(*k))
                            name = cu->name;
                    } else if (const auto *re = mod->religions.get(*k)) {
                        name = re->name;
                    }
                }
                const double v = double(top[i].second) * 100.0 / double(tot);
                const QString pct = v >= 9.5 ? py::fmt_f(v, 0) : v >= 0.05 ? py::fmt_f(v, 1) : u"<0.1"_s;
                parts << u"%1 %2%"_s.arg(name, pct);
            }
            extra = u"  ·  "_s + parts.join(u" · "_s) + (top.size() > 5 ? u" · …"_s : u""_s);
        }
    }
    if (tab == u"provinces")
        extra = u"  LR %1  %2  fort %3  rail %4"_s.arg(st.life_rating ? QString::number(*st.life_rating) : u"None"_s,
                                                     st.trade_goods.value_or(QString()))
                    .arg(st.fort)
                    .arg(st.railroad);
    if (color_mode == u"buildings") {
        const bool has_region = pr->region && !pr->region->isEmpty();
        const auto facs = has_region ? doc->state_factories(*pr->region)
                                     : std::vector<std::pair<long long, history::Factory>>{};
        OrderedMap<QString, long long> names;      // (a Counter: first seen first)
        for (const auto &[_p, f] : facs)
            names[f.building.value_or(u"None"_s).replace(u'_', u' ')] += 1;
        QStringList parts;
        for (const auto &[k, v] : names)
            parts << k + (v > 1 ? u" ×"_s + QString::number(v) : u""_s);
        extra = u"  ·  fort %1 · naval base %2 · railroad %3 · factories of the state %4: %5%6"_s.arg(
            QString::number(st.fort), QString::number(st.naval_base), QString::number(st.railroad),
            has_region ? *pr->region : u"—"_s, QString::number(static_cast<long long>(facs.size())),
            facs.empty() ? u""_s : u" ("_s + parts.join(u", "_s) + u")"_s);
    }
    if (mapmodes::handles(color_mode)) {
        const QString more = more_modes->describe(color_mode, pid);
        extra = more.isEmpty() ? u""_s : u"  ·  "_s + more;
    }
    st_hover->setText(u"Hovered: %1 (#%2)  %3  pop %4  %5%6"_s.arg(pr->name)
                          .arg(pid)
                          .arg(pr->region.value_or(QString()), widgets::fmt(tot), st.owner.value_or(QString()), extra));
}

// Diplomacy: a right-click makes that nation the main one. Armies: one brigade of the selected army from that
// province goes.
void MainWindow::on_right_click(long long pid, Qt::KeyboardModifiers mods)
{
    if (mode != u"edit")
        return;
    if (tab == u"armies") {
        if (const moddata::Province *pr = mod->provinces.get(pid); pr && !pr->sea)
            army_panel->right_clicked(pid);
        return;
    }
    if (tab == u"events") {
        // the Events tab: the right-clicked nation (Ctrl: the province) becomes the focus
        const moddata::Province *pr = mod->provinces.get(pid);
        if (!pr || pr->sea)
            return;
        const OptStr owner = doc->prov_state(pid).owner;
        if ((mods & Qt::ControlModifier) || !owner || owner->isEmpty())
            event_panel->focus_symbol(u"province"_s, QString::number(pid));
        else
            event_panel->focus_symbol(u"nation"_s, *owner);
        return;
    }
    if (tab != u"diplomacy")
        return;
    const moddata::Province *pr = mod->provinces.get(pid);
    const OptStr owner = pr && !pr->sea ? doc->prov_state(pid).owner : std::nullopt;
    if (owner && !owner->isEmpty()) {
        show_nation(owner);
        message(u"%1 is the main nation — click others to set their relations with it."_s.arg(*owner), 6000);
    }
}

void MainWindow::show_other(const QString &tag)
{
    // Diplomacy: the second nation (B)
    if (OptStr(tag) == nation_tag)
        return;
    diplo_b = tag;
    refresh_panel();
    message(u"%1 ↔ %2 — set their relations in the panel. Shift+click makes a nation the main one."_s.arg(
                nation_tag.value_or(u"None"_s), tag),
            8000);
}

void MainWindow::_econ_show(const QList<long long> &pids, const QString &level)
{
    // Output estimate → "Jump to Provinces & states tab": its provinces picked there (a state: the States sub-tab),
    // the map framed on them
    if (pids.isEmpty())
        return;
    go_page(u"provinces"_s);
    set_prov_sub(level == u"state" ? u"states"_s : u"provinces"_s);
    const std::vector<long long> ps(pids.begin(), pids.end());
    set_selection(ps);
    if (const auto box = mapdata->bbox(ps)) {
        const auto b = *box;
        QTimer::singleShot(0, this, [this, b] {
            map->fitInView(QRectF(double(b[0] - 40), double(b[1] - 40), double(b[2] - b[0] + 80), double(b[3] - b[1] + 80)),
                           Qt::KeepAspectRatio);
        });
    }
}

void MainWindow::escape()
{
    if (cmd_palette && cmd_palette->isVisible()) {
        cmd_palette->hide();
        return;
    }
    if (tab == u"econest" && (econ_ws->_sel || econ_ws->focus)) {
        econ_ws->detach();                         // (Output estimate: let go — the whole world again)
        return;
    }
    if (mode == u"flagpick") {                     // (the studio comes back as it was)
        mode = u"edit"_s;
        if (flag_win) {
            flag_win->showNormal();
            flag_win->raise();
        }
        message(u"Cancelled — the flag studio is back"_s);
        return;
    }
    if (tab == u"map" && _cancel_handler(active_map_panel())) {
        message(u"Cancelled"_s);
    } else if (tab == u"provinces" && prov_sub == u"states" && state_panel->handler.cancel()) {
        message(u"Stopped adding provinces"_s);
    } else if (mode == u"adjpick") {
        mode = u"edit"_s;
        message(u"Pick cancelled"_s);
    } else if (mode == u"statepick") {
        mode = u"edit"_s;
        message(_state_del && !_state_del->isEmpty() ? u"Delete cancelled"_s : u"Move cancelled"_s);
        _state_del.reset();
    } else if (mode == u"nationcap") {
        mode = u"edit"_s;
        message(u"Capital pick cancelled"_s);
    } else if (mode == u"armymove" || mode == u"navymove") {
        mode = u"edit"_s;
        message(u"Move cancelled"_s);
    } else if (tab == u"armies" && army_panel->deselect()) {
    } else if (tab == u"diplomacy" && ((diplo_b && !diplo_b->isEmpty()) || (nation_tag && !nation_tag->isEmpty()))) {
        if (diplo_b && !diplo_b->isEmpty()) {
            diplo_b.reset();
            refresh_panel();
        } else {
            show_nation(std::nullopt);
        }
    } else if (mode == u"split") {
        end_split();
    } else if (tab == u"provinces" && prov_sub == u"utility" && (util_edit || util_view)) {
        if (util_edit)
            _util_edit_toggled(false);
        else
            _util_view(QString());
    } else if (mode == u"capital") {
        mode = u"edit"_s;
        capital_region.reset();
        update_highlight();
        message(u"State capital change cancelled"_s);
    } else {
        set_selection({});
    }
}

// Regions whose provinces are all selected (only in state view).
std::vector<QString> MainWindow::selected_states() const
{
    if (view_mode != u"state" || selection.empty())
        return {};
    const std::set<long long> sel(selection.begin(), selection.end());
    std::vector<QString> rids;
    for (long long pid : selection) {
        const OptStr &rid = mod->provinces.get(pid)->region;
        if (!rid || rid->isEmpty() || std::find(rids.begin(), rids.end(), *rid) != rids.end())
            continue;
        bool all = true;
        for (long long p : mod->regions.get(*rid)->provinces)
            all = all && sel.contains(p);
        if (all)
            rids.push_back(*rid);
    }
    return rids;
}

std::optional<std::pair<QString, QString>> MainWindow::header() const
{
    const moddata::Mod &m = *mod;
    const auto &sel = selection;
    const auto &sea = sea_selection;
    if (sel.empty() && !sea.empty()) {
        if (sea.size() == 1)
            return std::make_pair(u"Sea tile — %1  (#%2)"_s.arg(m.provinces.get(sea[0])->name).arg(sea[0]),
                                  u"Sea tiles are listed under sea_starts in map/default.map and have no history."_s);
        return std::make_pair(u"%1 sea tiles selected"_s.arg(sea.size()), u"Sea tiles have no history."_s);
    }
    if (sel.empty())
        return std::make_pair(u"Nothing selected"_s,
                              u"Click a province on the map. Shift+click: its whole state · Alt+click: all of its "
                              u"owner · Alt+Shift+click: its continent · hold Ctrl as well to add / remove."_s);
    const auto states = selected_states();
    if (sel.size() == 1) {
        const moddata::Province &pr = *m.provinces.get(sel[0]);
        const OptStr otag = doc->prov_state(pr.id).owner;
        QString owner = u"no owner"_s;
        if (otag && !otag->isEmpty()) {
            const moddata::Country *cn = m.countries.get(*otag);
            owner = cn ? cn->name : *otag;
        }
        QStringList parts{u"Owner: %1"_s.arg(owner)};
        if (pr.region && !pr.region->isEmpty()) {
            const auto cap = doc->capital(*pr.region);
            parts << u"State: %1"_s.arg(*pr.region);
            if (cap == pr.id)
                parts << u"<span style='color:%1'>★ State capital</span>"_s.arg(theme::GOLD);
        }
        return std::make_pair(u"%1  (#%2)"_s.arg(pr.name).arg(pr.id), parts.join(u" · "_s));
    }
    if (states.size() == 1 && sel.size() == m.regions.get(states[0])->provinces.size()) {
        const QString &rid = states[0];
        std::set<QString> owners;
        for (long long p : sel)
            if (const OptStr &o = m.provinces.get(p)->owner; o && !o->isEmpty())
                owners.insert(*o);
        const QString nm = m.regions.get(rid)->name;
        return std::make_pair(nm == rid ? u"State %1"_s.arg(rid) : u"%1 (%2)"_s.arg(nm, rid),
                              u"%1 provinces · Owner: %2"_s.arg(sel.size()).arg(
                                  owners.empty() ? u"—"_s : QStringList(owners.begin(), owners.end()).join(u", "_s)));
    }
    return std::make_pair(u"%1 provinces selected"_s.arg(sel.size()),
                          !states.empty() ? u"%1 whole states"_s.arg(states.size()) : u"Ctrl+click to add or remove"_s);
}

panels::Header MainWindow::header3() const
{
    const auto [title, sub] = *header();
    const moddata::Mod &m = *mod;
    const auto &sel = selection;
    QString info;
    if (sel.empty()) {
        if (sea_selection.size() > 1) {
            QStringList ps;
            for (size_t i = 0; i < sea_selection.size() && i < 40; ++i)
                ps << u"#%1"_s.arg(sea_selection[i]);
            info = ps.join(u", "_s);
        }
    } else if (sel.size() == 1) {
        const moddata::PopProvince *src = doc->src.get(sel[0]);
        info = u"Pop file: %1"_s.arg(src ? src->file : u"(none yet → %1)"_s.arg(doc->default_file_for(sel[0])));
    } else {
        const auto states = selected_states();
        QStringList ps;
        if (states.size() == 1 && sel.size() == m.regions.get(states[0])->provinces.size()) {
            for (long long p : sel)
                ps << QString::number(p);
            info = u"Provinces: "_s + ps.join(u", "_s);
        } else {
            for (size_t i = 0; i < sel.size() && i < 40; ++i)
                ps << QString::number(sel[i]);
            info = ps.join(u", "_s) + (sel.size() > 40 ? u" …"_s : QString());
        }
    }
    return {title, sub, info};
}

void MainWindow::refresh_panel()
{
    if (mode == u"split")
        return;
    map->set_nation_capital(tab == u"nations" || tab == u"diplomacy" ? _nation_capital() : std::nullopt);
    if (tab == u"provinces") {
        stack->setCurrentWidget(prov_host);
        if (prov_sub == u"states") {
            prov_stack->setCurrentWidget(state_panel);
            state_panel->refresh(*doc, selection);
            return;
        }
        if (prov_sub == u"utility") {
            prov_stack->setCurrentWidget(util_panel);
            util_panel->refresh(doc.get(), util_view, util_edit, selection);
            return;
        }
        prov_stack->setCurrentWidget(prov_panel);
        prov_panel->refresh(*doc, QList<long long>(selection.begin(), selection.end()), header3(), last_clicked);
        if (color_mode == u"cores")
            _render_timer->start(50);
        return;
    }
    if (tab == u"map") {
        stack->setCurrentWidget(map_tab);
        if (map_sub == u"draw") {
            map_stack->setCurrentWidget(draw_panel);
            _draw_doc = doc;
            draw_panel->refresh(*doc, selection, sea_selection);
        } else if (map_sub == u"pos") {
            map_stack->setCurrentWidget(pos_panel);
            pos_panel->refresh(*doc, selection, header());
        } else if (map_sub == u"canal") {
            map_stack->setCurrentWidget(canal_panel);
            canal_panel->refresh(*doc, selection, header());
        } else {
            map_stack->setCurrentWidget(map_panel);
            map_panel->refresh(*doc, selection, sea_selection, header());
        }
        return;
    }
    if (tab == u"nations") {
        stack->setCurrentWidget(nations_panel);
        nations_panel->refresh(doc.get(), mod->nation_data.get(), nation_tag, nation_stats(nation_tag));
        return;
    }
    if (tab == u"armies") {
        stack->setCurrentWidget(army_panel);
        army_panel->refresh(doc.get(), nation_tag);
        _apply_overlay();
        return;
    }
    if (tab == u"diplomacy") {
        stack->setCurrentWidget(diplo_panel);
        diplo_panel->refresh(doc.get(), nation_tag, diplo_b);
        _apply_overlay();
        return;
    }
    if (tab == u"technology") {
        tech_ws->refresh(doc.get());
        return;
    }
    if (tab == u"politics") {
        pol_ws->refresh(doc.get());
        return;
    }
    if (tab == u"production") {
        prod_ws->refresh(doc.get());
        return;
    }
    if (tab == u"changelog") {
        cl_ws->refresh(doc.get(), _changelog_pending());
        return;
    }
    if (tab == u"warfare") {
        war_ws->refresh(doc.get());
        return;
    }
    if (tab == u"rules") {
        rules_ws->refresh(doc.get());
        return;
    }
    if (tab == u"library") {
        lib_ws->refresh(doc.get());
        return;
    }
    if (tab == u"import") {
        imp_ws->refresh(doc.get());
        return;
    }
    if (tab == u"mapsize") {
        size_ws->refresh(doc.get());
        return;
    }
    if (tab == u"shortcuts") {
        keys_ws->refresh(doc.get());
        return;
    }
    if (tab == u"submods") {
        sub_ws->refresh(doc.get());
        return;
    }
    if (tab == u"checksum") {
        sub_ws->refresh(doc.get());                  // (the mods it counts: as the Submods page finds them)
        ck_ws->refresh();
        return;
    }
    if (tab == u"econest") {
        econ_ws->refresh(doc.get());
        return;
    }
    if (tab == u"popinf") {
        popinf_ws->refresh(doc.get());
        return;
    }
    if (tab == u"errors") {
        stack->setCurrentWidget(chk_panel);
        chk_panel->refresh(doc.get());
        return;
    }
    if (tab == u"events") {
        stack->setCurrentWidget(event_panel);
        event_panel->refresh(doc.get());
        return;
    }
    stack->setCurrentWidget(edit_panel);             // (the Pops page: every tab not named above, as in Python)
    std::optional<long long> src;
    if (last_clicked && std::find(selection.begin(), selection.end(), *last_clicked) != selection.end())
        src = last_clicked;
    else if (selection.size() == 1)
        src = selection[0];
    std::vector<QString> st = selected_states();
    edit_panel->refresh(*doc, QList<long long>(selection.begin(), selection.end()), header3(),
                        QStringList(st.begin(), st.end()), src);
    pop_fix->refresh();                           // (pops this mod can't have: said at the top, with the ways out)
}

void MainWindow::on_filter_changed()
{
    edit_panel->refresh_filter_only(*doc, QList<long long>(selection.begin(), selection.end()));
    if (color_mode == u"filter")
        _render_timer->start(150);
}

// ---------------------------------------------------------------------------------------------- edits
namespace {
QString signed_commas(long long d) { return d >= 0 ? u"+"_s + widgets::fmt(d) : widgets::fmt(d); }   // "{:+,}"
}  // namespace

void MainWindow::do_scale(double pct)
{
    if (selection.empty())
        return;
    const document::PopFilter flt = edit_panel->current_filter();
    const long long delta = doc->scale(QList<long long>(selection.begin(), selection.end()), flt, pct);
    const QString g = py::fmt_g(pct);
    message(u"%1% → %2 people"_s.arg(g.startsWith(u'-') ? g : u"+"_s + g, signed_commas(delta)));
}

void MainWindow::do_set_total(long long total)
{
    if (selection.empty())
        return;
    const long long delta = doc->scale(QList<long long>(selection.begin(), selection.end()), edit_panel->current_filter(),
                                       std::nullopt, total);
    message(u"Set selected total to %1 (%2)"_s.arg(widgets::fmt(total), signed_commas(delta)));
}

void MainWindow::do_consolidate()
{
    const std::vector<QString> states = selected_states();
    const QSet<QString> types(edit_panel->elite_types.begin(), edit_panel->elite_types.end());
    if (states.empty() || types.isEmpty())
        return;
    long long moved = 0;
    for (const QString &rid : states)
        moved += doc->consolidate(rid, types);
    message(u"Moved %1 elite pops into %2 state capital(s)"_s.arg(widgets::fmt(moved)).arg(states.size()));
}

void MainWindow::start_capital_pick()
{
    const std::vector<QString> states = selected_states();
    if (states.size() != 1)
        return;
    mode = u"capital"_s;
    capital_region = states[0];
    update_highlight();
    message(u"Click a province of %1 to make it the state capital (Esc to cancel)"_s.arg(states[0]), 0);
}

QString MainWindow::filter_description() const
{
    const panels::EditPanel *ep = edit_panel;
    const auto keys = [](const widgets::ChipGroup *g) {
        QSet<QString> out;
        for (const auto &[k, _b] : g->boxes)
            out.insert(k);
        return out;
    };
    const auto sorted = [](const QSet<QString> &s, bool dash) {
        QStringList l;
        for (const QString &k : s)
            l << (dash && (k == panels::NONE_KEY || k.isEmpty()) ? u"—"_s : k);   // c or "—"
        std::sort(l.begin(), l.end());
        return l.join(u", "_s);
    };
    const QSet<QString> types = ep->types->checked(), cults = ep->cultures->checked();
    const QSet<QString> rels = ep->religions->checked(), all_r = keys(ep->religions);
    const QSet<QString> all_t = keys(ep->types), all_c = keys(ep->cultures);
    const QSet<QString> missing = all_t - types;
    const QString tdesc = types == all_t ? u"all types"_s
                          : missing.size() <= 3 ? u"all types except "_s + sorted(missing, false)
                                                : sorted(types, false);
    const QString cdesc = cults == all_c ? u"all cultures"_s : sorted(cults, true);
    const QString rdesc = rels == all_r ? u"all religions"_s : sorted(rels, true);
    return u"%1; %2; %3"_s.arg(tdesc, cdesc, rdesc);
}

void MainWindow::start_split()
{
    std::optional<long long> src;
    if (last_clicked && std::find(selection.begin(), selection.end(), *last_clicked) != selection.end())
        src = last_clicked;
    else if (selection.size() == 1)
        src = selection[0];
    if (!src || !*src)
        return;
    mode = u"split"_s;
    split_panel->start(*doc, *src, edit_panel->current_filter(), filter_description());
    stack->setCurrentWidget(split_panel);
    side_scroll->verticalScrollBar()->setValue(0);
    update_highlight();
    message(u"Split mode: click provinces to add/remove targets, Esc to cancel"_s, 0);
}

void MainWindow::end_split(bool silent)
{
    if (mode != u"split")
        return;
    mode = u"edit"_s;
    update_highlight();
    refresh_panel();
    if (!silent)
        message(u"Split cancelled"_s);
}

void MainWindow::apply_split()
{
    panels::SplitPanel *sp = split_panel;
    const long long lim = sp->ph_cb->isChecked() ? sp->ph_spin->value() : 0;
    const long long moved = doc->split(*sp->source, sp->weights(), *sp->flt, lim);
    const long long n = (long long)sp->target_pids().size();
    mode = u"edit"_s;
    set_selection({*sp->source});
    message(u"Moved %1 people from #%2 to %3 province(s). Ctrl+Z to undo."_s.arg(widgets::fmt(moved)).arg(*sp->source).arg(n));
}

// ---------------------------------------------------------------------------------------------- pop tools
void MainWindow::on_tool(const QString &key)
{
    if (selection.empty())
        return;
    const QList<long long> sel(selection.begin(), selection.end());
    const document::PopFilter flt = edit_panel->current_filter();
    const std::vector<moddata::Pop *> sel_pops = doc->selected_pops(sel, flt);
    if (key == u"copy") {
        std::vector<std::pair<document::PopKey, long long>> agg;
        std::map<document::PopKey, size_t> at;
        for (const moddata::Pop *p : sel_pops) {
            const document::PopKey k = document::pop_key(*p);
            auto it = at.find(k);
            if (it == at.end()) {
                at.emplace(k, agg.size());
                agg.emplace_back(k, p->size);
            } else {
                agg[it->second].second += p->size;
            }
        }
        if (agg.empty()) {
            message(u"Nothing to copy — no pops match the filter"_s);
            return;
        }
        std::vector<moddata::Pop> tmpl;
        long long total = 0;
        for (const auto &[k, s] : agg) {
            moddata::Pop p;
            p.type = std::get<0>(k);
            p.culture = std::get<1>(k);
            p.religion = std::get<2>(k);
            p.size = s;
            tmpl.push_back(p);
            total += s;
        }
        const QString where = selection.size() == 1 ? mod->provinces.get(selection[0])->name
                                                    : u"%1 provinces"_s.arg(selection.size());
        clipboard = std::make_pair(u"%1 pops, %2 people from %3 (%4)"_s.arg(QString::number(tmpl.size()), widgets::fmt(total),
                                                                          where, filter_description()),
                                   tmpl);
        edit_panel->can_paste = true;
        refresh_panel();
        message(u"Copied %1 pops (%2 people) from %3"_s.arg(QString::number(tmpl.size()), widgets::fmt(total), where));
    } else if (key == u"paste") {
        if (!clipboard)
            return;
        const auto &[desc, tmpl] = *clipboard;
        long long sum = 0;
        for (const moddata::Pop &t : tmpl)
            sum += t.size;
        dialogs::PasteDialog dlg(desc, sum, (long long)selection.size(), this);
        if (dlg.exec()) {
            const auto [replace, factor, total] = dlg.values();
            const long long n = doc->paste(sel, tmpl, replace, factor, total);
            message(u"Pasted %1 people into %2 province(s)"_s.arg(widgets::fmt(n)).arg(sel.size()));
        }
    } else if (key == u"add") {
        dialogs::AddPopDialog dlg(*mod, *doc, (long long)selection.size(), backup_root, this);
        if (dlg.exec()) {
            const auto [ptype, cult, rel, size] = dlg.values();
            doc->add_pop(sel, ptype.value_or(QString()), cult, rel, size);
            message(u"Added %1 %2 to %3 province(s)"_s.arg(widgets::fmt(size), ptype.value_or(u"None"_s)).arg(sel.size()));
        }
        after_culture_change();
    }
}

void MainWindow::do_mix(const QString &field, const OrderedMap<OptStr, long long> &targets)
{
    if (selection.empty())
        return;
    const long long n = doc->remix(QList<long long>(selection.begin(), selection.end()), edit_panel->current_filter(),
                                   field, targets);
    message(u"Changed the %1 mix: %2 people converted. Ctrl+Z to undo."_s.arg(field, widgets::fmt(n)));
    after_culture_change();
}

void MainWindow::do_pop_changes(const QList<long long> &pids, const document::PopFilter &flt,
                                std::optional<long long> total, const QString &field,
                                const std::vector<std::pair<OptStr, double>> &shares)
{
    // the Pops page's one Apply: the new total and the mix together, one undo step (C++ only)
    if (pids.isEmpty())
        return;
    const long long n = doc->pop_change(pids, flt, total, field, shares);
    if (total && shares.empty())                  // (the words of the steps it replaces)
        message(u"Set selected total to %1 (%2)"_s.arg(widgets::fmt(*total), signed_commas(n)));
    else if (!total)
        message(u"Changed the %1 mix: %2 people converted. Ctrl+Z to undo."_s.arg(field, widgets::fmt(n)));
    else
        message(u"Set selected total to %1 and changed the %2 mix. Ctrl+Z to undo."_s.arg(widgets::fmt(*total), field));
    if (!shares.empty())
        after_culture_change();
    else
        refresh_panel();
}

void MainWindow::_update_pend_bar()
{
    if (!pend_bar)
        return;
    const bool away = edit_panel->pending_live() && (tab != u"pops" || !edit_panel->pend_shown());
    if (away)
        pend_lbl->setText(edit_panel->pending_text() + u"  Apply them, discard them, or go back to them."_s);
    pend_bar->setVisible(away);
}

void MainWindow::_pend_go_back()
{
    if (!edit_panel->pend)
        return;
    const panels::Pending p = *edit_panel->pend;
    set_tab(u"pops"_s);
    edit_panel->types->unchecked = p.un_types;
    edit_panel->cultures->unchecked = p.un_cultures;
    edit_panel->religions->unchecked = p.un_religions;
    if (edit_panel->mix_field != p.field)
        edit_panel->set_mix_field(p.field);
    set_selection(std::vector<long long>(p.pids.begin(), p.pids.end()));
    refresh_panel();
    _update_pend_bar();
}

void MainWindow::open_culture_groups()
{
    // the cultures window on its second page: a group's own lines (union, leader pictures, unit graphics)
    open_cultures(1);
}

void MainWindow::open_cultures(int start)
{
    culturewin::CulturesWindow dlg(this, *mod, *doc, backup_root, start);
    cult_dlg = &dlg;
    cg_win = dlg.groups;
    dlg.exec();
    cg_win = nullptr;
    cult_dlg = nullptr;
    after_culture_change();
}

void MainWindow::open_religions()
{
    dialogs::ReligionManager dlg(*mod, *doc, backup_root, this);
    dlg.exec();
    after_culture_change();
}

void MainWindow::after_culture_change()
{
    // cultures.txt / religion.txt may have changed: refresh names/colours.
    panels::EditPanel *ep = edit_panel;
    ep->mix_add_religion->fill(dialogs::grouped_religions(*mod), ep->mix_add_religion->_lead(), std::nullopt);
    refresh_panel();
    if (color_mode == u"culture" || color_mode == u"religion")
        render_map();
}

// provinces.bmp pixels changed (paint / undo / redo): redraw and forget geometry caches.
void MainWindow::_pixels_changed(const std::array<long long, 4> &box, const std::set<long long> &ids)
{
    map->redraw_box(box);
    geo->reset();
    pos_panel->placer._cache.clear();
    pos_panel->_cache.clear();
    pos_panel->_skip.reset();
    map_panel->_river_cache.reset();
    map_panel->_status.reset();
    chk_panel->_sig.reset();
    schedule_checks();
    std::set<long long> sel(selection.begin(), selection.end());
    sel.insert(sea_selection.begin(), sea_selection.end());
    if (std::any_of(ids.begin(), ids.end(), [&sel](long long p) { return sel.contains(p); })) {
        std::vector<long long> all = selection;
        all.insert(all.end(), sea_selection.begin(), sea_selection.end());
        map->set_selection_layer(all, true);
    }
    std::vector<long long> caps;
    for (const auto &[rid, _r] : mod->regions)
        if (const auto cp = doc->capital(rid))
            caps.push_back(*cp);
    map->set_capitals(caps);
}

// Provinces were added / removed (new province, undo, redo): sea lists, search and selection follow.
void MainWindow::_defs_changed()
{
    QSet<long long> sea_ids;
    for (const auto &[p, pr] : mod->provinces)
        if (pr.sea && p < mapdata->n)
            sea_ids.insert(p);
    std::fill(geo->sea_lut.begin(), geo->sea_lut.end(), std::uint8_t(0));
    for (long long p : sea_ids)
        geo->sea_lut[size_t(p)] = 1;
    geo->reset();
    if (doc && doc->maps.contains(u"adjacency"_s))
        doc->map<adjacency::AdjacencyFile>(u"adjacency"_s).sea = sea_ids;
    map_panel->_status.reset();
    map_panel->_river_cache.reset();
    chk_panel->_sig.reset();
    std::vector<long long> keep, keep_sea;
    for (long long p : selection)
        if (mod->provinces.contains(p))
            keep.push_back(p);
    for (long long p : sea_selection)
        if (mod->provinces.contains(p))
            keep_sea.push_back(p);
    selection = keep;
    sea_selection = keep_sea;
    if (last_clicked && !mod->provinces.contains(*last_clicked))
        last_clicked.reset();
    _build_search_index();
}

// ---------------------------------------------------------------------------------------------- Provinces & states
void MainWindow::on_state_move(const QString &kind0)
{
    if (selection.empty())
        return;
    const OptStr cur = mod->provinces.get(selection[0])->region;
    QString kind = kind0;
    _state_del.reset();
    if (kind.startsWith(u"delete"_s)) {
        if (!cur || cur->isEmpty()) {
            message(u"⚠ This province is in no state — nothing to delete."_s, 8000);
            return;
        }
        _state_del = cur;
        if (mod->regions.get(*cur)->provinces.empty()) {
            _do_state_delete(std::nullopt);
            return;
        }
        kind = kind.mid(QStringLiteral("delete_").size());
    }
    if (kind == u"map") {
        mode = u"statepick"_s;
        message(_state_del ? u"Click any province of the state the provinces of %1 should join — %1 is then deleted "
                             u"(Esc to cancel)"_s.arg(*cur)
                           : u"Click any province of the state the selection should move to (Esc to cancel)"_s,
                0);
    } else if (kind == u"list") {
        QStringList rids;
        for (const auto &[rid, _r] : mod->regions)
            rids << rid;
        std::sort(rids.begin(), rids.end());
        std::vector<dialogs::PickItem> items;
        for (const QString &rid : rids) {
            const moddata::Region &r = *mod->regions.get(rid);
            if (r.provinces.empty() || (_state_del && rid == *_state_del))   // an empty state is gone
                continue;
            const auto cap = doc->capital(rid);
            const OptStr owner = cap && *cap ? doc->prov_state(*cap).owner : std::nullopt;
            QString oname = owner && !owner->isEmpty() ? *owner : u"no owner"_s;
            if (owner)
                if (const moddata::Country *c = mod->countries.get(*owner))
                    oname = c->name;
            const moddata::Province *cp = cap ? mod->provinces.get(*cap) : nullptr;
            const QString capname = cp ? cp->name : u"empty"_s;
            items.push_back({rid, u"%1 — %2 · %3 prov · %4/8 factories"_s.arg(rid, capname)
                                      .arg(r.provinces.size())
                                      .arg(doc->state_factories(rid).size()),
                             std::nullopt,
                             owner && !owner->isEmpty() ? u"%1 (%2)"_s.arg(oname, *owner) : u"No owner"_s});
        }
        std::stable_sort(items.begin(), items.end(), [](const dialogs::PickItem &a, const dialogs::PickItem &b) {
            return *a.group != *b.group ? *a.group < *b.group : a.key < b.key;
        });
        auto *pop = new dialogs::PickerPopup({}, items, _state_del ? std::nullopt : dialogs::Data(cur), this);
        connect(pop, &dialogs::PickerPopup::picked, this, [this](const dialogs::Data &rid) {
            if (_state_del && !_state_del->isEmpty())
                _do_state_delete(rid);
            else
                _do_state_move(rid.value_or(QString()));
        });
        const QPoint pos = QCursor::pos();
        pop->setGeometry(std::max(0, pos.x() - 420), pos.y(), 440, 520);
        pop->show();
        pop->search->setFocus();
    } else if (kind == u"new") {
        bool ok = false;
        QString key = QInputDialog::getText(this, u"New state"_s, u"Key for the new state (as in region.txt):"_s,
                                            QLineEdit::Normal, doc->new_state_key(cur), &ok);
        key = py::strip(key);
        if (!ok || key.isEmpty())
            return;
        if (mod->regions.contains(key)) {
            message(u"⚠ New state: %1 already exists — nothing moved."_s.arg(key), 10000);
            return;
        }
        _do_state_move(key, true);
    }
}

void MainWindow::on_make_capital()
{
    if (selection.size() != 1)
        return;
    const long long pid = selection[0];
    const OptStr rid = mod->provinces.get(pid)->region;
    if (!rid || rid->isEmpty() || doc->capital(*rid) == std::optional<long long>(pid))
        return;
    doc->set_capital(*rid, pid);
    const QString name = mod->provinces.get(pid)->name;
    const QStringList types = edit_panel->elite_types;
    const OptStr own = doc->prov_state(pid).owner;
    long long amount = 0;
    for (long long q : mod->regions.get(*rid)->provinces)
        if (q != pid && doc->prov_state(q).owner == own)
            for (const auto &p : doc->province_pops(q))
                if (types.contains(p.type))
                    amount += p.size;
    QString msg = u"%1 (#%2) is now the state capital of %3."_s.arg(name).arg(pid).arg(*rid);
    if (!types.isEmpty() && amount > 0 && prov_panel->cap_pops->isChecked()) {
        const long long moved = doc->consolidate(*rid, QSet<QString>(types.begin(), types.end()), pid, true);
        msg += u" Moved %1 elite pops there (only from provinces with the same owner)."_s.arg(widgets::fmt(moved));
    }
    message(msg + u" Ctrl+Z to undo."_s, 10000);
}

void MainWindow::_do_state_move(const QString &target, bool create)
{
    QString note;
    if (!create && mod->regions.contains(target)) {
        std::set<QString> there, moving;
        for (const auto &[o, _n] : provpanel::state_owners(*doc, target))
            if (o)
                there.insert(*o);
        for (long long p : selection)
            if (const OptStr &o = doc->prov_state(p).owner)
                moving.insert(*o);
        if (!there.empty() && !moving.empty() && !std::includes(there.begin(), there.end(), moving.begin(), moving.end())) {
            auto names = [this](const std::set<QString> &tags) {
                QStringList out;
                for (const QString &t : tags) {
                    const moddata::Country *c = mod->countries.get(t);
                    out << (c ? c->name : t);
                }
                return out.join(u", "_s);
            };
            note = u" ⚠ %1 belongs to %2, the moved province(s) to %3: in game a state with several owners is "
                   u"split per owner."_s.arg(target, names(there), names(moving));
        }
    }
    const auto [ok, msg] = doc->move_to_state(QList<long long>(selection.begin(), selection.end()), target, create);
    message((ok ? QString() : u"⚠ "_s) + msg + note + (ok ? u" Ctrl+Z to undo."_s : QString()), 12000);
}

void MainWindow::_do_state_delete(const OptStr &target)
{
    const OptStr rid = _state_del;
    _state_del.reset();
    if (!rid || rid->isEmpty())
        return;
    if (target == rid) {
        message(u"⚠ That is %1 itself — pick another state (nothing deleted)."_s.arg(*rid), 10000);
        return;
    }
    const auto [ok, msg] = doc->delete_state(*rid, target);
    message((ok ? QString() : u"⚠ "_s) + msg + (ok ? u" Ctrl+Z to undo."_s : QString()), 12000);
}

// region.txt membership changed (move / undo): rebuild borders, stars and search.
void MainWindow::_regions_changed()
{
    _region_version = doc->region_version;
    mapdata->rebuild_regions(*mod);
    std::vector<long long> caps;
    for (const auto &[rid, _r] : mod->regions)
        if (const auto c = doc->capital(rid))
            caps.push_back(*c);
    map->set_capitals(caps);
    _build_search_index();
    render_map();
}

void MainWindow::on_membership(const QString &kind, const QString &name)
{
    if (selection.empty() || name.isEmpty())
        return;
    doc->set_membership(kind, QList<long long>(selection.begin(), selection.end()), name);
    message(u"%1 → %2 (%3 province%4). Saved to map/%5.txt on Apply. Ctrl+Z to undo."_s.arg(py::capitalize(kind), name)
                .arg(selection.size())
                .arg(selection.size() == 1 ? QString() : u"s"_s, kind));
}

// Sizes of the separate parts of a province on the map, largest first (from the current pixels).
std::vector<std::int64_t> MainWindow::piece_sizes(long long pid) const
{
    return mapcheck::province_part_sizes(*mapdata, pid);
}

// A province / state name typed in Provinces (localisation; one undo step, written on Apply).
void MainWindow::on_name(const QString &key, const QString &text, const OptStr &section)
{
    if (!doc->maps.contains(u"loc"_s))
        return;
    auto &loc = doc->map<localisation::LocEdits>(u"loc"_s);
    const QString old = loc.text(key).value_or(QString()).isEmpty() ? key : *loc.text(key);
    _loc_tab[key] = rail_page().value_or(QString());   // (the pending numbers on the rail)
    doc->edit(u"Name %1: %2 → %3"_s.arg(key, old, text), {}, [&] { loc.set(key, text, section); }, {}, {},
              {u"loc"_s});
    _build_search_index();
    update_overlay();
    message(u"Renamed %1 → “%2”. Apply writes it to %3 (Ctrl+Z undoes it)."_s.arg(old, text, loc.where(key)), 10000);
}

// Where an edit of provinces was made, for the changelog and the undo list: the provinces by name, or — for many —
// how many, and in which states.
QString MainWindow::_where(const QList<long long> &pids0) const
{
    QList<long long> pids = pids0;
    std::sort(pids.begin(), pids.end());
    auto name = [this](long long p) {
        const moddata::Province *pr = mod->provinces.get(p);
        return pr ? u"%1 (#%2)"_s.arg(pr->name).arg(p) : u"#%1"_s.arg(p);
    };
    if (pids.size() <= 4) {
        QStringList out;
        for (long long p : pids)
            out << name(p);
        return out.join(u", "_s);
    }
    OrderedMap<QString, std::vector<long long>> states;
    for (long long p : pids) {
        const moddata::Province *pr = mod->provinces.get(p);
        const OptStr rid = pr ? pr->region : std::nullopt;
        const moddata::Region *r = rid && !rid->isEmpty() ? mod->regions.get(*rid) : nullptr;
        QString key = r ? r->name : QString();
        if (key.isEmpty())
            key = rid && !rid->isEmpty() ? *rid : u"no state"_s;
        states[key].push_back(p);
    }
    std::vector<std::pair<QString, std::vector<long long>>> kv(states.begin(), states.end());
    std::stable_sort(kv.begin(), kv.end(), [](const auto &a, const auto &b) {
        return a.second.size() != b.second.size() ? a.second.size() > b.second.size() : a.first < b.first;
    });
    QStringList parts;
    for (const auto &[s, v] : kv)
        parts << (v.size() > 1 ? u"%1 (%2)"_s.arg(s).arg(v.size())
                               : u"%1: %2"_s.arg(s, mod->provinces.get(v[0])->name));
    return u"%1 provinces — %2%3"_s.arg(pids.size()).arg(
        parts.mid(0, 6).join(u", "_s), parts.size() > 6 ? u" and %1 more states"_s.arg(parts.size() - 6) : QString());
}

void MainWindow::on_prov_edit_for(const QString &label0, const std::function<void(long long, history::ProvState &)> &fn,
                                  const QList<long long> &pids)
{
    if (pids.isEmpty())
        return;
    const QString label = u"%1: %2"_s.arg(label0, _where(pids));
    doc->edit_provinces(label, pids, fn);
    message(u"%1 (%2 province%3). Ctrl+Z to undo."_s.arg(label).arg(pids.size()).arg(pids.size() == 1 ? QString() : u"s"_s));
    _capital_follows(pids);
}

void MainWindow::on_prov_edit(const QString &label0, const std::function<void(long long, history::ProvState &)> &fn)
{
    if (selection.empty())
        return;
    const QList<long long> sel(selection.begin(), selection.end());
    const QString label = u"%1: %2"_s.arg(label0, _where(sel));
    doc->edit_provinces(label, sel, fn);
    message(u"%1 (%2 province%3). Ctrl+Z to undo."_s.arg(label).arg(sel.size()).arg(sel.size() == 1 ? QString() : u"s"_s));
    _capital_follows(sel);
}

// A nation that got provinces but whose capital isn't one of its own (a new nation: the template's) gets the first of
// them as its capital — a nation at the start must own its capital.
void MainWindow::_capital_follows(const QList<long long> &pids)
{
    if (!doc->maps.contains(u"nations"_s) || !mod->country_hist || !doc->maps.contains(u"new_nations"_s))
        return;
    auto &nations = doc->map<countryhist::NationEdits>(u"nations"_s);
    auto &fresh = doc->map<countryhist::NewNations>(u"new_nations"_s);
    std::set<QString> tags;
    for (long long p : pids)
        if (const OptStr &o = doc->prov_state(p).owner)
            tags.insert(*o);
    for (const QString &tag : tags) {
        if (!mod->country_hist->files.contains(tag) || !fresh.added.contains(tag))   // only nations made in the editor
            continue;
        const OptStr capv = nations.state(tag).at(u"capital"_s);
        std::optional<long long> cap;
        if (capv && !capv->isEmpty() && std::all_of(capv->begin(), capv->end(), [](QChar c) { return c.isDigit(); }))
            cap = capv->toLongLong();
        if (cap && mod->provinces.contains(*cap) && doc->prov_state(*cap).owner == OptStr(tag))
            continue;
        long long first = 0;
        for (long long p : pids)
            if (doc->prov_state(p).owner == OptStr(tag)) {
                first = p;
                break;
            }
        doc->edit(u"%1: capital → #%2"_s.arg(tag).arg(first), {}, [&] {
            nations.edit(tag, [first](countryhist::NationState &st) { st[u"capital"_s] = QString::number(first); });
        }, {}, {}, {u"nations"_s});
        message(u"%1 now owns them; its capital is #%2 %3 (it owned none of its old one) — change it in the "
                u"Nation page. Ctrl+Z undoes it."_s.arg(tag).arg(first).arg(mod->provinces.get(first)->name),
                12000);
    }
}

void MainWindow::_prov_switch_text()
{
    const bool util = prov_sub == u"utility";
    prov_switch->setText(util ? u"◂  Back to the provinces and states"_s : u"Utility states  ▸"_s);
    prov_switch->setToolTip(util ? QString()
                                 : u"States that only group provinces (the game doesn't treat them as states): see and "
                                   u"edit them"_s);
}

// Provinces & states: the provinces, or the utility states.
void MainWindow::set_prov_sub(const QString &key)
{
    if (key != u"provinces" && key != u"utility" && key != u"states")
        return;
    prov_sub = key;
    _prov_switch_text();
    settings->setValue(u"provSub"_s, key);
    for (auto &[k, b] : prov_sub_btns)
        b->setChecked(k == key);
    if (key != u"utility")
        util_edit = false;
    if (key != u"states" && state_panel->add_btn->isChecked())
        state_panel->add_btn->setChecked(false);
    _sync_rail();
    render_map();
    refresh_panel();
}

void MainWindow::_util_view(const QString &rid)
{
    util_view = rid.isEmpty() ? std::nullopt : OptStr(rid);
    if (rid.isEmpty())
        util_edit = false;
    render_map();
    refresh_panel();
    if (!rid.isEmpty())
        message(u"%1 in gold — ✎ in the panel adds / removes provinces by clicking the map."_s.arg(rid), 8000);
}

void MainWindow::_util_edit_toggled(bool on)
{
    util_edit = on && util_view.has_value();
    refresh_panel();
    if (util_edit)
        message(u"Click a province to put it into %1 or take it out; Shift+click: its whole state. Esc stops."_s.arg(
                    *util_view),
                0);
    else
        message(QString(), 1);
}

void MainWindow::_util_mark(const QStringList &rids0, bool on)
{
    auto &um = doc->map<document::UtilityMarks>(u"utility"_s);
    QStringList rids;
    for (const QString &r : rids0)
        if (mod->regions.contains(r) == on)
            rids << r;
    if (rids.isEmpty())
        return;
    const QString names = rids.mid(0, 3).join(u", "_s) + (rids.size() > 3 ? u" …"_s : QString());
    doc->edit(u"%1 %2"_s.arg(on ? u"Utility state:"_s : u"A state again:"_s, names), {}, [&] { um.set(rids, on); }, {},
              {}, {u"utility"_s});
    if (on && rids.size() == 1) {
        util_view = rids[0];
    } else if (!on && util_view && rids.contains(*util_view)) {
        util_view.reset();
        util_edit = false;
    }
    render_map();
    refresh_panel();
    const bool one = rids.size() == 1;
    message(u"%1 %2 — the state checks %3. Kept for this mod in the editor (Ctrl+Z undoes it)."_s.arg(
                names,
                on ? u"marked as utility state%1"_s.arg(one ? QString() : u"s"_s)
                   : u"count%1 as a state again"_s.arg(one ? u"s"_s : QString()),
                on ? u"ignore %1 now"_s.arg(one ? u"it"_s : u"them"_s) : u"see it again"_s),
            12000);
}

void MainWindow::_util_selected(bool add)
{
    if (!util_view)
        return;
    QList<long long> pids;
    for (long long p : selection)
        if (mod->provinces.contains(p))
            pids << p;
    const auto [ok, msg] = doc->utility_members(*util_view, pids, add);
    message((ok ? QString() : u"⚠ "_s) + msg, 8000);
    render_map();
}

// Provinces tab "Has islands": set / remove set_province_flag = island (one undo step, written on Apply).
void MainWindow::on_island(bool on, const QList<long long> &pids)
{
    if (!doc->maps.contains(u"islands"_s))
        return;
    auto &isl = doc->map<store::StoreOf<history::IslandFlags>>(u"islands"_s).obj;
    QList<long long> todo;
    for (long long p : pids)
        if (const moddata::Province *pr = mod->provinces.get(p); pr && !pr->sea && isl.marked.contains(p) != on)
            todo << p;
    if (todo.isEmpty())
        return;
    const QString who = todo.size() == 1 ? u"#%1 %2"_s.arg(todo[0]).arg(mod->provinces.get(todo[0])->name)
                                         : u"%1 provinces"_s.arg(todo.size());
    doc->edit(u"%1 island flag: %2"_s.arg(on ? u"Set"_s : u"Remove"_s, who), {}, [&] { isl.set(todo, on); }, {}, {},
              {u"islands"_s});
    message((on ? u"Marked %1 as having islands: the Errors tab no longer lists its separate parts. Apply writes "
                  u"set_province_flag = island into the history file. Ctrl+Z undoes it."_s
                : u"Removed the island flag of %1: the Errors tab checks its parts again. Ctrl+Z undoes it."_s)
                .arg(who),
            10000);
}

// ------------------------------------------------------------------ nations
// (provinces, states, people, [(culture, %)] top 3, capital name) of a nation at the start.
nationpanel::Stats MainWindow::nation_stats(const OptStr &tag)
{
    nationpanel::Stats s;
    if (!tag || tag->isEmpty() || !doc)
        return s;
    const std::vector<long long> owned = nationpanel::stats_owned(*doc, *mod, *tag);
    std::set<QString> states;
    for (long long p : owned)
        if (const OptStr &r = mod->provinces.get(p)->region; r && !r->isEmpty())
            states.insert(*r);
    OrderedMap<OptStr, long long> cul;
    for (long long p : owned)
        for (const moddata::Pop &pop : doc->province_pops(p))
            cul[pop.culture] += pop.size;
    long long tot = 0;
    for (const auto &[_k, v] : cul)
        tot += v;
    if (tot) {
        std::vector<std::pair<OptStr, long long>> kv(cul.begin(), cul.end());
        std::stable_sort(kv.begin(), kv.end(), [](const auto &a, const auto &b) { return -a.second < -b.second; });
        for (size_t i = 0; i < kv.size() && i < 3; ++i) {
            const auto &[k, v] = kv[i];
            const moddata::Culture *c = k ? mod->cultures.get(*k) : nullptr;
            s.cultures.emplace_back(c ? c->name : k.value_or(u"None"_s), py::round_int(double(v) * 100.0 / double(tot)));
        }
    }
    s.n_prov = static_cast<long long>(owned.size());
    s.n_states = static_cast<long long>(states.size());
    s.pops = tot;
    return s;
}

// Nation page: this nation — its provinces are selected on the map.
void MainWindow::show_nation(const OptStr &tag, bool zoom)
{
    const bool changed = tag != nation_tag;
    nation_tag = tag;
    const std::vector<long long> owned =
        tag && !tag->isEmpty() ? nationpanel::stats_owned(*doc, *mod, *tag) : std::vector<long long>{};
    if (tab == u"armies" || tab == u"diplomacy") {
        // their maps colour the nation themselves — the red selection would hide markers and colours
        if (changed && diplo_b == tag)
            diplo_b.reset();
        set_selection({}, true);
        if (changed && (color_mode == u"military" || color_mode == u"status" || color_mode == u"relvalues")) {
            render_map();
            _show_banner();                         // ("pick a nation" goes / comes back)
        }
    } else {
        set_selection(owned);
    }
    if (zoom && !owned.empty()) {
        std::optional<countryhist::NationState> st;
        if (mod->country_hist && mod->country_hist->files.contains(*tag))
            st = doc->map<countryhist::NationEdits>(u"nations"_s).state(*tag);
        const OptStr capv = st ? st->at(u"capital"_s) : std::nullopt;
        const long long cap = capv && !capv->isEmpty() && py::isdigit(*capv) ? py::to_int(*capv) : owned[0];
        if (0 < cap && cap < mapdata->n && mapdata->area[size_t(cap)])
            zoom_to_point(double(mapdata->cx[size_t(cap)]) + 0.5, double(mapdata->cy[size_t(cap)]) + 0.5, 2.0);
    }
    QScrollBar *vb = side_scroll->verticalScrollBar();
    if (vb->maximum() > 0 && vb->value() >= vb->maximum() - 30) {
        // at the bottom: the next nation's page is a little taller / shorter — stay at its bottom
        _stick_bottom = true;
        _stick_timer->start();
    }
    refresh_panel();
    if (_stick_bottom)
        vb->setValue(vb->maximum());
    if (changed)
        _refresh_windows();
}

// The shown nation's capital at the start (with unapplied edits), or None.
std::optional<long long> MainWindow::_nation_capital()
{
    if (!nation_tag || nation_tag->isEmpty() || !mod->country_hist || !doc || !doc->maps.contains(u"nations"_s) ||
        !mod->country_hist->files.contains(*nation_tag))
        return std::nullopt;
    const OptStr cap = doc->map<countryhist::NationEdits>(u"nations"_s).state(*nation_tag).at(u"capital"_s);
    return cap && !cap->isEmpty() && py::isdigit(*cap) ? std::optional<long long>(py::to_int(*cap)) : std::nullopt;
}

void MainWindow::start_nation_capital_pick()
{
    if (!nation_tag || nation_tag->isEmpty())
        return;
    mode = u"nationcap"_s;
    message(u"Click the new capital of %1 on the map — one of its provinces (Esc cancels)."_s.arg(*nation_tag), 0);
}

void MainWindow::on_nation_edit(const QString &label, const QString &tag,
                                const std::function<void(countryhist::NationState &)> &fn)
{
    auto &nations = doc->map<countryhist::NationEdits>(u"nations"_s);
    doc->edit(label, {}, [&] { nations.edit(tag, fn); }, {}, {}, {u"nations"_s});
    message(u"%1 — written on Apply (Ctrl+Z undoes it)."_s.arg(label), 8000);
}

void MainWindow::new_nation(const QString &tmpl_ref)
{
    const QString tmpl = tmpl_ref;                 // (a copy: the caller's tag changes with show_nation)
    auto &nn = doc->map<countryhist::NewNations>(u"new_nations"_s);
    nationpanel::NewNationDialog dlg(*mod, tmpl, nn, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const auto [t, name, adj, color] = dlg.result_data();
    doc->edit(u"New nation %1 %2"_s.arg(t, name), {}, [&] { nn.add(t, name, adj, color, tmpl); }, {}, {},
              {u"new_nations"_s});
    _build_search_index();
    show_nation(t);
    message(u"New nation %1 (%2) from %3 — made on Apply. Give it provinces in Provinces (owner), then pick its "
            u"capital here."_s.arg(name, t, tmpl),
            12000);
}

// A panel changed one script file's text (unit stats, reforms …): one undoable step, written on Apply.
void MainWindow::on_script_text(const QString &label, const QString &rel, const QString &text)
{
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    QStringList drop = store.drop_names;
    store.drop_names.clear();
    localisation::LocEdits *loc = doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    {
        QStringList keep;
        for (const QString &k : drop)
            if (loc && loc->fresh.contains(k))
                keep << k;
        drop = keep;
    }
    if (drop.isEmpty() && !store.gone.contains(rel) && scriptstore::canon(text) == scriptstore::canon(store.text(rel))) {
        message(u"Nothing changed (%1 says the same as before)."_s.arg(rel), 6000);
        return;                                    // only the spacing / a number's spelling differs: no edit
    }
    doc->edit(label, {}, [&] {
        store.set_text(rel, text);
        for (const QString &k : drop)              // the names of something removed go with it
            loc->drop(k);
    }, {}, {}, !drop.isEmpty() ? QStringList{u"scripts"_s, u"loc"_s} : QStringList{u"scripts"_s});
    message(u"%1 — written to %2 on Apply (Ctrl+Z undoes it)."_s.arg(label, rel), 8000);
}

void MainWindow::on_script_remove(const QString &label, const QString &rel)
{
    // a file of the mod goes (a war at the start, a pop type …): one undoable step; Apply deletes it after a backup
    if (!doc->maps.contains(u"scripts"_s))
        return;
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    const QStringList drop = store.drop_names;
    store.drop_names.clear();
    localisation::LocEdits *loc = doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    doc->edit(label, {}, [&] {
        entryview::drop_file(*mod, store, rel);
        for (const QString &k : drop)
            if (loc)
                loc->drop(k);
    }, {}, {}, QStringList{u"scripts"_s, u"loc"_s});
    message(u"%1 — %2 goes on Apply (Ctrl+Z undoes it)."_s.arg(label, rel), 9000);
}

// The information line "#literacy event" above an event / decision, put there or taken away.
void MainWindow::on_event_literacy(const QString &rel, const QString &uid, bool on)
{
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    const QString old = store.text(rel);
    const QString neu = events::set_lit_line(old, py::split(rel, u"/"_s).first(), uid, on);
    if (neu == old)
        return;
    doc->edit(u"%1: %2 the line %3"_s.arg(py::split(rel, u"/"_s).last(), on ? u"＋"_s : u"−"_s, events::LIT_LINE), {},
              [&] { store.set_text(rel, neu); }, {}, {}, {u"scripts"_s});
    message(u"%1 — the map mode “Literacy at the start” %2. A comment in the file, written on Apply "
            u"(Ctrl+Z undoes it)."_s.arg(on ? u"Assigned as a literacy event"_s : u"No longer a literacy event"_s,
                                         on ? u"counts it in"_s : u"leaves it out"_s), 10000);
}

void MainWindow::on_event_cats(const std::vector<std::pair<std::pair<QString, OptStr>, QStringList>> &changes,
                               const QString &label)
{
    auto &cats = doc->map<events::EventCats>(u"event_cats"_s);
    doc->edit(label, {}, [&] {
        for (const auto &[key, names] : changes)
            cats.set(key, names);
    }, {}, {}, {u"scripts"_s});                      // the listeners refresh the panel and the map
    message(u"%1 — written into the files as comments on Apply (Ctrl+Z undoes it)."_s.arg(label), 8000);
}

// Events tab: a province link — the map goes there and rings it.
void MainWindow::on_event_province(long long pid)
{
    const mapview::MapData &d = *mapdata;
    if (!(0 < pid && pid < d.n) || d.area[size_t(pid)] == 0) {
        message(u"Province %1 isn't on the map."_s.arg(pid), 6000);
        return;
    }
    if (tab == u"events" && !script_ws->map_on())
        script_ws->show_map();                     // the map opens to show it
    map->center_on_province(pid, 3.0);
    map->_zoomed();
    _jump_mark = JumpMark{QPointF(double(d.cx[size_t(pid)]), double(d.cy[size_t(pid)])), 14.0, Rgb{255, 230, 70}};
    map->shape_layer->set_shapes(_jump_shapes());  // the tab has no markers of its own: just the ring
    map->shape_layer->setVisible(true);
    const moddata::Province *pr = mod->provinces.get(pid);
    message(u"%1 (#%2)"_s.arg(pr ? pr->name : QString::number(pid), QString::number(pid)), 6000);
}

void MainWindow::on_script_replace(const OrderedMap<QString, QString> &changes, const QString &label)
{
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    doc->edit(label, {}, [&] {
        for (const auto &[rel, text] : changes)
            store.set_text(rel, text);
    }, {}, {}, {u"scripts"_s});
    message(u"%1 — Ctrl+Z undoes it, Apply writes it."_s.arg(label), 10000);
}

void MainWindow::_events_changed()
{
    if (tab != u"events")
        return;
    update_pending();
    if (!map->isVisible())
        return;
    const std::pair<QString, QString> key{color_mode, event_panel->fill_key()};
    if (key != _events_fill_key || color_mode != u"events") {
        _events_fill_key = key;
        render_map();
    }
    _events_focus();
}

// The map marks what the editor's cursor is on: a nation (outline), provinces (rim + ◆), a culture (dots), a
// religion (lines), a state (dashes).
void MainWindow::_events_focus()
{
    const auto [layers, pins] = event_panel->focus_spec();
    map->set_focus_layer(layers);
    const mapview::MapData &d = *mapdata;
    const std::set<long long> pinset(pins.begin(), pins.end());
    auto ok = [&d](long long p) { return 0 < p && p < d.n && d.area[size_t(p)] > 0; };
    std::vector<mapview::Shape> shapes;
    // every province the event / the selection names: a cyan pin (screen size — seen even when tiny)
    const auto &sp = event_panel->set_pins;
    for (size_t i = 0; i < sp.size() && i < 1500; ++i) {
        const long long p = sp[i];
        if (ok(p) && !pinset.contains(p)) {
            mapview::Shape s;
            s.kind = u"diamond"_s;
            s.pts = {QPointF(double(d.cx[size_t(p)]), double(d.cy[size_t(p)]))};
            s.color = {70, 220, 255};
            s.size = 7.0;
            shapes.push_back(std::move(s));
        }
    }
    // the province at the cursor: a big one with a white halo
    for (long long p : pins)
        if (ok(p)) {
            mapview::Shape s;
            s.kind = u"diamond"_s;
            s.pts = {QPointF(double(d.cx[size_t(p)]), double(d.cy[size_t(p)]))};
            s.color = {90, 225, 255};
            s.size = 12.0;
            s.halo = true;
            shapes.push_back(std::move(s));
        }
    for (auto &s : _jump_shapes())
        shapes.push_back(std::move(s));
    map->shape_layer->set_shapes(shapes);
    map->show_box(map->focus_box());
    script_ws->legend->setText(
        u"<b>Fill</b> — %1<br><b>Marks</b> — cyan hatching: every province the event at the cursor (or the "
        u"selected text) names · the word at the cursor: <b>nation</b> = thick bright outline · <b>province</b> = "
        u"cyan rim + ◆ · <b>culture</b> = green rim inside its provinces, diagonal hatching (denser = more of the "
        u"people) · <b>religion</b> = the same in yellow, level hatching · <b>trade goods</b> = orange rims · <b>state</b> = dashed outline · <b>country flag</b> = who holds / can get it, outlined — "
        u"<span style='color:#ff46be'><b>pink outline</b></span> = the word at the cursor / the nation "
        u"right-clicked · <b>white outline</b> = the nation clicked for its own numbers"_s
            .arg(event_panel->legend->text()));
}

void MainWindow::_events_map_toggled(bool on)
{
    if (tab != u"events")
        return;
    _place_map();
    if (on && script_ws->map_mode == u"dock") {
        const QList<int> sz = left_split->sizes();
        if (sz.size() == 2) {
            const int tot = sz[0] + sz[1];
            left_split->setSizes({int(tot * 0.58), tot - int(tot * 0.58)});
        }
    }
    if (on) {                                      // (small window or docked: drawn for what is at the cursor)
        _events_fill_key.reset();
        render_map();
        _events_focus();
    }
}

// The Events tab's map: the selected event's chance within 10 years per nation (per province, for a province event)
// — striped where the start can't tell; a decision: who can take it; the chosen nation lit.
mapview::ColorTable MainWindow::_event_colors(std::vector<Rgb> &t, const std::vector<long long> &land)
{
    const long long n = mapdata->n;
    std::vector<std::pair<long long, OptStr>> owner;
    for (long long pid : land)
        owner.emplace_back(pid, doc->prov_state(pid).owner);
    const auto mv = event_panel->map_values();
    for (long long pid : land)
        t[size_t(pid)] = Rgb{44, 46, 52};
    auto value = [&mv](long long pid, const OptStr &o) -> const std::pair<Rgb, bool> * {
        if (mv->kind == u"c")
            return o ? mv->out.get(*o) : nullptr;
        return mv->out.get(QString::number(pid));
    };
    if (!mv) {                                     // nothing picked: the nations, dimmed
        for (const auto &[pid, o] : owner)
            if (o && !o->isEmpty()) {
                const moddata::Country *c = mod->countries.get(*o);
                const Rgb col = c ? c->color : Rgb{90, 90, 90};
                t[size_t(pid)] = Rgb{int(std::get<0>(col) * 0.45 + 60 * 0.55), int(std::get<1>(col) * 0.45 + 60 * 0.55),
                                     int(std::get<2>(col) * 0.45 + 60 * 0.55)};
            }
    } else {
        for (const auto &[pid, o] : owner)
            if (const auto *v = value(pid, o))
                t[size_t(pid)] = v->first;
    }
    mapview::ColorTable ct;
    ct.Q = MIX_Q;
    ct.rgb.resize(size_t(n) * MIX_Q * 3);
    auto put = [&ct](long long pid, int q, const Rgb &c) {
        const size_t o = (size_t(pid) * MIX_Q + size_t(q)) * 3;
        ct.rgb[o] = std::uint8_t(std::get<0>(c));
        ct.rgb[o + 1] = std::uint8_t(std::get<1>(c));
        ct.rgb[o + 2] = std::uint8_t(std::get<2>(c));
    };
    for (long long pid = 0; pid < n; ++pid)
        for (int q = 0; q < MIX_Q; ++q)
            put(pid, q, t[size_t(pid)]);
    if (mv)
        for (const auto &[pid, o] : owner) {
            const auto *v = value(pid, o);
            if (v && !v->second)                   // can't be told for sure: striped with the dark
                for (int q = 0; q < MIX_Q; ++q)
                    if ((q / 4) % 2)
                        put(pid, q, Rgb{58, 60, 66});
        }
    // (the nation clicked for its own numbers: a white outline — the map's marks, not a lighter fill)
    for (long long pid = 0; pid < n; ++pid)
        if (mapdata->sea[size_t(pid)])
            for (int q = 0; q < MIX_Q; ++q)
                put(pid, q, mapview::SEA_RGB);
    if (n)
        for (int q = 0; q < MIX_Q; ++q)
            put(0, q, NO_PROVINCE_RGB);
    return ct;
}

// The index of what the scripts refer to (the scripts are read first if they weren't yet).
refs::RefIndex *MainWindow::_script_refs()
{
    if (!event_panel->db || !event_panel->refs)
        event_panel->refresh(doc.get());
    return event_panel->refs.get();
}

// A line of a rebel type / a pop type shown on the page it is edited on. -> true when it is one.
bool MainWindow::_own_script_page(const QString &rel, long long pos)
{
    const QString r = py::lower(rel);
    if (r.endsWith(u"common/rebel_types.txt"_s)) {
        go_page(u"rules"_s);
        rules_ws->set_page(u"rebels"_s);
        static_cast<scriptpage::ScriptListPage *>(*rules_ws->views.get(u"rebels"_s))->select(rel, std::nullopt, pos);
        message(u"Game rules → Rebels: the line is where the cursor stands."_s, 8000);
        return true;
    }
    if (r.startsWith(u"poptypes/"_s)) {
        go_page(u"rules"_s);
        rules_ws->set_page(u"poptypes"_s);
        static_cast<scriptpage::ScriptListPage *>(*rules_ws->views.get(u"poptypes"_s))->select(rel, std::nullopt, pos);
        message(u"Game rules → Pop types: the line is where the cursor stands."_s, 8000);
        return true;
    }
    return false;
}

QWidget *MainWindow::_script_page(const scriptpage::Spec &spec)
{
    auto *v = new scriptpage::ScriptListPage(*mod, spec, event_panel);
    connect(v, &scriptpage::ScriptListPage::outside, this, [this](const QString &rel, long long pos) {
        _script_jump(rel, pos);
    });
    v->set_problems(_script_problems);
    v->ws->set_problems(_script_problems);       // (the markers in its own editor)
    script_pages.push_back(v);
    return v;
}

// A script file shown on the page it is edited on (its part of that page picked).
void MainWindow::open_script_place(const QString &rel, long long pos)
{
    const QString r = py::lower(rel);
    if (_own_script_page(rel, pos))
        return;
    if (r.endsWith(u"common/goods.txt"_s)) {
        go_page(u"production"_s);
        return;
    }
    if (r.endsWith(u"common/religion.txt"_s)) {
        go_page(u"pops"_s);
        open_religions();
        return;
    }
    const auto [page, part] = script_place(rel);
    if (page == u"events") {
        _script_jump(rel, pos);
        return;
    }
    go_page(page);
    if (part) {
        if (page == u"warfare")
            war_ws->set_page(*part);
        else if (page == u"rules")
            rules_ws->set_page(*part);
        else if (page == u"politics")
            pol_ws->set_page(*part);
    }
}

void MainWindow::_script_jump(const QString &rel, long long pos)
{
    go_page(u"events"_s);
    script_ws->open(rel, pos);
}

OptStr MainWindow::_event_name(const QString &eid)
{
    events::EventDB *db = event_panel->db.get();
    events::Entry *const *e = db ? db->by_id.get(eid) : nullptr;
    return e && *e ? OptStr(db->name(**e)) : std::nullopt;
}

void MainWindow::_script_tool(const QString &key)
{
    if (key == u"network") {
        open_network();
        return;
    }
    if (key == u"texts") {
        open_texts();
        return;
    }
    if (key == u"nodes") {
        open_nodes();
        return;
    }
    if (key == u"ids") {
        open_reid();
        return;
    }
    open_blocks(key == u"new_event" ? OptStr(u"event"_s) : key == u"new_decision" ? OptStr(u"decision"_s) : std::nullopt);
}

// The block builder: for the entry picked in Scripts, an entry given, or a new "event" / "decision".
void MainWindow::open_blocks(const OptStr &what, events::Entry *entry)
{
    refs::RefIndex *refs = _script_refs();
    eventpanel::EventPanel *ep = event_panel;
    if (!block_win) {
        block_win = new blockbuilder::BlockBuilder(*mod, this, [this, ep] {
            return ep->_picture_names(block_win->kind != OptStr(u"decision"_s));
        });
        blockbuilder::BlockBuilder *w = block_win;
        connect(w, &blockbuilder::BlockBuilder::scriptEdit, this,
                [this](const QString &lb, const QString &rel, const QString &text) { on_script_text(lb, rel, text); });
        connect(w, &blockbuilder::BlockBuilder::nameEdit, this,
                [this](const QString &key, const QString &text) { on_name(key, text, std::nullopt); });
        connect(w, &blockbuilder::BlockBuilder::openInEditor, this,
                [this](const QString &rel, int pos) { _script_jump(rel, pos); });
        connect(w, &blockbuilder::BlockBuilder::undoRequested, this, [this] { undo(); });
        connect(w, &blockbuilder::BlockBuilder::redoRequested, this, [this] { redo(); });
    }
    blockbuilder::BlockBuilder *w = block_win;
    w->set_doc(doc.get(), refs, ep->db.get());
    events::Entry *e = entry ? entry : ep->sel;
    w->rel_hint = e ? e->rel : ep->sel_file;
    if (what) {
        w->show_new(*what);
    } else if (e) {
        w->open_entry(e->rel.value_or(u"None"_s), e->is_event() ? u"event"_s : u"decision"_s,
                      e->is_event() ? e->id.value_or(u"None"_s) : e->key);
    } else if (!w->root) {
        w->_show_empty(u"Pick an event or a decision in the list of the Scripts page and press “🧩 Blocks” — or make a "
                       "new one with the buttons above."_s);
    }
    _show_window(w, 1760, 1020);
}

// The block builder, when it is the window the user works in.
QWidget *MainWindow::_tool_window() const
{
    blockbuilder::BlockBuilder *w = block_win;
    return w && w->isVisible() && w->isActiveWindow() ? w : nullptr;
}

static double py_monotonic()                     // time.monotonic()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The user is building in the block builder right now (a mouse button is down there, or the last change is under two
// seconds old): heavy work of the main window waits.
bool MainWindow::_tool_busy() const
{
    if (!_tool_window())
        return false;
    return QApplication::mouseButtons() != Qt::NoButton || py_monotonic() - _tool_edit < 2.0;
}

void MainWindow::_lazy_refresh()
{
    if (_tool_busy()) {
        _lazy_panel->start(500);
        return;
    }
    if (mode == u"edit") {
        refresh_panel();
    } else if (tab == u"events" && event_panel->db) {
        event_panel->reparse_now();
        event_panel->cats_changed();
    }
}

// A script text and the texts of its new keys (an event made from a template, a follow-up): one undo step, written on
// Apply. A key that has a text already keeps it.
void MainWindow::on_script_with_names(const QString &label, const QString &rel, const QString &text,
                                      const OrderedMap<QString, QString> &names)
{
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    localisation::LocEdits *loc = doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    doc->edit(label, {}, [&] {
        store.set_text(rel, text);
        if (loc)
            for (const auto &[k, t] : names) {
                const OptStr have = loc->text(k);
                if (!have || have->isEmpty())
                    loc->set(k, t, std::nullopt);
            }
    }, {}, {}, {u"scripts"_s, u"loc"_s});
    message(u"%1 — written to %2 on Apply (Ctrl+Z undoes it)."_s.arg(label, rel), 9000);
}

// An event shown by its id: in the Scripts list and in the node window (the files read anew first).
void MainWindow::open_event_id(const QString &eid)
{
    eventpanel::EventPanel *ep = event_panel;
    try {
        ep->reparse_now();
    } catch (...) {
    }
    events::Entry *e = nullptr;
    if (ep->db)
        for (const auto &x : ep->db->entries)
            if (x->is_event() && x->id.value_or(u"None"_s) == eid) {
                e = x.get();
                break;
            }
    if (!e) {
        message(u"Event %1 is not in the scripts (yet)."_s.arg(eid), 6000);
        return;
    }
    ep->select(e);
    if (node_win && node_win->isVisible())
        node_win->refresh(doc.get(), e, ep->refs.get(), ep->db.get());
}

// The event / decision picked in Scripts as a graph of nodes.
void MainWindow::open_nodes()
{
    eventpanel::EventPanel *ep = event_panel;
    if (!node_win) {
        node_win = new nodegraph::NodeWindow(*mod, this);
        connect(node_win, &nodegraph::NodeWindow::scriptEdit, this,
                [this](const QString &lb, const QString &rel, const QString &text) { on_script_text(lb, rel, text); });
        connect(node_win, &nodegraph::NodeWindow::scriptNamesEdit, this,
                [this](const QString &lb, const QString &rel, const QString &text,
                       const eventtemplates::Names &names) { on_script_with_names(lb, rel, text, names); });
        connect(node_win, &nodegraph::NodeWindow::entryWanted, this, [this](const QString &eid) { open_event_id(eid); });
        connect(node_win, &nodegraph::NodeWindow::undoRequested, this, [this] { undo(); });
        connect(node_win, &nodegraph::NodeWindow::redoRequested, this, [this] { redo(); });
        connect(ep, &eventpanel::EventPanel::changed, this, [this, ep] {
            if (node_win->isVisible())
                node_win->refresh(doc.get(), ep->sel, ep->refs.get(), ep->db.get());
        });
    }
    node_win->refresh(doc.get(), ep->sel, _script_refs(), ep->db.get());
    _show_window(node_win, 1760, 1020);
}

// An event's / decision's name, with the names typed in this session.
QString MainWindow::_entry_name(const events::Entry &e)
{
    localisation::LocEdits *loc = doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    const OptStr t = loc && !e.title.isEmpty() ? loc->text(e.title) : std::nullopt;
    return t && !t->isEmpty() ? *t : event_panel->db->name(e);
}

void MainWindow::open_network()
{
    refs::RefIndex *refs = _script_refs();
    if (!net_win) {
        net_win = new eventnet::EventNet(*mod, this);
        connect(net_win, &eventnet::EventNet::openEntry, this,
                [this](const QString &rel, long long pos) { _script_jump(rel, pos); });
        connect(net_win, &eventnet::EventNet::blocksRequested, this,
                [this](events::Entry *e) { open_blocks(std::nullopt, e); });
    }
    const bool first = !net_win->property("_shown_once").toBool();
    net_win->set_data(event_panel->db.get(), refs, [this](const events::Entry &e) { return _entry_name(e); });
    _show_window(net_win, 1760, 1020);
    if (first) {                                   // (the picture fills the window once it has its size)
        eventnet::NetView *v = net_win->view;
        QTimer::singleShot(80, v, [v] { v->fit(); });
    }
}

// ---------------------------------------------------------------------------------------------- the palette (Ctrl+K)
void MainWindow::open_palette()
{
    if (!cmd_palette)
        cmd_palette = new palette::CommandPalette(this, settings.get(), [this] { return _palette_commands(); });
    cmd_palette->open();
}

// Every function the palette offers: (name, where it is, what to do -> the widget to frame).
std::vector<palette::Command> MainWindow::_palette_commands()
{
    std::vector<palette::Command> out;
    auto page = [this](const QString &key) {
        return [this, key]() -> QWidget * {
            go_page(key);
            return rail->items.value(key, nullptr);
        };
    };
    for (const auto &group : ::rail::RAIL)
        for (const auto &p : group.pages)
            out.push_back({p.name, u"%1 — %2"_s.arg(group.name, p.tip), page(p.key)});

    for (const auto &[rail_key, title, ws] : {std::tuple{u"warfare"_s, u"Warfare"_s, war_ws},
                                              std::tuple{u"rules"_s, u"Game rules"_s, rules_ws},
                                              std::tuple{u"library"_s, u"Library"_s, lib_ws}}) {
        // (Politics has no list of pages of its own in Python: getattr(ws, "pages", ()) finds none)
        for (const auto &pd : ws->pages) {
            const QString text = QString(pd.text).replace(u"&&"_s, u"&"_s);
            const QString k = pd.key;
            entryview::PagesWorkspace *w = ws;
            const QString rk = rail_key;
            out.push_back({text, u"%1 → %2"_s.arg(title, text), [this, rk, w, k]() -> QWidget * {
                               go_page(rk);
                               w->set_page(k);
                               QWidget *b = w->seg.value(k, nullptr);
                               return b ? b : w;
                           }});
        }
    }
    eventpanel::EventPanel *ep = event_panel;
    for (const auto &[k, name] : {std::pair{u"event"_s, u"Events"_s}, {u"decision"_s, u"Decisions"_s},
                                  {u"both"_s, u"Events and decisions"_s}, {u"mods"_s, u"Modifiers"_s},
                                  {u"cbs"_s, u"War goals"_s}})
        if (ep->kind_btn.contains(k)) {
            const QString kk = k;
            out.push_back({name, u"Scripts → the list of %1"_s.arg(py::lower(name)), [this, ep, kk]() -> QWidget * {
                               go_page(u"events"_s);
                               ep->kind_btn[kk]->click();
                               return ep->kind_btn[kk];
                           }});
        }
    for (const auto &[k, name, where] :
         {std::tuple{u"new_event"_s, u"New event"_s, u"Scripts → ＋ New event (the block builder)"_s},
          std::tuple{u"new_decision"_s, u"New decision"_s, u"Scripts → ＋ New decision (the block builder)"_s},
          std::tuple{u"blocks"_s, u"Blocks"_s, u"Scripts → 🧩 the event / decision picked, built from blocks"_s},
          std::tuple{u"nodes"_s, u"Nodes"_s, u"Scripts → ⧉ the event / decision picked as a graph of nodes and wires"_s},
          std::tuple{u"texts"_s, u"Texts & preview"_s,
                     u"Scripts → ✎ the texts of the event / decision picked, as in the game"_s},
          std::tuple{u"network"_s, u"Network"_s, u"Scripts → 🕸 what calls what"_s}}) {
        const QString kk = k;
        out.push_back({name, where, [this, kk]() -> QWidget * {
                           go_page(u"events"_s);
                           _script_tool(kk);
                           return nullptr;
                       }});
    }
    out.push_back({u"Flag studio"_s, u"Nation → the nation's flags, drawn from shapes"_s, [this]() -> QWidget * {
                       go_page(u"nations"_s);
                       open_flag_studio();
                       return nullptr;
                   }});
    out.push_back({u"Parties window"_s, u"Nation → every party on a timeline"_s, [this]() -> QWidget * {
                       go_page(u"nations"_s);
                       open_parties_window();
                       return nullptr;
                   }});
    out.push_back({u"Graphical culture"_s, u"Nation → look of its units and towns"_s, [this]() -> QWidget * {
                       go_page(u"nations"_s);
                       return nations_panel->parties_box->gc_box;
                   }});
    out.push_back({u"Cultures"_s, u"Population → the cultures and their groups"_s, [this]() -> QWidget * {
                       open_cultures();
                       return nullptr;
                   }});
    out.push_back({u"Culture groups"_s, u"Population → a group's union nation, leader pictures, unit graphics"_s,
                   [this]() -> QWidget * {
                       open_culture_groups();
                       return nullptr;
                   }});
    out.push_back({u"Religions"_s, u"Population → the religions"_s, [this]() -> QWidget * {
                       open_religions();
                       return nullptr;
                   }});
    out.push_back({u"Make the 4K submod"_s, u"Tools → Submods & 4K"_s, [this]() -> QWidget * {
                       go_page(u"submods"_s);
                       return sub_ws->make_btn;
                   }});
    out.push_back({u"Apply"_s, u"write every pending edit into the mod (Ctrl+S)"_s, [this]() -> QWidget * {
                       save();
                       return apply_btn;
                   }});
    out.push_back({u"Undo"_s, u"take the last edit back (Ctrl+Z)"_s, [this]() -> QWidget * {
                       undo();
                       return nullptr;
                   }});
    out.push_back({u"Redo"_s, u"do the undone edit again (Ctrl+Y)"_s, [this]() -> QWidget * {
                       redo();
                       return nullptr;
                   }});
    out.push_back({u"Revert all"_s, u"throw every pending edit away (asks once more)"_s, [this]() -> QWidget * {
                       revert();
                       return revert_btn;
                   }});
    out.push_back({u"Pending edits by file"_s, u"apply or revert one file at a time"_s, [this]() -> QWidget * {
                       open_pending_panel();
                       return nullptr;
                   }});
    for (const auto &[k, name] :
         {std::pair{u"none"_s, u"Terrain / rivers view: no borders (the pure picture)"_s},
          std::pair{u"prov"_s, u"Terrain / rivers view: province borders on / off"_s},
          std::pair{u"state"_s, u"Terrain / rivers view: state borders on / off"_s}}) {
        const QString kk = k;
        out.push_back({name, u"Drawing → Borders (lines between the pixels, never a tint)"_s, [this, kk]() -> QWidget * {
                           draw_panel->set_borders(kk);
                           return draw_panel->border_row;
                       }});
    }
    out.push_back({u"Rescan mod files"_s,
                   u"read the mod's files again (a file copied in by hand …) — your unapplied edits are kept and put "
                   u"back on top"_s,
                   [this]() -> QWidget * {
                       rescan_mod();
                       return nullptr;
                   }});
    for (auto &c : _palette_more())
        out.push_back(std::move(c));
    out.push_back({u"Switch mod"_s, u"open another mod or start date"_s, [this]() -> QWidget * {
                       if (confirm_discard(u"mod"_s)) {   // (Python: switch_mod)
                           switch_requested = true;
                           close();
                       }
                       return nullptr;
                   }});
    out.push_back({u"Search"_s, u"a province, state or nation by name (Ctrl+F)"_s, [this]() -> QWidget * {
                       search->setFocus();
                       return search;
                   }});
    out.push_back({u"Show the whole map"_s, u"zoom out to all of it"_s, [this]() -> QWidget * {
                       map->fit();
                       return nullptr;
                   }});
    if (color_box->isEnabled())
        for (int i = 0; i < color_box->count(); ++i)
            if (color_box->itemData(i).isValid())   // (not the group headings)
                out.push_back({u"Map mode: %1"_s.arg(color_box->itemText(i)), u"the map of this page"_s,
                               [this, i]() -> QWidget * {
                                   color_box->setCurrentIndex(i);
                                   Q_EMIT color_box->activated(i);
                                   return color_box;
                               }});
    QSet<QString> have;
    for (const auto &c : out)
        have.insert(py::lower(c.title));
    for (auto &c : _palette_buttons(have))
        out.push_back(std::move(c));
    return out;
}

// Ctrl+K entries for what the button scan doesn't find: step groups as ONE function each, the state / canal / core /
// building actions, every technology, invention, good and pop type, the loading screens, the flag studio, cultures.
// (All of it read from the mod AS IT IS NOW — imported or made, applied or still pending.)
std::vector<palette::Command> MainWindow::_palette_more()
{
    std::vector<palette::Command> out;
    scriptstore::ScriptStore *store =
        doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    localisation::LocEdits *lc = doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    auto loc = [this, lc](const QString &k) -> OptStr {
        if (lc)
            return lc->text(k);
        if (const QString *v = mod->loc.get(k))
            return *v;
        return std::nullopt;
    };
    auto truthy = [](const OptStr &s) { return s && !s->isEmpty(); };
    using Get = std::function<QWidget *()>;
    auto frame = [this](const QString &pg, Get get, const QString &need = QString(),
                        std::function<void()> sub = nullptr) {
        return [this, pg, get, need, sub]() -> QWidget * {
            go_page(pg);
            if (sub)
                sub();
            if (need == u"province" && selection.empty())
                message(u"Click a province first — then this is in the panel on the right."_s, 8000);
            if (need == u"nation" && !nation_tag)
                message(u"Click a nation's province first — then this is in the panel."_s, 8000);
            QWidget *w = get();
            QWidget *p = w ? w->parentWidget() : nullptr;
            while (p) {                                  // (brought into view where it scrolls)
                if (auto *sa = qobject_cast<QScrollArea *>(p))
                    sa->ensureWidgetVisible(w);
                p = p->parentWidget();
            }
            return w;
        };
    };
    panels::EditPanel *ep = edit_panel;
    provpanel::ProvincePanel *pp = prov_panel;
    // --- Population: the step groups as one function each, split, the filter
    out.push_back({u"Change province population"_s,
                   u"Population → the selected pops: −100 % … +100 % steps, or an exact total (type it and Enter)"_s,
                   frame(u"pops"_s, [ep] { return static_cast<QWidget *>(ep->step_label); }, u"province"_s)});
    out.push_back({u"Split pops of a province"_s,
                   u"Population → divide a province's filtered pops across other provinces by %"_s,
                   frame(u"pops"_s, [ep] { return static_cast<QWidget *>(ep->split_btn); }, u"province"_s)});
    out.push_back({u"Filter pops to edit"_s, u"Population → which pops the changes touch: by type, culture, religion"_s,
                   frame(u"pops"_s, [ep] { return static_cast<QWidget *>(ep->types); }, u"province"_s)});
    // --- Provinces: life rating, buildings, cores
    out.push_back({u"Change life rating"_s, u"Provinces & states → Land: −5 / −1 / +1 / +5"_s,
                   frame(u"provinces"_s, [pp] { return static_cast<QWidget *>(pp->lr_value); }, u"province"_s)});
    for (const auto &[key, name] : {std::pair{u"fort"_s, u"fort"_s}, {u"naval_base"_s, u"naval base"_s},
                                    {u"railroad"_s, u"railroad"_s}}) {
        const provpanel::ProvincePanel::LevelRow *row = nullptr;
        for (const auto &[k, r] : pp->level_rows)
            if (k == key)
                row = &r;
        if (row) {
            QPushButton *plus = row->plus, *minus = row->minus;
            out.push_back({u"Add %1"_s.arg(name), u"Provinces & states → Buildings: one level more"_s,
                           frame(u"provinces"_s, [plus] { return static_cast<QWidget *>(plus); }, u"province"_s)});
            out.push_back({u"Remove %1"_s.arg(name), u"Provinces & states → Buildings: one level less"_s,
                           frame(u"provinces"_s, [minus] { return static_cast<QWidget *>(minus); }, u"province"_s)});
        }
    }
    out.push_back({u"Remove a core"_s, u"Provinces & states → Ownership: ✕ on the nation in Cores"_s,
                   frame(u"provinces"_s, [pp] { return pp->cores_host; }, u"province"_s)});
    out.push_back({u"Add a core"_s, u"Provinces & states → Ownership: ＋ Add core…"_s,
                   frame(u"provinces"_s, [pp] { return static_cast<QWidget *>(pp->add_core); }, u"province"_s)});
    // --- States, utility states
    statepanel::StatePanel *sp = state_panel;
    auto st = [this] { set_prov_sub(u"states"_s); };
    out.push_back({u"Make state capital"_s,
                   u"Provinces & states → States: ★ Make capital (the province picked in the state's list)"_s,
                   frame(u"provinces"_s, [sp] { return static_cast<QWidget *>(sp->cap_btn); }, QString(), st)});
    out.push_back({u"Add provinces to a state"_s,
                   u"Provinces & states → States: ＋ Add provinces — click them on the map"_s,
                   frame(u"provinces"_s, [sp] { return static_cast<QWidget *>(sp->add_btn); }, QString(), st)});
    out.push_back({u"Delete state"_s, u"Provinces & states → States: its provinces join another state"_s,
                   frame(u"provinces"_s, [sp] { return static_cast<QWidget *>(sp->del_btn); }, QString(), st)});
    out.push_back({u"Utility states"_s,
                   u"Provinces & states → Utility states: entries of map/region.txt that are no real state — they "
                   u"only group provinces for events and decisions (like ANY_PROVINCE); a province's state is the "
                   u"first real state that lists it. Mark / unmark them, add or take out provinces"_s,
                   frame(u"provinces"_s, [this] { return static_cast<QWidget *>(util_panel); }, QString(),
                         [this] { set_prov_sub(u"utility"_s); })});
    // --- Adjacencies, canals
    canalpanel::CanalPanel *cp = canal_panel;
    out.push_back({u"Canals"_s, u"Adjacencies → ⚓ Canals: every canal, its model on the map, how it opens"_s,
                   frame(u"canal"_s, [cp] { return static_cast<QWidget *>(cp); })});
    out.push_back({u"Add new canal"_s, u"Adjacencies → Canals: ＋ New canal — click its province"_s,
                   frame(u"canal"_s, [cp] { return static_cast<QWidget *>(cp->new_btn); })});
    out.push_back({u"Delete canal"_s, u"Adjacencies → Canals: the canal picked in the list"_s,
                   frame(u"canal"_s, [cp] { return static_cast<QWidget *>(cp->del_btn); })});
    out.push_back({u"Remove adjacency"_s,
                   u"Adjacencies → Sea crossings & borders: remove the adjacency of the province picked"_s,
                   frame(u"adj"_s, [this] { return static_cast<QWidget *>(map_panel); }, u"province"_s)});
    // --- Nation, diplomacy
    out.push_back({u"Change nation colour"_s, u"Nation → its colour on the map (the colour wheel)"_s,
                   frame(u"nations"_s, [this]() -> QWidget * {
                       QWidget *b = nations_panel->findChild<colorwheel::ColorButton *>();
                       return b ? b : nations_panel;
                   }, u"nation"_s)});
    out.push_back({u"Change relations"_s,
                   u"Diplomacy → Relations: −200 … +200 between the main nation and a second one"_s,
                   frame(u"diplomacy"_s, [this] { return static_cast<QWidget *>(diplo_panel->rel); }, u"nation"_s)});
    // --- flags, loading screens, cultures
    out.push_back({u"Flag"_s, u"Flag studio — a nation's flags (any nation: pick it inside, or click it on the map)"_s,
                   [this]() -> QWidget * {
                       open_flag_studio();
                       return nullptr;
                   }});
    entryview::PagesWorkspace *lib = lib_ws;
    OptStr menu_key;
    for (const auto &pd : lib->pages)
        if (py::lower(pd.text).contains(u"menu"_s)) {
            menu_key = pd.key;
            break;
        }
    if (menu_key)
        for (const QString &name : {u"Loading screen pictures"_s, u"Main menu picture"_s}) {
            const QString mk = *menu_key;
            out.push_back({name, u"Library → Menu & loading: the pictures of the loading screens and the main menu"_s,
                           [this, lib, mk]() -> QWidget * {
                               go_page(u"library"_s);
                               lib->set_page(mk);
                               return lib;
                           }});
        }
    out.push_back({u"Add / remove cultures and culture groups"_s, u"Population → the Cultures window"_s,
                   [this]() -> QWidget * {
                       open_cultures();
                       return nullptr;
                   }});
    // --- every technology and invention: straight to it
    techpanel::TechWorkspace *tw = tech_ws;
    try {
        tw->refresh(doc.get());                      // (its list of every tech and invention, now)
    } catch (const std::exception &) {
    }
    for (const auto &[text, _v] : tw->_search_map) {
        const QString t = text;
        out.push_back({text, u"Technology → straight to it in the tree"_s, [this, tw, t]() -> QWidget * {
                           go_page(u"technology"_s);
                           tw->_search_pick(t);
                           return tw;
                       }});
    }
    // --- every good: Economy and the Output estimate
    prodpanel::ProductionWorkspace *pw = prod_ws;
    auto good = [this, pw](const QString &g, const QString &where) {
        return [this, pw, g, where]() -> QWidget * {
            if (where == u"output") {
                go_page(u"output"_s);
                try {
                    econ_ws->show_good(g);
                } catch (const std::exception &) {
                }
                return econ_ws;
            }
            go_page(u"production"_s);
            pw->explore(prodtree::Cid{u"good"_s, g, 0});
            return pw;
        };
    };
    OrderedMap<QString, QString> goods;              // key -> its "name" (Python: dict(self.mod.goods))
    for (const auto &[g, gd] : mod->goods)
        goods.insert(g, gd.name);
    try {                                            // goods.txt as the editor has it now
        if (store)
            for (const script::Node &grp : script::parse(store->text(u"common/goods.txt"_s)))
                if (grp.is_block)
                    for (const script::Node &c : grp.kids)
                        if (c.has_key && !c.key.isEmpty() && c.is_block && !goods.contains(c.key)) {
                            const OptStr l = loc(c.key);
                            goods.insert(c.key, truthy(l) ? *l : c.key);
                        }
    } catch (const std::exception &) {
    }
    std::vector<std::pair<QString, QString>> gl(goods.begin(), goods.end());
    std::stable_sort(gl.begin(), gl.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
    for (const auto &[g, gname] : gl) {
        const OptStr l = loc(g);
        const QString nm = truthy(l) ? *l : gname;
        out.push_back({u"Good: %1"_s.arg(nm), u"Economy → its production chain (who makes it, who uses it)"_s,
                       good(g, u"chain"_s)});
        out.push_back({u"Good: %1 — output"_s.arg(nm), u"Economy → Output estimate: how much of it is made per day"_s,
                       good(g, u"output"_s)});
    }
    // --- every nation, by name and tag
    auto nat = [this](const QString &t) {
        return [this, t]() -> QWidget * {
            go_page(u"nations"_s);
            show_nation(t);
            return nations_panel;
        };
    };
    QSet<QString> owned;
    for (const auto &[_pid, p] : mod->provinces)
        if (p.owner && !p.owner->isEmpty())
            owned.insert(*p.owner);
    OrderedMap<QString, QString> nations;            // tag -> its "name"
    for (const auto &[t, c] : mod->countries)
        nations.insert(t, c.name);
    if (doc->maps.contains(u"new_nations"_s))        // (made in the editor, not applied yet)
        for (const auto &[t, r] : doc->map<countryhist::NewNations>(u"new_nations"_s).added)
            if (!nations.contains(t))
                nations.insert(t, r.name);
    std::vector<std::pair<QString, QString>> nl(nations.begin(), nations.end());
    std::stable_sort(nl.begin(), nl.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
    for (const auto &[t, name] : nl)
        out.push_back({u"Nation: %1 (%2)"_s.arg(name, t),
                       u"Nation → %1"_s.arg(owned.contains(t) ? u"its government, parties, flags"_s
                                                              : u"not on the map at the start"_s),
                       nat(t)});
    // --- the Drawing layers straight away
    for (const auto &[lay, name] : {std::pair{u"prov"_s, u"Draw provinces"_s}, {u"terrain"_s, u"Draw terrain"_s},
                                    {u"rivers"_s, u"Draw rivers"_s}}) {
        const QString l = lay;
        out.push_back({name, u"Drawing → the %1 layer (provinces.bmp / terrain.bmp / rivers.bmp)"_s.arg(lay),
                       frame(u"draw"_s, [this, l]() -> QWidget * {
                           draw_panel->layer_btns.at(l)->setChecked(true);
                           return draw_panel->layer_btns.at(l);
                       })});
    }
    // --- every unit → its stats; every pop type: Pop influences
    auto popt = [this](const QString &k) {
        return [this, k]() -> QWidget * {
            go_page(u"popinf"_s);
            popinf_ws->show_place(k, u"promote_to"_s, QString(), u"in"_s);
            return popinf_ws;
        };
    };
    auto *uv = qobject_cast<unitpanel::UnitStatsView *>(war_ws->view(u"units"_s));
    if (uv) {
        try {
            uv->refresh(doc.get());
        } catch (const std::exception &) {
        }
        QStringList uk;
        for (const auto &[k, _rel] : uv->units)
            uk << k;
        std::stable_sort(uk.begin(), uk.end(),
                         [uv](const QString &a, const QString &b) { return py::lower(uv->uname(a)) < py::lower(uv->uname(b)); });
        for (const QString &k : uk)
            out.push_back({u"Unit: %1"_s.arg(uv->uname(k)), u"Warfare → Unit stats (%1)"_s.arg(k),
                           [this, uv, k]() -> QWidget * {
                               go_page(u"warfare"_s);
                               war_ws->set_page(u"units"_s);
                               uv->show_unit(k);
                               return uv;
                           }});
    }
    QStringList pts = popcheck::valid_types(*mod, store).values();   // the mod's pop types now (imported ones too)
    std::sort(pts.begin(), pts.end());
    for (const QString &k : pts) {
        const OptStr l = loc(k);
        QString nm;
        if (truthy(l))
            nm = *l;
        else if (const moddata::PopType *pt = mod->poptypes.get(k); pt && !pt->name.isEmpty())
            nm = pt->name;
        else
            nm = py::capitalize(QString(k).replace(u'_', u' '));
        out.push_back({u"Pop type: %1"_s.arg(nm),
                       u"Pop influences → who becomes it, what it becomes, where it moves, its ideology and issues"_s,
                       popt(k)});
    }
    return out;
}

// Every button and tick box of every page as a function of the palette: choosing it goes to its page (and the part of
// the page it is in) and frames it — it is not pressed.
std::vector<palette::Command> MainWindow::_palette_buttons(QSet<QString> have)
{
    struct Owner {
        QString page;
        QWidget *panel;
        const OrderedMap<QString, QWidget *> *views = nullptr;
        OrderedMap<QString, QString> titles;
        std::function<void(const QString &)> set_page;
    };
    std::vector<Owner> owners;
    auto plain = [&owners](const QString &pg, QWidget *w) { owners.push_back({pg, w, nullptr, {}, nullptr}); };
    auto pages_ws = [&owners](const QString &pg, entryview::PagesWorkspace *w) {
        Owner o{pg, w, &w->views, {}, [w](const QString &k) { w->set_page(k); }};
        for (const auto &pd : w->pages)
            o.titles.insert(pd.key, QString(pd.text).replace(u"&&"_s, u"&"_s));
        owners.push_back(std::move(o));
    };
    plain(u"pops"_s, edit_panel);
    plain(u"pops"_s, split_panel);
    plain(u"provinces"_s, prov_panel);
    plain(u"provinces"_s, util_panel);
    plain(u"draw"_s, draw_panel);
    plain(u"pos"_s, pos_panel);
    plain(u"adj"_s, map_panel);
    plain(u"mapsize"_s, size_ws);
    plain(u"nations"_s, nations_panel);
    plain(u"armies"_s, army_panel);
    plain(u"diplomacy"_s, diplo_panel);
    owners.push_back({u"politics"_s, pol_ws, &pol_ws->views, {}, [this](const QString &k) { pol_ws->set_page(k); }});
    plain(u"technology"_s, tech_ws);
    plain(u"production"_s, prod_ws);
    pages_ws(u"warfare"_s, war_ws);
    pages_ws(u"rules"_s, rules_ws);
    plain(u"events"_s, event_panel);
    plain(u"events"_s, script_ws);
    plain(u"shortcuts"_s, keys_ws);
    plain(u"errors"_s, chk_panel);
    plain(u"changelog"_s, cl_ws);
    pages_ws(u"library"_s, lib_ws);
    plain(u"import"_s, imp_ws);
    plain(u"submods"_s, sub_ws);
    plain(u"checksum"_s, ck_ws);
    if (!_palette_parts_made) {                      // (the parts of a page are made when first shown: made once
        _palette_parts_made = true;                  //  here, so their buttons are found)
        for (entryview::PagesWorkspace *ws : {war_ws, rules_ws, lib_ws})
            for (const auto &pd : ws->pages) {
                try {
                    ws->view(pd.key);
                } catch (const std::exception &) {
                }
            }
    }
    static const QRegularExpression SPACES(u"\\s+"_s, QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression NOT_LETTER(u"[^A-Za-z]"_s);
    auto cp_len = [](const QString &s) { return s.toUcs4().size(); };
    std::vector<palette::Command> out;
    for (const Owner &o : owners) {
        for (QAbstractButton *b : o.panel->findChildren<QAbstractButton *>()) {
            const auto *wb = qobject_cast<widgets::WrapButton *>(b);   // (its text is its label's: text() isn't virtual)
            const QString text = py::strip(QString(wb ? wb->text() : b->text()).replace(u"&&"_s, u"&"_s).replace(SPACES, u" "_s),
                                           u" …▸▾⤢:"_s);
            if (QString(text).remove(NOT_LETTER).size() < 3 || cp_len(text) > 70 || text == u"Order by type")
                continue;                            // (PALETTE_LEFT_OUT)
            OptStr part;
            if (o.views)
                for (const auto &[k, v] : *o.views)
                    if (v == b || v->isAncestorOf(b)) {
                        part = k;
                        break;
                    }
            const QString where = ::rail::PAGE_NAMES.value(o.page, o.page) +
                                  (part ? u" → "_s + o.titles.value(*part, *part) : QString());
            const QString key = py::lower(text + u" "_s + where);
            if (have.contains(key) || (have.contains(py::lower(text)) && !part))
                continue;
            have.insert(key);
            QString tip = QString(b->toolTip()).replace(SPACES, u" "_s);
            const QList<uint> tu = tip.toUcs4();
            if (tu.size() > 110)
                tip = QString::fromUcs4(reinterpret_cast<const char32_t *>(tu.constData()), 110) + u"…"_s;
            QWidget *panel = o.panel;
            const QString pg = o.page;
            const auto set_page = o.set_page;
            QPointer<QAbstractButton> bp(b);
            out.push_back({text, where + (!tip.isEmpty() ? u" — "_s + tip : QString()),
                           [this, pg, panel, bp, part, set_page]() -> QWidget * {
                               go_page(pg);
                               if (part && set_page)
                                   set_page(*part);
                               if (!bp)
                                   return panel;
                               QWidget *w = bp->parentWidget();
                               while (w && w != panel) {   // brought into view where it scrolls
                                   if (auto *sa = qobject_cast<QScrollArea *>(w))
                                       sa->ensureWidgetVisible(bp);
                                   w = w->parentWidget();
                               }
                               return bp->isVisible() ? static_cast<QWidget *>(bp.data()) : panel;
                           }});
        }
    }
    return out;
}

// The texts of the event / decision picked in Scripts, with the preview as in the game.
void MainWindow::open_texts()
{
    eventpanel::EventPanel *ep = event_panel;
    if (!text_win) {
        text_win = new locwin::TextsWindow(*mod, this);
        connect(text_win, &locwin::TextsWindow::nameEdit, this,
                [this](const QString &key, const QString &text) { on_name(key, text, std::nullopt); });
        connect(text_win, &locwin::TextsWindow::scriptEdit, this,
                [this](const QString &lb, const QString &rel, const QString &text) { on_script_text(lb, rel, text); });
        text_win->picture_names = [ep](bool is_event) { return ep->_picture_names(is_event); };
        text_win->set_picture = [ep](const QString &name) { ep->_set_picture(name); };
        connect(ep, &eventpanel::EventPanel::changed, this, [this, ep] {
            if (text_win->isVisible())
                text_win->refresh(doc.get(), ep->sel);
        });
    }
    text_win->refresh(doc.get(), ep->sel);
    _show_window(text_win, 1560, 980);
}

// The scripts were read again: the block builder's pickers and the network follow.
void MainWindow::_refresh_script_windows()
{
    if (block_win && block_win->isVisible())
        block_win->refresh(doc.get(), event_panel->refs.get(), event_panel->db.get());
    if (net_win && net_win->isVisible() && event_panel->db)
        net_win->set_data(event_panel->db.get(), event_panel->refs.get(),
                          [this](const events::Entry &e) { return _entry_name(e); });
}

// Scripts → 🔢 IDs: the mod's event-id system and Re-ID (a window of its own).
void MainWindow::open_reid()
{
    if (!reid_win) {
        reid_win = new reidwin::ReIdWindow(*mod, this);
        connect(reid_win, &reidwin::ReIdWindow::scriptsEdit, this,
                [this](const OrderedMap<QString, QString> &ch, const QString &l) { on_script_replace(ch, l); });
    }
    reid_win->open_for(doc.get());
    _show_window(reid_win, 1180, 760);
}

// Nation page ▸: the national value / the tech school on the page it is edited on.
void MainWindow::open_place(const QString &kind, const QString &key)
{
    if (kind == u"nv") {
        go_page(u"rules"_s);
        rules_ws->set_page(u"values"_s);
        if (auto *v = qobject_cast<iconboard::BoardWithList *>(rules_ws->view(u"values"_s))) {
            v->set_mode(0);
            if (!key.isEmpty()) {
                v->board->sel = key;
                v->board->refresh(doc.get());
            }
        }
        message(u"Game rules → National values."_s, 8000);
    } else if (kind == u"school") {
        go_page(u"technology"_s);
        tech_ws->set_unciv(true);
        if (!key.isEmpty()) {
            tech_ws->schools->cur = key;
            tech_ws->schools->refresh(doc.get());
        }
        message(u"Technology → Tech schools."_s, 8000);
    }
}

void MainWindow::on_tech_edit(const QString &label, const QString &key, const QString &text)
{
    auto &techs = doc->map<techdata::TechEdits>(u"techs"_s);
    doc->edit(label, {}, [&] { techs.set(key, text); }, {}, {}, {u"techs"_s});
    message(u"%1 — written on Apply (Ctrl+Z undoes it)."_s.arg(label), 8000);
}

void MainWindow::on_prod_edit(const QString &label, const QString &key, const QString &text)
{
    auto &prod = doc->map<production::ProdEdits>(u"production"_s);
    doc->edit(label, {}, [&] { prod.set(key, text); }, {}, {}, {u"production"_s});
    message(u"%1 — written on Apply (Ctrl+Z undoes it)."_s.arg(label), 8000);
}

void MainWindow::on_prod_other(const QString &label, const QString &what, const QString &key, const QString &value)
{
    // A good's price, a building or a pop type's needs changed in the Production tab.
    auto &prod = doc->map<production::ProdEdits>(u"production"_s);
    if (what == u"need" && doc->maps.contains(u"scripts"_s)) {   // a pop type file: the script texts hold it (Game rules
        on_script_text(label, prod.pop_rel(key), value);         // → Pop types edits the same file)
        return;
    }
    doc->edit(label, {}, [&] { prod.set_other(what, key, value); }, {}, {}, {u"production"_s});
    message(u"%1 — written on Apply (Ctrl+Z undoes it)."_s.arg(label), 8000);
}

void MainWindow::on_prod_add(const QString &key, const QString &text,
                             const std::optional<std::pair<QString, QString>> &building, const QString &bname)
{
    auto &prod = doc->map<production::ProdEdits>(u"production"_s);
    auto &loc = doc->map<localisation::LocEdits>(u"loc"_s);
    doc->edit(u"New production "_s + key, {}, [&] {
        prod.add(key, text, building);
        if (building && !bname.isEmpty()) {
            loc.set(building->first, bname, u"other"_s);
            _loc_tab[building->first] = u"production"_s;
        }
    }, {}, {}, {u"production"_s, u"loc"_s});
    message(u"New production "_s + key +
                (building ? u" with its factory building "_s + building->first +
                                u" (unlock it with a technology / invention: “Unlocks building”)"_s
                          : QString()) +
                u" — give it its inputs; the chart ties it in. Written on Apply."_s,
            12000);
}

// ---------------------------------------------------------------------------------------------- the mod's name
// The mod's name in the bar is a text box (read only): a double click or its ✎ makes it writable; Enter / a click
// elsewhere renames the mod (its .mod file at once, submods naming it, the editor's own folders named after it), Esc
// keeps the old name. C++ only (2026-11-08; a text box of its own since 2026-11-17).
void MainWindow::_show_mod_name()
{
    if (!mod_name_edit->property("renaming").toBool()) {
        const QString shown = mod->submod ? u"Submod: "_s + mod->name() : mod->name();
        mod_name_edit->setText(shown);
        mod_name_edit->setCursorPosition(0);
        const int tw = mod_name_edit->fontMetrics().horizontalAdvance(shown) + 54;   // (room for ✎ and the frame)
        mod_name_edit->setFixedWidth(std::clamp(tw, 180, 340));
    }
    setWindowTitle(doc ? u"ModEditor " MODEDITOR_VERSION_TEXT " — %1 — %2"_s.arg(mod->name(), doc->scenario) : u"ModEditor " MODEDITOR_VERSION_TEXT " — %1"_s.arg(mod->name()));
}

void MainWindow::begin_rename()
{
    mod_name_edit->setProperty("renaming", true);
    mod_name_edit->setText(mod->name());
    mod_name_edit->setReadOnly(false);
    mod_name_edit->setFixedWidth(std::max(mod_name_edit->width(), 260));
    mod_name_note->hide();
    mod_name_edit->selectAll();
    mod_name_edit->setFocus();
    message(u"Rename the mod: type its new name — Enter renames it, Esc keeps “%1”."_s.arg(mod->name()), 15000);
}

void MainWindow::end_rename(bool keep)
{
    if (!mod_name_edit->property("renaming").toBool())
        return;                                      // (editingFinished again as the box hides)
    if (keep) {
        const QString nw = mod_name_edit->text().trimmed();
        if (nw != mod->name()) {
            if (const QString why = modrename::check_name(nw); !why.isEmpty()) {
                mod_name_note->setText(u"⚠ "_s + why);    // the box stays open: fix it, or Esc
                mod_name_note->show();
                mod_name_edit->setFocus();
                return;
            }
            const QString msg = rename_mod(nw);
            message(msg, 20000, msg.startsWith(u"⚠"_s));
        }
    }
    mod_name_edit->setProperty("renaming", false);
    mod_name_note->hide();
    mod_name_edit->setReadOnly(true);
    mod_name_edit->deselect();
    _show_mod_name();
    if (mod_name_edit->hasFocus() && centralWidget())
        centralWidget()->setFocus();                 // (the box looks done: the caret leaves it)
}

QString MainWindow::rename_mod(const QString &name)
{
    const QString old = mod->name();
    const QString folder = mod->submod ? mod->submod->root : mod->root;
    const modrename::Result r = modrename::rename(folder, old, name);
    if (!r.ok)
        return u"⚠ The mod keeps its name: "_s + r.error;
    mod->display_name = name;
    // the editor's own folders named after the mod go along (they'd be left behind under the old name)
    QStringList notes;
    auto move = [&](const QString &kind, const QString &from, const QString &to) {
        if (from == to || !QDir(from).exists())
            return;
        if (QDir(to).exists())
            notes << u"the %1 under the new name already existed — the old ones stay in %2"_s.arg(kind, from);
        else if (!QDir().rename(from, to))
            notes << u"the %1 could not be moved (they stay in %2)"_s.arg(kind, from);
    };
    static const QRegularExpression CL_BAD(u"[^\\w .-]"_s);
    static const QRegularExpression AL_BAD(u"[^\\w .-]"_s, QRegularExpression::UseUnicodePropertiesOption);
    auto safe = [](QString s, const QRegularExpression &bad) { return s.isEmpty() ? u"mod"_s : s.replace(bad, u"_"_s); };
    const QString cl_to = pypath::join(changelog::app_dir(), {u"changelogs"_s, safe(name, CL_BAD)});
    move(u"changelogs"_s, pypath::join(changelog::app_dir(), {u"changelogs"_s, safe(old, CL_BAD)}), cl_to);
    const QString al_to = pypath::join(moreviews::app_dir(), {u"almanacs"_s, safe(name, AL_BAD)});
    move(u"almanacs"_s, pypath::join(moreviews::app_dir(), {u"almanacs"_s, safe(old, AL_BAD)}), al_to);
    for (auto *pg : findChildren<moreviews::AlmanacPage *>())
        pg->dir = al_to;
    if (cl_ws)
        cl_ws->dir = cl_to;
    // event category colours were kept under the mod's name
    auto st = settings_::settings();
    for (const QString &k : st->allKeys())
        for (const QString &pre : {u"eventCats/"_s, u"eventCatColors/"_s})
            if (k == pre + old || k.startsWith(pre + old + u"/"_s)) {
                const QString nk = pre + name + k.mid((pre + old).size());
                if (!st->contains(nk))
                    st->setValue(nk, st->value(k));
                st->remove(k);
            }
    _show_mod_name();
    QString msg = u"Renamed to “%1” — written to %2 at once (the launcher shows the new name; the folder, saves and "
                  u"settings stay as they were)."_s.arg(name, pypath::basename(r.modfile));
    if (!r.dependents.isEmpty())
        msg += u" Submods that named it now name the new one: %1."_s.arg(r.dependents.join(u", "_s));
    if (!notes.isEmpty())
        msg += u" ⚠ "_s + notes.join(u"; "_s) + u"."_s;
    if (!r.error.isEmpty())
        msg += u" ⚠"_s + r.error;
    return msg;
}

void MainWindow::on_prod_remove(const QString &key)
{
    auto &prod = doc->map<production::ProdEdits>(u"production"_s);
    auto &loc = doc->map<localisation::LocEdits>(u"loc"_s);
    const OptStr b = prod.building_of(key);
    doc->edit(u"Delete production "_s + key, {}, [&] {
        if (const auto *nb = prod.new_buildings.get(key))
            loc.drop(nb->first);
        prod.remove(key);
    }, {}, {}, {u"production"_s, u"loc"_s});
    prod_ws->sel.reset();
    message(key + u" deleted"_s + (b && !b->isEmpty() ? u" with its building "_s + *b : QString()) +
                u" — written on Apply (Ctrl+Z brings it back)."_s,
            10000);
}

void MainWindow::on_invention_new(const QString &key, const QString &file, const QString &text, const QString &name)
{
    auto &techs = doc->map<techdata::TechEdits>(u"techs"_s);
    auto &loc = doc->map<localisation::LocEdits>(u"loc"_s);
    doc->edit(u"New invention %1"_s.arg(name), {}, [&] {
        techs.add(key, file, text, u"inv"_s);
        loc.set(key, name, u"other"_s);
        _loc_tab[key] = u"technology"_s;
    }, {}, {}, {u"techs"_s, u"loc"_s});
    message(u"New invention “%1” — give it its effects; written to %2 on Apply (Ctrl+Z undoes it)."_s.arg(
                name, pypath::basename(file)), 10000);
}

void MainWindow::on_invention_remove(const QString &key)
{
    auto &techs = doc->map<techdata::TechEdits>(u"techs"_s);
    auto &loc = doc->map<localisation::LocEdits>(u"loc"_s);
    doc->edit(u"Invention %1 deleted"_s.arg(key), {}, [&] {
        const bool fresh = techs.added.contains(key);
        techs.remove(key);
        if (fresh) {
            loc.drop(key);
            loc.drop(key + u"_desc"_s);
        }
    }, {}, {}, {u"techs"_s, u"loc"_s});
    message(u"Invention %1 deleted — it goes from its file on Apply (Ctrl+Z undoes it)."_s.arg(key), 9000);
}

void MainWindow::on_tech_order(const OptStr &area, const QString &key, long long step)
{
    auto &techs = doc->map<techdata::TechEdits>(u"techs"_s);
    const QString way = step < 0 ? u"earlier"_s : u"later"_s;
    doc->edit(u"%1: %2"_s.arg(key, way), {}, [&] { techs.move(area, key, step); }, {}, {}, {u"techs"_s});
    message(u"%1 now comes %2 in its column (the file gets this order on Apply)."_s.arg(key, way), 8000);
}

void MainWindow::on_tech_drop(const OptStr &area, const QString &key, const QString &target)
{
    auto &techs = doc->map<techdata::TechEdits>(u"techs"_s);
    doc->edit(u"%1 moved to %2's place"_s.arg(key, target), {}, [&] { techs.move_to(area, key, target); }, {}, {},
              {u"techs"_s});
    message(u"%1 now comes %2 %3 in its column (the file gets this order on Apply)."_s.arg(
                key, u"right before / after"_s, target), 8000);
}

void MainWindow::on_tech_row_remove()
{
    auto &techs = doc->map<techdata::TechEdits>(u"techs"_s);
    techpanel::TechWorkspace *ws = tech_ws;
    std::vector<QStringList> cols;
    for (const techpanel::Folder &fo : ws->_folders_cache)
        for (const techpanel::Area &ar : fo.areas)
            cols.push_back(ar.keys);
    long long rows = 0;
    for (const QStringList &ks : cols)
        rows = std::max<long long>(rows, ks.size());
    if (rows < 7)
        return;
    QStringList gone;
    for (const QStringList &ks : cols)
        if (ks.size() == rows)
            gone << ks.last();
    long long inv = 0;
    for (const QString &k : gone)
        inv += ws->_inv_of.value(k).size();
    auto &loc = doc->map<localisation::LocEdits>(u"loc"_s);
    doc->edit(u"Remove the %1 tech row (%2 technologies)"_s.arg(techpanel::ordinal(rows), QString::number(gone.size())),
              {}, [&] {
                  for (const QString &k : gone) {
                      if (techs.added.contains(k))
                          loc.drop(k);             // a new technology's name goes with it
                      techs.remove(k);
                  }
              }, {}, {}, {u"techs"_s, u"loc"_s});
    if (ws->sel && gone.contains(*ws->sel))
        ws->sel.reset();
    message(u"The %1 tech row is gone (%2 technologies)%3 — Ctrl+Z brings it back."_s.arg(
                techpanel::ordinal(rows), QString::number(gone.size()),
                inv ? u"; ⚠ %1 invention%2 came with them and now come with none"_s.arg(
                          QString::number(inv), inv == 1 ? QString() : u"s"_s)
                    : QString()),
            12000);
}

void MainWindow::on_tech_row()
{
    auto &techs = doc->map<techdata::TechEdits>(u"techs"_s);
    auto &loc = doc->map<localisation::LocEdits>(u"loc"_s);
    const std::vector<techpanel::NewTech> plan = tech_ws->new_row_plan();
    if (plan.empty())
        return;
    const QString n = techpanel::ordinal(py::to_int(py::split(plan[0].name).last()));
    doc->edit(u"Add the %1 tech row (%2 technologies)"_s.arg(n, QString::number(plan.size())), {}, [&] {
        for (const techpanel::NewTech &t : plan) {
            techs.add(t.key, t.file, t.text);
            loc.set(t.key, t.name, u"other"_s);
            _loc_tab[t.key] = u"technology"_s;
        }
    }, {}, {}, {u"techs"_s, u"loc"_s});
    tech_ws->select(plan[0].key);
    message(u"The %1 tech row: %2 new technologies (one per column), years and costs continued from the row "
            "above — name them and give them effects. Written on Apply (Ctrl+Z undoes it)."_s.arg(
                n, QString::number(plan.size())),
            12000);
}

void MainWindow::on_tech_gui(long long rows)
{
    // the game's technology window made to fit `rows` rows (interface/country_technology.gui of the mod)
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    const QString rel = u"interface/country_technology.gui"_s;
    const OptStr fresh = techpanel::fit_tech_window(store.text(rel), rows);
    if (!fresh) {
        message(u"⚠ The technology window file (%1) isn't laid out as expected — not changed."_s.arg(rel), 0);
        return;
    }
    on_script_text(u"Technology window for %1 rows"_s.arg(rows), rel, *fresh);
}

void MainWindow::start_unit_move(const QString &kind)
{
    mode = kind == u"army" ? u"armymove"_s : u"navymove"_s;
    message(u"Click the province the %1 starts in (Esc cancels)."_s.arg(kind == u"army" ? u"army"_s : u"navy — a port"_s),
            0);
}

void MainWindow::on_oob_edit(const QString &label, const QString &tag, const std::function<void(oob::OOB &)> &fn)
{
    auto &o = doc->map<oob::OOBEdits>(u"oob"_s);
    doc->edit(label, {}, [&] { o.edit(tag, fn); }, {}, {}, {u"oob"_s});
    message(u"%1 — written on Apply (Ctrl+Z undoes it)."_s.arg(label), 8000);
}

void MainWindow::on_diplo_edit(const QString &label, const diplopanel::EditFn &fn)
{
    auto &o = doc->map<oob::OOBEdits>(u"oob"_s);
    auto &dp = doc->map<diplomacy::DiploEdits>(u"diplomacy"_s);
    doc->edit(label, {}, [&] { fn(o, dp); }, {}, {}, {u"oob"_s, u"diplomacy"_s});
    message(u"%1 — written on Apply (Ctrl+Z undoes it)."_s.arg(label), 8000);
}

// Flags made in the flag studio: {file name: picture}, one undoable step, written on Apply.
void MainWindow::on_flag_edit(const QString &label, const OrderedMap<QString, QImage> &files)
{
    if (!doc->maps.contains(u"flags"_s))
        return;
    auto &fe = doc->map<nationextras::FlagEdits>(u"flags"_s);
    doc->edit(label, {}, [&] { fe.put(files); }, {}, {}, {u"flags"_s});
    message(u"%1 — written to gfx/flags on Apply (Ctrl+Z undoes it)."_s.arg(label), 9000);
}

// Flag files taken out of the mod: one undoable step; Apply deletes them after a backup.
void MainWindow::on_flag_delete(const QString &label, const QStringList &names)
{
    if (!doc->maps.contains(u"flags"_s))
        return;
    auto &fe = doc->map<nationextras::FlagEdits>(u"flags"_s);
    doc->edit(label, {}, [&] { fe.drop(names); }, {}, {}, {u"flags"_s});
    message(u"%1 — the file goes on Apply (Ctrl+Z undoes it)."_s.arg(label), 9000);
}

// The flag studio — on its own (the user, 2026-10-19): without a nation picked it opens on the selected province's
// owner or the first nation, and another one is picked inside it or clicked on the map.
void MainWindow::open_flag_studio(const OptStr &sfx, const OptStr &tag0)
{
    OptStr tag = tag0 && !tag0->isEmpty() ? tag0 : nation_tag;
    if (!tag || tag->isEmpty()) {
        tag.reset();
        for (long long p : selection)
            if (mod->provinces.contains(p)) {
                const OptStr o = doc->prov_state(p).owner;
                if (o && !o->isEmpty()) {
                    tag = o;
                    break;
                }
            }
        if (!tag) {
            QStringList ts;
            for (const auto &[t, _c] : mod->countries)
                ts << t;
            std::sort(ts.begin(), ts.end());
            if (!ts.isEmpty())
                tag = ts.front();
        }
    }
    if (!tag)
        return;
    if (tag != nation_tag)
        show_nation(tag);
    if (!flag_win) {
        flag_win = new flagstudio::FlagStudio(*mod, this);
        connect(flag_win, &flagstudio::FlagStudio::flagEdit, this,
                [this](const QString &l, const OrderedMap<QString, QImage> &f) { on_flag_edit(l, f); });
        connect(flag_win, &flagstudio::FlagStudio::flagDelete, this,
                [this](const QString &l, const QStringList &n) { on_flag_delete(l, n); });
        connect(flag_win, &flagstudio::FlagStudio::nationWanted, this,
                [this](const QString &t) { open_flag_studio(std::nullopt, t); });
        connect(flag_win, &flagstudio::FlagStudio::pickOnMap, this, [this] { _flag_pick_on_map(); });
    }
    flag_win->open_for(doc.get(), *tag, sfx);
    _show_window(flag_win);
}

// The studio steps aside; the next click on the map opens its owner's flags (its work is kept).
void MainWindow::_flag_pick_on_map()
{
    flag_win->showMinimized();
    mode = u"flagpick"_s;
    raise();
    activateWindow();
    message(u"Click a province on the map — its owner's flags open in the flag studio (Esc cancels)."_s, 0);
}

// An entry of a list page (a pop type, a war goal …) in the large window.
void MainWindow::open_big(const QString &rel, const QStringList &path, const QString &noun)
{
    if (!big_win || big_win->doc != doc.get() || !doc->maps.contains(u"scripts"_s) ||
        big_win->store != &doc->map<scriptstore::ScriptStore>(u"scripts"_s)) {
        if (big_win)
            big_win->close();                    // (Python keeps the old one: a child of this window)
        big_win = new bigedit::BigEditor(*mod, doc.get(), [this] { _big_typed(); }, this);
    }
    big_win->open_entry(rel, path, noun);
    _show_window(big_win, 1640, 1000);
}

void MainWindow::_big_typed()
{
    update_pending();
    if (mode == u"edit" && (tab == u"warfare" || tab == u"rules" || tab == u"politics"))
        refresh_panel();
}

// A window of its own shown: the first time large (but inside the screen) and in the middle of this window's screen.
void MainWindow::_show_window(QWidget *win, std::optional<int> w, std::optional<int> h)
{
    const bool first = !win->property("_shown_once").toBool();
    win->show();
    if (first) {
        win->setProperty("_shown_once", true);
        const QRect scr = screen()->availableGeometry();
        if (w)
            win->resize(std::min(*w, scr.width() - 60), std::min(*h, scr.height() - 80));
        const QRect fg = win->frameGeometry();
        win->move(scr.left() + std::max(0, int(py::floordiv((long long)(scr.width() - fg.width()), 2LL))),
                  scr.top() + std::max(0, int(py::floordiv((long long)(scr.height() - fg.height()), 2LL))));
    }
    win->raise();
    win->activateWindow();
}

void MainWindow::open_parties_window()
{
    if (!nation_tag || nation_tag->isEmpty())
        return;
    if (!party_win) {
        party_win = new partieswin::PartiesWindow(*mod, this);
        connect(party_win, &partieswin::PartiesWindow::scriptEdit, this,
                [this](const QString &l, const QString &rel, const QString &text) { on_script_text(l, rel, text); });
        connect(party_win, &partieswin::PartiesWindow::nameEdit, this,
                [this](const QString &k, const QString &t) { on_name(k, t, std::nullopt); });
    }
    party_win->open_for(doc.get(), nation_tag);
    _show_window(party_win, 1640, 940);
}

// The windows that are open follow the document (an edit, an undo, Apply, another nation).
void MainWindow::_refresh_windows()
{
    if (block_win && block_win->isVisible())
        block_win->refresh(doc.get(), event_panel->refs.get(), event_panel->db.get());
    if (big_win && big_win->isVisible())
        big_win->refresh(doc.get());
    if (cg_win && cg_win->isVisible())
        cg_win->refresh(doc.get());
    if (text_win && text_win->isVisible())
        text_win->refresh(doc.get(), event_panel->sel);
    if (flag_win && flag_win->isVisible())
        flag_win->refresh(doc.get());
    if (party_win && party_win->isVisible())
        party_win->refresh(doc.get(), nation_tag && mod->countries.contains(*nation_tag) ? nation_tag : std::nullopt);
}

// {tag: (r, g, b)} as the nations' files say now, for the files the editor has open.
std::map<QString, Rgb> MainWindow::_file_colors()
{
    std::map<QString, Rgb> out;
    if (!doc->maps.contains(u"scripts"_s))
        return out;
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    for (const auto &[tag, c] : mod->countries) {
        if (c.file.isEmpty())
            continue;
        const QString rel = u"common/"_s + QString(c.file).replace(u'\\', u'/');
        if (!store.is_open(rel))
            continue;
        const auto m = py::re_search(u"(?m)^\\s*color\\s*=\\s*\\{\\s*(\\d+)\\s+(\\d+)\\s+(\\d+)\\s*\\}"_s, store.text(rel));
        if (m.hasMatch())
            out[tag] = {std::min(255, m.captured(1).toInt()), std::min(255, m.captured(2).toInt()),
                        std::min(255, m.captured(3).toInt())};
    }
    return out;
}

// The map takes the nations' colours from their files as edited (when the Nation page's switch is on, or always
// after Apply).
void MainWindow::_sync_nation_colors(bool force)
{
    if (!force && !nations_panel->parties_box->is_live())
        return;
    bool changed = false;
    for (const auto &[tag, rgb] : _file_colors()) {
        moddata::Country *c = mod->countries.get(tag);
        if (c->color != rgb) {
            c->color = rgb;
            changed = true;
        }
    }
    if (changed)
        _render_timer->start(40);
}

// The colour wheel is open: the map shows the colour it is on (None: back to what the files say).
void MainWindow::_nation_color_preview(const std::optional<Rgb> &rgb)
{
    if (!rgb || !nation_tag || !mod->countries.contains(*nation_tag)) {
        _sync_nation_colors();
        return;
    }
    moddata::Country *c = mod->countries.get(*nation_tag);
    if (c->color != *rgb) {
        c->color = *rgb;
        _render_timer->start(40);
    }
}

void MainWindow::on_edit_definitions(const QString &kind)
{
    auto &mf = doc->map<mapfiles::MembershipFile>(kind);
    std::optional<QString> start;
    if (!selection.empty())
        if (const QStringList cur = mf.of(selection[0]); !cur.isEmpty())
            start = cur[0];
    mapdefs::MapDefsDialog dlg(mf, start, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const auto [mods, fresh, removed, moves] = dlg.result_changes();
    OrderedMap<QString, QString> renames;
    for (const auto &[k, v] : dlg.renames)
        if (!removed.contains(k))
            renames.insert(k, v);
    if (!renames.isEmpty()) {
        auto &loc = doc->map<localisation::LocEdits>(u"loc"_s);
        doc->edit(u"Rename %1 %2%3"_s.arg(renames.size()).arg(kind, renames.size() == 1 ? QString() : u"s"_s), {},
                  [&] {
                      for (const auto &[k, v] : renames) {
                          loc.set(k, v, kind == u"continent" ? u"continent"_s : u"other"_s);
                          _loc_tab[k] = u"provinces"_s;
                      }
                  },
                  {}, {}, {u"loc"_s});
    }
    if (mods.isEmpty() && fresh.isEmpty() && removed.isEmpty()) {
        if (!renames.isEmpty()) {
            QStringList rn;
            for (const auto &[k, v] : renames)
                rn << u"%1 → %2"_s.arg(k, v);
            message(u"Renamed %1 — written to the localisation on Apply. Ctrl+Z to undo."_s.arg(rn.join(u", "_s)));
        } else {
            message(u"No %1 changes."_s.arg(kind));
        }
        return;
    }
    doc->set_definitions(kind, mods, fresh, removed, moves);
    QStringList parts;
    if (!fresh.isEmpty())
        parts << u"added %1"_s.arg(fresh.join(u", "_s));
    if (!removed.isEmpty())
        parts << u"removed %1"_s.arg(removed.join(u", "_s));
    QStringList changed;
    for (const auto &[n, _m] : mods)
        if (!fresh.contains(n))
            changed << n;
    if (!changed.isEmpty())
        parts << u"changed %1"_s.arg(changed.join(u", "_s));
    message(u"%1s: %2. Saved to map/%3.txt on Apply. Ctrl+Z to undo."_s.arg(py::capitalize(kind), parts.join(u"; "_s), kind));
}

// Files from the user's library for the mod ({rel: (how, what)}): one undoable step, written on Apply.
void MainWindow::on_asset_edit(const QString &label, const OrderedMap<QString, moreviews::Asset> &files)
{
    if (!doc->maps.contains(u"assets"_s))
        return;
    auto &assets = doc->map<moreviews::AssetEdits>(u"assets"_s);
    doc->edit(label, {}, [&] {
        for (const auto &[k, a] : files)
            assets.fresh.insert(k, a);
    }, {}, {}, {u"assets"_s});
    message(u"%1 — written into the mod on Apply (Ctrl+Z undoes it)."_s.arg(label), 9000);
}

// Things came over from another mod: what is listed by name knows the new ones.
void MainWindow::_after_import()
{
    after_culture_change();
    try {                                          // (Python passes over a failure there)
        _build_search_index();
    } catch (...) {
    }
    if (doc->maps.contains(u"loc"_s))                // (the texts that came along count for the Import page)
        for (const auto &kv : doc->map<localisation::LocEdits>(u"loc"_s).fresh)
            _loc_tab.emplace(kv.first, u"import"_s);
    update_pending();
}

// C++ only (Building types, 2026-11-16): texts, pictures and the names of new things as one undoable step.
void MainWindow::on_bundle_names(const QString &label, const OrderedMap<QString, QString> &texts,
                                 const OrderedMap<QString, moreviews::Asset> &files, const OrderedMap<QString, QString> &names)
{
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    moreviews::AssetEdits *assets = doc->maps.contains(u"assets"_s) ? &doc->map<moreviews::AssetEdits>(u"assets"_s)
                                                                     : nullptr;
    localisation::LocEdits *loc = doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    doc->edit(label, {}, [&] {
        for (const auto &[rel, text] : texts)
            store.set_text(rel, text);
        if (assets)
            for (const auto &[k, a] : files)
                assets->fresh.insert(k, a);
        if (loc)
            for (const auto &[k, t] : names) {
                const OptStr have = loc->text(k);
                if (!have || have->isEmpty())
                    loc->set(k, t, std::nullopt);
            }
    }, {}, {}, {u"scripts"_s, u"assets"_s, u"loc"_s});
    message(u"%1 — written into the mod on Apply (Ctrl+Z undoes it)."_s.arg(label), 9000);
    refresh_panel();
}

// buildings.txt as the editor has it now: the buildings and the province histories' extra levels follow it.
void MainWindow::_buildings_changed()
{
    if (!doc || !doc->maps.contains(u"scripts"_s))
        return;
    const QString t = doc->map<scriptstore::ScriptStore>(u"scripts"_s).text(buildtypes::BUILDINGS);
    mod->buildings_from(t);
    QStringList more;
    for (const QString &k : provbuild::province_keys(t))
        if (!provbuild::GAME_THREE.contains(k) && k != u"colonial")
            more << k;
    doc->levels_changed(more);
    if (tab == u"provinces" || tab == u"rules")
        refresh_panel();
}

// Script texts and pictures that belong together (map marks: two interface files and their pictures): one undoable
// step, written on Apply.
void MainWindow::on_bundle_edit(const QString &label, const OrderedMap<QString, QString> &texts,
                                const OrderedMap<QString, moreviews::Asset> &files)
{
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    moreviews::AssetEdits *assets = doc->maps.contains(u"assets"_s) ? &doc->map<moreviews::AssetEdits>(u"assets"_s)
                                                                     : nullptr;
    doc->edit(label, {}, [&] {
        for (const auto &[rel, text] : texts)
            store.set_text(rel, text);
        if (assets)
            for (const auto &[k, a] : files)
                assets->fresh.insert(k, a);
    }, {}, {}, {u"scripts"_s, u"assets"_s});
    message(u"%1 — written into the mod on Apply (Ctrl+Z undoes it)."_s.arg(label), 9000);
}

QWidget *MainWindow::_boards_page()
{
    auto *v = new iconboard::AllBoardsPage(*mod, [this](const QString &lb, const iconstrips::StripFiles &f,
                                                        const OrderedMap<QString, QString> &tx) { on_strip_change(lb, f, tx); });
    connect(v, &iconboard::AllBoardsPage::placeRequested, this,
            [this](const QString &rel, long long pos) { open_script_place(rel, pos); });
    return v;
}

QWidget *MainWindow::_mapmarks_page()
{
    auto *v = new mapmarkspage::MapMarksPage(*mod);
    v->md = mapdata.get();                             // (C++ only: the game's screen previews draw the map)
    connect(v, &mapmarkspage::MapMarksPage::bundleEdit, this,
            [this](const QString &lb, const mapmarkspage::Texts &tx, const mapmarkspage::Files &f) { on_bundle_edit(lb, tx, f); });
    return v;
}

// The picture names the events use (lower case), or None while the scripts aren't read.
std::optional<QSet<QString>> MainWindow::_used_pictures()
{
    events::EventDB *db = event_panel->db.get();
    if (!db)
        return std::nullopt;
    QSet<QString> out;
    for (const auto &e : db->entries)
        if (!e->picture.isEmpty())
            out.insert(py::lower(py::strip(py::strip(e->picture),u"\""_s)));
    return out;
}

// An icon strip made longer / a picture of it replaced: the pictures and the interface files that count them, as one
// undoable step.
void MainWindow::on_strip_change(const QString &label, const iconstrips::StripFiles &files,
                                 const OrderedMap<QString, QString> &texts)
{
    if (!doc->maps.contains(u"assets"_s) || !doc->maps.contains(u"scripts"_s))
        return;
    auto &assets = doc->map<moreviews::AssetEdits>(u"assets"_s);
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    doc->edit(label, {}, [&] {
        for (const auto &[k, a] : files)
            assets.fresh.insert(k, a);
        for (const auto &[rel, text] : texts)
            store.set_text(rel, text);
    }, {}, {}, {u"assets"_s, u"scripts"_s});
    message(u"%1 — written into the mod on Apply (Ctrl+Z undoes it)."_s.arg(label), 9000);
}

// Map size: the provinces a cut takes away completely are deleted and written before the files are cut (their pixels
// become "no province" — they lie in the part that goes).
bool MainWindow::_delete_before_cut(const std::vector<long long> &pids)
{
    provdelete::delete_provinces(*doc, *mapdata, pids, u"blank"_s, landsea.get());
    return save(false);                            // (the cut itself asks for the mod to be read again)
}

// The map's files were replaced (Map size), or the province numbers were closed up: this window closes and the same
// mod is read again.
bool MainWindow::_pending_counts()
{
    // Is there a pending edit that changes the game's checksum (anything but interface files, pictures, flags,
    // texts and positions)?
    document::Document &d = *doc;
    if (!d.dirty_provinces().empty() || !d.dirty_regions().isEmpty() || !d.dirty_histories().empty())
        return true;
    for (const auto &[kind, mf] : d.maps) {
        const QStringList dirty = mf->dirty();
        if (dirty.isEmpty() || kind == u"assets" || kind == u"flags" || kind == u"loc" || kind == u"positions")
            continue;
        if (kind != u"scripts")
            return true;
        for (const QString &rel : dirty)
            if (submods::counts(rel))
                return true;
    }
    return false;
}

void MainWindow::_show_sub_strip()
{
    const auto &sub = mod->submod;
    if (!sub || !doc)
        return;
    const auto files = submods::holds(sub->root);
    const auto [same, says] = submods::verdict(files, _pending_counts());
    const QString nf = QString::number(files.size());
    sub_strip->setText(u"<b>SUBMOD of %1</b> — you see the parent's files; only what you change is saved, into "
                       u"<b>mod\\%2</b> (%3 file%4 now). &nbsp; <span style='color:%5; font-weight:700'>%6</span>"_s.arg(
                           sub->parents.join(u", "_s), pypath::basename(sub->root), nf,
                           files.size() == 1 ? u""_s : u"s"_s, same ? theme::OK : theme::WARN,
                           says.left(1).toUpper() + says.mid(1)));
}

QString MainWindow::_submod_sync()
{
    // A submod is edited in a working copy (submods.py): what changed there since the last look goes into the
    // submod's own folder — only what differs from the parent mod. -> a sentence about it ("" = nothing).
    const auto &sub = mod->submod;
    if (!sub)
        return u""_s;
    auto game = [this](const QString &rel) -> std::optional<QString> {   // the game's own file, where the game reads it
        const QStringList parts = rel.split(u'/');
        if (!mod->base || mod->base->isEmpty())
            return std::nullopt;
        QString path = *mod->base;
        for (const QString &x : parts)
            path = pypath::join(path, x);
        return !mod->replaced(parts) && QFileInfo(path).isFile() ? std::optional<QString>(path) : std::nullopt;
    };
    submods::Synced s;
    try {
        s = submods::sync(sub->work, sub->view, sub->state, sub->root, backup_root, game);
    } catch (const fs::OSError &e) {
        return u" ⚠ The submod's folder could not be written (%1): the changes are kept in the editor's working copy "
               u"and go over at the next Apply."_s.arg(QString::fromUtf8(e.what()));
    }
    sub->state = s.now;
    QString note;
    if (!s.written.isEmpty() || !s.removed.isEmpty() || !s.emptied.isEmpty()) {
        QStringList parts;
        if (!s.written.isEmpty())
            parts << u"%1 file%2 written"_s.arg(QString::number(s.written.size()), s.written.size() == 1 ? u""_s : u"s"_s);
        if (!s.removed.isEmpty())
            parts << u"%1 taken out (no longer different)"_s.arg(s.removed.size());
        if (!s.emptied.isEmpty())
            parts << u"%1 of the parent's replaced by an empty file"_s.arg(s.emptied.size());
        note = u" Submod %1: %2."_s.arg(pypath::basename(sub->root), parts.join(u", "_s));
    }
    if (!s.cannot.isEmpty())
        note += u" ⚠ A submod cannot take a file away from its parent mod — the game still reads: %1%2."_s.arg(
            s.cannot.mid(0, 4).join(u", "_s), s.cannot.size() > 4 ? u" …"_s : u""_s);
    _show_sub_strip();
    return note;
}

void MainWindow::_reload_mod()
{
    const QString note = _submod_sync();
    if (note.contains(u"⚠"_s))
        reload_note = py::strip(reload_note + note);
    reload_requested = true;
    _check_timer->stop();                          // (this window's map is the old one: nothing more is checked)
    doc->touched.clear();
    doc->regions_touched.clear();
    close();
}
void MainWindow::on_doc_changed()
{
    _cl_note();
    if (doc->maps.contains(u"scripts"_s)) {
        localisation::LocEdits *loc = doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
        if (importer::sync_cultures(*mod, doc->map<scriptstore::ScriptStore>(u"scripts"_s), loc))
            after_culture_change();                // cultures.txt / religion.txt changed (import, undo, Library)
    }
    if (_jump_mark) {
        _jump_mark.reset();
        _apply_overlay();
    }
    if (doc->region_version != _region_version)
        _regions_changed();
    else
        update_highlight();
    update_pending();
    schedule_checks();
    schedule_render();
    _sync_nation_colors();
    _refresh_windows();
    if (tab == u"provinces" && prov_sub == u"utility")
        _render_timer->start(120);                 // a utility state's provinces changed (or an undo)
    if (_tool_window()) {                          // built in the block builder: the page behind follows in a pause
        _tool_edit = py_monotonic();               // (reading every script again takes a moment)
        _lazy_panel->start(500);
        return;
    }
    if (mode == u"edit") {
        refresh_panel();
    } else if (tab == u"events" && event_panel->db) {
        event_panel->reparse_now();                // an edit of the scripts undone / redone: read them again
        event_panel->cats_changed();
    }
}

OrderedMap<QString, long long> MainWindow::pending_by_tab()
{
    // {rail page: pending edits} — the parts of the "Pending edits" number, by the page they belong to
    document::Document &d = *doc;
    OrderedMap<QString, long long> out;
    auto add = [&out](const QString &t, long long n) {
        if (n)
            out[t] += n;
    };
    add(u"pops"_s, static_cast<long long>(d.dirty_provinces().size()));
    add(u"provinces"_s, static_cast<long long>(d.dirty_regions().size() + d.dirty_histories().size()));
    static const std::map<QString, QString> where = {
        {u"nations"_s, u"nations"_s}, {u"new_nations"_s, u"nations"_s}, {u"flags"_s, u"nations"_s},
        {u"assets"_s, u"library"_s}, {u"oob"_s, u"armies"_s}, {u"diplomacy"_s, u"diplomacy"_s},
        {u"techs"_s, u"technology"_s}, {u"production"_s, u"production"_s}, {u"utility"_s, u"provinces"_s},
        {u"event_cats"_s, u"events"_s}, {u"event_mods"_s, u"events"_s}, {u"climate"_s, u"provinces"_s},
        {u"continent"_s, u"provinces"_s}, {u"adjacency"_s, u"adj"_s}, {u"positions"_s, u"pos"_s}};
    for (const auto &[kind, mf] : d.maps) {
        const QStringList dirty = mf->dirty();
        if (dirty.isEmpty())
            continue;
        if (kind == u"scripts") {
            for (const QString &rel : dirty)
                add(script_page(rel), 1);
        } else if (kind == u"loc") {
            for (const QString &key : dirty) {
                const auto lt = _loc_tab.find(key);
                if (lt != _loc_tab.end() && !lt->second.isEmpty())
                    add(lt->second, 1);
                else
                    add(mod->countries.contains(key) || key.endsWith(u"_ADJ"_s) ? u"nations"_s : u"provinces"_s, 1);
            }
        } else {
            const auto it = where.find(kind);
            add(it != where.end() ? it->second : u"draw"_s, dirty.size());
        }
    }
    return out;
}

void MainWindow::update_pending()
{
    const auto by = pending_by_tab();
    long long n = 0;
    for (const auto &[_k, v] : by)
        n += v;
    st_pending->setText(u"Pending edits: %1"_s.arg(n));
    QStringList tip;
    for (const auto &[k, name] : ::rail::PAGE_NAMES)
        if (const long long *v = by.get(k); v && *v)
            tip << u"%1: %2"_s.arg(name).arg(*v);
    st_pending->setToolTip(tip.isEmpty() ? u"Nothing to apply"_s : tip.join(u'\n'));
    apply_btn->setEnabled(n > 0);
    revert_btn->setEnabled(n > 0);
    _refresh_undo_tips();
    _pend_pages = by;
    _show_sub_strip();
    rail->set_counts(_pend_pages, _err_pages);
}

// ---------------------------------------------------------------------------------------------- undo / redo
std::pair<long long, QString> MainWindow::_scope_text(const void *step) const
{
    const auto &s = *static_cast<const document::Step *>(step);
    const auto [n, parts] = doc->step_scope(s);
    return {n, u"%1 — %2 change%3: %4"_s.arg(s.label).arg(n).arg(n == 1 ? u""_s : u"s"_s,
                                                                   parts.isEmpty() ? u"—"_s : parts.join(u"; "_s))};
}

// Edits a panel holds back for a moment (a slider being dragged writes once it rests): made steps of their own NOW, so
// an undo / redo / Apply right after them meets them in the order they were made (2026-11-13: a Ctrl+Z within 0.45 s
// of moving a nation's slider undid the edit BEFORE it, and the slider's change landed afterwards).
void MainWindow::_flush_held_edits()
{
    if (nations_panel && nations_panel->_timer->isActive()) {
        nations_panel->_timer->stop();
        nations_panel->_flush();
    }
    if (diplo_panel && diplo_panel->_timer->isActive()) {
        diplo_panel->_timer->stop();
        diplo_panel->_flush();
    }
    if (dip_view && dip_view->_rel_timer->isActive()) {
        dip_view->_rel_timer->stop();
        dip_view->_rel_done();
    }
}

OptStr MainWindow::undo()
{
    _flush_held_edits();
    doc->flush_live();                            // (typing not reported yet: its own step first)
    if (edit_panel->pending_undo()) {             // Pops: a new total / mix not applied yet is the newest change
        message(u"Undid an unapplied pop change (the preview went one step back)"_s);
        return u"unapplied pop change"_s;
    }
    _cl_note();
    const auto &st = doc->undo_stack;
    if (!st.empty()) {
        const void *last = st.back().get();
        const auto [n, what] = _scope_text(last);
        if (n > UNDO_CONFIRM && _undo_armed != last) {
            _undo_armed = last;                    // asks once: the same undo again within 8 s does it
            QTimer::singleShot(8000, this, [this, last] {
                if (_undo_armed == last) {
                    _undo_armed = nullptr;
                    notice->hide();
                }
            });
            notice->setText(u"This undo takes back %1 changes at once: %2.  Press Undo (Ctrl+Z) again within 8 seconds "
                            u"to do it — Redo (Ctrl+Y) brings them back."_s.arg(n).arg(what));
            notice->show();
            message(u"⚠ Undo would take back %1 changes at once — press it again to confirm."_s.arg(n), 8000);
            return std::nullopt;
        }
    }
    _undo_armed = nullptr;
    notice->hide();
    std::optional<ClInfo> info;
    if (!st.empty()) {
        auto &cl = _cl_edits();
        if (auto it = cl.find(st.back().get()); it != cl.end())
            info = *it;
    }
    const OptStr label = doc->undo();
    if (label && info && info->applied)           // it was applied already: the undo is a change now
        message(u"Undid: %1 — that was applied already: the undo is a pending change now, Apply writes it."_s.arg(*label),
                12000);
    else
        message(label ? u"Undid: %1"_s.arg(*label) : u"Nothing to undo"_s);
    std::vector<long long> caps;
    for (const auto &[rid, _r] : mod->regions)
        if (const auto c = doc->capital(rid))
            caps.push_back(*c);
    map->set_capitals(caps);
    _refresh_undo_tips();
    return label;
}

OptStr MainWindow::redo()
{
    _flush_held_edits();
    doc->flush_live();
    _cl_note();
    _undo_armed = nullptr;
    if (edit_panel->pending_redo()) {
        message(u"Redid an unapplied pop change (not applied yet)"_s);
        return u"unapplied pop change"_s;
    }
    const OptStr label = doc->redo();
    message(label ? u"Redid: %1"_s.arg(*label) : u"Nothing to redo"_s);
    std::vector<long long> caps;
    for (const auto &[rid, _r] : mod->regions)
        if (const auto c = doc->capital(rid))
            caps.push_back(*c);
    map->set_capitals(caps);
    _refresh_undo_tips();
    return label;
}

void MainWindow::_refresh_undo_tips()
{
    // the ↶ / ↷ buttons say what the next undo / redo takes back — before it happens
    if (!_undo_btn || !doc)
        return;
    const auto &st = doc->undo_stack, &rs = doc->redo_stack;
    _undo_btn->setToolTip(!st.empty() ? u"Undo (Ctrl+Z): %1\n%2 step%3 to undo (the last %4 are kept)"_s
                                            .arg(_scope_text(st.back().get()).second)
                                            .arg(st.size())
                                            .arg(st.size() == 1 ? u""_s : u"s"_s)
                                            .arg(document::UNDO_STEPS)
                                      : u"Undo (Ctrl+Z): nothing to undo"_s);
    _redo_btn->setToolTip(!rs.empty() ? u"Redo (Ctrl+Y): %1"_s.arg(_scope_text(rs.back().get()).second)
                                      : u"Redo (Ctrl+Y): nothing to redo"_s);
}

// Typing in a script text (the Scripts page, the large editor …): each burst an undo step of its own.
void MainWindow::_script_typed(const QString &rel)
{
    if (!doc || doc->busy())
        return;
    if (doc->record_live(u"scripts"_s, rel, u"Typed in %1"_s.arg(rel))) {
        _cl_note();
        if (!_typing_timer) {
            _typing_timer = new QTimer(this);
            _typing_timer->setSingleShot(true);
            _typing_timer->setInterval(400);
            connect(_typing_timer, &QTimer::timeout, this, [this] {
                update_pending();
                _refresh_undo_tips();
            });
        }
        _typing_timer->start();
    }
}

void MainWindow::_refresh_apply_undo()
{
    if (!journal)
        return;
    const auto u = journal->undoable(), r = journal->redoable();
    undo_apply_btn->setVisible(u.has_value());
    redo_apply_btn->setVisible(r.has_value());
    if (u)
        undo_apply_btn->setToolTip(
            u"Undo the Apply of %1: puts back %2 as they were before it, then reads the mod again.\n"
            "(Every Apply keeps a copy of each file it changed — backups/applies.)"_s.arg(
                u->second.obj().value(u"when"_s).str(), applyjournal::describe(u->second, 12)));
    if (r)
        redo_apply_btn->setToolTip(u"Redo the Apply of %1: writes %2 again as that Apply wrote them."_s.arg(
            r->second.obj().value(u"when"_s).str(), applyjournal::describe(r->second, 12)));
}

// Undo / redo of a whole Apply (asks once: the button again within 6 s).
bool MainWindow::_apply_whole(const QString &which)
{
    const bool undo_ = which == u"undo";
    auto e = undo_ ? journal->undoable() : journal->redoable();
    QPushButton *btn = undo_ ? undo_apply_btn : redo_apply_btn;
    if (!e) {
        message(u"Nothing to %1"_s.arg(undo_ ? u"undo"_s : u"redo"_s));
        return false;
    }
    const pyjson::Object &man = e->second.obj();
    const QString when = man.value(u"when"_s).str();
    const long long pend = doc->pending_count();
    if (_whole_armed != std::optional<std::pair<QString, QString>>(std::pair{which, e->first})) {
        _whole_armed = std::pair{which, e->first};
        const qsizetype nf = man.value(u"files"_s).obj().size();
        btn->setText(u"Click again: %1 %2 file%3"_s.arg(undo_ ? u"put back"_s : u"rewrite"_s).arg(nf).arg(
            nf == 1 ? u""_s : u"s"_s));
        QTimer::singleShot(6000, this, [this] {
            _whole_armed.reset();
            undo_apply_btn->setText(u"↶ Apply"_s);
            redo_apply_btn->setText(u"↷ Apply"_s);
        });
        message(u"%1 the Apply of %2: %3. Click again within 6 s.%4"_s.arg(
                    undo_ ? u"Undo"_s : u"Redo"_s, when, applyjournal::describe(e->second),
                    pend ? u" Your %1 unapplied edit(s) are kept: the mod is read again and they are put back on top."_s
                               .arg(pend)
                         : QString()),
                10000);
        return false;
    }
    _whole_armed.reset();
    // (it reads the mod again: the pending edits are kept — put back on top in the new window)
    if (pend && !keep_edits_over_reload(u"Kept over the %1 of the Apply of %2."_s.arg(undo_ ? u"undo"_s : u"redo"_s,
                                                                                      when))) {
        message(u"⚠ The unapplied edits could not be kept — nothing was undone. Apply or revert them first."_s, 12000);
        return false;
    }
    const auto [n, since] = undo_ ? journal->undo(*e) : journal->redo(*e);
    (void)n;
    const QString text = u"Apply of %1 %2 — %3 %4%5"_s.arg(
        when, undo_ ? u"undone"_s : u"redone"_s, applyjournal::describe(e->second),
        undo_ ? u"put back as before it"_s : u"written again"_s,
        !since.isEmpty() ? u" (%1 had changed since; those versions are kept in backups/applies/%2)"_s
                               .arg(since.size())
                               .arg(man.value(u"stamp"_s).str())
                         : QString());
    try {
        pyjson::Object c;
        c.insert(u"text"_s, pyjson::Value(text));
        c.insert(u"section"_s, pyjson::Value(u"Apply"_s));
        c.insert(u"src"_s, pyjson::Value(u"apply"_s));
        cl_ws->record(pyjson::Array{pyjson::Value(std::move(c))}, doc->scenario);
    } catch (const std::exception &) {
    }
    reload_note = text + u"."_s;
    message(text + u" Reading the mod again…"_s, 15000);
    _reload_mod();
    return true;
}

bool MainWindow::undo_apply() { return _apply_whole(u"undo"_s); }
bool MainWindow::redo_apply() { return _apply_whole(u"redo"_s); }

// ---------------------------------------------------------------------------------------------- the recovery file
void MainWindow::_recovery_tick()
{
    // every few seconds: when the edits changed, the recovery file is written again (or goes: none pending)
    if (!doc || _rec_path.isEmpty())
        return;
    const auto *st = doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    const QString sig = u"%1|%2|%3|%4"_s.arg(doc->undo_stack.size())
                            .arg(doc->undo_stack.empty() ? 0 : reinterpret_cast<quintptr>(doc->undo_stack.back().get()))
                            .arg(doc->redo_stack.size())
                            .arg(st ? st->live_sig() : u"None"_s);
    if (_rec_sig == sig)
        return;
    _rec_sig = sig;
    _recovery_save();
}

bool MainWindow::_recovery_save(bool auto_, const QString &note)
{
    try {
        return recovery::save(_rec_path, *doc, auto_, note).has_value();
    } catch (const std::exception &e) {           // (never stops the editor — but said)
        message(u"⚠ The recovery file of the unapplied edits could not be written: %1"_s.arg(QString::fromUtf8(e.what())),
                12000);
        return false;
    }
}

// Before the mod is read again (undo / redo of an Apply, rescan): the pending edits go into the recovery file marked
// to be put back at once in the new window. -> true when there were any.
bool MainWindow::keep_edits_over_reload(const QString &note)
{
    if (!doc || !doc->pending_count())
        return false;
    return _recovery_save(true, note);
}

// ---------------------------------------------------------------------- files added / taken away while it runs
std::set<QString> MainWindow::_mod_listing() const
{
    std::set<QString> out;
    for (const QString &sub : {u"map"_s, u"common"_s, u"localisation"_s, u"interface"_s, u"poptypes"_s, u"history"_s,
                               u"events"_s, u"decisions"_s, u"technologies"_s, u"inventions"_s, u"units"_s,
                               u"gfx/flags"_s}) {
        const QDir d(mod->root + u'/' + sub);       // (os.listdir: files and folders, hidden ones too; none = skipped)
        for (const QString &fn : d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
            out.insert(sub + u'/' + fn);
    }
    return out;
}

void MainWindow::_scan_tick()
{
    if (!doc)
        return;
    std::set<QString> now = _mod_listing();
    if (!_listing) {
        _listing = std::move(now);
        return;
    }
    if (now == *_listing)
        return;
    QStringList added, gone;
    std::set_difference(now.begin(), now.end(), _listing->begin(), _listing->end(), std::back_inserter(added));
    std::set_difference(_listing->begin(), _listing->end(), now.begin(), now.end(), std::back_inserter(gone));
    _scan_seen = {added, gone};
    QStringList parts;
    if (!added.isEmpty())
        parts << u"added: %1%2"_s.arg(added.mid(0, 5).join(u", "_s), added.size() > 5 ? u" …"_s : QString());
    if (!gone.isEmpty())
        parts << u"taken away: %1%2"_s.arg(gone.mid(0, 5).join(u", "_s), gone.size() > 5 ? u" …"_s : QString());
    scan_lbl->setText(u"Files of the mod changed while the editor runs — %1. Read them in: the mod is read again, your "
                      u"unapplied edits are kept and put back on top."_s.arg(parts.join(u"; "_s)));
    scan_bar->show();
}

// Read the mod's files again without losing an edit (the pending ones are kept and put back on top).
void MainWindow::rescan_mod()
{
    scan_bar->hide();
    const long long n = doc ? doc->pending_count() : 0;
    if (n && !keep_edits_over_reload(u"Kept over reading the mod's files again."_s)) {
        message(u"⚠ The unapplied edits could not be kept — the mod was not read again."_s, 12000);
        return;
    }
    reload_note = u"The mod's files were read again%1."_s.arg(
        n ? u" — your %1 unapplied edit(s) were kept and put back"_s.arg(n) : QString());
    _reload_mod();
}

void MainWindow::_offer_recovery()
{
    if (!_rec_offer) {
        rec_bar->hide();
        return;
    }
    const auto rec = recovery::load(*_rec_offer);
    if (!rec) {
        rec_bar->hide();
        return;
    }
    if (rec->auto_) {                            // kept over a re-read of the mod (undo of an Apply, rescan …)
        _recovery_restore();
        return;
    }
    QStringList parts;
    for (const auto &[_u, w] : recovery::units(rec->picture))
        parts << w;
    rec_lbl->setText(u"Unapplied edits from %1 were kept (%2 change%3: %4%5). They are NOT in the files yet."_s
                         .arg(rec->time.isEmpty() ? u"?"_s : rec->time)
                         .arg(rec->count)
                         .arg(rec->count == 1 ? u""_s : u"s"_s, parts.mid(0, 5).join(u", "_s),
                              parts.size() > 5 ? u" …"_s : u""_s));
    rec_bar->show();
}

void MainWindow::_recovery_restore()
{
    const auto rec = _rec_offer ? recovery::load(*_rec_offer) : std::nullopt;
    rec_bar->hide();
    if (!rec)
        return;
    try {
        recovery::put_back(*doc, rec->picture, u"Unapplied edits put back (%1)"_s.arg(rec->time));
    } catch (const std::exception &e) {
        message(u"⚠ The unapplied edits could not be put back: %1 — the file is kept: %2"_s.arg(QString::fromUtf8(e.what()),
                                                                                                *_rec_offer),
                0);
        return;
    }
    const QString p = *_rec_offer;
    _rec_offer.reset();
    recovery::keep_aside(p);                      // (put back: the edits are pending again — and in the next file)
    _rec_sig.reset();
    _recovery_tick();
    refresh_panel();
    render_map();
    update_pending();
    message(u"%1 unapplied change(s) put back — pending, not written yet (Ctrl+Z takes them out again).%2"_s
                .arg(rec->count)
                .arg(rec->note.isEmpty() ? u""_s : u" "_s + rec->note),
            15000);
}

void MainWindow::_recovery_discard()
{
    rec_bar->hide();
    if (_rec_offer) {
        recovery::keep_aside(*_rec_offer);
        message(u"The kept edits were put aside (not deleted): %1"_s.arg(pypath::dirname(*_rec_offer)), 10000);
    }
    _rec_offer.reset();
}

// ---------------------------------------------------------------------------------------------- close / revert / Apply
bool MainWindow::confirm_discard(const QString &what)
{
    // no dialog: with unsaved edits the first try only says so (in red); doing the same again within 10 s discards
    const long long n = (doc ? doc->pending_count() : 0) + (edit_panel && edit_panel->pending_live() ? 1 : 0);
    if (n == 0)
        return true;
    static QElapsedTimer clock;
    if (!clock.isValid())
        clock.start();
    const double now = double(clock.elapsed()) / 1000.0;
    if (_discard_armed.first == what && now - _discard_armed.second < 10) {
        _discard_armed = {QString(), 0.0};
        return true;
    }
    _discard_armed = {what, now};
    const QString again = what == u"close" ? u"close the window again"_s
                          : what == u"scenario" ? u"pick the scenario again"_s
                          : what == u"mod"      ? u"switch the mod again"_s
                                                : u"do it again"_s;
    message(u"⚠ %1 unsaved edit(s): Apply them first (Ctrl+S), or %2 within 10 s to discard them."_s.arg(n).arg(again),
            10000);
    notice->setText(u"%1 change%2 NOT applied — the editor is still open.  To throw %3 away: %4 within 10 seconds.  "
                    u"To keep %3: Apply (Ctrl+S)."_s.arg(n)
                        .arg(n == 1 ? u" is"_s : u"s are"_s, n == 1 ? u"it"_s : u"them"_s, again));
    notice->show();
    _notice_timer->start(10000);
    return false;
}

void MainWindow::closeEvent(QCloseEvent *ev)
{
    if (switch_requested || confirm_discard(u"close"_s)) {
        _submod_sync();                           // (what a page wrote without an Apply: a new start date …)
        if (doc && !_rec_path.isEmpty() && !reload_requested && !switch_requested)
            _recovery_save();                     // the unapplied edits: offered back on the next start
        ev->accept();
    } else {
        ev->ignore();
    }
}

void MainWindow::revert()
{
    if (doc->pending_count() == 0)
        return;
    auto disarm = [this] {
        _revert_armed = false;
        revert_btn->setText(u"Revert all"_s);
        revert_btn->setObjectName(u""_s);
        revert_btn->style()->unpolish(revert_btn);
        revert_btn->style()->polish(revert_btn);
    };
    if (!_revert_armed) {
        // no dialog: the button asks once more (it can't be undone)
        _revert_armed = true;
        revert_btn->setText(u"Click again: discard %1 edits"_s.arg(doc->pending_count()));
        revert_btn->setObjectName(u"confirm"_s);
        revert_btn->style()->unpolish(revert_btn);
        revert_btn->style()->polish(revert_btn);
        QTimer::singleShot(4000, this, [this, disarm] {
            if (_revert_armed)
                disarm();
        });
        return;
    }
    disarm();
    try {                                          // (thrown away on purpose — but kept aside, never deleted)
        _recovery_save();
        recovery::keep_aside(_rec_path);
    } catch (const std::exception &) {
    }
    for (const auto &[rid, order] : doc->region_orig)
        if (moddata::Region *r = mod->regions.get(rid))
            r->provinces = order;
    load_scenario(doc->scenario);
    message(u"Reverted to the files on disk (what was thrown away is kept aside in backups/recovery)"_s);
}

void MainWindow::_revert_inside()
{
    for (const auto &[rid, order] : doc->region_orig)
        if (moddata::Region *r = mod->regions.get(rid))
            r->provinces = order;
    _rec_internal = true;
    struct Reset {                                 // (try / finally)
        bool &b;
        ~Reset() { b = false; }
    } reset{_rec_internal};
    load_scenario(doc->scenario);
}

// Apply only these parts (recovery.units): the rest is taken out, the chosen part written, the rest put back on top
// — pending again. (The undo list starts anew: the steps can't be split by file.)
bool MainWindow::apply_only(const QSet<QString> &keep_units)
{
    const auto pic = recovery::picture(*doc);
    if (!pic)
        return false;
    QSet<QString> every;
    for (const auto &[u, _w] : recovery::units(pic))
        every.insert(u);
    const auto mine = recovery::cut(pic, keep_units), rest = recovery::cut(pic, every - keep_units);
    if (!mine)
        return false;
    _recovery_save();                              // (all of it on disk first: nothing lost if anything fails)
    _revert_inside();
    QStringList ks(keep_units.begin(), keep_units.end());
    ks.sort();
    recovery::put_back(*doc, mine, u"To apply: "_s + ks.join(u", "_s).left(80));   // (the names cut at 80, not the line)
    const bool ok = save();
    if (rest)
        recovery::put_back(*doc, rest, u"Still pending (not applied)"_s);
    _rec_sig.reset();
    _recovery_tick();
    refresh_panel();
    render_map();
    update_pending();
    return ok;
}

// Throw away only these parts (kept aside in backups/recovery, never deleted); the rest stays pending.
bool MainWindow::revert_only(const QSet<QString> &drop_units)
{
    const auto pic = recovery::picture(*doc);
    if (!pic)
        return false;
    QSet<QString> every;
    for (const auto &[u, _w] : recovery::units(pic))
        every.insert(u);
    const auto rest = recovery::cut(pic, every - drop_units);
    _recovery_save();
    recovery::keep_aside(_rec_path);               // (what is thrown away: kept aside)
    _revert_inside();
    if (rest)
        recovery::put_back(*doc, rest, u"Still pending (the rest reverted)"_s);
    _rec_sig.reset();
    _recovery_tick();
    refresh_panel();
    render_map();
    update_pending();
    QStringList ds(drop_units.begin(), drop_units.end());
    ds.sort();
    message(u"Reverted: %1 — the rest is still pending. (What was thrown away is kept aside in backups/recovery.)"_s.arg(
                ds.join(u", "_s)),
            12000);
    return true;
}

// The Apply button's tooltip: what Apply will write — the same lines the changelog gets.
QString MainWindow::_apply_preview()
{
    pyjson::Array changes;
    try {
        changes = _changelog_pending();
    } catch (const std::exception &) {
        changes.clear();
    }
    const long long n = doc ? doc->pending_count() : 0;
    if (!n)
        return u"Apply (Ctrl+S): nothing to write"_s;
    QStringList lines;
    for (const auto &c : changes)
        if (const pyjson::Value *t = c.get(u"text"_s); t && !t->str().isEmpty())
            lines << t->str();
    qsizetype back = 0;
    for (const QString &t : lines)
        back += t.startsWith(u"Undone: "_s);
    const QString head = u"Apply (Ctrl+S) writes %1 pending edit%2%3:"_s.arg(n).arg(n == 1 ? u""_s : u"s"_s).arg(
        back ? u" — %1 of them take back changes already in the files"_s.arg(back) : QString());
    QStringList shown;
    for (const QString &t : lines.mid(0, 25))
        shown << u"• "_s + t;
    return head + u'\n' + shown.join(u'\n') +
           (lines.size() > 25 ? u"\n… %1 more"_s.arg(lines.size() - 25) : QString()) +
           u"\n\nA copy of every file it changes is kept: ↶ Apply puts them back."_s;
}

bool MainWindow::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == mod_name_edit && e->type() == QEvent::MouseButtonDblClick && mod_name_edit->isReadOnly()) {
        begin_rename();                              // (a double click on the name: it becomes writable)
        return true;
    }
    if (obj == mod_name_edit) {
        auto *k = dynamic_cast<QKeyEvent *>(e);
        if (k && k->key() == Qt::Key_Escape) {
            if (e->type() == QEvent::ShortcutOverride)   // (Esc is the window's shortcut otherwise)
                e->accept();
            else if (e->type() == QEvent::KeyPress)
                end_rename(false);
            return e->type() == QEvent::KeyPress || e->type() == QEvent::ShortcutOverride;
        }
    }
    if (e->type() == QEvent::ToolTip) {
        if (obj == apply_btn) {
            apply_btn->setToolTip(_apply_preview());
        } else if (obj == _undo_btn || obj == _redo_btn) {
            doc->flush_live();
            _refresh_undo_tips();
        }
    }
    return QMainWindow::eventFilter(obj, e);
}

// The pending edits by file / group, each with Apply ▸ and Revert (a panel under the Apply button).
void MainWindow::open_pending_panel()
{
    const auto pic = recovery::picture(*doc);
    const auto us = recovery::units(pic);
    auto *panel = new QFrame(this, Qt::Popup);
    panel->setObjectName(u"pendingPanel"_s);
    panel->setStyleSheet(u"QFrame#pendingPanel { background: %1; border: 2px solid %2; border-radius: 6px; }"_s.arg(
        theme::PANEL, theme::GOLD_DIM));
    auto *v = new QVBoxLayout(panel);
    v->setContentsMargins(12, 10, 12, 10);
    const long long n = doc->pending_count();
    auto *head = new QLabel(u"Pending edits by file — %1 change%2"_s.arg(n).arg(n == 1 ? u""_s : u"s"_s));
    head->setStyleSheet(u"color: %1; font-weight: 700; font-size: 14px;"_s.arg(theme::GOLD));
    v->addWidget(head);
    auto *note = new QLabel(u"Apply or revert one file at a time. A failed file stays pending; the others go on. "
                            u"(Doing it by file starts the undo list anew.)"_s);
    note->setWordWrap(true);
    note->setStyleSheet(u"color: %1;"_s.arg(theme::INK_DIM));
    v->addWidget(note);
    auto *host = new QWidget();
    auto *hl = new QVBoxLayout(host);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(4);
    QSet<QString> fails;
    for (const auto &[k, _m] : doc->apply_failures)
        fails.insert(k);
    for (const auto &[u, words] : us) {
        auto *row = new QHBoxLayout();
        const qsizetype colon = u.indexOf(u':');
        const QString tail = colon >= 0 ? u.mid(colon + 1) : u;     // (u.split(":", 1)[-1])
        const bool bad = fails.contains(tail);
        auto *lab = new QLabel((bad ? u"⚠ "_s : QString()) + words);
        lab->setStyleSheet(u"color: %1;"_s.arg(bad ? theme::ERROR_ : theme::INK));
        QString tip = u;
        for (const auto &[k, m] : doc->apply_failures)
            if (k == tail) {
                tip = u"Last Apply failed: "_s + m;
                break;
            }
        lab->setToolTip(tip);
        row->addWidget(lab, 1);
        auto *a = new QPushButton(u"Apply ▸"_s);
        const QString uu = u;
        connect(a, &QPushButton::clicked, this, [this, panel, uu] {
            panel->close();
            apply_only({uu});
        });
        auto *r = new QPushButton(u"Revert"_s);
        connect(r, &QPushButton::clicked, this, [this, panel, uu] {
            panel->close();
            revert_only({uu});
        });
        row->addWidget(a);
        row->addWidget(r);
        hl->addLayout(row);
    }
    if (us.empty())
        hl->addWidget(new QLabel(u"Nothing pending."_s));
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setWidget(host);
    sc->setMaximumHeight(420);
    v->addWidget(sc);
    panel->setMinimumWidth(520);
    panel->adjustSize();
    const QPoint pt = apply_btn->mapToGlobal(apply_btn->rect().topRight());
    panel->move(pt.x() - panel->width(), pt.y() - panel->height() - 6);
    panel->show();
    _pending_panel = panel;                        // (parented to the window, as Python's: an old one stays hidden)
}

// What Apply would write, for the changelog: the script files' changes and the edit steps since the last Apply.
pyjson::Array MainWindow::_changelog_pending()
{
    scriptstore::ScriptStore *store =
        doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    localisation::LocEdits *lc = doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    changelog::Loc loc;
    if (lc)
        loc = [lc](const QString &k) { return lc->text(k); };
    else
        loc = [this](const QString &k) -> OptStr {
            const QString *t = mod->loc.get(k);
            return t ? OptStr(*t) : std::nullopt;
        };
    pyjson::Array out = store ? changelog::script_changes(*store, loc) : pyjson::Array{};
    auto &info = _cl_edits();
    std::vector<std::tuple<QString, QString, bool>> steps;
    for (const auto &e : doc->undo_stack) {
        const auto it = info.find(e.get());
        const ClInfo *i = it != info.end() ? &*it : nullptr;
        if (i && i->applied)
            continue;
        if (!_step_matters(*e))                   // made and taken back again: nothing to log
            continue;
        if (_event_step(*e))                      // events / decisions are listed by id (added / edited / deleted),
            continue;                             // not step by step
        steps.emplace_back(e->label, i ? i->tab : tab, i ? i->script : false);
    }
    for (const auto &e : doc->redo_stack) {       // applied, then undone: Apply would write the undo —
        const auto it = info.find(e.get());       // every one listed, events too (a revert is never silent)
        if (it != info.end() && it->applied && _step_matters(*e))
            steps.emplace_back(u"Undone: "_s + e->label, it->tab, it->script);
    }
    QSet<QString> added;
    for (const pyjson::Value &c : out)
        if (const pyjson::Value *a = c.get(u"added"_s); a && a->truthy()) {
            const pyjson::Value *k = c.get(u"key"_s);
            added.insert(k ? k->str() : QString());
        }
    static const QString named = u"^Name (%1+): \\1 → "_s.arg(py::PY_NS);
    pyjson::Array rest;                           // (naming a brand-new thing is part of "Added …")
    for (const pyjson::Value &s : changelog::edit_steps(steps)) {
        const auto m = py::re_match(named, s.get(u"text"_s)->str());
        const bool drop = m.hasMatch() && std::any_of(added.begin(), added.end(), [&](const QString &k) {
                              return !k.isEmpty() && m.captured(1).startsWith(k);
                          });
        if (!drop)
            rest.push_back(s);
    }
    // the texts of events and decisions: no line each — the event / decision counts as edited
    QSet<QString> listed;
    for (const pyjson::Value &c : out) {
        const pyjson::Value *r = c.get(u"rel"_s);
        const QString rel = r ? r->str() : QString();
        if (rel.startsWith(u"events/") || rel.startsWith(u"decisions/")) {
            const pyjson::Value *k = c.get(u"key"_s);
            listed.insert(k ? k->str() : u"None"_s);
        }
    }
    static const QString ev_text = u"^Name (?:EVT(?:NAME|DESC|OPT[A-Z]?)(\\d+)|(\\w+?)_(?:title|desc)): "_s;
    std::optional<OrderedMap<QString, std::vector<std::pair<QString, qsizetype>>>> dec_keys;
    pyjson::Array keep;
    OrderedMap<std::pair<QString, QString>, bool> texts;
    for (const pyjson::Value &s : rest) {
        const auto m = py::re_match(ev_text, s.get(u"text"_s)->str());
        if (m.hasMatch() && m.capturedLength(1) > 0) {
            texts[{u"event"_s, m.captured(1)}];
            continue;
        }
        if (m.hasMatch() && m.capturedLength(2) > 0) {
            if (!dec_keys)
                dec_keys = store ? eventtools::decision_keys(*mod, *store)
                                 : OrderedMap<QString, std::vector<std::pair<QString, qsizetype>>>{};
            if (dec_keys->contains(m.captured(2))) {
                texts[{u"decision"_s, m.captured(2)}];
                continue;
            }
        }
        keep.push_back(s);
    }
    for (const auto &[kk, _v] : texts) {
        const auto &[kind, key] = kk;
        if (listed.contains(key))
            continue;
        const bool ev = kind == u"event";
        const OptStr nm = lc ? lc->text(ev ? u"EVTNAME"_s + key : key + u"_title"_s) : std::nullopt;
        const QString name = nm && !nm->isEmpty() ? u" “%1”"_s.arg(*nm) : (ev ? QString() : u"“%1”"_s.arg(key));
        out.push_back(pyjson::Value(pyjson::Object{
            {u"text"_s, u"%1 %2%3 edited (its texts)"_s.arg(ev ? u"Event"_s : u"Decision"_s, ev ? key : QString(), name)},
            {u"section"_s, ev ? u"Events"_s : u"Decisions"_s}, {u"src"_s, u"edit"_s}}));
    }
    for (const pyjson::Value &s : keep)
        out.push_back(s);
    return out;
}

// the keys whose pictures differ between two keyed pictures (Python: {r for r in set(b) | set(a) if b.get(r) != a.get(r)})
static QSet<QString> changed_keys(const store::ItemsSnap &b, const store::ItemsSnap &a)
{
    QSet<QString> out;
    for (const auto &[k, v] : b.items) {
        const store::Snap *w = a.items.get(k);
        if (!w || !store::same(v, *w))
            out.insert(k.str());
    }
    for (const auto &[k, v] : a.items)
        if (!b.items.contains(k))
            out.insert(k.str());
    return out;
}

// The step changed nothing but event / decision files (the block builder, the script editor's tools).
bool MainWindow::_event_step(const document::Step &entry) const
{
    const document::Picture &before = entry.before, &after = entry.after;
    const store::Snap *bs = before.lists.get(u"scripts"_s), *as = after.lists.get(u"scripts"_s);
    const auto *b = bs ? dynamic_cast<const store::ItemsSnap *>(bs->get()) : nullptr;
    const auto *a = as ? dynamic_cast<const store::ItemsSnap *>(as->get()) : nullptr;
    if (!b || !a || b->pairs || a->pairs)
        return false;
    for (const auto &[k, v] : before.lists) {
        if (k == u"scripts")
            continue;
        const store::Snap *w = after.lists.get(k);
        if (!w || !store::same(v, *w))
            return false;
    }
    if (!before.pops.isEmpty() || !before.regs.isEmpty() || !before.provs.isEmpty())
        return false;
    const QSet<QString> rels = changed_keys(*b, *a);
    return !rels.isEmpty() && std::all_of(rels.begin(), rels.end(), [](const QString &r) {
        return r.startsWith(u"events/") || r.startsWith(u"decisions/");
    });
}

// An edit step still shows in what Apply would write (it wasn't undone by hand afterwards).
bool MainWindow::_step_matters(const document::Step &entry)
{
    const document::Picture &before = entry.before, &after = entry.after;
    {
        const auto dp = doc->dirty_provinces();
        const QSet<long long> dps(dp.begin(), dp.end());
        for (const auto &kv : before.pops)
            if (dps.contains(kv.first))
                return true;
        const QStringList dr = doc->dirty_regions();
        for (const auto &kv : before.regs)
            if (dr.contains(kv.first))
                return true;
        const auto dh = doc->dirty_histories();
        const QSet<long long> dhs(dh.begin(), dh.end());
        for (const auto &kv : before.provs)
            if (dhs.contains(kv.first))
                return true;
    }
    for (const auto &[k, bsnap] : before.lists) {
        const store::StoreP *mf = doc->maps.get(k);
        const QStringList dl = mf ? (*mf)->dirty() : QStringList();
        if (dl.isEmpty())
            continue;
        const QSet<QString> dirty(dl.begin(), dl.end());
        const store::Snap *asnap = after.lists.get(k);
        const auto *b = dynamic_cast<const store::ItemsSnap *>(bsnap.get());
        const auto *a = asnap ? dynamic_cast<const store::ItemsSnap *>(asnap->get()) : nullptr;
        if (k == u"scripts" && b && a && !b->pairs && !a->pairs) {
            if (changed_keys(*b, *a).intersects(dirty))
                return true;
        } else if (k == u"loc") {
            if (!b || !a)                         // (Python: dict(None) raises — and an error counts as "matters")
                return true;
            if (changed_keys(*b, *a).intersects(dirty))
                return true;
        } else {
            return true;
        }
    }
    return false;
}

// {undo step: [step, the tab it was made in, it changed script files, already applied]}
QHash<const document::Step *, MainWindow::ClInfo> &MainWindow::_cl_edits()
{
    if (_cl_doc != doc.get()) {                   // another scenario: its own undo list
        _cl_doc = doc.get();
        _cl_info.clear();
        _cl_script = false;
        if (doc->maps.contains(u"scripts"_s))
            doc->map<scriptstore::ScriptStore>(u"scripts"_s).listeners.push_back([this](const QString &) {
                _cl_script = true;
            });
    }
    return _cl_info;
}

// After every change of the document: a new undo step remembers the tab (the editor function) it came from — the
// changelog's category.
void MainWindow::_cl_note()
{
    auto &info = _cl_edits();
    const auto &st = doc->undo_stack;
    if (!st.empty() && !info.contains(st.back().get())) {
        info.insert(st.back().get(), ClInfo{st.back(), tab, _cl_script, false});
        if (info.size() > 900) {                  // steps that fell off the undo list
            QSet<const document::Step *> alive;
            for (const auto &e : st)
                alive.insert(e.get());
            for (const auto &e : doc->redo_stack)
                alive.insert(e.get());
            for (auto it = info.begin(); it != info.end();)
                it = alive.contains(it.key()) ? std::next(it) : info.erase(it);
        }
    }
    _cl_script = false;
}

// The labels of the steps that are in the files but undone: Apply would take them back.
QStringList MainWindow::_applied_undone()
{
    auto &info = _cl_edits();
    QStringList out;
    for (const auto &e : doc->redo_stack) {
        const auto it = info.find(e.get());
        if (it != info.end() && it->applied && _step_matters(*e))
            out << e->label;
    }
    return out;
}

bool MainWindow::save(bool reload)
{
    _flush_held_edits();                          // (C++: a slider still resting is written too)
    doc->flush_live();                            // typing not reported yet: a step of its own (it is in the log)
    if (draw_panel && draw_panel->clip.floating()) // (C++ only) pasted provinces still floating: placed first, where
        message(draw_panel->clip.place(), 12000);  // they float — a cut map would leave land provinces without land
    _cl_note();
    if (doc->pending_count() == 0) {
        message(u"Nothing to save"_s);
        return true;
    }
    pyjson::Array changes;
    try {
        changes = _changelog_pending();
    } catch (const std::exception &) {            // the changelog never stops an Apply
        changes.clear();
    }
    const QStringList back = _applied_undone();
    if (back.size() > APPLY_CONFIRM && _apply_armed != back.size()) {
        _apply_armed = back.size();               // asks once: Apply again within 10 s writes
        QTimer::singleShot(10000, this, [this] {
            if (_apply_armed && *_apply_armed) {
                _apply_armed.reset();
                notice->hide();
            }
        });
        notice->setText(u"Apply would take back %1 changes that are already in the files: %2%3.  Apply again within 10 "
                        u"seconds to write it — or Redo (Ctrl+Y) first to keep them."_s.arg(back.size())
                            .arg(back.mid(0, 6).join(u"; "_s), back.size() > 6 ? u" …"_s : QString()));
        notice->show();
        message(u"⚠ Apply would take back %1 applied changes — Apply again to confirm."_s.arg(back.size()), 10000);
        return false;
    }
    _apply_armed.reset();
    notice->hide();
    const auto todo = doc->maps.contains(u"definitions"_s) ? draw_panel->unfinished()
                                                           : std::vector<std::pair<long long, QString>>{};
    // factories over the state rules are listed in red in the Provinces and Errors tabs: Apply removes them
    const auto problems = doc->factory_problems(doc->session_rids());
    if (!problems.empty()) {
        doc->remove_factories(problems);
        try {
            changes = _changelog_pending();
        } catch (const std::exception &) {
        }
    }
    const long long n = doc->pending_count();
    QStringList written;
    OptStr bdir;
    std::optional<pyjson::Value> man;
    // the files this Apply adds / takes away itself are no news for the "changed while the editor runs" check: they
    // go into its listing, while a change from outside still shows (C++ only, 2026-11-14: the Python editor showed
    // its own first copy of localisation/text.csv — or a new nation's history and flags — as added from outside)
    const std::set<QString> listed_before = _mod_listing();
    auto own_listing = [this, &listed_before] {
        if (!_listing)
            return;
        const std::set<QString> after = _mod_listing();
        std::set<QString> base = *_listing;
        for (const QString &f : after)
            if (!listed_before.contains(f))
                base.insert(f);
        for (const QString &f : listed_before)
            if (!after.contains(f))
                base.erase(f);
        _listing = std::move(base);
    };
    try {
        journal->record(u"Apply"_s, doc->scenario,
                        [&](applyjournal::Recorder &) { std::tie(written, bdir) = doc->save(backup_root); }, &man);
        script_ws->refresh_titles();
        script_ws->rehighlight_all();             // (the colours follow the texts as written)
    } catch (const std::exception &e) {
        own_listing();
        doc->last_error = QString::fromUtf8(e.what());
        _submod_sync();                           // (what was written before it failed)
        if (man && man->truthy()) {
            try {
                cl_ws->record({changelog::Value(pyjson::Object{
                                  {u"text"_s, u"Apply FAILED (%1) after writing %2 — ↶ Apply puts them back"_s.arg(
                                                  QString::fromUtf8(e.what()), applyjournal::describe(*man))},
                                  {u"section"_s, u"Apply"_s}, {u"src"_s, u"apply"_s}})},
                              doc->scenario);
            } catch (const std::exception &) {
            }
        }
        _refresh_apply_undo();
        _rec_sig.reset();
        _recovery_save();                         // (the edits are still pending — and kept on disk too)
        message(u"⚠ Apply failed: %1 — see the Errors tab. Your edits are still pending (and kept in "
                u"backups/recovery)."_s.arg(QString::fromUtf8(e.what())),
                0);
        chk_panel->refresh(doc.get(), true);
        return false;
    }
    own_listing();
    const auto fails = doc->apply_failures;
    const long long left = doc->pending_count();
    _rec_sig.reset();
    _recovery_save();                             // (what is left pending: kept; nothing left: the file goes)
    if (!fails.empty()) {
        QStringList f;
        for (const auto &[a, b] : fails)
            f << u"%1: %2"_s.arg(a, b);
        doc->last_error = u"Applied %1 edit(s); %2 still pending — not written: %3"_s.arg(n - left).arg(left).arg(f.join(u"; "_s));
    } else {
        doc->last_error.reset();
    }
    QSet<const document::Step *> done;            // what is in the files now: the steps not undone
    for (const auto &e : doc->undo_stack)
        done.insert(e.get());
    for (auto it = _cl_edits().begin(); it != _cl_edits().end(); ++it)
        it->applied = done.contains(it.key());
    QString summary = u"Apply: no file changed"_s;
    if (man && man->truthy()) {
        summary = u"Apply: %1 written (a copy of each as it was is kept: ↶ Apply puts them back)"_s.arg(
            applyjournal::describe(*man));
        const pyjson::Value *ut = man->get(u"untracked"_s);
        if (ut && ut->truthy()) {
            QStringList u;
            for (const pyjson::Value &x : ut->arr())
                if (u.size() < 6)
                    u << x.str();
            summary += u" — ⚠ also changed without a copy: %1"_s.arg(u.join(u", "_s));
        }
    }
    changes.push_back(pyjson::Value(pyjson::Object{{u"text"_s, summary}, {u"section"_s, u"Apply"_s}, {u"src"_s, u"apply"_s}}));
    cl_ws->record(changes, doc->scenario);
    _refresh_apply_undo();
    _sync_nation_colors(true);
    refresh_scenarios();
    refresh_panel();
    _refresh_windows();
    render_map();
    QString extra = _submod_sync();
    if (man && man->truthy())                     // (something was written: the almanac follows)
        extra += _almanac_after_apply();
    const bool sub_warn = extra.contains(u"⚠"_s);
    if (!problems.empty())
        extra += u" Removed %1 factor%2 over the state rules."_s.arg(problems.size()).arg(problems.size() == 1 ? u"y"_s : u"ies"_s);
    if (!todo.empty())
        extra += u" ⚠ Saved %1 new province%2 that %3 not finished (see the Errors tab)."_s.arg(
            QString::number(todo.size()), todo.size() == 1 ? u""_s : u"s"_s, todo.size() == 1 ? u"is"_s : u"are"_s);
    const auto &[lost, kinds] = doc->undo_lost;
    if (lost) {                                   // what Undo can't take back once it is written
        static const std::map<QString, QString> names = {
            {u"oob"_s, u"armies"_s}, {u"techs"_s, u"technologies"_s}, {u"production"_s, u"production"_s},
            {u"diplomacy"_s, u"diplomacy"_s}, {u"new_nations"_s, u"new nations"_s}, {u"assets"_s, u"added files"_s},
            {u"flags"_s, u"flags"_s}, {u"definitions"_s, u"new / deleted provinces"_s},
            {u"hist_new"_s, u"new history files"_s}};
        QStringList ks;
        for (const QString &k : kinds)
            ks << (names.contains(k) ? names.at(k) : k);
        extra += u" Undo can't go back past this Apply for %1 (%2 earlier step%3)."_s.arg(ks.join(u", "_s)).arg(lost).arg(
            lost == 1 ? u""_s : u"s"_s);
    } else if (!doc->undo_stack.empty()) {
        extra += u" Undo still works: undoing an applied change makes it a pending change again."_s;
    }
    if (!fails.empty()) {                          // X applied, Y failed — the failed ones stay pending
        QStringList f;
        for (const auto &[a, b] : fails)
            f << u"%1 — %2"_s.arg(a, b);
        message(u"⚠ %1 applied, %2 failed (kept pending): %3"_s.arg(n - left).arg(fails.size()).arg(f.join(u"; "_s)), 0,
                true);
        chk_panel->refresh(doc.get(), true);
    } else {
        message(u"Saved %1 edit(s) to %2 file(s). Backups in %3.%4"_s.arg(n)
                    .arg(written.size())
                    .arg(bdir && !bdir->isEmpty() ? pypath::relpath(*bdir, backup_root) : u"—"_s, extra),
                15000, !todo.empty() || sub_warn);
    }
    schedule_checks();
    if (!fails.empty())
        return false;
    if (!mod->free_slots.isEmpty() || !mod->free_heirs.isEmpty())
        return close_number_gaps(reload);         // provinces were deleted: numbers and scripts follow
    return true;
}

QString MainWindow::_almanac_after_apply()
{
    if (!almanac_cb || !almanac_cb->isChecked())
        return QString();
    const QString folder = mod->submod ? mod->submod->root : mod->root;   // (a submod: its real folder)
    const auto [path, err] = moreviews::write_almanac(*mod, doc.get(), folder);
    return err.isEmpty() ? u" Almanac: %1."_s.arg(pypath::basename(path))
                         : u" ⚠ The almanac was not written: %1."_s.arg(err);
}

bool MainWindow::close_number_gaps(bool reload)
{
    // A deleted province's number can't stay empty (to the game it is a land province without a pixel: it crashes)
    // and the numbers can't have a gap: the highest provinces take the freed numbers, in every file — written at
    // once, each file backed up. Then the mod is read again (the numbers in memory are the old ones).
    if (doc->pending_count()) {
        message(u"⚠ Apply (Ctrl+S) first — the numbers are closed up in the files."_s, 10000, true);
        return false;
    }
    std::optional<provrenumber::Job> job;
    QStringList written;
    QString bdir;
    try {
        job = provrenumber::plan(*mod);
        if (!job)
            return true;
        journal->record(u"Province numbers closed up"_s, doc->scenario,   // (undoable as well)
                        [&](applyjournal::Recorder &) { std::tie(written, bdir) = provrenumber::apply(*mod, *job, backup_root); });
    } catch (const std::exception &e) {
        doc->last_error = u"The province numbers could not be closed up (%1) — nothing was renumbered. The Errors tab "
                          u"lists the unused numbers; the game crashes on them."_s.arg(QString::fromUtf8(e.what()));
        message(u"⚠ "_s + *doc->last_error, 0, true);
        chk_panel->refresh(doc.get(), true);
        return false;
    }
    reload_note = u"%1 %2 files written; the old ones are in %3."_s.arg(
        job->summary(), QString::number(written.size()), pypath::relpath(bdir, backup_root));
    renumbered = job->moves;
    message(reload_note + (reload ? u" Reading the mod again…"_s : u""_s), 20000, !job->left.empty());
    if (reload)
        _reload_mod();
    return true;
}

}  // namespace mainwindow
