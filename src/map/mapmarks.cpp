// mapmarks.py — see mapmarks.h.
#include "map/mapmarks.h"

#include "core/pynum.h"
#include "core/pytext.h"
#include "core/script.h"
#include "core/scriptstore.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <exception>
#include <set>
#include <tuple>

using namespace Qt::Literals::StringLiterals;

namespace mapmarks {

const QString GUI = u"interface/mapicons.gui"_s;
const QString GFX = u"interface/mapicons.gfx"_s;
const QString PIC_DIR = u"gfx/interface/mapmarks"_s;
const QStringList SHARED = {u"GFX_unit_attr"_s, u"GFX_mobilization_progress"_s};
const QString MAP_SUFFIX = u"_mapmark"_s;

static const QRegularExpression &TEX_RE()
{
    static const QRegularExpression re(uR"re((texture[Ff]ile\d?\s*=\s*")([^"]+)("))re"_s,
                                       QRegularExpression::UseUnicodePropertiesOption);
    return re;
}
static const QRegularExpression &FONT_RE()
{
    static const QRegularExpression re(uR"re((\bfont\s*=\s*")([^"]+)("))re"_s, QRegularExpression::CaseInsensitiveOption |
                                                                                   QRegularExpression::UseUnicodePropertiesOption);
    return re;
}
static const QRegularExpression &SIZE_RE()           // (\d+)$ — searched: the digits the name ends in
{
    static const QRegularExpression re(uR"((\d+)$)"_s, QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

// re.sub(pattern, fn, text): every match replaced by fn(match)
static QString sub(const QRegularExpression &re, const QString &text,
                   const std::function<QString(const QRegularExpressionMatch &)> &fn)
{
    QString out;
    qsizetype at = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(at, m.capturedStart(0) - at);
        out += fn(m);
        at = m.capturedEnd(0);
    }
    out += text.mid(at);
    return out;
}

OrderedMap<QString, long long> fonts_of(const QStringList &texts)
{
    static const QRegularExpression re(uR"re(bitmapfont\s*=\s*\{[^{}]*?name\s*=\s*"([^"]+)")re"_s,
                                       QRegularExpression::CaseInsensitiveOption |
                                                                                   QRegularExpression::UseUnicodePropertiesOption);
    OrderedMap<QString, long long> out;
    for (const QString &text : texts) {
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const QString name = it.next().captured(1);
            const QRegularExpressionMatch n = SIZE_RE().match(name);
            if (n.hasMatch())
                out.insert(name, py::to_int(n.captured(1)));
        }
    }
    return out;
}

static QString family(const QString &name, qsizetype digits_at)   // name[:m.start()].lower().rstrip("_")
{
    QString f = py::lower(name.left(digits_at));
    while (f.endsWith(u'_'))
        f.chop(1);
    return f;
}

QString font_for(const QString &name, double factor, const OrderedMap<QString, long long> &fonts)
{
    const QRegularExpressionMatch m = SIZE_RE().match(name);
    if (!m.hasMatch() || std::abs(factor - 1.0) < 1e-9)
        return name;
    const double want = double(py::to_int(m.captured(1))) * factor;
    const QString fam = family(name, m.capturedStart(1));
    struct Cand {
        bool other;
        double dist;
        QString name;
        long long px;
    };
    std::vector<Cand> cands;
    for (const auto &[k, px] : fonts)
        cands.push_back({family(k, SIZE_RE().match(k).capturedStart(1)) != fam, std::abs(double(px) - want), k, px});
    std::stable_sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b) {
        return std::tie(a.other, a.dist, a.name) < std::tie(b.other, b.dist, b.name);
    });
    // (a font with another family only when it is clearly nearer in size)
    std::pair<QString, long long> best = cands.empty() ? std::pair{name, py::to_int(m.captured(1))}
                                                       : std::pair{cands[0].name, cands[0].px};
    std::pair<QString, long long> near_ = best;
    if (!cands.empty()) {
        const Cand *mn = &cands[0];
        for (const Cand &c : cands)                  // min(…, key=(abs(px - want), name)): the first of the least
            if (std::tie(c.dist, c.name) < std::tie(mn->dist, mn->name))
                mn = &c;
        near_ = {mn->name, mn->px};
    }
    if (std::abs(double(near_.second) - want) + 2 < std::abs(double(best.second) - want))
        best = near_;
    return best.first;
}

OrderedMap<QString, long long> frames_of(const QString &text)
{
    OrderedMap<QString, long long> out;
    for (const script::Node &top : script::parse(text)) {
        if (!top.is_block)
            continue;
        for (const script::Node &sp : top.kids) {
            if (!sp.is_block)
                continue;
            const fourk::OptStr n = fourk::val_of(sp, u"noofframes"_s);
            for (const QString &key : fourk::TEXTURE_KEYS) {
                const fourk::OptStr t = fourk::val_of(sp, key);
                if (t && !t->isEmpty())
                    out.insert(*t, n && !n->isEmpty() && fourk::NUM(*n) ? py::trunc_int(*py::to_float(*n)) : 1);
            }
        }
    }
    return out;
}

fourk::Pic scale_picture(const fourk::Pic &rgba, double factor, long long frames)
{
    const long long h = rgba.h, w = rgba.w;
    const long long nh = std::max<long long>(1, py::round_int(double(h) * factor));
    if (frames > 1 && w % frames == 0) {
        const long long fw = w / frames;
        const long long nfw = std::max<long long>(1, py::round_int(double(fw) * factor));
        fourk::Pic out(nh, nfw * frames, rgba.ch);
        for (long long i = 0; i < frames; ++i) {     // np.concatenate([...], axis=1)
            const fourk::Pic part = fourk::resized(rgba.part(0, h, i * fw, (i + 1) * fw), nfw, nh);
            for (long long y = 0; y < nh; ++y)
                std::copy_n(part.d.begin() + (y * nfw * part.ch), nfw * part.ch,
                            out.d.begin() + ((y * out.w + i * nfw) * out.ch));
        }
        return out;
    }
    return fourk::resized(rgba, std::max<long long>(1, py::round_int(double(w) * factor)), nh);
}

QString _copy_shared(const QString &gfx_text)
{
    const script::Nodes tree = script::parse(gfx_text);
    QStringList adds;
    std::optional<qsizetype> end;
    for (const script::Node &top : tree) {
        if (top.is_block && py::lower(top.has_key ? top.key : QString()) == u"spritetypes") {
            end = top.vend - 1;                      // (the closing brace of spriteTypes)
            for (const script::Node &sp : top.kids) {
                if (!sp.is_block)
                    continue;
                const fourk::OptStr nm = fourk::val_of(sp, u"name"_s);
                if (nm && SHARED.contains(*nm)) {
                    QString block = gfx_text.mid(sp.kstart, sp.vend - sp.kstart);
                    const QString was = u"\"%1\""_s.arg(*nm);
                    const qsizetype at = block.indexOf(was);
                    if (at >= 0)                     // .replace(…, 1)
                        block.replace(at, was.size(), u"\"%1%2\""_s.arg(*nm, MAP_SUFFIX));
                    adds << block;
                }
            }
        }
    }
    if (!end || adds.isEmpty())
        return gfx_text;
    return gfx_text.left(*end) + u"\n\t"_s + adds.join(u"\n\n\t"_s) + u"\n"_s + gfx_text.mid(*end);
}

static QString slashes(const QString &path)          // path.replace("\\\\", "/").replace("\\", "/")
{
    QString p = path;
    p.replace(u"\\\\"_s, u"/"_s);
    p.replace(u'\\', u'/');
    return p;
}

Made make(const QString &gui_text, const QString &gfx_text, const Find &find, double factor,
          const OrderedMap<QString, long long> &fonts)
{
    Made out;
    // .gfx: the shared sprites copied, the textures pointed at the map's own copies, the sizes scaled
    QString gfx = _copy_shared(gfx_text);
    const OrderedMap<QString, long long> frames = frames_of(gfx);
    gfx = sub(TEX_RE(), gfx, [&](const QRegularExpressionMatch &m) -> QString {
        const QString path = m.captured(2);
        const QString base = slashes(path).split(u'/').last();
        const QString rel = PIC_DIR + u"/"_s + base;
        if (!out.pics.contains(rel)) {
            const std::optional<QString> full = find(path);
            if (!full) {
                out.notes << u"%1: not found — left as it is"_s.arg(path);
                return m.captured(0);
            }
            try {
                auto [rgba, info] = fourk::read_picture(*full);
                auto pd = std::make_shared<moreviews::PicData>();
                pd->rgba = scale_picture(rgba, factor, frames.value(m.captured(2), 1));
                pd->info = info;
                out.pics.insert(rel, pd);
            } catch (const std::exception &e) {      // (reported; the rest goes on)
                out.notes << u"%1: %2"_s.arg(path, QString::fromStdString(e.what()));
                return m.captured(0);
            }
        }
        QString r = rel;
        r.replace(u"/"_s, u"\\\\"_s);
        return m.captured(1) + r + m.captured(3);
    });
    gfx = fourk::scale_script(gfx, false, factor, true).text;
    // .gui: positions and sizes, the shared sprites' map copies, the font
    QString gui = fourk::scale_script(gui_text, true, factor, true).text;
    for (const QString &nm : SHARED)
        gui.replace(u"\"%1\""_s.arg(nm), u"\"%1%2\""_s.arg(nm, MAP_SUFFIX));
    std::map<QString, QString> swapped;               // (sorted(swapped.items()))
    gui = sub(FONT_RE(), gui, [&](const QRegularExpressionMatch &m) -> QString {
        const QString fresh = font_for(m.captured(2), factor, fonts);
        if (fresh != m.captured(2))
            swapped[m.captured(2)] = fresh;
        return m.captured(1) + fresh + m.captured(3);
    });
    for (const auto &[a, b] : swapped)
        out.notes << u"font %1 → %2"_s.arg(a, b);
    out.gui = gui;
    out.gfx = gfx;
    return out;
}

QStringList originals(const moddata::Mod &mod, scriptstore::ScriptStore *store)
{
    QStringList out;
    QString pic_dir = PIC_DIR;
    pic_dir.replace(u"/"_s, u"\\\\"_s);
    for (const QString &rel : {GUI, GFX}) {
        std::optional<QString> text;
        if (store)
            text = store->text(rel);
        if (!text || text->contains(pic_dir) || text->contains(MAP_SUFFIX)) {
            // os.path.join(mod.base or "", *rel.split("/")): with no game folder, the path from the current folder
            const QString b = mod.base.value_or(QString());
            const QString base = b.isEmpty() ? rel : b + u"/"_s + rel;
            try {
                text = script::read_text(base);
            } catch (const std::exception &) {
                text = text.value_or(QString());
            }
        }
        out << *text;
    }
    return out;
}

}  // namespace mapmarks
