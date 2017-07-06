#include <mbgl/style/expression/type_check.hpp>

namespace mbgl {
namespace style {
namespace expression {

using namespace mbgl::style::expression::type;

TypecheckResult typecheck(const Type& expected, const std::unique_ptr<Expression>& e) {
    if (LiteralExpression* literal = dynamic_cast<LiteralExpression*>(e.get())) {
        const auto& error = matchType(expected, literal->getType());
        if (error) return std::vector<CompileError> {{ *error, literal->getKey() }};
        return std::make_unique<LiteralExpression>(literal->getKey(), literal->getValue());
    } else if (LambdaExpression* lambda = dynamic_cast<LambdaExpression*>(e.get())) {
        // Check if the expected type matches the expression's output type;
        // If expression's output type is generic and the expected type is concrete,
        // pick up a typename binding
        std::unordered_map<std::string, Type> initialTypenames;
        const auto& resultMatchError = matchType(expected, lambda->getType(), initialTypenames, TypenameScope::actual);
        if (resultMatchError) return std::vector<CompileError> {{ *resultMatchError, lambda->getKey() }};
        
        std::vector<CompileError> errors;
        for (const auto& params : lambda->signatures) {
            std::unordered_map<std::string, Type> typenames(initialTypenames);
            errors.clear();
            // "Unroll" NArgs if present in the parameter list:
            // argCount = nargType.type.length * n + nonNargParameterCount
            // where n is the number of times the NArgs sequence must be
            // repeated.
            std::vector<Type> expandedParams;
            for (const auto& param : params) {
                param.match(
                    [&](const NArgs& nargs) {
                        size_t count = ceil(
                            float(lambda->args.size() - (params.size() - 1)) /
                            nargs.types.size()
                        );
                        if (nargs.N && *(nargs.N) < count) count = *(nargs.N);
                        while(count-- > 0) {
                            for(const auto& type : nargs.types) {
                                expandedParams.emplace_back(type);
                            }
                        }
                    },
                    [&](const Type& t) {
                        expandedParams.emplace_back(t);
                    }
                );
            }
            
            if (expandedParams.size() != lambda->args.size()) {
                errors.emplace_back(CompileError {
                    "Expected " + std::to_string(expandedParams.size()) +
                    " arguments, but found " + std::to_string(lambda->args.size()) + " instead.",
                    lambda->getKey()
                });
                continue;
            }
            
            // Iterate through arguments to:
            //  - match parameter type vs argument type, checking argument's result type only (don't recursively typecheck subexpressions at this stage)
            //  - collect typename mappings when ^ succeeds or type errors when it fails
            for (size_t i = 0; i < lambda->args.size(); i++) {
                const auto& param = expandedParams.at(i);
                const auto& arg = lambda->args.at(i);
//                    if (arg instanceof Reference) {
//                        arg = scope.get(arg.name);
//                    }

                const auto& error = matchType(
                    resolveTypenamesIfPossible(param, typenames),
                    arg->getType(),
                    typenames
                );
                if (error) {
                    errors.emplace_back(CompileError{ *error, arg->getKey() });
                }
            }
            
            const auto& resultType = resolveTypenamesIfPossible(expected, typenames);

            if (isGeneric(resultType)) {
                errors.emplace_back(CompileError {
                    "Could not resolve " + toString(lambda->getType()) + ".  This expression must be wrapped in a type conversion, e.g. [\"string\", ${stringifyExpression(e)}].",
                    lambda->getKey()
                });
            };
            
            // If we already have errors, return early so we don't get duplicates when
            // we typecheck against the resolved argument types
            if (errors.size()) continue;
            
            std::vector<Type> resolvedParams;
            std::vector<std::unique_ptr<Expression>> checkedArgs;
            for (std::size_t i = 0; i < expandedParams.size(); i++) {
                const auto& t = expandedParams.at(i);
                const auto& expectedArgType = resolveTypenamesIfPossible(t, typenames);
                auto checked = typecheck(expectedArgType, lambda->args.at(i));
                if (checked.is<std::vector<CompileError>>()) {
                    const auto& errs = checked.get<std::vector<CompileError>>();
                    errors.insert(errors.end(), errs.begin(), errs.end());
                } else if (checked.is<std::unique_ptr<Expression>>()) {
                    resolvedParams.emplace_back(expectedArgType);
                    checkedArgs.emplace_back(std::move(checked.get<std::unique_ptr<Expression>>()));
                }
            }
            
            if (errors.size() == 0) {
                return lambda->applyInferredType(resultType, std::move(checkedArgs));
            }
        }
        
        return errors;
    }
    
    assert(false);
}

} // namespace expression
} // namespace style
} // namespace mbgl
