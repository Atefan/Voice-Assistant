#include "Wav.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

// WAV stores numbers little-endian: the lowest byte comes first
static uint32_t readLE(const std::vector<char>& bytes, size_t pos, int nBytes)
{
    uint32_t value = 0;
    for(int i = 0; i < nBytes; ++i)
        value |= uint32_t(static_cast<unsigned char>(bytes[pos + i])) << (8 * i);
    return value;
}

std::vector<char> readWavPcm16(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if(!file)
        throw std::runtime_error("cannot open " + path);

    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    if(bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
        throw std::runtime_error(path + " is not a WAV file");

    // after the 12-byte header the file is a list of chunks:
    // 4-byte name, 4-byte body size, then the body
    bool formatChecked = false;
    size_t pos = 12;
    while(pos + 8 <= bytes.size())
    {
        const char* id = bytes.data() + pos;
        const uint32_t size = readLE(bytes, pos + 4, 4);
        const size_t body = pos + 8;

        if(body + size > bytes.size())
            throw std::runtime_error(path + ": chunk runs past the end of the file");

        if(std::memcmp(id, "fmt ", 4) == 0)
        {
            if(size < 16)
                throw std::runtime_error(path + ": fmt chunk too small");

            const uint32_t audioFormat   = readLE(bytes, body, 2);       // 1 = PCM
            const uint32_t channels      = readLE(bytes, body + 2, 2);
            const uint32_t sampleRate    = readLE(bytes, body + 4, 4);
            const uint32_t bitsPerSample = readLE(bytes, body + 14, 2);

            if(audioFormat != 1 || channels != 1 || sampleRate != 16000 || bitsPerSample != 16)
                throw std::runtime_error(path + ": expected 16 kHz mono 16-bit PCM");

            formatChecked = true;
        }
        else if(std::memcmp(id, "data", 4) == 0)
        {
            if(!formatChecked)
                throw std::runtime_error(path + ": data chunk before fmt chunk");

            return std::vector<char>(bytes.begin() + body, bytes.begin() + body + size);
        }

        // chunk bodies are padded to an even number of bytes
        pos = body + size + (size % 2);
    }

    throw std::runtime_error(path + ": no data chunk");
}
