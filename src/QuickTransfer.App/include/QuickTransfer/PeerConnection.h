#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace qt {

enum class ConnectionStatus {
    Disconnected,
    Listening,
    Connecting,
    Connected,
    Error
};

struct PeerCallbacks {
    std::function<void(ConnectionStatus, const std::string&)> onStatus;
    std::function<void(const std::string&)> onLog;
    std::function<void(const std::string&, const std::string&, std::uint64_t, std::uint64_t, const std::string&)> onTransfer;
    std::function<void(const std::filesystem::path&)> onFileReceived;
};

class PeerConnection {
public:
    explicit PeerConnection(PeerCallbacks callbacks);
    ~PeerConnection();

    PeerConnection(const PeerConnection&) = delete;
    PeerConnection& operator=(const PeerConnection&) = delete;

    void startServer(unsigned short port, std::string passphrase, std::filesystem::path destinationFolder, bool overwriteExisting, bool restartListenOnFailure);
    void startClient(std::string host, unsigned short port, std::string passphrase, std::filesystem::path destinationFolder, bool overwriteExisting, bool reconnectOnFailure);
    void disconnect();
    bool sendFile(const std::filesystem::path& filePath);
    bool isConnected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace qt
