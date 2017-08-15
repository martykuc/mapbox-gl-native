#pragma once

#include <array>
#include <mbgl/style/expression/type.hpp>
#include <mbgl/style/position.hpp>
#include <mbgl/style/types.hpp>
#include <mbgl/util/color.hpp>
#include <mbgl/util/enum.hpp>
#include <mbgl/util/feature.hpp>
#include <mbgl/util/variant.hpp>


namespace mbgl {
namespace style {
namespace expression {

struct Value;

using ValueBase = variant<
    NullValue,
    bool,
    double,
    std::string,
    mbgl::Color,
    mapbox::util::recursive_wrapper<std::vector<Value>>,
    mapbox::util::recursive_wrapper<std::unordered_map<std::string, Value>>>;
struct Value : ValueBase {
    using ValueBase::ValueBase;

    // Javascript's Number.MAX_SAFE_INTEGER
    static uint64_t max() { return 9007199254740991ULL; }
    
    static bool isSafeNumericValue(uint64_t x) { return x <= max(); };
    static bool isSafeNumericValue(int64_t x) {
        return static_cast<uint64_t>(x > 0 ? x : -x) <= max();
    }
    static bool isSafeNumericValue(double x) {
        return static_cast<uint64_t>(x > 0 ? x : -x) <= max();
    }
    
};

Value toExpressionValue(const Value&);

template <typename T, typename Enable = std::enable_if_t< !std::is_convertible<T, Value>::value >>
Value toExpressionValue(const T& value);

template <typename T>
std::enable_if_t< !std::is_convertible<T, Value>::value,
optional<T>> fromExpressionValue(const Value& v);

template <typename T>
std::enable_if_t< std::is_convertible<T, Value>::value,
optional<T>> fromExpressionValue(const Value& v)
{
    return v.template is<T>() ? v.template get<T>() : optional<T>();
}

constexpr NullValue Null = NullValue();

type::Type typeOf(const Value& value);
std::string stringify(const Value& value);

/*
  Returns a Type object representing the expression type that corresponds to
  the value type T.  (Specialized for primitives and specific array types in
  the .cpp.)
*/
template <typename T>
type::Type valueTypeToExpressionType();


} // namespace expression
} // namespace style
} // namespace mbgl
