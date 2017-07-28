#include <mbgl/style/expression/match.hpp>

namespace mbgl {
namespace style {
namespace expression {

template<> Result<std::string> TypedMatch<std::string>::evaluateInput(const EvaluationParameters& params) const {
    const auto& inputValue = input->evaluate<std::string>(params);
    if (!inputValue) {
        return inputValue.error();
    }
    return *inputValue;
}

template<> Result<int64_t> TypedMatch<int64_t>::evaluateInput(const EvaluationParameters& params) const {
    const auto& inputValue = input->evaluate<float>(params);
    if (!inputValue) {
        return inputValue.error();
    }
    int64_t rounded = ceilf(*inputValue);
    if (*inputValue == rounded) {
        return rounded;
    } else {
        return EvaluationError {
            "Input to \"match\" must be an integer value; found " +
            std::to_string(*inputValue) + " instead ."
        };
    }
}

} // namespace expression
} // namespace style
} // namespace mbgl
