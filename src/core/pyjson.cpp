// Python's json, as the editor uses it.
#include "core/pyjson.h"
#include "core/pynum.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace pyjson {

bool Value::truthy() const
{
    if (const auto *b = std::get_if<bool>(&v))
        return *b;
    if (const auto *i = std::get_if<long long>(&v))
        return *i != 0;
    if (const auto *d = std::get_if<double>(&v))
        return *d != 0;
    if (const auto *s = std::get_if<QString>(&v))
        return !s->isEmpty();
    if (is_arr())
        return !arr().empty();
    if (is_obj())
        return !obj().isEmpty();
    return false;
}

const Value *Value::get(const QString &k) const { return is_obj() ? obj().get(k) : nullptr; }

static QString enc(const QString &s, bool ensure_ascii)
{
    // json's string encoder: \" \\ \n \r \t \b \f, other control characters (and with ensure_ascii everything above
    // 0x7e) as \uXXXX (non-BMP characters as their surrogate pair)
    QString out = u"\""_s;
    for (const QChar c : s) {
        const char16_t u = c.unicode();
        switch (u) {
        case u'"': out += u"\\\""_s; break;
        case u'\\': out += u"\\\\"_s; break;
        case u'\n': out += u"\\n"_s; break;
        case u'\r': out += u"\\r"_s; break;
        case u'\t': out += u"\\t"_s; break;
        case u'\b': out += u"\\b"_s; break;
        case u'\f': out += u"\\f"_s; break;
        default:
            if (u < 0x20 || (ensure_ascii && u > 0x7e))
                out += u"\\u%1"_s.arg(int(u), 4, 16, QChar(u'0'));
            else
                out += c;
        }
    }
    return out + u'"';
}

static void dump(const Value &v, std::optional<int> indent, bool ea, int level, QString &out)
{
    if (v.is_null()) {
        out += u"null"_s;
    } else if (const auto *b = std::get_if<bool>(&v.v)) {
        out += *b ? u"true"_s : u"false"_s;
    } else if (const auto *i = std::get_if<long long>(&v.v)) {
        out += QString::number(*i);
    } else if (const auto *d = std::get_if<double>(&v.v)) {
        out += std::isnan(*d) ? u"NaN"_s : std::isinf(*d) ? (*d > 0 ? u"Infinity"_s : u"-Infinity"_s) : py::repr_float(*d);
    } else if (const auto *s = std::get_if<QString>(&v.v)) {
        out += enc(*s, ea);
    } else {
        const bool arr = v.is_arr();
        const qsizetype n = arr ? qsizetype(v.arr().size()) : v.obj().size();
        if (n == 0) {
            out += arr ? u"[]"_s : u"{}"_s;
            return;
        }
        out += arr ? u'[' : u'{';
        const QString nl = indent ? u"\n"_s + QString(u' ').repeated(*indent * (level + 1)) : QString();
        const QString sep = indent ? u","_s + nl : u", "_s;
        out += nl;
        qsizetype k = 0;
        auto item = [&](const Value &x) { dump(x, indent, ea, level + 1, out); };
        if (arr) {
            for (const Value &x : v.arr()) {
                if (k++)
                    out += sep;
                item(x);
            }
        } else {
            for (const auto &[key, x] : v.obj()) {
                if (k++)
                    out += sep;
                out += enc(key, ea) + u": "_s;
                item(x);
            }
        }
        if (indent)
            out += u"\n"_s + QString(u' ').repeated(*indent * level);
        out += arr ? u']' : u'}';
    }
}

QString dumps(const Value &v, std::optional<int> indent, bool ensure_ascii)
{
    QString out;
    dump(v, indent, ensure_ascii, 0, out);
    return out;
}

namespace {
struct Reader {
    const QString &t;
    qsizetype i = 0;
    void ws()
    {
        while (i < t.size() && (t[i] == u' ' || t[i] == u'\t' || t[i] == u'\n' || t[i] == u'\r'))
            ++i;
    }
    std::optional<QString> string()
    {
        if (i >= t.size() || t[i] != u'"')
            return std::nullopt;
        ++i;
        QString out;
        while (i < t.size()) {
            const QChar c = t[i++];
            if (c == u'"')
                return out;
            if (c.unicode() < 0x20)
                return std::nullopt;                 // (json's strict mode: no control characters)
            if (c != u'\\') {
                out += c;
                continue;
            }
            if (i >= t.size())
                return std::nullopt;
            const QChar e = t[i++];
            switch (e.unicode()) {
            case u'"': out += u'"'; break;
            case u'\\': out += u'\\'; break;
            case u'/': out += u'/'; break;
            case u'b': out += u'\b'; break;
            case u'f': out += u'\f'; break;
            case u'n': out += u'\n'; break;
            case u'r': out += u'\r'; break;
            case u't': out += u'\t'; break;
            case u'u': {
                if (i + 4 > t.size())
                    return std::nullopt;
                bool ok = false;
                const uint cp = t.mid(i, 4).toUInt(&ok, 16);
                if (!ok)
                    return std::nullopt;
                out += QChar(char16_t(cp));
                i += 4;
                break;
            }
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }
    std::optional<Value> value()
    {
        ws();
        if (i >= t.size())
            return std::nullopt;
        const QChar c = t[i];
        if (c == u'{') {
            ++i;
            Object o;
            ws();
            if (i < t.size() && t[i] == u'}') {
                ++i;
                return Value(o);
            }
            while (true) {
                ws();
                const auto k = string();
                if (!k)
                    return std::nullopt;
                ws();
                if (i >= t.size() || t[i] != u':')
                    return std::nullopt;
                ++i;
                const auto x = value();
                if (!x)
                    return std::nullopt;
                o.insert(*k, *x);
                ws();
                if (i < t.size() && t[i] == u',') {
                    ++i;
                    continue;
                }
                if (i < t.size() && t[i] == u'}') {
                    ++i;
                    return Value(o);
                }
                return std::nullopt;
            }
        }
        if (c == u'[') {
            ++i;
            Array a;
            ws();
            if (i < t.size() && t[i] == u']') {
                ++i;
                return Value(a);
            }
            while (true) {
                const auto x = value();
                if (!x)
                    return std::nullopt;
                a.push_back(*x);
                ws();
                if (i < t.size() && t[i] == u',') {
                    ++i;
                    continue;
                }
                if (i < t.size() && t[i] == u']') {
                    ++i;
                    return Value(a);
                }
                return std::nullopt;
            }
        }
        if (c == u'"') {
            const auto s = string();
            return s ? std::optional<Value>(Value(*s)) : std::nullopt;
        }
        for (const auto &[word, val] : {std::pair{u"true"_s, Value(true)}, {u"false"_s, Value(false)}, {u"null"_s, Value()}})
            if (t.mid(i, word.size()) == word) {
                i += word.size();
                return val;
            }
        const qsizetype a = i;
        while (i < t.size() && (t[i].isDigit() || t[i] == u'-' || t[i] == u'+' || t[i] == u'.' || t[i] == u'e' || t[i] == u'E'))
            ++i;
        const QString num = t.mid(a, i - a);
        if (num.isEmpty())
            return std::nullopt;
        if (!num.contains(u'.') && !num.contains(u'e') && !num.contains(u'E')) {
            bool ok = false;
            const long long n = num.toLongLong(&ok);
            if (ok)
                return Value(n);
        }
        const auto d = py::to_float(num);
        return d ? std::optional<Value>(Value(*d)) : std::nullopt;
    }
};
}  // namespace

std::optional<Value> loads(const QString &text)
{
    Reader r{text};
    auto v = r.value();
    r.ws();
    if (!v || r.i != text.size())
        return std::nullopt;
    return v;
}

}  // namespace pyjson
