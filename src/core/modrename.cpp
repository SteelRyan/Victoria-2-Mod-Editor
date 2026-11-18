// modrename — see modrename.h.
#include "core/modrename.h"

#include "core/moddata.h"
#include "core/pypath.h"
#include "core/script.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace modrename {

// Windows-1252's letters above 127 that are not latin-1 (the game's files are Windows-1252)
static const QString CP1252_EXTRA = u"€‚ƒ„…†‡ˆ‰Š‹ŒŽ‘’“”•–—˜™š›œžŸ"_s;

QString check_name(const QString &name)
{
    const QString n = name.trimmed();
    if (n.isEmpty())
        return u"A mod needs a name."_s;
    if (n.size() > 100)
        return u"At most 100 letters."_s;
    for (const QChar c : n) {
        if (c == u'"' || c == u'{' || c == u'}' || c == u'#' || c.unicode() < 32)
            return u"Not allowed in a mod's name: %1"_s.arg(c.unicode() < 32 ? u"a line break / tab"_s : QString(c));
        if (c.unicode() > 255 && !CP1252_EXTRA.contains(c))
            return u"The game can't show “%1” (only Western European letters)."_s.arg(c);
    }
    return QString();
}

QString with_name(const QString &text, const QString &name)
{
    static const QRegularExpression RE(u"^([ \\t]*name[ \\t]*=[ \\t]*)(\"[^\"\\r\\n]*\"|[^\\s#]+)"_s,
                                       QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = RE.match(text);
    const QString q = u"\""_s + name + u"\""_s;
    if (m.hasMatch())
        return text.left(m.capturedEnd(1)) + q + text.mid(m.capturedEnd(2));
    const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    return u"name = "_s + q + nl + text;
}

QString with_dependency(const QString &text, const QString &old_name, const QString &new_name, bool &changed)
{
    changed = false;
    static const QRegularExpression BLOCK(u"(dependencies\\s*=\\s*\\{)([^}]*)\\}"_s);
    static const QRegularExpression ITEM(u"\"([^\"]*)\"|([^\\s\"{}]+)"_s);
    const QString want = old_name.trimmed().toLower();
    QString out;
    qsizetype at = 0;
    auto bit = BLOCK.globalMatch(text);
    while (bit.hasNext()) {
        const auto b = bit.next();
        const QString body = b.captured(2);
        QString nb;
        qsizetype bat = 0;
        auto it = ITEM.globalMatch(body);
        while (it.hasNext()) {
            const auto i = it.next();
            const QString v = i.hasCaptured(1) ? i.captured(1) : i.captured(2);
            if (v.trimmed().toLower() != want)
                continue;
            nb += body.mid(bat, i.capturedStart(0) - bat) + u"\""_s + new_name + u"\""_s;
            bat = i.capturedEnd(0);
            changed = true;
        }
        nb += body.mid(bat);
        out += text.mid(at, b.capturedStart(2) - at) + nb;
        at = b.capturedEnd(2);
    }
    out += text.mid(at);
    return out;
}

Result rename(const QString &folder, const QString &old_name, const QString &new_name0)
{
    Result r;
    const QString new_name = new_name0.trimmed();
    if (const QString why = check_name(new_name); !why.isEmpty()) {
        r.error = why;
        return r;
    }
    const QString me = pypath::normcase(pypath::abspath(folder));
    const QString mods_dir = pypath::dirname(pypath::abspath(folder));
    const auto [mods, _base] = moddata::discover_mods(mods_dir);
    const moddata::ModEntry *own = nullptr;
    for (const auto &e : mods)
        if (e.folder && e.modfile && pypath::normcase(pypath::abspath(*e.folder)) == me) {
            own = &e;
            break;
        }
    if (!own) {
        r.error = u"No .mod file for this folder in %1 — the game's launcher has no name for it to change."_s.arg(mods_dir);
        return r;
    }
    try {
        script::write_text(*own->modfile, with_name(script::read_text(*own->modfile), new_name));
    } catch (const std::exception &ex) {
        r.error = u"Could not write %1: %2"_s.arg(pypath::basename(*own->modfile), QString::fromUtf8(ex.what()));
        return r;
    }
    r.modfile = *own->modfile;
    r.ok = true;
    for (const auto &e : mods) {
        if (!e.modfile || &e == own)
            continue;
        bool named = false;
        for (const QString &d : e.deps)
            named = named || d.trimmed().toLower() == old_name.trimmed().toLower();
        if (!named)
            continue;
        try {
            bool changed = false;
            const QString t = with_dependency(script::read_text(*e.modfile), old_name, new_name, changed);
            if (changed) {
                script::write_text(*e.modfile, t);
                r.dependents << pypath::basename(*e.modfile);
            }
        } catch (const std::exception &ex) {   // (the mod itself is renamed; this submod still names the old name)
            r.error += u" Could not change %1: %2."_s.arg(pypath::basename(*e.modfile), QString::fromUtf8(ex.what()));
        }
    }
    return r;
}

}  // namespace modrename
