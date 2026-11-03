// Minimal JSON reader for models/manifest.json (UTF-8, RFC 8259 subset: no comments).
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace arx::core {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type() const { return type_; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }
    bool isString() const { return type_ == Type::String; }
    bool isNumber() const { return type_ == Type::Number; }

    bool asBool() const { return b_; }
    double asNumber() const { return num_; }
    const std::string& asString() const { return str_; }
    const std::vector<JsonValue>& items() const { return arr_; }  // Array

    // Object member lookup; nullptr if absent or not an object.
    const JsonValue* get(std::string_view key) const;

    // Parse a whole document. Returns false and sets `error` (with byte offset) on failure.
    static bool parse(std::string_view text, JsonValue& out, std::string& error);

private:
    friend class JsonParser;
    Type type_ = Type::Null;
    bool b_ = false;
    double num_ = 0.0;
    std::string str_;
    std::vector<JsonValue> arr_;   // Array items, or Object values
    std::vector<std::string> keys_;  // Object keys (parallel to arr_)
};

}  // namespace arx::core
