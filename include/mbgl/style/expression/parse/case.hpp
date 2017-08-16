#pragma once

#include <mbgl/style/conversion.hpp>
#include <mbgl/style/expression/case.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/expression/type.hpp>

namespace mbgl {
namespace style {
namespace expression {

struct ParseCase {
    template <class V>
    static ParseResult parse(const V& value, ParsingContext ctx) {
        using namespace mbgl::style::conversion;
        
        assert(isArray(value));
        auto length = arrayLength(value);
        if (length < 4) {
            ctx.error("Expected at least 3 arguments, but found only " + std::to_string(length - 1) + ".");
            return ParseResult();
        }
        
        // Expect even-length array: ["case", 2 * (n pairs)..., otherwise]
        if (length % 2 != 0) {
            ctx.error("Expected an odd number of arguments");
            return ParseResult();
        }
        
        optional<type::Type> outputType = ctx.expected;

        std::vector<Case::Branch> branches;
        for (size_t i = 1; i + 1 < length; i += 2) {
            auto test = parseExpression(arrayMember(value, i), ParsingContext(ctx, i, {type::Boolean}));
            if (!test) {
                return test;
            }

            auto output = parseExpression(arrayMember(value, i + 1), ParsingContext(ctx, i + 1, outputType));
            if (!output) {
                return output;
            }
            
            if (!outputType) {
                outputType = (*output)->getType();
            }

            branches.push_back(std::make_pair(std::move(*test), std::move(*output)));
        }
        
        assert(outputType);
        
        auto otherwise = parseExpression(arrayMember(value, length - 1), ParsingContext(ctx, length - 1, outputType));
        if (!otherwise) {
            return otherwise;
        }
        
        return ParseResult(std::make_unique<Case>(*outputType,
                                      std::move(branches),
                                      std::move(*otherwise)));
    }
};

} // namespace expression
} // namespace style
} // namespace mbgl
