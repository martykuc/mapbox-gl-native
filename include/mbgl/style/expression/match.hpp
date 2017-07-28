#pragma once

#include <mbgl/style/expression/expression.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/conversion.hpp>

namespace mbgl {
namespace style {
namespace expression {

using InputType = variant<int64_t, std::string>;
using MatchKey = variant<InputType, std::vector<InputType>>;

template <typename T>
class TypedMatch : public TypedExpression {
public:
    TypedMatch(std::unique_ptr<TypedExpression> input_,
               std::vector<std::pair<MatchKey, std::unique_ptr<TypedExpression>>> cases_,
               std::unique_ptr<TypedExpression> otherwise_
    ) : TypedExpression(otherwise_->getType()),
        input(std::move(input_)),
        otherwise(std::move(otherwise_))
    {
        for(auto& pair : cases_) {
            pair.first.match(
                [&](const InputType& key) {
                    cases.emplace(key.get<T>(), std::move(pair.second));
                },
                [&](const std::vector<InputType>& keys) {
                    std::shared_ptr<TypedExpression> output = std::move(pair.second);
                    for (const auto& key : keys) {
                        cases.emplace(key.get<T>(), output);
                    }
                }
            );
        }
    }
    
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
    
    EvaluationResult evaluate(const EvaluationParameters& params) const override {
        const auto& inputValue = evaluateInput(params);
        if (!inputValue) {
            return inputValue.error();
        }
        if (cases.find(*inputValue) == cases.end()) {
            return otherwise->evaluate(params);
        }
        return cases.at(*inputValue)->evaluate(params);
    }

private:
    Result<T> evaluateInput(const EvaluationParameters& params) const;

    std::unique_ptr<TypedExpression> input;
    std::unordered_map<T, std::shared_ptr<TypedExpression>> cases;
    std::unique_ptr<TypedExpression> otherwise;
};

class UntypedMatch : public UntypedExpression {
public:
    using Cases = std::vector<std::pair<MatchKey, std::unique_ptr<UntypedExpression>>>;
    
    UntypedMatch(std::string key,
                 std::unique_ptr<UntypedExpression> input_,
                 Cases cases_,
                 std::unique_ptr<UntypedExpression> otherwise_,
                 const type::Type& inputType_) :
        UntypedExpression(key),
        input(std::move(input_)),
        cases(std::move(cases_)),
        otherwise(std::move(otherwise_)),
        inputType(inputType_)
    {}
    
    template <class V>
    static ParseResult parse(const V& value, const ParsingContext& ctx) {
        using namespace mbgl::style::conversion;
        
        assert(isArray(value));
        auto length = arrayLength(value);
        if (length < 3) {
            return CompileError {
                "Expected at least 2 arguments, but found only " + std::to_string(length - 1) + ".",
                ctx.key()
            };
        }
        
        // Expect odd-length array: ["match", input, 2 * (n pairs)..., otherwise]
        if (length % 2 != 1) {
            return CompileError {
                "Missing final output value for \"match\" expression.",
                ctx.key()
            };
        }
        
        auto parsedInput = parseExpression(arrayMember(value, 1), ParsingContext(ctx, {1}, {"match"}));
        if (parsedInput.template is<CompileError>()) {
            return parsedInput;
        }
        
        auto otherwise = parseExpression(arrayMember(value, length - 1), ParsingContext(ctx, {length - 1}, {"match"}));
        if (otherwise.template is<CompileError>()) {
            return otherwise;
        }
        
        Cases cases;
        optional<type::Type> inputType;
        for (size_t i = 2; i + 1 < length; i += 2) {
            auto output = parseExpression(arrayMember(value, i + 1), ParsingContext(ctx, {i + 1}, {"match"}));
            if (output.template is<CompileError>()) {
                return output.template get<CompileError>();
            }

            const auto& inputArg = arrayMember(value, i);
            // Match pair inputs are provided as either a literal value or a
            // raw JSON array of string / number / boolean values.
            if (isArray(inputArg)) {
                auto groupLength = arrayLength(inputArg);
                if (groupLength == 0) return CompileError {
                    "Expected at least one input value.",
                    ctx.key(i)
                };
                std::vector<InputType> inputGroup;
                for (size_t j = 0; j < groupLength; j++) {
                    const auto& inputValue = parseInputValue(arrayMember(inputArg, j), ctx.key(i), inputType);
                    if (inputValue.template is<CompileError>()) {
                        return inputValue.template get<CompileError>();
                    }
                    inputGroup.emplace_back(inputValue.template get<InputType>());
                }
                cases.push_back(std::make_pair(
                    inputGroup,
                    std::move(output.template get<std::unique_ptr<UntypedExpression>>()))
                );
            } else {
                const auto& inputValue = parseInputValue(inputArg, ctx.key(i), inputType);
                if (inputValue.template is<CompileError>()) {
                    return inputValue.template get<CompileError>();
                }
                cases.push_back(std::make_pair(
                    inputValue.template get<InputType>(),
                    std::move(output.template get<std::unique_ptr<UntypedExpression>>()))
                );
            }
        }
        
        return std::make_unique<UntypedMatch>(ctx.key(),
                                              std::move(parsedInput.template get<std::unique_ptr<UntypedExpression>>()),
                                              std::move(cases),
                                              std::move(otherwise.template get<std::unique_ptr<UntypedExpression>>()),
                                              *inputType);
    }
    
    template <typename V>
    static variant<CompileError, InputType> parseInputValue(const V& input, const std::string& key, optional<type::Type>& inputType) {
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
            return CompileError {
                "Match inputs must be either literal integer or string values or arrays of integer or string values.",
                key
            };
        }
        
        if (!inputType) {
            inputType = type;
        } else {
            const auto& expected = toString(*inputType);
            const auto& actual = toString(*type);
            // TODO: replace with real == check
            if (expected != actual) {
                return CompileError {
                    "Expected " + expected + " but found " + actual + " instead.",
                    key
                };
            }
        }
        
        return *result;
    }
    
    TypecheckResult typecheck(std::vector<CompileError>& errors) const override {
        auto checkedInput = input->typecheck(errors);
        if (!checkedInput) {
            return TypecheckResult();
        }
        auto checkedOtherwise = otherwise->typecheck(errors);
        if (!checkedOtherwise) {
            return TypecheckResult();
        }

        auto error = matchType(inputType, (*checkedInput)->getType());
        if (error) {
            errors.push_back({ *error, input->getKey() });
        }
        
        optional<type::Type> outputType;
        std::vector<std::pair<MatchKey, std::unique_ptr<TypedExpression>>> checkedCases;
        for (const auto& pair : cases) {
            auto checkedOutput = pair.second->typecheck(errors);
            if (!checkedOutput) continue;
            if (!outputType) {
                outputType = (*checkedOutput)->getType();
            } else {
                error = matchType(*outputType, (*checkedOutput)->getType());
                if (error) {
                    errors.push_back({ *error, pair.second->getKey() });
                    continue;
                }
            }
            checkedCases.emplace_back(pair.first, std::move(*checkedOutput));
        }
        
        error = matchType(*outputType, (*checkedOtherwise)->getType());
        if (error) {
            errors.push_back({ *error, otherwise->getKey() });
        }
        
        if (inputType.is<type::StringType>()) {
            return TypecheckResult(std::make_unique<TypedMatch<std::string>>(
                std::move(*checkedInput), std::move(checkedCases), std::move(*checkedOtherwise)
            ));
        } else if (inputType.is<type::NumberType>()) {
            return TypecheckResult(std::make_unique<TypedMatch<int64_t>>(
                std::move(*checkedInput), std::move(checkedCases), std::move(*checkedOtherwise)
            ));
        }
        
        assert(false);
        return TypecheckResult();
    }
    
private:
    static bool isIntegerValue(const mbgl::Value& v) {
        return v.match(
            [] (uint64_t) { return true; },
            [] (int64_t) { return true; },
            [] (double t) { return t == ceilf(t); },
            [] (const auto&) { return false; }
        );
    }

    std::unique_ptr<UntypedExpression> input;
    Cases cases;
    std::unique_ptr<UntypedExpression> otherwise;
    type::Type inputType;
};

} // namespace expression
} // namespace style
} // namespace mbgl
