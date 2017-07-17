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
    float zoom;
    const GeometryTileFeature& feature;
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

using EvaluationResult = Result<Value>;

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
      Evaluate this expression to a particular value type T. (See expression/value.hpp for
      possible types T.)
    */
    template <typename T>
    Result<T> evaluate(const EvaluationParameters& params) {
        const auto& result = evaluate(params);
        if (!result) { return result.error(); }
        return result->match(
            [&] (const T& v) -> variant<EvaluationError, T> { return v; },
            [&] (const auto& v) -> variant<EvaluationError, T> {
                return EvaluationError {
                    "Expected value to be of type " + toString(valueTypeToExpressionType<T>()) +
                    ", but found " + toString(typeOf(v)) + " instead."
                };
            }
        );
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

class TypedLiteral : public TypedExpression {
public:
    TypedLiteral(Value value_) : TypedExpression(typeOf(value_)), value(value_) {}
    EvaluationResult evaluate(const EvaluationParameters&) const override {
        return value;
    }
private:
    Value value;
};

class UntypedLiteral : public UntypedExpression {
public:
    UntypedLiteral(std::string key_, Value value_) : UntypedExpression(key_), value(value_) {}

    TypecheckResult typecheck(std::vector<CompileError>&) const override {
        return {std::make_unique<TypedLiteral>(value)};
    }

    template <class V>
    static ParseResult parse(const V& value, const ParsingContext& ctx) {
        const Value& parsedValue = parseValue(value);
        return std::make_unique<UntypedLiteral>(ctx.key(), parsedValue);
    }

private:
    template <class V>
    static Value parseValue(const V& value) {
        using namespace mbgl::style::conversion;
        if (isUndefined(value)) return Null;
        if (isObject(value)) {
            std::unordered_map<std::string, Value> result;
            eachMember(value, [&] (const std::string& k, const V& v) -> optional<conversion::Error> {
                result.emplace(k, parseValue(v));
                return {};
            });
            return result;
        }
        if (isArray(value)) {
            std::vector<Value> result;
            const auto length = arrayLength(value);
            for(std::size_t i = 0; i < length; i++) {
                result.emplace_back(parseValue(arrayMember(value, i)));
            }
            return result;
        }
        
        optional<mbgl::Value> v = toValue(value);
        assert(v);
        return convertValue(*v);
    }
    
    Value value;
};

} // namespace expression
} // namespace style
} // namespace mbgl
