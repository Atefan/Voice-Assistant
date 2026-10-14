#include "Tasks.h"
#include "TaskHandler.h"
#include "Models.h"
#include "Wav.h"
#include "json.hpp"
#include "Sockets.h"
#include "Protocol.h"
#include "Errors.h"

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


// Owns nothing: main creates the fd and closes it after this thread has joined.
//
// Blocks in recvFrame() until a whole frame arrives, turns it into a Task and
// enqueues it, then goes straight back to waiting. The connection stays open
// across requests - only a clean close by the client, or a real socket error,
// ends the loop.
// Takes a plain int, not the FdGuard: this thread borrows the descriptor and
// must never close it. main owns the guard and outlives this thread.
//
// (Passing reference_wrapper<FdGuard> does not work the way it looks: fd.get()
// then resolves to reference_wrapper::get(), which hands back an FdGuard&, not
// the descriptor.)
void Listener(std::stop_token stop_token, int fd, std::reference_wrapper<TaskQueue>TQ, std::reference_wrapper<std::exception_ptr> err)
{
    try
    {
        while(stop_token.stop_requested() != true)
        {
            std::optional<Frame> frame = recvFrame(fd);

            if(!frame)
            {
                std::cout << "Listener: client closed the connection\n";
                break;
            }

            std::cout << "Listener: frame type=" << frame->type
                      << " bytes=" << frame->payload.size() << "\n";

            try
            {
                Task t = frameToTask(std::move(*frame), fd);
                TQ.get().enqueueTask(t.type, t.connectionID, std::move(t.payload));
            }
            catch(const RequestError& e)
            {
                std::cerr << "Listener: rejected a frame: " << e.what() << '\n';

                const json reply = {{"error", {{"type", "invalid_request"}, {"message", e.what()}}}};
                sendFrame(fd, replyToFrame(reply.dump()));
            }
        }
    }
    catch(...)
    {
        std::cerr << "Exception in Listener\n";
        err.get() = std::current_exception();
    }

    std::cout<<"Thread ended work!\n\n";

    TQ.get().terminate();
}

int main(int argc, char** argv)
{
    const bool runDirect = false;

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
            // the models are loaded
            TaskHandler th;
            {
                FdGuard fd = connectToClient(TALK_PORT);
                std::jthread trs(&Listener, fd.get(), std::ref(TQ), std::ref(err));

                try
                {
                    std::optional<Task> optNewTask;
                    while((optNewTask = TQ.dequeueTask()))
                    {
                        std::string response = th.handle(optNewTask.value());
                        std::cout << "Answer  [" << optNewTask->connectionID << "] " << response << "\n";

                        sendFrame(optNewTask->connectionID, replyToFrame(response));
                    }
                }
                catch(...)
                {
                    shutdownConnection(fd);
                    throw;
                }
            }   // trs joins here, so nothing is still using fd
        }
        catch(const SocketError& e)
        {
            std::cerr << "Socket error in main: " << e.what() << '\n';
        }
        catch(const std::exception& e)
        {
            std::cerr << "Error in main: " << e.what() << '\n';
        }
        catch(...)
        {
            std::cerr << "Unknown error in main" << '\n';
        }
        
        if(err)
        {
            try
            {
                std::rethrow_exception(err);
            }
            catch(const std::exception& e)
            {
                std::cerr << "Error in Listener: " << e.what() << '\n';
            }
        }
    }
}