// refs.py — the words of the scripts and what they are. Line by line.
#include "core/refs.h"
#include "core/cbcheck.h"
#include "core/countryhist.h"
#include "core/events.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pyre.h"
#include "core/pytext.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace refs {

using events::Entry;

const QString MODS_REL = u"common/event_modifiers.txt"_s;
const QStringList EXTRA_MOD_FILES = {u"common/static_modifiers.txt"_s, u"common/triggered_modifiers.txt"_s};
const QString REBEL_REL = u"common/rebel_types.txt"_s;

static OrderedMap<QString, std::pair<QString, std::tuple<int, int, int>>> make_kinds()
{
    OrderedMap<QString, std::pair<QString, std::tuple<int, int, int>>> m;
    const std::vector<std::tuple<QString, QString, int, int, int>> l = {
        {u"event"_s, u"Event"_s, 232, 196, 118},          {u"decision"_s, u"Decision"_s, 246, 160, 80},
        {u"modifier"_s, u"Modifier"_s, 200, 140, 255},    {u"cflag"_s, u"Country flag"_s, 90, 220, 190},
        {u"gflag"_s, u"Global flag"_s, 90, 220, 190},     {u"pflag"_s, u"Province flag"_s, 90, 220, 190},
        {u"var"_s, u"Variable"_s, 120, 210, 170},         {u"nation"_s, u"Nation"_s, 255, 125, 200},
        {u"province"_s, u"Province"_s, 90, 215, 255},     {u"state"_s, u"State"_s, 150, 185, 255},
        {u"culture"_s, u"Culture"_s, 140, 225, 110},      {u"cgroup"_s, u"Culture group"_s, 140, 225, 110},
        {u"religion"_s, u"Religion"_s, 240, 220, 100},    {u"rgroup"_s, u"Religion group"_s, 240, 220, 100},
        {u"goods"_s, u"Goods"_s, 210, 165, 115},          {u"tech"_s, u"Technology"_s, 130, 170, 250},
        {u"invention"_s, u"Invention"_s, 130, 170, 250},  {u"ideology"_s, u"Ideology"_s, 235, 190, 150},
        {u"issue"_s, u"Reform"_s, 235, 190, 150},         {u"option"_s, u"Reform option"_s, 235, 190, 150},
        {u"government"_s, u"Government"_s, 235, 190, 150}, {u"nv"_s, u"National value"_s, 235, 190, 150},
        {u"poptype"_s, u"Pop type"_s, 200, 200, 170},     {u"continent"_s, u"Continent"_s, 150, 185, 255},
        {u"loc"_s, u"Text (localisation)"_s, 175, 175, 175}, {u"cb"_s, u"War goal"_s, 250, 138, 104},
        {u"rebel"_s, u"Rebel type"_s, 214, 214, 122}};
    for (const auto &[k, n, r, g, b] : l)
        m.insert(k, {n, {r, g, b}});
    return m;
}
const OrderedMap<QString, std::pair<QString, std::tuple<int, int, int>>> KINDS = make_kinds();
static OrderedMap<QString, QString> om(std::initializer_list<std::pair<QString, QString>> l)
{
    OrderedMap<QString, QString> m;
    for (const auto &kv : l)
        m.insert(kv.first, kv.second);
    return m;
}
const OrderedMap<QString, QString> ROLES = om({{u"def"_s, u"defines it"_s},   {u"fire"_s, u"fires it"_s},
                                               {u"set"_s, u"sets it"_s},      {u"clear"_s, u"clears it"_s},
                                               {u"check"_s, u"checks it"_s},  {u"add"_s, u"adds it"_s},
                                               {u"remove"_s, u"removes it"_s}, {u"change"_s, u"changes it"_s},
                                               {u"use"_s, u"uses it"_s},      {u"scope"_s, u"acts in it"_s}});
const QSet<QString> COND_BLOCKS = {
    u"trigger"_s, u"potential"_s, u"allow"_s, u"mean_time_to_happen"_s, u"limit"_s, u"ai_will_do"_s, u"ai_chance"_s,
    u"modifier"_s, u"can_use"_s, u"is_valid"_s, u"allowed_states"_s, u"allowed_states_in_crisis"_s,
    u"allowed_substate_regions"_s, u"allowed_countries"_s, u"prerequisites"_s,
    // rebel types and pop types
    u"will_rise"_s, u"spawn_chance"_s, u"movement_evaluation"_s, u"siege_won_trigger"_s, u"demands_met_trigger"_s,
    u"demands_enforced_trigger"_s, u"promote_to"_s, u"migration_target"_s, u"country_migration_target"_s};
static const OrderedMap<QString, QString> CB_GIVE = om({{u"casus_belli"_s, u"add"_s}, {u"add_casus_belli"_s, u"add"_s},
                                                        {u"remove_casus_belli"_s, u"remove"_s}});
static const QStringList CB_GOALS = {u"war_goal"_s, u"attacker_goal"_s, u"defender_goal"_s};
const QSet<QString> CULTURE_KEYS = {u"culture"_s, u"primary_culture"_s, u"is_primary_culture"_s, u"is_accepted_culture"_s,
                                    u"add_accepted_culture"_s, u"remove_accepted_culture"_s, u"has_pop_culture"_s,
                                    u"is_culture_group"_s, u"culture_group"_s, u"accepted_culture"_s};
const QSet<QString> RELIGION_KEYS = {u"religion"_s, u"has_pop_religion"_s, u"is_state_religion"_s, u"state_religion"_s};
const QSet<QString> PROV_KEYS = {u"province_id"_s, u"capital"_s, u"owns"_s,     u"controls"_s,
                                 u"add_core"_s,    u"remove_core"_s, u"is_core"_s, u"province"_s};
static const OrderedMap<QString, std::pair<QString, QString>> FLAG_KEYS = [] {
    OrderedMap<QString, std::pair<QString, QString>> m;
    for (const auto &[k, a, b] : std::vector<std::tuple<QString, QString, QString>>{
             {u"set_country_flag"_s, u"cflag"_s, u"set"_s}, {u"clr_country_flag"_s, u"cflag"_s, u"clear"_s},
             {u"has_country_flag"_s, u"cflag"_s, u"check"_s}, {u"set_global_flag"_s, u"gflag"_s, u"set"_s},
             {u"clr_global_flag"_s, u"gflag"_s, u"clear"_s}, {u"has_global_flag"_s, u"gflag"_s, u"check"_s},
             {u"set_province_flag"_s, u"pflag"_s, u"set"_s}, {u"clr_province_flag"_s, u"pflag"_s, u"clear"_s},
             {u"has_province_flag"_s, u"pflag"_s, u"check"_s}})
        m.insert(k, {a, b});
    return m;
}();
static const OrderedMap<QString, QString> MOD_KEYS = om({{u"has_country_modifier"_s, u"check"_s},
                                                         {u"has_province_modifier"_s, u"check"_s},
                                                         {u"remove_country_modifier"_s, u"remove"_s},
                                                         {u"remove_province_modifier"_s, u"remove"_s}});
static const OrderedMap<QString, QString> VAR_PARENTS = om({{u"set_variable"_s, u"set"_s}, {u"change_variable"_s, u"change"_s},
                                                            {u"check_variable"_s, u"check"_s}});
static const OrderedMap<QString, QString> SIMPLE = om({
    {u"trade_goods"_s, u"goods"_s}, {u"region"_s, u"state"_s}, {u"continent"_s, u"continent"_s},
    {u"activate_technology"_s, u"tech"_s}, {u"activate_invention"_s, u"invention"_s}, {u"invention"_s, u"invention"_s},
    {u"government"_s, u"government"_s}, {u"ruling_party_ideology"_s, u"ideology"_s}, {u"enable_ideology"_s, u"ideology"_s},
    {u"is_ideology_enabled"_s, u"ideology"_s}, {u"nationalvalue"_s, u"nv"_s}, {u"political_reform"_s, u"option"_s},
    {u"social_reform"_s, u"option"_s}, {u"type"_s, u"poptype"_s}, {u"has_pop_type"_s, u"poptype"_s},
    {u"pop_type"_s, u"poptype"_s}});
const QSet<QString> NOT_NAMES = {u"THIS"_s, u"ROOT"_s, u"FROM"_s, u"yes"_s, u"no"_s};
// blocks that change whom / where the lines inside act on (a flag set in `TAG = { … }` is TAG's, not the event's)
const QSet<QString> SCOPE_KEYS = {
    u"any_country"_s, u"random_country"_s, u"all_country"_s, u"any_neighbor_country"_s, u"war_countries"_s,
    u"any_greater_power"_s, u"any_sphere_member"_s, u"overlord"_s, u"sphere_owner"_s, u"owner"_s, u"controller"_s,
    u"FROM"_s, u"THIS"_s, u"ROOT"_s, u"capital_scope"_s, u"any_owned_province"_s, u"any_owned"_s, u"any_core"_s,
    u"all_core"_s, u"random_owned"_s, u"any_state"_s, u"random_state"_s, u"any_pop"_s, u"random_pop"_s, u"location"_s,
    u"cultural_union"_s, u"any_neighbor_province"_s, u"random_neighbor_province"_s, u"random_province"_s,
    u"any_province"_s, u"random_list"_s, u"random"_s};

static bool loc_key(const QString &v) { return py::re_match(u"^[A-Za-z0-9_.\\-]+$"_s, v).hasMatch(); }
static bool is_tag(const QString &v) { return countryhist::tag_ok(v); }

QString esc(const QString &s)
{
    QString out;
    for (QChar c : s) {
        switch (c.unicode()) {
        case u'&': out += u"&amp;"_s; break;
        case u'<': out += u"&lt;"_s; break;
        case u'>': out += u"&gt;"_s; break;
        case u'"': out += u"&quot;"_s; break;
        case u'\'': out += u"&#x27;"_s; break;
        default: out += c;
        }
    }
    return out;
}

OptStr loc_text(moddata::Mod &mod, const QString &key)
{
    // The localisation text of a key, or None — the game doesn't mind upper / lower case.
    if (const QString *t = mod.loc.get(key))
        return *t;
    if (key.isEmpty())
        return std::nullopt;
    if (!mod.loc_lower || mod.loc_lower->first != mod.loc.size()) {
        QHash<QString, QString> low;
        for (const auto &[k, v] : mod.loc)
            low.insert(py::lower(k), v);
        mod.loc_lower = std::make_pair(mod.loc.size(), low);
    }
    const auto it = mod.loc_lower->second.constFind(py::lower(key));
    return it == mod.loc_lower->second.cend() ? std::nullopt : OptStr(*it);
}

long long line_of(const QString &text, qsizetype pos) { return py::head(text, pos).count(u'\n') + 1; }

// ---------------------------------------------------------------------- building
RefIndex::RefIndex(moddata::Mod &mod_, events::EventDB &db_, const std::function<QString(const QString &)> &text_of_,
                   const QStringList &extra)
    : mod(mod_), db(&db_), text_of(text_of_)
{
    if (const countryhist::NationData *data = mod.nation_data.get()) {
        for (const auto &kv : data->techs)
            techs.insert(kv.first);
        for (const auto &kv : data->inventions)
            inventions.insert(kv.first);
        issues = data->reform_issues;
        for (const auto &kv : data->reform_of)
            options.insert(kv.first);
    }
    for (const auto &[rel, nodes] : db->trees)
        _index(rel, db->texts.value(rel), *nodes);
    QStringList more{MODS_REL};
    more << EXTRA_MOD_FILES << cbcheck::REL << extra;
    for (const QString &rel : more) {
        QString t;
        try {
            t = text_of(rel);
        } catch (const fs::OSError &) {
            t = QString();
        }
        _index(rel, t, script::parse(t));
    }
    _finish();
}

void RefIndex::update(const QString &rel, const QString &text, const Nodes *nodes)
{
    // One file's text changed: its refs again.
    if (const auto *lst = spans.get(rel))
        for (const Ref &r : *lst)
            if (auto *u = uses.get({r.kind, r.target}); u && !u->empty())
                std::erase_if(*u, [&](const Use &x) { return x.rel == rel; });
    const bool modfile = rel == MODS_REL || EXTRA_MOD_FILES.contains(rel);
    if (modfile)
        modifiers.clear();
    if (rel == cbcheck::REL)
        cbs.clear();
    if (nodes) {
        _index(rel, text, *nodes);
    } else {
        const Nodes parsed = script::parse(text);
        _index(rel, text, parsed);
    }
    if (modfile)
        for (const QString &r2 : QStringList{MODS_REL} + EXTRA_MOD_FILES)
            if (const auto *lst = spans.get(r2))
                for (const Ref &r : *lst)
                    if (r.kind == u"modifier" && r.role == u"def")
                        modifiers.insert(r.target);
    _finish();
}

void RefIndex::_index(const QString &rel, const QString &text, const Nodes &nodes)
{
    std::vector<Ref> out;
    _scope.reset();
    _walk(rel, text, nodes, {}, out);
    std::stable_sort(out.begin(), out.end(), [](const Ref &a, const Ref &b) { return a.pos < b.pos; });
    std::vector<qsizetype> starts;
    for (const Ref &r : out)
        starts.push_back(r.pos);
    if (rel == cbcheck::REL) {
        cb_spans.clear();
        for (const Node &n : nodes)
            if (n.has_key && !n.key.isEmpty() && n.is_block && n.key != u"peace_order" && !cbcheck::KNOWN.contains(n.key))
                cb_spans.emplace_back(n.kstart, n.vend, n.key);
    }
    for (const Ref &r : out) {
        uses[{r.kind, r.target}].push_back(Use{rel, r.pos, r.role});
        if (r.kind == u"modifier" && r.role == u"def")
            modifiers.insert(r.target);
        else if (r.kind == u"cb" && r.role == u"def")
            cbs.insert(r.target);
        else if (r.kind == u"rebel" && r.role == u"def")
            rebels.insert(r.target);
    }
    spans.insert(rel, std::move(out));
    _starts.insert(rel, std::move(starts));
}

void RefIndex::_finish()
{
    // The `ok` of every ref (after all files are in: a flag is fine when something anywhere sets it).
    const QSet<QString> none;
    auto setters = [&](const QString &kind) -> const QSet<QString> * {
        if (kind == u"cflag")
            return &db->set_flags;
        if (kind == u"gflag")
            return &db->set_gflags;
        if (kind == u"pflag")
            return &none;
        if (kind == u"var")
            return &db->set_vars;
        return nullptr;
    };
    QSet<ThingKey> is_set;
    for (const auto &[k, lst] : uses)
        if (setters(k.first) && std::any_of(lst.begin(), lst.end(), [](const Use &u) { return u.role == u"set" || u.role == u"change"; }))
            is_set.insert(k);
    QHash<ThingKey, bool> cache;
    for (auto &[rel, lst] : spans) {
        for (Ref &r : lst) {
            if (const QSet<QString> *s = setters(r.kind); s && r.role == u"check") {
                r.ok = s->contains(r.target) || is_set.contains({r.kind, r.target});
            } else if (r.kind == u"event" && r.role == u"def") {   // an id of its own: no other event may have it
                long long n = 0;
                if (const auto *u = uses.get({r.kind, r.target}))
                    for (const Use &x : *u)
                        n += x.role == u"def";
                r.ok = n <= 1;
            } else {
                const ThingKey k{r.kind, r.target};
                if (!cache.contains(k))
                    cache.insert(k, _ok(r.kind, r.target));
                r.ok = cache.value(k);
            }
        }
    }
}

bool RefIndex::_ok(const QString &kind, const QString &t)
{
    if (kind == u"event")
        return db->by_id.contains(t);
    if (kind == u"loc")
        return loc_text(mod, t).has_value();
    if (kind == u"modifier")
        return modifiers.contains(t);
    if (kind == u"cb")
        return cbs.contains(t);
    if (kind == u"culture")
        return mod.cultures.contains(t);
    if (kind == u"religion")
        return mod.religions.contains(t);
    if (kind == u"goods")
        return mod.goods.contains(t);
    if (kind == u"state")
        return mod.regions.contains(t);
    if (kind == u"poptype")
        return mod.poptypes.contains(t);
    return true;
}

void RefIndex::_add(std::vector<Ref> &out, qsizetype pos, qsizetype length, const QString &kind, const QString &target,
                    const QString &role)
{
    out.push_back(Ref{pos, length, kind, target, role, true, _scope});
}

void RefIndex::_walk(const QString &rel, const QString &text, const Nodes &nodes, const QStringList &ctx,
                     std::vector<Ref> &out, const OptStr &scope)
{
    const OptStr parent = ctx.isEmpty() ? std::nullopt : OptStr(ctx.last());
    const bool cond = std::any_of(ctx.begin(), ctx.end(), [](const QString &c) { return COND_BLOCKS.contains(c); });
    for (const Node &n : nodes) {
        if (!n.has_key) {
            if (rel == cbcheck::REL && ctx == QStringList{u"peace_order"_s} && !n.is_block && !n.value.isEmpty() &&
                !cbcheck::GAME_ORDER_ONLY.contains(n.value)) {   // (the order of the war goals in a peace: their names)
                _scope = scope;
                _add(out, n.kstart, n.value.size(), u"cb"_s, n.value, u"use"_s);
            }
            continue;
        }
        const QString &k = n.key;
        const QString kl = py::lower(k);
        _scope = scope;
        if (n.is_block) {
            const bool modfile = rel == MODS_REL || EXTRA_MOD_FILES.contains(rel);
            if (ctx.isEmpty() && rel == cbcheck::REL) {
                if (k != u"peace_order" && !cbcheck::KNOWN.contains(k))
                    _add(out, n.kstart, k.size(), u"cb"_s, k, u"def"_s);
            } else if (ctx.isEmpty() && rel == REBEL_REL) {
                _add(out, n.kstart, k.size(), u"rebel"_s, k, u"def"_s);
            } else if (is_tag(k) && mod.countries.contains(k)) {
                _add(out, n.kstart, k.size(), u"nation"_s, k, u"scope"_s);
            } else if (py::isdigit(k) && mod.provinces.contains(py::to_int(k))) {
                _add(out, n.kstart, k.size(), u"province"_s, k, u"scope"_s);
            } else if (mod.regions.contains(k)) {
                _add(out, n.kstart, k.size(), u"state"_s, k, u"scope"_s);
            } else if (ctx.isEmpty() && modfile) {
                _add(out, n.kstart, k.size(), u"modifier"_s, k, u"def"_s);
            } else if (parent == OptStr(u"political_decisions"_s) && ctx.size() == 1 && rel.startsWith(u"decisions/")) {
                _add(out, n.kstart, k.size(), u"decision"_s, k, u"def"_s);
            }
            OptStr inner = scope;
            if ((is_tag(k) && mod.countries.contains(k)) || py::isdigit(k) || SCOPE_KEYS.contains(k) || mod.regions.contains(k))
                inner = k;
            QStringList c2 = ctx;
            c2 << kl;
            _walk(rel, text, n.kids, c2, out, inner);
            continue;
        }
        const QString &v = n.value;
        if (v.isEmpty())
            continue;
        const qsizetype vpos = n.vstart + (py::slice(text, n.vstart, n.vstart + 1) == u"\"" ? 1 : 0);
        const qsizetype vl = v.size();
        const QString role = cond ? u"check"_s : u"use"_s;
        // a key that is itself a name: a technology, a reform
        if (techs.contains(kl)) {
            _add(out, n.kstart, k.size(), u"tech"_s, k, role);
            continue;
        }
        if (issues.contains(k)) {
            _add(out, n.kstart, k.size(), u"issue"_s, k, role);
            if (options.contains(v))
                _add(out, vpos, vl, u"option"_s, v, role);
            continue;
        }
        if (kl == u"id" && (parent == OptStr(u"country_event"_s) || parent == OptStr(u"province_event"_s))) {
            _add(out, vpos, vl, u"event"_s, v, (ctx.size() == 1 && rel.startsWith(u"events/")) ? u"def"_s : u"fire"_s);
            continue;
        }
        if (kl == u"country_event" || kl == u"province_event") {
            _add(out, vpos, vl, u"event"_s, v, u"fire"_s);
            continue;
        }
        if ((kl == u"title" || kl == u"desc") &&
            (ctx.size() == 1 || parent == OptStr(u"option"_s) || (ctx.size() == 2 && ctx[0] == u"political_decisions"))) {
            if (loc_key(v))                       // (plain text written in place is no key)
                _add(out, vpos, vl, u"loc"_s, v, u"use"_s);
            continue;
        }
        if (kl == u"name") {
            if (parent == OptStr(u"add_country_modifier"_s) || parent == OptStr(u"add_province_modifier"_s))
                _add(out, vpos, vl, u"modifier"_s, v, u"add"_s);
            else if (parent == OptStr(u"option"_s) && loc_key(v))
                _add(out, vpos, vl, u"loc"_s, v, u"use"_s);
            continue;
        }
        if (const QString *mr = MOD_KEYS.get(kl)) {
            _add(out, vpos, vl, u"modifier"_s, v, *mr);
            continue;
        }
        if (kl == u"type" && parent && CB_GIVE.contains(*parent)) {   // casus_belli = { target = … type = <war goal> }
            _add(out, vpos, vl, u"cb"_s, v, CB_GIVE.value(*parent));
            continue;
        }
        if (kl == u"casus_belli" && parent && CB_GOALS.contains(*parent)) {
            _add(out, vpos, vl, u"cb"_s, v, u"use"_s);
            continue;
        }
        if (const auto *fk = FLAG_KEYS.get(kl)) {
            _add(out, vpos, vl, fk->first, v, fk->second);
            continue;
        }
        if (kl == u"which" && parent && VAR_PARENTS.contains(*parent)) {
            _add(out, vpos, vl, u"var"_s, v, VAR_PARENTS.value(*parent));
            continue;
        }
        if (NOT_NAMES.contains(v))
            continue;
        if (CULTURE_KEYS.contains(kl)) {
            _add(out, vpos, vl, mod.culture_groups.contains(v) ? u"cgroup"_s : u"culture"_s, v,
                 (cond || kl.startsWith(u"is_")) ? role : u"change"_s);
            continue;
        }
        if (RELIGION_KEYS.contains(kl)) {
            _add(out, vpos, vl, mod.religion_groups.contains(v) ? u"rgroup"_s : u"religion"_s, v, cond ? role : u"change"_s);
            continue;
        }
        if (const QString *kind = SIMPLE.get(kl)) {
            if (*kind == u"state" && !mod.regions.contains(v))
                continue;
            if (*kind == u"poptype" && !mod.poptypes.contains(v))
                continue;
            _add(out, vpos, vl, *kind, v, cond ? role : u"change"_s);
            continue;
        }
        if (py::isdigit(v) && PROV_KEYS.contains(kl) && mod.provinces.contains(py::to_int(v))) {
            _add(out, vpos, vl, u"province"_s, v, cond ? role : u"change"_s);
            continue;
        }
        if (is_tag(v) && mod.countries.contains(v))
            _add(out, vpos, vl, u"nation"_s, v, cond ? role : u"change"_s);
    }
}

// ---------------------------------------------------------------------- looking up
const Ref *RefIndex::at(const QString &rel, qsizetype pos) const
{
    // The ref at a position of a file (the cursor just after a word counts too), or None.
    const auto *lst = spans.get(rel);
    if (!lst || lst->empty())
        return nullptr;
    const auto &st = *_starts.get(rel);
    const qsizetype i = qsizetype(std::upper_bound(st.begin(), st.end(), pos) - st.begin()) - 1;
    if (i >= 0) {
        const Ref &r = (*lst)[size_t(i)];
        if (r.pos <= pos && pos <= r.pos + r.length)
            return &r;
    }
    return nullptr;
}

std::vector<Ref> RefIndex::in_range(const QString &rel, qsizetype a, qsizetype b) const
{
    const auto *lst = spans.get(rel);
    if (!lst || lst->empty())
        return {};
    const auto &st = *_starts.get(rel);
    const auto i = std::lower_bound(st.begin(), st.end(), a) - st.begin();
    const auto j = std::lower_bound(st.begin(), st.end(), b) - st.begin();
    return j > i ? std::vector<Ref>(lst->begin() + i, lst->begin() + j) : std::vector<Ref>();
}

std::optional<std::pair<QString, qsizetype>> RefIndex::definition(const QString &kind, const QString &target) const
{
    // (rel, pos) where the thing is defined (a flag / variable: where it is first set), or None.
    const auto *u = uses.get({kind, target});
    if (!u)
        return std::nullopt;
    for (const QStringList &want : {QStringList{u"def"_s}, QStringList{u"set"_s, u"add"_s, u"change"_s}})
        for (const Use &x : *u)
            if (want.contains(x.role))
                return std::make_pair(x.rel, x.pos);
    return std::nullopt;
}

std::vector<Use> RefIndex::usages(const QString &kind, const QString &target) const
{
    const auto *u = uses.get({kind, target});
    return u ? *u : std::vector<Use>();
}

// ---------------------------------------------------------------------- describing
QString RefIndex::name_of(const QString &kind, const QString &t) const
{
    if (kind == u"event") {
        Entry *const *e = db->by_id.get(t);
        return e ? u"%1 (#%2)"_s.arg(db->name(**e), t) : u"#"_s + t;
    }
    if (kind == u"decision" || kind == u"rebel") {
        const OptStr l = loc_text(mod, t + u"_title"_s);
        return (l && !l->isEmpty()) ? *l : QString(t).replace(u'_', u' ');
    }
    if (kind == u"nation") {
        const moddata::Country *c = mod.countries.get(t);
        return u"%1 (%2)"_s.arg(c ? c->name : t, t);
    }
    if (kind == u"province") {
        const moddata::Province *pr = py::isdigit(t) ? mod.provinces.get(py::to_int(t)) : nullptr;
        return u"%1 (#%2)"_s.arg(pr ? pr->name : u"?"_s, t);
    }
    if (kind == u"state") {
        const moddata::Region *r = mod.regions.get(t);
        return u"%1 (%2)"_s.arg((r && !r->name.isEmpty()) ? r->name : t, t);
    }
    if (kind == u"culture") {
        const moddata::Culture *c = mod.cultures.get(t);
        return c ? c->name : t;
    }
    if (kind == u"religion") {
        const moddata::Religion *c = mod.religions.get(t);
        return c ? c->name : t;
    }
    if (kind == u"goods") {
        const moddata::Good *g = mod.goods.get(t);
        return g ? g->name : t;
    }
    if (kind == u"loc")
        return t;
    const OptStr l = loc_text(mod, t);
    return (l && !l->isEmpty()) ? *l : QString(t).replace(u'_', u' ');
}

OptStr RefIndex::cb_at(qsizetype pos) const
{
    // The war goal (its key) a position of common/cb_types.txt is in, or None.
    for (const auto &[a, b, key] : cb_spans)
        if (a <= pos && pos < b)
            return key;
    return std::nullopt;
}

QString RefIndex::place_name(const QString &rel, qsizetype pos) const
{
    const events::Entry *e = (rel.startsWith(u"events/") || rel.startsWith(u"decisions/")) ? db->entry_at(rel, pos) : nullptr;
    if (e)
        return db->name(*e) + (e->is_event() ? u" #"_s + (e->id ? *e->id : u"None"_s) : u" (decision)"_s);
    if (rel == cbcheck::REL)
        if (const OptStr key = cb_at(pos); key && !key->isEmpty())
            return name_of(u"cb"_s, *key) + u" (war goal)"_s;
    return u"%1 line %2"_s.arg(rel.split(u'/').last()).arg(line_of(text_of(rel), pos));
}

QStringList RefIndex::usage_lines(const QString &kind, const QString &t, qsizetype per_role, const QStringList &roles) const
{
    // ["<b>Sets it</b>: A #1, B #2 … (+3)", …] — where the thing is set / cleared / checked / fired …
    OrderedMap<QString, std::vector<std::pair<QString, qsizetype>>> by;
    if (const auto *u = uses.get({kind, t}))
        for (const Use &x : *u)
            by[x.role].emplace_back(x.rel, x.pos);
    QStringList out;
    const QStringList order = !roles.isEmpty() ? roles
                                               : QStringList{u"def"_s, u"set"_s, u"add"_s, u"fire"_s, u"change"_s,
                                                             u"clear"_s, u"remove"_s, u"check"_s, u"use"_s, u"scope"_s};
    for (const QString &role : order) {
        const auto *lst = by.get(role);
        if (!lst || lst->empty())
            continue;
        QStringList names;
        for (const auto &[rel, pos] : *lst) {
            const QString n = place_name(rel, pos);
            if (!names.contains(n))
                names << n;
        }
        out << u"<b>%1</b>: %2%3"_s.arg(py::capitalize(ROLES.value(role)), esc(names.mid(0, per_role).join(u", "_s)),
                                         names.size() > per_role ? u" … (+%1)"_s.arg(names.size() - per_role) : QString());
    }
    return out;
}

static QString drop_colour_codes(const QString &s)   // re.sub("§.", "", s)
{
    QString out;
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s[i] == u'§' && i + 1 < s.size() && s[i + 1] != u'\n') {
            ++i;
            continue;
        }
        out += s[i];
    }
    return out;
}

QString RefIndex::info_html(const QString &kind, const QString &t, const QStringList &extra, bool usages) const
{
    // A short description of the thing (for the hover tooltip).
    const auto k = KINDS.get(kind);
    const QString label = k ? k->first : kind;
    const auto [r, g, b] = k ? k->second : std::tuple<int, int, int>{200, 200, 200};
    const QString head = u"<span style=\"color:rgb(%1,%2,%3); font-weight:600\">%4</span> · <b>%5</b>"_s.arg(r).arg(g).arg(b)
                             .arg(esc(label), esc(name_of(kind, t)));
    QStringList lines;
    const QString faint = u"<i style=\"color:#a89c84\">%1</i>"_s;
    if (kind == u"event") {
        Entry *const *ep = db->by_id.get(t);
        if (!ep) {
            lines << u"<span style=\"color:#ff786e\">No event has this id.</span>"_s;
        } else {
            const Entry &e = **ep;
            const QString how = (e.triggered_only || !e.mtth || *e.mtth == 0.0) ? u"fired by something"_s
                                                                                : u"MTTH %1 days"_s.arg(py::fmt_f(*e.mtth, 0));
            lines << u"%1 · %2%3"_s.arg(esc(e.rel.value_or(u"None"_s)), how, e.once ? u" · fires once"_s : QString());
            const QString d = db->description(e);
            if (!d.isEmpty())
                lines << faint.arg(esc(QString(d).replace(u"\\n"_s, u" "_s).left(220)));
            QStringList names;
            if (const auto *src = db->fired_by.get(t))
                for (const events::Fired &f : *src) {
                    const QString n = f.src ? db->name(*f.src) + (f.src->is_event() ? u" #"_s + (f.src->id ? *f.src->id : u"None"_s)
                                                                                  : u" (decision)"_s)
                                            : u"the game (on_actions / elections)"_s;
                    if (!names.contains(n))
                        names << n;
                }
            if (!names.isEmpty())
                lines << u"<b>Fired by</b>: %1%2"_s.arg(esc(names.mid(0, 6).join(u", "_s)),
                                                         names.size() > 6 ? u" … (+%1)"_s.arg(names.size() - 6) : QString());
        }
    } else if (kind == u"decision") {
        if (const QString *d = mod.loc.get(t + u"_desc"_s); d && !d->isEmpty())
            lines << faint.arg(esc(QString(*d).replace(u"\\n"_s, u" "_s).left(220)));
    } else if (kind == u"modifier") {
        if (!modifiers.contains(t))
            lines << u"<span style=\"color:#ff786e\">No modifier of this name (common/event_modifiers.txt, static / "
                     "triggered modifiers).</span>"_s;
        const OptStr nm = loc_text(mod, t), ds = loc_text(mod, t + u"_desc"_s);
        if (nm && !nm->isEmpty()) {
            lines << u"Name in the game: “%1”"_s.arg(esc(*nm));
            if (ds && !ds->isEmpty())
                lines << faint.arg(esc(QString(*ds).replace(u"\\n"_s, u" "_s).left(220)));
        } else {
            lines << u"<span style=\"color:#e0a050\">No localisation present (no text for the key “%1” — the game shows "
                     "the key)</span>"_s.arg(esc(t));
        }
    } else if (kind == u"cb") {
        if (!cbs.contains(t)) {
            lines << u"<span style=\"color:#ff786e\">No war goal of this name (common/cb_types.txt).</span>"_s;
        } else {
            const OptStr ds = loc_text(mod, t + u"_desc"_s);
            if (ds && !ds->isEmpty())
                lines << faint.arg(esc(drop_colour_codes(*ds).replace(u"\\n"_s, u" "_s).left(220)));
        }
    } else if (kind == u"cflag" || kind == u"gflag" || kind == u"pflag" || kind == u"var") {
        bool set = false;
        if (const auto *u = uses.get({kind, t}))
            for (const Use &x : *u)
                set = set || x.role == u"set" || x.role == u"change";
        if (!set) {
            const QSet<QString> *hist = kind == u"cflag" ? &db->set_flags : kind == u"gflag" ? &db->set_gflags
                                                         : kind == u"var" ? &db->set_vars : nullptr;
            lines << ((hist && hist->contains(t)) ? u"set by the start history"_s
                                                  : u"<span style=\"color:#ff786e\">Nothing in the mod sets it — a check for "
                                                    "it is never true.</span>"_s);
        }
    } else if (kind == u"loc") {
        const OptStr txt = loc_text(mod, t);
        lines << ((txt && !txt->isEmpty()) ? u"“%1”"_s.arg(esc(txt->left(300)))
                                           : u"<span style=\"color:#ff786e\">No localisation text for this key.</span>"_s);
    }
    lines += extra;
    if (kind != u"event" && kind != u"loc" && usages)
        lines += usage_lines(kind, t);
    QString out = head;
    for (const QString &x : lines)
        out += u"<br>"_s + x;
    return out;
}

}  // namespace refs
