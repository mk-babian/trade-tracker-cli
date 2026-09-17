#include "model/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace tt::json {

namespace {

std::string escape_string(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
    return out;
}

// Shortest round-trippable decimal for a double, or "null" if non-finite.
std::string format_double(double d) {
    if (!std::isfinite(d)) {
        return "null";
    }
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), d, std::chars_format::general);
    return std::string(buf, res.ptr);
}

}  // namespace

// ---- type predicates ----
bool Value::is_null() const noexcept { return std::holds_alternative<std::monostate>(data_); }
bool Value::is_bool() const noexcept { return std::holds_alternative<bool>(data_); }
bool Value::is_int() const noexcept { return std::holds_alternative<std::int64_t>(data_); }
bool Value::is_double() const noexcept { return std::holds_alternative<double>(data_); }
bool Value::is_string() const noexcept { return std::holds_alternative<std::string>(data_); }
bool Value::is_array() const noexcept { return std::holds_alternative<Array>(data_); }
bool Value::is_object() const noexcept { return std::holds_alternative<Object>(data_); }

bool Value::as_bool() const { return std::get<bool>(data_); }
std::int64_t Value::as_int() const {
    if (is_int()) {
        return std::get<std::int64_t>(data_);
    }
    if (is_double()) {
        return static_cast<std::int64_t>(std::get<double>(data_));
    }
    throw std::bad_variant_access();
}
double Value::as_double() const {
    if (is_double()) {
        return std::get<double>(data_);
    }
    if (is_int()) {
        return static_cast<double>(std::get<std::int64_t>(data_));
    }
    throw std::bad_variant_access();
}
const std::string& Value::as_string() const { return std::get<std::string>(data_); }
const Array& Value::as_array() const { return std::get<Array>(data_); }
const Object& Value::as_object() const { return std::get<Object>(data_); }

bool Value::contains(std::string_view key) const noexcept {
    if (!is_object()) {
        return false;
    }
    return std::get<Object>(data_).find(key) != std::get<Object>(data_).end();
}

const Value& Value::at(std::string_view key) const {
    const auto& o = std::get<Object>(data_);
    auto it = o.find(key);
    if (it == o.end()) {
        throw std::out_of_range("missing key: " + std::string(key));
    }
    return it->second;
}

const Value& Value::at(std::size_t index) const {
    const auto& a = std::get<Array>(data_);
    if (index >= a.size()) {
        throw std::out_of_range("array index out of range");
    }
    return a[index];
}

// ---- serialize ----
namespace {

void dump_into(const Value& v, std::string& out) {
    switch (v.type()) {
        case Value::Type::Null:
            out += "null";
            break;
        case Value::Type::Bool:
            out += v.as_bool() ? "true" : "false";
            break;
        case Value::Type::Int:
            out += std::to_string(v.as_int());
            break;
        case Value::Type::Double:
            out += format_double(v.as_double());
            break;
        case Value::Type::String:
            out += escape_string(v.as_string());
            break;
        case Value::Type::Array: {
            out.push_back('[');
            const auto& a = v.as_array();
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (i) {
                    out.push_back(',');
                }
                dump_into(a[i], out);
            }
            out.push_back(']');
            break;
        }
        case Value::Type::Object: {
            out.push_back('{');
            const auto& o = v.as_object();
            bool first = true;
            for (const auto& [key, val] : o) {
                if (!first) {
                    out.push_back(',');
                }
                first = false;
                out += escape_string(key);
                out.push_back(':');
                dump_into(val, out);
            }
            out.push_back('}');
            break;
        }
    }
}

}  // namespace

std::string dump(const Value& v) {
    std::string out;
    dump_into(v, out);
    return out;
}

// ---- parser ----
namespace {

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}

    Value parse() {
        skip_ws();
        Value v = parse_value();
        skip_ws();
        if (i_ != s_.size()) {
            fail("unexpected trailing characters");
        }
        return v;
    }

private:
    std::string_view s_;
    std::size_t i_ = 0;

    [[noreturn]] void fail(const std::string& msg) {
        throw std::runtime_error("JSON parse error at offset " + std::to_string(i_) +
                                 ": " + msg);
    }

    void skip_ws() {
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++i_;
            } else {
                break;
            }
        }
    }

    char peek() {
        if (i_ >= s_.size()) {
            fail("unexpected end of input");
        }
        return s_[i_];
    }

    void expect(char c) {
        if (i_ >= s_.size() || s_[i_] != c) {
            fail(std::string("expected '") + c + "'");
        }
        ++i_;
    }

    void consume_literal(std::string_view lit) {
        if (s_.substr(i_, lit.size()) != lit) {
            fail("invalid literal");
        }
        i_ += lit.size();
    }

    Value parse_value() {
        char c = peek();
        switch (c) {
            case 'n':
                consume_literal("null");
                return Value{};
            case 't':
                consume_literal("true");
                return Value{true};
            case 'f':
                consume_literal("false");
                return Value{false};
            case '"':
                return Value{parse_string()};
            case '[':
                return Value{parse_array()};
            case '{':
                return Value{parse_object()};
            default:
                if (c == '-' || (c >= '0' && c <= '9')) {
                    return parse_number();
                }
                fail("unexpected character");
        }
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (true) {
            if (i_ >= s_.size()) {
                fail("unterminated string");
            }
            char c = s_[i_++];
            if (c == '"') {
                break;
            }
            if (c == '\\') {
                if (i_ >= s_.size()) {
                    fail("unterminated escape");
                }
                char e = s_[i_++];
                switch (e) {
                    case '"':
                        out.push_back('"');
                        break;
                    case '\\':
                        out.push_back('\\');
                        break;
                    case '/':
                        out.push_back('/');
                        break;
                    case 'b':
                        out.push_back('\b');
                        break;
                    case 'f':
                        out.push_back('\f');
                        break;
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 'r':
                        out.push_back('\r');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    case 'u': {
                        if (i_ + 4 > s_.size()) {
                            fail("invalid unicode escape");
                        }
                        unsigned code = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = s_[i_++];
                            code <<= 4;
                            if (h >= '0' && h <= '9') {
                                code |= static_cast<unsigned>(h - '0');
                            } else if (h >= 'a' && h <= 'f') {
                                code |= static_cast<unsigned>(h - 'a' + 10);
                            } else if (h >= 'A' && h <= 'F') {
                                code |= static_cast<unsigned>(h - 'A' + 10);
                            } else {
                                fail("invalid unicode escape");
                            }
                        }
                        append_utf8(out, code);
                        break;
                    }
                    default:
                        fail("invalid escape character");
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    static void append_utf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }


    Array parse_array() {
        expect('[');
        Array a;
        skip_ws();
        if (i_ < s_.size() && s_[i_] == ']') {
            ++i_;
            return a;
        }
        while (true) {
            skip_ws();
            a.push_back(parse_value());
            skip_ws();
            char c = peek();
            if (c == ',') {
                ++i_;
                continue;
            }
            if (c == ']') {
                ++i_;
                break;
            }
            fail("expected ',' or ']'");
        }
        return a;
    }

    Object parse_object() {
        expect('{');
        Object o;
        skip_ws();
        if (i_ < s_.size() && s_[i_] == '}') {
            ++i_;
            return o;
        }
        while (true) {
            skip_ws();
            if (peek() != '"') {
                fail("expected string key");
            }
            std::string key = parse_string();
            skip_ws();
            expect(':');
            skip_ws();
            Value val = parse_value();
            o[std::move(key)] = std::move(val);
            skip_ws();
            char c = peek();
            if (c == ',') {
                ++i_;
                continue;
            }
            if (c == '}') {
                ++i_;
                break;
            }
            fail("expected ',' or '}'");
        }
        return o;
    }

    Value parse_number() {
        std::size_t start = i_;
        if (i_ < s_.size() && s_[i_] == '-') {
            ++i_;
        }
        while (i_ < s_.size()) {
            char c = s_[i_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
                c == '+' || c == '-') {
                ++i_;
            } else {
                break;
            }
        }
        std::string_view tok = s_.substr(start, i_ - start);
        const bool is_double = tok.find('.') != std::string_view::npos ||
                               tok.find('e') != std::string_view::npos ||
                               tok.find('E') != std::string_view::npos;
        if (is_double) {
            double d = 0.0;
            auto res = std::from_chars(tok.data(), tok.data() + tok.size(), d);
            if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size()) {
                fail("invalid number");
            }
            return Value{d};
        }
        std::int64_t n = 0;
        auto res = std::from_chars(tok.data(), tok.data() + tok.size(), n);
        if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size()) {
            // Too large for int64 — fall back to double.
            double d = 0.0;
            auto r2 = std::from_chars(tok.data(), tok.data() + tok.size(), d);
            if (r2.ec != std::errc{} || r2.ptr != tok.data() + tok.size()) {
                fail("invalid number");
            }
            return Value{d};
        }
        return Value{n};
    }
};

}  // namespace

Value parse(std::string_view text, std::string& error) {
    try {
        Parser p(text);
        return p.parse();
    } catch (const std::exception& e) {
        error = e.what();
        return Value{};
    }
}

}  // namespace tt::json

