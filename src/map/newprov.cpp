// newprov.py, the data half. Line by line.
#include "map/newprov.h"
#include "core/fs.h"
#include "core/localisation.h"
#include "core/mapfiles.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"
#include "grid/pyset.h"
#include "grid/random.h"

#include <QDateTime>
#include <QImage>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

using namespace Qt::StringLiterals;
using pypath::join;

namespace newprov {

const QStringList BUILDING_KEYS = {u"fort"_s, u"naval_base"_s, u"railroad"_s, u"state_building"_s};
const QRegularExpression BAD_FILE_CHARS(uR"([\\/:*?"<>|])"_s);

static QString stamp_now() { return QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s); }
static QString rel_path(const QString &root, const QString &rel) { return join(root, rel.split(u'/')); }

// set(a) - set(b) of dict keys, walked in Python's order
template <typename M>
static std::vector<long long> keys_of(const M &m)
{
    std::vector<long long> k;
    for (const auto &kv : m)
        k.push_back(kv.first);
    return k;
}
template <typename A, typename B>
static std::vector<long long> key_diff(const A &a, const B &b)
{
    return grid::PySet<long long>::from_keys(keys_of(a)).difference(grid::PySet<long long>::from_keys(keys_of(b))).items();
}
template <typename M>
static std::vector<long long> sorted_keys(const M &m)
{
    std::vector<long long> k = keys_of(m);
    std::sort(k.begin(), k.end());
    return k;
}

// ---------------------------------------------------------------------------------------------- the definitions
ProvinceDefs::ProvinceDefs(moddata::Mod &mod_, mapview::MapData &md_, history::ProvinceHistory &hist_)
    : mod(mod_), md(md_), hist(hist_)
{
    path = mod.p({u"map"_s, u"definition.csv"_s});
    if (fs::isfile(path))
        hash = moddata::text_hash(script::read_text(path));
}

void ProvinceDefs::restore(const store::Snap &snap)
{
    const DefsSnap &s = store::unbox<DefsSnap>(snap);
    const OrderedMap<long long, NewProv> old = added;
    const OrderedMap<long long, Deleted> old_del = gone;
    added = s.added;
    gone = s.gone;
    _sync_deleted(old_del);
    QSet<long long> a, b;
    for (const auto &kv : old_del)
        a.insert(kv.first);
    for (const auto &kv : gone)
        b.insert(kv.first);
    _sync(old, a != b);
}

QStringList ProvinceDefs::dirty() const
{
    QStringList out;
    for (long long p : sorted_keys(added))
        out << u"#%1"_s.arg(p);
    for (long long p : sorted_keys(gone))
        out << u"deleted #%1"_s.arg(p);
    return out;
}

QList<long long> ProvinceDefs::deleted() const
{
    QList<long long> out;
    for (const auto &kv : gone)
        out << kv.first;
    return out;
}

void ProvinceDefs::del(const QList<long long> &pids, const OrderedMap<long long, long long> &heirs)
{
    // Provinces deleted: one made in this session is simply gone again; one of the files gives up its place in the
    // mod's lists (its row in definition.csv is dealt with on Apply). heirs: {province: the province that stands for
    // it from now on} — what the scripts that name it are pointed at.
    const OrderedMap<long long, NewProv> old = added;
    const OrderedMap<long long, Deleted> old_del = gone;
    for (long long pid : pids) {
        if (added.contains(pid)) {
            added.remove(pid);
        } else if (const moddata::Province *pr = mod.provinces.get(pid)) {
            const auto *files = hist.files.get(pid);
            const long long *h = heirs.get(pid);
            gone.insert(pid, {*pr, files ? std::optional(*files) : std::nullopt,
                              h ? std::optional<long long>(*h) : std::nullopt});
        }
    }
    _sync_deleted(old_del);
    _sync(old, true);
}

void ProvinceDefs::_sync_deleted(const OrderedMap<long long, Deleted> &old)
{
    for (long long pid : key_diff(gone, old)) {               // deleted now
        mod.provinces.remove(pid);
        hist.files.remove(pid);
        if (pid < md.n)
            md.sea[pid] = 0;
    }
    for (long long pid : key_diff(old, gone)) {               // back again (undo)
        const Deleted &d = *old.get(pid);
        mod.provinces.insert(pid, d.pr);
        if (d.files && !d.files->empty())
            hist.files.insert(pid, *d.files);
        if (pid < md.n)
            md.sea[pid] = d.pr.sea;
    }
}

void ProvinceDefs::add(long long pid, const NewProv &rec)
{
    const OrderedMap<long long, NewProv> old = added;
    added.insert(pid, rec);
    _sync(old);
}

void ProvinceDefs::update(long long pid, std::optional<long long> done_version, long long done_area)
{
    if (NewProv *r = added.get(pid)) {
        NewProv n = *r;
        n.done_version = done_version;
        n.done_area = done_area;
        added.insert(pid, n);
    }
}

void ProvinceDefs::_sync(const OrderedMap<long long, NewProv> &old, bool tell)
{
    for (long long pid : key_diff(old, added)) {
        mod.provinces.remove(pid);
        mod.loc.remove(u"PROV%1"_s.arg(pid));
        hist.remove_virtual(pid);
        if (pid < md.n)
            md.sea[pid] = 0;
    }
    for (const auto &[pid, rec] : added) {
        if (!mod.provinces.contains(pid)) {
            moddata::Province np;
            np.id = pid;
            np.rgb = rec.rgb;
            np.name = rec.name;
            np.sea = rec.sea;
            mod.provinces.insert(pid, np);
        }
        moddata::Province &pr = *mod.provinces.get(pid);
        pr.name = rec.name;
        pr.rgb = rec.rgb;
        pr.sea = rec.sea;
        pr.owner = rec.info.owner;
        pr.controller = rec.info.controller;
        pr.cores = rec.info.cores;
        pr.life_rating = rec.info.life_rating ? OptStr(QString::number(*rec.info.life_rating)) : std::nullopt;
        pr.trade_goods = rec.info.trade_goods;
        pr.history_file = rec.hist_rel;
        mod.loc.insert(u"PROV%1"_s.arg(pid), rec.name);
        if (rec.hist_rel)
            hist.add_virtual(pid, *rec.hist_rel, rec.hist_text.value_or(QString()));
        md.sea[pid] = rec.sea;
    }
    QSet<long long> a, b;
    for (const auto &kv : old)
        a.insert(kv.first);
    for (const auto &kv : added)
        b.insert(kv.first);
    if (tell || a != b)
        for (const auto &fn : listeners)
            fn();
}

// --- choosing id / colour
long long ProvinceDefs::next_id() const
{
    // The next number: above every province, every unused row and every province deleted in this session.
    long long top = std::numeric_limits<long long>::min();
    for (const auto &kv : mod.provinces)
        top = std::max(top, kv.first);
    for (const auto &kv : mod.free_slots)
        top = std::max(top, kv.first);
    for (const auto &kv : gone)
        top = std::max(top, kv.first);
    if (top == std::numeric_limits<long long>::min())
        throw std::runtime_error("max() arg is an empty sequence");
    return top + 1;
}

const std::vector<bool> &ProvinceDefs::file_colours()
{
    // Every colour used by a pixel of provinces.bmp on disk (as 24-bit keys, bool lookup).
    if (!_file_colours) {
        const QImage img = QImage(mod.p({u"map"_s, u"provinces.bmp"_s})).convertToFormat(QImage::Format_RGB888);
        std::vector<bool> seen(size_t(1) << 24, false);
        for (int y = 0; y < img.height(); ++y) {
            const uchar *row = img.constScanLine(y);
            for (int x = 0; x < img.width(); ++x)
                seen[(size_t(row[3 * x]) << 16) | (size_t(row[3 * x + 1]) << 8) | row[3 * x + 2]] = true;
        }
        _file_colours = std::move(seen);
    }
    return *_file_colours;
}

std::vector<Rgb> ProvinceDefs::spare_rows() const
{
    // Colours of the rows at the end of definition.csv that have no id yet, in file order.
    std::vector<Rgb> out;
    if (!fs::isfile(path))
        return out;
    for (const QString &line : py::splitlines(script::read_text(path)))
        if (const auto c = _spare_rgb(line))
            out.push_back(*c);
    return out;
}

std::pair<Rgb, QString> ProvinceDefs::pick_colour()
{
    // -> (rgb, where) — a spare definition.csv row, or a fresh colour nobody uses.
    std::set<Rgb> used;
    for (const auto &kv : mod.provinces)
        used.insert(kv.second.rgb);
    for (const auto &kv : mod.free_slots)
        used.insert(kv.second);
    for (const auto &kv : gone)
        used.insert(kv.second.pr.rgb);
    const std::vector<bool> &seen = file_colours();
    auto is_free = [&](const Rgb &c) {
        const auto [r, g, b] = c;
        return !used.count(c) && c != Rgb{0, 0, 0} && !seen[(size_t(r) << 16) | (size_t(g) << 8) | size_t(b)];
    };
    for (const Rgb &c : spare_rows())
        if (is_free(c))
            return {c, u"a spare row of definition.csv"_s};
    grid::Generator rng(static_cast<std::uint64_t>(mod.provinces.size()));
    for (int i = 0; i < 100000; ++i) {
        const auto v = rng.integers(16, 240, 3);
        const Rgb c{int(v[0]), int(v[1]), int(v[2])};
        if (is_free(c))
            return {c, u"a new colour"_s};
    }
    throw std::runtime_error("No free colour found");
}

// --- saving
std::optional<QStringList> ProvinceDefs::save(const QString &backup_root)
{
    const auto p = save_file(backup_root);
    return p ? std::optional<QStringList>(QStringList{*p}) : std::nullopt;
}

std::optional<QString> ProvinceDefs::save_file(const QString &backup_root)
{
    if (added.isEmpty() && gone.isEmpty())
        return std::nullopt;
    path = mod.adopt(path);                        // (the base game's file: copied into the mod first)
    QString text = script::read_text(path);
    if (hash && moddata::text_hash(text) != *hash)
        throw std::runtime_error("map/definition.csv was changed on disk after it was loaded. Reload before saving.");
    for (const auto &[pid, rec] : added)
        if (rec.hist_rel && fs::exists(rel_path(hist.hdir, *rec.hist_rel)))
            throw std::runtime_error(("history/provinces/" + *rec.hist_rel + " already exists — not overwritten.")
                                         .toStdString());
    const QString bdir = join(backup_root, {added.isEmpty() ? u"deleted_provinces"_s : u"new_provinces"_s, stamp_now()});
    fs::makedirs(bdir);
    // definition.csv: fill the spare row of the colour, or add the row after the last province
    fs::copy2(path, join(bdir, u"definition.csv"_s));
    const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    QStringList lines = text.split(nl);
    std::vector<history::HistFileP> gone_files;
    if (!gone.isEmpty()) {
        // a deleted province: the numbers above the highest province that stays leave the file (spare colours); one
        // below it keeps its row for the moment, named unused — right after this Apply the highest province takes
        // that number (provrenumber: the game needs the numbers without a gap, and crashes on one without land)
        auto &free = mod.free_slots;
        long long top = 0;
        for (const auto &kv : mod.provinces)
            top = std::max(top, kv.first);
        for (qsizetype i = 0; i < lines.size(); ++i) {
            QStringList f;
            for (const QString &x : lines[i].split(u';'))
                f << py::strip(x);
            if (f.size() < 4 || !py::isdigit(f[0]))
                continue;
            const long long pid = py::to_int(f[0]);
            const bool unused = f.size() > 4 && f[4] == moddata::FREE_NAME;
            if (added.contains(pid) || !(gone.contains(pid) || unused))
                continue;
            if (pid > top) {
                lines[i] = u";%1;%2;%3;;x"_s.arg(f[1], f[2], f[3]);
                free.remove(pid);
            } else if (!unused) {
                lines[i] = u"%1;%2;%3;%4;%5;x"_s.arg(pid).arg(f[1], f[2], f[3], moddata::FREE_NAME);
                free.insert(pid, gone.get(pid)->pr.rgb);
            }
        }
        for (const auto &[pid, d] : gone)
            if (d.files)
                for (const auto &hf : *d.files)
                    if (!hf->pending && fs::isfile(hf->path) && mod.in_mod(hf->path))
                        gone_files.push_back(hf);
    }
    std::vector<long long> order;
    for (const auto &kv : added)
        order.push_back(kv.first);
    std::sort(order.begin(), order.end());
    for (long long pid : order) {
        const NewProv &rec = *added.get(pid);
        const auto [r, g, b] = rec.rgb;
        QString name = rec.name;
        const QString row = u"%1;%2;%3;%4;%5;x"_s.arg(pid).arg(r).arg(g).arg(b).arg(name.replace(u';', u','));
        std::optional<qsizetype> spare;
        for (qsizetype i = 0; i < lines.size() && !spare; ++i)
            if (_spare_rgb(lines[i]) == std::optional<Rgb>(rec.rgb))
                spare = i;
        if (spare) {
            lines[*spare] = row;
        } else {
            qsizetype last = -1;
            for (qsizetype i = 0; i < lines.size(); ++i)
                if (py::isdigit(py::strip(lines[i].split(u';')[0])))
                    last = i;
            if (last < 0)
                last = lines.size() - 1;
            lines.insert(last + 1, row);
        }
    }
    text = lines.join(nl);
    script::write_text(path, text);
    hash = moddata::text_hash(text);
    _file_colours.reset();
    for (const auto &hf : gone_files) {              // the deleted provinces' history files (a copy is kept)
        const QString dst = join(join(bdir, u"history"_s), hf->rel.split(u'/'));
        fs::makedirs(pypath::dirname(dst));
        fs::copy2(hf->path, dst);
        const std::optional<QString> base =
            mod.base ? std::optional(join(*mod.base, QStringList{u"history"_s, u"provinces"_s} + hf->rel.split(u'/')))
                     : std::nullopt;
        if (base && fs::isfile(*base) && !mod.replaced({u"history"_s, u"provinces"_s}))
            script::write_text(hf->path, QString());     // (the game would read its own file of that name instead)
        else
            fs::remove(hf->path);
    }
    for (const auto &kv : gone)
        mod.history.remove(kv.first);
    if (!gone.isEmpty()) {                             // (for the step right after this Apply: what stands for each)
        for (const auto &[pid, d] : gone)
            mod.free_heirs.insert(pid, d.heir);
    }
    gone = {};
    // history files
    QStringList written = {path};
    for (long long pid : order) {
        const NewProv &rec = *added.get(pid);
        if (!rec.hist_rel)
            continue;
        const QString p = rel_path(hist.hdir, *rec.hist_rel);
        fs::makedirs(pypath::dirname(p));
        const auto *files = hist.files.get(pid);
        const history::HistFileP hf = files && !files->empty() ? (*files)[0] : nullptr;
        script::write_text(p, hf && hf->pending ? hf->text : rec.hist_text.value_or(QString()));
        if (hf)
            hf->reload();
        mod.history.insert(pid, {{*rec.hist_rel, script::parse(script::read_text(p))}});
        written << p;
    }
    if (!added.isEmpty())
        if (const auto l = _save_localisation(bdir))
            written << *l;
    added = {};
    return written[0];
}

std::optional<QString> ProvinceDefs::_save_localisation(const QString &bdir)
{
    // PROV<id> names of the new provinces: new keys go to the editor's own localisation file (a key the mod already
    // has is changed where it is).
    OrderedMap<QString, QString> names, sections;
    for (const auto &[pid, rec] : added)
        if (!rec.name.isEmpty()) {
            names.insert(u"PROV%1"_s.arg(pid), rec.name);
            sections.insert(u"PROV%1"_s.arg(pid), u"prov"_s);
        }
    const QStringList w = localisation::write_names(mod, names, bdir, sections);
    return w.isEmpty() ? std::nullopt : std::optional<QString>(w[0]);
}

std::optional<Rgb> _spare_rgb(const QString &line)
{
    const QStringList f = line.split(u';');
    if (f.size() >= 4 && py::strip(f[0]).isEmpty() && !py::lstrip(line).startsWith(u'#')) {
        try {
            return Rgb{int(py::to_int(f[1])), int(py::to_int(f[2])), int(py::to_int(f[3]))};
        } catch (const py::ValueError &) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------- planning
QString history_without_buildings(const QString &text0)
{
    // The text of a history file with every fort / naval_base / railroad / state_building entry cut (top level and
    // inside dated blocks) — and the template's island flag (the new province has its own shape).
    QString text = text0;
    std::vector<std::pair<qsizetype, qsizetype>> cuts;
    std::function<void(const script::Nodes &, int)> walk = [&](const script::Nodes &nodes, int depth) {
        for (const script::Node &n : nodes) {
            if ((n.has_key && BUILDING_KEYS.contains(n.key)) || history::is_island_flag(n))
                cuts.push_back(_line_span(text, n.kstart, n.vend));
            else if (n.is_block && depth == 0 && n.has_key && !n.key.isEmpty() && script::parse_date(n.key))
                walk(n.kids, 1);
        }
    };
    walk(script::parse(text), 0);
    std::sort(cuts.begin(), cuts.end(), std::greater<>());
    for (const auto &[s, e] : cuts)
        text = text.left(s) + text.mid(e);
    return text;
}

std::pair<qsizetype, qsizetype> _line_span(const QString &text, qsizetype s, qsizetype e)
{
    const qsizetype ls = (s > 0 ? text.lastIndexOf(u'\n', s - 1) : -1) + 1;
    if (!py::strip(text.mid(ls, s - ls)).isEmpty())
        return {s, e};
    const qsizetype j = text.indexOf(u'\n', e);
    const QString rest = py::strip(text.mid(e, (j >= 0 ? j : text.size()) - e));
    if (!rest.isEmpty() && !rest.startsWith(u'#'))
        return {ls, e};
    return {ls, j >= 0 ? j + 1 : text.size()};
}

QString safe_file_name(const QString &name)
{
    QString s = name;
    s.remove(BAD_FILE_CHARS);
    s = py::strip(s);
    while (s.endsWith(u'.'))
        s.chop(1);
    QString out;                                   // .encode(latin-1, "replace").decode(latin-1)
    for (char32_t c : s.toUcs4())
        out += c <= 0xFF ? QChar(char16_t(c)) : QChar(u'?');
    return out.isEmpty() ? u"New province"_s : out;
}

Plan plan(document::Document &doc, ProvinceDefs &defs, const QString &name, bool sea, std::optional<long long> tmpl)
{
    // Everything a new province needs -> (pid, NewProv, state, continent, climate, want_state).
    moddata::Mod &mod = doc.mod;
    const long long pid = defs.next_id();
    if (pid >= defs.md.n)
        throw std::runtime_error("No room for more new provinces in this session — Apply and restart the editor.");
    const Rgb rgb = defs.pick_colour().first;
    Plan out{pid, {}, std::nullopt, std::nullopt, std::nullopt, std::nullopt};
    Info info;
    if (!sea) {
        const moddata::Province *tpl = tmpl ? mod.provinces.get(*tmpl) : nullptr;
        if (!tpl || tpl->sea)
            throw py::ValueError("Pick a land province to copy the history, state, continent and climate from.");
        const history::HistFileP hf = doc.hist->main_file(*tmpl);
        const QString folder = hf ? hf->rel.split(u'/')[0] : _history_folder_near(*doc.hist, *tmpl);
        out.rec.hist_rel = u"%1/%2 - %3.txt"_s.arg(folder, QString::number(pid), safe_file_name(name));
        const QString src = hf && !hf->nodes.empty() ? history_without_buildings(hf->text) : QString();
        const QString nl = src.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
        out.rec.hist_text = u"# %1 - %2: made in ModEditor from #%3 %4 (without buildings)%5%6"_s.arg(
            QString::number(pid), name, QString::number(*tmpl), tpl->name, nl, src);   // (one pass: names hold no %N)
        const history::ProvState st = doc.prov_state(*tmpl);
        info = {st.owner, st.controller, st.cores, st.life_rating, st.trade_goods};
        out.rid = tpl->region;
        const QStringList co = doc.map<mapfiles::MembershipFile>(u"continent"_s).of(*tmpl);
        const QStringList cl = doc.map<mapfiles::MembershipFile>(u"climate"_s).of(*tmpl);
        if (!co.isEmpty())
            out.continent = co[0];
        if (!cl.isEmpty())
            out.climate = cl[0];
        if (doc.prov_want.contains(*tmpl)) {         // unsaved Provinces-tab edits of the template come along
            history::ProvState want = history::clone_state(st);
            want.fort = want.naval_base = want.railroad = 0;
            want.factories.clear();
            out.want = want;
        }
    }
    out.rec.name = name;
    out.rec.rgb = rgb;
    out.rec.sea = sea;
    out.rec.tmpl = sea ? std::nullopt : tmpl;
    out.rec.info = info;
    out.rec.done_version = std::nullopt;
    out.rec.done_area = 0;
    return out;
}

QString _history_folder_near(const history::ProvinceHistory &hist, long long pid)
{
    // History folder of the province with the nearest id (for a template without a history file).
    std::optional<std::pair<long long, QString>> best;
    for (const auto &[p, fs_] : hist.files) {
        if (fs_.empty())
            continue;
        const std::pair<long long, QString> c{std::llabs(p - pid), fs_[0]->rel.split(u'/')[0]};
        if (!best || c < *best)
            best = c;
    }
    return best ? best->second : u"new_provinces"_s;
}

// ---------------------------------------------------------------------------------------------- after painting
OrderedMap<long long, long long> pixel_donors(const provbmp::ProvinceBitmap &bmp, long long pid,
                                              std::optional<long long> since)
{
    // {province id: pixels} the new province took from them (per pixel: the id it had before it first became `pid`),
    // counting strokes from bmp version `since` on and pixels that are still `pid`.
    std::map<std::int64_t, std::uint16_t> first;     // idx -> old (the first stroke that made it pid)
    const long long from = since.value_or(0);
    for (long long v = from; v < bmp.version && v < static_cast<long long>(bmp.diffs.size()); ++v) {
        const provbmp::Diff &d = bmp.diffs[size_t(v)];
        for (size_t i = 0; i < d.idx.size(); ++i)
            if (d.nw[i] == pid)
                first.emplace(d.idx[i], d.old[i]);
    }
    std::map<long long, long long> cnt;
    const auto &flat = bmp.md.idmap.flat();
    for (const auto &[idx, old] : first)
        if (flat[size_t(idx)] == pid && old != pid)
            cnt[old] += 1;
    OrderedMap<long long, long long> out;
    for (const auto &[k, v] : cnt)
        out.insert(k, v);
    return out;
}

std::pair<QString, QString> new_status(const ProvinceDefs &defs, const provbmp::ProvinceBitmap &bmp,
                                       const mapview::MapData &md, long long pid)
{
    // (key, text) of a new province: "offmap" / "todo" / "changed" / "done".
    const NewProv &rec = *defs.added.get(pid);
    if (md.area[pid] == 0)
        return {u"offmap"_s, u"Not on the map yet — paint it."_s};
    if (!rec.done_version)
        return {u"todo"_s, u"Not finished yet — press Finish."_s};
    if (!pixel_donors(bmp, pid, rec.done_version).isEmpty() || md.area[pid] != rec.done_area)
        return {u"changed"_s, u"Painted since Finish — Finish again."_s};
    return {u"done"_s, u"Finished."_s};
}

std::vector<std::pair<long long, double>> pop_shares(const document::Document &doc, const mapview::MapData &md,
                                                     const OrderedMap<long long, long long> &donors)
{
    // [(donor, share of its pops that goes along)] — share = pixels taken / its area before.
    std::vector<std::pair<long long, long long>> items(donors.begin(), donors.end());
    std::sort(items.begin(), items.end());
    std::vector<std::pair<long long, double>> out;
    for (const auto &[d, c] : items) {
        const moddata::Province *pr = doc.mod.provinces.get(d);
        const auto *pops = doc.pops.get(d);
        if (!pr || pr->sea || !pops || pops->empty())
            continue;
        const long long before = md.area[d] + c;
        if (before > 0)
            out.emplace_back(d, double(c) / double(before));
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- Document steps
void create_province(document::Document &doc, long long pid, const NewProv &rec, const OptStr &rid,
                     const OptStr &continent, const OptStr &climate, const std::optional<history::ProvState> &want)
{
    // A new province: definition, default.map, state, continent, climate and history, as one undo step.
    ProvinceDefs &defs = doc.map<ProvinceDefs>(u"definitions"_s);
    auto &dm = doc.map<mapfiles::DefaultMapFile>(u"default_map"_s);
    QStringList rids;
    if (rid && doc.mod.regions.contains(*rid))
        rids << *rid;
    doc.edit(u"New province #%1 %2"_s.arg(pid).arg(rec.name), {pid}, [&]() {
        defs.add(pid, rec);
        if (dm.max_provinces && *dm.max_provinces < pid + 1)
            dm.max_provinces = pid + 1;              // always the highest id + 1
        if (rec.sea && std::find(dm.sea.begin(), dm.sea.end(), pid) == dm.sea.end())
            dm.sea.push_back(pid);
        if (!rids.isEmpty()) {
            doc.mod.regions.get(*rid)->provinces.push_back(pid);
            doc._sync_regions();
        }
        if (continent && !continent->isEmpty())
            doc.map<mapfiles::MembershipFile>(u"continent"_s).assign({pid}, *continent);
        if (climate && !climate->isEmpty())
            doc.map<mapfiles::MembershipFile>(u"climate"_s).assign({pid}, *climate);
        if (want)
            doc.prov_want.insert(pid, *want);
    }, rids, {pid}, {u"definitions"_s, u"default_map"_s, u"continent"_s, u"climate"_s});
}

long long finish_province(document::Document &doc, long long pid, const std::vector<std::pair<long long, double>> &moves,
                          const OrderedMap<long long, positions::Entry> &entries,
                          const std::vector<adjacency::RowFields> &add_specs, const QList<long long> &stale_uids,
                          std::optional<long long> done_version, long long done_area)
{
    // After a new province was painted: move pop shares [(donor, share)] into it, set positions entries {pid: entry},
    // add / remove adjacency rows — one undo step.
    auto &pf = doc.map<positions::PositionsFile>(u"positions"_s);
    auto &adj = doc.map<adjacency::AdjacencyFile>(u"adjacency"_s);
    ProvinceDefs &defs = doc.map<ProvinceDefs>(u"definitions"_s);
    QList<long long> pids = {pid};
    for (const auto &m : moves)
        pids << m.first;
    return doc.edit(u"Finish new province #%1"_s.arg(pid), pids, [&]() -> long long {
        long long moved = 0;
        for (const auto &[d, share] : moves) {
            const auto *lst = doc.pops.get(d);
            const size_t n = lst ? lst->size() : 0;
            for (size_t i = 0; i < n; ++i) {
                moddata::Pop &p = (*doc.pops.get(d))[i];    // (fetched again: adding to pid may move the lists)
                const auto parts = document::distribute(p.size, {1.0 - share, share});
                const long long keep = parts[0], go = parts[1];
                if (go > 0) {
                    p.size = keep;
                    const moddata::Pop tmpl = p;
                    doc._add_to(pid, tmpl, go);
                    moved += go;
                }
            }
            doc._prune({d});
        }
        for (const auto &[q, e] : entries)
            pf.set_entry(q, e);
        if (!stale_uids.isEmpty())
            adj.del(stale_uids);
        if (!add_specs.empty())
            adj.add(add_specs);
        defs.update(pid, done_version, done_area);
        return moved;
    }, {}, {}, {u"definitions"_s, u"positions"_s, u"adjacency"_s});
}

}  // namespace newprov
