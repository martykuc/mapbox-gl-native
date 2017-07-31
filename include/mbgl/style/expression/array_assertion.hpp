#pragma once

#include <vector>
#include <memory>
#include <mbgl/util/optional.hpp>
#include <mbgl/util/variant.hpp>
#include <mbgl/style/expression/expression.hpp>
#include <mbgl/style/expression/type.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/conversion.hpp>


namespace mbgl {
namespace style {
namespace expression {


class TypedArrayAssertion : public TypedExpression  {
public:
    TypedArrayAssertion(type::Array type, std::unique_ptr<TypedExpression> input_) :
        TypedExpression(type),
        input(std::move(input_))
    {}
    
    bool isFeatureConstant() const override {
        return input->isFeatureConstant();
    }
    
    bool isZoomConstant() const override {
        return input->isZoomConstant();
    }
    
    EvaluationResult evaluate(const EvaluationParameters& params) const override {
        auto result = input->evaluate(params);
        if (!result) {
            return result.error();
        }
        auto error = matchType(getType(), typeOf(*result));
        if (error) {
            return EvaluationError { *error };
        }
        return *result;
    }
private:
    std::unique_ptr<TypedExpression> input;
};

class UntypedArrayAssertion : public UntypedExpression {
public:
    UntypedArrayAssertion(std::string key_,
                          type::Type itemType_,
                          optional<std::size_t> length_,
                          std::unique_ptr<UntypedExpression> input_
    ) : UntypedExpression(key_),
        itemType(itemType_),
        length(length_),
        input(std::move(input_))
    {}

    TypecheckResult typecheck(std::vector<CompileError>& errors) const override {
        auto checkedInput = input->typecheck(errors);
        if (!checkedInput) {
            return TypecheckResult();
        }
        return TypecheckResult(std::make_unique<TypedArrayAssertion>(
            type::Array(itemType, length), std::move(*checkedInput))
        );
    }
    
    template <class V>
    static ParseResult parse(const V& value, const ParsingContext& ctx) {
        using namespace mbgl::style::conversion;
        
        static std::unordered_map<std::string, type::Type> itemTypes {
            {"string", type::String},
            {"number", type::Number},
            {"boolean", type::Boolean}
        };
    
        auto length = arrayLength(value);
        if (length < 2 || length > 4) {
            return CompileError {
                "Expected 1, 2, or 3 arguments, but found " + std::to_string(length - 1) + " instead.",
                ctx.key()
            };
        }
        
        auto input = parseExpression(arrayMember(value, length - 1), ParsingContext(ctx, {length - 1}, {"array"}));
        if (input.template is<CompileError>()) {
            return input;
        }

        optional<type::Type> itemType;
        optional<std::size_t> N;
        if (length > 2) {
            auto type = toString(arrayMember(value, 2));
            if (!type || itemTypes.find(*type) == itemTypes.end()) {
                return CompileError {
                    "The item type argument of \"array\" must be one of string, number, boolean",
                    ctx.key(2)
                };
            }
            itemType = itemTypes.at(*type);
        } else {
            itemType = {type::Value};
        }
        
        if (length > 3) {
            auto n = toNumber(arrayMember(value, 3));
            if (!n || *n != ceilf(*n)) {
                return CompileError {
                    "The length argument to \"array\" must be a positive integer literal.",
                    ctx.key(3)
                };
            }
            N = optional<std::size_t>(*n);
        }
    
        return std::make_unique<UntypedArrayAssertion>(
            ctx.key(),
            *itemType,
            N,
            std::move(input.template get<std::unique_ptr<UntypedExpression>>())
        );
    }

private:
    
    type::Type itemType;
    optional<std::size_t> length;
    std::unique_ptr<UntypedExpression> input;
};

} // namespace expression
} // namespace style
} // namespace mbgl
