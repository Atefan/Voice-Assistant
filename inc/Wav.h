#ifndef SRC_WAV_H
#define SRC_WAV_H

#include <string>
#include <vector>

// Reads a 16 kHz, mono, 16-bit PCM WAV file and returns its raw samples
// (the body of the "data" chunk) as bytes, ready to use as a Task payload.
// Throws std::runtime_error if the file can't be read or has another format.
std::vector<char> readWavPcm16(const std::string& path);

#endif
