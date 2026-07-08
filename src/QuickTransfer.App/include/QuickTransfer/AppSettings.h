#pragma once

#include <filesystem>
#include <string>

namespace qt {

struct AppSettings {
    unsigned short listenPort = 7777;
    bool restartListenOnFailure = true;
    std::string lastHost = "127.0.0.1";
    unsigned short lastPort = 7777;
    bool reconnectClientOnFailure = true;
    std::filesystem::path destinationFolder;
    bool overwriteExisting = false;
    std::filesystem::path autoSyncFolder;
    bool autoSyncEnabled = false;
    std::string lastMode = "client";

    static AppSettings load();
    void save() const;

    static std::filesystem::path settingsPath();
    static std::filesystem::path defaultDestinationFolder();
};

} // namespace qt
