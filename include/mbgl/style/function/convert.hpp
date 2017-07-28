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
    static std::unique_ptr<UntypedLiteral> makeLiteral(const T& value) {
        return std::make_unique<UntypedLiteral>("", Value(toExpressionValue(value)));
    }
    
    static std::unique_ptr<UntypedExpression> makeGet(const std::string& type, const std::string& property) {
        UntypedCompoundExpression::Args getArgs;
        getArgs.push_back(makeLiteral(property));
        auto get = std::make_unique<UntypedCompoundExpression>("", "get", std::move(getArgs));
        UntypedCompoundExpression::Args assertionArgs;
        assertionArgs.push_back(std::move(get));
        return std::make_unique<UntypedCompoundExpression>("", type, std::move(assertionArgs));
    }
    
    static std::unique_ptr<UntypedExpression> makeZoom() {
        return std::make_unique<UntypedCompoundExpression>("", "zoom", UntypedCompoundExpression::Args());
    }

    template <typename T>
    static std::unique_ptr<UntypedExpression> makeCoalesceToDefault(std::unique_ptr<UntypedExpression> main, optional<T> defaultValue) {
        if (!defaultValue) {
            return main;
        }
        
        UntypedCoalesce::Args args;
        args.push_back(std::move(main));
        args.push_back(makeLiteral(*defaultValue));
        return(std::make_unique<UntypedCoalesce>("", std::move(args)));
    }

    template <typename T>
    static std::unique_ptr<TypedExpression> makeCurve(std::unique_ptr<UntypedExpression> input,
                                                      UntypedCurve::Interpolation interpolation,
                                                      const std::map<float, T>& stops,
                                                      optional<T> defaultValue)
    {
        UntypedCurve::Stops convertedStops;
        for(const auto& stop : stops) {
            convertedStops.push_back(std::make_pair(
                stop.first,
                makeLiteral(stop.second)
            ));
        }
        
        std::unique_ptr<UntypedExpression> curve = std::make_unique<UntypedCurve>("", std::move(input), std::move(convertedStops), interpolation);
        
        std::vector<CompileError> errors;
        auto checked = makeCoalesceToDefault(std::move(curve), defaultValue)->typecheck(errors);
        assert(checked);
        return std::move(*checked);
    }
    
    template <typename Key, typename T>
    static std::unique_ptr<UntypedExpression> makeMatch(std::unique_ptr<UntypedExpression> input,
                                                        const CategoricalStops<T>& stops) {
        // match expression
        UntypedMatch::Cases cases;
        optional<type::Type> inputType;
        for(const auto& stop : stops.stops) {
            if (!inputType) {
                inputType = stop.first.template is<std::string>() ?
                    type::Type(type::String) : type::Type(type::Number);
            }
            assert(stop.first.template is<Key>());
            auto key = stop.first.template get<Key>();
            cases.push_back(std::make_pair(
                std::move(key),
                makeLiteral(stop.second)
            ));
        }
        
        return std::make_unique<UntypedMatch>("",
                                             std::move(input),
                                             std::move(cases),
                                             makeLiteral(Null),
                                             *inputType);
    }
    
    template <typename T>
    static std::unique_ptr<UntypedExpression> makeCase(std::unique_ptr<UntypedExpression> input,
                                                       const CategoricalStops<T>& stops) {
        // case expression
        UntypedCase::Cases cases;
        auto true_case = stops.stops.find(true) == stops.stops.end() ?
            makeLiteral(Null) :
            makeLiteral(stops.stops.at(true));
        auto false_case = stops.stops.find(false) == stops.stops.end() ?
            makeLiteral(Null) :
            makeLiteral(stops.stops.at(false));
        cases.push_back(std::make_pair(std::move(input), std::move(true_case)));
        return std::make_unique<UntypedCase>("", std::move(cases), std::move(false_case));
    }
    
    template <typename T>
    static std::unique_ptr<TypedExpression> toExpression(const ExponentialStops<T>& stops)
    {
        return makeCurve(makeZoom(), ExponentialInterpolation{stops.base}, stops.stops, {});
    }
    
    template <typename T>
    static std::unique_ptr<TypedExpression> toExpression(const IntervalStops<T>& stops)
    {
        return makeCurve(makeZoom(), StepInterpolation(), stops.stops, {});
    }
    
    template <typename T>
    static std::unique_ptr<TypedExpression> toExpression(const std::string& property,
                                                  const ExponentialStops<T>& stops,
                                                  optional<T> defaultValue)
    {
        return makeCurve(makeGet("number", property), ExponentialInterpolation{stops.base}, stops.stops, defaultValue);
    }
    
    template <typename T>
    static std::unique_ptr<TypedExpression> toExpression(const std::string& property,
                                                  const IntervalStops<T>& stops,
                                                  optional<T> defaultValue)
    {
        return makeCurve(makeGet("number", property), StepInterpolation(), stops.stops, defaultValue);
    }
    
    template <typename T>
    static std::unique_ptr<TypedExpression> toExpression(const std::string& property,
                                                  const CategoricalStops<T>& stops,
                                                  optional<T> defaultValue)
    {
        assert(stops.stops.size() > 0);

        const auto& firstKey = stops.stops.begin()->first;
        auto expr = firstKey.match(
            [&](bool) {
                auto input = makeGet("boolean", property);
                return makeCase(std::move(input), stops);
            },
            [&](const std::string&) {
                auto input = makeGet("string", property);
                return makeMatch<std::string>(std::move(input), stops);
            },
            [&](int64_t) {
                auto input = makeGet("number", property);
                return makeMatch<int64_t>(std::move(input), stops);
            }

        );
        
        std::vector<CompileError> errors;
        auto checked = makeCoalesceToDefault(std::move(expr), defaultValue)->typecheck(errors);
        assert(checked);
        return std::move(*checked);
    }
    
    template <typename T>
    static std::unique_ptr<TypedExpression> toExpression(const std::string& property,
                                                  const IdentityStops<T>&,
                                                  optional<T> defaultValue)
    {
        auto input = valueTypeToExpressionType<T>().match(
            [&] (const type::StringType&) {
                return makeGet("string", property);
            },
            [&] (const type::NumberType&) {
                return makeGet("number", property);
            },
            [&] (const type::BooleanType&) {
                return makeGet("boolean", property);
            },
            [&] (const type::Array& arr) {
                UntypedCompoundExpression::Args getArgs;
                getArgs.push_back(makeLiteral(property));
                auto get = std::make_unique<UntypedCompoundExpression>("", "get", std::move(getArgs));
                return std::make_unique<UntypedArrayAssertion>("", arr.itemType, arr.N, std::move(get));
            },
            [&] (const auto&) -> std::unique_ptr<UntypedExpression> {
                return makeLiteral(Null);
            }
        );
        
        std::vector<CompileError> errors;
        auto checked = makeCoalesceToDefault(std::move(input), defaultValue)->typecheck(errors);
        assert(checked);
        return std::move(*checked);
    }
};

} // namespace expression
} // namespace style
} // namespace mbgl
