#pragma once

#include <string>
#include <vector>
#include <mbgl/util/optional.hpp>
#include <mbgl/style/expression/type.hpp>

namespace mbgl {
namespace style {
namespace expression {

struct ParsingError {
    std::string message;
    std::string key;
};

class ParsingContext {
public:
    ParsingContext(std::vector<ParsingError>& errors_, optional<type::Type> expected_ = {})
        : errors(errors_),
          expected(std::move(expected_))
    {}
    
    ParsingContext(const ParsingContext previous, std::size_t index_, optional<type::Type> expected_ = {})
        : key(previous.key + "[" + std::to_string(index_) + "]"),
          errors(previous.errors),
          expected(std::move(expected_))
    {}

    void error(std::string message) {
        errors.push_back({message, key});
    }
    
    void  error(std::string message, std::size_t child) {
        errors.push_back({message, key + "[" + std::to_string(child) + "]"});
    }

    std::string key;
    std::vector<ParsingError>& errors;
    optional<type::Type> expected;
};

} // namespace expression
} // namespace style
} // namespace mbgl
