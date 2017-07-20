#pragma once

#include <map>
#include <mbgl/util/interpolate.hpp>
#include <mbgl/style/expression/expression.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/conversion.hpp>

namespace mbgl {
namespace style {
namespace expression {

class TypedCoalesce : public TypedExpression {
public:
    using Args = std::vector<std::unique_ptr<TypedExpression>>;
    TypedCoalesce(const type::Type& type, Args args_) :
        TypedExpression(type),
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
    
private:
    Args args;
};

class UntypedCoalesce : public UntypedExpression {
public:
    using Args = std::vector<std::unique_ptr<UntypedExpression>>;
    
    UntypedCoalesce(const std::string& key, Args args_) :
        UntypedExpression(key),
        args(std::move(args_))
    {}
    
    template <typename V>
    static ParseResult parse(const V& value, const ParsingContext& ctx) {
        using namespace mbgl::style::conversion;
        assert(isArray(value));
        auto length = arrayLength(value);
        if (length < 2) {
            return CompileError {
                "Expected at least one argument to \"coalesce\".",
                ctx.key()
            };
        }
        
        Args args;
        for (std::size_t i = 1; i < length; i++) {
            auto parsed = parseExpression(arrayMember(value, i), ParsingContext(ctx, {i}, {"coalesce"}));
            if (parsed.template is<CompileError>()) {
                return parsed;
            }
            args.push_back(std::move(parsed.template get<std::unique_ptr<UntypedExpression>>()));
        }
        return std::make_unique<UntypedCoalesce>(ctx.key(), std::move(args));
    }
    
    TypecheckResult typecheck(std::vector<CompileError>& errors) const override {
        optional<type::Type> outputType;
        TypedCoalesce::Args checkedArgs;
        
        for (const auto& arg : args) {
            auto checked = arg->typecheck(errors);
            if (!checked) {
                continue;
            }
            if (!outputType) {
                outputType = (*checked)->getType();
            } else {
                const auto& error = matchType(*outputType, (*checked)->getType());
                if (error) {
                    errors.push_back({ *error, arg->getKey() });
                    continue;
                }
            }
            
            checkedArgs.push_back(std::move(*checked));
        }
        
        if (errors.size() > 0) return TypecheckResult();
        
        return TypecheckResult(std::make_unique<TypedCoalesce>(*outputType, std::move(checkedArgs)));
    }

private:
    Args args;
};

} // namespace expression
} // namespace style
} // namespace mbgl
