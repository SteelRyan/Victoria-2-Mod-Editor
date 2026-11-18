// script.py — Minimal Paradox script parser that keeps source offsets, so files can be edited surgically.
#pragma once
#include <QString>
#include <QStringList>
#include <optional>
#include <tuple>
#include <vector>

namespace script {

inline const char *ENC = "latin-1";  // byte-transparent round trip; Vic2 files are Windows-1252

struct Node {
    bool has_key = false;      // key is None  <->  has_key == false
    QString key;               // the key (when has_key)
    bool is_block = false;     // value is list[Node]  <->  is_block
    QString value;             // str value (when !is_block)
    std::vector<Node> kids;    // list value (when is_block)
    qsizetype kstart = 0;      // offset of the key (or of the value when bare)
    qsizetype vstart = 0;      // offset of the value (the '{' for blocks)
    qsizetype vend = 0;        // offset just past the value (past the '}' for blocks)
};
using Nodes = std::vector<Node>;

struct Token {
    QChar kind;                // '{' '}' '=' or 's'
    QString text;
    qsizetype start, end;
};

std::vector<Token> tokenize(const QString &text);
Nodes parse(const QString &text);

// read_text: latin-1, newline="" — every byte becomes one character, offsets in the text = offsets in the file.
// Throws fs::OSError with Python's message ("[Errno 2] No such file or directory: '…'") where Python raises.
QString read_text(const QString &path);
// the bytes write_text would write: latin-1, characters above 255 through the "vic2" handler (their Windows-1252
// byte where there is one, else '?')
QByteArray game_bytes(const QString &text);
// write_text: turned into bytes BEFORE the file is opened — never an emptied file left behind
bool write_text(const QString &path, const QString &text);

const Node *first(const Nodes &nodes, const QString &key);
std::vector<long long> bare_ints(const Nodes &nodes);
std::optional<std::tuple<int, int, int>> color_of(const Nodes &nodes);
std::optional<std::tuple<long long, long long, long long>> parse_date(const QString &s);

}  // namespace script
