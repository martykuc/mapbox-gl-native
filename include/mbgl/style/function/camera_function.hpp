#pragma once

#include <mbgl/style/function/convert.hpp>
#include <mbgl/style/function/exponential_stops.hpp>
#include <mbgl/style/function/interval_stops.hpp>
#include <mbgl/util/interpolate.hpp>
#include <mbgl/util/variant.hpp>

namespace mbgl {
namespace style {

template <class T>
class CameraFunction {
public:
    using Stops = std::conditional_t<
        util::Interpolatable<T>::value,
        variant<
            ExponentialStops<T>,
            IntervalStops<T>>,
        variant<
            IntervalStops<T>>>;

    CameraFunction(Stops stops_)
        : stops(std::move(stops_)),
          expression(stops.match([&] (const auto& s) {
            return expression::Convert::toExpression(s);
          }))
    {}

    T evaluate(float zoom) const {
        auto result = expression->evaluate<T>(expression::EvaluationParameters { zoom });
        if (!result) return T();
        return *result;
    }
    
    friend bool operator==(const CameraFunction& lhs,
                           const CameraFunction& rhs) {
        return lhs.stops == rhs.stops;
    }

    bool useIntegerZoom = false;
    
    // retained for compatibility with pre-expression function API
    Stops stops;

private:
    std::shared_ptr<expression::TypedExpression> expression;
};

} // namespace style
} // namespace mbgl
