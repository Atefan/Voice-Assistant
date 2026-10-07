#ifndef SRC_CHATML_H
#define SRC_CHATML_H

#include "json.hpp"

#include <string>

// Converts a request like  {"messages": [{"role": "system", "content": "..."}, {"role": "user", "content": "..."}]}
// into Qwen3's chat template (ChatML)
std::string jsonToChatML(const nlohmann::json& request);

#endif
