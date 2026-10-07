#include "TaskHandler.h"
#include "ChatML.h"
#include "Errors.h"
#include "json.hpp"

#include <string>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <stdexcept>


namespace
{
    using json = nlohmann::json;

    // {"error": {"type": "...", "message": "..."}}
    std::string errorReply(const std::string& type, const std::string& message)
    {
        json reply = {{"error", {{"type", type}, {"message", message}}}};
        return reply.dump(-1, ' ', false, json::error_handler_t::replace);
    }

    // whisper puts a space in front of the text
    std::string trim(const std::string& s)
    {
        const size_t first = s.find_first_not_of(" \t\n");
        if (first == std::string::npos)
            return "";
        const size_t last = s.find_last_not_of(" \t\n");
        return s.substr(first, last - first + 1);
    }
}

std::string TaskHandler::handle(const Task &t)
{
    try
    {
        switch(t.type)
        {
            case TYPE_TEXT:  return this->handleText(t.payload);
            case TYPE_AUDIO: return this->handleAudio(t.payload);
        }
        throw RequestError("unknown task type " + std::to_string(t.type));
    }
    catch(const json::parse_error& e)
    {
        return errorReply("invalid_json", e.what());
    }
    catch(const json::exception& e)
    {
        return errorReply("invalid_request", e.what());
    }
    catch(const RequestError& e)
    {
        return errorReply("invalid_request", e.what());
    }
    catch(const ModelError& e)
    {
        return errorReply("model_error", e.what());
    }
    catch(const std::exception& e)
    {
        return errorReply("internal_error", e.what());
    }
}

std::string TaskHandler::handleText(const std::vector<char>& payload)
{
    const json request = json::parse(payload.begin(), payload.end());

    if (!request.is_object())
        throw RequestError("the request must be a JSON object");

    const std::string prompt  = jsonToChatML(request);
    const std::string grammar = request.value("grammar", std::string());

    return this->llmm.generate(prompt, grammar);
}

std::string TaskHandler::handleAudio(const std::vector<char>& payload)
{
    // for now audio only returns the transcription as plain text
    return trim(this->sttm.generate(bytesToFloat(payload)));
}

std::vector<float> TaskHandler::bytesToFloat(const std::vector<char>& payload)
{
    if (payload.size() % sizeof(int16_t) != 0) {
        throw RequestError("audio payload is not a whole number of 16-bit samples");
    }

    std::vector<int16_t> samples(payload.size() / sizeof(int16_t));
    std::memcpy(samples.data(), payload.data(), payload.size());

    std::vector<float> floats(samples.size());
    std::transform(samples.begin(), samples.end(), floats.begin(), [](int16_t s){ return s / 32768.0f; });

    return floats;
}
