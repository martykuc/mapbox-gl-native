#pragma once

#include <map>
#include <mbgl/util/interpolate.hpp>
#include <mbgl/style/expression/expression.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/conversion.hpp>

namespace mbgl {
namespace style {
namespace expression {

template <class T>
class ExponentialCurve {
public:
    using Stops = std::map<float, std::unique_ptr<Expression>>;

    ExponentialCurve(Stops stops_, float base_)
        : stops(std::move(stops_)),
          base(base_)
    {}

    Stops stops;
    float base;

    EvaluationResult evaluate(float x, const EvaluationParameters& parameters) const {
        if (stops.empty()) {
            return EvaluationError { "No stops in exponential curve." };
        }

        auto it = stops.upper_bound(x);
        if (it == stops.end()) {
            return stops.rbegin()->second->evaluate(parameters);
        } else if (it == stops.begin()) {
            return stops.begin()->second->evaluate(parameters);
        } else {
            const auto& lower = std::prev(it)->second->template evaluate<T>(parameters);
            if (!lower) { return lower.error(); }
            const auto& upper = it->second->template evaluate<T>(parameters);
            if (!upper) { return upper.error(); }
            T result = util::interpolate(*lower, *upper,
                util::interpolationFactor(base, { std::prev(it)->first, it->first }, x));
            return toExpressionValue(result);
        }
    }
};

class StepCurve {
public:
    using Stops = std::map<float, std::unique_ptr<Expression>>;
    StepCurve(Stops stops_) : stops(std::move(stops_)) {}
    
    Stops stops;

    EvaluationResult evaluate(float x, const EvaluationParameters& parameters) const {
        if (stops.empty()) {
            return EvaluationError { "No stops in exponential curve." };
        }
        
        auto it = stops.upper_bound(x);
        if (it == stops.end()) {
            return stops.rbegin()->second->evaluate(parameters);
        } else if (it == stops.begin()) {
            return stops.begin()->second->evaluate(parameters);
        } else {
            return std::prev(it)->second->evaluate(parameters);
        }
    }
};

namespace detail {

// used for storing intermediate state during parsing
struct ExponentialInterpolation { float base; std::string name = "exponential"; };
struct StepInterpolation {};

}

template <typename CurveType>
class Curve : public Expression {
public:
    Curve(const type::Type& type, std::unique_ptr<Expression> input_, CurveType curve_) :
        Expression(type),
        input(std::move(input_)),
        curve(std::move(curve_))
    {}
    
    EvaluationResult evaluate(const EvaluationParameters& params) const override {
        const auto& x = input->evaluate<float>(params);
        if (!x) { return x.error(); }
        return curve.evaluate(*x, params);
    }
    
    bool isFeatureConstant() const override {
        if (!input->isFeatureConstant()) { return false; }
        for (const auto& stop : curve.stops) {
            if (!stop.second->isFeatureConstant()) { return false; }
        }
        return true;
    }

    bool isZoomConstant() const override {
        if (!input->isZoomConstant()) { return false; }
        for (const auto& stop : curve.stops) {
            if (!stop.second->isZoomConstant()) { return false; }
        }
        return true;
    }
    
private:
    std::unique_ptr<Expression> input;
    CurveType curve;
};

struct ParseCurve {
    template <typename V>
    static ParseResult parse(const V& value, ParsingContext ctx) {
        using namespace mbgl::style::conversion;
        assert(isArray(value));
        auto length = arrayLength(value);
        if (length < 5) {
            ctx.error("Expected at least 4 arguments, but found only " + std::to_string(length - 1) + ".");
            return ParseResult();
        }
        
        // [curve, interp, input, 2 * (n pairs)...]
        if (length % 2 != 1) {
            ctx.error("Expected an even number of arguments.");
            return ParseResult();
        }
        
        const auto& interp = arrayMember(value, 1);
        if (!isArray(interp) || arrayLength(interp) == 0) {
            ctx.error("Expected an interpolation type expression.");
            return ParseResult();
        }

        variant<detail::StepInterpolation,
                detail::ExponentialInterpolation> interpolation;
        
        const auto& interpName = toString(arrayMember(interp, 0));
        if (interpName && *interpName == "step") {
            interpolation = detail::StepInterpolation{};
        } else if (interpName && *interpName == "linear") {
            interpolation = detail::ExponentialInterpolation { 1.0f, "linear" };
        } else if (interpName && *interpName == "exponential") {
            optional<double> base;
            if (arrayLength(interp) == 2) {
                base = toDouble(arrayMember(interp, 1));
            }
            if (!base) {
                ctx.error("Exponential interpolation requires a numeric base.");
                return ParseResult();
            }
            interpolation = detail::ExponentialInterpolation { static_cast<float>(*base) };
        } else {
            ctx.error("Unknown interpolation type " + (interpName ? *interpName : ""));
            return ParseResult();
        }
        
        ParseResult input = parseExpression(arrayMember(value, 2), ParsingContext(ctx, 2, {type::Number}));
        if (!input) {
            return input;
        }
        
        std::map<float, std::unique_ptr<Expression>> stops;
        optional<type::Type> outputType = ctx.expected;
        
        double previous = - std::numeric_limits<double>::infinity();
        for (std::size_t i = 3; i + 1 < length; i += 2) {
            const auto& label = toDouble(arrayMember(value, i));
            if (!label) {
                ctx.error("Input/output pairs for \"curve\" expressions must be defined using literal numeric values (not computed expressions) for the input values.");
                return ParseResult();
            }
            
            if (*label < previous) {
                ctx.error(
                    "Input/output pairs for \"curve\" expressions must be arranged with input values in strictly ascending order."
                );
                return ParseResult();
            }
            previous = *label;
            
            auto output = parseExpression(arrayMember(value, i + 1), ParsingContext(ctx, i + 1, outputType));
            if (!output) {
                return ParseResult();
            }
            if (!outputType) {
                outputType = (*output)->getType();
            }

            stops.emplace(*label, std::move(*output));
        }
        
        assert(outputType);
        
        if (
            !interpolation.template is<detail::StepInterpolation>() &&
            *outputType != type::Number &&
            *outputType != type::Color &&
            !(
                outputType->is<type::Array>() &&
                outputType->get<type::Array>().itemType == type::Number
            )
        )
        {
            ctx.error("Type " + toString(*outputType) +
                " is not interpolatable, and thus cannot be used as a " +
                *interpName + " curve's output type");
            return ParseResult();
        }
        
        return interpolation.match(
            [&](const detail::StepInterpolation&) -> ParseResult {
                return ParseResult(std::make_unique<Curve<StepCurve>>(
                    *outputType,
                    std::move(*input),
                    StepCurve(std::move(stops))
                ));
            },
            [&](const detail::ExponentialInterpolation& exponentialInterpolation) -> ParseResult {
                const float base = exponentialInterpolation.base;
                return outputType->match(
                    [&](const type::NumberType&) -> ParseResult {
                        return ParseResult(std::make_unique<Curve<ExponentialCurve<float>>>(
                            *outputType,
                            std::move(*input),
                            ExponentialCurve<float>(std::move(stops), base)
                        ));
                    },
                    [&](const type::ColorType&) -> ParseResult {
                        return ParseResult(std::make_unique<Curve<ExponentialCurve<mbgl::Color>>>(
                            *outputType,
                            std::move(*input),
                            ExponentialCurve<mbgl::Color>(std::move(stops), base)
                        ));
                    },
                    [&](const type::Array& arrayType) -> ParseResult {
                        if (arrayType.itemType == type::Number && arrayType.N) {
                            return ParseResult(std::make_unique<Curve<ExponentialCurve<std::vector<float>>>>(
                                *outputType,
                                std::move(*input),
                                ExponentialCurve<std::vector<float>>(std::move(stops), base)
                            ));
                        } else {
                            assert(false); // interpolability already checked above.
                            return ParseResult();
                        }
                    },
                    [&](const auto&) {
                        assert(false); // interpolability already checked above.
                        return ParseResult();
                    }
                );
            }
        );
    }
};

} // namespace expression
} // namespace style
} // namespace mbgl
