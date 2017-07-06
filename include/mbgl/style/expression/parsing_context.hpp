#pragma once

#include <string>
#include <vector>
#include <mbgl/util/optional.hpp>

namespace mbgl {
namespace style {
namespace expression {

class ParsingContext {
public:
    ParsingContext() {}
    ParsingContext(ParsingContext previous,
                   optional<size_t> index,
                   optional<std::string> name) :
                   path(previous.path),
                   ancestors(previous.ancestors)
    {
        if (index) path.emplace_back(*index);
        if (name) ancestors.emplace_back(*name);
    }
    
    std::string key() const {
        std::string result;
        for(auto const& index : path) { result += "[" + std::to_string(index) + "]"; }
        return result;
    }
    
    std::string key(size_t lastIndex) const {
        return key() + "[" + std::to_string(lastIndex) + "]";
    }
    
    std::vector<size_t> path;
    std::vector<std::string> ancestors;
};

} // namespace expression
} // namespace style
} // namespace mbgl
