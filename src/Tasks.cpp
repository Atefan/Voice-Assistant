#include "Tasks.h"

#include <vector>
#include <deque>
#include <optional>
#include <mutex>


void TaskQueue::enqueueTask(TASK_TYPE type, int connectionID, std::vector<char> payload)
{
    {
        std::lock_guard<std::mutex> lg(this->m);
        Task t = {.type = type, .connectionID = connectionID, .payload = std::move(payload)};
        this->tasks.push_back(std::move(t));
    }
    cv.notify_all();
}

std::optional<Task> TaskQueue::dequeueTask()
{
    std::unique_lock lk(this->m);
    cv.wait(lk, [this]{ return !this->tasks.empty() || this->stopped; });

    // this->stopped == true is the exit flag
    int queueSize = this->tasks.size();
    if(queueSize <= 0 && this->stopped == true)
        return std::nullopt;

    Task t = std::move(this->tasks.front());
    this->tasks.pop_front();

    lk.unlock();
    return std::move(t);
}

void TaskQueue::terminate()
{
    {
        std::lock_guard<std::mutex> lkg(m);
        stopped = true;
    }
    cv.notify_all();
}
