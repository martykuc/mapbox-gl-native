#pragma once

#include <map>
#include <memory>
#include <mbgl/util/optional.hpp>
#include <mbgl/style/expression/curve.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/conversion.hpp>

namespace mbgl {
namespace style {
namespace expression {

namespace detail {

// used for storing intermediate state during parsing
struct ExponentialInterpolation { float base; std::string name = "exponential"; };
struct StepInterpolation {};

} // namespace detail

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
            const optional<mbgl::Value>& labelValue = toValue(arrayMember(value, i));
            optional<double> label;
            optional<std::string> labelError;
            if (labelValue) {
                labelValue->match(
                    [&](uint64_t n) {
                        if (!Value::isSafeNumericValue(n)) {
                            labelError = {"Numeric values must be no larger than " + std::to_string(Value::max()) + "."};
                        } else {
                            label = {static_cast<double>(n)};
                        }
                    },
                    [&](int64_t n) {
                        if (!Value::isSafeNumericValue(n)) {
                            labelError = {"Numeric values must be no larger than " + std::to_string(Value::max()) + "."};
                        } else {
                            label = {static_cast<double>(n)};
                        }
                    },
                    [&](double n) {
                        if (!Value::isSafeNumericValue(n)) {
                            labelError = {"Numeric values must be no larger than " + std::to_string(Value::max()) + "."};
                        } else {
                            label = {static_cast<double>(n)};
                        }
                    },
                    [&](const auto&) {}
                );
            }
            if (!label) {
                ctx.error(labelError ? *labelError :
                    R"(Input/output pairs for "curve" expressions must be defined using literal numeric values (not computed expressions) for the input values.)",
                    i);
                return ParseResult();
            }
            
            if (*label < previous) {
                ctx.error(
                    R"(Input/output pairs for "curve" expressions must be arranged with input values in strictly ascending order.)",
                    i
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
