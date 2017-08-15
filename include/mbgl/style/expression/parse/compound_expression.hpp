#pragma once

#include <array>
#include <vector>
#include <memory>
#include <mbgl/style/expression/compound_expression.hpp>
#include <mbgl/style/expression/type.hpp>
#include <mbgl/style/expression/value.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/conversion.hpp>

namespace mbgl {
namespace style {
namespace expression {

struct ParseCompoundExpression {
    template <class V>
    static ParseResult parse(const V& value, ParsingContext ctx) {
        using namespace mbgl::style::conversion;
        assert(isArray(value) && arrayLength(value) > 0);
        const auto& name = toString(arrayMember(value, 0));
        assert(name);
        
        auto it = CompoundExpressions::definitions.find(*name);
        if (it == CompoundExpressions::definitions.end()) {
            ctx.error(
                 R"(Unknown expression ")" + *name + R"(". If you wanted a literal array, use ["literal", [...]].)",
                0
            );
            return ParseResult();
        }
        const CompoundExpressions::Definition& definition = it->second;

        // parse subexpressions first
        std::vector<std::unique_ptr<Expression>> args;
        auto length = arrayLength(value);
        for (std::size_t i = 1; i < length; i++) {
            auto parsed = parseExpression(arrayMember(value, i), ParsingContext(ctx, i));
            if (!parsed) {
                return parsed;
            }
            args.push_back(std::move(*parsed));
        }
        return CompoundExpressions::create(*name, definition, std::move(args), ctx);
    }
};

} // namespace expression
} // namespace style
} // namespace mbgl
