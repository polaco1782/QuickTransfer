#include "QuickTransfer/CryptoSession.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <memory>
#include <stdexcept>
#include <utility>

namespace qt {

namespace {

using CtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

void requireOpenSsl(int result, const char* message) {
    if (result != 1) {
        throw std::runtime_error(message);
    }
}

} // namespace

CryptoSession::CryptoSession(std::vector<std::uint8_t> key)
    : key_(std::move(key)) {
}

std::vector<std::uint8_t> CryptoSession::randomBytes(std::size_t count) {
    std::vector<std::uint8_t> data(count);
    requireOpenSsl(RAND_bytes(data.data(), static_cast<int>(data.size())), "OpenSSL random generation failed");
    return data;
}

CryptoSession CryptoSession::derive(const std::string& passphrase, std::span<const std::uint8_t> salt) {
    std::vector<std::uint8_t> key(32);
    requireOpenSsl(PKCS5_PBKDF2_HMAC(passphrase.c_str(),
                                      static_cast<int>(passphrase.size()),
                                      salt.data(),
                                      static_cast<int>(salt.size()),
                                      150000,
                                      EVP_sha256(),
                                      static_cast<int>(key.size()),
                                      key.data()),
                   "OpenSSL key derivation failed");
    return CryptoSession(std::move(key));
}

std::vector<std::uint8_t> CryptoSession::encrypt(std::span<const std::uint8_t> plain) const {
    auto nonce = randomBytes(12);
    std::vector<std::uint8_t> cipher(plain.size());
    std::array<std::uint8_t, 16> tag{};

    CtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx) {
        throw std::runtime_error("OpenSSL cipher context allocation failed");
    }

    requireOpenSsl(EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr), "OpenSSL encrypt init failed");
    requireOpenSsl(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr), "OpenSSL nonce setup failed");
    requireOpenSsl(EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key_.data(), nonce.data()), "OpenSSL encrypt key setup failed");

    int written = 0;
    if (!plain.empty()) {
        requireOpenSsl(EVP_EncryptUpdate(ctx.get(), cipher.data(), &written, plain.data(), static_cast<int>(plain.size())), "OpenSSL encrypt failed");
    }

    int finalWritten = 0;
    requireOpenSsl(EVP_EncryptFinal_ex(ctx.get(), cipher.data() + written, &finalWritten), "OpenSSL encrypt final failed");
    cipher.resize(static_cast<std::size_t>(written + finalWritten));
    requireOpenSsl(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size()), tag.data()), "OpenSSL tag generation failed");

    std::vector<std::uint8_t> sealed;
    sealed.reserve(nonce.size() + cipher.size() + tag.size());
    sealed.insert(sealed.end(), nonce.begin(), nonce.end());
    sealed.insert(sealed.end(), cipher.begin(), cipher.end());
    sealed.insert(sealed.end(), tag.begin(), tag.end());
    return sealed;
}

std::vector<std::uint8_t> CryptoSession::decrypt(std::span<const std::uint8_t> sealed) const {
    if (sealed.size() < 28) {
        throw std::runtime_error("encrypted payload is too small");
    }

    auto nonce = sealed.subspan(0, 12);
    auto cipher = sealed.subspan(12, sealed.size() - 28);
    auto tag = sealed.subspan(sealed.size() - 16, 16);
    std::vector<std::uint8_t> plain(cipher.size());

    CtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx) {
        throw std::runtime_error("OpenSSL cipher context allocation failed");
    }

    requireOpenSsl(EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr), "OpenSSL decrypt init failed");
    requireOpenSsl(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr), "OpenSSL nonce setup failed");
    requireOpenSsl(EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key_.data(), nonce.data()), "OpenSSL decrypt key setup failed");

    int written = 0;
    if (!cipher.empty()) {
        requireOpenSsl(EVP_DecryptUpdate(ctx.get(), plain.data(), &written, cipher.data(), static_cast<int>(cipher.size())), "OpenSSL decrypt failed");
    }
    requireOpenSsl(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size()), const_cast<std::uint8_t*>(tag.data())), "OpenSSL tag setup failed");

    int finalWritten = 0;
    requireOpenSsl(EVP_DecryptFinal_ex(ctx.get(), plain.data() + written, &finalWritten), "wrong key or corrupted encrypted payload");
    plain.resize(static_cast<std::size_t>(written + finalWritten));
    return plain;
}

} // namespace qt
