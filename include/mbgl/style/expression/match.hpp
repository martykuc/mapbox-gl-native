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

} // namespace expression
} // namespace style
} // namespace mbgl
