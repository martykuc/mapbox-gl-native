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

class Literal : public Expression {
public:
    Literal(Value value_) : Expression(typeOf(value_)), value(value_) {}
    Literal(type::Array type_, std::vector<Value> value_) : Expression(type_), value(value_) {}
    EvaluationResult evaluate(const EvaluationParameters&) const override {
        return value;
    }
    bool isFeatureConstant() const override { return true; }
    bool isZoomConstant() const override { return true; }
    
    template <class V>
    static ParseResult parse(const V& value, ParsingContext ctx) {
        const Value& parsedValue = parseValue(value);
        
        // special case: infer the item type if possible for zero-length arrays
        if (
            ctx.expected &&
            ctx.expected->template is<type::Array>() &&
            parsedValue.template is<std::vector<Value>>()
        ) {
            auto type = typeOf(parsedValue).template get<type::Array>();
            auto expected = ctx.expected->template get<type::Array>();
            if (
                type.N && (*type.N == 0) &&
                (!expected.N || (*expected.N == 0))
            ) {
                return ParseResult(std::make_unique<Literal>(expected, parsedValue.template get<std::vector<Value>>()));
            }
        }
        return ParseResult(std::make_unique<Literal>(parsedValue));
    }
    
private:
    Value value;
};

} // namespace expression
} // namespace style
} // namespace mbgl
