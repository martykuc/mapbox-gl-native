#pragma once

#include <array>
#include <vector>
#include <memory>
#include <mbgl/util/optional.hpp>
#include <mbgl/util/variant.hpp>
#include <mbgl/util/color.hpp>
#include <mbgl/style/expression/type.hpp>
#include <mbgl/style/expression/value.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/conversion.hpp>


namespace mbgl {

class GeometryTileFeature;

namespace style {
namespace expression {

struct EvaluationError {
    std::string message;
};

struct EvaluationParameters {
    EvaluationParameters(float zoom_) : zoom(zoom_), feature(nullptr) {}
    EvaluationParameters(GeometryTileFeature const * feature_) : zoom(optional<float>()), feature(feature_) {}
    EvaluationParameters(float zoom_, GeometryTileFeature const * feature_) :
        zoom(zoom_), feature(feature_)
    {}
    
    optional<float> zoom;
    GeometryTileFeature const * feature;
};

template<typename T>
class Result : private variant<EvaluationError, T> {
public:
    using variant<EvaluationError, T>::variant;
    using Value = T;
    
    explicit operator bool () const {
        return this->template is<T>();
    }
    
    // optional does some type trait magic for this one, so this might
    // be problematic as is.
    const T* operator->() const {
        assert(this->template is<T>());
        return std::addressof(this->template get<T>());
    }
    
    T* operator->() {
        assert(this->template is<T>());
        return std::addressof(this->template get<T>());
    }
    
    T& operator*() {
        assert(this->template is<T>());
        return this->template get<T>();
    }
    
    const T& operator*() const {
        assert(this->template is<T>());
        return this->template get<T>();
    }
    
    const EvaluationError& error() const {
        assert(this->template is<EvaluationError>());
        return this->template get<EvaluationError>();
    }
};

struct EvaluationResult : public Result<Value> {
    using Result::Result;
    
    EvaluationResult(const std::array<double, 4>& arr) :
        Result(toExpressionValue(arr))
    {}
};

class Expression {
public:
    Expression(type::Type type_) : type(std::move(type_)) {}
    virtual ~Expression() = default;
    
    virtual bool isFeatureConstant() const = 0;
    virtual bool isZoomConstant() const = 0;
    
    virtual EvaluationResult evaluate(const EvaluationParameters& params) const = 0;
    
    /*
      Evaluate this expression to a particular type T. 
      (See expression/value.xpp for possible types T.)
    */
    template <typename T>
    Result<T> evaluate(const EvaluationParameters& params) {
        const auto& result = evaluate(params);
        if (!result) { return result.error(); }
        optional<T> converted = fromExpressionValue<T>(*result);
        if (converted) {
            return *converted;
        } else {
            return EvaluationError {
                "Expected value to be of type " + toString(valueTypeToExpressionType<T>()) +
                ", but found " + toString(typeOf(*result)) + " instead."
            };
        }
    }
    
    EvaluationResult evaluate(float z, const Feature& feature) const;
    
    type::Type getType() const { return type; };
    
private:
    type::Type type;
};

using ParseResult = optional<std::unique_ptr<Expression>>;


} // namespace expression
} // namespace style
} // namespace mbgl
