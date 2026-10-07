#include "Tasks.h"
#include "TaskHandler.h"
#include "Models.h"
#include "Wav.h"
#include "json.hpp"

#include <thread>
#include <chrono>
#include <iostream>
#include <functional>   
#include <exception>
#include <stop_token>

using json = nlohmann::json;

// forces the answer to be {"text": "...", "action": "answer" | "none"}
const std::string TEST_GRAMMAR = R"gbnf(
root   ::= "{" ws "\"text\":" ws string "," ws "\"action\":" ws action ws "}"
action ::= "\"answer\"" | "\"none\""
string ::= "\"" ( [^"\\\x7F\x00-\x1F] | "\\" (["\\bfnrt] | "u" [0-9a-fA-F]{4}) )* "\""
ws     ::= " "?
)gbnf";

std::vector<char> bytes(const std::string& s)
{
    return std::vector<char>(s.begin(), s.end());
}

// valid requests are built with json instead of typed by hand, so all the quotes get escaped for us
json withGrammar = {
    {"messages", json::array({
        {{"role", "system"},    {"content", "You are a helpful assistant. Answer in one short sentence.\n"
                                            "Reply ONLY with JSON: {\"text\": \"<your answer>\", \"action\": \"<answer or none>\"}"}},
        {{"role", "user"},      {"content", "What is 2 plus 2?"}},
        {{"role", "assistant"}, {"content", "{\"text\": \"2 plus 2 is 4.\", \"action\": \"answer\"}"}},
        {{"role", "user"},      {"content", "What is the capital of France?"}}
    })},
    {"grammar", TEST_GRAMMAR}
};

void Listener(std::stop_token stop_token, int timer, std::reference_wrapper<TaskQueue>TQ, std::reference_wrapper<std::exception_ptr> err)
{
    try
    {  
        Task t = {.type = TYPE_TEXT, .connectionID = 0, .payload = bytes(withGrammar.dump())};
        for(size_t i = 0; i < 10 && stop_token.stop_requested() != true; ++i)
        {
            std::this_thread::sleep_for(std::chrono::seconds(timer));

            // the connectionID is the index, so main can print which test an answer belongs to
            std::cout << "Thread: Enqueue [" << i << "] \n";
            TQ.get().enqueueTask(TYPE_TEXT, 0, bytes(withGrammar.dump()));
        }
    }
    catch(...)
    {
        std::cerr << "Exception in Listener\n" << '\n';
        err.get() = std::current_exception();
    }
    
    std::cout<<"Thread ended work!\n\n";
    TQ.get().terminate();
}

int main(int argc, char** argv)
{
    const bool runDirect = true;

    if(runDirect)
    {
        try
        {
            TaskHandler th;
            Task t = {.type = TYPE_TEXT, .connectionID = 0, .payload = bytes(withGrammar.dump())};
            std::string res = th.handle(t);
            std::cout<<res;

        }
        catch(const std::exception& e)
        {
            std::cerr << "Error in main: " << e.what() << '\n';
        }
    }
    else
    {
        TaskQueue TQ;
        std::exception_ptr err;
        try
        {
            TaskHandler th;
            std::jthread trs(&Listener,0, std::ref(TQ), std::ref(err));
            
            
            std::optional<Task> optNewTask;
            while((optNewTask = TQ.dequeueTask()))
            {
                std::string response = th.handle(optNewTask.value());
                std::cout << "Answer  [" << optNewTask->connectionID << "] " << response << "\n";
            }
        
        }
        catch(...)
        {
            std::cerr << "Error in main" << '\n';
        }
        
        if(err != NULL)
            std::rethrow_exception(err);
            
    }
}