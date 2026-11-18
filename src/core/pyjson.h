// Python's json module, as far as the editor uses it: a value that keeps its keys in order (a dict), json.load and
// json.dump(obj, indent=…, ensure_ascii=…) byte for byte (separators ", " / ": ", or "," at a line end when indented).
#pragma once
#include "core/ordered_map.h"

#include <QString>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace pyjson {

struct Value;
using Array = std::vector<Value>;
using Object = OrderedMap<QString, Value>;
struct Value {
    std::variant<std::monostate, bool, long long, double, QString, std::shared_ptr<Array>, std::shared_ptr<Object>> v;
    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : v(b) {}
    Value(int i) : v(static_cast<long long>(i)) {}
    Value(long long i) : v(i) {}
    Value(double d) : v(d) {}
    Value(const QString &s) : v(s) {}
    Value(const char *) = delete;
    Value(Array a) : v(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : v(std::make_shared<Object>(std::move(o))) {}
    bool is_null() const { return std::holds_alternative<std::monostate>(v); }
    bool is_obj() const { return std::holds_alternative<std::shared_ptr<Object>>(v); }
    bool is_arr() const { return std::holds_alternative<std::shared_ptr<Array>>(v); }
    Object &obj() { return *std::get<std::shared_ptr<Object>>(v); }
    const Object &obj() const { return *std::get<std::shared_ptr<Object>>(v); }
    Array &arr() { return *std::get<std::shared_ptr<Array>>(v); }
    const Array &arr() const { return *std::get<std::shared_ptr<Array>>(v); }
    QString str() const { return std::holds_alternative<QString>(v) ? std::get<QString>(v) : QString(); }
    bool truthy() const;                          // Python's bool(x)
    const Value *get(const QString &k) const;     // d.get(k) of an object (nullptr: missing / not an object)
};

QString dumps(const Value &v, std::optional<int> indent = std::nullopt, bool ensure_ascii = true);
std::optional<Value> loads(const QString &text);  // None: not valid JSON (Python: ValueError)

}  // namespace pyjson
