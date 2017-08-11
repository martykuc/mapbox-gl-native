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

CompoundExpressionBase::~CompoundExpressionBase() {};

template <class R, class... Params>
std::unique_ptr<Expression> Signature<R (const EvaluationParameters&, Params...)>
::makeExpression(const std::string& name, std::vector<std::unique_ptr<Expression>> args) const
{
    typename Signature::Args argsArray;
    std::copy_n(std::make_move_iterator(args.begin()), sizeof...(Params), argsArray.begin());
    return std::make_unique<CompoundExpression<Signature>>(name, *this, std::move(argsArray));
};

template <typename R, typename T>
std::unique_ptr<Expression> Signature<R (const Varargs<T>&)>
::makeExpression(const std::string& name, std::vector<std::unique_ptr<Expression>> args) const {
    return std::make_unique<CompoundExpression<Signature>>(name, *this, std::move(args));
};

template <class R, class... Params>
std::unique_ptr<Expression> Signature<R (Params...)>
::makeExpression(const std::string& name, std::vector<std::unique_ptr<Expression>> args) const {
    typename Signature::Args argsArray;
    std::copy_n(std::make_move_iterator(args.begin()), sizeof...(Params), argsArray.begin());
    return std::make_unique<CompoundExpression<Signature>>(name, *this, std::move(argsArray));
};

using Definition = CompoundExpressions::Definition;

// Helper for creating expression Definigion from one or more lambdas
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


// Helper for defining feature-dependent expression
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

// Special case defineHas() and defineGet() functions because their overloads
// have differing values for isFeatureConstant.
static Definition defineHas() {
    Definition definition;
    definition.push_back(
        std::make_unique<Signature<Result<bool> (const EvaluationParameters&, const std::string&)>>(
            [](const EvaluationParameters& params, const std::string& key) -> Result<bool> {
                if (!params.feature) {
                    return EvaluationError {
                        "Feature data is unavailable in the current evaluation context."
                    };
                }
                
                return params.feature->getValue(key) ? true : false;
            },
            false
        )
    );
    definition.push_back(
        std::make_unique<Signature<Result<bool> (const std::string&, const std::unordered_map<std::string, Value>&)>>(
            [](const std::string& key, const std::unordered_map<std::string, Value>& object) -> Result<bool> {
                return object.find(key) != object.end();
            }
        )
    );
    return definition;
}

static Definition defineGet() {
    Definition definition;
    definition.push_back(
        std::make_unique<Signature<Result<Value> (const EvaluationParameters&, const std::string&)>>(
            [](const EvaluationParameters& params, const std::string& key) -> Result<Value> {
                if (!params.feature) {
                    return EvaluationError {
                        "Feature data is unavailable in the current evaluation context."
                    };
                }

                auto propertyValue = params.feature->getValue(key);
                if (!propertyValue) {
                    return EvaluationError {
                        "Property '" + key + "' not found in feature.properties"
                    };
                }
                return Value(toExpressionValue(*propertyValue));
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

// Define "zoom" expression, it being the only one for which isZoomConstant == false.
static std::pair<std::string, Definition> defineZoom() {
    auto zoom = [](const EvaluationParameters& params) -> Result<float> {
        if (!params.zoom) {
            return EvaluationError {
                "The 'zoom' expression is unavailable in the current evaluation context."
            };
        }
        return *(params.zoom);
    };
    Definition definition;
    definition.push_back(std::make_unique<Signature<decltype(zoom)>>(zoom, true, false));
    return std::pair<std::string, Definition>("zoom", std::move(definition));
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

std::string stringifyColor(float r, float g, float b, float a) {
    return stringify(r) + ", " +
        stringify(g) + ", " +
        stringify(b) + ", " +
        stringify(a);
}
Result<mbgl::Color> rgba(float r, float g, float b, float a) {
    if (
        r < 0 || r > 255 ||
        g < 0 || g > 255 ||
        b < 0 || b > 255
    ) {
        return EvaluationError {
            "Invalid rgba value [" + stringifyColor(r, g, b, a)  +
            "]: 'r', 'g', and 'b' must be between 0 and 255."
        };
    }
    if (a < 0 || a > 1) {
        return EvaluationError {
            "Invalid rgba value [" + stringifyColor(r, g, b, a)  +
            "]: 'a' must be between 0 and 1."
        };
    }
    return mbgl::Color(r / 255, g / 255, b / 255, a);
}

template <typename ...Entries>
std::unordered_map<std::string, CompoundExpressions::Definition> initializeDefinitions(Entries... entries) {
    std::unordered_map<std::string, CompoundExpressions::Definition> definitions;
    expand_pack(definitions.insert(std::move(entries)));
    return definitions;
}

std::unordered_map<std::string, Definition> CompoundExpressions::definitions = initializeDefinitions(
    define("e", []() -> Result<float> { return 2.718281828459045f; }),
    define("pi", []() -> Result<float> { return 3.141592653589793f; }),
    define("ln2", []() -> Result<float> { return 0.6931471805599453; }),

    define("typeof", [](const Value& v) -> Result<std::string> { return toString(typeOf(v)); }),
    define("number", assertion<float>),
    define("string", assertion<std::string>),
    define("boolean", assertion<bool>),
    
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
    
    define("rgba", rgba),
    define("rgb", [](float r, float g, float b) { return rgba(r, g, b, 1.0f); }),
    
    defineZoom(),
    
    std::pair<std::string, Definition>("has", defineHas()),
    std::pair<std::string, Definition>("get", defineGet()),
    
    define("length", [](const std::vector<Value>& arr) -> Result<float> {
        return arr.size();
    }, [] (const std::string s) -> Result<float> {
        return s.size();
    }),
    
    defineFeatureFunction("properties", [](const EvaluationParameters& params) -> Result<std::unordered_map<std::string, Value>> {
        if (!params.feature) {
            return EvaluationError {
                "Feature data is unavailable in the current evaluation context."
            };
        }
        std::unordered_map<std::string, Value> result;
        const auto& properties = params.feature->getProperties();
        for (const auto& entry : properties) {
            result[entry.first] = toExpressionValue(entry.second);
        }
        return result;
    }),
    
    defineFeatureFunction("geometry_type", [](const EvaluationParameters& params) -> Result<std::string> {
        if (!params.feature) {
            return EvaluationError {
                "Feature data is unavailable in the current evaluation context."
            };
        }
    
        auto type = params.feature->getType();
        if (type == FeatureType::Point) {
            return "Point";
        } else if (type == FeatureType::LineString) {
            return "LineString";
        } else if (type == FeatureType::Polygon) {
            return "Polygon";
        } else {
            return "Unknown";
        }
    }),
    
    defineFeatureFunction("id", [](const EvaluationParameters& params) -> Result<Value> {
        if (!params.feature) {
            return EvaluationError {
                "Feature data is unavailable in the current evaluation context."
            };
        }
    
        auto id = params.feature->getID();
        if (!id) {
            return EvaluationError {
                "Property 'id' not found in feature"
            };
        }
        return id->match(
            [](const auto& idValue) {
                return toExpressionValue(mbgl::Value(idValue));
            }
        );
    }),
    
    define("+", [](const Varargs<float>& args) -> Result<float> {
        float sum = 0.0f;
        for (auto arg : args) {
            sum += arg;
        }
        return sum;
    }),
    define("-", [](float a, float b) -> Result<float> { return a - b; }),
    define("*", [](const Varargs<float>& args) -> Result<float> {
        float prod = 1.0f;
        for (auto arg : args) {
            prod *= arg;
        }
        return prod;
    }),
    define("/", [](float a, float b) -> Result<float> { return a / b; }),
    
    define("&&", [](const Varargs<bool>& args) -> Result<bool> {
        bool result = true;
        for (auto arg : args) result = result && arg;
        return result;
    }),
    
    define("||", [](const Varargs<bool>& args) -> Result<bool> {
        bool result = false;
        for (auto arg : args) result = result || arg;
        return result;
    }),
    
    define("!", [](bool e) -> Result<bool> { return !e; })
    
);

} // namespace expression
} // namespace style
} // namespace mbgl
