#include "Models.h"
#include "Errors.h"
#include "llama.h"
#include "whisper.h"

#include <string>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <cmath>
#include <memory>

// fill the batch with tokens at consecutive positions starting from pos_0, output logits only for the last one
static void batch_set_tokens(llama_batch_ext * batch, const llama_token * tokens, int32_t n_tokens, llama_pos pos_0) {
    llama_batch_ext_clear(batch);
    for (int32_t i = 0; i < n_tokens; ++i) {
        const int32_t idx = llama_batch_ext_add_token(batch, 0, tokens[i]);
        const llama_pos pos = pos_0 + i;
        llama_batch_ext_set_pos(batch, idx, &pos);
    }
    llama_batch_ext_set_output_logits(batch, n_tokens - 1, true);
}

static void print_usage(int, char ** argv) {
    printf("\nexample usage:\n");
    printf("\n    %s -m model.gguf [-n n_predict] [-ngl n_gpu_layers] [prompt]\n", argv[0]);
    printf("\n");
}

LLMModel::LLMModel()
{
    
    // initialize the model
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = ngl;

    this->model = llama_model_load_from_file(model_path.c_str(), model_params);

    if (model == NULL) {
        throw std::runtime_error("LLMModel: unable to load model from " + model_path);
    }


    // initialize the context
    llama_context_params ctx_params = llama_context_default_params();
    // n_ctx is the context size
    ctx_params.n_ctx = n_ctx;
    // n_batch is the maximum number of tokens that can be processed in a single call to llama_decode,
    // the whole prompt is processed in one call so it has to fit
    ctx_params.n_batch = n_ctx;
    // enable performance counters
    ctx_params.no_perf = false;

    this->ctx = llama_init_from_model(model, ctx_params);

    if (ctx == NULL) {
        // the destructor doesn't run when the constructor throws, so free the model here
        llama_model_free(model);
        throw std::runtime_error("LLMModel: failed to create the llama_context");
    }


}

LLMModel::~LLMModel()
{
    llama_free(ctx);
    llama_model_free(model);
}

std::string LLMModel::generate(const std::string& prompt, const std::string& grammar)
{
    const llama_vocab * vocab = llama_model_get_vocab(this->model);

    // every request is independent: forget the tokens of the previous one
    llama_memory_clear(llama_get_memory(ctx), true);

    // tokenize the prompt
    // find the number of tokens in the prompt: with no output buffer llama_tokenize returns minus the count
    const int n_prompt = -llama_tokenize(vocab, prompt.c_str(), prompt.size(), NULL, 0, true, true);

    // allocate space for the tokens and tokenize the prompt
    std::vector<llama_token> prompt_tokens(n_prompt);
    if (llama_tokenize(vocab, prompt.c_str(), prompt.size(), prompt_tokens.data(), prompt_tokens.size(), true, true) < 0) {
        throw ModelError("LLMModel: failed to tokenize the prompt");
    }

    if (n_prompt + n_predict > n_ctx) {
        throw RequestError("LLMModel: prompt too long (" + std::to_string(n_prompt) + " tokens, max " + std::to_string(n_ctx - n_predict) + ")");
    }

    // unique_ptr with a custom deleter: the C objects are freed on every exit path, also when we throw
    using SamplerPtr = std::unique_ptr<llama_sampler, decltype(&llama_sampler_free)>;
    using BatchPtr   = std::unique_ptr<llama_batch_ext, decltype(&llama_batch_ext_free)>;

    // initialize the sampler
    SamplerPtr smpl(llama_sampler_chain_init(llama_sampler_chain_default_params()), llama_sampler_free);

    // the grammar goes first, so the samplers after it only ever see tokens the grammar allows
    if (!grammar.empty()) {
        llama_sampler * grammar_smpl = llama_sampler_init_grammar(vocab, grammar.c_str(), "root");
        if (grammar_smpl == NULL) {
            throw RequestError("LLMModel: failed to parse the grammar");
        }
        llama_sampler_chain_add(smpl.get(), grammar_smpl);
    }
    llama_sampler_chain_add(smpl.get(), llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(smpl.get(), llama_sampler_init_temp(0.8f));
    llama_sampler_chain_add(smpl.get(), llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    BatchPtr batch(llama_batch_ext_init(this->ctx), llama_batch_ext_free);

    // the tokens to evaluate next: the prompt, then the sampled token
    const llama_token * tokens = prompt_tokens.data();
    int n_tokens = prompt_tokens.size();
    int n_pos = 0;

    std::string response;
    llama_token new_token_id;
    for (int n_generated = 0; ; ++n_generated) {
        // a grammar only guarantees valid JSON if the answer is finished, a cut-off answer is an error
        if (n_generated == n_predict) {
            throw ModelError("LLMModel: answer cut off after " + std::to_string(n_predict) + " tokens");
        }

        batch_set_tokens(batch.get(), tokens, n_tokens, n_pos);
        n_pos += n_tokens;

        if (llama_process(ctx, LLAMA_PROCESS_TYPE_DECODE, batch.get()) != 0) {
            throw ModelError("LLMModel: failed to decode");
        }

        // sample the next token
        new_token_id = llama_sampler_sample(smpl.get(), ctx, -1);

        // is it an end of generation?
        if (llama_vocab_is_eog(vocab, new_token_id)) {
            break;
        }

        // convert the token to a string and add it to the response
        char buf[256];
        int n = llama_token_to_piece(vocab, new_token_id, buf, sizeof(buf), 0, true);
        if (n < 0) {
            throw ModelError("LLMModel: failed to convert token to piece");
        }
        response.append(buf, n);

        // prepare the next batch with the sampled token
        tokens   = &new_token_id;
        n_tokens = 1;
    }

    return response;
}



STTModel::STTModel()
{
    // initialize the model
    whisper_context_params ctx_params = whisper_context_default_params();
    // the Raspberry Pi has no GPU backend, run on the CPU
    ctx_params.use_gpu = false;

    this->ctx = whisper_init_from_file_with_params(model_path.c_str(), ctx_params);

    if (ctx == NULL) {
        throw std::runtime_error("STTModel: unable to load whisper model from " + model_path);
    }
}

STTModel::~STTModel()
{
    whisper_free(ctx);
}

std::string STTModel::generate(std::vector<float> prompt)
{
    // whisper expects samples in [-1, 1]; anything else (wrong conversion, NaN)
    // makes it decode garbage like "!!!!" for minutes, so fail fast instead
    for (float s : prompt) {
        if (!std::isfinite(s) || s < -1.0f || s > 1.0f) {
            throw RequestError("STTModel: audio sample out of range [-1, 1], is the input converted correctly?");
        }
    }

    // greedy: always take the most likely token, fast and deterministic
    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

    params.n_threads        = n_threads;
    params.language         = "en";
    // don't feed the previous transcription back in, every task is independent
    params.no_context       = true;
    // keep whisper quiet, we only want the returned text
    params.print_progress   = false;
    params.print_realtime   = false;
    params.print_timestamps = false;
    params.print_special    = false;

    // run the whole model: PCM -> log mel spectrogram -> encoder -> decoder -> text
    if (whisper_full(ctx, params, prompt.data(), prompt.size()) != 0) {
        throw ModelError("STTModel: failed to process audio");
    }

    // whisper splits the transcription into segments, join them back together
    std::string text;
    const int n_segments = whisper_full_n_segments(ctx);
    for (int i = 0; i < n_segments; ++i) {
        text += whisper_full_get_segment_text(ctx, i);
    }

    return text;
}
