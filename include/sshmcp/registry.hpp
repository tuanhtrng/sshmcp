#pragma once

#include <sshmcp/types.hpp>

#include <nlohmann/json.hpp>

#include <concepts>
#include <optional>
#include <string_view>

namespace sshmcp {

template<typename ToolType>
concept tool = requires(context_t& context,
                        nlohmann::json const& arguments) {
    { ToolType::NAME } -> std::convertible_to<std::string_view>;
    {
        ToolType::DESCRIPTION
    } -> std::convertible_to<std::string_view>;
    { ToolType::schema() } -> std::same_as<nlohmann::json>;
    {
        ToolType::invoke(context, arguments)
    } -> std::same_as<tool_result_t>;
};

template<tool... ToolTypes>
class tool_set_t {
public:
    static auto list() -> nlohmann::json {
        auto tools = nlohmann::json::array();
        (tools.push_back(
             {{"name", ToolTypes::NAME},
              {"description", ToolTypes::DESCRIPTION},
              {"inputSchema", ToolTypes::schema()}}),
         ...);
        return tools;
    }

    static auto call(context_t& context, std::string_view name,
                     nlohmann::json const& arguments)
        -> std::optional<tool_result_t> {
        auto result = std::optional<tool_result_t>{};
        ((name == ToolTypes::NAME
              ? (result = ToolTypes::invoke(context, arguments),
                 true)
              : false) ||
         ...);
        return result;
    }
};

}  // namespace sshmcp
