#include "Json.h"

#include <charconv>
#include <cstdint>

namespace arx::core {

const JsonValue* JsonValue::get(std::string_view key) const {
    if (type_ != Type::Object) return nullptr;
    for (size_t i = 0; i < keys_.size(); ++i)
        if (keys_[i] == key) return &arr_[i];
    return nullptr;
}

class JsonParser {
public:
    explicit JsonParser(std::string_view t) : t_(t) {}

    bool document(JsonValue& out, std::string& err) {
        if (!value(out, 0)) { err = err_ + " at byte " + std::to_string(p_); return false; }
        ws();
        if (p_ != t_.size()) { err = "trailing data at byte " + std::to_string(p_); return false; }
        return true;
    }

private:
    static constexpr int kMaxDepth = 64;
    std::string_view t_;
    size_t p_ = 0;
    std::string err_;

    bool fail(const char* m) { err_ = m; return false; }
    void ws() { while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\t' || t_[p_] == '\n' || t_[p_] == '\r')) ++p_; }
    bool lit(std::string_view s) {
        if (t_.substr(p_, s.size()) != s) return false;
        p_ += s.size();
        return true;
    }

    bool value(JsonValue& v, int depth) {
        if (depth > kMaxDepth) return fail("nesting too deep");
        ws();
        if (p_ >= t_.size()) return fail("unexpected end");
        char c = t_[p_];
        if (c == '{') return object(v, depth);
        if (c == '[') return array(v, depth);
        if (c == '"') { v.type_ = JsonValue::Type::String; return string(v.str_); }
        if (lit("true")) { v.type_ = JsonValue::Type::Bool; v.b_ = true; return true; }
        if (lit("false")) { v.type_ = JsonValue::Type::Bool; v.b_ = false; return true; }
        if (lit("null")) { v.type_ = JsonValue::Type::Null; return true; }
        return number(v);
    }

    bool number(JsonValue& v) {
        size_t s = p_;
        if (p_ < t_.size() && t_[p_] == '-') ++p_;
        while (p_ < t_.size() && (isdigit((unsigned char)t_[p_]) || t_[p_] == '.' || t_[p_] == 'e' || t_[p_] == 'E' ||
                                  t_[p_] == '+' || t_[p_] == '-'))
            ++p_;
        if (s == p_) return fail("invalid value");
        double d = 0;
        auto r = std::from_chars(t_.data() + s, t_.data() + p_, d);
        if (r.ec != std::errc() || r.ptr != t_.data() + p_) return fail("invalid number");
        v.type_ = JsonValue::Type::Number;
        v.num_ = d;
        return true;
    }

    static void putUtf8(std::string& o, uint32_t cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }

    bool hex4(uint32_t& cp) {
        if (p_ + 4 > t_.size()) return fail("bad \\u escape");
        cp = 0;
        for (int i = 0; i < 4; ++i) {
            char h = t_[p_++];
            cp <<= 4;
            if (h >= '0' && h <= '9') cp |= h - '0';
            else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
            else return fail("bad \\u escape");
        }
        return true;
    }

    bool string(std::string& o) {
        ++p_;  // opening quote
        o.clear();
        while (p_ < t_.size()) {
            char c = t_[p_++];
            if (c == '"') return true;
            if ((unsigned char)c < 0x20) return fail("control character in string");
            if (c != '\\') { o += c; continue; }
            if (p_ >= t_.size()) break;
            char e = t_[p_++];
            switch (e) {
            case '"': o += '"'; break;
            case '\\': o += '\\'; break;
            case '/': o += '/'; break;
            case 'b': o += '\b'; break;
            case 'f': o += '\f'; break;
            case 'n': o += '\n'; break;
            case 'r': o += '\r'; break;
            case 't': o += '\t'; break;
            case 'u': {
                uint32_t cp;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00) {  // surrogate pair
                    uint32_t lo;
                    if (!lit("\\u") || !hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return fail("bad surrogate pair");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                putUtf8(o, cp);
                break;
            }
            default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }

    bool array(JsonValue& v, int depth) {
        ++p_;
        v.type_ = JsonValue::Type::Array;
        ws();
        if (lit("]")) return true;
        for (;;) {
            v.arr_.emplace_back();
            if (!value(v.arr_.back(), depth + 1)) return false;
            ws();
            if (lit(",")) continue;
            if (lit("]")) return true;
            return fail("expected , or ]");
        }
    }

    bool object(JsonValue& v, int depth) {
        ++p_;
        v.type_ = JsonValue::Type::Object;
        ws();
        if (lit("}")) return true;
        for (;;) {
            ws();
            if (p_ >= t_.size() || t_[p_] != '"') return fail("expected key");
            std::string k;
            if (!string(k)) return false;
            ws();
            if (!lit(":")) return fail("expected :");
            v.keys_.push_back(std::move(k));
            v.arr_.emplace_back();
            if (!value(v.arr_.back(), depth + 1)) return false;
            ws();
            if (lit(",")) continue;
            if (lit("}")) return true;
            return fail("expected , or }");
        }
    }
};

bool JsonValue::parse(std::string_view text, JsonValue& out, std::string& error) {
    out = JsonValue{};
    JsonParser p(text);
    return p.document(out, error);
}

}  // namespace arx::core
