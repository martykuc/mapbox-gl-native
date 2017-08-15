#pragma once

#include <mbgl/style/expression/expression.hpp>
#include <mbgl/style/expression/coalesce.hpp>
#include <mbgl/style/expression/curve.hpp>
#include <mbgl/style/function/convert.hpp>
#include <mbgl/style/function/composite_exponential_stops.hpp>
#include <mbgl/style/function/composite_interval_stops.hpp>
#include <mbgl/style/function/composite_categorical_stops.hpp>
#include <mbgl/util/interpolate.hpp>
#include <mbgl/util/range.hpp>
#include <mbgl/util/variant.hpp>

#include <string>
#include <tuple>

namespace mbgl {

class GeometryTileFeature;

namespace style {

// A CompositeFunction consists of an outer zoom function whose stop range values are
// "inner" source functions. It provides the GL Native implementation of
// "zoom-and-property" functions from the style spec.

template <class T>
class CompositeFunction {
public:
    using InnerStops = std::conditional_t<
        util::Interpolatable<T>::value,
        variant<
            ExponentialStops<T>,
            IntervalStops<T>,
            CategoricalStops<T>>,
        variant<
            IntervalStops<T>,
            CategoricalStops<T>>>;

    using Stops = std::conditional_t<
        util::Interpolatable<T>::value,
        variant<
            CompositeExponentialStops<T>,
            CompositeIntervalStops<T>,
            CompositeCategoricalStops<T>>,
        variant<
            CompositeIntervalStops<T>,
            CompositeCategoricalStops<T>>>;

    using Interpolator = expression::ExponentialInterpolator<T>;
    using Curve = expression::Curve<Interpolator>;

    CompositeFunction(std::unique_ptr<expression::Expression> expression_)
    :   expression(std::move(expression_)),
        interpolator([&]() -> Interpolator {
            optional<Curve*> zoomCurve = findZoomCurve(expression.get());
            assert(zoomCurve);
            return (*zoomCurve)->getInterpolator();
        }())
    {
        assert(!expression->isZoomConstant());
        assert(!expression->isFeatureConstant());
    }

    CompositeFunction(std::string property_, Stops stops_, optional<T> defaultValue_ = {})
    :   property(std::move(property_)),
        stops(std::move(stops_)),
        defaultValue(std::move(defaultValue_)),
        expression(stops.match([&] (const auto& s) {
            return expression::Convert::toExpression(property, s, defaultValue);
        })),
        interpolator([&]() -> Interpolator {
            optional<Curve*> zoomCurve = findZoomCurve(expression.get());
            assert(zoomCurve);
            return (*zoomCurve)->getInterpolator();
        }())
    {}

    // Return the range obtained by evaluating the function at each of the zoom levels in zoomRange
    template <class Feature>
    Range<T> evaluate(const Range<float>& zoomRange, const Feature& feature, T finalDefaultValue) {
        return Range<T> {
            evaluate(zoomRange.min, feature, finalDefaultValue),
            evaluate(zoomRange.max, feature, finalDefaultValue)
        };
    }

    template <class Feature>
    T evaluate(float zoom, const Feature& feature, T finalDefaultValue) const {
        auto result = expression->evaluate<T>(expression::EvaluationParameters { {zoom}, &feature });
        if (!result) {
            return finalDefaultValue;
        }
        return *result;
    }
    
    Interpolator getInterpolator() const {
        return interpolator;
    }

    friend bool operator==(const CompositeFunction& lhs,
                           const CompositeFunction& rhs) {
        return std::tie(lhs.property, lhs.stops, lhs.defaultValue)
            == std::tie(rhs.property, rhs.stops, rhs.defaultValue);
    }

    std::string property;
    Stops stops;
    optional<T> defaultValue;
    bool useIntegerZoom = false;

private:
    static optional<Curve*> findZoomCurve(expression::Expression* e) {
        if (auto curve = dynamic_cast<Curve*>(e)) {
            assert(curve->isZoomCurve());
            return {curve};
//        } else if (auto let = dynamic_cast<expression::Let*>(e)) {
//            return let->getUnsafeResultExpressionPointer();
        } else if (auto coalesce = dynamic_cast<expression::Coalesce*>(e)) {
            std::size_t length = coalesce->getLength();
            for (std::size_t i = 0; i < length; i++) {
                optional<Curve*> childCurve = findZoomCurve(coalesce->getChild(i));
                if (!childCurve) {
                    continue;
                } else {
                    return childCurve;
                }
            }
        }
        
        return optional<Curve*>();
    }

    std::shared_ptr<expression::Expression> expression;
    const Interpolator interpolator;
};

} // namespace style
} // namespace mbgl
