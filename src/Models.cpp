#include "Models.h"
#include "llama.h"

#include <string>
#include <cstring>
#include <vector>

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
        fprintf(stderr , "%s: error: unable to load model\n" , __func__);
        throw EXIT_FAILURE;
    }


    // initialize the context
    llama_context_params ctx_params = llama_context_default_params();
    // n_ctx is the context size
    ctx_params.n_ctx = n_prompt + this->n_predict - 1;
    // n_batch is the maximum number of tokens that can be processed in a single call to llama_decode
    ctx_params.n_batch = n_prompt;
    // enable performance counters
    ctx_params.no_perf = false;

    this->ctx = llama_init_from_model(model, ctx_params);

    if (ctx == NULL) {
        fprintf(stderr , "%s: error: failed to create the llama_context\n" , __func__);
        throw EXIT_FAILURE;
    }


}

LLMModel::~LLMModel()
{
    llama_free(ctx);
    llama_model_free(model);
}

std::string LLMModel::generate(std::string prompt)
{

    // initialize the sampler
    llama_sampler * smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.8f));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    llama_batch_ext * batch = llama_batch_ext_init(this->ctx);


    const llama_vocab * vocab = llama_model_get_vocab(this->model);
    // tokenize the prompt

    // find the number of tokens in the prompt
    const int n_prompt = -llama_tokenize(vocab, prompt.c_str(), prompt.size(), NULL, 0, true, true);

    // allocate space for the tokens and tokenize the prompt
    std::vector<llama_token> prompt_tokens(n_prompt);
    if (llama_tokenize(vocab, prompt.c_str(), prompt.size(), prompt_tokens.data(), prompt_tokens.size(), true, true) < 0) {
        fprintf(stderr, "%s: error: failed to tokenize the prompt\n", __func__);
        throw EXIT_FAILURE;
    }

    // helper function to evaluate a prompt and generate a response
    auto generate = [&](const std::string & prompt) {
        std::string response;

        const bool is_first = llama_memory_seq_pos_max(llama_get_memory(ctx), 0) == -1;

        // tokenize the prompt
        const int n_prompt_tokens = -llama_tokenize(vocab, prompt.c_str(), prompt.size(), NULL, 0, is_first, true);
        std::vector<llama_token> prompt_tokens(n_prompt_tokens);
        if (llama_tokenize(vocab, prompt.c_str(), prompt.size(), prompt_tokens.data(), prompt_tokens.size(), is_first, true) < 0) {
            GGML_ABORT("failed to tokenize the prompt\n");
        }

        // the tokens to evaluate next: the prompt, then the sampled token
        const llama_token * tokens = prompt_tokens.data();
        int n_tokens = prompt_tokens.size();

        llama_token new_token_id;
        while (true) {
            // check if we have enough space in the context to evaluate this batch
            int n_ctx = llama_n_ctx(ctx);
            int n_ctx_used = llama_memory_seq_pos_max(llama_get_memory(ctx), 0) + 1;
            if (n_ctx_used + n_tokens > n_ctx) {
                printf("\033[0m\n");
                fprintf(stderr, "context size exceeded\n");
                throw EXIT_FAILURE;
            }

            // positions continue from the memory
            batch_set_tokens(batch, tokens, n_tokens, n_ctx_used);

            int ret = llama_process(ctx, LLAMA_PROCESS_TYPE_DECODE, batch);
            if (ret != 0) {
                GGML_ABORT("failed to decode, ret = %d\n", ret);
            }

            // sample the next token
            new_token_id = llama_sampler_sample(smpl, ctx, -1);

            // is it an end of generation?
            if (llama_vocab_is_eog(vocab, new_token_id)) {
                break;
            }

            // convert the token to a string, print it and add it to the response
            char buf[256];
            int n = llama_token_to_piece(vocab, new_token_id, buf, sizeof(buf), 0, true);
            if (n < 0) {
                GGML_ABORT("failed to convert token to piece\n");
            }
            std::string piece(buf, n);
            printf("%s", piece.c_str());
            fflush(stdout);
            response += piece;

            // prepare the next batch with the sampled token
            tokens   = &new_token_id;
            n_tokens = 1;
        }

        return response;
    };

    std::string response = generate(prompt);

    llama_batch_ext_free(batch);
    llama_sampler_free(smpl);

    return response;
}

