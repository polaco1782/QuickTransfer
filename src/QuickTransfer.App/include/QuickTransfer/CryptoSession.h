#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace qt {

class CryptoSession {
public:
    static std::vector<std::uint8_t> randomBytes(std::size_t count);
    static CryptoSession derive(const std::string& passphrase, std::span<const std::uint8_t> salt);

    std::vector<std::uint8_t> encrypt(std::span<const std::uint8_t> plain) const;
    std::vector<std::uint8_t> decrypt(std::span<const std::uint8_t> sealed) const;

private:
    explicit CryptoSession(std::vector<std::uint8_t> key);

    std::vector<std::uint8_t> key_;
};

} // namespace qt
