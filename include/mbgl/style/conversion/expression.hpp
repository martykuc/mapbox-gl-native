#pragma once

#include <memory>
#include <mbgl/style/expression/parse.hpp>
#include <mbgl/style/expression/type.hpp>
#include <mbgl/style/conversion.hpp>

namespace mbgl {
namespace style {
namespace conversion {

using namespace mbgl::style::expression;

template<> struct Converter<std::unique_ptr<Expression>> {
    template <class V>
    optional<std::unique_ptr<Expression>> operator()(const V& value, Error& error, type::Type expected) const {
        std::vector<ParsingError> errors;
        auto parsed = parseExpression(value, ParsingContext(errors, expected));
        if (parsed) {
            return std::move(*parsed);
        }
        std::string combinedError;
        for (const ParsingError& parsingError : errors) {
            if (combinedError.size() > 0) {
                combinedError += "\n";
            }
            combinedError += parsingError.key + ": " + parsingError.message;
        }
        error = { combinedError };
        return {};
    };
};

} // namespace conversion
} // namespace style
} // namespace mbgl
