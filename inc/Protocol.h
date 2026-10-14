#ifndef SRC_PROTOCOL_H
#define SRC_PROTOCOL_H

#include "Sockets.h"
#include "Tasks.h"

#include <string>

// The seam between the wire and the work.
//
//   Sockets.h   bytes on an fd          - knows nothing about Task
//   Protocol.h  Frame <-> Task          - knows nothing about fds or models  <- here
//   TaskHandler Task  -> answer string  - knows nothing about the wire
//
// Note what is deliberately NOT here: turning a Task into a prompt. That is
// jsonToChatML() in ChatML.h, called from TaskHandler::handleText(). Moving it
// would leave two files deciding what a prompt looks like.


// Validates the frame's type and hands back the Task the queue wants.
// Throws RequestError if the type is not a TASK_TYPE - which matters, because
// the type field is attacker-controlled bytes and casting an arbitrary uint32
// straight into the enum is undefined behaviour.
//
// Takes the Frame by value so the payload can be moved rather than copied:
// an audio payload is large and this sits on the hot path.
Task frameToTask(Frame frame, int connectionID);

// Wraps a handler reply (always a JSON string) in a reply frame.
Frame replyToFrame(const std::string& reply);

#endif
