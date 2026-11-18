// issuepanel.py, the data half.
#include "core/issuepanel_data.h"
#include "core/pytext.h"

#include <QStringList>
#include <algorithm>

namespace issuepanel {

QString reindent(const QString &body, long long base, const QString &unit)
{
    QStringList out;
    long long depth = 0;
    for (const QString &ln : py::splitlines(body)) {
        const QString s = py::strip(ln);
        if (s.isEmpty()) {
            out << QString();
            continue;
        }
        const long long lead = depth - (s.startsWith(u'}') ? 1 : 0);
        out << unit.repeated(int(std::max(0LL, base + lead))) + s;
        const qsizetype hash = s.indexOf(u'#');
        const QString code = hash >= 0 ? s.left(hash) : s;
        depth += code.count(u'{') - code.count(u'}');
    }
    return out.join(u'\n');
}

}  // namespace issuepanel
