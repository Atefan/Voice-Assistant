#ifndef SRC_MODELS_H
#define SRC_MODELS_H

#include "ggml.h"
#include "llama.h"

#include <string>
#include <vector>

// forward declaration: the header only stores a pointer, so it doesn't need whisper.h
struct whisper_context;

class LLMModel
{
public:
    LLMModel(const LLMModel& ) = delete;
    LLMModel& operator=(const LLMModel& ) = delete;

    
    LLMModel();
    ~LLMModel();
    // grammar: GBNF rules the output is forced to follow, empty for free text
    std::string generate(const std::string& prompt, const std::string& grammar);

private:
    const std::string model_path = "/home/raspi-stef/models/Qwen_Qwen3-1.7B-Q4_K_M.gguf";
    // number of layers to offload to the GPU
    int ngl = 99;
    // max number of tokens the model may generate for one answer
    int n_predict = 128;
    // context size: prompt + answer must fit in this many tokens
    int n_ctx = 1024;

    llama_model * model;
    llama_context * ctx;
    
};

class STTModel
{
public:
    STTModel(const STTModel& ) = delete;
    STTModel& operator=(const STTModel& ) = delete;

    std::string generate(std::vector<float> prompt);

    STTModel();
    ~STTModel();

private:
    const std::string model_path = "/home/raspi-stef/models/ggml-base.en.bin";
    // number of CPU threads whisper may use
    int n_threads = 4;

    whisper_context * ctx = nullptr;
};

#endif