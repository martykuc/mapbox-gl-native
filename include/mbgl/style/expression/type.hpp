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
};

struct NumberType {
    constexpr NumberType() {}
    std::string getName() const { return "Number"; }
};

struct BooleanType {
    constexpr BooleanType() {}
    std::string getName() const { return "Boolean"; }
};

struct StringType {
    constexpr StringType() {}
    std::string getName() const { return "String"; }
};

struct ColorType {
    constexpr ColorType() {}
    std::string getName() const { return "Color"; }
};

struct ObjectType {
    constexpr ObjectType() {}
    std::string getName() const { return "Object"; }
};

struct ValueType {
    constexpr ValueType() {}
    std::string getName() const { return "Value"; }
};

constexpr NullType Null;
constexpr NumberType Number;
constexpr StringType String;
constexpr BooleanType Boolean;
constexpr ColorType Color;
constexpr ValueType Value;
constexpr ObjectType Object;

class Typename {
public:
    Typename(std::string name_) : name(name_) {}
    std::string getName() const { return name; }
private:
    std::string name;
};

struct Array;

using Type = variant<
    NullType,
    NumberType,
    BooleanType,
    StringType,
    ColorType,
    ObjectType,
    ValueType,
    Typename,
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
    
    Type itemType;
    optional<std::size_t> N;
};

template <class T>
std::string toString(const T& t) { return t.match([&] (const auto& t) { return t.getName(); }); }

bool isGeneric(const Type& t);
Type resolveTypenamesIfPossible(const Type&, const std::unordered_map<std::string, Type>&);

optional<std::string> matchType(const Type& expected, const Type& t);

enum TypenameScope { expected, actual };
optional<std::string> matchType(const Type& expected,
                                const Type& t,
                                std::unordered_map<std::string, Type>& typenames,
                                TypenameScope scope = TypenameScope::expected);


} // namespace type
} // namespace expression
} // namespace style
} // namespace mbgl
