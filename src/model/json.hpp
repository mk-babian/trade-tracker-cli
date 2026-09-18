#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// A minimal, dependency-free JSON Document Object Model (DOM) and
// serializer/parser used for persistence. Numbers are kept as either a signed
// 64-bit integer or a double so large integer fields (ids, microsecond
// timestamps) survive a round-trip without floating-point precision loss.
namespace tt::json {

class Value;

using Array = std::vector<Value>;
using Object = std::map<std::string, Value, std::less<>>;

class Value {
public:
    enum class Type { Null, Bool, Int, Double, String, Array, Object };

    Value() : data_(std::monostate{}) {}
    Value(std::nullptr_t) : data_(std::monostate{}) {}
    Value(bool b) : data_(b) {}
    Value(int v) : data_(static_cast<std::int64_t>(v)) {}
    Value(std::int64_t v) : data_(v) {}
    Value(double d) : data_(d) {}
    Value(const char* s) : data_(std::string(s)) {}
    Value(std::string s) : data_(std::move(s)) {}
    Value(Array a) : data_(std::move(a)) {}
    Value(Object o) : data_(std::move(o)) {}

    Type type() const noexcept { return static_cast<Type>(data_.index()); }

    bool is_null() const noexcept;
    bool is_bool() const noexcept;
    bool is_int() const noexcept;
    bool is_double() const noexcept;
    bool is_number() const noexcept { return is_int() || is_double(); }
    bool is_string() const noexcept;
    bool is_array() const noexcept;
    bool is_object() const noexcept;

    bool as_bool() const;
    std::int64_t as_int() const;
    double as_double() const;
    const std::string& as_string() const;
    const Array& as_array() const;
    const Object& as_object() const;

    // Object access (throws std::out_of_range when missing).
    bool contains(std::string_view key) const noexcept;
    const Value& at(std::string_view key) const;

    // Array access.
    const Value& at(std::size_t index) const;

private:
    std::variant<std::monostate, bool, std::int64_t, double, std::string, Array,
                 Object>
        data_;
};

// Serialize a Value into compact JSON text.
std::string dump(const Value& v);

// Parse JSON text into a Value. On failure, sets `error` and returns a null
// Value (does not throw).
Value parse(std::string_view text, std::string& error);

// Serialize a Value into compact JSON text.
std::string dump(const Value& v);

// Serialize a Value into indented, multi-line JSON text.
std::string dump_pretty(const Value& v, int indent_width = 2);

}  // namespace tt::json

