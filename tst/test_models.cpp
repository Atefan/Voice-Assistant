#include "Models.h"
#include "Wav.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
// whisper.cpp's sample: JFK, 11 s, 16 kHz mono 16-bit PCM
const std::string JFK_WAV = "/home/raspi-stef/whisper.cpp/samples/jfk.wav";

// same conversion as TaskHandler::bytesToFloat: int16 samples -> floats in [-1, 1]
std::vector<float> pcm16ToFloat(const std::vector<char>& bytes)
{
    std::vector<int16_t> samples(bytes.size() / sizeof(int16_t));
    std::memcpy(samples.data(), bytes.data(), bytes.size());

    std::vector<float> floats;
    for (int16_t s : samples)
        floats.push_back(s / 32768.0f);
    return floats;
}
}

// Loading whisper takes a few seconds, so the suite loads it once and every
// test in it shares the same STTModel.
class STTModelTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()    { stt = std::make_unique<STTModel>(); }
    static void TearDownTestSuite() { stt.reset(); }

    static std::unique_ptr<STTModel> stt;
};

std::unique_ptr<STTModel> STTModelTest::stt;

TEST_F(STTModelTest, TranscribesJfkSample)
{
    std::string text = stt->generate(pcm16ToFloat(readWavPcm16(JFK_WAV)));

    EXPECT_NE(text.find("ask not what your country can do for you"), std::string::npos) << "got: " << text;
}

TEST_F(STTModelTest, RejectsSamplesOutOfRange)
{
    std::vector<float> loud(16000, 0.0f);
    loud[100] = 2.0f;

    EXPECT_THROW(stt->generate(loud), std::runtime_error);
}

TEST_F(STTModelTest, RejectsNaN)
{
    std::vector<float> broken(16000, 0.0f);
    broken[100] = std::nanf("");

    EXPECT_THROW(stt->generate(broken), std::runtime_error);
}
