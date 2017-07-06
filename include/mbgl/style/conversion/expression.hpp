#pragma once

#include <memory>
#include <mbgl/style/expression/parse.hpp>
#include <mbgl/style/conversion.hpp>

namespace mbgl {
namespace style {
namespace conversion {

using namespace mbgl::style::expression;

template<> struct Converter<std::unique_ptr<Expression>> {
    template <class V>
    optional<std::unique_ptr<Expression>> operator()(const V& value, Error& error) const {
        auto parsed = parseExpression(value, ParsingContext());
        if (parsed.template is<std::unique_ptr<Expression>>()) {
            return std::move(parsed.template get<std::unique_ptr<Expression>>());
        }
        error = { parsed.template get<CompileError>().message };
        return {};
    };
};

} // namespace conversion
} // namespace style
} // namespace mbgl
