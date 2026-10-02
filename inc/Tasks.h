#ifndef SRC_TASKS_H
#define SRC_TASKS_H

#include <deque>
#include <optional>
#include <vector>

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

private:
    std::deque<Task> tasks;
};

#endif