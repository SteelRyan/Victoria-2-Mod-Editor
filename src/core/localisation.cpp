// localisation.py — names and how the editor writes them. Line by line.
#include "core/localisation.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"

#include <QDateTime>
#include <QHash>
#include <algorithm>
#include <variant>

using namespace Qt::StringLiterals;

namespace localisation {

using pypath::join;

const QString OWN_FILE = u"ModEditor_Additions.csv"_s;
const QString LEGACY_FILE = u"FM_Additions.csv"_s;
const QString RULE = u"#############################################;;;;;;;;;;;;;;x"_s;
const QStringList HEADER = {RULE, u"#ModEditor additions;;;;;;;;;;;;;;x"_s, RULE};
const QString SECTION_END = u"##############;;;;;;;;;;;;;x"_s;
const std::vector<std::pair<QString, QString>> SECTIONS = {
    {u"prov"_s, u"Added_Provinces"_s},     {u"state"_s, u"Added_States"_s},       {u"country"_s, u"Added_Nations"_s},
    {u"culture"_s, u"Added_Cultures"_s},   {u"religion"_s, u"Added_Religions"_s}, {u"continent"_s, u"Added_Continents"_s},
    {u"other"_s, u"Added_Misc"_s}};
const OrderedMap<QString, QString> TITLES = [] {
    OrderedMap<QString, QString> m;
    for (const auto &[k, t] : SECTIONS)
        m.insert(k, t);
    return m;
}();

static QString stamp_now() { return QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s); }

QString own_file(const moddata::Mod &mod)
{
    // The file new keys go to: the mod's FM_Additions.csv if it has one, else ModEditor_Additions.csv.
    if (!mod.root.isEmpty() && fs::isfile(join(mod.root, {u"localisation"_s, LEGACY_FILE})))
        return LEGACY_FILE;
    return OWN_FILE;
}

QString banner(const QString &title) { return u"#############%1##############;;;;;;;;;;;;;x"_s.arg(title); }

QString entry(const QString &key, const QString &text) { return u"%1;%2;;;;;;;;;;;;;x"_s.arg(key, text); }

// Windows-1252's characters for its bytes 0x80..0x9F (0: none — Python's codec refuses them)
static const char16_t CP1252_HIGH[32] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};

QString clean(const QString &text)
{
    // " ".join(str(text).replace(";", ",").split()).encode("cp1252", "replace").decode("latin-1")
    QString t = text;
    t.replace(u';', u',');
    t = py::split(t).join(u' ');
    QString out;
    out.reserve(t.size());
    for (qsizetype i = 0; i < t.size(); ++i) {
        const char16_t u = t[i].unicode();
        if (u < 0x80 || (u >= 0xA0 && u <= 0xFF)) {
            out += QChar(u);
            continue;
        }
        if (QChar::isHighSurrogate(u) && i + 1 < t.size() && QChar::isLowSurrogate(t[i + 1].unicode()))
            ++i;                                  // (one character for Python: one '?')
        char16_t b = u'?';
        for (int k = 0; k < 32; ++k)
            if (CP1252_HIGH[k] == u && u != 0) {
                b = char16_t(0x80 + k);
                break;
            }
        out += QChar(b);
    }
    return out;
}

QString section_of(const QString &key)
{
    const QString k = py::upper(key);
    if (k.startsWith(u"PROV") && py::isdigit(k.mid(4)))
        return u"prov"_s;
    return u"other"_s;
}

std::vector<Where> loc_dirs(const moddata::Mod &mod)
{
    std::vector<Where> dirs{{join(mod.root, u"localisation"_s), true}};
    if (mod.base && !mod.replaced({u"localisation"_s}))
        dirs.emplace_back(join(*mod.base, u"localisation"_s), false);
    std::vector<Where> out;
    for (const auto &d : dirs)
        if (fs::isdir(d.first))
            out.push_back(d);
    return out;
}

std::vector<Where> loc_files(const moddata::Mod &mod)
{
    // [(path, in the mod?)] in the order the game reads them (Mod.loc_files — measured).
    return mod.loc_files();
}

namespace {
struct IndexEntry {
    std::vector<std::pair<QString, std::optional<std::pair<long long, long long>>>> sig;
    OrderedMap<QString, Where> idx;
};
QHash<QString, IndexEntry> &index_cache()
{
    static QHash<QString, IndexEntry> c;          // mod folder -> (files' signature, {key: (path, in the mod?)})
    return c;
}
}  // namespace

const OrderedMap<QString, Where> &_index(const moddata::Mod &mod)
{
    // Where every key is defined (the first definition with a text, in the game's order) — read once, again only
    // when a localisation file changed on disk.
    const auto files = loc_files(mod);
    std::vector<std::pair<QString, std::optional<std::pair<long long, long long>>>> sig;
    for (const auto &[path, own] : files)
        sig.emplace_back(path, fs::stat_sig(path));
    auto &cache = index_cache();
    auto it = cache.find(mod.root);
    if (it != cache.end() && it->sig == sig)
        return it->idx;
    OrderedMap<QString, Where> idx;
    for (const auto &[path, own] : files) {
        QString text;
        try {
            text = script::read_text(path);
        } catch (const fs::OSError &) {
            continue;
        }
        for (const QString &line : py::splitlines(text)) {
            if (line.isEmpty() || line.startsWith(u'#'))
                continue;
            const QStringList parts = py::split(line, u";"_s, 2);
            if (parts.size() > 1 && !parts[1].isEmpty() && !idx.contains(parts[0]))
                idx.insert(parts[0], {path, own});
        }
    }
    cache.insert(mod.root, IndexEntry{sig, idx});
    return cache.find(mod.root)->idx;
}

OrderedMap<QString, Where> defined_where(const moddata::Mod &mod, const QStringList &keys)
{
    // {key: (path, in the mod?)} of the definition the game uses, for the given keys.
    const auto &idx = _index(mod);
    OrderedMap<QString, Where> out;
    for (const QString &k : keys)
        if (const Where *w = idx.get(k))
            out.insert(k, *w);
    return out;
}

// _natural: sort key that puts NEW_2 before NEW_12 — [int(p) if p.isdigit() else p.lower() for p in
// re.split(r"(\d+)", key)]
using NatPart = std::variant<long long, QString>;
static std::vector<NatPart> _natural(const QString &key)
{
    // re.split(r"(\d+)", key) alternates text, number, text … — starting and ending with a text (maybe "")
    std::vector<NatPart> out;
    const qsizetype n = key.size();
    auto digit = [&](qsizetype i) { return key[i].category() == QChar::Number_DecimalDigit; };
    qsizetype i = 0;
    while (true) {
        qsizetype start = i;
        while (i < n && !digit(i))
            ++i;
        out.emplace_back(py::lower(key.mid(start, i - start)));
        if (i >= n)
            break;
        start = i;
        while (i < n && digit(i))
            ++i;
        out.emplace_back(py::to_int(key.mid(start, i - start)));
    }
    return out;
}
QString _add_to_own(const QString &text, const OrderedMap<QString, std::vector<std::pair<QString, QString>>> &entries)
{
    // OWN_FILE's text with entries {section: [(key, text)]} added: into their section (before its closing line),
    // new sections at the end.
    const QString nl = (text.contains(u"\r\n"_s) || text.isEmpty()) ? u"\r\n"_s : u"\n"_s;
    QStringList lines = text.isEmpty() ? HEADER : text.split(nl);
    while (!lines.isEmpty() && lines.back().isEmpty())
        lines.removeLast();
    for (const auto &[sec, _title] : SECTIONS) {
        const auto *items = entries.get(sec);
        if (!items || items->empty())
            continue;
        const QString head = banner(TITLES.value(sec));
        std::vector<std::pair<QString, QString>> sorted_items = *items;
        std::stable_sort(sorted_items.begin(), sorted_items.end(),
                         [](const auto &a, const auto &b) { return _natural(a.first) < _natural(b.first); });
        QStringList fresh;
        for (const auto &[k, t] : sorted_items)
            fresh << entry(k, t);
        const qsizetype hi = lines.indexOf(head);
        if (hi >= 0) {
            qsizetype i = hi + 1;
            while (i < lines.size() && !lines[i].startsWith(u'#'))
                ++i;
            for (qsizetype j = 0; j < fresh.size(); ++j)
                lines.insert(i + j, fresh[j]);
        } else {
            lines << head << fresh << SECTION_END;
        }
    }
    return lines.join(nl) + nl;
}

QStringList write_names(moddata::Mod &mod, const OrderedMap<QString, QString> &names0, const QString &backup_root,
                        const OrderedMap<QString, QString> &sections)
{
    // Write localisation names {key: text}. Keys the mod already defines are changed where the game reads them; the
    // others are added to OWN_FILE. Returns the written paths. mod.loc is updated.
    if (names0.isEmpty())
        return {};
    OrderedMap<QString, QString> names;
    for (const auto &[k, v] : names0)
        if (!k.isEmpty())
            names.insert(k, clean(v));
    QStringList keys;
    for (const auto &kv : names)
        keys << kv.first;
    const OrderedMap<QString, Where> where = defined_where(mod, keys);
    const QString bdir = join(backup_root, {u"localisation"_s, stamp_now()});
    QStringList written;
    OrderedMap<QString, OrderedMap<QString, QString>> in_place;
    for (const auto &[k, t] : names) {
        const Where *w = where.get(k);
        if (!w)
            continue;
        QString path = w->first;
        if (!w->second) {
            // defined in a base game file: the mod gets its own copy of that file (same name — it takes the base
            // file's place in the game) and the name is changed there
            path = join(mod.root, {u"localisation"_s, pypath::basename(w->first)});
            if (!fs::isfile(path)) {
                fs::makedirs(pypath::dirname(path));
                fs::copy2(w->first, path);
                written << path;
            }
        }
        in_place[path].insert(k, t);
    }
    for (const auto &[path, todo] : in_place) {
        const QString text = script::read_text(path);
        const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
        QStringList lines = text.split(nl);
        QSet<QString> done;
        for (QString &line : lines) {
            if (line.isEmpty() || line.startsWith(u'#'))
                continue;
            QStringList parts = line.split(u';');
            const QString k = parts[0];
            if (todo.contains(k) && !done.contains(k) && parts.size() > 1 && !parts[1].isEmpty()) {
                parts[1] = *todo.get(k);
                line = parts.join(u';');
                done.insert(k);                   // only the first one: that's the one the game reads
            }
        }
        fs::makedirs(bdir);
        fs::copy2(path, join(bdir, pypath::basename(path)));
        script::write_text(path, lines.join(nl));
        if (!written.contains(path))
            written << path;
    }
    OrderedMap<QString, std::vector<std::pair<QString, QString>>> add;
    for (const auto &[k, t] : names) {
        if (where.contains(k))
            continue;
        const QString *s = sections.get(k);
        add[(s && !s->isEmpty()) ? *s : section_of(k)].emplace_back(k, t);
    }
    if (!add.isEmpty()) {
        const QString d = join(mod.root, u"localisation"_s);
        fs::makedirs(d);
        const QString own = own_file(mod);
        const QString path = join(d, own);
        const QString text = fs::exists(path) ? script::read_text(path) : QString();
        if (!text.isEmpty()) {
            fs::makedirs(bdir);
            fs::copy2(path, join(bdir, own));
        }
        script::write_text(path, _add_to_own(text, add));
        written << path;
    }
    for (const auto &[k, t] : names)
        mod.loc.insert(k, t);
    return written;
}

QString _where_text(const std::optional<Where> &w, const moddata::Mod &mod)
{
    // Where a key's name is written: its file — a base game file's copy in the mod — or the own file (a new key).
    if (!w)
        return own_file(mod);
    return w->second ? pypath::basename(w->first)
                     : u"%1 (the game's file, copied into the mod)"_s.arg(pypath::basename(w->first));
}

// ---------------------------------------------------------------------- LocEdits
OptStr LocEdits::text(const QString &key) const
{
    if (const auto *v = fresh.get(key))
        return v->first;
    const QString *l = mod.loc.get(key);
    return l ? OptStr(*l) : std::nullopt;
}

void LocEdits::set(const QString &key, const QString &text0, const OptStr &section)
{
    const QString t = py::split(text0).join(u' ');
    if (t.isEmpty())
        return;
    const QString *l = mod.loc.get(key);
    if (l && t == *l)
        fresh.remove(key);
    else
        fresh.insert(key, {t, (section && !section->isEmpty()) ? *section : section_of(key)});
    _show();
}

void LocEdits::drop(const QString &key)
{
    // A name set in this session goes again (e.g. the technology it was for was removed).
    if (fresh.remove(key))
        _show();
}

void LocEdits::prefetch(const QStringList &keys0)
{
    // Look up where several keys are written in one pass over the files.
    QStringList keys;
    for (const QString &k : keys0)
        if (!k.isEmpty() && !_where.contains(k))
            keys << k;
    if (!keys.isEmpty()) {
        const auto found = defined_where(mod, keys);
        for (const QString &k : keys) {
            const Where *w = found.get(k);
            _where.insert(k, _where_text(w ? std::optional<Where>(*w) : std::nullopt, mod));
        }
    }
}

QString LocEdits::where(const QString &key)
{
    // Where the key's text is / will be written: a file name, or OWN_FILE for a new one.
    if (!_where.contains(key)) {
        const auto found = defined_where(mod, {key});
        const Where *w = found.get(key);
        _where.insert(key, _where_text(w ? std::optional<Where>(*w) : std::nullopt, mod));
    }
    return *_where.get(key);
}

void LocEdits::_show()
{
    // Province / state names in the editor follow the edits (and go back on undo).
    QStringList keys;
    for (const auto &kv : fresh)
        keys << kv.first;
    for (const auto &kv : _orig)
        if (!keys.contains(kv.first))
            keys << kv.first;
    for (const QString &key : keys) {
        auto now = [&](const QString &orig) { return fresh.contains(key) ? fresh.get(key)->first : orig; };
        if (key.startsWith(u"PROV") && py::isdigit(key.mid(4))) {
            if (auto *pr = mod.provinces.get(py::to_int(key.mid(4)))) {
                if (!_orig.contains(key))
                    _orig.insert(key, pr->name);
                pr->name = now(*_orig.get(key));
            }
        } else if (auto *r = mod.regions.get(key)) {
            if (!_orig.contains(key))
                _orig.insert(key, r->name);
            r->name = now(*_orig.get(key));
        } else if (auto *c = mod.cultures.get(key)) {
            if (!_orig.contains(key))
                _orig.insert(key, c->name);
            c->name = now(*_orig.get(key));
        } else if (auto *rl = mod.religions.get(key)) {
            if (!_orig.contains(key))
                _orig.insert(key, rl->name);
            rl->name = now(*_orig.get(key));
        }
    }
}

store::Snap LocEdits::snapshot() const
{
    // tuple(sorted(self.new.items())) — (key, (text, section)) pairs, sorted
    std::vector<std::pair<QString, std::pair<QString, QString>>> v(fresh.begin(), fresh.end());
    std::sort(v.begin(), v.end());
    store::Items it;
    for (const auto &[k, val] : v)
        it.insert(store::Key(k), store::box(val));
    return std::make_shared<const store::ItemsSnap>(it, true);
}

void LocEdits::restore(const store::Snap &snap)
{
    fresh.clear();
    for (const auto &[k, val] : dynamic_cast<const store::ItemsSnap &>(*snap).items)
        fresh.insert(k.text(), store::unbox<std::pair<QString, QString>>(val));
    _show();
}

QStringList LocEdits::dirty() const
{
    QStringList out;
    for (const auto &kv : fresh)
        out << kv.first;
    std::sort(out.begin(), out.end());
    return out;
}

// --- undo past an Apply: a name not in a step's picture was "as in the files" — said outright once they change
store::Snap LocEdits::rebase_begin()
{
    OrderedMap<QString, std::pair<OptStr, QString>> tok;
    for (const auto &[k, ts] : fresh) {
        const QString *l = mod.loc.get(k);
        tok.insert(k, {l ? OptStr(*l) : std::nullopt, ts.second});
    }
    return store::box(tok);
}

store::Snap LocEdits::rebase(const store::Snap &snap, const store::Snap &old_)
{
    OrderedMap<QString, std::pair<QString, QString>> d;
    for (const auto &[k, val] : dynamic_cast<const store::ItemsSnap &>(*snap).items)
        d.insert(k.text(), store::unbox<std::pair<QString, QString>>(val));
    const auto &old = store::unbox<OrderedMap<QString, std::pair<OptStr, QString>>>(old_);
    for (const auto &[k, ts] : old)
        if (!d.contains(k) && ts.first && !ts.first->isEmpty())
            d.insert(k, {*ts.first, ts.second});
    std::vector<std::pair<QString, std::pair<QString, QString>>> v;
    for (const auto &[k, val] : d) {
        const QString *l = mod.loc.get(k);
        if (!l || val.first != *l)
            v.emplace_back(k, val);
    }
    std::sort(v.begin(), v.end());
    store::Items it;
    for (const auto &[k, val] : v)
        it.insert(store::Key(k), store::box(val));
    return std::make_shared<const store::ItemsSnap>(it, true);
}

std::optional<QStringList> LocEdits::save(const QString &backup_root)
{
    if (fresh.isEmpty())
        return std::nullopt;
    OrderedMap<QString, QString> names, secs;
    for (const auto &[k, ts] : fresh) {
        names.insert(k, ts.first);
        secs.insert(k, ts.second);
    }
    const QStringList written = write_names(mod, names, backup_root, secs);
    fresh.clear();
    _orig.clear();
    _where.clear();
    return written;
}

}  // namespace localisation
