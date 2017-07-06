#pragma once

#include <mbgl/util/variant.hpp>
#include <mbgl/style/expression/expression.hpp>

namespace mbgl {
namespace style {
namespace expression {

TypecheckResult typecheck(const type::Type& expected, const std::unique_ptr<Expression>& e);

} // namespace expression
} // namespace style
} // namespace mbgl
