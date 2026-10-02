#include "Tasks.h"

#include <gtest/gtest.h>

#include <atomic>
#include <optional>
#include <thread>
#include <vector>

// TaskQueue exists to hand work from the socket threads to the worker threads,
// so these are the tests that matter most. They are expected to FAIL (or abort
// the process) against the current implementation: enqueueTask and dequeueTask
// each lock a std::mutex declared as a *local variable*, so every caller locks
// its own private mutex and no mutual exclusion happens at all. Two threads
// then call std::vector::insert concurrently, which is a data race.
//
// Making the mutex a TaskQueue member is what turns these green.

namespace
{

constexpr int kProducers = 4;
constexpr int kPerProducer = 500;
constexpr int kTotal = kProducers * kPerProducer;

TEST(TaskQueueThreading, ConcurrentEnqueueLosesNoTasks)
{
    TaskQueue q;

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p)
    {
        producers.emplace_back([&q, p] {
            for (int i = 0; i < kPerProducer; ++i)
            {
                q.enqueueTask(TYPE_TEXT, p * kPerProducer + i, std::vector<char>{'x'});
            }
        });
    }
    for (std::thread & t : producers)
    {
        t.join();
    }

    std::vector<bool> seen(kTotal, false);
    for (int i = 0; i < kTotal; ++i)
    {
        const std::optional<Task> t = q.dequeueTask();
        ASSERT_TRUE(t.has_value()) << "queue ran dry after " << i << " of " << kTotal << " tasks";
        ASSERT_GE(t->connectionID, 0);
        ASSERT_LT(t->connectionID, kTotal);
        EXPECT_FALSE(seen[t->connectionID]) << "duplicate task " << t->connectionID;
        seen[t->connectionID] = true;
    }

    EXPECT_FALSE(q.dequeueTask().has_value()) << "queue held more tasks than were enqueued";
}

TEST(TaskQueueThreading, ConcurrentProducersAndConsumerSeeEveryTaskExactlyOnce)
{
    TaskQueue q;

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p)
    {
        producers.emplace_back([&q, p] {
            for (int i = 0; i < kPerProducer; ++i)
            {
                q.enqueueTask(TYPE_AUDIO, p * kPerProducer + i, std::vector<char>{'x'});
            }
        });
    }

    std::vector<bool> seen(kTotal, false);
    int drained = 0;
    // There is no blocking dequeue, so the consumer has to spin on nullopt to
    // find out the queue is only momentarily empty.
    while (drained < kTotal)
    {
        const std::optional<Task> t = q.dequeueTask();
        if (!t.has_value())
        {
            std::this_thread::yield();
            continue;
        }

        ASSERT_GE(t->connectionID, 0);
        ASSERT_LT(t->connectionID, kTotal);
        EXPECT_FALSE(seen[t->connectionID]) << "duplicate task " << t->connectionID;
        seen[t->connectionID] = true;
        ++drained;
    }

    for (std::thread & t : producers)
    {
        t.join();
    }
    EXPECT_EQ(drained, kTotal);
}

TEST(TaskQueueThreading, PayloadBytesSurviveConcurrentEnqueue)
{
    TaskQueue q;
    const std::vector<char> expected(64, 'z');

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p)
    {
        producers.emplace_back([&q, &expected] {
            for (int i = 0; i < kPerProducer; ++i)
            {
                q.enqueueTask(TYPE_AUDIO, i, expected);
            }
        });
    }
    for (std::thread & t : producers)
    {
        t.join();
    }

    for (int i = 0; i < kTotal; ++i)
    {
        const std::optional<Task> t = q.dequeueTask();
        ASSERT_TRUE(t.has_value()) << "queue ran dry after " << i << " tasks";
        EXPECT_EQ(t->payload, expected) << "payload corrupted at " << i;
    }
}

}  // namespace
