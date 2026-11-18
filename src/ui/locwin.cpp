// locwin.py — GameLook and its helpers (see locwin.h).
#include "ui/locwin.h"
#include "core/document.h"
#include "core/localisation.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QComboBox>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QRectF>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSplitter>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

using namespace Qt::StringLiterals;

namespace locwin {

bool NUM(const QString &s)
{
    static const QRegularExpression re(uR"(^[+-]?(\d+\.?\d*|\.\d+)$)"_s, QRegularExpression::UseUnicodePropertiesOption);
    return re.match(s).hasMatch();
}

OptStr _val(const script::Node &node, const QString &name)
{
    for (const script::Node &c : node.kids)
        if (c.has_key && !c.key.isEmpty() && py::lower(c.key) == name && !c.is_block)
            return c.value;
    return std::nullopt;
}

const script::Node *_kid(const script::Node &node, const QString &name)
{
    for (const script::Node &c : node.kids)
        if (c.has_key && !c.key.isEmpty() && py::lower(c.key) == name)
            return &c;
    return nullptr;
}

XY _xy(const script::Node &node, const QString &name)
{
    const script::Node *b = _kid(node, name);
    if (!b || !b->is_block)
        return {0.0, 0.0};
    std::map<QString, double> d;
    for (const script::Node &c : b->kids)
        if (c.has_key && !c.key.isEmpty() && !c.is_block && NUM(c.value))
            d[py::lower(c.key)] = c.value.toDouble();
    const auto get = [&d](const QString &k) {
        const auto it = d.find(k);
        return it != d.end() ? it->second : 0.0;
    };
    return {get(u"x"_s), get(u"y"_s)};
}

double _num(const script::Node &node, const QString &name, double dflt)
{
    const OptStr v = _val(node, name);
    return v && !v->isEmpty() && NUM(*v) ? v->toDouble() : dflt;
}

GameLook::GameLook(const moddata::Mod &mod, scriptstore::ScriptStore *store) : mod(mod), store(store), fonts(mod, store)
{
}

void GameLook::_load_sprites()
{
    sprites.emplace();
    files = std::make_unique<fourk::Files>(mod);
    std::vector<std::pair<QString, QString>> tree;
    for (const auto &[rel, full] : files->tree(u"interface"_s))
        tree.emplace_back(rel, full);
    for (const QString &g : more_gfx) {            // (C++ only: .gfx files the editor has not written yet)
        bool have = false;
        for (const auto &[rel, _f] : tree)
            have = have || py::lower(rel) == py::lower(g);
        if (!have)
            tree.emplace_back(g, QString());
    }
    std::sort(tree.begin(), tree.end());
    for (const auto &[rel, _full] : tree) {
        if (!rel.endsWith(u".gfx") || rel.count(u'/') != 1)
            continue;
        script::Nodes tops;
        try {
            tops = script::parse(store->text(rel));
        } catch (...) {
            continue;
        }
        for (const script::Node &top : tops)
            if (top.is_block && py::lower(top.has_key ? top.key : QString()) == u"spritetypes")
                for (const script::Node &sp : top.kids) {
                    if (!sp.is_block)
                        continue;
                    const QString name = py::lower(_val(sp, u"name"_s).value_or(QString()));
                    OptStr tex = _val(sp, u"texturefile"_s);
                    if (!tex || tex->isEmpty())
                        tex = _val(sp, u"texturefile1"_s);   // (a shield: its frame)
                    if (!name.isEmpty() && tex && !tex->isEmpty()) {
                        const long long frames = static_cast<long long>(_num(sp, u"noofframes"_s, 1));
                        (*sprites)[name] = {*tex, frames ? frames : 1};
                        kinds[name] = Kind{py::lower(sp.has_key ? sp.key : QString()),
                                           _kid(sp, u"size"_s) ? std::optional<XY>(_xy(sp, u"size"_s)) : std::nullopt,
                                           _kid(sp, u"bordersize"_s) ? std::optional<XY>(_xy(sp, u"bordersize"_s))
                                                                      : std::nullopt,
                                           _val(sp, u"texturefile2"_s).value_or(QString())};
                    }
                }
    }
}

std::optional<QImage> GameLook::picture(const QString &path)
{
    // a picture file as a QImage (cached) or None
    if (const auto it = _pix.find(path); it != _pix.end())
        return it->second;
    std::optional<QImage> img;
    if (!path.isEmpty() && QFileInfo(path).isFile()) {
        try {
            img = fourk::to_qimage(fourk::read_picture(path).first);
        } catch (...) {
            img.reset();
        }
    }
    _pix[path] = img;
    return img;
}

std::optional<QImage> GameLook::sprite(const OptStr &name)
{
    if (!sprites)
        _load_sprites();
    const auto it = sprites->find(py::lower(name.value_or(QString())));
    if (it == sprites->end())
        return std::nullopt;
    const auto &[tex, frames] = it->second;
    std::optional<QImage> img;
    if (!fresh.empty()) {                          // (C++ only: a picture the editor has not written yet)
        QString k = py::lower(tex);
        k.replace(u'\\', u'/');
        while (k.contains(u"//"_s))
            k.replace(u"//"_s, u"/"_s);
        if (const auto f = fresh.find(k); f != fresh.end())
            img = f->second;
    }
    if (!img) {
        const auto hit = files->find(tex);
        img = hit ? picture(hit->second) : std::nullopt;
    }
    if (img && frames > 1)
        img = img->copy(0, 0, static_cast<int>(py::floordiv(static_cast<long long>(img->width()), frames)), img->height());
    return img;
}

std::optional<QImage> GameLook::bar(const OptStr &name, double full)
{
    if (!sprites)
        _load_sprites();
    const QString lname = py::lower(name.value_or(QString()));
    const auto kt = kinds.find(lname);
    if (kt == kinds.end() || kt->second.kind != u"progressbartype" || !kt->second.size)
        return std::nullopt;
    const int w = std::max(1, int(kt->second.size->first)), h = std::max(1, int(kt->second.size->second));
    const std::optional<QImage> one = sprite(name);
    std::optional<QImage> two;
    if (!kt->second.second.isEmpty()) {
        const auto hit = files->find(kt->second.second);
        two = hit ? picture(hit->second) : std::nullopt;
    }
    QImage o(w, h, QImage::Format_RGBA8888);
    o.fill(0);
    QPainter p(&o);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const double cut = std::clamp(full, 0.0, 1.0) * w;
    if (two && !two->isNull())
        p.drawImage(QRectF(0, 0, w, h), *two);
    else
        p.fillRect(QRectF(cut, 0, w - cut, h), QColor(170, 20, 16));
    p.setClipRect(QRectF(0, 0, cut, h));
    if (one && !one->isNull())
        p.drawImage(QRectF(0, 0, w, h), *one);
    else
        p.fillRect(QRectF(0, 0, cut, h), QColor(20, 128, 20));
    p.end();
    return o;
}

std::optional<XY> GameLook::stretches(const OptStr &name)
{
    // is this a sprite the game stretches to the size it is asked for (corners kept, the middle filled)? -> its own
    // size (w, h) or None
    if (!sprites)
        _load_sprites();
    const auto it = kinds.find(py::lower(name.value_or(QString())));
    if (it != kinds.end() && it->second.kind == u"corneredtilespritetype")
        return it->second.size ? *it->second.size : XY{0.0, 0.0};
    return std::nullopt;
}

std::optional<QImage> GameLook::sprite_fit(const OptStr &name, double w, double h)
{
    // a "cornered tile" sprite made w x h as the game does it: its four corners as they are, the edges and the middle
    // stretched between them
    const QString lname = py::lower(name.value_or(QString()));
    const auto key = std::tuple{lname, static_cast<long long>(w), static_cast<long long>(h)};
    if (const auto it = _fit.find(key); it != _fit.end())
        return it->second;
    const std::optional<QImage> img = sprite(name);
    const auto kt = kinds.find(lname);
    std::optional<QImage> out;
    if (img && kt != kinds.end() && w >= 2 && h >= 2) {
        const int wi = static_cast<int>(w), hi = static_cast<int>(h);
        auto [bx, by] = kt->second.border ? *kt->second.border : XY{img->width() / 3.0, img->height() / 3.0};
        bx = std::max(1.0, std::min({bx, img->width() / 2.0 - 1, wi / 2.0}));
        by = std::max(1.0, std::min({by, img->height() / 2.0 - 1, hi / 2.0}));
        QImage o(wi, hi, QImage::Format_RGBA8888);
        o.fill(0);
        QPainter p(&o);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const double sx[4] = {0.0, bx, img->width() - bx, double(img->width())};
        const double sy[4] = {0.0, by, img->height() - by, double(img->height())};
        const double dx[4] = {0.0, bx, wi - bx, double(wi)};
        const double dy[4] = {0.0, by, hi - by, double(hi)};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                if (dx[i + 1] > dx[i] && dy[j + 1] > dy[j])
                    p.drawImage(QRectF(dx[i], dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j]), *img,
                                QRectF(sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j]));
        p.end();
        out = o;
    }
    _fit[key] = out;
    return out;
}

const std::map<QString, const script::Node *> &GameLook::gui(const QString &rel)
{
    // {name (lower case): node} of every element of an interface file, at any depth
    if (const auto it = _gui.find(rel); it != _gui.end())
        return it->second.second;
    auto &slot = _gui[rel];
    try {
        slot.first = script::parse(store->text(rel));
    } catch (...) {
        slot.first.clear();
    }
    std::function<void(const script::Nodes &)> walk = [&](const script::Nodes &nodes) {
        for (const script::Node &n : nodes)
            if (n.is_block) {
                const OptStr nm = _val(n, u"name"_s);
                if (nm && !nm->isEmpty() && n.has_key && !n.key.isEmpty() && py::lower(n.key) != u"guitypes")
                    slot.second.emplace(py::lower(*nm), &n);   // (setdefault: the first one stays)
                walk(n.kids);
            }
    };
    walk(slot.first);
    return slot.second;
}

OptStr GameLook::find_picture(const QString &sub, const QString &name) const
{
    for (const QString ext : {u".tga"_s, u".dds"_s}) {
        const QString p = mod.p({u"gfx"_s, u"pictures"_s, sub, name + ext});
        if (QFileInfo(p).isFile())
            return p;
    }
    return std::nullopt;
}

// ================================================================================================ the preview
const OrderedMap<QString, QString> MEANING = {
    {u"COUNTRY"_s, u"the nation the event or decision is for"_s}, {u"COUNTRY_ADJ"_s, u"its adjective (British)"_s},
    {u"FROMCOUNTRY"_s, u"the nation that sent the event"_s}, {u"FROMCOUNTRY_ADJ"_s, u"that nation's adjective"_s},
    {u"CAPITAL"_s, u"the nation's capital"_s}, {u"PROVINCENAME"_s, u"the province of a province event"_s},
    {u"STATENAME"_s, u"the state of a province event"_s}, {u"CULTURE"_s, u"the culture concerned"_s},
    {u"UNION"_s, u"the cultural union nation"_s}, {u"UNION_ADJ"_s, u"the cultural union's adjective"_s},
    {u"MONARCHTITLE"_s, u"the ruler's title (King, President …)"_s}, {u"YEAR"_s, u"the year"_s}, {u"DATE"_s, u"the date"_s},
    {u"OURCOUNTRY"_s, u"the player's nation"_s}, {u"OURCOUNTRY_ADJ"_s, u"the player's nation's adjective"_s},
    {u"OURCAPITAL"_s, u"the player's capital"_s}, {u"ANYPROVINCE"_s, u"any province of the nation"_s},
    {u"CRISISTAKER"_s, u"the nation that takes up the crisis"_s}, {u"CRISISTARGET"_s, u"the nation the crisis is about"_s},
    {u"CRISISATTACKER"_s, u"the crisis' attacker"_s}, {u"CRISISDEFENDER"_s, u"the crisis' defender"_s},
    {u"CRISISAREA"_s, u"where the crisis is"_s},
    {u"NATIONALVALUE"_s, u"the nation's national value"_s}, {u"RELIGION"_s, u"the religion concerned"_s},
    {u"GOVERNMENT"_s, u"the form of government"_s},
};
const QStringList VARS = QString(
    u"ACTION ACTIVE ACTOR ADJ AGAINST AGRESSOR ALLOWED AMOUNT ANYPROVINCE ARMY_NAME ARMY ASTATE ATTACKER ATTUNIT "
    "AVG BAC BADBOY BADWORD BASE_PERCENTAGE BASE BAT BLD BON BONUS BRIG BUD BUILDING BUY CAP CAPITAL CASH CASUS CAT "
    "CATEGORY CB_TARGET_NAME_ADJ CB_TARGET_NAME CHAN CHANCE CHANGE COMMANDER CON CONSTRUCTION CONTINENTNAME COST COUNT "
    "COUNTRIES COUNTRY_ADJ COUNTRY COUNTRY1 COUNTRY2 COUNTRYADJ COUNTRYCULTURE COUNTRYNAME CREATOR CREDITS CRISISAREA "
    "CRISISATTACKER CRISISDEFENDER CRISISTAKER_ADJ CRISISTAKER CRISISTARGET_ADJ CRISISTARGET CUL CULTURE_GROUP_UNION "
    "CULTURE CULTUREGROUP CURR CURRENT D DATE DAY DAYS DEFENDER DEFUNIT DESC DEST DETAILS DIRECTION DIST EFFECT EFFECTS "
    "ENEMY EVENT EVENTDESC EXC EXP EXPLANATION FACTION FACTORY FIRST FOCUS FOLDER FOR FRACTION FRIEND FROM "
    "FROMCOUNTRY_ADJ FROMCOUNTRY FROMPROVINCE FROMRULER FUNDS GOAL GOOD GOODS GOV GOVERNMENT GOVT GP_ADJ GP GROUP HOME "
    "IDE IDEOLOGY IMPACT INCOME INDEP INF INFAMY INPUT INV INVENTION ISSUE LAW LEADER LEV LEVEL LEVELS LIM LIMIT LIST "
    "LITERACY LOC LOCAL LOCATION LORD LOSE LVL M MAX MEN MIL MILITANCY MIN MONARCHTITLE MONEY MONTH MONTHS MOVEMENT "
    "MUCH N NAME NATION NATIONALVALUE NATIVES NAVY_NAME NAVY NEED NEEDED NEGATIVE NEW NEWCOUNTRY NF NOW NUM NUMBER ODDS "
    "OLD OLDCOUNTRY OPINION OPT OPTION ORDER ORG ORGANISATION OTHER OURCAPITAL OURCOUNTRY_ADJ OURCOUNTRY OUTPUT OVERLORD "
    "PARTY PASSIVE PAY PEN PENALTY PER PERC PERCENT PERCENTAGE PLAYER POLICY POP POPTYPE POPULARITY POSITION POSITIVE "
    "POWER PRES PRESTIGE PRODUCED PRODUCER PROG PROGRESS PROV PROVINCE PROVINCECULTURE PROVINCENAME PROVINCERELIGION "
    "PROVINCES PROVS RANK RATE REC RECIPIENT REFORM REGION REL RELATION REQ REQUIRED RESOURCE RESULT RSTATE RULE SEA "
    "SECOND_COUNTRY SECOND SELF SELL SETTING SHIPS SIZE SKILL SOURCE SPEED SPHEREMASTER STATE STATENAME STR STRATA "
    "STRENGTH SUB TAG TARGET_COUNTRY TARGET TARGETLIST TECH TERMS TERRAIN TEXT THEM THIRD THREAT TIME TITLE TO TOT TOTAL "
    "TYPE UNEMPLOYED UNION_ADJ UNION UNIT UNITS UNTIL VAL VALUE VERB VERSUS WAR WARGOAL WE WHAT WHERE WHICH WHO WINNER X "
    "Y YEAR YEARS").split(u' ');
const OrderedMap<QString, WindowDef> WINDOWS = {
    {u"country"_s, {u"interface/event_country_window.gui"_s, u"event_country_window"_s, u"event_country_optionbutton"_s,
                    u"event_country_option_start"_s, u"event_country_option_offset"_s}},
    {u"province"_s, {u"interface/event_province_window.gui"_s, u"event_province_window"_s, u"event_province_optionbutton"_s,
                     u"event_province_option_start"_s, u"event_province_option_offset"_s}},
    {u"major"_s, {u"interface/event_major_window.gui"_s, u"event_major_window"_s, u"event_major_optionbutton"_s,
                  u"event_major_option_start"_s, u"event_major_option_offset"_s}},
};

static OptStr or_none(const OptStr &a, const OptStr &b)   // `a or b` of two texts / None
{
    return a && !a->isEmpty() ? a : b;
}

static std::optional<QImage> _slot(const std::optional<QImage> &pic, const std::optional<QImage> &dflt)
{
    // A picture in the place of the window's own: the game draws it at that one's size (Ferrum Mare's 190 × 190
    // decision pictures stand in a 95 × 95 place).
    if (!pic)
        return dflt;
    if (dflt && pic->size() != dflt->size())
        return pic->scaled(dflt->width(), dflt->height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return pic;
}

struct Op {                                          // (kind, x, y, w, h, paint)
    QString kind;
    double x, y, w, h;
    std::function<void(QPainter &)> paint;
};

static QImage _paint(const std::vector<Op> &ops, double margin = 14, const QColor &bg = QColor(u"#2a2f37"_s))
{
    // Draw operations [(kind, x, y, …)] into one picture that holds them all.
    double x0 = 0, y0 = 0, x1 = 10, y1 = 10;
    for (const Op &o : ops) {
        x0 = std::min(x0, o.x);
        y0 = std::min(y0, o.y);
        x1 = std::max(x1, o.x + o.w);
        y1 = std::max(y1, o.y + o.h);
    }
    QImage img(int(x1 - x0 + 2 * margin), int(y1 - y0 + 2 * margin), QImage::Format_RGBA8888);
    img.fill(bg);
    QPainter p(&img);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.translate(margin - x0, margin - y0);
    for (const Op &o : ops)
        o.paint(p);
    p.end();
    return img;
}

static QString pyd(double x) { return QString::number(static_cast<long long>(x)); }   // "%d" % a float

std::pair<std::optional<QImage>, Notes> render_event(GameLook &look, const QString &kind, const QString &title,
                                                     const QString &desc, const QStringList &options,
                                                     const OptStr &picture, const Samples &samples)
{
    const WindowDef &wd = *WINDOWS.get(kind);
    const auto &g = look.gui(wd.file);
    const auto wit = g.find(wd.window);
    Notes notes;
    std::vector<Op> ops;
    if (wit == g.end())
        return {std::nullopt, {{u"warn"_s, u"The layout %1 was not found."_s.arg(wd.file)}}};
    const script::Node *win = wit->second;
    GameLook *lk = &look;
    for (const script::Node &n : win->kids) {
        if (!n.is_block || !n.has_key || n.key.isEmpty())
            continue;
        const QString k = py::lower(n.key), name = py::lower(_val(n, u"name"_s).value_or(QString()));
        const auto [x, y] = _xy(n, u"position"_s);
        if (k == u"icontype" || k == u"guibuttontype") {
            std::optional<QImage> img = look.sprite(or_none(_val(n, u"spritetype"_s), _val(n, u"quadtexturesprite"_s)));
            if (name == u"event_images") {
                const OptStr path = picture && !picture->isEmpty() ? look.find_picture(u"events"_s, *picture) : std::nullopt;
                img = path ? _slot(look.picture(*path), img) : img;
                if (picture && !picture->isEmpty() && !path)
                    notes.emplace_back(u"warn"_s, u"The picture “%1” is not in gfx/pictures/events."_s.arg(*picture));
            }
            if (!img || std::abs(x) > 5000 || std::abs(y) > 5000)
                continue;
            const QImage im = *img;
            ops.push_back({u"pic"_s, x, y, double(im.width()), double(im.height()),
                           [im, x, y](QPainter &p) { p.drawImage(QPointF(x, y), im); }});
        } else if (k == u"instanttextboxtype" || k == u"textboxtype") {
            OptStr text;
            if (name == u"title")
                text = title;
            else if (name == u"description")
                text = desc;
            else if (name == u"date")
                text = samples.value(u"DATE"_s, u"1 January, 1836"_s);
            else if (name == u"province")
                text = samples.value(u"PROVINCENAME"_s, QString());
            if (!text)
                continue;
            const double mw = _num(n, u"maxwidth"_s), mh = _num(n, u"maxheight"_s);
            const OptStr font = _val(n, u"font"_s), fmt = _val(n, u"format"_s);
            const auto got = gamefont::layout(look.fonts, font, *text, mw, samples);
            if (!got) {
                notes.emplace_back(u"warn"_s, u"The font %1 was not found."_s.arg(font.value_or(u"None"_s)));
                continue;
            }
            const long long nl = static_cast<long long>(got->lines.size());
            const double h = double(nl * got->font->line);
            if (mh != 0 && h > mh && (name == u"title" || name == u"description"))
                notes.emplace_back(u"warn"_s, u"The %1 is %2 lines; its box holds %3 — in the game the rest runs over what "
                                              "lies below it."_s.arg(name, QString::number(nl),
                                                                    QString::number(std::max(1LL, static_cast<long long>(
                                                                        py::floordiv(mh, double(got->font->line)))))));
            const QString t = *text;
            ops.push_back({u"text"_s, x, y, mw != 0 ? mw : double(got->widest), h,
                           [lk, font, t, x, y, mw, mh, fmt, samples](QPainter &p) {
                               gamefont::draw(p, lk->fonts, font, t, x, y, mw, mh, fmt.value_or(QString()), false, samples);
                           }});
        }
    }
    const auto bit = g.find(wd.button);
    const script::Node *btn = bit != g.end() ? bit->second : nullptr;
    const std::optional<QImage> bimg = btn ? look.sprite(_val(*btn, u"quadtexturesprite"_s)) : std::nullopt;
    const auto st = g.find(wd.start), of = g.find(wd.offset);
    const auto [sx, sy] = st != g.end() ? _xy(*st->second, u"position"_s) : XY{40.0, 500.0};
    const auto [ox, oy] = of != g.end() ? _xy(*of->second, u"position"_s) : XY{0.0, 30.0};
    const OptStr bfont = or_none(btn ? _val(*btn, u"buttonfont"_s) : std::nullopt, u"vic_18_black"_s);
    for (qsizetype i = 0; i < options.size(); ++i) {
        const QString text = options[i];
        const double x = sx + ox * double(i), y = sy + oy * double(i);
        const double bw = bimg ? bimg->width() : 440, bh = bimg ? bimg->height() : 26;
        const auto got = gamefont::layout(look.fonts, bfont, text, 0, samples);
        const long long tw = got ? got->widest : 0, th = got ? got->font->line : 16;
        if (double(tw) > bw - 12)
            notes.emplace_back(u"warn"_s, u"Option %1 is %2 pixels wide; its button is %3 — the ends are cut off."_s.arg(
                                              QString::number(i + 1), QString::number(tw), pyd(bw)));
        ops.push_back({u"opt"_s, x, y, bw, bh, [lk, bimg, bfont, text, x, y, bw, bh, tw, th, samples](QPainter &p) {
                           if (bimg)
                               p.drawImage(QPointF(x, y), *bimg);
                           p.save();
                           p.setClipRect(QRectF(x + 4, y, bw - 8, bh));
                           gamefont::draw(p, lk->fonts, bfont, text, x + (bw - double(tw)) / 2.0,
                                          y + (bh - double(th)) / 2.0, 0, 0, u"left"_s, false, samples);
                           p.restore();
                       }});
    }
    return {_paint(ops), notes};
}

std::pair<std::optional<QImage>, Notes> render_decision(GameLook &look, const QString &title, const QString &desc,
                                                        const OptStr &picture, const Samples &samples)
{
    const auto &g = look.gui(u"interface/country_politics.gui"_s);
    const auto wit = g.find(u"decision_entry"_s);
    Notes notes;
    std::vector<Op> ops;
    if (wit == g.end())
        return {std::nullopt, {{u"warn"_s, u"The layout of a decision was not found (interface/country_politics.gui)."_s}}};
    GameLook *lk = &look;
    double bottom = 0.0;
    for (const script::Node &n : wit->second->kids) {
        if (!n.is_block || !n.has_key || n.key.isEmpty())
            continue;
        const QString k = py::lower(n.key), name = py::lower(_val(n, u"name"_s).value_or(QString()));
        const auto [x, y] = _xy(n, u"position"_s);
        if (k == u"icontype" || k == u"guibuttontype") {
            std::optional<QImage> img = look.sprite(or_none(_val(n, u"spritetype"_s), _val(n, u"quadtexturesprite"_s)));
            if (name == u"decision_image") {
                const OptStr path = picture && !picture->isEmpty() ? look.find_picture(u"decisions"_s, *picture)
                                                                   : std::nullopt;
                img = path ? _slot(look.picture(*path), img) : img;
            }
            if (!img)
                continue;
            bottom = std::max(bottom, y + img->height());
            const QImage im = *img;
            ops.push_back({u"pic"_s, x, y, double(im.width()), double(im.height()),
                           [im, x, y](QPainter &p) { p.drawImage(QPointF(x, y), im); }});
        } else if (k == u"instanttextboxtype" || k == u"textboxtype") {
            OptStr text;
            if (name == u"decision_name")
                text = title;
            else if (name == u"decision_desc")
                text = desc;
            if (!text)
                continue;
            const double mw = _num(n, u"maxwidth"_s), mh = _num(n, u"maxheight"_s);
            const OptStr font = _val(n, u"font"_s), fmt = _val(n, u"format"_s);
            const auto got = gamefont::layout(look.fonts, font, *text, mw, samples);
            if (!got)
                continue;
            const long long nl = static_cast<long long>(got->lines.size()), line = got->font->line;
            const long long fits = mh != 0 ? std::max(1LL, static_cast<long long>(py::floordiv(mh, double(line)))) : nl;
            if (name == u"decision_desc" && nl > fits)
                notes.emplace_back(u"info"_s, u"The description is %1 lines; %2 show in the list — the whole text is in "
                                              "the hover text below."_s.arg(QString::number(nl), QString::number(fits)));
            bottom = std::max(bottom, y + double(std::min(nl, fits) * line));
            if (name == u"decision_name" && double(got->widest) > mw)
                notes.emplace_back(u"warn"_s, u"The name is %1 pixels wide; its line holds %2."_s.arg(
                                                  QString::number(got->widest), pyd(mw)));
            const QString t = *text;
            ops.push_back({u"text"_s, x, y, mw, double(std::min(nl, fits) * line),
                           [lk, font, t, x, y, mw, mh, fmt, samples](QPainter &p) {
                               gamefont::draw(p, lk->fonts, font, t, x, y, mw, mh, fmt.value_or(QString()), true, samples);
                           }});
        }
    }
    // the hover text: the whole description on the game's tooltip ground
    const QString tip_font = u"ToolTip_Font"_s;
    const double tip_w = 420;
    const auto got = gamefont::layout(look.fonts, tip_font, desc, tip_w, samples);
    if (got && !desc.isEmpty()) {
        const double w = double(got->widest), th = double(static_cast<long long>(got->lines.size()) * got->font->line);
        const double tx = 20.0, ty = bottom + 34;
        ops.push_back({u"tip"_s, tx - 10, ty - 8, w + 20, th + 16, [lk, tip_font, desc, tx, ty, w, th, tip_w, samples](QPainter &p) {
                           p.setPen(QPen(QColor(150, 126, 78), 2));
                           p.setBrush(QColor(18, 18, 18, 240));
                           p.drawRect(QRectF(tx - 10, ty - 8, w + 20, th + 16));
                           gamefont::draw(p, lk->fonts, tip_font, desc, tx, ty, tip_w, 0, u"left"_s, false, samples);
                       }});
    }
    return {_paint(ops), notes};
}

// ================================================================================================ the text field
CodeEdit::CodeEdit(TextsWindow *win_, QWidget *parent) : QPlainTextEdit(parent), win(win_)
{
    setTabChangesFocus(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    pop = new QListWidget(this);
    pop->setWindowFlags(Qt::ToolTip);
    pop->setFocusPolicy(Qt::NoFocus);
    pop->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(pop, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) { _take(it); });
    pop->hide();
    connect(this, &QPlainTextEdit::textChanged, this, [this] { _suggest(); });
}

void CodeEdit::set_text(const QString &text)
{
    blockSignals(true);
    setPlainText(text);
    blockSignals(false);
    _at = text;
}

QString CodeEdit::value() const { return toPlainText().replace(u'\n', u"\\n"_s); }

void CodeEdit::focusOutEvent(QFocusEvent *e)
{
    pop->hide();
    QPlainTextEdit::focusOutEvent(e);
    if (value() != _at) {
        _at = value();
        Q_EMIT committed();
    }
}

void CodeEdit::insert(const QString &text, int back)
{
    QTextCursor c = textCursor();
    c.insertText(text);
    if (back)
        c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, back);
    setTextCursor(c);
    setFocus();
}

void CodeEdit::wrap(const QString &code)
{
    QTextCursor c = textCursor();
    const QString sel = c.selectedText();
    c.insertText(u"§%1%2§!"_s.arg(code, sel));
    if (sel.isEmpty())
        c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 2);
    setTextCursor(c);
    setFocus();
}

void CodeEdit::keyPressEvent(QKeyEvent *e)
{
    if (pop->isVisible()) {
        if (e->key() == Qt::Key_Down || e->key() == Qt::Key_Up) {
            const int n = std::max(1, pop->count());
            const int r = pop->currentRow() + (e->key() == Qt::Key_Down ? 1 : -1);
            pop->setCurrentRow(((r % n) + n) % n);
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Tab) {
            _take(pop->currentItem());
            return;
        }
        if (e->key() == Qt::Key_Escape) {
            pop->hide();
            return;
        }
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        insert(u"\\n"_s);                        // the game's line break is written \n
        return;
    }
    QPlainTextEdit::keyPressEvent(e);
}

std::optional<CodeEdit::WordAt> CodeEdit::_word()
{
    const QTextCursor c = textCursor();
    const QString left = c.block().text().left(c.positionInBlock());
    static const QRegularExpression re(uR"((§)$|(\$)([A-Za-z_0-9]*)$|(@)([A-Za-z0-9]{0,3})$|(\\)$)"_s);
    const auto m = re.match(left);
    if (!m.hasMatch())
        return std::nullopt;
    if (m.capturedStart(1) >= 0)
        return WordAt{u"§"_s, QString(), 0};
    if (m.capturedStart(2) >= 0) {
        if (left.count(u'$') % 2 == 0)           // (this $ closes a word)
            return std::nullopt;
        return WordAt{u"$"_s, m.captured(3), int(m.captured(3).size())};
    }
    if (m.capturedStart(4) >= 0)
        return WordAt{u"@"_s, m.captured(5), int(m.captured(5).size())};
    return WordAt{u"\\"_s, QString(), 0};
}

void CodeEdit::_suggest()
{
    const auto w = _word();
    if (!w || !hasFocus()) {
        pop->hide();
        return;
    }
    const auto rows = win->suggestions(w->kind, w->typed);
    if (rows.empty()) {
        pop->hide();
        return;
    }
    pop->clear();
    for (size_t i = 0; i < rows.size() && i < 60; ++i) {
        auto *it = new QListWidgetItem(rows[i].text);
        it->setData(Qt::UserRole, rows[i].put);
        if (rows[i].colour) {
            const auto [r, g, b] = *rows[i].colour;
            it->setForeground(QColor(r, g, b));
            it->setBackground(r + g + b < 120 ? QColor(60, 60, 60) : QColor(26, 26, 26));
        }
        pop->addItem(it);
    }
    pop->setCurrentRow(0);
    const QRect r = cursorRect();
    pop->setFixedSize(430, std::min(300, 8 + 22 * pop->count()));
    pop->move(viewport()->mapToGlobal(QPoint(r.left(), r.bottom() + 4)));
    pop->show();
}

void CodeEdit::_take(QListWidgetItem *it)
{
    if (!it)
        return;
    const auto w = _word();
    const QString put = it->data(Qt::UserRole).toString();
    QTextCursor c = textCursor();
    if (w)
        for (int i = 0; i < w->n; ++i)
            c.deletePreviousChar();
    c.insertText(put);
    setTextCursor(c);
    pop->hide();
}

Field::Field(TextsWindow *win_, const QString &title_, QWidget *parent) : QWidget(parent), win(win_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(3);
    auto *head = new QHBoxLayout();
    head->setSpacing(6);
    title = widgets::label(title_, u"sub"_s, false);
    QFont f(title->font());
    f.setBold(true);
    title->setFont(f);
    head->addWidget(title);
    key_lbl = widgets::label(QString(), u"faint"_s, false);
    key_lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    head->addWidget(key_lbl, 1);
    lay->addLayout(head);
    auto *bar = new QHBoxLayout();
    bar->setSpacing(4);
    for (const QString code : {u"Y"_s, u"R"_s, u"G"_s, u"B"_s, u"W"_s, u"g"_s, u"b"_s}) {
        auto *b = new QPushButton(u"§"_s + code);
        const auto [r, g, bl] = *gamefont::COLOURS.get(code);
        const QString cname = *gamefont::COLOUR_NAMES.get(code);
        b->setFixedSize(40, 26);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(u"%1: the marked words (or what you type next) in %2 — written §%3 … §!"_s.arg(
            py::capitalize(cname), cname, code));
        b->setStyleSheet(u"QPushButton { color: rgb(%1,%2,%3); background: %4; font-weight: 700; padding: 0; "
                         "border: 2px solid #0a0a0a; border-radius: 4px; }"_s.arg(r).arg(g).arg(bl).arg(
                             r + g + bl < 120 ? u"#8a8a8a"_s : u"#1c1c1c"_s));
        connect(b, &QPushButton::clicked, this, [this, code] { edit->wrap(code); });
        bar->addWidget(b);
    }
    const std::vector<std::tuple<QString, QString, std::function<void()>>> more = {
        {u"§!"_s, u"Back to the normal colour from here on"_s, [this] { edit->insert(u"§!"_s); }},
        {u"↵ \\n"_s, u"A new line (the game writes it \\n) — the Enter key does the same"_s, [this] { edit->insert(u"\\n"_s); }},
        {u"$ word"_s, u"A word the game fills in: type $ and pick from the list"_s, [this] { edit->insert(u"$"_s); }},
        {u"@ flag"_s, u"A nation's flag: type @ and the nation's tag"_s, [this] { edit->insert(u"@"_s); }}};
    for (const auto &[text, tip, fn] : more) {
        auto *b = new QPushButton(text);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tip);
        b->setStyleSheet(u"QPushButton { padding: 2px 8px; }"_s);
        connect(b, &QPushButton::clicked, this, [fn] { fn(); });
        bar->addWidget(b);
    }
    bar->addStretch(1);
    lay->addLayout(bar);
    edit = new CodeEdit(win);
    edit->setMinimumHeight(54);
    connect(edit, &QPlainTextEdit::textChanged, this, &Field::changed);
    connect(edit, &CodeEdit::committed, this, [this] { Q_EMIT committed(key, edit->value()); });
    lay->addWidget(edit);
    note = new QLabel(QString());
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    note->hide();
    lay->addWidget(note);
}

void Field::set(const QString &key_, const QString &text, int rows, const QString &hint)
{
    key = key_;
    key_lbl->setText(key_);
    edit->set_text(text);
    edit->setFixedHeight(22 + 19 * rows);
    edit->setPlaceholderText(hint);
}

static QString sev_colour(const QString &s)          // {"error": ERROR, "warn": WARN, "info": INK_DIM}
{
    return s == u"error" ? theme::ERROR_ : s == u"info" ? theme::INK_DIM : theme::WARN;
}

void Field::show_notes(const Notes &notes)
{
    if (notes.empty()) {
        note->hide();
        return;
    }
    QStringList parts;
    for (const auto &[s, t] : notes)
        parts << u"<span style='color:%1; font-weight:%2'>%3%4</span>"_s.arg(
            sev_colour(s), s != u"info" ? u"600"_s : u"400"_s, s != u"info" ? u"⚠ "_s : QString(), t);
    note->setText(parts.join(u"<br>"_s));
    note->show();
}

bool is_literal(const QString &key)
{
    static const QRegularExpression re(QRegularExpression::anchoredPattern(uR"([A-Za-z0-9_.\-]+)"_s));   // (fullmatch)
    return !key.isEmpty() && !re.match(key).hasMatch();
}

OptStr set_in_script(const QString &text, const std::pair<qsizetype, qsizetype> &span, const QString &what,
                     std::optional<long long> nth, const QString &old, const QString &new_)
{
    const auto [a, b] = span;
    const QString block = py::slice(text, a, b);
    struct Hit {
        qsizetype s2, e2;
        QString g2;
    };
    auto find_all = [&block](const QString &word) {
        std::vector<Hit> out;
        const QRegularExpression re(uR"re((\b%1[ \t]*=[ \t]*)"([^"\r\n]*)")re"_s.arg(word),
                                    QRegularExpression::UseUnicodePropertiesOption);
        auto it = re.globalMatch(block);
        while (it.hasNext()) {
            const auto m = it.next();
            out.push_back({m.capturedStart(2), m.capturedEnd(2), m.captured(2)});
        }
        return out;
    };
    std::vector<Hit> hits;
    if (what == u"name") {
        const auto all = find_all(u"name"_s);
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].g2 == old && (!nth || static_cast<long long>(i) == *nth))
                hits.push_back(all[i]);
        if (hits.empty())
            for (const Hit &h : all)
                if (h.g2 == old)
                    hits.push_back(h);
    } else {
        for (const Hit &h : find_all(what))
            if (h.g2 == old)
                hits.push_back(h);
    }
    if (hits.empty())
        return std::nullopt;
    const Hit &m = hits[0];
    return py::head(text, a) + block.left(m.s2) + QString(new_).replace(u'"', u'\'') + block.mid(m.e2) + py::tail(text, b);
}

// ================================================================================================ the window
TextsWindow::TextsWindow(moddata::Mod &mod_, QWidget *parent) : QWidget(parent, Qt::Window), mod(mod_)
{
    setWindowTitle(u"Texts & preview"_s);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    outer->addWidget(split, 1);
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setObjectName(u"sideScroll"_s);
    sc->setMinimumWidth(520);
    auto *left = new QWidget();
    left->setObjectName(u"sideInner"_s);
    ll = new QVBoxLayout(left);
    ll->setContentsMargins(14, 12, 14, 12);
    ll->setSpacing(10);
    head = widgets::label(u"Pick an event or a decision in the list of the Scripts page."_s, u"title"_s);
    ll->addWidget(head);
    sub = widgets::label(QString(), u"sub"_s);
    ll->addWidget(sub);
    fields_host = new QWidget();
    fl = new QVBoxLayout(fields_host);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(12);
    ll->addWidget(fields_host);
    ll->addWidget(widgets::label(
        u"The codes: §Y §R §G §B §W §g §b colour what follows, §! goes back to normal. \\n is a new line. "
        "$COUNTRY$ and its kind are filled in by the game — the preview puts sample words there. @TAG draws "
        "that nation's flag. Type § $ @ or \\ for suggestions. A text is saved as a pending edit when you "
        "leave its field."_s, u"sub"_s));
    ll->addStretch(1);
    sc->setWidget(left);
    split->addWidget(sc);
    auto *right = new QWidget();
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(10, 10, 10, 10);
    rl->setSpacing(6);
    auto *row = new QHBoxLayout();
    row->setSpacing(8);
    row->addWidget(widgets::label(u"As in the game"_s, u"title"_s));
    row->addStretch(1);
    row->addWidget(widgets::label(u"Sample nation"_s, u"sub"_s, false));
    nation_box = new QComboBox();
    nation_box->setMinimumWidth(220);
    connect(nation_box, &QComboBox::activated, this, [this](int) { _show(); });
    row->addWidget(nation_box);
    rl->addLayout(row);
    auto *prow = new QHBoxLayout();
    prow->setSpacing(8);
    prow->addWidget(widgets::label(u"Picture"_s, u"sub"_s, false));
    pic_box = new QComboBox();
    pic_box->setMinimumWidth(260);
    pic_box->setToolTip(u"The picture of this event / decision (gfx/pictures/events or …/decisions). "
                        "Choosing one writes  picture = \"…\"  into its script."_s);
    connect(pic_box, &QComboBox::activated, this, [this](int) { _pick_picture(); });
    prow->addWidget(pic_box, 1);
    pic_note = widgets::label(QString(), u"faint"_s, false);
    prow->addWidget(pic_note);
    rl->addLayout(prow);
    view = new QLabel();
    view->setAlignment(Qt::AlignCenter);
    view->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    view->setMinimumSize(300, 240);
    view->setStyleSheet(u"QLabel { background: #2a2f37; border: 1px solid %1; }"_s.arg(theme::LINE));
    rl->addWidget(view, 1);
    notes = new QLabel(QString());
    notes->setWordWrap(true);
    notes->setTextFormat(Qt::RichText);
    rl->addWidget(notes);
    rl->addWidget(widgets::label(u"Drawn with the game's own window, picture, fonts and colours; the words the game fills "
                                 "in are samples. The hover text's frame and width are the editor's guess."_s,
                                 u"faint"_s));
    split->addWidget(right);
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 6);
    split->setSizes({620, 900});
    _tick = new QTimer(this);
    _tick->setSingleShot(true);
    connect(_tick, &QTimer::timeout, this, [this] { _show(); });
}

// ------------------------------------------------------------------ data
QString TextsWindow::loc(const QString &key) const
{
    if (doc && doc->maps.contains(u"loc"_s))
        if (const OptStr t = doc->map<localisation::LocEdits>(u"loc"_s).text(key); t && !t->isEmpty())
            return *t;
    const QString *t = mod.loc.get(key);
    return t ? *t : QString();
}

const OrderedMap<QString, long long> &TextsWindow::usage()
{
    if (!_usage) {
        _usage.emplace();
        static const QRegularExpression re(uR"(\$([A-Za-z_0-9]+)\$)"_s);
        for (const auto &kv : mod.loc) {
            if (!kv.second.contains(u'$'))
                continue;
            auto it = re.globalMatch(kv.second);
            while (it.hasNext()) {
                const QString w = py::upper(it.next().captured(1));
                (*_usage)[w] += 1;
            }
        }
    }
    return *_usage;
}

QStringList TextsWindow::known_vars()
{
    QSet<QString> s(VARS.begin(), VARS.end());
    for (const auto &kv : usage())
        s.insert(kv.first);
    QStringList out(s.begin(), s.end());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<TextsWindow::Suggestion> TextsWindow::suggestions(const QString &kind, const QString &typed)
{
    // [(what the list shows, what is written, colour or None)] for what was typed after § $ @ \ .
    std::vector<Suggestion> out;
    if (kind == u"§") {
        for (const QString c : {u"Y"_s, u"R"_s, u"G"_s, u"B"_s, u"W"_s, u"g"_s, u"b"_s})
            out.push_back({u"§%1   %2"_s.arg(c, *gamefont::COLOUR_NAMES.get(c)), c, *gamefont::COLOURS.get(c)});
        out.push_back({u"§!   back to the normal colour"_s, u"!"_s, std::nullopt});
        return out;
    }
    if (kind == u"\\") {
        out.push_back({u"\\n   a new line"_s, u"n"_s, std::nullopt});
        return out;
    }
    if (kind == u"$") {
        const auto &use = usage();
        const QString t = py::upper(typed);
        QStringList names = known_vars();
        std::stable_sort(names.begin(), names.end(), [&](const QString &a, const QString &b) {
            const bool sa = !a.startsWith(t), sb = !b.startsWith(t);
            if (sa != sb)
                return sa < sb;
            const long long ua = -use.value(a, 0), ub = -use.value(b, 0);
            if (ua != ub)
                return ua < ub;
            return a < b;
        });
        for (const QString &v : names) {
            if (!v.contains(t))
                continue;
            const QString *mean = MEANING.get(v);
            const long long n = use.value(v, 0);
            out.push_back({u"$%1$%2%3"_s.arg(v, mean ? u"   — "_s + *mean : QString(),
                                              n ? u"   (%1× in the texts)"_s.arg(n) : QString()),
                           v + u'$', std::nullopt});
        }
        return out;
    }
    if (kind == u"@") {
        const QString t = py::upper(typed);
        QStringList tags;
        for (const auto &kv : mod.countries)
            if (py::upper(kv.first).startsWith(t))
                tags << kv.first;
        std::sort(tags.begin(), tags.end());
        for (const QString &tg : tags)
            out.push_back({u"@%1   %2"_s.arg(tg, mod.countries.get(tg)->name), tg, std::nullopt});
    }
    return out;
}

Samples TextsWindow::samples() const
{
    const QVariant d = nation_box->currentData();
    const OptStr tag = d.isValid() && !d.toString().isEmpty() ? OptStr(d.toString()) : std::nullopt;
    const moddata::Country *c = tag ? mod.countries.get(*tag) : nullptr;
    const QString name = c && !c->name.isEmpty() ? c->name : u"Nation"_s;
    const QString adj = tag ? loc(*tag + u"_ADJ"_s) : QString();
    OptStr o = tag;
    for (const auto &kv : mod.countries)
        if (!tag || kv.first != *tag) {
            o = kv.first;
            break;
        }
    const moddata::Country *oc = o ? mod.countries.get(*o) : nullptr;
    const QString oname = o ? (oc ? oc->name : u"Other nation"_s) : u"Other nation"_s;
    const moddata::Province *prov = nullptr;
    for (const auto &kv : mod.provinces)
        if (kv.second.owner == tag) {
            prov = &kv.second;
            break;
        }
    const QString oadj = o ? loc(*o + u"_ADJ"_s) : QString();
    const QString capital = prov ? prov->name : u"Capital"_s, pname = prov ? prov->name : u"Province"_s;
    const QString state = prov ? (prov->region && !prov->region->isEmpty() ? *prov->region : u"State"_s) : u"State"_s;
    const QString an = !adj.isEmpty() ? adj : name;
    return Samples{{u"COUNTRY"_s, name}, {u"COUNTRY_ADJ"_s, an}, {u"COUNTRYNAME"_s, name}, {u"COUNTRYADJ"_s, an},
                   {u"OURCOUNTRY"_s, name}, {u"OURCOUNTRY_ADJ"_s, an}, {u"NATION"_s, name},
                   {u"FROMCOUNTRY"_s, oname}, {u"FROMCOUNTRY_ADJ"_s, !oadj.isEmpty() ? oadj : oname},
                   {u"CAPITAL"_s, capital}, {u"CAP"_s, capital}, {u"OURCAPITAL"_s, capital}, {u"PROVINCENAME"_s, pname},
                   {u"ANYPROVINCE"_s, pname}, {u"STATENAME"_s, state},
                   {u"YEAR"_s, u"1836"_s}, {u"DATE"_s, u"1 January, 1836"_s}, {u"MONARCHTITLE"_s, u"King"_s}};
}

// ------------------------------------------------------------------ the entry
void TextsWindow::refresh(document::Document *d, const events::Entry *e_in)
{
    doc = d;
    scriptstore::ScriptStore *store = d && d->maps.contains(u"scripts"_s) ? &d->map<scriptstore::ScriptStore>(u"scripts"_s)
                                                                          : nullptr;
    if (!look && store)
        look = std::make_unique<GameLook>(mod, store);
    if (!nation_box->count()) {
        OrderedMap<QString, long long> owners;
        for (const auto &kv : mod.provinces)
            if (kv.second.owner && !kv.second.owner->isEmpty())
                owners[*kv.second.owner] += 1;
        std::vector<std::pair<QString, long long>> by(owners.begin(), owners.end());
        std::stable_sort(by.begin(), by.end(), [](const auto &a, const auto &b) { return -a.second < -b.second; });
        for (size_t i = 0; i < by.size() && i < 400; ++i) {
            const QString &tag = by[i].first;
            const moddata::Country *c = mod.countries.get(tag);
            nation_box->addItem(u"%1  (%2)"_s.arg(c ? c->name : tag, tag), tag);
        }
    }
    if (e_in)
        entry = *e_in;
    if (!entry)
        return;
    const events::Entry &e = *entry;
    struct Want {
        QString title, key;
        int rows;
    };
    std::vector<Want> want = {{e.is_event() ? u"Title"_s : u"Name"_s, e.title, 2}, {u"Description"_s, e.desc, 7}};
    if (e.is_event())
        for (size_t i = 0; i < e.options.size(); ++i)
            want.push_back({u"Option %1"_s.arg(i + 1), e.options[i].name, 1});
    QStringList keys, have;
    for (const Want &w : want)
        keys << w.key;
    for (Field *f : fields)
        have << f->key;
    if (have != keys) {
        for (Field *f : fields) {
            f->setParent(nullptr);
            f->deleteLater();
        }
        fields.clear();
        for (const Want &w : want) {
            auto *f = new Field(this, w.title);
            connect(f, &Field::changed, this, [this] { _tick->start(120); });
            connect(f, &Field::committed, this, [this](const QString &k, const QString &t) { _commit(k, t); });
            fl->addWidget(f);
            fields.push_back(f);
        }
    }
    for (size_t i = 0; i < fields.size() && i < want.size(); ++i) {
        Field *f = fields[i];
        const Want &w = want[i];
        if (w.key.isEmpty()) {
            f->set(QString(), QString(), w.rows, u"This %1 names no text key in its script."_s.arg(
                                                     e.is_event() ? u"event"_s : u"decision"_s));
            f->edit->setEnabled(false);
            continue;
        }
        f->edit->setEnabled(true);
        if (f->edit->hasFocus() && f->key == w.key)
            continue;                            // (being typed in: left alone)
        const QString text = loc(w.key);
        f->in_script = text.isEmpty() && is_literal(w.key);
        if (f->in_script) {                      // (the text stands in the script itself: it IS the "key")
            f->set(w.key, w.key, w.rows, QString());
            f->key_lbl->setText(u"written in the script itself — a change goes into the script"_s);
        } else {
            f->set(w.key, text, w.rows, u"No text yet — the game shows the key itself: %1"_s.arg(w.key));
        }
    }
    const QString kind = !e.is_event() ? u"decision"_s : e.province() ? u"province event"_s
                                                     : e.major        ? u"major event"_s
                                                                      : u"country event"_s;
    const QString ident = e.is_event() ? e.id.value_or(u"None"_s) : e.key;
    head->setText(u"%1 %2"_s.arg(py::capitalize(kind), ident));
    sub->setText(e.rel.value_or(QString()) + (!e.picture.isEmpty() ? u"  ·  picture: %1"_s.arg(e.picture) : QString()));
    setWindowTitle(u"Texts & preview — %1"_s.arg(ident));
    if (picture_names) {
        const QStringList names = picture_names(e.is_event());
        const QString cur = !e.picture.isEmpty() ? e.picture : (e.is_event() ? QString() : e.key);
        pic_box->blockSignals(true);
        pic_box->clear();
        pic_box->addItem(e.is_event() ? u"(none)"_s : u"(the one named like the decision, if there is one)"_s, QString());
        for (const QString &n : names)
            pic_box->addItem(n, n);
        const int i = pic_box->findData(cur);
        pic_box->setCurrentIndex(std::max(0, i));
        pic_box->blockSignals(false);
        pic_note->setText(i >= 0 || cur.isEmpty() ? u"%1 pictures"_s.arg(names.size()) : u"“%1” is not there"_s.arg(cur));
    }
    _show();
}

void TextsWindow::_pick_picture()
{
    const QString name = pic_box->currentData().toString();
    if (!name.isEmpty() && set_picture && entry && name != entry->picture)
        set_picture(name);
}

void TextsWindow::_commit(const QString &key, const QString &text)
{
    if (key.isEmpty())
        return;
    Field *f = nullptr;
    size_t at = 0;
    for (size_t i = 0; i < fields.size(); ++i)
        if (fields[i]->key == key) {
            f = fields[i];
            at = i;
            break;
        }
    if (f && f->in_script) {
        scriptstore::ScriptStore *store = doc && doc->maps.contains(u"scripts"_s)
                                              ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
        if (!entry || !store || text == key || py::strip(text).isEmpty())
            return;
        const events::Entry &e = *entry;
        const QString what = at == 0 ? u"title"_s : at == 1 ? u"desc"_s : u"name"_s;
        const std::optional<long long> nth = at >= 2 ? std::optional<long long>(static_cast<long long>(at) - 2) : std::nullopt;
        const QString rel = e.rel.value_or(QString());
        if (const OptStr nw = set_in_script(store->text(rel), e.span, what, nth, key, text))
            Q_EMIT scriptEdit(u"Text of %1 %2"_s.arg(e.is_event() ? u"event"_s : u"decision"_s,
                                                     e.is_event() ? e.id.value_or(u"None"_s) : e.key),
                              rel, *nw);
        return;
    }
    if (text.contains(u';')) {
        _show();
        return;                                  // (said in red under the field; not saved)
    }
    if (text != loc(key))
        Q_EMIT nameEdit(key, text);
}

void TextsWindow::_show()
{
    if (!entry || !look || fields.empty())
        return;
    const events::Entry &e = *entry;
    QStringList vals, shown;
    for (Field *f : fields) {
        vals << (!f->key.isEmpty() ? f->edit->value() : QString());
        f->semicolon_ok = f->in_script;          // (a semicolon is fine in the script, not in a text file)
    }
    for (size_t i = 0; i < fields.size(); ++i)
        shown << (!vals[qsizetype(i)].isEmpty() ? vals[qsizetype(i)] : fields[i]->key);   // (no text: the game shows the key)
    const Samples smp = samples();
    const QStringList known = known_vars();
    QStringList tags;
    for (const auto &kv : mod.countries)
        tags << kv.first;
    for (size_t i = 0; i < fields.size(); ++i) {
        Notes keep;
        for (const auto &n : gamefont::check(vals[qsizetype(i)], known, tags))
            if (!(fields[i]->semicolon_ok && n.second.contains(u"semicolon"_s)))
                keep.push_back(n);
        fields[i]->show_notes(keep);
    }
    std::pair<std::optional<QImage>, Notes> got;
    try {
        if (e.is_event()) {
            const QString kind = e.province() ? u"province"_s : e.major ? u"major"_s : u"country"_s;
            got = render_event(*look, kind, shown[0], shown[1], shown.mid(2),
                               !e.picture.isEmpty() ? OptStr(e.picture) : std::nullopt, smp);
        } else {
            got = render_decision(*look, shown[0], shown[1], !e.picture.isEmpty() ? e.picture : e.key, smp);
        }
    } catch (const std::exception &ex) {          // (a layout file the mod changed in a way not foreseen)
        got = {std::nullopt, {{u"warn"_s, u"The preview could not be drawn: %1"_s.arg(QString::fromUtf8(ex.what()))}}};
    }
    _img = got.first;
    _fit();
    QStringList parts;
    for (const auto &[s, t] : got.second)
        parts << u"<span style='color:%1; font-weight:600'>%2%3</span>"_s.arg(
            s == u"error" ? theme::ERROR_ : s == u"info" ? theme::INK_DIM : theme::WARN,
            s == u"warn" ? u"⚠ "_s : QString(), t);
    notes->setText(parts.join(u"<br>"_s));
}

void TextsWindow::_fit()
{
    if (!_img) {
        view->setPixmap(QPixmap());
        return;
    }
    QPixmap pm = QPixmap::fromImage(*_img);
    if (pm.width() > view->width() || pm.height() > view->height())
        pm = pm.scaled(view->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    view->setPixmap(pm);
}

void TextsWindow::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    _fit();
}

}  // namespace locwin
