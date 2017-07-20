#include <mbgl/style/expression/compound_expression.hpp>
#include <mbgl/tile/geometry_tile_data.hpp>

namespace mbgl {
namespace style {
namespace expression {

namespace detail {
    template <> Value get<Value const&>(const Value& value) {
        return value;
    }
    template <typename T> std::decay_t<T> get(const Value& value) {
        return value.get<std::decay_t<T>>();
    }
} // namespace detail

template <class R, class... Params>
std::unique_ptr<TypedExpression> Signature<R (const EvaluationParameters&, Params...)>::makeTypedExpression(std::vector<std::unique_ptr<TypedExpression>> args) const {
    typename Signature::Args argsArray;
    std::copy_n(std::make_move_iterator(args.begin()), sizeof...(Params), argsArray.begin());
    return std::make_unique<TypedCompoundExpression<Signature>>(*this, std::move(argsArray));
};

template <typename R, typename T>
std::unique_ptr<TypedExpression> Signature<R (const Varargs<T>&)>::makeTypedExpression(std::vector<std::unique_ptr<TypedExpression>> args) const {
    return std::make_unique<TypedCompoundExpression<Signature>>(*this, std::move(args));
};

template <class R, class... Params>
std::unique_ptr<TypedExpression> Signature<R (Params...)>::makeTypedExpression(std::vector<std::unique_ptr<TypedExpression>> args) const {
    typename Signature::Args argsArray;
    std::copy_n(std::make_move_iterator(args.begin()), sizeof...(Params), argsArray.begin());
    return std::make_unique<TypedCompoundExpression<Signature>>(*this, std::move(argsArray));
};

using Definition = CompoundExpression::Definition;

template <typename ...Evals, typename std::enable_if_t<sizeof...(Evals) != 0, int> = 0>
static std::pair<std::string, Definition> define(std::string name, Evals... evalFunctions) {
    Definition definition;
    expand_pack(definition.push_back(std::make_unique<Signature<Evals>>(evalFunctions)));
    const auto& t0 = definition.at(0)->result;
    for (const auto& signature : definition) {
        // TODO replace with real ==
        assert(toString(t0) == toString(signature->result));
    }
    return std::pair<std::string, Definition>(name, std::move(definition));
}

template <typename ...Evals, typename std::enable_if_t<sizeof...(Evals) != 0, int> = 0>
static std::pair<std::string, Definition> defineFeatureFunction(std::string name, Evals... evalFunctions) {
    Definition definition;
    expand_pack(definition.push_back(std::make_unique<Signature<Evals>>(evalFunctions, false)));
    const auto& t0 = definition.at(0)->result;
    for (const auto& signature : definition) {
        // TODO replace with real ==
        assert(toString(t0) == toString(signature->result));
    }
    return std::pair<std::string, Definition>(name, std::move(definition));
}

template <typename ...Entries>
std::unordered_map<std::string, CompoundExpression::Definition> initializeDefinitions(Entries... entries) {
    std::unordered_map<std::string, CompoundExpression::Definition> definitions;
    expand_pack(definitions.insert(std::move(entries)));
    return definitions;
}

static Definition defineGet() {
    Definition definition;
    definition.push_back(
        std::make_unique<Signature<Result<Value> (const EvaluationParameters&, const std::string&)>>(
            [](const EvaluationParameters& params, const std::string& key) -> Result<Value> {
                const auto propertyValue = params.feature.getValue(key);
                if (!propertyValue) {
                    return EvaluationError {
                        "Property '" + key + "' not found in feature.properties"
                    };
                }
                return convertValue(*propertyValue);
            },
            false
        )
    );
    definition.push_back(
        std::make_unique<Signature<Result<Value> (const std::string&, const std::unordered_map<std::string, Value>&)>>(
            [](const std::string& key, const std::unordered_map<std::string, Value>& object) -> Result<Value> {
                if (object.find(key) == object.end()) {
                    return EvaluationError {
                        "Property '" + key + "' not found in object"
                    };
                }
                return object.at(key);
            }
        )
    );
    return definition;
}

template <typename T>
Result<T> assertion(const Value& v) {
    if (!v.is<T>()) {
        return EvaluationError {
            "Expected value to be of type " + toString(valueTypeToExpressionType<T>()) +
            ", but found " + toString(typeOf(v)) + " instead."
        };
    }
    return v.get<T>();
}

std::unordered_map<std::string, CompoundExpression::Definition> CompoundExpression::definitions = initializeDefinitions(
    define("e", []() -> Result<float> { return 2.7f; }),
    define("pi", []() -> Result<float> { return 3.141f; }),
    define("ln2", []() -> Result<float> { return 0.693f; }),
    
    define("typeof", [](const Value& v) -> Result<std::string> { return toString(typeOf(v)); }),
    define("number", assertion<float>),
    define("string", assertion<std::string>),
    define("boolean", assertion<bool>),
    define("array", assertion<std::vector<Value>>), // TODO: [array, type, value], [array, type, length, value]
    
    define("to_string", [](const Value& v) -> Result<std::string> { return stringify(v); }),
    define("to_number", [](const Value& v) -> Result<float> {
        optional<float> result = v.match(
            [](const float f) -> optional<float> { return f; },
            [](const std::string& s) -> optional<float> {
                try {
                    return std::stof(s);
                } catch(std::exception) {
                    return optional<float>();
                }
            },
            [](const auto&) { return optional<float>(); }
        );
        if (!result) {
            return EvaluationError {
                "Could not convert " + stringify(v) + " to number."
            };
        }
        return *result;
    }),
    define("to_boolean", [](const Value& v) -> Result<bool> {
        return v.match(
            [&] (float f) { return (bool)f; },
            [&] (const std::string& s) { return s.length() > 0; },
            [&] (bool b) { return b; },
            [&] (const NullValue&) { return false; },
            [&] (const auto&) { return true; }
        );
    }),
    define("to_rgba", [](const mbgl::Color& color) -> Result<std::array<float, 4>> {
        return std::array<float, 4> {{ color.r, color.g, color.b, color.a }};
    }),
    
    define("parse_color", [](const std::string& colorString) -> Result<mbgl::Color> {
        const auto& result = mbgl::Color::parse(colorString);
        if (result) return *result;
        return EvaluationError {
            "Could not parse color from value '" + colorString + "'"
        };
    }),
    
    std::pair<std::string, Definition>("get", defineGet()),
    
    define("+", [](const Varargs<float>& args) -> Result<float> {
        float sum = 0.0f;
        for (auto arg : args) {
            sum += arg;
        }
        return sum;
    }),
    define("-", [](float a, float b) -> Result<float> { return a - b; })
);

//        if (*op == "to_string") return LambdaExpression::parse<ToString>(value, context);
//        if (*op == "to_number") return LambdaExpression::parse<ToNumber>(value, context);
//        if (*op == "to_boolean") return LambdaExpression::parse<ToBoolean>(value, context);
//        if (*op == "to_rgba") return LambdaExpression::parse<ToRGBA>(value, context);
//        if (*op == "parse_color") return LambdaExpression::parse<ParseColor>(value, context);
//        if (*op == "rgba") return LambdaExpression::parse<RGBA>(value, context);
//        if (*op == "rgb") return LambdaExpression::parse<RGB>(value, context);
//        if (*op == "get") return LambdaExpression::parse<Get>(value, context);
//        if (*op == "has") return LambdaExpression::parse<Has>(value, context);
//        if (*op == "at") return LambdaExpression::parse<At>(value, context);
//        if (*op == "length") return LambdaExpression::parse<Length>(value, context);
//        if (*op == "properties") return LambdaExpression::parse<Properties>(value, context);
//        if (*op == "id") return LambdaExpression::parse<Id>(value, context);
//        if (*op == "geometry_type") return LambdaExpression::parse<GeometryType>(value, context);
//        if (*op == "+") return LambdaExpression::parse<Plus>(value, context);
//        if (*op == "-") return LambdaExpression::parse<Minus>(value, context);
//        if (*op == "*") return LambdaExpression::parse<Times>(value, context);
//        if (*op == "/") return LambdaExpression::parse<Divide>(value, context);
//        if (*op == "^") return LambdaExpression::parse<Power>(value, context);
//        if (*op == "%") return LambdaExpression::parse<Mod>(value, context);
//        if (*op == "coalesce") return LambdaExpression::parse<Coalesce>(value, context);

} // namespace expression
} // namespace style
} // namespace mbgl
