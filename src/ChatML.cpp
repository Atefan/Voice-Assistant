#include "ChatML.h"
#include "Errors.h"

#include <string>

namespace
{
    using json = nlohmann::json;

    bool isValidRole(const std::string& role)
    {
        return role == "system" || role == "user" || role == "assistant";
    }
}

std::string jsonToChatML(const json& request)
{
    const json& messages = request.at("messages");

    if (!messages.is_array() || messages.empty())
        throw RequestError("messages must be a non-empty array");

    std::string prompt;
    std::string lastRole;

    for (int i = 0; i < messages.size(); ++i)
    {
        const std::string where = "messages[" + std::to_string(i) + "]";

        std::string role;
        std::string content;
        try
        {
            role    = messages[i].at("role").get<std::string>();
            content = messages[i].at("content").get<std::string>();
        }
        catch (const json::exception& e)
        {
            throw RequestError(where + ": " + e.what());
        }

        if (!isValidRole(role))
            throw RequestError(where + ": unknown role \"" + role + "\", expected system, user or assistant");

        prompt += "<|im_start|>" + role + "\n" + content + "<|im_end|>\n";
        lastRole = role;
    }

    prompt += "<|im_start|>assistant\n<think>\n\n</think>\n\n";

    return prompt;
}
