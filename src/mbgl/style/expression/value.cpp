#include  <mbgl/style/expression/value.hpp>
#include <sstream>

namespace mbgl {
namespace style {
namespace expression {

type::Type typeOf(const Value& value) {
    return value.match(
        [&](bool) -> type::Type { return type::Boolean; },
        [&](double) -> type::Type { return type::Number; },
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
        [&] (double f) {
            std::stringstream ss;
            ss << f;
            return ss.str();
        },
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


template <class T, class Enable = void>
struct Converter {
    static Value toExpressionValue(const T& value) {
        return Value(value);
    }
    static optional<T> fromExpressionValue(const Value& value) {
        return value.template is<T>() ? value.template get<T>() : optional<T>();
    }
};

template <>
struct Converter<float> {
    static Value toExpressionValue(const float& value) {
        return static_cast<double>(value);
    }
    
    static optional<float> fromExpressionValue(const Value& value) {
        if (value.template is<double>()) {
            double v = value.template get<double>();
            if (v <= Value::max()) {
                return static_cast<float>(v);
            }
        }
        return optional<float>();
    }
    
    static type::Type expressionType() {
        return type::Number;
    }
};

template<>
struct Converter<mbgl::Value> {
    static Value toExpressionValue(const mbgl::Value& value) {
        return mbgl::Value::visit(value, Converter<mbgl::Value>());
    }


    // Double duty as a variant visitor for mbgl::Value:
    Value operator()(const std::vector<mbgl::Value>& v) {
        std::vector<Value> result;
        for(const auto& item : v) {
            result.emplace_back(toExpressionValue(item));
        }
        return result;
    }
    
    Value operator()(const std::unordered_map<std::string, mbgl::Value>& v) {
        std::unordered_map<std::string, Value> result;
        for(const auto& entry : v) {
            result.emplace(entry.first, toExpressionValue(entry.second));
        }
        return result;
    }
    
    Value operator()(const std::string& s) { return s; }
    Value operator()(const bool& b) { return b; }
    Value operator()(const mbgl::NullValue) { return Null; }
    Value operator()(const double& v) { return v; }
    Value operator()(const uint64_t& v) {
        return v > static_cast<uint64_t>(Value::max()) ? static_cast<double>(Value::max()) : v;
    }
    Value operator()(const int64_t& v) {
        return v > std::numeric_limits<double>::max() ? std::numeric_limits<double>::max() : v;
    }
};

template <typename T, typename Container>
std::vector<Value> toArrayValue(const Container& value) {
    std::vector<Value> result;
    for (const T& item : value) {
        result.push_back(Converter<T>::toExpressionValue(item));
    }
    return result;
}

template <typename T, typename Container>
optional<Container> fromArrayValue(const std::vector<Value>& value) {
    Container result;
    auto it = result.begin();
    for(const Value& item : value) {
        optional<T> convertedItem = Converter<T>::fromExpressionValue(item);
        if (!convertedItem) {
            return optional<Container>();
        }
        *it = *convertedItem;
        it = std::next(it);
    }
    return result;
}

template <typename T, std::size_t N>
struct Converter<std::array<T, N>> {
    static Value toExpressionValue(const std::array<T, N>& value) {
        return toArrayValue<T>(value);
    }
    
    static optional<std::array<T, N>> fromExpressionValue(const Value& value) {
        return value.match(
            [&] (const std::vector<Value>& v) -> optional<std::array<T, N>> {
                if (v.size() != N) return optional<std::array<T, N>>();
                return fromArrayValue<T, std::array<T, N>>(v);
            },
            [&] (const auto&) { return optional<std::array<T, N>>(); }
        );
    }
    
    static type::Type expressionType() {
        return type::Array(valueTypeToExpressionType<T>(), N);
    }
};

template <typename T>
struct Converter<std::vector<T>> {
    static Value toExpressionValue(const std::vector<T>& value) {
        return toArrayValue<T>(value);
    }
    
    static optional<std::vector<T>> fromExpressionValue(const Value& value) {
        return value.match(
            [&] (const std::vector<Value>& v) -> optional<std::vector<T>> {
                return fromArrayValue<T, std::vector<T>>(v);
            },
            [&] (const auto&) { return optional<std::vector<T>>(); }
        );
    }
    
    static type::Type expressionType() {
        return type::Array(valueTypeToExpressionType<T>());
    }
};

template <>
struct Converter<Position> {
    static Value toExpressionValue(const mbgl::style::Position& value) {
        return Converter<std::array<float, 3>>::toExpressionValue(value.getSpherical());
    }
    
    static optional<Position> fromExpressionValue(const Value& v) {
        auto pos = Converter<std::array<float, 3>>::fromExpressionValue(v);
        return pos ? optional<Position>(Position(*pos)) : optional<Position>();
    }
    
    static type::Type expressionType() {
        return type::Array(type::Number, 3);
    }
};

template <typename T>
struct Converter<T, std::enable_if_t< std::is_enum<T>::value >> {
    static Value toExpressionValue(const T& value) {
        return std::string(Enum<T>::toString(value));
    }
    
    static optional<T> fromExpressionValue(const Value& value) {
        return value.match(
            [&] (const std::string& v) { return Enum<T>::toEnum(v); },
            [&] (const auto&) { return optional<T>(); }
        );
    }
    
    static type::Type expressionType() {
        return type::String;
    }
};

Value toExpressionValue(const Value& v) {
    return v;
}

template <typename T, typename Enable>
Value toExpressionValue(const T& value) {
    return Converter<T>::toExpressionValue(value);
}

template <typename T>
std::enable_if_t< !std::is_convertible<T, Value>::value,
optional<T>> fromExpressionValue(const Value& v)
{
    return Converter<T>::fromExpressionValue(v);
}

template <typename T>
type::Type valueTypeToExpressionType() {
    return Converter<T>::expressionType();
}

template <> type::Type valueTypeToExpressionType<Value>() { return type::Value; }
template <> type::Type valueTypeToExpressionType<NullValue>() { return type::Null; }
template <> type::Type valueTypeToExpressionType<bool>() { return type::Boolean; }
template <> type::Type valueTypeToExpressionType<double>() { return type::Number; }
template <> type::Type valueTypeToExpressionType<std::string>() { return type::String; }
template <> type::Type valueTypeToExpressionType<mbgl::Color>() { return type::Color; }
template <> type::Type valueTypeToExpressionType<std::unordered_map<std::string, Value>>() { return type::Object; }
template <> type::Type valueTypeToExpressionType<std::vector<Value>>() { return type::Array(type::Value); }


template Value toExpressionValue(const mbgl::Value&);

// instantiate templates fromExpressionValue<T>, toExpressionValue<T>, and valueTypeToExpressionType<T>
template <typename T>
struct instantiate {
    void noop(const T& t) {
        fromExpressionValue<T>(toExpressionValue(t));
        valueTypeToExpressionType<T>();
    }
};

// for to_rgba expression
template struct instantiate<std::array<double, 4>>;

// layout/paint property types
template struct instantiate<float>;
template struct instantiate<std::array<float, 2>>;
template struct instantiate<std::array<float, 4>>;
template struct instantiate<std::vector<float>>;
template struct instantiate<std::vector<std::string>>;
template struct instantiate<AlignmentType>;
template struct instantiate<CirclePitchScaleType>;
template struct instantiate<IconTextFitType>;
template struct instantiate<LineCapType>;
template struct instantiate<LineJoinType>;
template struct instantiate<SymbolPlacementType>;
template struct instantiate<TextAnchorType>;
template struct instantiate<TextJustifyType>;
template struct instantiate<TextTransformType>;
template struct instantiate<TranslateAnchorType>;
template struct instantiate<LightAnchorType>;
template struct instantiate<Position>;

} // namespace expression
} // namespace style
} // namespace mbgl
