#include "QuickTransfer/Protocol.h"

#include <charconv>
#include <stdexcept>
#include <system_error>

namespace qt {

namespace {

void put16(std::array<std::uint8_t, 16>& out, std::size_t offset, std::uint16_t value) {
    out[offset] = static_cast<std::uint8_t>((value >> 8) & 0xff);
    out[offset + 1] = static_cast<std::uint8_t>(value & 0xff);
}

void put32(std::array<std::uint8_t, 16>& out, std::size_t offset, std::uint32_t value) {
    for (int i = 3; i >= 0; --i) {
        out[offset + (3 - i)] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xff);
    }
}

void put64(std::array<std::uint8_t, 16>& out, std::size_t offset, std::uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        out[offset + (7 - i)] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xff);
    }
}

std::uint16_t get16(const std::array<std::uint8_t, 16>& in, std::size_t offset) {
    return static_cast<std::uint16_t>((in[offset] << 8) | in[offset + 1]);
}

std::uint32_t get32(const std::array<std::uint8_t, 16>& in, std::size_t offset) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value = (value << 8) | in[offset + i];
    }
    return value;
}

std::uint64_t get64(const std::array<std::uint8_t, 16>& in, std::size_t offset) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | in[offset + i];
    }
    return value;
}

} // namespace

std::array<std::uint8_t, 16> encodeHeader(const FrameHeader& header) {
    std::array<std::uint8_t, 16> out{};
    put32(out, 0, header.magic);
    put16(out, 4, header.version);
    put16(out, 6, static_cast<std::uint16_t>(header.type));
    put64(out, 8, header.payloadSize);
    return out;
}

FrameHeader decodeHeader(const std::array<std::uint8_t, 16>& data) {
    FrameHeader header;
    header.magic = get32(data, 0);
    header.version = get16(data, 4);
    header.type = static_cast<FrameType>(get16(data, 6));
    header.payloadSize = get64(data, 8);
    if (header.magic != 0x51544631 || header.version != 1) {
        throw std::runtime_error("invalid QuickTransfer frame header");
    }
    return header;
}

std::vector<std::uint8_t> bytesFromString(const std::string& value) {
    return {value.begin(), value.end()};
}

std::string stringFromBytes(const std::vector<std::uint8_t>& value) {
    return {value.begin(), value.end()};
}

std::string toHex(const std::vector<std::uint8_t>& bytes) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        out.push_back(hex[(byte >> 4) & 0xf]);
        out.push_back(hex[byte & 0xf]);
    }
    return out;
}

std::vector<std::uint8_t> fromHex(const std::string& hex) {
    if ((hex.size() % 2) != 0) {
        throw std::runtime_error("invalid hex string");
    }
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        unsigned int value = 0;
        auto [ptr, ec] = std::from_chars(hex.data() + i, hex.data() + i + 2, value, 16);
        if (ec != std::errc{} || ptr != hex.data() + i + 2) {
            throw std::runtime_error("invalid hex string");
        }
        out.push_back(static_cast<std::uint8_t>(value));
    }
    return out;
}

} // namespace qt
