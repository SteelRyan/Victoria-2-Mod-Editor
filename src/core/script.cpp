// script.py — Minimal Paradox script parser that keeps source offsets, so files can be edited surgically.
#include "core/script.h"
#include "core/pytext.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <stdexcept>
#include <sys/stat.h>
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pytext.h"

#include <QFile>
#include <QSaveFile>

namespace script {

static inline bool is_space(QChar c)
{
    // " \t\r\n﻿" (the BOM can't occur in latin-1 text, kept as the Python has it)
    const char16_t u = c.unicode();
    return u == u' ' || u == u'\t' || u == u'\r' || u == u'\n' || u == 0xFEFF;
}

std::vector<Token> tokenize(const QString &text)
{
    qsizetype i = 0;
    const qsizetype n = text.size();
    const QChar *s = text.constData();
    std::vector<Token> toks;
    toks.reserve(static_cast<size_t>(n / 6));
    while (i < n) {
        const QChar c = s[i];
        if (is_space(c)) {
            i += 1;
        } else if (c == u'#') {
            const qsizetype j = text.indexOf(u'\n', i);
            i = j < 0 ? n : j + 1;
        } else if (c == u'{' || c == u'}' || c == u'=') {
            toks.push_back({c, QString(c), i, i + 1});
            i += 1;
        } else if (c == u'"') {
            qsizetype j = text.indexOf(u'"', i + 1);
            j = j < 0 ? n - 1 : j;
            toks.push_back({u's', text.mid(i + 1, j - (i + 1)), i, j + 1});
            i = j + 1;
        } else {
            qsizetype j = i;
            while (j < n) {
                const char16_t u = s[j].unicode();
                if (u == u' ' || u == u'\t' || u == u'\r' || u == u'\n' || u == u'{' || u == u'}' || u == u'=' ||
                    u == u'#' || u == u'"')
                    break;
                ++j;
            }
            toks.push_back({u's', text.mid(i, j - i), i, j});
            i = j;
        }
    }
    return toks;
}

namespace {
struct Parser {
    const std::vector<Token> &toks;
    qsizetype ntok;
    qsizetype textlen;
    qsizetype pos = 0;

    Nodes block(int depth)
    {
        Nodes items;
        while (pos < ntok) {
            const Token &t = toks[size_t(pos)];
            if (t.kind == u'}') {
                if (depth > 0)
                    return items;
                pos += 1;  // stray '}' at top level
                continue;
            }
            if (t.kind == u'=') {
                pos += 1;
                continue;
            }
            if (t.kind == u'{') {
                const qsizetype start = t.start;
                pos += 1;
                Nodes sub = block(depth + 1);
                const qsizetype end = pos < ntok ? toks[size_t(pos)].end : textlen;
                pos += 1;
                Node nd;
                nd.is_block = true;
                nd.kids = std::move(sub);
                nd.kstart = nd.vstart = start;
                nd.vend = end;
                items.push_back(std::move(nd));
                continue;
            }
            if (pos + 1 < ntok && toks[size_t(pos + 1)].kind == u'=') {
                const QString key = t.text;
                const qsizetype kstart = t.start;
                pos += 2;
                if (pos >= ntok)
                    break;
                const Token &v = toks[size_t(pos)];
                if (v.kind == u'{') {
                    const qsizetype vstart = v.start;
                    pos += 1;
                    Nodes sub = block(depth + 1);
                    const qsizetype vend = pos < ntok ? toks[size_t(pos)].end : textlen;
                    pos += 1;
                    Node nd;
                    nd.has_key = true;
                    nd.key = key;
                    nd.is_block = true;
                    nd.kids = std::move(sub);
                    nd.kstart = kstart;
                    nd.vstart = vstart;
                    nd.vend = vend;
                    items.push_back(std::move(nd));
                } else if (v.kind == u's') {
                    pos += 1;
                    Node nd;
                    nd.has_key = true;
                    nd.key = key;
                    nd.value = v.text;
                    nd.kstart = kstart;
                    nd.vstart = v.start;
                    nd.vend = v.end;
                    items.push_back(std::move(nd));
                } else {
                    pos += 1;
                }
            } else {
                pos += 1;
                Node nd;
                nd.value = t.text;
                nd.kstart = nd.vstart = t.start;
                nd.vend = t.end;
                items.push_back(std::move(nd));
            }
        }
        return items;
    }
};
}  // namespace

Nodes parse(const QString &text)
{
    const std::vector<Token> toks = tokenize(text);
    Parser p{toks, qsizetype(toks.size()), text.size()};
    return p.block(0);
}

QString read_text(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        // what Python's open() raises, worded as Python words it (it reaches the editor's warnings)
        if (fs::isdir(path))
            throw fs::OSError("[Errno 13] Permission denied: " + py::repr_str(path).toStdString(), 13);
        if (!fs::exists(path))
            throw fs::OSError("[Errno 2] No such file or directory: " + py::repr_str(path).toStdString(), 2);
        throw fs::OSError("[Errno 13] Permission denied: " + py::repr_str(path).toStdString(), 13);
    }
    return QString::fromLatin1(f.readAll());
}

// Windows-1252's bytes 0x80..0x9F (0 = undefined there: Python's cp1252 codec has no character for them)
static const char16_t CP1252_HIGH[32] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};

QByteArray game_bytes(const QString &text)
{
    QByteArray out;
    out.reserve(text.size());
    const qsizetype n = text.size();
    for (qsizetype i = 0; i < n; ++i) {
        const char16_t u = text[i].unicode();
        if (u < 256) {
            out.append(char(u));
            continue;
        }
        // a surrogate pair is ONE character for Python: one '?'
        if (QChar::isHighSurrogate(u) && i + 1 < n && QChar::isLowSurrogate(text[i + 1].unicode())) {
            ++i;
            out.append('?');
            continue;
        }
        char b = '?';
        for (int k = 0; k < 32; ++k)
            if (CP1252_HIGH[k] == u) {
                b = char(0x80 + k);
                break;
            }
        out.append(b);
    }
    return out;
}

bool write_text(const QString &path, const QString &text)
{
    // As Python's open(path, "wb"): the C runtime's _wopen, and an OSError with Python's own message when it fails
    // ("[Errno 22] Invalid argument: '…'") — callers go on or stop exactly where Python's do (found 2026-10-20: a
    // nation whose name made an impossible file name).
    const QByteArray data = game_bytes(text);   // made before the file is opened
    const QString target = fs::watched(path);     // (the Apply journal: copy-on-write of a base game file …)
    auto fail = [&](int err) {
        char msg[256] = {};
        strerror_s(msg, sizeof msg, err);
        throw fs::OSError(QStringLiteral("[Errno %1] %2: %3")
                              .arg(err)
                              .arg(QString::fromLocal8Bit(msg), py::repr_str(target))
                              .toStdString(),
                          err);
    };
    int fd = -1;
    const errno_t oe = _wsopen_s(&fd, reinterpret_cast<const wchar_t *>(target.utf16()),
                                 _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY | _O_NOINHERIT, _SH_DENYNO, _S_IREAD | _S_IWRITE);
    if (oe || fd < 0)
        fail(oe ? oe : errno);
    qsizetype done = 0;
    while (done < data.size()) {
        const int n = _write(fd, data.constData() + done, static_cast<unsigned>(std::min<qsizetype>(data.size() - done, 1 << 30)));
        if (n < 0) {
            const int err = errno;
            _close(fd);
            fail(err);
        }
        done += n;
    }
    _close(fd);
    return true;
}

const Node *first(const Nodes &nodes, const QString &key)
{
    for (const Node &n : nodes)
        if (n.has_key && n.key == key)
            return &n;
    return nullptr;
}

std::vector<long long> bare_ints(const Nodes &nodes)
{
    std::vector<long long> out;
    for (const Node &c : nodes)
        if (!c.has_key && !c.is_block && py::isdigit(c.value))
            out.push_back(py::to_int(c.value));
    return out;
}

std::optional<std::tuple<int, int, int>> color_of(const Nodes &nodes)
{
    const Node *n = first(nodes, QStringLiteral("color"));
    if (!n || !n->is_block)
        return std::nullopt;
    std::vector<double> vals;
    for (const Node &c : n->kids)
        if (!c.has_key && !c.is_block)
            if (auto v = py::to_float(c.value))
                vals.push_back(*v);
    if (vals.size() < 3)
        return std::nullopt;
    vals.resize(3);
    bool small = true;
    for (double v : vals)
        small = small && v <= 1.0;
    if (small)  // 0..1 float colours (religions)
        for (double &v : vals)
            v = v * 255;
    auto ch = [](double v) { return int(std::max<long long>(0, std::min<long long>(255, py::round_int(v)))); };
    return std::make_tuple(ch(vals[0]), ch(vals[1]), ch(vals[2]));
}

std::optional<std::tuple<long long, long long, long long>> parse_date(const QString &s)
{
    // ^(-?\d+)\.(\d+)\.(\d+)$  — \d is any Unicode digit (Nd) for Python's re: in latin-1 only 0-9 are;
    // Python's $ also matches just before ONE final "\n"
    QString t = s;
    if (t.endsWith(u'\n'))
        t.chop(1);
    const QStringList parts = t.split(u'.');
    if (parts.size() != 3)
        return std::nullopt;
    auto digits = [](const QString &p, bool sign) {
        qsizetype i = (sign && p.startsWith(u'-')) ? 1 : 0;
        if (i >= p.size())
            return false;
        for (; i < p.size(); ++i)
            if (p[i] < u'0' || p[i] > u'9')
                return false;
        return true;
    };
    if (!digits(parts[0], true) || !digits(parts[1], false) || !digits(parts[2], false))
        return std::nullopt;
    return std::make_tuple(parts[0].toLongLong(), parts[1].toLongLong(), parts[2].toLongLong());
}

}  // namespace script
