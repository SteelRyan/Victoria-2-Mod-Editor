// blocks.py — Small, careful edits of a script file's text by a path of keys: set / add / take off a value inside a
// block (anywhere in the file: ["infantry", "build_cost"], ["political_reforms", "slavery", "no_slavery"]), or replace
// a nested block's contents — everything else in the file (comments, layout) stays as it is.
#pragma once
#include "core/script.h"

#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

namespace blocks {

using script::Node;
using script::Nodes;
using OptStr = std::optional<QString>;

const Node *find(const Nodes &nodes, const QStringList &path);   // the block at a path of keys (the first of each)
std::vector<Node> children(const QString &text, const QStringList &path);       // (key, value, is_block) inside it
QString _indent_inside(const QString &text, const Node &node);
QString _insert_before_close(const QString &text, const Node &node, const QString &line);
QString set_value(const QString &text, const QStringList &path, const QString &key, const QString &value,
                  const OptStr &like = std::nullopt);
QString del_value(const QString &text, const QStringList &path, const QString &key);
OptStr block_text(const QString &text, const QStringList &path);
std::vector<Node> children_top(const QString &text, const QStringList &path);
QString add_block(const QString &text, const QStringList &path, const QString &key, const QStringList &lines);
QString set_block(const QString &text, const QStringList &path, const QString &inner);

}  // namespace blocks
