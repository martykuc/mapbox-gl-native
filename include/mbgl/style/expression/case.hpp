#pragma once

#include <mbgl/style/expression/expression.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/conversion.hpp>

namespace mbgl {
namespace style {
namespace expression {

class TypedCase : public TypedExpression {
public:
    using Case = std::pair<std::unique_ptr<TypedExpression>, std::unique_ptr<TypedExpression>>;
    
    TypedCase(std::vector<Case> cases_,
              std::unique_ptr<TypedExpression> otherwise_
    ) : TypedExpression(otherwise_->getType()),
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
            if (!pair.first->isFeatureConstant() || !pair.second->isZoomConstant()) {
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

private:
    std::unique_ptr<TypedExpression> input;
    std::vector<Case> cases;
    std::unique_ptr<TypedExpression> otherwise;
};

class UntypedCase : public UntypedExpression {
public:
    using Cases = std::vector<std::pair<std::unique_ptr<UntypedExpression>, std::unique_ptr<UntypedExpression>>>;
    
    UntypedCase(std::string key,
                 Cases cases_,
                 std::unique_ptr<UntypedExpression> otherwise_)
    :   UntypedExpression(key),
        cases(std::move(cases_)),
        otherwise(std::move(otherwise_))
    {}
    
    template <class V>
    static ParseResult parse(const V& value, const ParsingContext& ctx) {
        using namespace mbgl::style::conversion;
        
        assert(isArray(value));
        auto length = arrayLength(value);
        if (length < 4) {
            return CompileError {
                "Expected at least 3 arguments, but found only " + std::to_string(length - 1) + ".",
                ctx.key()
            };
        }
        
        // Expect even-length array: ["case", 2 * (n pairs)..., otherwise]
        if (length % 2 != 0) {
            return CompileError {
                "Missing final output value for \"case\" expression.",
                ctx.key()
            };
        }
        
        auto otherwise = parseExpression(arrayMember(value, length - 1), ParsingContext(ctx, {length - 1}, {"case"}));
        if (otherwise.template is<CompileError>()) {
            return otherwise;
        }
        
        Cases cases;
        for (size_t i = 1; i + 1 < length; i += 2) {
            auto condition = parseExpression(arrayMember(value, i), ParsingContext(ctx, {i}, {"case"}));
            if (condition.template is<CompileError>()) {
                return condition.template get<CompileError>();
            }

            auto output = parseExpression(arrayMember(value, i + 1), ParsingContext(ctx, {i + 1}, {"case"}));
            if (output.template is<CompileError>()) {
                return output.template get<CompileError>();
            }

            cases.push_back(std::make_pair(
                std::move(condition.template get<std::unique_ptr<UntypedExpression>>()),
                std::move(output.template get<std::unique_ptr<UntypedExpression>>()))
            );
        }
        
        return std::make_unique<UntypedCase>(ctx.key(),
                                             std::move(cases),
                                             std::move(otherwise.template get<std::unique_ptr<UntypedExpression>>()));
    }
    
    TypecheckResult typecheck(std::vector<CompileError>& errors) const override {
        auto checkedOtherwise = otherwise->typecheck(errors);
        if (!checkedOtherwise) {
            return TypecheckResult();
        }

        optional<type::Type> outputType;
        std::vector<TypedCase::Case> checkedCases;
        for (const auto& pair : cases) {
            auto condition = pair.first->typecheck(errors);
            if (!condition) continue;
            auto error = matchType(type::Boolean, (*condition)->getType());
            if (error) {
                errors.push_back({ *error, pair.first->getKey() });
            }

            auto output = pair.second->typecheck(errors);
            if (!output) continue;
            if (!outputType) {
                outputType = (*output)->getType();
            } else {
                error = matchType(*outputType, (*output)->getType());
                if (error) {
                    errors.push_back({ *error, pair.second->getKey() });
                }
            }
            
            if (errors.size() == 0) {
                checkedCases.emplace_back(std::move(*condition), std::move(*output));
            }
        }
        
        auto error = matchType(*outputType, (*checkedOtherwise)->getType());
        if (error) {
            errors.push_back({ *error, otherwise->getKey() });
        }
        
        if (errors.size() > 0) {
            return TypecheckResult();
        }

        return TypecheckResult(std::make_unique<TypedCase>(std::move(checkedCases), std::move(*checkedOtherwise)));
    };
    
private:
    Cases cases;
    std::unique_ptr<UntypedExpression> otherwise;
};

} // namespace expression
} // namespace style
} // namespace mbgl
