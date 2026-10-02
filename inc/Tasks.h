#ifndef SRC_TASKS_H
#define SRC_TASKS_H

#include <deque>
#include <optional>
#include <vector>
#include <mutex>
#include <condition_variable>

enum TASK_TYPE
{
TYPE_TEXT = 0,
TYPE_AUDIO = 1
};

struct Task
{
    TASK_TYPE type;
    int connectionID;
    std::vector<char> payload;
};


class TaskQueue
{
public:
    void enqueueTask(TASK_TYPE type, int connectionID, std::vector<char> payload);
    std::optional<Task> dequeueTask();
    void terminate();

private:
    std::deque<Task> tasks;
    std::mutex m;
    std::condition_variable cv;
    bool stopped = false;
};

#endif