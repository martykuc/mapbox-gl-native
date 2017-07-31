#pragma once

#include <vector>
#include <memory>
#include <mbgl/util/optional.hpp>
#include <mbgl/util/variant.hpp>
#include <mbgl/util/color.hpp>
#include <mbgl/style/expression/expression.hpp>
#include <mbgl/style/expression/type.hpp>
#include <mbgl/style/expression/value.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/conversion.hpp>


namespace mbgl {
namespace style {
namespace expression {

class TypedLiteral : public TypedExpression {
public:
    TypedLiteral(Value value_) : TypedExpression(typeOf(value_)), value(value_) {}
    EvaluationResult evaluate(const EvaluationParameters&) const override {
        return value;
    }
    bool isFeatureConstant() const override { return true; }
    bool isZoomConstant() const override { return true; }
private:
    Value value;
};

class UntypedLiteral : public UntypedExpression {
public:
    UntypedLiteral(std::string key_, Value value_) : UntypedExpression(key_), value(value_) {}

    TypecheckResult typecheck(std::vector<CompileError>&) const override {
        return {std::make_unique<TypedLiteral>(value)};
    }
    
    Value getValue() const { return value; }

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

        return Value(toExpressionValue(*v));
    }
    
    Value value;
};


} // namespace expression
} // namespace style
} // namespace mbgl
