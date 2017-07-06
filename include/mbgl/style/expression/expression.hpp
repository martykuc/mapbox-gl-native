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

class Expression {
public:
    Expression(std::string key_, type::Type type_) : key(key_), type(type_) {}
    virtual ~Expression() {};
    
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
    std::string getKey() const { return key; }
    
    virtual bool isFeatureConstant() const { return true; }
    virtual bool isZoomConstant() const { return true; }
    
protected:
    void setType(type::Type newType) { type = newType; }
    
private:
    std::string key;
    type::Type type;
};


using ParseResult = variant<CompileError, std::unique_ptr<Expression>>;
template <class V>
ParseResult parseExpression(const V& value, const ParsingContext& context);

using TypecheckResult = variant<std::vector<CompileError>, std::unique_ptr<Expression>>;

using namespace mbgl::style::conversion;

class LiteralExpression : public Expression {
public:
    LiteralExpression(std::string key_, Value value_) : Expression(key_, typeOf(value_)), value(value_) {}
    
    Value getValue() const { return value; }

    EvaluationResult evaluate(const EvaluationParameters&) const override {
        return value;
    }
    
    template <class V>
    static ParseResult parse(const V& value, const ParsingContext& ctx) {
        const Value& parsedValue = parseValue(value);
        return std::make_unique<LiteralExpression>(ctx.key(), parsedValue);
    }
    
private:
    template <class V>
    static Value parseValue(const V& value) {
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

struct NArgs {
    std::vector<type::Type> types;
    optional<std::size_t> N;
};

class LambdaExpression : public Expression {
public:
    using Params = std::vector<variant<type::Type, NArgs>>;
    using Args = std::vector<std::unique_ptr<Expression>>;

    LambdaExpression(std::string key_,
                    std::string name_,
                    Args args_,
                    type::Type type_,
                    std::vector<Params> signatures_) :
        Expression(key_, type_),
        args(std::move(args_)),
        signatures(signatures_),
        name(name_)
    {}
    
    std::string getName() const { return name; }
    
    virtual bool isFeatureConstant() const override {
        bool isFC = true;
        for (const auto& arg : args) {
            isFC = isFC && arg->isFeatureConstant();
        }
        return isFC;
    }
    
    virtual bool isZoomConstant() const override {
        bool isZC = true;
        for (const auto& arg : args) {
            isZC = isZC && arg->isZoomConstant();
        }
        return isZC;
    }
    
    virtual std::unique_ptr<Expression> applyInferredType(const type::Type& type, Args args) const = 0;
    
    friend TypecheckResult typecheck(const type::Type& expected, const std::unique_ptr<Expression>& e);

    template <class Expr, class V>
    static ParseResult parse(const V& value, const ParsingContext& ctx) {
        assert(isArray(value));
        auto length = arrayLength(value);
        const std::string& name = *toString(arrayMember(value, 0));
        Args args;
        for(size_t i = 1; i < length; i++) {
            const auto& arg = arrayMember(value, i);
            auto parsedArg = parseExpression(arg, ParsingContext(ctx, i, {}));
            if (parsedArg.template is<std::unique_ptr<Expression>>()) {
                args.push_back(std::move(parsedArg.template get<std::unique_ptr<Expression>>()));
            } else {
                return parsedArg.template get<CompileError>();
            }
        }
        return std::make_unique<Expr>(ctx.key(), name, std::move(args));
    }
    
protected:
    Args args;
private:
    std::vector<Params> signatures;
    std::string name;
};

template<class Expr>
class LambdaBase : public LambdaExpression {
public:
    LambdaBase(const std::string& key, const std::string& name, Args args) :
        LambdaExpression(key, name, std::move(args), Expr::type(), Expr::signatures())
    {}
    LambdaBase(const std::string& key, const std::string& name, const type::Type& type, Args args) :
        LambdaExpression(key, name, std::move(args), type, Expr::signatures())
    {}

    std::unique_ptr<Expression> applyInferredType(const type::Type& type, Args args) const override {
        return std::make_unique<Expr>(getKey(), getName(), type, std::move(args));
    }
};


} // namespace expression
} // namespace style
} // namespace mbgl
