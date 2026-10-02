#include "Tasks.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// ===========================================================================
// READ THIS FIRST - dequeueTask() BLOCKS
// ===========================================================================
// dequeueTask() calls cv.wait(), so on an empty queue it does NOT return
// nullopt - it parks the thread until a task is enqueued or terminate() is
// called. So this hangs forever and the test run never finishes:
//
//     TaskQueue q;
//     EXPECT_FALSE(q.dequeueTask().has_value());   // <-- HANGS
//
// Every test below either dequeues exactly as many tasks as it enqueued, or
// calls terminate() first. If you add a test that hangs, Ctrl-C it and run:
//     gdb -p $(pgrep test_tasks)
//     (gdb) thread apply all bt
// A frame in __pthread_cond_wait is a dequeue nobody will ever wake.
// ===========================================================================

namespace
{

using namespace std::chrono_literals;

std::vector<char> bytes(const std::string & s)
{
    return std::vector<char>(s.begin(), s.end());
}

std::string text(const std::vector<char> & payload)
{
    return std::string(payload.begin(), payload.end());
}

// ---------------------------------------------------------------------------
// Socket simulation
// ---------------------------------------------------------------------------
// Each "socket reader" thread stands in for the thread that will eventually
// sit on a recv() loop for one connection. It stamps every frame with its own
// connection ID and a sequence number, so the consumer can prove afterwards
// that nothing was lost, duplicated or reordered.

constexpr int kConnA = 101;
constexpr int kConnB = 202;
constexpr int kFramesPerSocket = 200;
constexpr int kTotalFrames = kFramesPerSocket * 2;

std::vector<char> frameBytes(int connectionID, int sequence)
{
    return bytes(std::to_string(connectionID) + ":" + std::to_string(sequence));
}

std::pair<int, int> parseFrame(const std::vector<char> & payload)
{
    const std::string s = text(payload);
    const std::size_t colon = s.find(':');
    if (colon == std::string::npos)
        return {-1, -1};
    return {std::stoi(s.substr(0, colon)), std::stoi(s.substr(colon + 1))};
}

// The yield every 16 frames makes the two readers genuinely interleave rather
// than one finishing before the other is scheduled.
void runSocketReader(TaskQueue & q, int connectionID, TASK_TYPE type, int frames)
{
    for (int sequence = 0; sequence < frames; ++sequence)
    {
        q.enqueueTask(type, connectionID, frameBytes(connectionID, sequence));
        if (sequence % 16 == 0)
            std::this_thread::yield();
    }
}

// ===========================================================================
// Two socket readers feeding one worker
// ===========================================================================

TEST(TaskQueueSockets, TwoReadersDeliverEveryFrameExactlyOnce)
{
    TaskQueue q;
    std::thread socketA(runSocketReader, std::ref(q), kConnA, TYPE_TEXT, kFramesPerSocket);
    std::thread socketB(runSocketReader, std::ref(q), kConnB, TYPE_AUDIO, kFramesPerSocket);

    std::map<int, std::vector<bool>> seen;
    seen[kConnA].assign(kFramesPerSocket, false);
    seen[kConnB].assign(kFramesPerSocket, false);

    for (int i = 0; i < kTotalFrames; ++i)
    {
        const std::optional<Task> t = q.dequeueTask();
        ASSERT_TRUE(t.has_value()) << "worker woke with no task after " << i << " frames";

        const auto [connectionID, sequence] = parseFrame(t->payload);
        ASSERT_EQ(connectionID, t->connectionID) << "payload and connectionID disagree";
        ASSERT_TRUE(seen.contains(connectionID)) << "unknown connection " << connectionID;
        ASSERT_GE(sequence, 0);
        ASSERT_LT(sequence, kFramesPerSocket);

        EXPECT_FALSE(seen[connectionID][sequence])
            << "frame " << connectionID << ":" << sequence << " delivered twice";
        seen[connectionID][sequence] = true;
    }

    socketA.join();
    socketB.join();

    for (const auto & [connectionID, frames] : seen)
    {
        for (int sequence = 0; sequence < kFramesPerSocket; ++sequence)
        {
            EXPECT_TRUE(frames[sequence]) << "lost frame " << connectionID << ":" << sequence;
        }
    }
}

// Frames from two different sockets may interleave in any order - that part is
// up to the scheduler and asserting on it would make the test flaky. What must
// hold is that one connection's own frames stay in the order it sent them,
// otherwise a sentence arrives scrambled or audio plays out of sequence.
TEST(TaskQueueSockets, PreservesOrderWithinEachConnection)
{
    TaskQueue q;
    std::thread socketA(runSocketReader, std::ref(q), kConnA, TYPE_TEXT, kFramesPerSocket);
    std::thread socketB(runSocketReader, std::ref(q), kConnB, TYPE_AUDIO, kFramesPerSocket);

    std::map<int, int> lastSequence{{kConnA, -1}, {kConnB, -1}};

    for (int i = 0; i < kTotalFrames; ++i)
    {
        const std::optional<Task> t = q.dequeueTask();
        ASSERT_TRUE(t.has_value()) << "worker woke with no task after " << i << " frames";

        const auto [connectionID, sequence] = parseFrame(t->payload);
        ASSERT_TRUE(lastSequence.contains(connectionID)) << "unknown connection " << connectionID;

        EXPECT_EQ(sequence, lastSequence[connectionID] + 1)
            << "connection " << connectionID << " arrived out of order";
        lastSequence[connectionID] = sequence;
    }

    socketA.join();
    socketB.join();

    EXPECT_EQ(lastSequence[kConnA], kFramesPerSocket - 1);
    EXPECT_EQ(lastSequence[kConnB], kFramesPerSocket - 1);
}

TEST(TaskQueueSockets, KeepsTheTaskTypeEachReaderSent)
{
    TaskQueue q;
    std::thread socketA(runSocketReader, std::ref(q), kConnA, TYPE_TEXT, kFramesPerSocket);
    std::thread socketB(runSocketReader, std::ref(q), kConnB, TYPE_AUDIO, kFramesPerSocket);

    for (int i = 0; i < kTotalFrames; ++i)
    {
        const std::optional<Task> t = q.dequeueTask();
        ASSERT_TRUE(t.has_value()) << "worker woke with no task after " << i << " frames";

        const TASK_TYPE expected = (t->connectionID == kConnA) ? TYPE_TEXT : TYPE_AUDIO;
        EXPECT_EQ(t->type, expected) << "connection " << t->connectionID << " got the wrong type";
    }

    socketA.join();
    socketB.join();
}

// A connectionID-only check would miss a half-copied payload. Each reader
// sends a 64-byte block filled with its own ID, so a torn vector shows up as
// a wrong byte rather than a wrong count.
TEST(TaskQueueSockets, PayloadBytesSurviveTwoConcurrentReaders)
{
    TaskQueue q;

    const auto reader = [&q](int connectionID) {
        const std::vector<char> block(64, static_cast<char>(connectionID));
        for (int sequence = 0; sequence < kFramesPerSocket; ++sequence)
        {
            q.enqueueTask(TYPE_AUDIO, connectionID, block);
        }
    };

    std::thread socketA(reader, kConnA);
    std::thread socketB(reader, kConnB);

    for (int i = 0; i < kTotalFrames; ++i)
    {
        const std::optional<Task> t = q.dequeueTask();
        ASSERT_TRUE(t.has_value()) << "worker woke with no task after " << i << " frames";

        const std::vector<char> expected(64, static_cast<char>(t->connectionID));
        EXPECT_EQ(t->payload, expected) << "payload corrupted on frame " << i;
    }

    socketA.join();
    socketB.join();
}

TEST(TaskQueueSockets, WorkerBlocksUntilASocketDelivers)
{
    TaskQueue q;
    std::atomic<bool> returned{false};

    std::thread worker([&q, &returned] {
        const std::optional<Task> t = q.dequeueTask();
        returned.store(t.has_value());
    });

    std::this_thread::sleep_for(100ms);
    EXPECT_FALSE(returned.load()) << "dequeueTask returned before any frame arrived";

    q.enqueueTask(TYPE_TEXT, kConnA, bytes("first"));
    worker.join();
    EXPECT_TRUE(returned.load());
}

TEST(TaskQueueSockets, TerminateStopsTheWorkerWhileSocketsAreStillReading)
{
    TaskQueue q;
    std::atomic<bool> socketsShouldStop{false};
    std::atomic<int> drained{0};

    const auto reader = [&q, &socketsShouldStop](int connectionID) {
        int sequence = 0;
        while (!socketsShouldStop.load())
        {
            q.enqueueTask(TYPE_TEXT, connectionID, frameBytes(connectionID, sequence++));
            std::this_thread::sleep_for(1ms);
        }
    };

    std::thread socketA(reader, kConnA);
    std::thread socketB(reader, kConnB);
    std::thread worker([&q, &drained] {
        while (q.dequeueTask().has_value())
        {
            drained.fetch_add(1);
        }
    });

    std::this_thread::sleep_for(50ms);
    q.terminate();
    worker.join();

    socketsShouldStop.store(true);
    socketA.join();
    socketB.join();

    EXPECT_GT(drained.load(), 0) << "worker exited without processing anything";
}

// ===========================================================================
// Ordering
// ===========================================================================

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
        ASSERT_TRUE(t.has_value());
        EXPECT_EQ(t->connectionID, i) << "order broke at " << i;
    }
}

// ===========================================================================
// Payload
// ===========================================================================

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

// With a char* payload the consumer would be reading freed memory here.
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

// Audio frames are full of zero bytes, so nothing may treat payload as a
// C string.
TEST(TaskQueue, PreservesEmbeddedNullBytes)
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

// ===========================================================================
// terminate()
// ===========================================================================

TEST(TaskQueue, TerminateMakesDequeueReturnNullopt)
{
    TaskQueue q;
    q.terminate();

    EXPECT_FALSE(q.dequeueTask().has_value());
}

TEST(TaskQueue, TerminateWakesABlockedConsumer)
{
    TaskQueue q;
    std::atomic<bool> finished{false};

    std::thread consumer([&q, &finished] {
        const std::optional<Task> t = q.dequeueTask();
        EXPECT_FALSE(t.has_value());
        finished.store(true);
    });

    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(finished.load()) << "consumer returned before terminate()";

    q.terminate();
    consumer.join();
    EXPECT_TRUE(finished.load());
}

// This is what distinguishes notify_all() from notify_one(): with
// notify_one() three of the four workers stay parked and the join() hangs.
TEST(TaskQueue, TerminateWakesEveryBlockedConsumer)
{
    constexpr int kConsumers = 4;
    TaskQueue q;
    std::atomic<int> woken{0};

    std::vector<std::thread> consumers;
    for (int i = 0; i < kConsumers; ++i)
    {
        consumers.emplace_back([&q, &woken] {
            q.dequeueTask();
            woken.fetch_add(1);
        });
    }

    std::this_thread::sleep_for(100ms);
    q.terminate();

    for (std::thread & c : consumers)
    {
        c.join();
    }
    EXPECT_EQ(woken.load(), kConsumers);
}

// Documents current behaviour: dequeueTask() checks `stopped` before it looks
// at the queue, so a terminate() throws away whatever is still pending. For a
// chat server you probably want the backlog flushed to the sockets first - if
// you swap those two checks in dequeueTask(), this expectation flips.
TEST(TaskQueue, TerminateDiscardsQueuedTasks)
{
    TaskQueue q;
    q.enqueueTask(TYPE_TEXT, 1, bytes("unsent"));
    q.enqueueTask(TYPE_TEXT, 2, bytes("also unsent"));

    q.terminate();

    EXPECT_FALSE(q.dequeueTask().has_value());
}

TEST(TaskQueue, TerminateIsIdempotent)
{
    TaskQueue q;
    q.terminate();
    q.terminate();

    EXPECT_FALSE(q.dequeueTask().has_value());
}

TEST(TaskQueue, EnqueueAfterTerminateIsNotDelivered)
{
    TaskQueue q;
    q.terminate();
    q.enqueueTask(TYPE_TEXT, 1, bytes("too late"));

    EXPECT_FALSE(q.dequeueTask().has_value());
}

// ===========================================================================
// Input validation
// ===========================================================================

// Not a bug today - pinned down so that adding validation shows up as a
// failing test rather than a silent behaviour change.
TEST(TaskQueue, AcceptsOutOfRangeTaskType)
{
    TaskQueue q;
    q.enqueueTask(static_cast<TASK_TYPE>(99), 1, bytes("a"));

    const std::optional<Task> t = q.dequeueTask();
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(static_cast<int>(t->type), 99);
}

}  // namespace
