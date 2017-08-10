#pragma once

#include <mbgl/style/expression/check_subtype.hpp>
#include <mbgl/style/expression/expression.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/conversion.hpp>

namespace mbgl {
namespace style {
namespace expression {

class Case : public Expression {
public:
    using Branch = std::pair<std::unique_ptr<Expression>, std::unique_ptr<Expression>>;
    
    Case(type::Type type, std::vector<Branch> cases_, std::unique_ptr<Expression> otherwise_
    ) : Expression(type),
        cases(std::move(cases_)),
        otherwise(std::move(otherwise_))
    {}
    
    bool isFeatureConstant() const override {
        if (!otherwise->isFeatureConstant()) { return false; }
        for (const auto& pair : cases) {
            if (!pair.first->isFeatureConstant() || !pair.second->isFeatureConstant()) {
                return false;
            }
        }
        return true;
    }

    bool isZoomConstant() const override {
        if (!otherwise->isZoomConstant()) { return false; }
        for (const auto& pair : cases) {
            if (!pair.first->isZoomConstant() || !pair.second->isZoomConstant()) {
                return false;
            }
        }
        return true;
    }
    
    EvaluationResult evaluate(const EvaluationParameters& params) const override {
        for (const auto& casePair : cases) {
            const auto& condition = casePair.first->template evaluate<bool>(params);
            if (!condition) {
                return condition.error();
            }
            if (*condition) {
                return casePair.second->evaluate(params);
            }
        }
        
        return otherwise->evaluate(params);
    }
    
        
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

        std::vector<Branch> branches;
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

private:
    std::unique_ptr<Expression> input;
    std::vector<Branch> cases;
    std::unique_ptr<Expression> otherwise;
};

} // namespace expression
} // namespace style
} // namespace mbgl
