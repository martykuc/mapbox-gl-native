#pragma once

#include <memory>
#include <mbgl/style/conversion.hpp>
#include <mbgl/style/expression/array_assertion.hpp>
#include <mbgl/style/expression/case.hpp>
#include <mbgl/style/expression/check_subtype.hpp>
#include <mbgl/style/expression/coalesce.hpp>
#include <mbgl/style/expression/compound_expression.hpp>
#include <mbgl/style/expression/curve.hpp>
#include <mbgl/style/expression/expression.hpp>
#include <mbgl/style/expression/literal.hpp>
#include <mbgl/style/expression/match.hpp>
#include <mbgl/style/expression/parsing_context.hpp>

namespace mbgl {
namespace style {
namespace expression {

using namespace mbgl::style;

template <class V>
std::string getJSType(const V& value) {
    using namespace mbgl::style::conversion;
    if (isUndefined(value)) {
        return "undefined";
    }
    if (isArray(value) || isObject(value)) {
        return "object";
    }
    optional<mbgl::Value> v = conversion::toValue(value);
    assert(v);
    return v->match(
        [&] (std::string) { return "string"; },
        [&] (bool) { return "boolean"; },
        [&] (auto) { return "number"; }
    );
}

template <class V>
ParseResult parseExpression(const V& value, ParsingContext context)
{
    using namespace mbgl::style::conversion;
    
    ParseResult parsed;
    
    if (isArray(value)) {
        const std::size_t length = arrayLength(value);
        if (length == 0) {
            context.error(R"(Expected an array with at least one element. If you wanted a literal array, use ["literal", []].)");
            return ParseResult();
        }
        
        const optional<std::string>& op = toString(arrayMember(value, 0));
        if (!op) {
            context.error(
                "Expression name must be a string, but found " + getJSType(arrayMember(value, 0)) +
                    R"( instead. If you wanted a literal array, use ["literal", [...]].)",
                0
            );
            return ParseResult();
        }
        
        if (*op == "literal") {
            if (length != 2) {
                context.error(
                    "'literal' expression requires exactly one argument, but found " + std::to_string(length - 1) + " instead."
                );
                return ParseResult();
            }
            
            parsed = Literal::parse(arrayMember(value, 1), ParsingContext(context, 1, context.expected));
        } else if (*op == "match") {
            parsed = ParseMatch::parse(value, context);
        } else if (*op == "curve") {
            parsed = ParseCurve::parse(value, context);
        } else if (*op == "coalesce") {
            parsed = Coalesce::parse(value, context);
        } else if (*op == "case") {
            parsed = Case::parse(value, context);
        } else if (*op == "array") {
            parsed = ArrayAssertion::parse(value, context);
        } else {
            parsed = CompoundExpressions::parse(value, context);
        }
    } else {
        if (isObject(value)) {
            context.error(R"(Bare objects invalid. Use ["literal", {...}] instead.)");
            return ParseResult();
        }
        
        parsed = Literal::parse(value, context);
    }
    
    if (!parsed) {
        assert(context.errors.size() > 0);
    } else if (context.expected) {
        checkSubtype(*(context.expected), (*parsed)->getType(), context);
        if (context.errors.size() > 0) {
            return ParseResult();
        }
    }
    
    return parsed;
}


} // namespace expression
} // namespace style
} // namespace mbgl
