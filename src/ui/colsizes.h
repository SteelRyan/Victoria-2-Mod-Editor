// colsizes.py — Columns the user can resize (drag the lines between the column names): every column but the last keeps
// the width it is given, the last one fills what is left; together they never get wider than the list (no sideways
// scrolling), and the widths are remembered (settings "cols/<key>").
#pragma once
#include <QString>
#include <functional>
#include <optional>
#include <vector>

class QTreeWidget;

namespace colsizes {

inline constexpr int LAST_MIN = 120;              // the last column never narrower (its numbers stay readable)

// defaults: widths of all columns but the last; nullopt for the first = what's left over at first.
// stretch_first: the first column (the names) takes what is left over instead of the last; the others keep their
// (resizable, remembered) widths. -> the function that sets the widths again (none with stretch_first)
std::function<void()> make_resizable(QTreeWidget *tree, const QString &key, const std::vector<std::optional<int>> &defaults,
                                     bool stretch_first = false);

}  // namespace colsizes
