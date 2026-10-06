#ifndef SRC_MODELS_H
#define SRC_MODELS_H

#include "ggml.h"
#include "llama.h"

#include <string>
#include <vector>

class LLMModel
{
public:
    LLMModel(const LLMModel& ) = delete;
    LLMModel& operator=(const LLMModel& ) = delete;

    
    LLMModel();
    ~LLMModel();
    std::string generate(std::string prompt); 

private:
    const std::string model_path = "/home/raspi-stef/models/Qwen_Qwen3-1.7B-Q4_K_M.gguf";
    // number of layers to offload to the GPU
    int ngl = 99;
    // number of tokens to predict
    int n_predict = 50;
    // find the number of tokens in the prompt
    int n_prompt = 50;

    llama_model * model;
    llama_context * ctx;
    
};

class STTModel
{
public:
    STTModel(const STTModel& ) = delete;
    STTModel& operator=(const STTModel& ) = delete;

    int generate(std::vector<ggml_fp16_t> prompt); 

    // TODO: load the whisper model here once STT is implemented
    STTModel() = default;
    ~STTModel() = default;
};

#endif