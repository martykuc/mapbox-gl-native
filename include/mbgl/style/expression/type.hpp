#pragma once

#include <unordered_map>
#include <vector>
#include <mbgl/util/optional.hpp>
#include <mbgl/util/variant.hpp>


namespace mbgl {
namespace style {
namespace expression {
namespace type {

template <class T>
std::string toString(const T& t);

struct NullType {
    constexpr NullType() {}
    std::string getName() const { return "Null"; }
    bool operator==(const NullType&) const { return true; }
};

struct NumberType {
    constexpr NumberType() {}
    std::string getName() const { return "Number"; }
    bool operator==(const NumberType&) const { return true; }
};

struct BooleanType {
    constexpr BooleanType() {}
    std::string getName() const { return "Boolean"; }
    bool operator==(const BooleanType&) const { return true; }
};

struct StringType {
    constexpr StringType() {}
    std::string getName() const { return "String"; }
    bool operator==(const StringType&) const { return true; }
};

struct ColorType {
    constexpr ColorType() {}
    std::string getName() const { return "Color"; }
    bool operator==(const ColorType&) const { return true; }
};

struct ObjectType {
    constexpr ObjectType() {}
    std::string getName() const { return "Object"; }
    bool operator==(const ObjectType&) const { return true; }
};

struct ValueType {
    constexpr ValueType() {}
    std::string getName() const { return "Value"; }
    bool operator==(const ValueType&) const { return true; }
};

constexpr NullType Null;
constexpr NumberType Number;
constexpr StringType String;
constexpr BooleanType Boolean;
constexpr ColorType Color;
constexpr ValueType Value;
constexpr ObjectType Object;

struct Array;

using Type = variant<
    NullType,
    NumberType,
    BooleanType,
    StringType,
    ColorType,
    ObjectType,
    ValueType,
    mapbox::util::recursive_wrapper<Array>>;

struct Array {
    Array(Type itemType_) : itemType(itemType_) {}
    Array(Type itemType_, std::size_t N_) : itemType(itemType_), N(N_) {}
    Array(Type itemType_, optional<std::size_t> N_) : itemType(itemType_), N(N_) {}
    std::string getName() const {
        if (N) {
            return "Array<" + toString(itemType) + ", " + std::to_string(*N) + ">";
        } else if (toString(itemType) == "Value") {
            return "Array";
        } else {
            return "Array<" + toString(itemType) + ">";
        }
    }

    bool operator==(const Array& rhs) const { return itemType == rhs.itemType && N == rhs.N; }
    
    Type itemType;
    optional<std::size_t> N;
};

template <class T>
std::string toString(const T& t) { return t.match([&] (const auto& t) { return t.getName(); }); }

} // namespace type
} // namespace expression
} // namespace style
} // namespace mbgl
