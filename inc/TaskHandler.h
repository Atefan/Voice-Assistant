#ifndef SRC_TASKHANDLER_H
#define SRC_TASKHANDLER_H


#include "Tasks.h"
#include "Models.h"

#include <string>


class TaskHandler
{
public:
    // never throws for a bad request: errors come back as {"error": {"type": ..., "message": ...}}
    std::string handle(const Task &t);

private:
    STTModel sttm;
    LLMModel llmm;

    std::string handleText(const std::vector<char>& payload);
    std::string handleAudio(const std::vector<char>& payload);

    std::vector<float> bytesToFloat(const std::vector<char>& payload);
};

#endif
