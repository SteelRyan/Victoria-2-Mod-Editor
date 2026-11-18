// issuepanel.py, its data half (PLAN.md §4.1): re-indenting a block's inside. The reforms / issues views are kind D.
#pragma once
#include <QString>

namespace issuepanel {

// The lines of a block's inside indented by how deep in braces they are (base = the block's own depth).
QString reindent(const QString &body, long long base, const QString &unit = QStringLiteral("\t"));

}  // namespace issuepanel
