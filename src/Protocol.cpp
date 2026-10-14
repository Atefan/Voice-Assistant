#include "Protocol.h"
#include "Errors.h"

#include <utility>


namespace
{
    // The only two values TASK_TYPE may legally hold. Add to this when the enum
    // grows, or an unknown type silently becomes a valid-looking Task.
    bool isKnownTaskType(uint32_t type)
    {
        return type == static_cast<uint32_t>(TYPE_TEXT)
            || type == static_cast<uint32_t>(TYPE_AUDIO);
    }
}


Task frameToTask(Frame frame, int connectionID)
{
    if (!isKnownTaskType(frame.type))
        throw RequestError("unknown task type " + std::to_string(frame.type));

    return Task{
        .type         = static_cast<TASK_TYPE>(frame.type),
        .connectionID = connectionID,
        .payload      = std::move(frame.payload)
    };
}


Frame replyToFrame(const std::string& reply)
{
    Frame frame;
    frame.type = FRAME_TYPE_REPLY;
    frame.payload.assign(reply.begin(), reply.end());
    return frame;
}
