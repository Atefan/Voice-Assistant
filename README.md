# Voice Assistant

A local-first voice assistant server written in C++20. Speech-to-text and LLM
inference both run on the device — a Raspberry Pi 4 — with no cloud service and
no network dependency beyond the client socket.

Clients connect over TCP and send either text or raw audio. The server
transcribes audio with [whisper.cpp](https://github.com/ggerganov/whisper.cpp),
answers with a quantised Qwen3-1.7B via [llama.cpp](https://github.com/ggerganov/llama.cpp),
and replies with JSON.

## Architecture

Three layers, each ignorant of the ones around it, which is what makes them
testable in isolation:

```
Sockets.cpp     bytes on a file descriptor      knows nothing about Task
Protocol.cpp    Frame  <-> Task                 knows nothing about fds or models
TaskHandler     Task   -> answer string         knows nothing about the wire
```

A listener thread blocks on `recv`, turns each completed frame into a `Task`
and pushes it onto a `TaskQueue`. The main thread drains that queue and runs
inference. The split exists because the queue is a fan-in point: the socket is
one producer, and a local microphone or wake-word path can be another, while
the single `llama_context` requires exactly one consumer.

```
listener thread ──┐
                  ├──▶ TaskQueue ──▶ main thread ──▶ whisper / llama ──▶ reply
(future: mic)   ──┘   mutex + condvar     single consumer
```

## Wire protocol

Length-prefixed binary framing, because TCP is a byte stream with no message
boundaries:

| Bytes | Field | Notes |
|-------|-------|-------|
| 0–3 | `type` | uint32, network byte order |
| 4–7 | `length` | uint32, network byte order |
| 8…  | `payload` | `length` bytes |

`type` is `0` for text (UTF-8 JSON) or `1` for audio (little-endian 16-bit PCM).
Replies use `0xFFFFFFFF` — not `0`, since that is already a valid request type.
Frames larger than 16 MiB are rejected before allocation, so a corrupt length
field cannot be used to request gigabytes.

Text requests carry a message list that is rendered into Qwen's ChatML template,
and may include a [GBNF](https://github.com/ggerganov/llama.cpp/blob/master/grammars/README.md)
grammar to constrain the model's output to a required JSON shape.

## Design notes

- **Typed exceptions over error codes.** `SocketError`, `RequestError` and
  `ModelError` separate "the network broke" from "the client sent nonsense"
  from "inference failed". A bad request gets a JSON error reply and the
  connection stays open; only a socket failure ends it.
- **RAII for descriptors.** `FdGuard` is move-only with a deleted copy
  constructor, so a descriptor cannot be closed twice — which matters because
  the kernel recycles fd numbers immediately, and a double close can land on an
  unrelated connection.
- **Partial transfers are the default case.** `send` and `recv` are both free to
  move fewer bytes than asked; every transfer loops until complete, and a close
  partway through a frame is treated as an error rather than a short read.
- **`shutdown`, not `close`, to stop a blocked thread.** A `std::stop_token`
  cannot interrupt a blocking syscall. `shutdown()` makes a parked `recv()`
  return 0; `close()` does not wake it and frees the fd number for reuse.

## Building

Requires CMake ≥ 3.16, a C++20 compiler, and llama.cpp and whisper.cpp
installed under `~/ai-libs`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
./build/talk_server
```

Model paths are currently hardcoded in `inc/Models.h`.

## Tests

GoogleTest, fetched automatically by CMake:

```bash
ctest --test-dir build --output-on-failure
```

`notes.txt` holds the working notes: debugging recipes, gdb and
ThreadSanitizer invocations, and socket diagnostics.

## Status

Working end to end — a client can connect, send a question, and get a generated
answer back, with the connection staying open across multiple requests.

Not done yet:

- No client implementation; the server is driven by a short Python script
  (see `notes.txt`)
- One connection per run — the listening socket closes after `accept()`
- The audio path works in `TaskHandler` but has not been driven over the socket
  with real recorded audio
- No unit tests yet for the socket and protocol layers
