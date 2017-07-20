#pragma once

#include <map>
#include <mbgl/util/interpolate.hpp>
#include <mbgl/style/expression/expression.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/conversion.hpp>

namespace mbgl {

namespace util {

struct InterpolateExpressionValue {
    const double t;
    
    template <typename T, typename Enabled = void>
    optional<Value> operator()(const T&, const T&) const {
        return optional<Value>();
    };
    
    template <typename T, typename std::enable_if_t<Interpolatable<T>::value>>
    optional<Value> operator()(const T& a, const T& b) const {
        return util::interpolate(a, b, t);
    }
    
    template <typename T, typename U>
    optional<Value> operator()(const T&, const U&) const {
        return {};
    }
};

template<>
struct Interpolator<std::vector<Value>> {
    std::vector<Value> operator()(const std::vector<Value>& a, const std::vector<Value>& b, const double t) const {
        assert(a.size() == b.size());
        if (a.size() == 0) return {};
        std::vector<Value> result;
        InterpolateExpressionValue visitor {t};
        for (std::size_t i = 0; i < a.size(); i++) {
            const auto& v = Value::binary_visit(a[i], b[i], visitor);
            assert(v);
            result.push_back(*v);
        }
        return result;
    }
};

} // namespace util

namespace style {
namespace expression {

template <class T = void>
class ExponentialCurve {
public:
    using Stops = std::map<float, std::unique_ptr<TypedExpression>>;

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
            return util::interpolate(*lower, *upper,
                util::interpolationFactor(base, { std::prev(it)->first, it->first }, x));
        }
    }
};

class StepCurve {
public:
    using Stops = std::map<float, std::unique_ptr<TypedExpression>>;
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

template <typename Curve>
class TypedCurve : public TypedExpression {
public:
    TypedCurve(const type::Type& type, std::unique_ptr<TypedExpression> input_, Curve curve_) :
        TypedExpression(type),
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
    std::unique_ptr<TypedExpression> input;
    Curve curve;
};

struct ExponentialInterpolation { float base; std::string name = "exponential"; };
struct StepInterpolation {};

class UntypedCurve : public UntypedExpression {
public:
    using Stops = std::vector<std::pair<float, std::unique_ptr<UntypedExpression>>>;
    using Interpolation = variant<
        StepInterpolation,
        ExponentialInterpolation>;
    UntypedCurve(const std::string& key,
                 std::unique_ptr<UntypedExpression> input_,
                 Stops stops_,
                 Interpolation interpolation_
    ) : UntypedExpression(key),
        input(std::move(input_)),
        stops(std::move(stops_)),
        interpolation(interpolation_)
    {}
    
    template <typename V>
    static ParseResult parse(const V& value, const ParsingContext& ctx) {
        using namespace mbgl::style::conversion;
        assert(isArray(value));
        auto length = arrayLength(value);
        if (length < 4) {
            return CompileError {
                "Expected at least 3 arguments, but found only " + std::to_string(length - 1) + ".",
                ctx.key()
            };
        }
        
        // [curve, interp, input, 2 * (n pairs)...]
        if (length % 2 != 1) {
            return CompileError {
                "Missing final output value for \"curve\" expression.",
                ctx.key()
            };
        }
        
        const auto& interp = arrayMember(value, 1);
        if (!isArray(interp) || arrayLength(interp) == 0) {
            return CompileError {
                "Expected an interpolation type expression, e.g. [\"linear\"].",
                ctx.key(1)
            };
        }
        
        Interpolation interpolation;
        const auto& interpName = toString(arrayMember(interp, 0));
        if (interpName && *interpName == "step") {
            interpolation = StepInterpolation {};
        } else if (interpName && *interpName == "linear") {
            interpolation = ExponentialInterpolation { 1.0f, "linear" };
        } else if (interpName && *interpName == "exponential") {
            optional<double> base;
            if (arrayLength(interp) == 2) {
                base = toDouble(arrayMember(interp, 1));
            }
            if (!base) {
                return CompileError {
                    "Exponential interpolation requires a numeric base",
                    ctx.key(1)
                };
            }
            interpolation = ExponentialInterpolation { static_cast<float>(*base) };
        } else {
            std::string name = interpName ? *interpName : "";
            return CompileError {
                "Unknown interpolation type " + name,
                ctx.key(1)
            };
        }
        
        auto input = parseExpression(arrayMember(value, 2), ParsingContext(ctx, {2}, {"curve"}));
        if (input.template is<CompileError>()) {
            return input;
        }
        
        Stops stops;
        double previous = - std::numeric_limits<double>::infinity();
        for (std::size_t i = 3; i + 1 < length; i += 2) {
            const auto& inputValue = toDouble(arrayMember(value, i));
            if (!inputValue) {
                return CompileError {
                    "Input/output pairs for \"curve\" expressions must be defined using literal numeric values (not computed expressions) for the input values.",
                    ctx.key(i)
                };
            }
            if (*inputValue < previous) {
                return CompileError {
                    "Input/output pairs for \"curve\" expressions must be arranged with input values in strictly ascending order.",
                    ctx.key(i)
                };
            }
            previous = *inputValue;
            
            auto output = parseExpression(arrayMember(value, i + 1), ParsingContext(ctx, {i + 1}, {"curve"}));
            if (output.template is<CompileError>()) {
                return output;
            }
            stops.push_back(std::make_pair(
                *inputValue,
                std::move(output.template get<std::unique_ptr<UntypedExpression>>())
            ));
        }
        
        return std::make_unique<UntypedCurve>(ctx.key(),
                                              std::move(input.template get<std::unique_ptr<UntypedExpression>>()),
                                              std::move(stops),
                                              interpolation);
    }
    
    TypecheckResult typecheck(std::vector<CompileError>& errors) const override {
        auto checkedInput = input->typecheck(errors);
        if (!checkedInput) {
            return TypecheckResult();
        }
        auto error = matchType(type::Number, (*checkedInput)->getType());
        if (error) {
            errors.push_back({*error, input->getKey()});
        }
        
        optional<type::Type> outputType;
        std::map<float, std::unique_ptr<TypedExpression>> checkedStops;
        for (const auto& stop : stops) {
            auto checkedOutput = stop.second->typecheck(errors);
            if (!checkedOutput) {
                continue;
            }
            if (!outputType) {
                outputType = (*checkedOutput)->getType();
            } else {
                error = matchType(*outputType, (*checkedOutput)->getType());
                if (error) {
                    errors.push_back({ *error, stop.second->getKey() });
                    continue;
                }
            }
            checkedStops.emplace(stop.first, std::move(*checkedOutput));
        }
        
        if (stops.size() > 0) return TypecheckResult();
        
        return interpolation.match(
            [&](const StepInterpolation&) -> TypecheckResult {
                return {std::make_unique<TypedCurve<StepCurve>>(
                        *outputType,
                        std::move(*checkedInput),
                        StepCurve(std::move(checkedStops)))};
            },
            [&](const ExponentialInterpolation& interp) {
                TypecheckResult result = outputType->match(
                    [&](const type::NumberType&) -> TypecheckResult {
                        return makeExponential<float>(*outputType, std::move(*checkedInput), std::move(checkedStops), interp.base);
                    },
                    [&](const type::ColorType&) -> TypecheckResult {
                        return makeExponential<mbgl::Color>(*outputType, std::move(*checkedInput), std::move(checkedStops), interp.base);
                    },
                    [&](const type::Array& arrayType) -> TypecheckResult {
                        if (toString(arrayType.itemType) == type::Number.getName() && arrayType.N) {
                            return makeExponential<std::vector<Value>>(*outputType, std::move(*checkedInput), std::move(checkedStops), interp.base);
                        } else {
                            return TypecheckResult();
                        }
                    },
                    [&](const auto&) { return TypecheckResult(); }
                );
                if (!result) {
                    errors.push_back({"Type " + toString(*outputType) + " is not interpolatable, and thus cannot be used as an exponential curve's output type", stops.begin()->second->getKey() });
                }
                return result;
            }
        );
    }
    
    
private:
    template <typename T>
    std::unique_ptr<TypedExpression> makeExponential(const type::Type type, std::unique_ptr<TypedExpression> checkedInput, std::map<float, std::unique_ptr<TypedExpression>> checkedStops, float base) const {
        return std::make_unique<TypedCurve<ExponentialCurve<T>>>(
            type,
            std::move(checkedInput),
            ExponentialCurve<T>(std::move(checkedStops), base)
        );
    }

    std::unique_ptr<UntypedExpression> input;
    Stops stops;
    Interpolation interpolation;
};

} // namespace expression
} // namespace style
} // namespace mbgl
