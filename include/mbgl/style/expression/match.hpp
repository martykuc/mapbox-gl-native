#pragma once

#include <mbgl/style/expression/check_subtype.hpp>
#include <mbgl/style/expression/expression.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/conversion.hpp>

namespace mbgl {
namespace style {
namespace expression {

using InputType = variant<int64_t, std::string>;

template <typename T>
class Match : public Expression {
public:
    using Cases = std::unordered_map<T, std::shared_ptr<Expression>>;

    Match(type::Type type,
          std::unique_ptr<Expression> input_,
          Cases cases_,
          std::unique_ptr<Expression> otherwise_
    ) : Expression(type),
        input(std::move(input_)),
        cases(std::move(cases_)),
        otherwise(std::move(otherwise_))
    {}
    
    bool isFeatureConstant() const override {
        if (!input->isFeatureConstant()) { return false; }
        if (!otherwise->isFeatureConstant()) { return false; }
        for (const auto& pair : cases) {
            if (!pair.second->isFeatureConstant()) { return false; }
        }
        return true;
    }

    bool isZoomConstant() const override {
        if (!input->isZoomConstant()) { return false; }
        if (!otherwise->isZoomConstant()) { return false; }
        for (const auto& pair : cases) {
            if (!pair.second->isZoomConstant()) { return false; }
        }
        return true;
    }
    
    EvaluationResult evaluate(const EvaluationParameters& params) const override;
    
private:
    
    std::unique_ptr<Expression> input;
    Cases cases;
    std::unique_ptr<Expression> otherwise;
};

struct ParseMatch {
    template <class V>
    static ParseResult parse(const V& value, ParsingContext ctx) {
        using namespace mbgl::style::conversion;
        
        assert(isArray(value));
        auto length = arrayLength(value);
        if (length < 5) {
            ctx.error(
                "Expected at least 4 arguments, but found only " + std::to_string(length - 1) + "."
            );
            return ParseResult();
        }
        
        // Expect odd-length array: ["match", input, 2 * (n pairs)..., otherwise]
        if (length % 2 != 1) {
            ctx.error("Expected an even number of arguments.");
            return ParseResult();
        }
        
        optional<type::Type> inputType;
        optional<type::Type> outputType = ctx.expected;
        std::vector<std::pair<std::vector<InputType>,
                              std::unique_ptr<Expression>>> cases;
        
        for (size_t i = 2; i + 1 < length; i += 2) {
            const auto& label = arrayMember(value, i);

            ParsingContext labelContext(ctx, i);
            std::vector<InputType> labels;
            // Match pair inputs are provided as either a literal value or a
            // raw JSON array of string / number / boolean values.
            if (isArray(label)) {
                auto groupLength = arrayLength(label);
                if (groupLength == 0) {
                    labelContext.error("Expected at least one branch label.");
                    return ParseResult();
                }
                
                for (size_t j = 0; j < groupLength; j++) {
                    const optional<InputType>& inputValue = parseInputValue(arrayMember(label, j), ParsingContext(ctx, i), inputType);
                    if (!inputValue) {
                        return ParseResult();
                    }
                    labels.push_back(*inputValue);
                }
            } else {
                const optional<InputType>& inputValue = parseInputValue(label, ParsingContext(ctx, i), inputType);
                if (!inputValue) {
                    return ParseResult();
                }
                labels.push_back(*inputValue);
            }
            
            ParseResult output = parseExpression(arrayMember(value, i + 1), ParsingContext(ctx, i + 1, outputType));
            if (!output) {
                return ParseResult();
            }
            
            if (!outputType) {
                outputType = (*output)->getType();
            }
            
            cases.push_back(std::make_pair(std::move(labels), std::move(*output)));
        }
        
        auto input = parseExpression(arrayMember(value, 1), ParsingContext(ctx, 1, inputType));
        if (!input) {
            return ParseResult();
        }
        
        auto otherwise = parseExpression(arrayMember(value, length - 1), ParsingContext(ctx, length - 1, outputType));
        if (!otherwise) {
            return ParseResult();
        }
        
        assert(inputType && outputType);
        
        return inputType->match(
            [&](const type::NumberType&) {
                return create<int64_t>(*outputType, std::move(*input), std::move(cases), std::move(*otherwise), ctx);
            },
            [&](const type::StringType&) {
                return create<std::string>(*outputType, std::move(*input), std::move(cases), std::move(*otherwise), ctx);
            },
            [&](const auto&) {
                assert(false);
                return ParseResult();
            }
        );
    }

private:
    template <typename V>
    static optional<InputType> parseInputValue(const V& input, ParsingContext ctx, optional<type::Type>& inputType) {
        using namespace mbgl::style::conversion;
        optional<InputType> result;
        optional<type::Type> type;
        
        auto value = toValue(input);
        if (value && isIntegerValue(*value)) {
            type = {type::Number};
            result = {*numericValue<int64_t>(*value)};
        } else if (value && value->template is<std::string>()) {
            type = {type::String};
            result = {value->template get<std::string>()};
        } else {
            ctx.error("Branch labels must be numbers or strings.");
            return optional<InputType>();
        }
        
        if (!inputType) {
            inputType = type;
        } else if (checkSubtype(*inputType, *type, ctx)) {
            return optional<InputType>();
        }
        
        return result;
    }
    
    template <typename T>
    static ParseResult create(type::Type outputType,
                              std::unique_ptr<Expression>input,
                              std::vector<std::pair<std::vector<InputType>,
                                                    std::unique_ptr<Expression>>> cases,
                              std::unique_ptr<Expression> otherwise,
                              ParsingContext ctx) {
        typename Match<T>::Cases typedCases;
        
        std::size_t index = 2;
        for (std::pair<std::vector<InputType>,
                       std::unique_ptr<Expression>>& pair : cases) {
            std::shared_ptr<Expression> result = std::move(pair.second);
            for (const InputType& label : pair.first) {
                const T& typedLabel = label.template get<T>();
                if (typedCases.find(typedLabel) != typedCases.end()) {
                    ctx.error("Branch labels must be unique.", index);
                    return ParseResult();
                }
                typedCases.emplace(typedLabel, result);
            }
            
            index += 2;
        }
        return ParseResult(std::make_unique<Match<T>>(
            outputType,
            std::move(input),
            std::move(typedCases),
            std::move(otherwise)
        ));
    }

    static bool isIntegerValue(const mbgl::Value& v) {
        return v.match(
            [] (uint64_t) { return true; },
            [] (int64_t) { return true; },
            [] (double t) { return t == ceilf(t); },
            [] (const auto&) { return false; }
        );
    }
};

} // namespace expression
} // namespace style
} // namespace mbgl
