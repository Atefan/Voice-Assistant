#include "Tasks.h"

#include <vector>
#include <optional>
#include <mutex>


void TaskQueue::enqueueTask(TASK_TYPE type, int connectionID, std::vector<char> payload)
{
    std::mutex m;
    std::lock_guard<std::mutex> lg(m);
    Task t = {.type = type, .connectionID = connectionID, .payload = std::move(payload)};
    this->tasks.push_back(t);
}

std::optional<Task> TaskQueue::dequeueTask()
{
    std::mutex m;
    std::lock_guard<std::mutex> lg(m);

    int queueSize = this->tasks.size();
    if(queueSize <= 0)
        return std::nullopt;

    Task t = std::move(this->tasks.back());
    this->tasks.pop_front();
    return std::move(t);
}
