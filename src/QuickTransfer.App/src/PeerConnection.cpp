#include "QuickTransfer/PeerConnection.h"

#include "QuickTransfer/CryptoSession.h"
#include "QuickTransfer/Protocol.h"

#include <asio.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace qt {

namespace {

constexpr std::uint64_t MaxPayloadSize = 32ull * 1024ull * 1024ull;
constexpr std::size_t ChunkSize = 256 * 1024;

std::string baseNameOnly(const std::filesystem::path& path) {
    auto name = path.filename().wstring();
    for (auto& ch : name) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|') {
            ch = L'_';
        }
    }
    if (name.empty() || name == L"." || name == L"..") {
        name = L"received-file";
    }
    return std::filesystem::path(name).string();
}

std::filesystem::path uniqueDestination(const std::filesystem::path& folder, const std::string& fileName, bool overwrite) {
    std::filesystem::create_directories(folder);
    auto candidate = folder / std::filesystem::path(fileName).filename();
    if (overwrite || !std::filesystem::exists(candidate)) {
        return candidate;
    }

    const auto stem = candidate.stem().wstring();
    const auto extension = candidate.extension().wstring();
    for (int index = 1; index < 10000; ++index) {
        auto next = folder / (stem + L" (" + std::to_wstring(index) + L")" + extension);
        if (!std::filesystem::exists(next)) {
            return next;
        }
    }
    throw std::runtime_error("could not choose a unique destination filename");
}

std::vector<std::uint8_t> combineSalt(const std::vector<std::uint8_t>& salt,
                                      const std::vector<std::uint8_t>& clientNonce,
                                      const std::vector<std::uint8_t>& serverNonce) {
    std::vector<std::uint8_t> combined;
    combined.reserve(salt.size() + clientNonce.size() + serverNonce.size());
    combined.insert(combined.end(), salt.begin(), salt.end());
    combined.insert(combined.end(), clientNonce.begin(), clientNonce.end());
    combined.insert(combined.end(), serverNonce.begin(), serverNonce.end());
    return combined;
}

} // namespace

struct PeerConnection::Impl {
    explicit Impl(PeerCallbacks cb)
        : callbacks(std::move(cb)),
          socket(io) {
    }

    ~Impl() {
        disconnect();
    }

    PeerCallbacks callbacks;
    asio::io_context io;
    asio::ip::tcp::socket socket;
    std::unique_ptr<asio::ip::tcp::acceptor> acceptor;
    std::optional<CryptoSession> crypto;
    std::mutex sendMutex;
    std::mutex sendWorkersMutex;
    std::thread worker;
    std::thread keepalive;
    std::vector<std::thread> sendWorkers;
    std::atomic_bool connected = false;
    std::atomic_bool stopping = false;
    std::filesystem::path destinationFolder;
    bool overwriteExisting = false;
    bool restartListenOnFailure = true;
    bool reconnectClientOnFailure = true;

    struct ReceiveFile {
        std::string name;
        std::uint64_t total = 0;
        std::uint64_t received = 0;
        std::filesystem::path finalPath;
        std::filesystem::path tempPath;
        std::ofstream stream;
    };

    std::optional<ReceiveFile> receiveFile;

    void log(const std::string& message) {
        if (callbacks.onLog) {
            callbacks.onLog(message);
        }
    }

    void status(ConnectionStatus status, const std::string& message) {
        if (callbacks.onStatus) {
            callbacks.onStatus(status, message);
        }
        log(message);
    }

    void transfer(const std::string& name, const std::string& direction, std::uint64_t done, std::uint64_t total, const std::string& statusText) {
        if (callbacks.onTransfer) {
            callbacks.onTransfer(name, direction, done, total, statusText);
        }
    }

    void disconnect() {
        stopping = true;
        connected = false;
        std::error_code ignored;
        if (acceptor) {
            acceptor->close(ignored);
        }
        socket.cancel(ignored);
        socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
        socket.close(ignored);

        {
            std::scoped_lock lock(sendWorkersMutex);
            for (auto& thread : sendWorkers) {
                if (thread.joinable()) {
                    thread.join();
                }
            }
            sendWorkers.clear();
        }

        if (worker.joinable()) {
            worker.join();
        }
        if (keepalive.joinable()) {
            keepalive.join();
        }

        if (receiveFile) {
            receiveFile->stream.close();
            std::filesystem::remove(receiveFile->tempPath, ignored);
            receiveFile.reset();
        }
        crypto.reset();
    }

    void startServer(unsigned short port, std::string passphrase, std::filesystem::path destination, bool overwrite, bool restartOnFailure) {
        disconnect();
        stopping = false;
        destinationFolder = std::move(destination);
        overwriteExisting = overwrite;
        restartListenOnFailure = restartOnFailure;
        worker = std::thread([this, port, passphrase = std::move(passphrase)]() mutable {
            while (!stopping) {
                try {
                    crypto.reset();
                    status(ConnectionStatus::Listening, "Listening on port " + std::to_string(port));
                    acceptor = std::make_unique<asio::ip::tcp::acceptor>(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port));
                    socket = asio::ip::tcp::socket(io);
                    acceptor->accept(socket);
                    acceptor.reset();
                    handshakeServer(passphrase);
                    connected = true;
					status(ConnectionStatus::Connected, "Peer connected, " + socket.remote_endpoint().address().to_string() + ":" + std::to_string(socket.remote_endpoint().port()));
                    startKeepalive();
                    readLoop();
                } catch (const std::exception& ex) {
                    if (!stopping) {
                        status(ConnectionStatus::Error, ex.what());
                    }
                }

                cleanupConnectionAttempt();
                if (!stopping && !restartListenOnFailure) {
                    break;
                }
                if (!stopping) {
                    status(ConnectionStatus::Listening, "Restarting listener");
                }
            }
            connected = false;
            if (!stopping) {
                status(ConnectionStatus::Disconnected, "Disconnected");
            }
        });
    }

    void startClient(std::string host, unsigned short port, std::string passphrase, std::filesystem::path destination, bool overwrite, bool reconnectOnFailure) {
        disconnect();
        stopping = false;
        destinationFolder = std::move(destination);
        overwriteExisting = overwrite;
        reconnectClientOnFailure = reconnectOnFailure;
        worker = std::thread([this, host = std::move(host), port, passphrase = std::move(passphrase)]() mutable {
            while (!stopping) {
                try {
                    crypto.reset();
                    status(ConnectionStatus::Connecting, "Connecting to " + host + ":" + std::to_string(port));
                    asio::ip::tcp::resolver resolver(io);
                    auto endpoints = resolver.resolve(host, std::to_string(port));
                    socket = asio::ip::tcp::socket(io);
                    asio::connect(socket, endpoints);
                    handshakeClient(passphrase);
                    connected = true;
                    status(ConnectionStatus::Connected, "Connected to peer");
                    startKeepalive();
                    readLoop();
                } catch (const std::exception& ex) {
                    if (!stopping) {
                        status(ConnectionStatus::Error, ex.what());
                    }
                }

                cleanupConnectionAttempt();
                if (!stopping && !reconnectClientOnFailure) {
                    break;
                }
                if (!stopping) {
                    status(ConnectionStatus::Connecting, "Reconnecting to peer");
                    for (int i = 0; i < 2 && !stopping; ++i) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
            }
            connected = false;
            if (!stopping) {
                status(ConnectionStatus::Disconnected, "Disconnected");
            }
        });
    }

    bool sendFile(const std::filesystem::path& filePath) {
        if (!connected || !std::filesystem::is_regular_file(filePath)) {
            return false;
        }

        std::scoped_lock workersLock(sendWorkersMutex);
        sendWorkers.emplace_back([this, filePath]() {
            try {
                const auto fileSize = std::filesystem::file_size(filePath);
                const auto displayName = baseNameOnly(filePath);
                nlohmann::json offer = {{"name", displayName}, {"size", fileSize}};
                sendEncrypted(FrameType::FileOffer, bytesFromString(offer.dump()));

                std::ifstream in(filePath, std::ios::binary);
                if (!in) {
                    throw std::runtime_error("could not open file for reading");
                }

                std::vector<std::uint8_t> buffer(ChunkSize);
                std::uint64_t sent = 0;
                transfer(displayName, "Send", sent, fileSize, "Sending");
                while (in && !stopping) {
                    in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
                    const auto got = static_cast<std::size_t>(in.gcount());
                    if (got == 0) {
                        break;
                    }
                    sendEncrypted(FrameType::FileChunk, std::span<const std::uint8_t>(buffer.data(), got));
                    sent += got;
                    transfer(displayName, "Send", sent, fileSize, "Sending");
                }

                sendEncrypted(FrameType::FileDone, std::span<const std::uint8_t>{});
                transfer(displayName, "Send", sent, fileSize, "Done");
            } catch (const std::exception& ex) {
                log(std::string("send failed: ") + ex.what());
            }
        });
        return true;
    }

    void startKeepalive() {
        keepalive = std::thread([this]() {
            while (!stopping && connected) {
                for (int i = 0; i < 8 && !stopping && connected; ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                if (!stopping && connected) {
                    try {
                        sendEncrypted(FrameType::Ping, std::span<const std::uint8_t>{});
                    } catch (...) {
                        disconnectSocketOnly();
                        return;
                    }
                }
            }
        });
    }

    void cleanupConnectionAttempt() {
        connected = false;
        std::error_code ignored;
        if (acceptor) {
            acceptor->close(ignored);
            acceptor.reset();
        }
        socket.cancel(ignored);
        socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
        socket.close(ignored);
        crypto.reset();
        if (keepalive.joinable()) {
            keepalive.join();
        }
        if (receiveFile) {
            receiveFile->stream.close();
            std::filesystem::remove(receiveFile->tempPath, ignored);
            receiveFile.reset();
        }
    }

    void readLoop() {
        while (!stopping) {
            auto frame = readEncrypted();
            handleFrame(frame);
        }
    }

    void handleFrame(const Frame& frame) {
        switch (frame.type) {
        case FrameType::Ping:
            sendEncrypted(FrameType::Pong, std::span<const std::uint8_t>{});
            break;
        case FrameType::Pong:
            break;
        case FrameType::FileOffer:
            beginReceive(frame.payload);
            break;
        case FrameType::FileChunk:
            receiveChunk(frame.payload);
            break;
        case FrameType::FileDone:
            finishReceive();
            break;
        case FrameType::FileCancel:
            cancelReceive("Canceled");
            break;
        default:
            break;
        }
    }

    void beginReceive(const std::vector<std::uint8_t>& payload) {
        auto offer = nlohmann::json::parse(stringFromBytes(payload));
        auto name = baseNameOnly(offer.value("name", "received-file"));
        auto size = offer.value("size", std::uint64_t{0});
        auto finalPath = uniqueDestination(destinationFolder, name, overwriteExisting);
        auto tempPath = finalPath;
        tempPath += ".qtpartial";

        receiveFile.emplace();
        receiveFile->name = name;
        receiveFile->total = size;
        receiveFile->finalPath = finalPath;
        receiveFile->tempPath = tempPath;
        receiveFile->stream.open(tempPath, std::ios::binary | std::ios::trunc);
        if (!receiveFile->stream) {
            receiveFile.reset();
            throw std::runtime_error("could not open destination file");
        }

        transfer(name, "Receive", 0, size, "Receiving");
        sendEncrypted(FrameType::FileAccept, bytesFromString(name));
    }

    void receiveChunk(const std::vector<std::uint8_t>& payload) {
        if (!receiveFile) {
            throw std::runtime_error("received file chunk without an active file");
        }
        receiveFile->stream.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        if (!receiveFile->stream) {
            throw std::runtime_error("failed writing destination file");
        }
        receiveFile->received += payload.size();
        transfer(receiveFile->name, "Receive", receiveFile->received, receiveFile->total, "Receiving");
    }

    void finishReceive() {
        if (!receiveFile) {
            return;
        }
        receiveFile->stream.close();
        std::filesystem::rename(receiveFile->tempPath, receiveFile->finalPath);
        transfer(receiveFile->name, "Receive", receiveFile->received, receiveFile->total, "Done");
        if (callbacks.onFileReceived) {
            callbacks.onFileReceived(receiveFile->finalPath);
        }
        receiveFile.reset();
    }

    void cancelReceive(const std::string& text) {
        if (!receiveFile) {
            return;
        }
        std::error_code ignored;
        receiveFile->stream.close();
        std::filesystem::remove(receiveFile->tempPath, ignored);
        transfer(receiveFile->name, "Receive", receiveFile->received, receiveFile->total, text);
        receiveFile.reset();
    }

    void handshakeClient(const std::string& passphrase) {
        const auto clientNonce = CryptoSession::randomBytes(16);
        nlohmann::json hello = {{"nonce", toHex(clientNonce)}};
        writeFrame({FrameType::Hello, bytesFromString(hello.dump())});

        const auto ackFrame = readPlainFrame();
        if (ackFrame.type != FrameType::HelloAck) {
            throw std::runtime_error("unexpected handshake response");
        }
        auto ack = nlohmann::json::parse(stringFromBytes(ackFrame.payload));
        auto serverNonce = fromHex(ack.value("nonce", ""));
        auto salt = fromHex(ack.value("salt", ""));
        auto combined = combineSalt(salt, clientNonce, serverNonce);
        crypto = CryptoSession::derive(passphrase, combined);
        sendEncrypted(FrameType::AuthOk, bytesFromString("ok"));

        const auto ok = readEncrypted();
        if (ok.type != FrameType::AuthOk) {
            throw std::runtime_error("authentication failed");
        }
    }

    void handshakeServer(const std::string& passphrase) {
        const auto helloFrame = readPlainFrame();
        if (helloFrame.type != FrameType::Hello) {
            throw std::runtime_error("unexpected handshake request");
        }
        auto hello = nlohmann::json::parse(stringFromBytes(helloFrame.payload));
        auto clientNonce = fromHex(hello.value("nonce", ""));
        auto serverNonce = CryptoSession::randomBytes(16);
        auto salt = CryptoSession::randomBytes(16);

        nlohmann::json ack = {{"nonce", toHex(serverNonce)}, {"salt", toHex(salt)}};
        writeFrame({FrameType::HelloAck, bytesFromString(ack.dump())});

        auto combined = combineSalt(salt, clientNonce, serverNonce);
        crypto = CryptoSession::derive(passphrase, combined);

        const auto auth = readEncrypted();
        if (auth.type != FrameType::AuthOk) {
            throw std::runtime_error("authentication failed");
        }
        sendEncrypted(FrameType::AuthOk, bytesFromString("ok"));
    }

    void sendEncrypted(FrameType type, std::span<const std::uint8_t> payload) {
        if (!crypto) {
            throw std::runtime_error("encrypted session is not ready");
        }
        writeFrame({type, crypto->encrypt(payload)});
    }

    Frame readEncrypted() {
        if (!crypto) {
            throw std::runtime_error("encrypted session is not ready");
        }
        auto frame = readPlainFrame();
        frame.payload = crypto->decrypt(frame.payload);
        return frame;
    }

    void writeFrame(const Frame& frame) {
        std::scoped_lock lock(sendMutex);
        FrameHeader header;
        header.type = frame.type;
        header.payloadSize = frame.payload.size();
        const auto headerBytes = encodeHeader(header);
        asio::write(socket, asio::buffer(headerBytes));
        if (!frame.payload.empty()) {
            asio::write(socket, asio::buffer(frame.payload));
        }
    }

    Frame readPlainFrame() {
        std::array<std::uint8_t, 16> headerBytes{};
        asio::read(socket, asio::buffer(headerBytes));
        const auto header = decodeHeader(headerBytes);
        if (header.payloadSize > MaxPayloadSize) {
            throw std::runtime_error("incoming payload is too large");
        }

        Frame frame;
        frame.type = header.type;
        frame.payload.resize(static_cast<std::size_t>(header.payloadSize));
        if (!frame.payload.empty()) {
            asio::read(socket, asio::buffer(frame.payload));
        }
        return frame;
    }

    void disconnectSocketOnly() {
        connected = false;
        std::error_code ignored;
        socket.cancel(ignored);
        socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
        socket.close(ignored);
    }
};

PeerConnection::PeerConnection(PeerCallbacks callbacks)
    : impl_(std::make_unique<Impl>(std::move(callbacks))) {
}

PeerConnection::~PeerConnection() = default;

void PeerConnection::startServer(unsigned short port, std::string passphrase, std::filesystem::path destinationFolder, bool overwriteExisting, bool restartListenOnFailure) {
    impl_->startServer(port, std::move(passphrase), std::move(destinationFolder), overwriteExisting, restartListenOnFailure);
}

void PeerConnection::startClient(std::string host, unsigned short port, std::string passphrase, std::filesystem::path destinationFolder, bool overwriteExisting, bool reconnectOnFailure) {
    impl_->startClient(std::move(host), port, std::move(passphrase), std::move(destinationFolder), overwriteExisting, reconnectOnFailure);
}

void PeerConnection::disconnect() {
    impl_->disconnect();
}

bool PeerConnection::sendFile(const std::filesystem::path& filePath) {
    return impl_->sendFile(filePath);
}

bool PeerConnection::isConnected() const {
    return impl_->connected;
}

} // namespace qt
