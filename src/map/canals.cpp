// canals.py — line by line.
#include "map/canals.h"
#include "core/entryview_data.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"
#include "map/imgio.h"

#include <QFile>
#include <QPainterPath>
#include <QPolygonF>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

using namespace Qt::StringLiterals;

namespace canals {

const QString SPRITE_DIR = u"map/province_flag_sprites"_s;
const QString OWN_DECISIONS = u"decisions/ModEditor_canals.txt"_s;     // the files the editor writes the openings into
const QString OWN_EVENTS = u"events/ModEditor_canals.txt"_s;
const QStringList OPEN_KINDS = {u"start"_s, u"decision"_s, u"none"_s};
const OrderedMap<QString, QString> OPEN_NAMES = {{u"start"_s, u"Open from the start (start-up event)"_s},
                                                 {u"decision"_s, u"Opened by a decision"_s},
                                                 {u"none"_s, u"Not opened by the editor"_s}};
const double MODEL_UNIT = 1.0;
const double OFFSET_UNIT = 1.0;
const bool OFFSET_Y_NORTH = true;
const double WATER_Y = -0.2;

static const auto RX = QRegularExpression::UseUnicodePropertiesOption;

// ---------------------------------------------------------------------------------------------- the 3D models
std::vector<PosTris> read_xac(const QString &path)
{
    std::vector<PosTris> out;
    for (Mesh &m : read_meshes(path))
        out.emplace_back(std::move(m.pos), std::move(m.tris));
    return out;
}

namespace {
struct Bytes {                                   // struct.unpack_from, little-endian, raising past the end
    const QByteArray &d;
    struct Error {};
    long long size() const { return d.size(); }
    std::uint32_t u32(long long at) const
    {
        if (at < 0 || at + 4 > size())
            throw Error{};
        std::uint32_t v;
        std::memcpy(&v, d.constData() + at, 4);
        return v;
    }
    std::uint8_t u8(long long at) const
    {
        if (at < 0 || at + 1 > size())
            throw Error{};
        return std::uint8_t(d[at]);
    }
    double f32(long long at) const
    {
        if (at < 0 || at + 4 > size())
            throw Error{};
        float v;
        std::memcpy(&v, d.constData() + at, 4);
        return double(v);
    }
};
}  // namespace

std::vector<Mesh> read_meshes(const QString &path)
{
    // The meshes with what the picture needs.
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw fs::OSError(u"[Errno 2] No such file or directory: %1"_s.arg(py::repr_str(path)).toStdString(), 2);
    const QByteArray raw = f.readAll();
    const Bytes d{raw};
    std::vector<Mesh> out;
    long long q = 8;
    while (q < d.size() - 40) {
        const long long cid = d.u32(q), sz = d.u32(q + 4), ver = d.u32(q + 8);
        if (cid != 1 || ver != 1 || !(100 < sz && sz <= d.size() - q - 12)) {
            q += 1;
            continue;
        }
        const long long nv = d.u32(q + 20), ni = d.u32(q + 24), nsub = d.u32(q + 28), nlay = d.u32(q + 32);
        if (!(0 < nv && nv < 200000 && 0 < ni && ni < 2000000 && 0 < nlay && nlay < 20 && 0 < nsub && nsub < 100)) {
            q += 1;
            continue;
        }
        long long r = q + 40;
        std::optional<std::vector<V3>> pos, nrm;
        std::optional<std::vector<V2>> uv;
        std::vector<Tri> tris;
        try {
            for (long long l = 0; l < nlay; ++l) {
                const long long t = d.u32(r), asz = d.u32(r + 4);
                d.u8(r + 8);
                d.u8(r + 9);
                if (r + 12 > d.size())                   // ("<IIBBxx": 12 bytes)
                    throw Bytes::Error{};
                auto three = [&] {
                    std::vector<V3> v;
                    v.reserve(size_t(nv));
                    for (long long i = 0; i < nv; ++i)
                        v.push_back({d.f32(r + 12 + 12 * i), d.f32(r + 16 + 12 * i), d.f32(r + 20 + 12 * i)});
                    return v;
                };
                if (t == 0 && asz == 12)
                    pos = three();
                else if (t == 1 && asz == 12 && !nrm)
                    nrm = three();
                else if (t == 3 && asz == 8 && !uv) {   // (the first set of texture coordinates)
                    std::vector<V2> v;
                    v.reserve(size_t(nv));
                    for (long long i = 0; i < nv; ++i)
                        v.push_back({d.f32(r + 12 + 8 * i), d.f32(r + 16 + 8 * i)});
                    uv = std::move(v);
                }
                r += 12 + asz * nv;
            }
            for (long long s = 0; s < nsub; ++s) {
                const long long sni = d.u32(r);
                d.u32(r + 4);
                d.u32(r + 8);
                const long long nb = d.u32(r + 12);
                r += 16;
                if (r + 4 * sni > d.size())
                    throw Bytes::Error{};
                std::vector<long long> idx(static_cast<size_t>(sni));
                for (long long i = 0; i < sni; ++i)
                    idx[size_t(i)] = d.u32(r + 4 * i);
                r += 4 * sni + 4 * nb;
                for (long long i = 0; i < sni - 2; i += 3)
                    tris.push_back({idx[size_t(i)], idx[size_t(i + 1)], idx[size_t(i + 2)]});
            }
        } catch (const Bytes::Error &) {
            q += 1;
            continue;
        }
        bool fits = pos && !pos->empty() && !tris.empty();
        if (fits) {
            long long top = 0;
            for (const Tri &t : tris)
                top = std::max(top, *std::max_element(t.begin(), t.end()));
            fits = top < static_cast<long long>(pos->size());
        }
        if (fits) {
            out.push_back({std::move(*pos), std::move(nrm), std::move(uv), std::move(tris)});
            q = r;
        } else
            q += 1;
    }
    return out;
}

OptStr texture_name(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    static const QRegularExpression re(uR"(([A-Za-z0-9_]+_[Dd]iffuse))"_s);
    const auto m = re.match(QString::fromLatin1(f.readAll()));
    return m.hasMatch() ? OptStr(m.captured(1)) : std::nullopt;
}

std::optional<Render> render_top(const std::vector<Mesh> &meshes, const std::optional<QImage> &tex, long long ppu,
                                 bool flip_v)
{
    // The model seen from straight above, as the game draws it (its texture, lit from above; untextured: stone and
    // water colours). (v as stored: the water strip lands on the bed)
    bool any = false;
    double minx = 0, maxx = 0, minz = 0, maxz = 0;
    for (const Mesh &m : meshes)
        for (const V3 &p : m.pos) {
            if (!any) {
                minx = maxx = p[0];
                minz = maxz = p[2];
                any = true;
            }
            minx = std::min(minx, p[0]);
            maxx = std::max(maxx, p[0]);
            minz = std::min(minz, p[2]);
            maxz = std::max(maxz, p[2]);
        }
    if (!any)
        return std::nullopt;
    const double fp = double(ppu);
    const long long W = static_cast<long long>(std::ceil((maxx - minx) * fp)) + 1;
    const long long H = static_cast<long long>(std::ceil((maxz - minz) * fp)) + 1;
    std::vector<double> rgb(size_t(H * W * 3), 0.0);
    std::vector<double> depth(size_t(H * W), -1e9);
    grid::Grid<std::uint8_t> kind(H, W, 0);
    // the texture as numbers (T[y, x, c])
    std::vector<double> T;
    long long TH = 0, TW = 0;
    if (tex && !tex->isNull()) {
        const QImage t = tex->convertToFormat(QImage::Format_RGB888);
        TH = t.height();
        TW = t.width();
        T.resize(size_t(TH * TW * 3));
        for (long long y = 0; y < TH; ++y) {
            const uchar *row = t.constScanLine(int(y));
            for (long long i = 0; i < TW * 3; ++i)
                T[size_t(y * TW * 3 + i)] = double(row[i]);
        }
    }
    const bool textured = !T.empty();
    double light[3] = {0.35, 1.0, 0.25};
    {
        const double nrm = std::sqrt(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
        for (double &c : light)
            c /= nrm;
    }
    for (const Mesh &m : meshes) {
        const auto &P = m.pos;
        const bool hasN = m.nrm && !m.nrm->empty(), hasU = m.uv && !m.uv->empty();
        std::vector<double> sx(P.size()), sy(P.size());
        for (size_t i = 0; i < P.size(); ++i) {
            sx[i] = (P[i][0] - minx) * fp;
            sy[i] = (maxz - P[i][2]) * fp;
        }
        for (const Tri &tri : m.tris) {
            const long long a = tri[0], b = tri[1], c = tri[2];
            const double xs[3] = {sx[size_t(a)], sx[size_t(b)], sx[size_t(c)]};
            const double ys[3] = {sy[size_t(a)], sy[size_t(b)], sy[size_t(c)]};
            const long long x0 = static_cast<long long>(std::max(0.0, std::floor(std::min({xs[0], xs[1], xs[2]}))));
            const long long x1 = static_cast<long long>(std::min(double(W - 1), std::ceil(std::max({xs[0], xs[1], xs[2]}))));
            const long long y0 = static_cast<long long>(std::max(0.0, std::floor(std::min({ys[0], ys[1], ys[2]}))));
            const long long y1 = static_cast<long long>(std::min(double(H - 1), std::ceil(std::max({ys[0], ys[1], ys[2]}))));
            if (x1 < x0 || y1 < y0)
                continue;
            const double den = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2]);
            if (std::fabs(den) < 1e-12)              // a wall: nothing of it seen from above
                continue;
            const double top_y = std::max({P[size_t(a)][1], P[size_t(b)][1], P[size_t(c)][1]});
            const bool water = top_y < WATER_Y;
            const long long ww = x1 - x0 + 1;
            const double d12 = ys[1] - ys[2], d21 = xs[2] - xs[1], d20 = ys[2] - ys[0], d02 = xs[0] - xs[2];
            for (long long y = y0; y <= y1; ++y)
                for (long long x = x0; x <= x1; ++x) {
                    const double gx = double(x) + 0.5, gy = double(y) + 0.5;
                    const double l0 = (d12 * (gx - xs[2]) + d21 * (gy - ys[2])) / den;
                    const double l1 = (d20 * (gx - xs[2]) + d02 * (gy - ys[2])) / den;
                    const double l2 = 1.0 - l0 - l1;
                    if (!(l0 >= -1e-6 && l1 >= -1e-6 && l2 >= -1e-6))
                        continue;
                    const double h = l0 * P[size_t(a)][1] + l1 * P[size_t(b)][1] + l2 * P[size_t(c)][1];
                    double &dp = depth[size_t(y * W + x)];
                    if (!(h > dp))                   // what is higher hides what is under it
                        continue;
                    double col[3];
                    if (textured && hasU) {
                        const auto &U = *m.uv;
                        const double u = l0 * U[size_t(a)][0] + l1 * U[size_t(b)][0] + l2 * U[size_t(c)][0];
                        const double v = l0 * U[size_t(a)][1] + l1 * U[size_t(b)][1] + l2 * U[size_t(c)][1];
                        const long long tx = std::clamp(static_cast<long long>(py::mod(u, 1.0) * double(TW)), 0LL, TW - 1);
                        const double vv = flip_v ? (1.0 - py::mod(v, 1.0)) : py::mod(v, 1.0);
                        const long long ty = std::clamp(static_cast<long long>(vv * double(TH)), 0LL, TH - 1);
                        for (int k = 0; k < 3; ++k)
                            col[k] = T[size_t((ty * TW + tx) * 3 + k)];
                    } else if (water) {
                        col[0] = 70, col[1] = 130, col[2] = 200;
                    } else {
                        col[0] = 190, col[1] = 185, col[2] = 170;
                    }
                    if (hasN) {
                        const auto &N = *m.nrm;
                        double n[3];
                        for (int k = 0; k < 3; ++k)
                            n[k] = l0 * N[size_t(a)][k] + l1 * N[size_t(b)][k] + l2 * N[size_t(c)][k];
                        const double len = std::max(std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), 1e-9);
                        for (double &e : n)
                            e /= len;
                        // n @ light: numpy's BLAS (measured 2026-10-20) — one pixel wide: the dot product's order
                        const double dot = ww == 1 ? std::fma(n[2], light[2], std::fma(n[1], light[1], n[0] * light[0]))
                                                   : std::fma(n[2], light[2], std::fma(n[0], light[0], n[1] * light[1]));
                        const double shade = 0.6 + 0.4 * std::clamp(dot, 0.0, 1.0);
                        for (double &e : col)
                            e = e * shade;
                    }
                    dp = h;
                    for (int k = 0; k < 3; ++k)
                        rgb[size_t((y * W + x) * 3 + k)] = col[k];
                    kind(y, x) = water ? 1 : 2;
                }
        }
    }
    QImage img(int(W), int(H), QImage::Format_ARGB32);
    img.fill(0);
    for (long long y = 0; y < H; ++y) {
        auto *row = reinterpret_cast<std::uint32_t *>(img.scanLine(int(y)));
        for (long long x = 0; x < W; ++x) {
            if (!kind(y, x))
                continue;
            std::uint32_t q[3];
            for (int k = 0; k < 3; ++k)
                q[k] = static_cast<std::uint32_t>(std::clamp(rgb[size_t((y * W + x) * 3 + k)], 0.0, 255.0));
            row[x] = (255u << 24) | (q[0] << 16) | (q[1] << 8) | q[2];
        }
    }
    Render out;
    out.img = img;
    out.minx = minx;
    out.maxz = maxz;
    out.ppu = ppu;
    out.kind = std::move(kind);
    out.w = maxx - minx;
    out.h = maxz - minz;
    return out;
}

Samples fit_samples(const Render &render)
{
    Samples s;
    const double fp = double(render.ppu);
    for (long long j = 0; j < render.kind.h(); ++j)
        for (long long i = 0; i < render.kind.w(); ++i)
            if (const std::uint8_t k = render.kind(j, i)) {
                s.xs.push_back(render.minx + (double(i) + 0.5) / fp);
                s.zs.push_back(render.maxz - (double(j) + 0.5) / fp);
                s.wet.push_back(k == 1);
            }
    return s;
}

Score fit_score(const mapview::MapData &md, const std::vector<std::uint8_t> &sea, long long prov,
                const std::vector<long long> &seas, const Samples &samples, double ox, double oy)
{
    // How a model put with its origin at display (ox, oy) sits on the map's pixels. Good: water on sea or in the
    // canal's province, the built parts in the canal's province, the water reaching both sea tiles; bad: anything on
    // another land province.
    const size_t n = samples.xs.size();
    long long ws = 0, wp = 0, wo = 0, bp = 0, bs = 0, bo = 0, other = 0, nwet = 0, ndry = 0;
    bool any_prov = false;
    std::vector<long long> ids(n, 0);
    for (size_t i = 0; i < n; ++i) {
        const long long px = static_cast<long long>(std::floor(ox + samples.xs[i]));
        const long long py_ = static_cast<long long>(std::floor(oy - samples.zs[i]));
        if (px >= 0 && px < md.w && py_ >= 0 && py_ < md.h)
            ids[i] = md.idmap(py_, px);
        const long long id = ids[i];
        const bool is_sea = sea[size_t(id)] && id > 0, is_prov = id == prov, oth = !is_sea && !is_prov;
        const bool wet = samples.wet[i];
        nwet += wet;
        ndry += !wet;
        ws += wet && is_sea;
        wp += wet && is_prov;
        wo += wet && oth;
        bp += !wet && is_prov;
        bs += !wet && is_sea;
        bo += !wet && oth;
        other += oth;
        any_prov = any_prov || is_prov;
    }
    const double nw = double(std::max(1LL, nwet)), nb = double(std::max(1LL, ndry));
    Score r;
    r.water_sea = double(ws) / nw;
    r.water_prov = double(wp) / nw;
    r.water_other = double(wo) / nw;
    r.built_prov = double(bp) / nb;
    r.built_sea = double(bs) / nb;
    r.built_other = double(bo) / nb;
    r.other = double(other) / double(std::max<long long>(1, static_cast<long long>(n)));   // of the whole model
    bool near_seas = false;
    std::set<long long> reach;
    for (long long s : seas) {
        if (!s)
            continue;
        bool hit = false, wet_hit = false;
        for (size_t i = 0; i < n; ++i)
            if (ids[i] == s) {
                hit = true;
                wet_hit = wet_hit || samples.wet[i];
            }
        near_seas = near_seas || hit;
        if (wet_hit)
            reach.insert(s);
    }
    r.near_ = any_prov || near_seas;                  // at its canal at all
    r.reaches.assign(reach.begin(), reach.end());
    r.score = r.water_sea + r.water_prov + r.built_prov + 0.5 * r.built_sea - 3.0 * (r.water_other + r.built_other) +
              0.5 * double(r.reaches.size());
    return r;
}

OrderedMap<QString, std::vector<Ring>> outline(const std::vector<PosTris> &meshes)
{
    // The model seen from above: {"water": polygons, "built": polygons} in model units (x east, z north). Water = the
    // low faces (the canal's bed), built = the rest (locks, bridges).
    QPainterPath parts[2];                            // water, built
    for (const auto &[pos, tris] : meshes)
        for (const Tri &t : tris) {
            const V3 p[3] = {pos[size_t(t[0])], pos[size_t(t[1])], pos[size_t(t[2])]};
            const double a = (p[1][0] - p[0][0]) * (p[2][2] - p[0][2]) - (p[1][2] - p[0][2]) * (p[2][0] - p[0][0]);
            if (std::fabs(a) < 1e-6)                  // a wall: seen from above it is a line
                continue;
            const int key = std::max({p[0][1], p[1][1], p[2][1]}) < -0.2 ? 0 : 1;
            QPolygonF poly;
            for (const V3 &q : p)
                poly << QPointF(q[0], q[2]);
            poly << QPointF(p[0][0], p[0][2]);
            parts[key].addPolygon(poly);
        }
    OrderedMap<QString, std::vector<Ring>> out;
    const QString names[2] = {u"water"_s, u"built"_s};
    for (int k = 0; k < 2; ++k) {
        parts[k].setFillRule(Qt::WindingFill);        // the triangles' union
        const QPainterPath u = parts[k].simplified();
        // its rings (outer edges and holes) one by one: drawn together they fill even-odd, holes stay open
        std::vector<Ring> rings;
        for (const QPolygonF &poly : u.toSubpathPolygons()) {
            Ring ring;
            for (const QPointF &q : poly)
                ring.emplace_back(q.x(), q.y());
            rings.push_back(std::move(ring));
        }
        out.insert(names[k], std::move(rings));
    }
    return out;
}

OrderedMap<QString, QString> actor_files(const moddata::Mod &mod)
{
    // {model name: .xac path} of every EMFXActorType in interface/*.gfx (the mod's files over the game's).
    OrderedMap<QString, QString> out;
    std::vector<moddata::LayeredFile> files;
    try {
        files = mod.layered({u"interface"_s});
    } catch (const std::exception &) {
    }
    static const QRegularExpression actor(uR"((?is)EMFXActorType\s*=\s*\{(.*?)\})"_s, RX);
    static const QRegularExpression name(uR"re((?i)\bname\s*=\s*"?([^"\s}]+)"?)re"_s, RX);
    static const QRegularExpression file(uR"re((?i)\bactorfile\s*=\s*"([^"]+)")re"_s, RX);
    for (const auto &lf : files) {
        if (!py::lower(lf.path).endsWith(u".gfx"))
            continue;
        QString text;
        try {
            text = script::read_text(lf.path);
        } catch (const std::exception &) {
            continue;
        }
        if (!text.contains(u"EMFXActorType"))
            continue;
        for (auto it = actor.globalMatch(text); it.hasNext();) {
            const QString body = it.next().captured(1);
            const auto n = name.match(body), f = file.match(body);
            if (n.hasMatch() && f.hasMatch())
                out.insert(n.captured(1), QString(f.captured(1)).replace(u'\\', u'/'));
        }
    }
    return out;
}

Models::Models(const moddata::Mod &mod_) : mod(mod_)
{
    for (const auto &[k, v] : actor_files(mod))
        if (py::lower(k).contains(u"canal"))
            files.insert(k, v);
}

bool Models::exists(const QString &name) const
{
    const OptStr p = path(name);
    return p && fs::isfile(*p);
}

const std::optional<Render> &Models::render(const QString &name, long long ppu)
{
    // The model from above (render_top) — None when its file is missing or can't be read (the game then draws
    // nothing there).
    if (!_render.contains(name)) {
        std::optional<Render> got;
        const OptStr p = path(name);
        if (p && fs::isfile(*p)) {
            try {
                const auto meshes = read_meshes(*p);
                std::optional<QImage> tex;
                if (const OptStr tn = texture_name(*p)) {
                    const QString tp = mod.p({u"gfx"_s, u"anims"_s, *tn + u".dds"_s});
                    if (fs::isfile(tp))
                        tex = imgio::read_image(tp);
                }
                got = render_top(meshes, tex, ppu);
            } catch (const std::exception &) {
                got = std::nullopt;
            }
        }
        _render.insert(name, std::move(got));
    }
    return *_render.get(name);
}

QStringList Models::names() const
{
    QStringList out;
    for (const auto &kv : files)
        out << kv.first;
    std::stable_sort(out.begin(), out.end(), [](const QString &a, const QString &b) { return py::lower(a) < py::lower(b); });
    return out;
}

OptStr Models::path(const QString &name) const
{
    const QString *rel = files.get(name);
    return rel && !rel->isEmpty() ? OptStr(mod.p(rel->split(u'/'))) : std::nullopt;
}

const std::optional<OrderedMap<QString, std::vector<Ring>>> &Models::outline(const QString &name)
{
    if (!_outline.contains(name)) {
        std::optional<OrderedMap<QString, std::vector<Ring>>> got;
        const OptStr p = path(name);
        if (p && fs::isfile(*p)) {
            try {
                got = canals::outline(read_xac(*p));
            } catch (const std::exception &) {
                got = std::nullopt;
            }
        }
        _outline.insert(name, std::move(got));
    }
    return *_outline.get(name);
}

// ---------------------------------------------------------------------------------------------- sprite files
Sprite parse_sprite(const QString &text)
{
    // {"sprite", "x", "y"} of a province_flag_sprites file (missing values: None / 0).
    Sprite out;
    static const QRegularExpression re(uR"re((?im)^\s*(sprite|x|y)\s*=\s*"?([^\s"#]+))re"_s, RX);
    for (auto it = re.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        const QString k = py::lower(m.captured(1)), v = m.captured(2);
        if (k == u"sprite")
            out.sprite = v;
        else if (const auto f = py::to_float(v))
            (k == u"x" ? out.x : out.y) = *f;
    }
    return out;
}

QString fmt_num(double v)
{
    QString s = py::fmt_f(v, 3);
    while (s.endsWith(u'0'))
        s.chop(1);
    while (s.endsWith(u'.'))
        s.chop(1);
    return (s == u"-0" || s.isEmpty()) ? u"0"_s : s;
}

static QString rstrip_nl(QString s)
{
    while (s.endsWith(u'\n'))
        s.chop(1);
    return s;
}

QString sprite_text(const OptStr &old, const QString &sprite, double x, double y)
{
    // The file with sprite / x / y set — the rest of it (comments …) kept.
    QString text = old.value_or(QString());
    const std::pair<QString, QString> kv[3] = {{u"sprite"_s, sprite}, {u"x"_s, fmt_num(x)}, {u"y"_s, fmt_num(y)}};
    for (const auto &[key, val] : kv) {
        const QRegularExpression pat(u"(?im)^(\\s*%1\\s*=\\s*)\"?[^\\s\"#]*\"?"_s.arg(key), RX);
        const auto m = pat.match(text);
        if (m.hasMatch())
            text = text.left(m.capturedStart()) + m.captured(1) + val + text.mid(m.capturedEnd());
        else
            text = rstrip_nl(text) + (py::strip(text).isEmpty() ? u""_s : u"\n"_s) + u"%1 = %2\n"_s.arg(key, val);
    }
    return text;
}

OrderedMap<QString, Sprite> sprites(const moddata::Mod &mod, scriptstore::ScriptStore &store)
{
    // {flag: {"sprite", "x", "y", "rel"}} of map/province_flag_sprites (the editor's texts).
    OrderedMap<QString, Sprite> out;
    for (const QString &rel : entryview::_files(mod, &store, SPRITE_DIR)) {
        const QString flag = pypath::splitext_stem(pypath::basename(rel));
        Sprite d;
        try {
            d = parse_sprite(store.text(rel));
        } catch (const std::exception &) {
            continue;
        }
        d.rel = rel;
        d.flag = flag;
        out.insert(py::lower(flag), d);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- the scripts
namespace {
using Path = std::vector<const script::Node *>;
struct Found {
    struct Open {
        const script::Node *blk;
        long long num;
        Path top;
        qsizetype pos;
    };
    struct Flag {
        const script::Node *blk;
        long long prov;
        QString flag;
        Path top;
        qsizetype pos;
    };
    std::vector<Open> opens;
    std::vector<Flag> flags;
};
Path first2(const Path &p) { return Path(p.begin(), p.begin() + std::min<size_t>(2, p.size())); }

void walk(const script::Nodes &nodes, Found &out, Path &path)
{
    // Every enable_canal and province flag set, with the block it stands in (its id) and the outer item.
    for (const script::Node &n : nodes) {
        if (n.is_block) {
            path.push_back(&n);
            walk(n.kids, out, path);
            path.pop_back();
            if (n.has_key && !n.key.isEmpty() && py::isdigit(n.key))     // <province> = { set_province_flag = x }
                for (const script::Node &c : n.kids)
                    if (c.has_key && c.key == u"set_province_flag" && !c.is_block)
                        out.flags.push_back({path.empty() ? nullptr : path.back(), py::to_int(n.key), py::strip(c.value),
                                             first2(path), c.vstart});
        } else if (n.has_key && n.key == u"enable_canal") {
            long long num;
            try {
                num = py::to_int(py::strip(n.value));
            } catch (const py::ValueError &) {
                continue;
            }
            out.opens.push_back({path.empty() ? nullptr : path.back(), num, first2(path), n.vstart});
        }
    }
}

std::pair<QString, OptStr> item_name(const Path &top)
{
    // (kind, name) of the outer item: ("decision", key) / ("event", id) / ("other", key).
    if (top.empty())
        return {u"other"_s, u""_s};
    const script::Node &t0 = *top[0];
    const OptStr k0 = t0.has_key ? OptStr(t0.key) : std::nullopt;
    if (k0 == u"political_decisions" && top.size() > 1)
        return {u"decision"_s, top[1]->has_key ? OptStr(top[1]->key) : std::nullopt};
    if (k0 == u"country_event" || k0 == u"province_event") {
        for (const script::Node &c : t0.kids)
            if (c.has_key && c.key == u"id" && !c.is_block)
                return {u"event"_s, py::strip(c.value)};
        return {u"event"_s, u"?"_s};
    }
    return {u"other"_s, k0 && !k0->isEmpty() ? *k0 : u""_s};
}
}  // namespace

OrderedMap<long long, std::vector<Opening>> openings(const moddata::Mod &mod, scriptstore::ScriptStore &store)
{
    // What the scripts do with canals: every place an enable_canal stands, with the province flags set in the same
    // block. (Python keeps each file's result while its text is the same object: a speed-up only.)
    OrderedMap<long long, std::vector<Opening>> out;
    for (const QString folder : {u"decisions"_s, u"events"_s})
        for (const QString &rel : entryview::_files(mod, &store, folder)) {
            QString text;
            try {
                text = store.text(rel);
            } catch (const std::exception &) {
                continue;
            }
            if (!text.contains(u"enable_canal"))
                continue;
            Found found;
            script::Nodes nodes;
            try {
                nodes = script::parse(text);
                Path path;
                walk(nodes, found, path);
            } catch (const std::exception &) {
            }
            for (const Found::Open &o : found.opens) {
                const auto [kind, name] = item_name(o.top);
                Opening d{kind, name, rel, o.pos, {}};
                for (const Found::Flag &f : found.flags)
                    if (f.blk == o.blk)
                        d.flags.emplace_back(f.prov, f.flag);
                if (auto *v = out.get(o.num))
                    v->push_back(d);
                else
                    out.insert(o.num, {d});
            }
        }
    return out;
}

// ---------------------------------------------------------------------------------------------- one canal
std::vector<Canal> canals(const adjacency::AdjacencyFile &adj, const moddata::Mod &mod, scriptstore::ScriptStore &store)
{
    // One per canal number, in order.
    const auto sp = sprites(mod, store);
    const auto ops = openings(mod, store);
    OrderedMap<long long, std::vector<adjacency::Row>> by_num;
    for (const adjacency::Row &r : adj.rows)
        if (r.type == u"canal") {
            if (auto *v = by_num.get(r.data))
                v->push_back(r);
            else
                by_num.insert(r.data, {r});
        }
    std::set<long long> nums;
    for (const auto &kv : by_num)
        nums.insert(kv.first);
    for (const auto &kv : ops)
        nums.insert(kv.first);
    std::vector<Canal> out;
    for (long long num : nums) {
        Canal c;
        c.num = num;
        c.rows = by_num.value(num);
        c.prov = c.rows.empty() ? 0 : c.rows[0].through;
        c.opens = ops.value(num);
        for (const Opening &o : c.opens) {
            for (const auto &[p, f] : o.flags)
                if (const Sprite *s = sp.get(py::lower(f))) {
                    c.flag = f;
                    c.sprite = *s;
                    c.flag_prov = p;
                    break;
                }
            if (c.flag && !c.flag->isEmpty())
                break;
        }
        for (const adjacency::Row &r : c.rows)
            if (!py::strip(r.comment).isEmpty()) {
                c.name = py::strip(r.comment);
                break;
            }
        if (!c.rows.empty())
            c.seas = {c.rows[0].a, c.rows[0].b};
        out.push_back(std::move(c));
    }
    return out;
}

QStringList problems(const Canal &c, const moddata::Mod &mod)
{
    // What is wrong with one canal, in words (the page shows them amber).
    QStringList out;
    if (c.rows.empty())
        out << u"enable_canal = %1 is used in a script, but adjacencies.csv has no canal %1"_s.arg(c.num);
    if (!c.rows.empty() && c.opens.empty())
        out << u"nothing opens it: no script has enable_canal = %1 (the canal stays closed)"_s.arg(c.num);
    if (c.flag && !c.flag->isEmpty() && c.flag_prov && *c.flag_prov && c.prov && *c.flag_prov != c.prov) {
        const moddata::Province *pr = mod.provinces.get(*c.flag_prov);
        out << u"the picture is set on #%1 %2, but the canal goes through #%3"_s.arg(*c.flag_prov)
                   .arg(pr ? pr->name : u"?"_s)
                   .arg(c.prov);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- writing
QString flag_name(long long num, const QSet<QString> &taken)
{
    const QString base = u"canal_%1"_s.arg(num);
    QString f = base;
    long long i = 2;
    while (taken.contains(py::lower(f))) {
        f = u"%1_%2"_s.arg(base).arg(i);
        i += 1;
    }
    return f;
}

QString decision_block(const QString &key, long long num, long long prov, const QString &flag, const QString &picture)
{
    // (QString::arg with several values fills them in one pass, as Python's %: a value holding "%2" stays as it is)
    return u"\t%1 = {\n\t\tpicture = %2\n\t\tpotential = {\n\t\t\towns = %3\n\t\t\tNOT = { is_canal_enabled = %4 }\n"
           "\t\t}\n\t\tallow = {\n\t\t}\n\t\teffect = {\n\t\t\tenable_canal = %4\n\t\t\t%3 = { set_province_flag = %5 }\n"
           "\t\t}\n\t\tai_will_do = {\n\t\t\tfactor = 1\n\t\t}\n\t}\n"_s.arg(key, picture, QString::number(prov),
                                                                         QString::number(num), flag);
}

QString event_block(const QString &eid, long long num, long long prov, const QString &flag)
{
    return u"country_event = {\n\tid = %1\n\ttitle = \"EVTNAME%1\"\n\tdesc = \"EVTDESC%1\"\n\n\tfire_only_once = yes\n"
           "\ttrigger = {\n\t\towns = %2\n\t\tNOT = { is_canal_enabled = %3 }\n\t}\n\tmean_time_to_happen = {\n"
           "\t\tdays = 1\n\t}\n\n\toption = {\n\t\tname = \"EVTOPTA%1\"\n\t\tenable_canal = %3\n"
           "\t\t%2 = { set_province_flag = %4 }\n\t}\n}\n"_s.arg(eid, QString::number(prov), QString::number(num), flag);
}

QString _cut(const QString &text, const script::Node &n)
{
    const qsizetype a = (n.kstart > 0 ? text.lastIndexOf(u'\n', n.kstart - 1) : -1) + 1;
    qsizetype b = text.indexOf(u'\n', n.vend);
    b = b < 0 ? text.size() : b + 1;
    while (b < text.size() && text[b] == u'\n' && (a == 0 || text[a - 1] == u'\n') && (a < 2 || text[a - 2] == u'\n'))
        b += 1;
    return text.left(a) + text.mid(b);
}

QString remove_item(const QString &text, const QString &kind, const OptStr &name)
{
    // The file's text without the decision `name` (kind "decision") / the event with id `name`.
    script::Nodes nodes;
    try {
        nodes = script::parse(text);
    } catch (const std::exception &) {
        return text;
    }
    for (const script::Node &top : nodes) {
        const bool k = top.has_key;
        if (kind == u"decision" && k && top.key == u"political_decisions" && top.is_block)
            for (const script::Node &n : top.kids)
                if ((n.has_key ? OptStr(n.key) : std::nullopt) == name && n.is_block)
                    return _cut(text, n);
        if (kind == u"event" && k && (top.key == u"country_event" || top.key == u"province_event") && top.is_block)
            for (const script::Node &c : top.kids)
                if (c.has_key && c.key == u"id" && !c.is_block) {
                    if (py::strip(c.value) == name.value_or(u"None"_s))     // (str(name))
                        return _cut(text, top);
                    break;
                }
    }
    return text;
}

QString add_decision(const QString &text, const QString &block)
{
    // `block` (one decision) put into the file's political_decisions (made when there is none).
    const QString t = rstrip_nl(text);
    if (!t.contains(u"political_decisions"))
        return (t.isEmpty() ? u""_s : t + u"\n\n"_s) + u"political_decisions = {\n"_s + block + u"}\n"_s;
    const qsizetype i = t.lastIndexOf(u'}');
    return py::head(t, i) + block + py::tail(t, i) + u"\n"_s;
}

QString add_event(const QString &text, const QString &block)
{
    const QString t = rstrip_nl(text);
    return (t.isEmpty() ? u""_s : t + u"\n\n"_s) + block;
}

}  // namespace canals
