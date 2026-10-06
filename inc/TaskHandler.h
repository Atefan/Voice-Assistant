#ifndef SRC_TASKHANDLER_H
#define SRC_TASKHANDLER_H


#include "Tasks.h"
#include "Models.h"

#include <string>


class TaskHandler
{
public:
    std::string handle(const Task &t);

private:
    STTModel sttm;
    LLMModel llmm;

    std::string bytesToPrompt(std::vector<char> payload);
    std::vector<ggml_fp16_t> bytesToAudio(std::vector<char> payload);
};

#endif