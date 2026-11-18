// staterefs.py — the places a script names a state, and their fix. Line by line.
#include "core/staterefs.h"
#include "core/blockcode.h"
#include "core/pyre.h"
#include "core/pytext.h"
#include "core/scriptstore.h"

using namespace Qt::StringLiterals;

namespace staterefs {

const QStringList FOLDERS = {u"events"_s, u"decisions"_s, u"common"_s};

static QString key_pattern(const QString &key)
{
    return u"(?<![A-Za-z0-9_])%1(?![A-Za-z0-9_])"_s.arg(QRegularExpression::escape(key));
}
static const QString SCOPE_RE = py::PY_S + u"*="_s + py::PY_S + u"*\\{"_s;   // (re_match: anchored at its offset)
static const QString REGION_RE = u"(?<![A-Za-z0-9_])region"_s + py::PY_S + u"*="_s + py::PY_S + u"*$"_s;

std::pair<long long, QString> _line_of(const QString &text, qsizetype pos)
{
    const qsizetype a = py::rfind(text, u'\n', pos) + 1;
    const qsizetype b = text.indexOf(u'\n', pos);
    return {static_cast<long long>(py::head(text, pos).count(u'\n')) + 1, py::slice(text, a, b >= 0 ? b : text.size())};
}

std::optional<qsizetype> _block_end(const QString &text, qsizetype brace)
{
    // Position just after the } that closes the { at `brace` (None when it is never closed).
    long long depth = 0;
    qsizetype i = brace;
    const qsizetype n = text.size();
    while (i < n) {
        const QChar c = text[i];
        if (c == u'#') {
            const qsizetype j = text.indexOf(u'\n', i);
            i = j < 0 ? n : j;
            continue;
        }
        if (c == u'"') {
            const qsizetype j = text.indexOf(u'"', i + 1);
            i = j < 0 ? n : j + 1;
            continue;
        }
        if (c == u'{') {
            ++depth;
        } else if (c == u'}') {
            --depth;
            if (depth == 0)
                return i + 1;
        }
        ++i;
    }
    return std::nullopt;
}

std::vector<Place> find(const moddata::Mod &mod, scriptstore::ScriptStore &store, const QString &key)
{
    // Every place a script names the state `key` (comments left out).
    std::vector<Place> out;
    const QRegularExpression &pat = py::rx(key_pattern(key));
    for (const QString &folder : FOLDERS) {
        for (const QString &rel : blockcode::files_of(mod, &store, folder)) {
            if (!py::lower(rel).endsWith(u".txt"))
                continue;
            QString text;
            try {
                text = store.text(rel);
            } catch (...) {
                continue;
            }
            if (!text.contains(key))
                continue;
            auto it = pat.globalMatch(text);
            while (it.hasNext()) {
                const auto m = it.next();
                const auto [ln, line] = _line_of(text, m.capturedStart(0));
                const qsizetype a = py::rfind(text, u'\n', m.capturedStart(0)) + 1;
                if (py::slice(text, a, m.capturedStart(0)).contains(u'#'))
                    continue;                                // in a comment
                const QString after = py::slice(text, m.capturedEnd(0), m.capturedEnd(0) + 40);
                const QString before = py::slice(text, std::max<qsizetype>(0, m.capturedStart(0) - 40), m.capturedStart(0));
                QString kind;
                if (py::re_match(SCOPE_RE, after).hasMatch())
                    kind = u"scope"_s;
                else if (py::re_search(REGION_RE, before).hasMatch())
                    kind = u"region"_s;
                else
                    kind = u"other"_s;
                out.push_back({rel, m.capturedStart(0), ln, py::strip(line), kind});
            }
        }
    }
    return out;
}

std::pair<QString, long long> fixed_text(QString text, const QString &key, const std::vector<long long> &provinces)
{
    // The text with every scope / `region =` use of `key` written as its provinces instead. -> (text, how many)
    if (provinces.empty())
        return {text, 0};
    const QRegularExpression &pat = py::rx(key_pattern(key));
    std::vector<std::pair<qsizetype, qsizetype>> hits;
    auto it = pat.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        hits.emplace_back(m.capturedStart(0), m.capturedEnd(0));
    }
    long long done = 0;
    for (auto h = hits.rbegin(); h != hits.rend(); ++h) {    // from the end: positions before stay right
        const auto [ms, me] = *h;
        const qsizetype a = py::rfind(text, u'\n', ms) + 1;
        if (py::slice(text, a, ms).contains(u'#'))
            continue;
        const auto sc = py::re_match(SCOPE_RE, text, me);
        if (sc.hasMatch()) {
            const qsizetype brace = sc.capturedEnd(0) - 1;
            const auto end = _block_end(text, brace);
            if (!end)
                continue;
            const QString body = py::slice(text, brace, *end);   // "{ … }"
            const QString indent = py::re_match(u"[ \\t]*"_s, text, a).captured(0);
            QStringList blocks;
            for (long long p : provinces)
                blocks << QString::number(p) + u" = "_s + body;
            text = py::head(text, ms) + blocks.join(u"\n"_s + indent) + py::tail(text, *end);
            ++done;
            continue;
        }
        const qsizetype from = std::max<qsizetype>(0, ms - 40);
        const auto rg = py::re_search(REGION_RE, py::slice(text, from, ms));
        if (rg.hasMatch()) {
            const qsizetype start = from + rg.capturedStart(0);
            QString nw;
            if (provinces.size() == 1) {
                nw = u"province_id = %1"_s.arg(provinces[0]);
            } else {
                QStringList p;
                for (long long x : provinces)
                    p << u"province_id = %1"_s.arg(x);
                nw = u"OR = { %1 }"_s.arg(p.join(u' '));
            }
            text = py::head(text, start) + nw + py::tail(text, me);
            ++done;
        }
    }
    return {text, done};
}

}  // namespace staterefs
