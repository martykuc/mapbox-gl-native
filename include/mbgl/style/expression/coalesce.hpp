#pragma once

#include <map>
#include <mbgl/util/interpolate.hpp>
#include <mbgl/style/expression/expression.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/conversion.hpp>

namespace mbgl {
namespace style {
namespace expression {

class Coalesce : public Expression {
public:
    using Args = std::vector<std::unique_ptr<Expression>>;
    Coalesce(const type::Type& type, Args args_) :
        Expression(type),
        args(std::move(args_))
    {}
    
    EvaluationResult evaluate(const EvaluationParameters& params) const override {
        for (auto it = args.begin(); it != args.end(); it++) {
            const auto& result = (*it)->evaluate(params);
            if (!result && (std::next(it) != args.end())) {
                continue;
            }
            return result;
        }
        
        return Null;
    }
    
    bool isFeatureConstant() const override {
        for (const auto& arg : args) {
            if (!arg->isFeatureConstant()) return false;
        }
        return true;
    }

    bool isZoomConstant() const override {
        for (const auto& arg : args) {
            if (!arg->isZoomConstant()) return false;
        }
        return true;
    }
    
    template <typename V>
    static ParseResult parse(const V& value, ParsingContext ctx) {
        using namespace mbgl::style::conversion;
        assert(isArray(value));
        auto length = arrayLength(value);
        if (length < 2) {
            ctx.error("Expected at least one argument.");
            return ParseResult();
        }
        
        Args args;
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
    
private:
    Args args;
};

} // namespace expression
} // namespace style
} // namespace mbgl
