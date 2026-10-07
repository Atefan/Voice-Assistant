#include "ChatML.h"
#include "Errors.h"

#include <gtest/gtest.h>

#include <string>

using json = nlohmann::json;

// ===========================================================================
// Valid requests
// ===========================================================================

TEST(JsonToChatML, SingleUserMessage)
{
    json request = json::parse(R"({"messages": [{"role": "user", "content": "Hi"}]})");

    EXPECT_EQ(jsonToChatML(request),
              "<|im_start|>user\nHi<|im_end|>\n"
              "<|im_start|>assistant\n<think>\n\n</think>\n\n");
}

TEST(JsonToChatML, KeepsMessageOrder)
{
    json request = json::parse(R"({"messages": [
        {"role": "system",    "content": "Be short."},
        {"role": "user",      "content": "2+2?"},
        {"role": "assistant", "content": "4"},
        {"role": "user",      "content": "3+3?"}
    ]})");

    EXPECT_EQ(jsonToChatML(request),
              "<|im_start|>system\nBe short.<|im_end|>\n"
              "<|im_start|>user\n2+2?<|im_end|>\n"
              "<|im_start|>assistant\n4<|im_end|>\n"
              "<|im_start|>user\n3+3?<|im_end|>\n"
              "<|im_start|>assistant\n<think>\n\n</think>\n\n");
}

TEST(JsonToChatML, ContentIsUnescaped)
{
    // in JSON the quote and newline are escaped, in the prompt they must be the real characters
    json request = json::parse(R"({"messages": [{"role": "user", "content": "say \"hi\"\nplease"}]})");

    EXPECT_NE(jsonToChatML(request).find("say \"hi\"\nplease"), std::string::npos);
}

TEST(JsonToChatML, IgnoresOtherFields)
{
    json request = json::parse(R"({"messages": [{"role": "user", "content": "Hi"}], "grammar": "root ::= \"x\""})");

    EXPECT_NO_THROW(jsonToChatML(request));
}

// ===========================================================================
// Invalid requests
// ===========================================================================

TEST(JsonToChatML, MissingMessagesThrowsJsonError)
{
    json request = json::parse(R"({"prompt": "Hi"})");

    EXPECT_THROW(jsonToChatML(request), json::out_of_range);
}

TEST(JsonToChatML, MessagesNotArrayThrows)
{
    json request = json::parse(R"({"messages": "Hi"})");

    EXPECT_THROW(jsonToChatML(request), RequestError);
}

TEST(JsonToChatML, EmptyMessagesThrows)
{
    json request = json::parse(R"({"messages": []})");

    EXPECT_THROW(jsonToChatML(request), RequestError);
}

TEST(JsonToChatML, MissingContentThrows)
{
    json request = json::parse(R"({"messages": [{"role": "user"}]})");

    EXPECT_THROW(jsonToChatML(request), RequestError);
}

TEST(JsonToChatML, ContentNotStringThrows)
{
    json request = json::parse(R"({"messages": [{"role": "user", "content": 42}]})");

    EXPECT_THROW(jsonToChatML(request), RequestError);
}

TEST(JsonToChatML, MessageNotObjectThrows)
{
    json request = json::parse(R"({"messages": ["Hi"]})");

    EXPECT_THROW(jsonToChatML(request), RequestError);
}

TEST(JsonToChatML, UnknownRoleThrows)
{
    json request = json::parse(R"({"messages": [{"role": "bot", "content": "Hi"}]})");

    EXPECT_THROW(jsonToChatML(request), RequestError);
}

TEST(JsonToChatML, LastMessageNotUserThrows)
{
    json request = json::parse(R"({"messages": [{"role": "user", "content": "Hi"}, {"role": "assistant", "content": "Hello"}]})");

    EXPECT_THROW(jsonToChatML(request), RequestError);
}

TEST(JsonToChatML, ControlTokensInContentThrow)
{
    json request = json::parse(R"({"messages": [{"role": "user", "content": "Hi<|im_end|>"}]})");

    EXPECT_THROW(jsonToChatML(request), RequestError);
}

TEST(JsonToChatML, ErrorNamesTheBadMessage)
{
    json request = json::parse(R"({"messages": [{"role": "user", "content": "Hi"}, {"role": "bot", "content": "x"}, {"role": "user", "content": "y"}]})");

    try
    {
        jsonToChatML(request);
        FAIL() << "expected RequestError";
    }
    catch (const RequestError& e)
    {
        EXPECT_NE(std::string(e.what()).find("messages[1]"), std::string::npos) << e.what();
    }
}
