#pragma once

#include <map>
#include <mbgl/util/interpolate.hpp>
#include <mbgl/util/range.hpp>
#include <mbgl/style/expression/expression.hpp>
#include <mbgl/style/expression/compound_expression.hpp>

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
    EvaluationResult interpolate(const Range<Value>&, const float&) const {
        // Assume that Curve::evaluate() will always short circuit due to
        // interpolationFactor always returning 0.
        assert(false);
        return Null;
    }
};

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

} // namespace expression
} // namespace style
} // namespace mbgl
