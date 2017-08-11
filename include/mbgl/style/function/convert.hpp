#pragma once

#include <mbgl/util/enum.hpp>
#include <mbgl/style/types.hpp>
#include <mbgl/style/expression/array_assertion.hpp>
#include <mbgl/style/expression/case.hpp>
#include <mbgl/style/expression/coalesce.hpp>
#include <mbgl/style/expression/compound_expression.hpp>
#include <mbgl/style/expression/curve.hpp>
#include <mbgl/style/expression/expression.hpp>
#include <mbgl/style/expression/literal.hpp>
#include <mbgl/style/expression/match.hpp>

#include <mbgl/style/function/exponential_stops.hpp>
#include <mbgl/style/function/interval_stops.hpp>
#include <mbgl/style/function/categorical_stops.hpp>
#include <mbgl/style/function/identity_stops.hpp>

#include <string>


namespace mbgl {
namespace style {
namespace expression {

namespace detail {

class ErrorExpression : public Expression {
public:
    ErrorExpression(std::string message_) : Expression(type::Error), message(std::move(message_)) {}
    bool isFeatureConstant() const override { return true; }
    bool isZoomConstant() const override { return true; }

    EvaluationResult evaluate(const EvaluationParameters&) const override {
        return EvaluationError{message};
    }

private:
    std::string message;
};

}

// Create expressions representing 'classic' (i.e. stop-based) style functions

struct Convert {
    // TODO: Organize. Where should each of these actually go?

    template <typename T>
    static std::unique_ptr<Literal> makeLiteral(const T& value) {
        return std::make_unique<Literal>(Value(toExpressionValue(value)));
    }
    
    static std::unique_ptr<Expression> makeGet(const std::string& type, const std::string& property, ParsingContext ctx) {
        std::vector<std::unique_ptr<Expression>> getArgs;
        getArgs.push_back(makeLiteral(property));
        ParseResult get = CompoundExpressions::create("get", std::move(getArgs), ctx);

        std::vector<std::unique_ptr<Expression>> assertionArgs;
        assertionArgs.push_back(std::move(*get));
        
        return std::move(*(CompoundExpressions::create(type, std::move(assertionArgs), ctx)));
    }
    
    static std::unique_ptr<Expression> makeZoom(ParsingContext ctx) {
        return std::move(*(CompoundExpressions::create("zoom", std::vector<std::unique_ptr<Expression>>(), ctx)));
    }
    
    static std::unique_ptr<Expression> makeError(std::string message) {
        return std::make_unique<detail::ErrorExpression>(message);
    }


    template <typename T>
    static ParseResult makeCoalesceToDefault(std::unique_ptr<Expression> main, optional<T> defaultValue) {
        if (!defaultValue) {
            return ParseResult(std::move(main));
        }
        
        Coalesce::Args args;
        args.push_back(std::move(main));
        args.push_back(makeLiteral(*defaultValue));
        return ParseResult(std::make_unique<Coalesce>(valueTypeToExpressionType<T>(), std::move(args)));
    }
    
    template <typename T>
    static std::map<float, std::unique_ptr<Expression>> convertStops(const std::map<float, T>& stops) {
        std::map<float, std::unique_ptr<Expression>> convertedStops;
        for(const std::pair<float, T>& stop : stops) {
            convertedStops.emplace(
                stop.first,
                makeLiteral(stop.second)
            );
        }
        return convertedStops;
    }
    
    template <typename T>
    static ParseResult makeExponentialCurve(std::unique_ptr<Expression> input,
                                                            std::map<float, std::unique_ptr<Expression>> convertedStops,
                                                            float base,
                                                            optional<T> defaultValue)
    {
        ParseResult curve = valueTypeToExpressionType<T>().match(
            [&](const type::NumberType& t) -> ParseResult {
                return ParseResult(std::make_unique<Curve<ExponentialInterpolator<float>>>(
                    t,
                    ExponentialInterpolator<float>(base),
                    std::move(input),
                    std::move(convertedStops)
                ));
            },
            [&](const type::ColorType& t) -> ParseResult {
                return ParseResult(std::make_unique<Curve<ExponentialInterpolator<mbgl::Color>>>(
                    t,
                    ExponentialInterpolator<mbgl::Color>(base),
                    std::move(input),
                    std::move(convertedStops)
                ));
            },
            [&](const type::Array& arrayType) -> ParseResult {
                if (arrayType.itemType == type::Number && arrayType.N) {
                    return ParseResult(std::make_unique<Curve<ExponentialInterpolator<std::vector<Value>>>>(
                        arrayType,
                        ExponentialInterpolator<std::vector<Value>>(base),
                        std::move(input),
                        std::move(convertedStops)
                    ));
                } else {
                    return ParseResult();
                }
            },
            [&](const auto&) -> ParseResult {
                return ParseResult();
            }
        );
        
        assert(curve);
        return makeCoalesceToDefault(std::move(*curve), defaultValue);
    }
    
    template <typename T>
    static ParseResult makeStepCurve(std::unique_ptr<Expression> input,
                                     const std::map<float, T>& stops,
                                     optional<T> defaultValue)
    {
        std::map<float, std::unique_ptr<Expression>> convertedStops = convertStops(stops);
        auto curve = std::make_unique<Curve<StepInterpolator>>(valueTypeToExpressionType<T>(),
                                                               StepInterpolator(),
                                                               std::move(input),
                                                               std::move(convertedStops));
        return makeCoalesceToDefault(std::move(curve), defaultValue);
    }
    
    template <typename Key, typename T>
    static ParseResult makeMatch(std::unique_ptr<Expression> input,
                                 const std::map<CategoricalValue, T>& stops) {
        // match expression
        typename Match<Key>::Cases cases;
        for(const std::pair<CategoricalValue, T>& stop : stops) {
            assert(stop.first.template is<Key>());
            Key key = stop.first.template get<Key>();
            cases.emplace(
                std::move(key),
                makeLiteral(stop.second)
            );
        }
        
        return ParseResult(std::make_unique<Match<Key>>(valueTypeToExpressionType<T>(),
                                            std::move(input),
                                            std::move(cases),
                                            makeError("No matching label")));
    }
    
    template <typename T>
    static ParseResult makeCase(std::unique_ptr<Expression> input,
                                const std::map<CategoricalValue, T>& stops) {
        // case expression
        std::vector<typename Case::Branch> cases;
        
        auto it = stops.find(true);
        std::unique_ptr<Expression> true_case = it == stops.end() ?
            makeError("No matching label") :
            makeLiteral(it->second);

        it = stops.find(false);
        std::unique_ptr<Expression> false_case = it == stops.end() ?
            makeError("No matching label") :
            makeLiteral(it->second);

        cases.push_back(std::make_pair(std::move(input), std::move(true_case)));
        return ParseResult(std::make_unique<Case>(valueTypeToExpressionType<T>(), std::move(cases), std::move(false_case)));
    }
    
    template <typename T>
    static ParseResult convertCategoricalStops(std::map<CategoricalValue, T> stops, const std::string& property) {
        assert(stops.size() > 0);

        std::vector<ParsingError> errors;
        ParsingContext ctx(errors);

        const CategoricalValue& firstKey = stops.begin()->first;
        return firstKey.match(
            [&](bool) {
                return makeCase(makeGet("boolean", property, ctx), stops);
            },
            [&](const std::string&) {
                return makeMatch<std::string>(makeGet("string", property, ctx), stops);
            },
            [&](int64_t) {
                return makeMatch<int64_t>(makeGet("number", property, ctx), stops);
            }
        );
    }
    
    template <typename T>
    static std::unique_ptr<Expression> toExpression(const ExponentialStops<T>& stops)
    {
        std::vector<ParsingError> errors;
        ParseResult e = makeExponentialCurve(makeZoom(ParsingContext(errors)),
                                             convertStops(stops.stops),
                                             stops.base,
                                             optional<T>());
        assert(e);
        return std::move(*e);
    }
    
    template <typename T>
    static std::unique_ptr<Expression> toExpression(const IntervalStops<T>& stops)
    {
        std::vector<ParsingError> errors;
        ParseResult e = makeStepCurve(makeZoom(ParsingContext(errors)), stops.stops, optional<T>());
        assert(e);
        return std::move(*e);
    }
    
    template <typename T>
    static std::unique_ptr<Expression> toExpression(const std::string& property,
                                                  const ExponentialStops<T>& stops,
                                                  optional<T> defaultValue)
    {
        std::vector<ParsingError> errors;
        ParseResult e = makeExponentialCurve(makeGet("number", property, ParsingContext(errors)),
                                             convertStops(stops.stops),
                                             stops.base,
                                             defaultValue);
        assert(e);
        return std::move(*e);
    }
    
    template <typename T>
    static std::unique_ptr<Expression> toExpression(const std::string& property,
                                                  const IntervalStops<T>& stops,
                                                  optional<T> defaultValue)
    {
        std::vector<ParsingError> errors;
        ParseResult e = makeStepCurve(makeGet("number", property, ParsingContext(errors)), stops.stops, defaultValue);
        assert(e);
        return std::move(*e);
    }
    
    template <typename T>
    static std::unique_ptr<Expression> toExpression(const std::string& property,
                                                  const CategoricalStops<T>& stops,
                                                  optional<T> defaultValue)
    {
        ParseResult expr = convertCategoricalStops(stops.stops, property);
        assert(expr);
        ParseResult e = makeCoalesceToDefault(std::move(*expr), defaultValue);
        assert(e);
        return std::move(*e);
    }
    
    template <typename T>
    static std::unique_ptr<Expression> toExpression(const std::string& property,
                                                  const IdentityStops<T>&,
                                                  optional<T> defaultValue)
    {
        std::vector<ParsingError> errors;

        std::unique_ptr<Expression> input = valueTypeToExpressionType<T>().match(
            [&] (const type::StringType&) {
                return makeGet("string", property, ParsingContext(errors));
            },
            [&] (const type::NumberType&) {
                return makeGet("number", property, ParsingContext(errors));
            },
            [&] (const type::BooleanType&) {
                return makeGet("boolean", property, ParsingContext(errors));
            },
            [&] (const type::Array& arr) {
                std::vector<std::unique_ptr<Expression>> getArgs;
                getArgs.push_back(makeLiteral(property));
                ParseResult get = CompoundExpressions::create("get", std::move(getArgs), ParsingContext(errors));
                return std::make_unique<ArrayAssertion>(arr, std::move(*get));
            },
            [&] (const auto&) -> std::unique_ptr<Expression> {
                return makeLiteral(Null);
            }
        );
        
        ParseResult e = makeCoalesceToDefault(std::move(input), defaultValue);
        assert(e);
        return std::move(*e);
    }
};

} // namespace expression
} // namespace style
} // namespace mbgl
