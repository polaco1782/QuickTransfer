#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace qt {

enum class FrameType : std::uint16_t {
    Hello = 1,
    HelloAck = 2,
    AuthOk = 3,
    Ping = 10,
    Pong = 11,
    FileOffer = 20,
    FileAccept = 21,
    FileChunk = 22,
    FileDone = 23,
    FileCancel = 24,
    Error = 99
};

struct FrameHeader {
    std::uint32_t magic = 0x51544631; // QTF1
    std::uint16_t version = 1;
    FrameType type = FrameType::Error;
    std::uint64_t payloadSize = 0;
};

struct Frame {
    FrameType type = FrameType::Error;
    std::vector<std::uint8_t> payload;
};

std::array<std::uint8_t, 16> encodeHeader(const FrameHeader& header);
FrameHeader decodeHeader(const std::array<std::uint8_t, 16>& data);

std::vector<std::uint8_t> bytesFromString(const std::string& value);
std::string stringFromBytes(const std::vector<std::uint8_t>& value);
std::string toHex(const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> fromHex(const std::string& hex);

} // namespace qt
