// entryview.py, the data half.
#include "core/entryview_data.h"
#include "core/fs.h"
#include "core/pypath.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace entryview {

const QRegularExpression LINE_RE(uR"(^(\s*)([A-Za-z_][A-Za-z0-9_]*)(\s*=\s*)([^,\n{]+?)(\s*,?\s*)(--\s*(.*))?$)"_s,
                                 QRegularExpression::UseUnicodePropertiesOption);
const QRegularExpression SECT_RE(uR"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*\{)"_s, QRegularExpression::UseUnicodePropertiesOption);

bool inherited(const moddata::Mod &mod, const QString &rel)
{
    const QStringList parts = rel.split(u'/');
    return mod.base && !mod.base->isEmpty() && fs::isfile(pypath::join(*mod.base, parts)) && !mod.replaced(parts);
}

bool in_mod(const moddata::Mod &mod, const scriptstore::ScriptStore &store, const QString &rel)
{
    return fs::isfile(pypath::join(mod.root, rel.split(u'/'))) ||
           (store.docs.contains(rel) && !store.gone.contains(rel) && !inherited(mod, rel));
}

void drop_file(const moddata::Mod &, scriptstore::ScriptStore &store, const QString &rel) { store.remove(rel); }

QStringList war_sides(const QString &text)
{
    // (Python's \b and \s of a str pattern are Unicode's)
    static const QRegularExpression re(uR"(\b(?:add_attacker|add_defender|rem_attacker|rem_defender)\s*=\s*([A-Za-z0-9]{3})\b)"_s,
                                       QRegularExpression::UseUnicodePropertiesOption);
    QStringList out;
    for (auto it = re.globalMatch(text); it.hasNext();)
        out << it.next().captured(1);
    return out;
}

}  // namespace entryview
