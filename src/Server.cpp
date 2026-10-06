#include "Tasks.h"
#include "TaskHandler.h"
#include "Models.h"

#include <thread>
#include <chrono>
#include <iostream>
#include <functional>   
#include <exception>


void Listener(std::stop_token stop_token, int timer, std::reference_wrapper<TaskQueue>TQ, std::reference_wrapper<std::exception_ptr> err)
{
    try
    {  
        for(int i = 0; i<10 && stop_token.stop_requested() != true;++i)
        {
            std::cout<<"Thread: Enqueue " << i << "\n";
            std::this_thread::sleep_for(std::chrono::seconds(timer));
            
            const std::string prompt =
            "<|im_start|>system\nYou are a helpful voice assistant. Answer in one short sentence.<|im_end|>\n"
            "<|im_start|>user\nWhat is the capital of France?<|im_end|>\n"
            "<|im_start|>assistant\n<think>\n\n</think>\n\n";

            TQ.get().enqueueTask(TYPE_TEXT, 11, std::vector<char>(prompt.begin(), prompt.end()));
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

int main()
{
    TaskQueue TQ;
    std::exception_ptr err;
    try
    {
        TaskHandler th;
        std::jthread trs(&Listener,3, std::ref(TQ), std::ref(err));
        
        
        std::optional<Task> optNewTask;
        while((optNewTask = TQ.dequeueTask()))
        {
            std::cout<<"Hello found a task, passing it to the model !\n";
            th.handle(optNewTask.value());
        }
    
    }
    catch(...)
    {
        std::cerr << "Error in main" << '\n';
    }
    
    if(err != NULL)
        std::rethrow_exception(err);
}