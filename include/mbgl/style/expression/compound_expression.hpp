#pragma once

#include <array>
#include <vector>
#include <memory>
#include <mbgl/util/optional.hpp>
#include <mbgl/util/variant.hpp>
#include <mbgl/util/color.hpp>
#include <mbgl/style/expression/check_subtype.hpp>
#include <mbgl/style/expression/expression.hpp>
#include <mbgl/style/expression/type.hpp>
#include <mbgl/style/expression/value.hpp>
#include <mbgl/style/expression/parsing_context.hpp>
#include <mbgl/style/conversion.hpp>

#define expand_pack(...) (void) std::initializer_list<int>{((void)(__VA_ARGS__), 0)...};

namespace mbgl {
namespace style {
namespace expression {

namespace detail {
    template <typename T>
    std::decay_t<T> get(const Value& value);
} // namespace detail


struct VarargsType { type::Type type; };
template <typename T>
struct Varargs : std::vector<T> { using std::vector<T>::vector; };


struct SignatureBase {
    SignatureBase(type::Type result_, variant<std::vector<type::Type>, VarargsType> params_) :
        result(result_),
        params(params_)
    {}
    virtual ~SignatureBase() {}
    virtual std::unique_ptr<Expression> makeExpression(const std::string& name, std::vector<std::unique_ptr<Expression>>) const = 0;
    type::Type result;
    variant<std::vector<type::Type>, VarargsType> params;
};

template <class, class Enable = void>
struct Signature;

// Signature from a zoom- or property-dependent evaluation function:
// (const EvaluationParameters&, T1, T2, ...) => Result<U>,
// where T1, T2, etc. are the types of successfully-evaluated subexpressions.
template <class R, class... Params>
struct Signature<R (const EvaluationParameters&, Params...)> : SignatureBase {
    using Args = std::array<std::unique_ptr<Expression>, sizeof...(Params)>;
    
    Signature(R (*evaluate_)(const EvaluationParameters&, Params...),
              bool isFeatureConstant_ = true,
              bool isZoomConstant_ = true) :
        SignatureBase(
            valueTypeToExpressionType<std::decay_t<typename R::Value>>(),
            std::vector<type::Type> {valueTypeToExpressionType<std::decay_t<Params>>()...}
        ),
        evaluate(evaluate_),
        featureConstant(isFeatureConstant_),
        zoomConstant(isZoomConstant_)
    {}
    
    std::unique_ptr<Expression> makeExpression(const std::string& name, std::vector<std::unique_ptr<Expression>> args) const override;
    
    bool isFeatureConstant() const { return featureConstant; }
    bool isZoomConstant() const { return zoomConstant; }
    EvaluationResult apply(const EvaluationParameters& evaluationParameters, const Args& args) const {
        return applyImpl(evaluationParameters, args, std::index_sequence_for<Params...>{});
    }
    
private:
    template <std::size_t ...I>
    EvaluationResult applyImpl(const EvaluationParameters& evaluationParameters, const Args& args, std::index_sequence<I...>) const {
        const std::vector<EvaluationResult>& evaluated = {std::get<I>(args)->evaluate(evaluationParameters)...};
        for (const auto& arg : evaluated) {
            if(!arg) return arg.error();
        }
        // TODO: assert correct runtime type of each arg value
        const R& result = evaluate(evaluationParameters, detail::get<Params>(*(evaluated.at(I)))...);
        if (!result) return result.error();
        return *result;
    }
    
    R (*evaluate)(const EvaluationParameters&, Params...);
    bool featureConstant;
    bool zoomConstant;
};

// Signature from varargs evaluation function: (Varargs<T>) => Result<U>,
// where T is the type of each successfully-evaluated subexpression (Varargs<T> being
// an alias for vector<T>).
template <class R, typename T>
struct Signature<R (const Varargs<T>&)> : SignatureBase {
    using Args = std::vector<std::unique_ptr<Expression>>;
    
    Signature(R (*evaluate_)(const Varargs<T>&)) :
        SignatureBase(
            valueTypeToExpressionType<std::decay_t<typename R::Value>>(),
            VarargsType { valueTypeToExpressionType<T>() }
        ),
        evaluate(evaluate_)
    {}
    
    std::unique_ptr<Expression> makeExpression(const std::string& name, std::vector<std::unique_ptr<Expression>> args) const override;
    
    bool isFeatureConstant() const { return true; }
    bool isZoomConstant() const { return true; }
    EvaluationResult apply(const EvaluationParameters& evaluationParameters, const Args& args) const {
        Varargs<T> evaluated;
        for (const auto& arg : args) {
            const auto& evaluatedArg = arg->evaluate<T>(evaluationParameters);
            if(!evaluatedArg) return evaluatedArg.error();
            evaluated.push_back(*evaluatedArg);
        }
        const R& result = evaluate(evaluated);
        if (!result) return result.error();
        return *result;
    }
    
    R (*evaluate)(const Varargs<T>&);
};

// Signature from "pure" evaluation function: (T1, T2, ...) => Result<U>,
// where T1, T2, etc. are the types of successfully-evaluated subexpressions.
template <class R, class... Params>
struct Signature<R (Params...)> : SignatureBase {
    using Args = std::array<std::unique_ptr<Expression>, sizeof...(Params)>;
    
    Signature(R (*evaluate_)(Params...)) :
        SignatureBase(
            valueTypeToExpressionType<std::decay_t<typename R::Value>>(),
            std::vector<type::Type> {valueTypeToExpressionType<std::decay_t<Params>>()...}
        ),
        evaluate(evaluate_)
    {}
    
    bool isFeatureConstant() const { return true; }
    bool isZoomConstant() const { return true; }
    EvaluationResult apply(const EvaluationParameters& evaluationParameters, const Args& args) const {
        return applyImpl(evaluationParameters, args, std::index_sequence_for<Params...>{});
    }
    
    std::unique_ptr<Expression> makeExpression(const std::string& name, std::vector<std::unique_ptr<Expression>> args) const override;
    
    R (*evaluate)(Params...);
private:
    template <std::size_t ...I>
    EvaluationResult applyImpl(const EvaluationParameters& evaluationParameters, const Args& args, std::index_sequence<I...>) const {
        const std::vector<EvaluationResult>& evaluated = {std::get<I>(args)->evaluate(evaluationParameters)...};
        for (const auto& arg : evaluated) {
            if(!arg) return arg.error();
        }
        // TODO: assert correct runtime type of each arg value
        const R& result = evaluate(detail::get<Params>(*(evaluated.at(I)))...);
        if (!result) return result.error();
        return *result;
    }
};

template <class R, class... Params>
struct Signature<R (*)(Params...)>
    : Signature<R (Params...)>
{ using Signature<R (Params...)>::Signature; };

template <class T, class R, class... Params>
struct Signature<R (T::*)(Params...) const>
    : Signature<R (Params...)>
{ using Signature<R (Params...)>::Signature;  };

template <class T, class R, class... Params>
struct Signature<R (T::*)(Params...)>
    : Signature<R (Params...)>
{ using Signature<R (T::*)(Params...)>::Signature; };

template <class Lambda>
struct Signature<Lambda, std::enable_if_t<std::is_class<Lambda>::value>>
    : Signature<decltype(&Lambda::operator())>
{ using Signature<decltype(&Lambda::operator())>::Signature; };

class CompoundExpressionBase : public Expression {
public:
    CompoundExpressionBase(const std::string& name_, type::Type type) :
        Expression(type),
        name(name_)
    {}
    
    std::string getName() { return name; }
    
    virtual ~CompoundExpressionBase();
private:
    std::string name;
};

template <typename Signature>
class CompoundExpression : public CompoundExpressionBase {
public:
    using Args = typename Signature::Args;
    
    CompoundExpression(const std::string& name,
                       Signature signature_,
                       typename Signature::Args args_) :
        CompoundExpressionBase(name, signature_.result),
        signature(signature_),
        args(std::move(args_))
    {}
    
    EvaluationResult evaluate(const EvaluationParameters& params) const override {
        return signature.apply(params, args);
    }
    
    bool isFeatureConstant() const override {
        if (!signature.isFeatureConstant()) {
            return false;
        }
        for (const auto& arg : args) {
            if (!arg->isFeatureConstant()) { return false; }
        }
        return true;
    }
    
    bool isZoomConstant() const override {
        if (!signature.isZoomConstant()) {
            return false;
        }
        for (const auto& arg : args) {
            if (!arg->isZoomConstant()) { return false; }
        }
        return true;
    }
    
private:
    Signature signature;
    typename Signature::Args args;
};


struct CompoundExpressions {
    using Definition = std::vector<std::unique_ptr<SignatureBase>>;
    static std::unordered_map<std::string, Definition> definitions;
    
    template <class V>
    static ParseResult parse(const V& value, ParsingContext ctx) {
        using namespace mbgl::style::conversion;
        assert(isArray(value) && arrayLength(value) > 0);
        const auto& name = toString(arrayMember(value, 0));
        assert(name);
        
        auto it = definitions.find(*name);
        if (it == definitions.end()) {
            ctx.error(
                std::string("Unknown expression \"") + *name + "\". If you wanted a literal array, use [\"literal\", [...]].",
                0
            );
            return ParseResult();
        }
        const Definition& definition = it->second;

        // parse subexpressions first
        std::vector<std::unique_ptr<Expression>> args;
        auto length = arrayLength(value);
        for (std::size_t i = 1; i < length; i++) {
            auto parsed = parseExpression(arrayMember(value, i), ParsingContext(ctx, i));
            if (!parsed) {
                return parsed;
            }
            args.push_back(std::move(*parsed));
        }
        
        return create(*name, definition, std::move(args), ctx);
    }
    
    static ParseResult create(const std::string& name,
                              std::vector<std::unique_ptr<Expression>> args,
                              ParsingContext ctx)
    {
        return create(name, definitions.at(name), std::move(args), ctx);
    }
    
    static ParseResult create(const std::string& name,
                              const Definition& definition,
                              std::vector<std::unique_ptr<Expression>> args,
                              ParsingContext ctx)
    {
        std::vector<ParsingError> currentSignatureErrors;

        ParsingContext signatureContext(currentSignatureErrors);
        signatureContext.key = ctx.key;
        
        for (const auto& signature : definition) {
            currentSignatureErrors.clear();
            
            
            if (signature->params.is<std::vector<type::Type>>()) {
                const auto& params = signature->params.get<std::vector<type::Type>>();
                if (params.size() != args.size()) {
                    signatureContext.error(
                        "Expected " + std::to_string(params.size()) +
                        " arguments, but found " + std::to_string(args.size()) + " instead."
                    );
                    continue;
                }

                for (std::size_t i = 0; i < args.size(); i++) {
                    const auto& arg = args.at(i);
                    checkSubtype(params.at(i), arg->getType(), ParsingContext(signatureContext, i + 1));
                }
            } else if (signature->params.is<VarargsType>()) {
                const auto& paramType = signature->params.get<VarargsType>().type;
                for (std::size_t i = 0; i < args.size(); i++) {
                    const auto& arg = args.at(i);
                    checkSubtype(paramType, arg->getType(), ParsingContext(signatureContext, i + 1));
                }
            }
            
            if (currentSignatureErrors.size() == 0) {
                return ParseResult(signature->makeExpression(name, std::move(args)));
            }
        }
        
        if (definition.size() == 1) {
            ctx.errors.insert(ctx.errors.end(), currentSignatureErrors.begin(), currentSignatureErrors.end());
        } else {
            std::string signatures;
            for (const auto& signature : definition) {
                signatures += (signatures.size() > 0 ? " | " : "");
                signature->params.match(
                    [&](const VarargsType& varargs) {
                        signatures += "(" + toString(varargs.type) + ")";
                    },
                    [&](const std::vector<type::Type>& params) {
                        signatures += "(";
                        for (const type::Type& param : params) {
                            signatures += toString(param);
                        }
                        signatures += ")";
                    }
                );
                
            }
            std::string actualTypes = "(";
            for (const auto& arg : args) {
                if (actualTypes.size() > 0) {
                    actualTypes += ", ";
                }
                actualTypes += toString(arg->getType());
            }
            ctx.error("Expected arguments of type ${signatures}, but found (${actualTypes}) instead.");
        }
        
        return ParseResult();
    }
};


} // namespace expression
} // namespace style
} // namespace mbgl
