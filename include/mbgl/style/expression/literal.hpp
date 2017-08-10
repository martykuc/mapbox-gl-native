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
    Literal(type::Array type, std::vector<Value> value_) : Expression(type), value(value_) {}
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
