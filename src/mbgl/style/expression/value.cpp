#include  <mbgl/style/expression/value.hpp>

namespace mbgl {
namespace style {
namespace expression {

struct ConvertValue {
//    null_value_t, bool, uint64_t, int64_t, double, std::string,
//                                         mapbox::util::recursive_wrapper<std::vector<value>>,
//                                         mapbox::util::recursive_wrapper<std::unordered_map<std::string, value>>
    Value operator()(const std::vector<mbgl::Value>& v) {
        std::vector<Value> result;
        for(const auto& item : v) {
            result.emplace_back(convertValue(item));
        }
        return result;
    }
    
    Value operator()(const std::unordered_map<std::string, mbgl::Value>& v) {
        std::unordered_map<std::string, Value> result;
        for(const auto& entry : v) {
            result.emplace(entry.first, convertValue(entry.second));
        }
        return result;
    }
    
    Value operator()(const std::string& s) { return s; }
    Value operator()(const bool& b) { return b; }
    Value operator()(const mbgl::NullValue) { return Null; }
    
    template <typename T>
    Value operator()(const T& v) { return *numericValue<float>(v); }
};

Value convertValue(const mbgl::Value& value) {
    return mbgl::Value::visit(value, ConvertValue());
}

type::Type typeOf(const Value& value) {
    return value.match(
        [&](bool) -> type::Type { return type::Boolean; },
        [&](float) -> type::Type { return type::Number; },
        [&](const std::string&) -> type::Type { return type::String; },
        [&](const mbgl::Color&) -> type::Type { return type::Color; },
        [&](const NullValue&) -> type::Type { return type::Null; },
        [&](const std::unordered_map<std::string, Value>&) -> type::Type { return type::Object; },
        [&](const std::vector<Value>& arr) -> type::Type {
            optional<type::Type> itemType;
            for (const auto& item : arr) {
                const auto& t = typeOf(item);
                const auto& tname = type::toString(t);
                if (!itemType) {
                    itemType = {t};
                } else if (type::toString(*itemType) == tname) {
                    continue;
                } else {
                    itemType = {type::Value};
                    break;
                }
            }
            
            if (!itemType) { itemType = {type::Value}; }

            return type::Array(*itemType, arr.size());
        }
    );
}

std::string stringify(const Value& value) {
    return value.match(
        [&] (const NullValue&) { return std::string("null"); },
        [&] (bool b) { return std::string(b ? "true" : "false"); },
        [&] (float f) { return ceilf(f) == f ? std::to_string((int)f) : std::to_string(f); },
        [&] (const std::string& s) { return "\"" + s + "\""; },
        [&] (const mbgl::Color& c) { return c.stringify(); },
        [&] (const std::vector<Value>& arr) {
            std::string result = "[";
            for(const auto& item : arr) {
                if (result.size() > 1) result += ",";
                result += stringify(item);
            }
            return result + "]";
        },
        [&] (const std::unordered_map<std::string, Value>& obj) {
            std::string result = "{";
            for(const auto& entry : obj) {
                if (result.size() > 1) result += ",";
                result += stringify(entry.first) + ":" + stringify(entry.second);
            }
            return result + "}";
        }
    );
}

template <> type::Type valueTypeToExpressionType<Value>() { return type::Value; }
template <> type::Type valueTypeToExpressionType<NullValue>() { return type::Null; }
template <> type::Type valueTypeToExpressionType<bool>() { return type::Boolean; }
template <> type::Type valueTypeToExpressionType<float>() { return type::Number; }
template <> type::Type valueTypeToExpressionType<std::string>() { return type::String; }
template <> type::Type valueTypeToExpressionType<mbgl::Color>() { return type::Color; }
template <> type::Type valueTypeToExpressionType<std::unordered_map<std::string, Value>>() { return type::Object; }
template <> type::Type valueTypeToExpressionType<std::array<float, 2>>() { return type::Array(type::Number, 2); }
template <> type::Type valueTypeToExpressionType<std::array<float, 4>>() { return type::Array(type::Number, 4); }
template <> type::Type valueTypeToExpressionType<std::vector<Value>>() { return type::Array(type::Value); }

} // namespace expression
} // namespace style
} // namespace mbgl
