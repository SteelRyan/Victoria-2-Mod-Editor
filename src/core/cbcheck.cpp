// cbcheck.py — the war goal check, the names-that-don't-exist check, the bracket check. Line by line.
#include "core/cbcheck.h"
#include "core/ordered_map.h"
#include "core/pydifflib.h"
#include "core/pynum.h"
#include "core/pyre.h"
#include "core/pytext.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace cbcheck {

using py::PY_S;

const QString REL = u"common/cb_types.txt"_s;
const QStringList MOD_RELS = {u"common/event_modifiers.txt"_s, u"common/static_modifiers.txt"_s,
                              u"common/triggered_modifiers.txt"_s};
const QString MAKE_REL = MOD_RELS[0];        // a missing modifier is made here
// every one of the game's own 31 war goals has these lines
const QStringList NEEDED = {u"sprite_index"_s, u"is_triggered_only"_s, u"months"_s, u"badboy_factor"_s,
                            u"prestige_factor"_s, u"peace_cost_factor"_s, u"penalty_factor"_s,
                            u"break_truce_prestige_factor"_s, u"break_truce_infamy_factor"_s,
                            u"break_truce_militancy_factor"_s, u"truce_months"_s, u"war_name"_s};
const QStringList PO = {u"po_annex"_s, u"po_demand_state"_s, u"po_add_to_sphere"_s, u"po_disarmament"_s,
                        u"po_reparations"_s, u"po_transfer_provinces"_s, u"po_remove_prestige"_s, u"po_make_puppet"_s,
                        u"po_release_puppet"_s, u"po_status_quo"_s, u"po_install_communist_gov_type"_s,
                        u"po_uninstall_communist_gov_type"_s, u"po_remove_cores"_s, u"po_colony"_s,
                        u"po_destroy_forts"_s, u"po_destroy_naval_bases"_s, u"po_gunboat"_s, u"po_clear_union_sphere"_s};
const QStringList YESNO = PO + QStringList{u"is_triggered_only"_s, u"crisis"_s, u"constructing_cb"_s, u"always"_s,
                                           u"all_allowed_states"_s, u"is_civil_war"_s, u"great_war_obligatory"_s,
                                           u"mutual"_s};
const QStringList NUMBER = {u"badboy_factor"_s, u"prestige_factor"_s, u"peace_cost_factor"_s, u"penalty_factor"_s,
                            u"tws_battle_factor"_s, u"break_truce_prestige_factor"_s, u"break_truce_infamy_factor"_s,
                            u"break_truce_militancy_factor"_s, u"good_relation_prestige_factor"_s,
                            u"good_relation_infamy_factor"_s, u"good_relation_militancy_factor"_s, u"construction_speed"_s};
const QStringList WHOLE = {u"sprite_index"_s, u"months"_s, u"truce_months"_s};
const QStringList BLOCKS = {u"can_use"_s, u"is_valid"_s, u"allowed_states"_s, u"allowed_states_in_crisis"_s,
                            u"allowed_substate_regions"_s, u"allowed_countries"_s, u"on_add"_s, u"on_po_accepted"_s,
                            u"prerequisites"_s};
const QSet<QString> KNOWN = [] {
    QSet<QString> s;
    for (const QStringList *l : {&YESNO, &NUMBER, &WHOLE, &BLOCKS})
        for (const QString &k : *l)
            s.insert(k);
    s.insert(u"war_name"_s);
    return s;
}();
const QStringList STATE_LINES = {u"allowed_states"_s, u"all_allowed_states"_s, u"allowed_substate_regions"_s};
// texts the game itself doesn't have (a mod that lacks them too is no worse than the game)
const QSet<QString> GAME_GAPS = {u"agression_war_boxer_name"_s, u"agression_war_gunboat_name"_s,
                                 u"agression_war_humiliate_name"_s, u"agression_war_puppet_name"_s, u"status_quo"_s};
const QSet<QString> GAME_ORDER_ONLY = {u"annex"_s};   // in the game's own peace_order without a war goal of that name
const QStringList SCAN_FOLDERS = {u"common"_s, u"events"_s, u"decisions"_s, u"inventions"_s, u"poptypes"_s, u"history/wars"_s};
const QStringList BRACKET_FOLDERS = {u"common"_s, u"events"_s, u"decisions"_s, u"inventions"_s, u"poptypes"_s,
                                     u"technologies"_s, u"units"_s, u"history/wars"_s};

static const QString NAME = u"\"?([^\\s\\x1c-\\x1f\"{}=#]+)\"?"_s;
static const QString HAS_RE = u"\\b(has_country_modifier|has_province_modifier|remove_country_modifier|"
                              "remove_province_modifier)"_s + PY_S + u"*="_s + PY_S + u"*"_s + NAME;
static const QString ADD_RE = u"\\b(add_country_modifier|add_province_modifier)"_s + PY_S + u"*="_s + PY_S +
                              u"*\\{([^{}]*)\\}"_s;
static const QString INNER_NAME_RE = u"\\bname"_s + PY_S + u"*="_s + PY_S + u"*"_s + NAME;
static const QString CB_RE = u"\\b(casus_belli|add_casus_belli|remove_casus_belli|war_goal|attacker_goal|defender_goal)"_s +
                             PY_S + u"*="_s + PY_S + u"*\\{([^{}]*)\\}"_s;
static const QString INNER_TYPE_RE = u"\\btype"_s + PY_S + u"*="_s + PY_S + u"*"_s + NAME;
static const QString INNER_CB_RE = u"\\bcasus_belli"_s + PY_S + u"*="_s + PY_S + u"*"_s + NAME;

static bool has(const Node &n) { return n.has_key && !n.key.isEmpty(); }
static bool is(const Node &n, const QString &k) { return n.has_key && n.key == k; }
static QString key_or_empty(const Node *n) { return (n && n->has_key) ? n->key : QString(); }   // (n.key or "")

long long line_of(const QString &text, qsizetype pos) { return py::head(text, pos).count(u'\n') + 1; }

QString blank_comments(const QString &text)
{
    // The text with its comments blanked (same length, so positions stay).
    if (!text.contains(u'#'))
        return text;
    return py::re_sub(u"#[^\\n]*"_s, text, [](const QRegularExpressionMatch &m) { return QString(m.capturedLength(0), u' '); });
}

static QString blank_quotes(const QString &code)
{
    return py::re_sub(u"\"[^\"\\n]*\""_s, code, [](const QRegularExpressionMatch &m) { return QString(m.capturedLength(0), u' '); });
}

long long _indent(const QString &text, qsizetype pos)
{
    const qsizetype a = py::rfind(text, u'\n', pos) + 1;
    const qsizetype b = text.indexOf(u'\n', a);
    const QString line = py::slice(text, a, b >= 0 ? b : text.size());
    long long col = 0;                            // line[:indent].expandtabs(4)
    for (QChar c : line) {
        if (c == u'\t')
            col += 4 - col % 4;
        else if (c == u' ')
            col += 1;
        else
            break;
    }
    return col;
}

bool _looks_like_goal(const Node &n)
{
    if (!(n.is_block && has(n) && !KNOWN.contains(n.key)))
        return false;
    long long k = 0;
    for (const Node &c : n.kids)
        k += c.has_key && NEEDED.contains(c.key);
    return k >= 2;
}

std::vector<const Node *> _swallowed(const Node &node, std::vector<const Node *> &out)
{
    // The war goals that stand inside `node` (a bracket before them was never closed).
    for (const Node &c : node.kids)
        if (c.is_block) {
            if (_looks_like_goal(c))
                out.push_back(&c);
            _swallowed(c, out);
        }
    return out;
}

const Node *_unclosed(const QString &text, const Nodes &nodes, std::optional<qsizetype> stop)
{
    // The block among `nodes` (or inside them) whose closing bracket doesn't stand where its opening line does — the
    // innermost, first in the file: most likely the one that was never closed (a guess from the indentation).
    for (const Node &c : nodes) {
        if (!c.is_block || (stop && c.kstart >= *stop))
            continue;
        if (const Node *got = _unclosed(text, c.kids, stop))
            return got;
        if (py::slice(text, c.kstart, c.vend).contains(u'\n')) {
            const bool closed = c.vend <= text.size() && py::slice(text, c.vend - 1, c.vend) == u"}";
            // (a column or so off is just untidy; a whole step further out is somebody else's bracket)
            if (!closed || _indent(text, c.kstart) - _indent(text, c.vend - 1) >= 2)
                return &c;
        }
    }
    return nullptr;
}

std::pair<long long, std::vector<qsizetype>> bracket_depths(const QString &text)
{
    // (depth at the end of the text, [positions of a } that closes nothing]) — comments and quoted texts don't count.
    const QString code = blank_quotes(blank_comments(text));
    long long depth = 0;
    std::vector<qsizetype> stray;
    for (qsizetype i = 0; i < code.size(); ++i) {
        if (code[i] == u'{')
            depth += 1;
        else if (code[i] == u'}') {
            if (depth == 0)
                stray.push_back(i);
            else
                depth -= 1;
        }
    }
    return {depth, stray};
}

std::optional<F> check_brackets(const QString &rel, const QString &text)
{
    // One script file: None when its brackets balance, else F (data: ("close", rel) when all it lacks is the } of its
    // last block at the very end, ("ref", rel, pos) otherwise).
    const auto [depth, stray] = bracket_depths(text);
    if (!depth && stray.empty())
        return std::nullopt;
    const QString code = blank_quotes(blank_comments(text));
    const QString head = u"%1: %2 { but %3 }"_s.arg(rel).arg(code.count(u'{')).arg(code.count(u'}'));
    if (depth) {
        // the block still open at the end of the file is the last one at the top: the bracket that is missing is its
        // own, or that of a block inside it
        const Nodes tops_all = script::parse(text);
        const Node *last = nullptr;
        for (const Node &n : tops_all)
            if (n.is_block)
                last = &n;
        const Node *bad = last ? _unclosed(text, last->kids) : nullptr;
        if (!bad && last && depth == 1 && stray.empty())
            return F{u"%1 — the last block, “%2 = {” (line %3), has no } at the end of the file."_s.arg(
                         head, key_or_empty(last)).arg(line_of(text, last->kstart)),
                     rel, last->kstart, std::nullopt, Data{u"close"_s, rel}};
        if (bad)
            return F{u"%1 — a { is never closed. By the indentation it is “%2 = {” in line %3: what follows it in the "
                     "file is read as part of it, so the blocks after it don't exist for the game."_s
                         .arg(head, key_or_empty(bad)).arg(line_of(text, bad->kstart)),
                     rel, bad->kstart, std::nullopt, Data{u"ref"_s, rel, static_cast<long long>(bad->kstart)}};
        const qsizetype pos = last ? last->kstart : 0;
        return F{u"%1 — a { is never closed (the file ends inside “%2 = {”, which starts in line %3)."_s.arg(
                     head, key_or_empty(last)).arg(line_of(text, pos)),
                 rel, pos, std::nullopt, Data{u"ref"_s, rel, static_cast<long long>(pos)}};
    }
    QString more;
    if (stray.size() > 1) {
        QStringList ls;
        for (size_t i = 1; i < stray.size() && i < 4; ++i)
            ls << QString::number(line_of(text, stray[i]));
        more = u" (and %1 more: line%2 %3)"_s.arg(stray.size() - 1).arg(stray.size() == 2 ? QString() : u"s"_s, ls.join(u", "_s));
    }
    return F{u"%1 — line %2 has a } that closes nothing%3. The lines between the block before it and that } stand "
             "outside every block (left over from something that was cut out?)."_s.arg(head).arg(line_of(text, stray[0])).arg(more),
             rel, stray[0], std::nullopt, Data{u"ref"_s, rel, static_cast<long long>(stray[0])}};
}

std::vector<std::pair<qsizetype, qsizetype>> doubled_equals(const QString &text)
{
    // [(start, end)] of every run of two or more = (spaces between them allowed) — comments and quoted texts don't
    // count.
    std::vector<std::pair<qsizetype, qsizetype>> out;
    if (!text.contains(u'='))
        return out;
    static const QRegularExpression re(u"=("_s + PY_S + u"*=)+"_s, QRegularExpression::UseUnicodePropertiesOption);
    const QString code = blank_quotes(blank_comments(text));
    for (auto it = re.globalMatch(code); it.hasNext();) {
        const auto m = it.next();
        out.emplace_back(m.capturedStart(), m.capturedEnd());
    }
    return out;
}

std::optional<F> check_equals(const QString &rel, const QString &text)
{
    // One script file: None, or F about its doubled = (data: ("equals", rel, position of the first)). Measured in the
    // game (2026-10-21, a tester's Modus): one `poor_strata_life_needs = = 0.95` in a pop type file and the game
    // crashes while it loads ("Loading Databases…").
    const auto spans = doubled_equals(text);
    if (spans.empty())
        return std::nullopt;
    std::vector<long long> lines;
    for (const auto &[a, b] : spans)
        lines.push_back(line_of(text, a));
    const qsizetype from = py::rfind(text, u'\n', spans[0].first) + 1;
    const qsizetype to = text.indexOf(u'\n', from);
    const QString first = py::strip(py::slice(text, from, to < 0 ? text.size() : to));
    QString more;
    if (lines.size() > 1) {
        QStringList ls;
        for (size_t i = 1; i < lines.size() && i < 6; ++i)
            ls << QString::number(lines[i]);
        more = u" (and %1 more: line%2 %3%4)"_s.arg(lines.size() - 1)
                   .arg(lines.size() == 2 ? QString() : u"s"_s, ls.join(u", "_s), lines.size() > 6 ? u" …"_s : QString());
    }
    return F{u"%1: line %2 has two = in a row — “%3”%4."_s.arg(rel)
                 .arg(lines[0])
                 .arg(first.size() <= 80 ? first : first.left(77) + u"…"_s, more),
             rel, spans[0].first, std::nullopt, Data{u"equals"_s, rel, static_cast<long long>(spans[0].first)}};
}

QString fix_equals(QString text)
{
    // The text with every doubled = made one (the first stays, what stood between them goes).
    const auto spans = doubled_equals(text);
    for (auto it = spans.rbegin(); it != spans.rend(); ++it)
        text = text.left(it->first) + u'=' + text.mid(it->second);
    return text;
}

QString close_brackets(const QString &text)
{
    // The text with the brackets it leaves open at its end closed.
    const long long depth = bracket_depths(text).first;
    if (depth <= 0)
        return text;
    QString out = (text.endsWith(u'\n') || text.isEmpty()) ? text : text + u'\n';
    for (long long i = 0; i < depth; ++i)
        out += u"}\n"_s;
    return out;
}

static QString lstrip_dash(QString s)                // str(v).lstrip("-")
{
    while (s.startsWith(u'-'))
        s.remove(0, 1);
    return s;
}

static bool _yes(const Node &n) { return !n.is_block && py::lower(py::strip(n.value)) == u"yes"; }

Structure read_structure(const QString &text, const Nodes &nodes, Report &rep)
{
    // The file's blocks: (peace_order node or None, [war goal nodes the game reads], {names read as part of another},
    // {names whose own lines can't be trusted}).
    Structure s;
    const Node *stray = nullptr, *prev = nullptr;
    for (const Node &n : nodes) {
        if (is(n, u"peace_order"_s) && n.is_block) {
            s.order = &n;
            prev = &n;
            continue;
        }
        if (has(n) && n.is_block && !KNOWN.contains(n.key)) {
            s.goals.push_back(&n);
            prev = &n;
            stray = nullptr;
            continue;
        }
        if (!stray) {                             // a line outside every war goal: a } too many before it
            stray = &n;
            const QString what = has(n) ? n.key + u" = …"_s : (n.is_block ? u"{ … }"_s : n.value);
            const QString before = prev ? u"war goal %1, which starts in line %2"_s.arg(key_or_empty(prev)).arg(line_of(text, prev->kstart))
                                        : u"the start of the file"_s;
            rep.broken.push_back(F{u"Line %1: “%2” stands outside every war goal — there is one } too many before it "
                                   "(in %3). Everything of that war goal after the surplus } is lost."_s
                                       .arg(line_of(text, n.kstart)).arg(what, before),
                                   REL, n.kstart, prev ? OptStr(key_or_empty(prev)) : std::nullopt, std::nullopt});
            if (prev)
                s.broken.insert(key_or_empty(prev));
        }
    }
    for (const Node *g : s.goals) {
        std::vector<const Node *> got;
        _swallowed(*g, got);
        if (got.empty())
            continue;
        s.broken.insert(g->key);
        for (const Node *c : got)
            s.inside.insert(c->key);
        const Node *bad = _unclosed(text, g->kids, got[0]->kstart);
        const QString where = bad ? u" — by the indentation it is “%1 = {” in line %2"_s.arg(
                                        has(*bad) ? bad->key : u"{"_s).arg(line_of(text, bad->kstart))
                                  : QString();
        QStringList names;
        for (const Node *c : got)
            names << c->key;
        const bool one = names.size() == 1;
        rep.broken.push_back(F{u"%1 (line %2): a { is never closed%3. The %4 war goal%5 after it (%6) %7 read as part of %8 — "
                               "for the game %9."_s.arg(g->key).arg(line_of(text, g->kstart)).arg(where).arg(names.size())
                                   .arg(one ? QString() : u"s"_s, one ? names[0] : u"%1 … %2"_s.arg(names.first(), names.last()),
                                        one ? u"is"_s : u"are"_s, g->key,
                                        one ? u"it doesn't exist"_s : u"they don't exist"_s),
                               REL, (bad ? bad : g)->kstart, g->key, std::nullopt});
    }
    const QString code = blank_comments(text);
    const qsizetype opens = code.count(u'{'), closes = code.count(u'}');
    if (opens != closes && rep.broken.empty() && !s.goals.empty()) {
        const Node *last = s.goals.back();
        const Node *bad = opens > closes ? _unclosed(text, last->kids) : nullptr;
        const bool ends = opens == closes + 1 && !bad;   // all it lacks is its own } at the very end
        QString tail;
        if (bad)
            tail = u" — by the indentation it is “%1 = {” in line %2 (war goal %3)"_s.arg(has(*bad) ? bad->key : u"{"_s)
                       .arg(line_of(text, bad->kstart)).arg(last->key);
        else if (opens > closes)
            tail = u" — the last war goal, %1 (line %2), has no } at the end of the file"_s.arg(last->key).arg(line_of(text, last->kstart));
        rep.broken.push_back(F{u"The file has %1 { but %2 }: %3%4."_s.arg(opens).arg(closes).arg(
                                   opens > closes ? u"a bracket is never closed"_s : u"one } too many"_s, tail),
                               REL, (bad ? bad : last)->kstart, last->key,
                               ends ? std::optional<Data>(Data{u"close"_s, REL}) : std::nullopt});
        if (!ends)
            s.broken.insert(last->key);
    }
    QHash<QString, const Node *> seen;
    for (const Node *g : s.goals) {
        if (const Node *first = seen.value(g->key, nullptr))
            rep.broken.push_back(F{u"%1 is defined twice (lines %2 and %3): the game keeps only one of them."_s.arg(g->key)
                                       .arg(line_of(text, first->kstart)).arg(line_of(text, g->kstart)),
                                   REL, g->kstart, g->key, std::nullopt});
        else
            seen.insert(g->key, g);
    }
    return s;
}

void check_goal(const QString &text, const Node &g, Report &rep, long long frames)
{
    // One war goal's own lines.
    std::vector<const Node *> kids;
    for (const Node &c : g.kids)
        if (has(c))
            kids.push_back(&c);
    OrderedMap<QString, long long> keys;          // Counter, in the order met
    QHash<QString, const Node *> node;
    for (const Node *c : kids) {
        keys[c->key] += 1;
        if (!node.contains(c->key))
            node.insert(c->key, c);
    }
    const QString name = g.key;
    auto add = [&](const QString &msg, const Node *c = nullptr) {
        rep.lines.push_back(F{name + u": "_s + msg, REL, (c ? c : &g)->kstart, name, std::nullopt});
    };
    QStringList miss;
    for (const QString &k : NEEDED)
        if (!keys.contains(k))
            miss << k;
    if (!miss.isEmpty())
        add(u"no %1 — every war goal of the game has %2."_s.arg(miss.join(u", "_s), miss.size() == 1 ? u"it"_s : u"them"_s));
    QStringList known(KNOWN.begin(), KNOWN.end());
    std::sort(known.begin(), known.end());
    for (const auto &[k, n] : keys) {
        if (!KNOWN.contains(k)) {
            const QStringList near = py::get_close_matches(k, known, 1, 0.75);
            add(u"line “%1” (line %2) — a war goal has no such line; the game skips it%3."_s.arg(k)
                    .arg(line_of(text, node.value(k)->kstart))
                    .arg(near.isEmpty() ? QString() : u" (meant: %1?)"_s.arg(near[0])),
                node.value(k));
        } else if (n > 1) {
            add(u"“%1” stands %2 times (first in line %3) — only one of them counts."_s.arg(k).arg(n)
                    .arg(line_of(text, node.value(k)->kstart)),
                node.value(k));
        }
    }
    for (const Node *cp : kids) {
        const Node &c = *cp;
        const QString &k = c.key;
        if (BLOCKS.contains(k) && !c.is_block)
            add(u"“%1 = %2” (line %3) must be a block: %1 = { … }."_s.arg(k, c.value).arg(line_of(text, c.kstart)), &c);
        else if (KNOWN.contains(k) && !BLOCKS.contains(k) && c.is_block)
            add(u"“%1 = { … }” (line %2) must be a single value."_s.arg(k).arg(line_of(text, c.kstart)), &c);
        else if (YESNO.contains(k) && py::lower(c.value) != u"yes" && py::lower(c.value) != u"no")
            add(u"“%1 = %2” (line %3) — yes or no."_s.arg(k, c.value).arg(line_of(text, c.kstart)), &c);
        else if (NUMBER.contains(k) && !py::re_match(u"^-?(\\d+\\.?\\d*|\\.\\d+)$"_s, c.value).hasMatch())
            add(u"“%1 = %2” (line %3) — a number."_s.arg(k, c.value).arg(line_of(text, c.kstart)), &c);
        else if (WHOLE.contains(k) && !py::isdigit(lstrip_dash(c.value)))
            add(u"“%1 = %2” (line %3) — a whole number."_s.arg(k, c.value).arg(line_of(text, c.kstart)), &c);
    }
    if (const Node *c = node.value(u"sprite_index"_s, nullptr);
        c && !c->is_block && py::isdigit(c->value) && frames) {
        const long long v = py::to_int(c->value);
        if (!(1 <= v && v <= frames))
            add(u"sprite_index = %1, but the strip of war goal pictures has %2 (1 … %2): a wrong or empty picture in the "
                "game. Library → Icon strips makes the strip longer."_s.arg(c->value).arg(frames),
                c);
    }
    auto on = [&](const QString &k) { return node.contains(k) && _yes(*node.value(k)); };
    bool states = false;
    for (const QString &k : STATE_LINES)
        states = states || keys.contains(k);
    for (const auto &[k, what] : std::vector<std::pair<QString, QString>>{
             {u"po_demand_state"_s, u"demands a state"_s}, {u"po_transfer_provinces"_s, u"hands provinces over"_s},
             {u"po_destroy_forts"_s, u"tears down forts"_s}, {u"po_destroy_naval_bases"_s, u"tears down naval bases"_s},
             {u"po_remove_cores"_s, u"removes cores"_s}})
        if (on(k) && !states)
            add(u"%1 (%2 = yes) but has no allowed_states — no state can be picked for it."_s.arg(what, k), node.value(k));
    if (on(u"po_release_puppet"_s) && !keys.contains(u"allowed_countries"_s))
        add(u"releases a puppet (po_release_puppet = yes) but has no allowed_countries — no nation can be picked for it."_s,
            node.value(u"po_release_puppet"_s));
    if (on(u"po_remove_cores"_s) && !(on(u"po_transfer_provinces"_s) || on(u"po_demand_state"_s) || on(u"po_annex"_s)))
        add(u"po_remove_cores = yes works only together with po_transfer_provinces, po_demand_state or po_annex (the "
            "game's own note in cb_types.txt)."_s,
            node.value(u"po_remove_cores"_s));
    bool any_po = false;
    for (const QString &k : PO)
        any_po = any_po || on(k);
    if (!any_po && !on(u"great_war_obligatory"_s) && !keys.contains(u"on_po_accepted"_s))
        add(u"demands nothing: no po_… line is set to yes and there is no on_po_accepted — winning with it changes nothing."_s);
    if (keys.contains(u"allowed_states_in_crisis"_s) && node.contains(u"crisis"_s) && !node.value(u"crisis"_s)->is_block &&
        py::lower(node.value(u"crisis"_s)->value) == u"no")
        add(u"has allowed_states_in_crisis, but crisis = no keeps it out of every crisis."_s,
            node.value(u"allowed_states_in_crisis"_s));
}

void check_order(const QString &text, const Node *order, const std::vector<const Node *> &goals, const QSet<QString> &inside,
                 Report &rep)
{
    QStringList names;
    for (const Node *g : goals)
        names << g->key;
    if (!order) {
        if (!names.isEmpty())
            rep.order.push_back(F{u"There is no peace_order block: the order in which the war goals of a peace are carried "
                                  "out isn't set."_s,
                                  REL, std::nullopt, std::nullopt, Data{u"order"_s, u"none"_s, std::monostate{}}});
        return;
    }
    std::vector<std::pair<QString, qsizetype>> listed;
    for (const Node &c : order->kids)
        if (!c.has_key && !c.is_block)
            listed.emplace_back(c.value, c.kstart);
    QSet<QString> have;
    for (const auto &kv : listed)
        have.insert(kv.first);
    for (const QString &n : names)
        if (!have.contains(n))
            rep.order.push_back(F{u"%1 is not in peace_order — every war goal of the game is listed there (the order they are "
                                  "carried out in when a peace is signed)."_s.arg(n),
                                  REL, order->kstart, n, Data{u"order"_s, u"add"_s, n}});
    QSet<QString> known = inside + GAME_ORDER_ONLY;
    for (const QString &n : names)
        known.insert(n);
    QSet<QString> seen;
    for (const auto &[v, pos] : listed) {
        if (seen.contains(v)) {
            rep.order.push_back(F{u"peace_order lists %1 twice (line %2)."_s.arg(v).arg(line_of(text, pos)), REL, pos,
                                  std::nullopt, Data{u"order"_s, u"dup"_s, v}});
        } else if (!known.contains(v)) {
            const QStringList near = py::get_close_matches(v, names, 1, 0.8);
            rep.order.push_back(F{u"peace_order lists %1 (line %2), but there is no war goal of that name%3."_s.arg(v)
                                      .arg(line_of(text, pos))
                                      .arg(near.isEmpty() ? QString() : u" — meant: %1?"_s.arg(near[0])),
                                  REL, pos, std::nullopt, Data{u"order"_s, u"drop"_s, v}});
        }
        seen.insert(v);
    }
}

QString fix_order(const QString &text, const std::vector<std::pair<QString, OptStr>> &todo)
{
    // peace_order after the fixes [(kind, name)]: names added at its end, entries without a war goal and doubled ones
    // taken out. -> the file's new text (the same text when nothing can be done).
    const Nodes nodes = script::parse(text);
    const Node *order = nullptr;
    for (const Node &n : nodes)
        if (is(n, u"peace_order"_s) && n.is_block) {
            order = &n;
            break;
        }
    QStringList add;
    QSet<QString> drop, dup;
    bool none = false;
    for (const auto &[k, n] : todo) {
        if (k == u"add" && n)
            add << *n;
        else if (k == u"drop" && n)
            drop.insert(*n);
        else if (k == u"dup" && n)
            dup.insert(*n);
        none = none || k == u"none";
    }
    if (!order) {
        QStringList goals;
        for (const Node &n : nodes)
            if (has(n) && n.is_block && !KNOWN.contains(n.key))
                goals << n.key;
        if (!none || goals.isEmpty())
            return text;
        QString block = u"peace_order = {\n"_s;
        for (const QString &g : goals)
            block += u'\t' + g + u'\n';
        block += u"}\n\n"_s;
        std::optional<qsizetype> first;
        for (const Node &n : nodes)
            if (has(n) && n.is_block)
                first = std::min(first.value_or(n.kstart), n.kstart);
        const qsizetype f = first.value_or(0);
        return py::head(text, f) + block + py::tail(text, f);
    }
    std::vector<std::pair<qsizetype, qsizetype>> cuts;
    QSet<QString> seen;
    for (const Node &c : order->kids)
        if (!c.has_key && !c.is_block) {
            if (drop.contains(c.value) || (dup.contains(c.value) && seen.contains(c.value)))
                cuts.emplace_back(c.kstart, c.vend);
            seen.insert(c.value);
        }
    const qsizetype close = order->vend - 1;
    const QSet<QString> have = seen - drop;
    QStringList fresh;
    for (const QString &n : add)
        if (!fresh.contains(n) && !have.contains(n))
            fresh << n;
    QString out = text;
    if (!fresh.isEmpty() && py::slice(out, close, close + 1) == u"}") {
        const qsizetype a = py::rfind(out, u'\n', close) + 1;
        if (py::strip(py::slice(out, a, close)).isEmpty()) {   // the } stands on a line of its own: a line each, set in
            const Node *last = nullptr;                          // like the others
            for (const Node &c : order->kids)
                if (!c.has_key && !c.is_block)
                    last = &c;
            const qsizetype la = last ? py::rfind(out, u'\n', last->kstart) + 1 : a;
            const QString lead = (last && py::strip(py::slice(out, la, last->kstart)).isEmpty())
                                     ? py::slice(out, la, last->kstart)
                                     : QString();
            QString ins;
            for (const QString &n : fresh)
                ins += lead + n + u'\n';
            out = py::head(out, a) + ins + py::tail(out, a);
        } else {                                                 // all on one line: on that line
            QString h = py::head(out, close);
            while (!h.isEmpty() && (h.back() == u' ' || h.back() == u'\t'))
                h.chop(1);
            out = h + u' ' + fresh.join(u' ') + u' ' + py::tail(out, close);
        }
    }
    std::sort(cuts.begin(), cuts.end(), std::greater<>());
    for (const auto &[a, b] : cuts) {
        const qsizetype la = py::rfind(out, u'\n', a) + 1;
        qsizetype lb = out.indexOf(u'\n', b);
        lb = lb < 0 ? out.size() : lb;
        if (py::strip(py::slice(out, la, a)).isEmpty() && py::strip(py::slice(out, b, lb).split(u'#').first()).isEmpty())
            out = py::head(out, la) + py::tail(out, lb + 1);   // the entry had its line to itself: the line goes
        else
            out = py::head(out, a) + py::tail(out, b);
    }
    return out;
}

std::vector<std::pair<QString, QString>> text_keys(const QString &name, const OptStr &war_name)
{
    // [(localisation key, what it is)] a war goal needs.
    std::vector<std::pair<QString, QString>> out = {{name, u"its name"_s},
                                                    {name + u"_desc"_s, u"its description"_s},
                                                    {name + u"_setup"_s, u"its line in the war goal window"_s},
                                                    {name + u"_short"_s, u"its short name"_s}};
    if (war_name && !war_name->isEmpty()) {
        out.emplace_back(u"NORMAL_"_s + *war_name, u"the war's name"_s);
        out.emplace_back(u"AGRESSION_"_s + *war_name, u"the war's name when the attacker has no war goal of its own right"_s);
    }
    return out;
}

void check_texts(const QString &, const std::vector<const Node *> &goals, const QSet<QString> &skip, Report &rep,
                 const std::function<bool(const QString &)> &has_text, const std::function<OptStr(const QString &)> &game_text)
{
    QSet<QString> done;
    for (const Node *g : goals) {
        if (skip.contains(g->key))
            continue;
        OptStr wn;
        for (const Node &c : g->kids)
            if (is(c, u"war_name"_s) && !c.is_block) {
                wn = c.value;
                break;
            }
        const QString nice = py::title(py::strip(QString(g->key).replace(u'_', u' ')));
        for (const auto &[key, what] : text_keys(g->key, wn)) {
            const QString low = py::lower(key);
            if (done.contains(low) || GAME_GAPS.contains(low) || has_text(key))
                continue;
            done.insert(low);
            QString sug;
            auto from_game = [&](const QString &k, const QString &dflt) {
                const OptStr t = game_text ? game_text(k) : std::nullopt;
                return (t && !t->isEmpty()) ? *t : dflt;
            };
            if (key.startsWith(u"NORMAL_"))
                sug = from_game(u"NORMAL_WAR_NAME"_s, u"$ORDER$$FIRST$-$SECOND$ War"_s);
            else if (key.startsWith(u"AGRESSION_"))
                sug = from_game(u"AGRESSION_WAR_NAME"_s, u"$ORDER$War of $FIRST$ Aggression"_s);
            else
                sug = nice;
            rep.texts.push_back(F{u"%1: no text for %2 (%3) — the game shows the bare key."_s.arg(g->key, key, what), REL,
                                  g->kstart, g->key, Data{u"name"_s, key, sug, u"other"_s}});
        }
    }
}

// ---------------------------------------------------------------------- names that don't exist
QSet<QString> modifier_names(const std::function<QString(const QString &)> &text_of)
{
    QSet<QString> out;
    for (const QString &rel : MOD_RELS) {
        try {
            for (const Node &n : script::parse(text_of(rel)))
                if (has(n) && n.is_block)
                    out.insert(n.key);
        } catch (...) {
        }
    }
    return out;
}

std::vector<Ref> scan_refs(const QString &text)
{
    // [(kind "mod" / "cb", the line's key, the name, position of the name)] a text names.
    if (!text.contains(u"modifier"_s) && !text.contains(u"casus_belli"_s))
        return {};
    const QString code = blank_comments(text);
    std::vector<Ref> out;
    for (auto it = py::rx(HAS_RE).globalMatch(code); it.hasNext();) {
        const auto m = it.next();
        out.push_back({u"mod"_s, m.captured(1), m.captured(2), m.capturedStart(2)});
    }
    for (auto it = py::rx(ADD_RE).globalMatch(code); it.hasNext();) {
        const auto m = it.next();
        const auto i = py::re_search(INNER_NAME_RE, m.captured(2));
        if (i.hasMatch())
            out.push_back({u"mod"_s, m.captured(1), i.captured(1), m.capturedStart(2) + i.capturedStart(1)});
    }
    for (auto it = py::rx(CB_RE).globalMatch(code); it.hasNext();) {
        const auto m = it.next();
        const auto i = py::re_search(m.captured(1).endsWith(u"goal") ? INNER_CB_RE : INNER_TYPE_RE, m.captured(2));
        if (i.hasMatch())
            out.push_back({u"cb"_s, m.captured(1), i.captured(1), m.capturedStart(2) + i.capturedStart(1)});
    }
    std::stable_sort(out.begin(), out.end(), [](const Ref &a, const Ref &b) { return a.pos < b.pos; });
    return out;
}

void check_refs(const QStringList &rels, const std::function<QString(const QString &)> &text_of, const QSet<QString> &mods,
                const QSet<QString> &goals, Report &rep, Cache *cache, const std::function<bool(const QString &)> &own,
                const std::function<QString(const QString &, qsizetype)> &where_in)
{
    // Every file of `rels`: the modifiers and war goals it names that don't exist. own(rel): the file is the mod's
    // (else the game's own, which the mod's changed lists break). where_in(rel, pos): a word for the place.
    QHash<QString, QString> low_mods, low_goals;   // (a set: which of two names that differ only in case — Python's order)
    for (const QString &m : mods)
        low_mods.insert(py::lower(m), m);
    for (const QString &g : goals)
        low_goals.insert(py::lower(g), g);
    QStringList smods(mods.begin(), mods.end()), sgoals(goals.begin(), goals.end());
    std::sort(smods.begin(), smods.end());
    std::sort(sgoals.begin(), sgoals.end());
    for (const QString &rel : rels) {
        QString text;
        try {
            text = text_of(rel);
        } catch (...) {
            continue;
        }
        std::vector<Ref> refs;
        if (cache && cache->refs.contains(rel) && cache->refs.value(rel).first == text) {
            refs = cache->refs.value(rel).second;
        } else {
            refs = scan_refs(text);
            if (cache)
                cache->refs.insert(rel, {text, refs});
        }
        for (const Ref &r : refs) {
            const bool mod = r.kind == u"mod";
            const QSet<QString> &names = mod ? mods : goals;
            const QHash<QString, QString> &low = mod ? low_mods : low_goals;
            std::vector<F> &lst = mod ? rep.no_mod : rep.no_cb;
            const QString noun = mod ? u"modifier"_s : u"war goal"_s;
            if (names.contains(r.name) || r.name == u"THIS" || r.name == u"FROM")
                continue;
            QString hint;
            if (low.contains(py::lower(r.name))) {
                hint = u" — it is written %1 (capital letters count)"_s.arg(low.value(py::lower(r.name)));
            } else {
                const QStringList near = py::get_close_matches(r.name, mod ? smods : sgoals, 1, 0.8);
                if (!near.isEmpty())
                    hint = u" — meant: %1?"_s.arg(near[0]);
            }
            const QString place = where_in ? where_in(rel, r.pos) : QString();
            const bool mine = own ? own(rel) : true;
            lst.push_back(F{u"%1 line %2%3: %4 … %5 — there is no %6 of that name%7%8."_s.arg(rel).arg(line_of(text, r.pos))
                                .arg(place, r.key, r.name, noun, hint,
                                     mine ? QString() : u" (the game's own file — the mod's list no longer has what it needs)"_s),
                            rel, r.pos, std::nullopt,
                            Data{u"ref"_s, rel, static_cast<long long>(r.pos), r.name, r.kind, r.key}});
        }
    }
}

QString make_modifiers(const QString &text, const QStringList &names)
{
    // common/event_modifiers.txt with empty modifiers of these names at its end (they do nothing until they are given
    // effects).
    QSet<QString> have;
    for (const Node &n : script::parse(text))
        if (has(n) && n.is_block)
            have.insert(n.key);
    QStringList fresh;
    for (const QString &n : names)
        if (!fresh.contains(n) && !have.contains(n) && py::re_match(u"^[A-Za-z0-9_.\\-]+$"_s, n).hasMatch())
            fresh << n;
    if (fresh.isEmpty())
        return text;
    QString out = close_brackets(text);           // (a file that ends inside its last block would swallow them)
    if (!(out.endsWith(u'\n') || out.isEmpty()))
        out += u'\n';
    out += u"\n# made by ModEditor: named by a script but missing - empty until given effects\n"_s;
    for (const QString &n : fresh)
        out += n + u" = {\n\ticon = 1\n}\n"_s;
    return out;
}

// ---------------------------------------------------------------------- everything
Report run(const std::function<QString(const QString &)> &text_of, const QStringList &rels,
           const std::function<bool(const QString &)> &has_text, long long frames,
           const std::function<OptStr(const QString &)> &game_text, Cache *cache, const std::function<bool(const QString &)> &own,
           const QStringList &bracket_rels)
{
    // The whole check. text_of(rel) -> a file's text; rels: the script files to look through for names; has_text(key):
    // the localisation has it; frames: pictures in the war goal strip; bracket_rels: the mod's own script files whose
    // brackets are counted.
    Report rep;
    rep.frames = frames;
    QString text;
    try {
        text = text_of(REL);
    } catch (...) {
        text = QString();
    }
    const Nodes nodes = script::parse(text);
    Structure s;
    if (!py::strip(text).isEmpty()) {
        s = read_structure(text, nodes, rep);
        for (const Node *g : s.goals)
            if (!s.broken.contains(g->key))
                check_goal(text, *g, rep, frames);
        check_order(text, s.order, s.goals, s.inside, rep);
        check_texts(text, s.goals, s.broken, rep, has_text, game_text);
    }
    for (const Node *g : s.goals)
        rep.goals << g->key;
    QSet<QString> names = s.inside;               // (a swallowed war goal is reported once, as the bracket)
    for (const QString &g : rep.goals)
        names.insert(g);
    auto where_in = [&](const QString &rel, qsizetype pos) -> QString {
        if (rel != REL)
            return {};
        for (const Node *g : s.goals) {
            if (g->kstart <= pos && pos < g->vend) {
                OptStr part;
                for (const Node &c : g->kids)
                    if (c.is_block && c.kstart <= pos && pos < c.vend) {
                        part = c.has_key ? OptStr(c.key) : std::nullopt;
                        break;
                    }
                return u" (war goal %1%2)"_s.arg(g->key, (part && !part->isEmpty()) ? u", "_s + *part : QString());
            }
        }
        return {};
    };
    const QSet<QString> mods = modifier_names(text_of);
    check_refs(rels, text_of, mods, names, rep, cache, own, where_in);
    if (names.isEmpty())                          // (no war goal file at all: nothing to hold the names against)
        rep.no_cb.clear();
    if (mods.isEmpty())
        rep.no_mod.clear();
    for (const QString &rel : bracket_rels) {
        QString t;
        try {
            t = text_of(rel);
        } catch (...) {
            continue;
        }
        std::optional<F> got, eq;
        if (cache && cache->brackets.contains(rel) && std::get<0>(cache->brackets.value(rel)) == t) {
            std::tie(std::ignore, got, eq) = cache->brackets.value(rel);
        } else {
            // (the war goal file has its own, closer look at its brackets)
            got = rel != REL ? check_brackets(rel, t) : std::nullopt;
            eq = check_equals(rel, t);
            if (cache)
                cache->brackets.insert(rel, {t, got, eq});
        }
        if (got)
            rep.brackets.push_back(*got);
        if (eq)
            rep.equals.push_back(*eq);
    }
    auto order = [](const F &a, const F &b) {
        return std::make_tuple(a.rel != REL, a.rel, a.pos.value_or(-1)) < std::make_tuple(b.rel != REL, b.rel, b.pos.value_or(-1));
    };
    std::stable_sort(rep.no_mod.begin(), rep.no_mod.end(), order);
    std::stable_sort(rep.no_cb.begin(), rep.no_cb.end(), order);
    return rep;
}

}  // namespace cbcheck
