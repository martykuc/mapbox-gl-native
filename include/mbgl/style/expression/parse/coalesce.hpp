#pragma once

#include <vector>
#include <memory>
#include <mbgl/style/expression/coalesce.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/expression/type.hpp>
#include <mbgl/style/conversion.hpp>

namespace mbgl {
namespace style {
namespace expression {

struct ParseCoalesce {
    template <typename V>
    static ParseResult parse(const V& value, ParsingContext ctx) {
        using namespace mbgl::style::conversion;
        assert(isArray(value));
        auto length = arrayLength(value);
        if (length < 2) {
            ctx.error("Expected at least one argument.");
            return ParseResult();
        }
        
        Coalesce::Args args;
        optional<type::Type> outputType = ctx.expected;
        for (std::size_t i = 1; i < length; i++) {
            auto parsed = parseExpression(arrayMember(value, i), ParsingContext(ctx, i, outputType));
            if (!parsed) {
                return parsed;
            }
            if (!outputType) {
                outputType = (*parsed)->getType();
            }
            args.push_back(std::move(*parsed));
        }
        
        assert(outputType);
        return ParseResult(std::make_unique<Coalesce>(*outputType, std::move(args)));
    }
};

} // namespace expression
} // namespace style
} // namespace mbgl
