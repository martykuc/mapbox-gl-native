#pragma once

#include <map>
#include <mbgl/util/interpolate.hpp>
#include <mbgl/util/range.hpp>
#include <mbgl/style/expression/expression.hpp>
#include <mbgl/style/expression/compound_expression.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/conversion.hpp>

namespace mbgl {
namespace style {
namespace expression {

template <class T>
class ExponentialInterpolator {
public:
    ExponentialInterpolator(float base_) : base(base_) {}

    float base;
    
    float interpolationFactor(const Range<float>& inputLevels, const float& input) const {
        return util::interpolationFactor(base, inputLevels, input);
    }
    
    EvaluationResult interpolate(const Range<Value>& outputs, const float& t) const {
        optional<T> lower = fromExpressionValue<T>(outputs.min);
        if (!lower) {
            // TODO - refactor fromExpressionValue to return EvaluationResult<T> so as to
            // consolidate DRY up producing this error message.
            return EvaluationError {
                "Expected value to be of type " + toString(valueTypeToExpressionType<T>()) +
                ", but found " + toString(typeOf(outputs.min)) + " instead."
            };
        }
        const auto& upper = fromExpressionValue<T>(outputs.max);
        if (!upper) {
            return EvaluationError {
                "Expected value to be of type " + toString(valueTypeToExpressionType<T>()) +
                ", but found " + toString(typeOf(outputs.min)) + " instead."
            };
        }
        T result = util::interpolate(*lower, *upper, t);
        return toExpressionValue(result);
    }
};

class StepInterpolator {
public:
    float interpolationFactor(const Range<float>&, const float&) const {
        return 0;
    }
    float interpolate(const Range<Value>&, const float&) const {
        // Assume that Curve::evaluate() will always short circuit due to
        // interpolationFactor always returning 0.
        assert(false);
        return 0.0f;
    }
};

namespace detail {

// used for storing intermediate state during parsing
struct ExponentialInterpolation { float base; std::string name = "exponential"; };
struct StepInterpolation {};

} // namespace detail


template <typename InterpolatorT>
class Curve : public Expression {
public:
    using Interpolator = InterpolatorT;
    
    Curve(const type::Type& type,
          Interpolator interpolator_,
          std::unique_ptr<Expression> input_,
          std::map<float, std::unique_ptr<Expression>> stops_
    ) : Expression(type),
        interpolator(std::move(interpolator_)),
        input(std::move(input_)),
        stops(std::move(stops_))
    {}
    
    EvaluationResult evaluate(const EvaluationParameters& params) const override {
        const auto& x = input->evaluate<float>(params);
        if (!x) { return x.error(); }
        
        if (stops.empty()) {
            return EvaluationError { "No stops in exponential curve." };
        }

        auto it = stops.upper_bound(*x);
        if (it == stops.end()) {
            return stops.rbegin()->second->evaluate(params);
        } else if (it == stops.begin()) {
            return stops.begin()->second->evaluate(params);
        } else {
            float t = interpolator.interpolationFactor({ std::prev(it)->first, it->first }, *x);
            if (t == 0.0f) {
                return std::prev(it)->second->evaluate(params);
            }
            if (t == 1.0f) {
                return it->second->evaluate(params);
            }
            
            EvaluationResult lower = std::prev(it)->second->evaluate(params);
            if (!lower) {
                return lower.error();
            }
            EvaluationResult upper = it->second->evaluate(params);
            if (!upper) {
                return upper.error();
            }

            return interpolator.interpolate({*lower, *upper}, t);
        }
        
    }
    
    bool isFeatureConstant() const override {
        if (!input->isFeatureConstant()) { return false; }
        for (const auto& stop : stops) {
            if (!stop.second->isFeatureConstant()) { return false; }
        }
        return true;
    }

    bool isZoomConstant() const override {
        if (!input->isZoomConstant()) { return false; }
        for (const auto& stop : stops) {
            if (!stop.second->isZoomConstant()) { return false; }
        }
        return true;
    }
    
    bool isZoomCurve() const {
        if (CompoundExpressionBase* z = dynamic_cast<CompoundExpressionBase*>(input.get())) {
            return z->getName() == "zoom";
        }
        return false;
    }
    
    Interpolator getInterpolator() const {
        return interpolator;
    }
    
private:
    Interpolator interpolator;
    std::unique_ptr<Expression> input;
    std::map<float, std::unique_ptr<Expression>> stops;
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
                ctx.error(R"(Input/output pairs for "curve" expressions must be defined using literal numeric values (not computed expressions) for the input values.)");
                return ParseResult();
            }
            
            if (*label < previous) {
                ctx.error(
                    R"(Input/output pairs for "curve" expressions must be arranged with input values in strictly ascending order.)"
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
                *interpName + " curve's output type.");
            return ParseResult();
        }
        
        return interpolation.match(
            [&](const detail::StepInterpolation&) -> ParseResult {
                return ParseResult(std::make_unique<Curve<StepInterpolator>>(
                    *outputType,
                    StepInterpolator(),
                    std::move(*input),
                    std::move(stops)
                ));
            },
            [&](const detail::ExponentialInterpolation& exponentialInterpolation) -> ParseResult {
                const float base = exponentialInterpolation.base;
                return outputType->match(
                    [&](const type::NumberType&) -> ParseResult {
                        return ParseResult(std::make_unique<Curve<ExponentialInterpolator<float>>>(
                            *outputType,
                            ExponentialInterpolator<float>(base),
                            std::move(*input),
                            std::move(stops)
                        ));
                    },
                    [&](const type::ColorType&) -> ParseResult {
                        return ParseResult(std::make_unique<Curve<ExponentialInterpolator<mbgl::Color>>>(
                            *outputType,
                            ExponentialInterpolator<mbgl::Color>(base),
                            std::move(*input),
                            std::move(stops)
                        ));
                    },
                    [&](const type::Array& arrayType) -> ParseResult {
                        if (arrayType.itemType == type::Number && arrayType.N) {
                            return ParseResult(std::make_unique<Curve<ExponentialInterpolator<std::vector<Value>>>>(
                                *outputType,
                                ExponentialInterpolator<std::vector<Value>>(base),
                                std::move(*input),
                                std::move(stops)
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
