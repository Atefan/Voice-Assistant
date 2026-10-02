#include "Tasks.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace
{

std::vector<char> bytes(const std::string & s)
{
    return std::vector<char>(s.begin(), s.end());
}

std::string text(const std::vector<char> & payload)
{
    return std::string(payload.begin(), payload.end());
}

TEST(TaskQueue, RoundTripsEveryField)
{
    TaskQueue q;
    q.enqueueTask(TYPE_AUDIO, 42, bytes("hello"));

    const std::optional<Task> t = q.dequeueTask();

    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->type, TYPE_AUDIO);
    EXPECT_EQ(t->connectionID, 42);
    EXPECT_EQ(text(t->payload), "hello");
}

TEST(TaskQueue, DequeuesInFifoOrder)
{
    TaskQueue q;
    q.enqueueTask(TYPE_TEXT, 1, bytes("a"));
    q.enqueueTask(TYPE_TEXT, 2, bytes("b"));
    q.enqueueTask(TYPE_TEXT, 3, bytes("c"));

    EXPECT_EQ(q.dequeueTask()->connectionID, 1);
    EXPECT_EQ(q.dequeueTask()->connectionID, 2);
    EXPECT_EQ(q.dequeueTask()->connectionID, 3);
}

TEST(TaskQueue, KeepsFifoOrderWhenEnqueueAndDequeueInterleave)
{
    TaskQueue q;
    q.enqueueTask(TYPE_TEXT, 1, bytes("a"));
    q.enqueueTask(TYPE_TEXT, 2, bytes("b"));
    EXPECT_EQ(q.dequeueTask()->connectionID, 1);

    q.enqueueTask(TYPE_TEXT, 3, bytes("c"));
    EXPECT_EQ(q.dequeueTask()->connectionID, 2);
    EXPECT_EQ(q.dequeueTask()->connectionID, 3);
}

TEST(TaskQueue, PreservesOrderAcrossManyTasks)
{
    constexpr int kCount = 2000;
    TaskQueue q;

    for (int i = 0; i < kCount; ++i)
    {
        q.enqueueTask(TYPE_TEXT, i, bytes("x"));
    }

    for (int i = 0; i < kCount; ++i)
    {
        const std::optional<Task> t = q.dequeueTask();
        ASSERT_TRUE(t.has_value()) << "queue ran dry at " << i;
        EXPECT_EQ(t->connectionID, i) << "order broke at " << i;
    }
}

TEST(TaskQueue, CarriesBothTaskTypesIndependently)
{
    TaskQueue q;
    q.enqueueTask(TYPE_TEXT, 1, bytes("a"));
    q.enqueueTask(TYPE_AUDIO, 2, bytes("b"));

    EXPECT_EQ(q.dequeueTask()->type, TYPE_TEXT);
    EXPECT_EQ(q.dequeueTask()->type, TYPE_AUDIO);
}

TEST(TaskQueue, InstancesDoNotShareState)
{
    TaskQueue first;
    TaskQueue second;

    first.enqueueTask(TYPE_TEXT, 1, bytes("a"));
    second.enqueueTask(TYPE_TEXT, 2, bytes("b"));

    EXPECT_EQ(first.dequeueTask()->connectionID, 1);
    EXPECT_EQ(second.dequeueTask()->connectionID, 2);
    EXPECT_FALSE(first.dequeueTask().has_value());
    EXPECT_FALSE(second.dequeueTask().has_value());
}

TEST(TaskQueue, DequeueOnEmptyQueueReturnsNullopt)
{
    TaskQueue q;

    EXPECT_FALSE(q.dequeueTask().has_value());
}

TEST(TaskQueue, DequeueReturnsNulloptOnceDrained)
{
    TaskQueue q;
    q.enqueueTask(TYPE_TEXT, 1, bytes("a"));

    EXPECT_TRUE(q.dequeueTask().has_value());
    EXPECT_FALSE(q.dequeueTask().has_value());
    EXPECT_FALSE(q.dequeueTask().has_value());
}

TEST(TaskQueue, QueueIsReusableAfterDraining)
{
    TaskQueue q;
    q.enqueueTask(TYPE_TEXT, 1, bytes("a"));
    EXPECT_EQ(q.dequeueTask()->connectionID, 1);
    ASSERT_FALSE(q.dequeueTask().has_value());

    q.enqueueTask(TYPE_AUDIO, 2, bytes("b"));
    const std::optional<Task> t = q.dequeueTask();
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->connectionID, 2);
    EXPECT_EQ(text(t->payload), "b");
}

TEST(TaskQueue, OwnsItsPayloadRatherThanAliasingTheCallers)
{
    std::vector<char> payload = bytes("hello");
    TaskQueue q;
    q.enqueueTask(TYPE_TEXT, 1, payload);

    payload[0] = 'J';
    payload.clear();

    const std::optional<Task> t = q.dequeueTask();
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(text(t->payload), "hello");
}

TEST(TaskQueue, PreservesEmptyPayload)
{
    TaskQueue q;
    q.enqueueTask(TYPE_TEXT, 1, {});

    const std::optional<Task> t = q.dequeueTask();
    ASSERT_TRUE(t.has_value());
    EXPECT_TRUE(t->payload.empty());
}

TEST(TaskQueue, PreservesEmbeddedNullBytesAndFullLength)
{
    const std::vector<char> payload = {'a', '\0', 'b', '\0', '\0', 'c'};
    TaskQueue q;
    q.enqueueTask(TYPE_AUDIO, 1, payload);

    const std::optional<Task> t = q.dequeueTask();
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->payload, payload);
    EXPECT_EQ(t->payload.size(), 6u);
}

TEST(TaskQueue, PreservesLargePayloadIntact)
{
    std::vector<char> payload(1 << 20);
    for (std::size_t i = 0; i < payload.size(); ++i)
    {
        payload[i] = static_cast<char>(i % 251);
    }

    TaskQueue q;
    q.enqueueTask(TYPE_AUDIO, 7, payload);

    const std::optional<Task> t = q.dequeueTask();
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->payload, payload);
}

TEST(TaskQueue, AcceptsAMovedPayload)
{
    std::vector<char> payload = bytes("hello");

    TaskQueue q;
    q.enqueueTask(TYPE_TEXT, 1, std::move(payload));

    const std::optional<Task> t = q.dequeueTask();
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(text(t->payload), "hello");
}

TEST(TaskQueue, AcceptsOutOfRangeTaskType)
{
    TaskQueue q;
    q.enqueueTask(static_cast<TASK_TYPE>(99), 1, bytes("a"));

    EXPECT_EQ(static_cast<int>(q.dequeueTask()->type), 99);
}

}  // namespace
