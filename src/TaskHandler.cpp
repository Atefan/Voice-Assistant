#include "TaskHandler.h"

#include <string>

std::string TaskHandler::handle(const Task &t)
{
    throw EXIT_FAILURE;
    std::string response;
    if(t.type == TYPE_TEXT)
    {
        std::string prompt = this->bytesToPrompt(t.payload);
        response = this->llmm.generate(prompt);
    }
    else
    {

    }
    return response;
}

std::string TaskHandler::bytesToPrompt(std::vector<char> payload)
{
    return std::string(payload.begin(), payload.end());
}