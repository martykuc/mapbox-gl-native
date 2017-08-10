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
        ParseResult get = CompoundExpressions::create(CompoundExpressions::definitions.at("get"),
                                                      std::move(getArgs),
                                                      ctx);

        std::vector<std::unique_ptr<Expression>> assertionArgs;
        assertionArgs.push_back(std::move(*get));
        
        return std::move(*(CompoundExpressions::create(CompoundExpressions::definitions.at(type),
                                                       std::move(assertionArgs),
                                                       ctx)));
    }
    
    static std::unique_ptr<Expression> makeZoom(ParsingContext ctx) {
        return std::move(*(CompoundExpressions::create(CompoundExpressions::definitions.at("zoom"),
                                                       std::vector<std::unique_ptr<Expression>>(),
                                                       ctx)));
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
        for(const auto& stop : stops) {
            convertedStops.emplace(
                stop.first,
                makeLiteral(stop.second)
            );
        }
        return convertedStops;
    }
    
    template <typename T>
    static ParseResult makeExponentialCurve(std::unique_ptr<Expression> input,
                                                            const ExponentialStops<T>& stops,
                                                            optional<T> defaultValue)
    {
        std::map<float, std::unique_ptr<Expression>> convertedStops = convertStops(stops.stops);
        ParseResult curve = valueTypeToExpressionType<T>().match(
            [&](const type::NumberType& t) -> ParseResult {
                return ParseResult(std::make_unique<Curve<ExponentialCurve<float>>>(
                    t,
                    std::move(input),
                    ExponentialCurve<float>(std::move(convertedStops), stops.base)
                ));
            },
            [&](const type::ColorType& t) -> ParseResult {
                return ParseResult(std::make_unique<Curve<ExponentialCurve<mbgl::Color>>>(
                    t,
                    std::move(input),
                    ExponentialCurve<mbgl::Color>(std::move(convertedStops), stops.base)
                ));
            },
            [&](const type::Array& arrayType) -> ParseResult {
                if (arrayType.itemType == type::Number && arrayType.N) {
                    return ParseResult(std::make_unique<Curve<ExponentialCurve<std::vector<Value>>>>(
                        arrayType,
                        std::move(input),
                        ExponentialCurve<std::vector<Value>>(std::move(convertedStops), stops.base)
                    ));
                } else {
                    // never: interpolability ensured by ExponentialStops<T>.
                    return ParseResult();
                }
            },
            [&](const auto&) -> ParseResult {
                // never: interpolability ensured by ExponentialStops<T>.
                return ParseResult();
            }
        );
        
        assert(curve);
        return makeCoalesceToDefault(std::move(*curve), defaultValue);
    }
    
    template <typename T>
    static ParseResult makeStepCurve(std::unique_ptr<Expression> input,
                                     const IntervalStops<T>& stops,
                                     optional<T> defaultValue)
    {
        std::map<float, std::unique_ptr<Expression>> convertedStops = convertStops(stops.stops);
        auto curve = std::make_unique<Curve<StepCurve>>(valueTypeToExpressionType<T>(),
                                                                  std::move(input),
                                                                  StepCurve(std::move(convertedStops)));
        return makeCoalesceToDefault(std::move(curve), defaultValue);
    }
    
    template <typename Key, typename T>
    static ParseResult makeMatch(std::unique_ptr<Expression> input,
                                                        const CategoricalStops<T>& stops) {
        // match expression
        typename Match<Key>::Cases cases;
        for(const auto& stop : stops.stops) {
            assert(stop.first.template is<Key>());
            auto key = stop.first.template get<Key>();
            cases.emplace(
                std::move(key),
                makeLiteral(stop.second)
            );
        }
        
        return ParseResult(std::make_unique<Match<Key>>(valueTypeToExpressionType<T>(),
                                            std::move(input),
                                            std::move(cases),
                                            makeLiteral(Null)));
    }
    
    template <typename T>
    static ParseResult makeCase(std::unique_ptr<Expression> input,
                                                       const CategoricalStops<T>& stops) {
        // case expression
        std::vector<typename Case::Branch> cases;
        auto true_case = stops.stops.find(true) == stops.stops.end() ?
            makeLiteral(Null) :
            makeLiteral(stops.stops.at(true));
        auto false_case = stops.stops.find(false) == stops.stops.end() ?
            makeLiteral(Null) :
            makeLiteral(stops.stops.at(false));
        cases.push_back(std::make_pair(std::move(input), std::move(true_case)));
        return ParseResult(std::make_unique<Case>(valueTypeToExpressionType<T>(), std::move(cases), std::move(false_case)));
    }
    
    template <typename T>
    static std::unique_ptr<Expression> toExpression(const ExponentialStops<T>& stops)
    {
        std::vector<ParsingError> errors;
        ParseResult e = makeExponentialCurve(makeZoom(ParsingContext(errors)), stops, optional<T>());
        assert(e);
        return std::move(*e);
    }
    
    template <typename T>
    static std::unique_ptr<Expression> toExpression(const IntervalStops<T>& stops)
    {
        std::vector<ParsingError> errors;
        ParseResult e = makeStepCurve(makeZoom(ParsingContext(errors)), stops, optional<T>());
        assert(e);
        return std::move(*e);
    }
    
    template <typename T>
    static std::unique_ptr<Expression> toExpression(const std::string& property,
                                                  const ExponentialStops<T>& stops,
                                                  optional<T> defaultValue)
    {
        std::vector<ParsingError> errors;
        ParseResult e = makeExponentialCurve(makeGet("number", property, ParsingContext(errors)), stops, defaultValue);
        assert(e);
        return std::move(*e);
    }
    
    template <typename T>
    static std::unique_ptr<Expression> toExpression(const std::string& property,
                                                  const IntervalStops<T>& stops,
                                                  optional<T> defaultValue)
    {
        std::vector<ParsingError> errors;
        ParseResult e = makeStepCurve(makeGet("number", property, ParsingContext(errors)), stops, defaultValue);
        assert(e);
        return std::move(*e);
    }
    
    template <typename T>
    static std::unique_ptr<Expression> toExpression(const std::string& property,
                                                  const CategoricalStops<T>& stops,
                                                  optional<T> defaultValue)
    {
        assert(stops.stops.size() > 0);

        std::vector<ParsingError> errors;

        const auto& firstKey = stops.stops.begin()->first;
        ParseResult expr = firstKey.match(
            [&](bool) {
                auto input = makeGet("boolean", property, ParsingContext(errors));
                return makeCase(std::move(input), stops);
            },
            [&](const std::string&) {
                auto input = makeGet("string", property, ParsingContext(errors));
                return makeMatch<std::string>(std::move(input), stops);
            },
            [&](int64_t) {
                auto input = makeGet("number", property, ParsingContext(errors));
                return makeMatch<int64_t>(std::move(input), stops);
            }

        );
        
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
                auto get = CompoundExpressions::create(CompoundExpressions::definitions.at("get"),
                                                       std::move(getArgs),
                                                       ParsingContext(errors));
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
