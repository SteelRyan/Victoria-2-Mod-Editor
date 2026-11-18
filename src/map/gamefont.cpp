// gamefont.py — line by line.
#include "map/gamefont.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace gamefont {

const Codes COLOURS = {{u"W"_s, {255, 255, 255}}, {u"B"_s, {0, 0, 255}}, {u"G"_s, {0, 159, 3}}, {u"R"_s, {255, 50, 50}},
                       {u"b"_s, {0, 0, 0}}, {u"g"_s, {176, 176, 176}}, {u"Y"_s, {255, 189, 0}}};
const OrderedMap<QString, QString> COLOUR_NAMES = {{u"W"_s, u"white"_s}, {u"Y"_s, u"yellow"_s}, {u"R"_s, u"red"_s},
                                                   {u"G"_s, u"green"_s}, {u"B"_s, u"blue"_s}, {u"b"_s, u"black"_s},
                                                   {u"g"_s, u"grey"_s}};
static const auto RX = QRegularExpression::UseUnicodePropertiesOption;
static bool NUM(const QString &s)
{
    static const QRegularExpression re(uR"(^[+-]?(\d+\.?\d*|\.\d+)$)"_s, RX);
    return re.match(s).hasMatch();
}

static OptStr val_of(const script::Node &node, const QString &name)
{
    for (const script::Node &c : node.kids)
        if (c.has_key && !c.key.isEmpty() && py::lower(c.key) == name && !c.is_block)
            return c.value;
    return std::nullopt;
}
static const script::Node *kid_of(const script::Node &node, const QString &name)
{
    for (const script::Node &c : node.kids)
        if (c.has_key && !c.key.isEmpty() && py::lower(c.key) == name)
            return &c;
    return nullptr;
}

// ---------------------------------------------------------------------------------------------- one letter sheet
static OrderedMap<QString, QString> pairs_of(const QString &ln)
{
    static const QRegularExpression re(uR"((\w+)=(-?\d+))"_s, RX);
    OrderedMap<QString, QString> v;
    for (auto it = re.globalMatch(ln); it.hasNext();) {
        const auto m = it.next();
        v.insert(m.captured(1), m.captured(2));
    }
    return v;
}

Font::Font(const QString &fnt_path, fourk::Pic sheet_) : sheet(std::move(sheet_))
{
    for (const QString &ln : py::splitlines(script::read_text(fnt_path))) {
        if (ln.startsWith(u"char ")) {
            const auto v = pairs_of(ln);
            const QString *id = v.get(u"id"_s);
            if (!id)
                throw std::out_of_range("'id'");
            std::array<long long, 7> c{};
            const QString keys[7] = {u"x"_s, u"y"_s, u"width"_s, u"height"_s, u"xoffset"_s, u"yoffset"_s, u"xadvance"_s};
            for (int k = 0; k < 7; ++k)
                c[size_t(k)] = py::to_int(v.value(keys[k], u"0"_s));
            chars[py::to_int(*id)] = c;
        } else if (ln.startsWith(u"common ")) {
            const auto v = pairs_of(ln);
            line = py::to_int(v.value(u"lineHeight"_s, u"16"_s));
            base = py::to_int(v.value(u"base"_s, u"12"_s));
        }
    }
}

int Font::code(QChar ch)
{
    // ch.encode("cp1252")[0] — 63 where Windows-1252 has no byte for it (U+0080 … U+009F among them)
    const char16_t u = ch.unicode();
    if (u < 0x80 || (u >= 0xA0 && u <= 0xFF))
        return u;
    static const std::pair<char16_t, int> SPECIAL[] = {
        {0x20AC, 0x80}, {0x201A, 0x82}, {0x0192, 0x83}, {0x201E, 0x84}, {0x2026, 0x85}, {0x2020, 0x86}, {0x2021, 0x87},
        {0x02C6, 0x88}, {0x2030, 0x89}, {0x0160, 0x8A}, {0x2039, 0x8B}, {0x0152, 0x8C}, {0x017D, 0x8E}, {0x2018, 0x91},
        {0x2019, 0x92}, {0x201C, 0x93}, {0x201D, 0x94}, {0x2022, 0x95}, {0x2013, 0x96}, {0x2014, 0x97}, {0x02DC, 0x98},
        {0x2122, 0x99}, {0x0161, 0x9A}, {0x203A, 0x9B}, {0x0153, 0x9C}, {0x017E, 0x9E}, {0x0178, 0x9F}};
    for (const auto &[c, b] : SPECIAL)
        if (c == u)
            return b;
    return 63;
}

const std::array<long long, 7> *Font::glyph(QChar ch) const
{
    if (auto it = chars.find(code(ch)); it != chars.end())
        return &it->second;
    if (auto it = chars.find(63); it != chars.end())
        return &it->second;
    return nullptr;
}

long long Font::advance(QChar ch) const
{
    const auto *c = glyph(ch);
    return c ? (*c)[6] : 0;
}

long long Font::width(const QString &text) const
{
    long long s = 0;
    for (QChar ch : text)
        s += advance(ch);
    return s;
}

const QImage &Font::tinted(const Rgb &rgb)
{
    // The sheet in one colour (the letters are white, or grey for soft edges, on see-through).
    auto it = _tinted.find(rgb);
    if (it == _tinted.end()) {
        fourk::Pic a = sheet;
        const long long n = a.h * a.w;
        std::vector<float> shade(static_cast<size_t>(n));
        bool lit = false;
        float mn = 0;
        for (long long i = 0; i < n; ++i) {
            const float s = float(std::max({a.d[size_t(i * 4)], a.d[size_t(i * 4 + 1)], a.d[size_t(i * 4 + 2)]})) / 255.0f;
            shade[size_t(i)] = s;
            if (a.d[size_t(i * 4 + 3)] > 0) {
                mn = lit ? std::min(mn, s) : s;
                lit = true;
            }
        }
        const bool flat = lit ? double(mn) > 0.98 : true;
        const float c[3] = {float(std::get<0>(rgb)), float(std::get<1>(rgb)), float(std::get<2>(rgb))};
        for (long long i = 0; i < n; ++i)
            for (int k = 0; k < 3; ++k)
                a.d[size_t(i * 4 + k)] = static_cast<std::uint8_t>(flat ? c[k] * 1.0f : c[k] * shade[size_t(i)]);
        it = _tinted.emplace(rgb, fourk::to_qimage(a)).first;
    }
    return it->second;
}

double Font::draw(QPainter &p, double x, double y, const QString &text, const Rgb &rgb)
{
    const QImage &img = tinted(rgb);
    for (QChar ch : text) {
        const auto *c = glyph(ch);
        if (!c)
            continue;
        if ((*c)[2] && (*c)[3])
            p.drawImage(QPointF(x + double((*c)[4]), y + double((*c)[5])), img,
                        QRectF(double((*c)[0]), double((*c)[1]), double((*c)[2]), double((*c)[3])));
        x += double((*c)[6]);
    }
    return x;
}

// ---------------------------------------------------------------------------------------------- the fonts
QString Fonts::_text(const QString &rel)
{
    if (store)
        return store->text(rel);
    return script::read_text(mod.p(rel.split(u'/')));
}

static long long int16(const QString &s0)
{
    // int(s, 16): whitespace, a sign, an optional 0x, hex digits with single underscores between them
    const QString s = py::strip(s0);
    qsizetype i = 0;
    bool neg = false;
    if (i < s.size() && (s[i] == u'+' || s[i] == u'-'))
        neg = s[i++] == u'-';
    bool prefix = false;
    if (i + 1 < s.size() && s[i] == u'0' && (s[i + 1] == u'x' || s[i + 1] == u'X')) {
        i += 2;
        prefix = true;
    }
    long long v = 0;
    bool any = false, prev_us = false;
    for (; i < s.size(); ++i) {
        if (s[i] == u'_') {                          // (between digits, or once right after "0x")
            if (prev_us || (!any && !prefix))
                throw py::ValueError("invalid literal for int() with base 16");
            prev_us = true;
            continue;
        }
        const int d = QChar(s[i]).isDigit() && s[i].unicode() < 0x80 ? s[i].unicode() - u'0'
                      : (s[i] >= u'a' && s[i] <= u'f')              ? s[i].unicode() - u'a' + 10
                      : (s[i] >= u'A' && s[i] <= u'F')              ? s[i].unicode() - u'A' + 10
                                                                    : -1;
        if (d < 0)
            throw py::ValueError("invalid literal for int() with base 16");
        v = v * 16 + d;
        any = true;
        prev_us = false;
    }
    if (!any || prev_us)
        throw py::ValueError("invalid literal for int() with base 16");
    return neg ? -v : v;
}

void Fonts::load()
{
    table.emplace();
    auto items = fourk::Files(mod).tree(u"interface"_s);
    std::vector<std::pair<QString, QString>> sorted(items.begin(), items.end());
    std::sort(sorted.begin(), sorted.end());
    for (const auto &[rel, full] : sorted) {
        if (!rel.endsWith(u".gfx") || rel.count(u'/') != 1)
            continue;
        script::Nodes tops;
        try {
            tops = script::parse(_text(rel));
        } catch (const std::exception &) {
            continue;
        }
        for (const script::Node &top : tops) {
            if (!top.is_block || !top.has_key || py::lower(top.key) != u"bitmapfonts")
                continue;
            for (const script::Node &bf : top.kids) {
                if (!bf.is_block)
                    continue;
                const OptStr name = val_of(bf, u"name"_s), fname = val_of(bf, u"fontname"_s);
                if (!name || name->isEmpty() || !fname || fname->isEmpty())
                    continue;
                OptStr col = val_of(bf, u"color"_s);
                if (!col || col->isEmpty())
                    col = u"0xffffffff"_s;
                long long argb;
                try {
                    argb = int16(*col);
                } catch (const py::ValueError &) {
                    argb = 0xffffffff;
                }
                Codes codes;
                const script::Node *cc = kid_of(bf, u"colorcodes"_s);
                if (cc && cc->is_block)
                    for (const script::Node &c : cc->kids)
                        if (c.has_key && !c.key.isEmpty() && c.is_block) {
                            std::vector<long long> n;
                            for (const script::Node &x : c.kids)
                                if (!x.is_block && NUM(x.value))
                                    n.push_back(py::trunc_int(*py::to_float(x.value)));
                            if (n.size() >= 3)
                                codes.insert(c.key, {int(n[0]), int(n[1]), int(n[2])});
                        }
                table->insert(py::lower(*name), {*fname, {int((argb >> 16) & 255), int((argb >> 8) & 255), int(argb & 255)}, codes});
            }
        }
    }
}

std::optional<Fonts::Got> Fonts::get(const OptStr &name)
{
    // (Font, colour, colour codes) of a font the interface names — or of the plainest one there is.
    if (!table)
        load();
    const FontEnt *ent = table->get(py::lower(name.value_or(QString())));
    if (!ent)
        ent = table->get(u"arial12"_s);
    if (!ent && !table->isEmpty())
        ent = &table->begin()->second;
    if (!ent)
        return std::nullopt;
    const QString key = py::lower(ent->fname);
    std::shared_ptr<Font> f = _fonts.value(key);
    if (!f) {
        const QString fnt = mod.p({u"gfx"_s, u"fonts"_s, ent->fname + u".fnt"_s});
        OptStr sheet;
        for (const QString e : {u".tga"_s, u".dds"_s}) {
            const QString p = mod.p({u"gfx"_s, u"fonts"_s, ent->fname + e});
            if (fs::isfile(p)) {
                sheet = p;
                break;
            }
        }
        if (!fs::isfile(fnt) || !sheet)
            return std::nullopt;
        f = std::make_shared<Font>(fnt, fourk::read_picture(*sheet).first);
        _fonts.insert(key, f);
    }
    return Got{f.get(), ent->rgb, ent->codes.isEmpty() ? COLOURS : ent->codes};
}

const std::optional<QImage> &Fonts::flag(const QString &tag, long long height)
{
    // A nation's flag at the height of a line (gfx/flags/TAG.tga) or None.
    const auto key = std::make_pair(py::upper(tag), height);
    auto it = _flags.find(key);
    if (it == _flags.end()) {
        std::optional<QImage> img;
        const QString path = mod.p({u"gfx"_s, u"flags"_s, py::upper(tag) + u".tga"_s});
        if (fs::isfile(path)) {
            try {
                const QImage q = fourk::to_qimage(fourk::read_picture(path).first);
                img = q.scaledToHeight(int(std::max(6LL, height - 2)), Qt::SmoothTransformation);
            } catch (const std::exception &) {
                img = std::nullopt;
            }
        }
        it = _flags.emplace(key, img).first;
    }
    return it->second;
}

// ---------------------------------------------------------------------------------------------- layout and drawing
std::optional<Layout> layout(Fonts &fonts, const OptStr &font_name, const QString &text, double max_width,
                             const OrderedMap<QString, QString> &samples)
{
    // The text broken into lines as the game does it; None when the font is not there.
    const auto got = fonts.get(font_name);
    if (!got)
        return std::nullopt;
    Font &font = *got->font;
    const Rgb base = got->rgb;
    const Codes &codes = got->codes;
    Rgb colour = base;
    std::vector<Word> words;                         // ("nl") | ("sp") | text | flag
    auto add_text = [&](const QString &s, const Rgb &col) {
        const QStringList parts = s.split(u' ');
        for (qsizetype i = 0; i < parts.size(); ++i) {
            if (i)
                words.push_back({u"sp"_s, u" "_s, {}, col, font.advance(u' ')});
            if (!parts[i].isEmpty())
                words.push_back({u"t"_s, parts[i], {}, col, font.width(parts[i])});
        }
    };
    static const QRegularExpression TOKEN(uR"(§(.)|\\n|\n|@([A-Za-z0-9]{3})|\$([A-Za-z_0-9]+)\$)"_s, RX);
    qsizetype pos = 0;
    for (auto it = TOKEN.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        if (m.capturedStart() > pos)
            add_text(text.mid(pos, m.capturedStart() - pos), colour);
        pos = m.capturedEnd();
        const QString tok = m.captured(0);
        if (tok.startsWith(u"§")) {
            const QString c = m.captured(1);
            colour = c == u"!" ? base : codes.value(c, colour);
        } else if (tok == u"\\n" || tok == u"\n")
            words.push_back({u"nl"_s, {}, {}, {}, 0});
        else if (tok.startsWith(u'@')) {
            const auto &img = fonts.flag(m.captured(2), font.line);
            if (img)
                words.push_back({u"f"_s, {}, *img, colour, img->width() + 2});
            else
                add_text(tok, colour);
        } else
            add_text(samples.value(py::upper(m.captured(3)), tok), colour);
    }
    if (pos < text.size())
        add_text(text.mid(pos), colour);
    std::vector<Line> lines;
    Line cur;
    long long w = 0;
    for (const Word &wd : words) {
        if (wd.kind == u"nl") {
            lines.push_back(cur);
            cur.clear();
            w = 0;
            continue;
        }
        if (wd.kind == u"sp") {
            if (!cur.empty()) {
                cur.push_back(wd);
                w += wd.width;
            }
            continue;
        }
        if (!cur.empty() && max_width != 0 && double(w + wd.width) > max_width) {
            while (!cur.empty() && cur.back().kind == u"sp")
                cur.pop_back();
            lines.push_back(cur);
            cur.clear();
            w = 0;
        }
        cur.push_back(wd);
        w += wd.width;
    }
    lines.push_back(cur);
    long long widest = 0;
    for (Line &ln : lines) {
        while (!ln.empty() && ln.back().kind == u"sp")
            ln.pop_back();
        long long s = 0;
        for (const Word &x : ln)
            s += x.width;
        widest = std::max(widest, s);
    }
    return Layout{&font, std::move(lines), widest};
}

std::tuple<long long, long long, long long> draw(QPainter &p, Fonts &fonts, const OptStr &font_name, const QString &text,
                                                 double x, double y, double max_width, double max_height,
                                                 const QString &fmt0, bool clip, const OrderedMap<QString, QString> &samples)
{
    // Draw a text at x, y into a box max_width wide.
    const auto got = layout(fonts, font_name, text, max_width, samples);
    if (!got)
        return {0, 0, 0};
    Font &font = *got->font;
    const auto &lines = got->lines;
    const long long n = static_cast<long long>(lines.size());
    const long long fits = max_height != 0 ? std::max(1LL, static_cast<long long>(std::floor(max_height / double(font.line)))) : n;
    const long long over = std::max(0LL, n - fits);
    const long long shown = clip ? std::min(fits, n) : n;
    const QString fmt = py::lower(fmt0.isEmpty() ? u"left"_s : fmt0);
    for (long long i = 0; i < shown; ++i) {
        const Line &ln = lines[size_t(i)];
        long long w = 0;
        for (const Word &t : ln)
            w += t.width;
        double px = x + ((fmt == u"centre" || fmt == u"center") && max_width != 0 ? (max_width - double(w)) / 2.0
                         : fmt == u"right" && max_width != 0                   ? (max_width - double(w))
                                                                               : 0.0);
        const double py_ = y + double(i * font.line);
        for (const Word &wd : ln) {
            if (wd.kind == u"f") {
                p.drawImage(QPointF(px, py_ + double(font.line - wd.img.height()) / 2.0), wd.img);
                px += double(wd.width);
            } else
                px = font.draw(p, px, py_, wd.text, wd.colour);
        }
    }
    return {n, over, shown * font.line};
}

std::vector<std::pair<QString, QString>> check(const QString &text, const QStringList &known_vars,
                                               const QStringList &known_tags)
{
    std::vector<std::pair<QString, QString>> out;
    if (text.contains(u';'))
        out.emplace_back(u"error"_s, u"A semicolon ( ; ) ends the text in the game's files — take it out."_s);
    static const QRegularExpression sect(u"§(.?)"_s, RX);
    for (auto it = sect.globalMatch(text); it.hasNext();) {
        const QString c = it.next().captured(1);
        if (c != u"!" && !COLOURS.contains(c))
            out.emplace_back(u"warn"_s, u"§%1 is no colour — the game knows §W §Y §R §G §B §b §g and §! (back to normal)."_s.arg(c));
    }
    if (text.count(u'$') % 2)
        out.emplace_back(u"warn"_s, u"A $ stands alone — a filled-in word is written $NAME$."_s);
    QSet<QString> known, tags;
    for (const QString &k : known_vars)
        known.insert(py::upper(k));
    for (const QString &t : known_tags)
        tags.insert(py::upper(t));
    static const QRegularExpression var(uR"(\$([A-Za-z_0-9]+)\$)"_s, RX);
    for (auto it = var.globalMatch(text); it.hasNext();) {
        const QString g = it.next().captured(1);
        if (!known.isEmpty() && !known.contains(py::upper(g)))
            out.emplace_back(u"warn"_s,
                             u"$%1$ is not in the list of words the game fills in — it may be shown as written."_s.arg(g));
    }
    static const QRegularExpression at(uR"(@([A-Za-z0-9]{3}))"_s, RX);
    for (auto it = at.globalMatch(text); it.hasNext();) {
        const QString g = it.next().captured(1);
        if (!tags.isEmpty() && !tags.contains(py::upper(g)))
            out.emplace_back(u"warn"_s, u"@%1: there is no nation %2 — no flag is drawn."_s.arg(g, py::upper(g)));
    }
    static const QRegularExpression bs(uR"((?<!\\)\\(?!n))"_s, RX);
    if (bs.match(text).hasMatch())
        out.emplace_back(u"warn"_s, u"A \\ that is not \\n (new line) is shown as it is."_s);
    std::vector<std::pair<QString, QString>> seen;
    for (const auto &x : out)
        if (std::find(seen.begin(), seen.end(), x) == seen.end())
            seen.push_back(x);
    return seen;
}

}  // namespace gamefont
