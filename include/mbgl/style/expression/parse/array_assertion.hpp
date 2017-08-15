#pragma once

#include <mbgl/style/conversion.hpp>
#include <mbgl/style/expression/array_assertion.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/expression/type.hpp>
#include <mbgl/util/optional.hpp>

namespace mbgl {
namespace style {
namespace expression {

struct ParseArrayAssertion {
    template <class V>
    static ParseResult parse(const V& value, ParsingContext ctx) {
        using namespace mbgl::style::conversion;
        
        static std::unordered_map<std::string, type::Type> itemTypes {
            {"string", type::String},
            {"number", type::Number},
            {"boolean", type::Boolean}
        };
    
        auto length = arrayLength(value);
        if (length < 2 || length > 4) {
            ctx.error("Expected 1, 2, or 3 arguments, but found " + std::to_string(length - 1) + " instead.");
            return ParseResult();
        }
        
        optional<type::Type> itemType;
        optional<std::size_t> N;
        if (length > 2) {
            optional<std::string> itemTypeName = toString(arrayMember(value, 1));
            auto it = itemTypeName ? itemTypes.find(*itemTypeName) : itemTypes.end();
            if (it == itemTypes.end()) {
                ctx.error(
                    R"(The item type argument of "array" must be one of string, number, boolean)",
                    1
                );
                return ParseResult();
            }
            itemType = it->second;
        } else {
            itemType = {type::Value};
        }
        
        if (length > 3) {
            auto n = toNumber(arrayMember(value, 2));
            if (!n || *n != ceilf(*n)) {
                ctx.error(
                    R"(The length argument to "array" must be a positive integer literal.)",
                    2
                );
                return ParseResult();
            }
            N = optional<std::size_t>(*n);
        }
        
        auto input = parseExpression(arrayMember(value, length - 1), ParsingContext(ctx, length - 1, {type::Value}));
        if (!input) {
            return input;
        }

        return ParseResult(std::make_unique<ArrayAssertion>(
            type::Array(*itemType, N),
            std::move(*input)
        ));
    }
};

} // namespace expression
} // namespace style
} // namespace mbgl
