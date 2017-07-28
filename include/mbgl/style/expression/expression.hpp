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
    optional<float> zoom;
    GeometryTileFeature const * feature = nullptr;
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
    
    EvaluationResult(const std::array<float, 4>& arr) :
        Result(std::vector<Value>(arr.begin(), arr.end()))
    {}
};

struct CompileError {
    std::string message;
    std::string key;
};

class TypedExpression {
public:
    TypedExpression(type::Type type_) : type(type_) {}
    virtual ~TypedExpression() {};
    
    virtual bool isFeatureConstant() const { return true; }
    virtual bool isZoomConstant() const { return true; }
    
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
    
    type::Type getType() const { return type; }
    
private:
    type::Type type;
};

using TypecheckResult = optional<std::unique_ptr<TypedExpression>>;

class UntypedExpression {
public:
    UntypedExpression(std::string key_) : key(key_) {}
    virtual ~UntypedExpression() {}
    
    std::string getKey() const { return key; }
    virtual TypecheckResult typecheck(std::vector<CompileError>& errors) const = 0;
private:
    std::string key;
};

using ParseResult = variant<CompileError, std::unique_ptr<UntypedExpression>>;
template <class V>
ParseResult parseExpression(const V& value, const ParsingContext& context);


} // namespace expression
} // namespace style
} // namespace mbgl
