#include <mbgl/style/expression/match.hpp>

namespace mbgl {
namespace style {
namespace expression {

template<> EvaluationResult Match<std::string>::evaluate(const EvaluationParameters& params) const {
    const Result<std::string>& inputValue = input->evaluate<std::string>(params);
    if (!inputValue) {
        return inputValue.error();
    }

    auto it = cases.find(*inputValue);
    if (it != cases.end()) {
        return (*it).second->evaluate(params);
    }

    return otherwise->evaluate(params);
}

template<> EvaluationResult Match<int64_t>::evaluate(const EvaluationParameters& params) const {
    const auto& inputValue = input->evaluate<float>(params);
    if (!inputValue) {
        return inputValue.error();
    }
    
    int64_t rounded = ceilf(*inputValue);
    if (*inputValue == rounded) {
        auto it = cases.find(rounded);
        if (it != cases.end()) {
            return (*it).second->evaluate(params);
        }
    }
    
    return otherwise->evaluate(params);
}

} // namespace expression
} // namespace style
} // namespace mbgl
