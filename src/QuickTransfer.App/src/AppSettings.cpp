#include "QuickTransfer/AppSettings.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>

namespace qt {

namespace {

std::filesystem::path appDataRoot() {
    if (const char* appData = std::getenv("APPDATA")) {
        return std::filesystem::path(appData) / "QuickTransfer";
    }
    return std::filesystem::current_path() / "QuickTransfer";
}

unsigned short portFromJson(const nlohmann::json& json, const char* key, unsigned short fallback) {
    const auto value = json.value(key, static_cast<int>(fallback));
    if (value <= 0 || value > 65535) {
        return fallback;
    }
    return static_cast<unsigned short>(value);
}

} // namespace

std::filesystem::path AppSettings::settingsPath() {
    return appDataRoot() / "settings.json";
}

std::filesystem::path AppSettings::defaultDestinationFolder() {
    if (const char* userProfile = std::getenv("USERPROFILE")) {
        return std::filesystem::path(userProfile) / "Downloads" / "QuickTransfer";
    }
    return std::filesystem::current_path() / "Received";
}

AppSettings AppSettings::load() {
    AppSettings settings;
    settings.destinationFolder = defaultDestinationFolder();

    const auto path = settingsPath();
    if (!std::filesystem::exists(path)) {
        return settings;
    }

    std::ifstream in(path);
    if (!in) {
        return settings;
    }

    nlohmann::json json;
    in >> json;

    const auto server = json.value("server", nlohmann::json::object());
    const auto client = json.value("client", nlohmann::json::object());
    const auto transfer = json.value("transfer", nlohmann::json::object());
    const auto autoSync = json.value("autoSync", nlohmann::json::object());
    const auto ui = json.value("ui", nlohmann::json::object());

    settings.listenPort = portFromJson(server, "listenPort", settings.listenPort);
    settings.restartListenOnFailure = server.value("restartListenOnFailure", settings.restartListenOnFailure);
    settings.lastHost = client.value("lastHost", settings.lastHost);
    settings.lastPort = portFromJson(client, "lastPort", settings.lastPort);
    settings.reconnectClientOnFailure = client.value("reconnectClientOnFailure", settings.reconnectClientOnFailure);
    settings.destinationFolder = transfer.value("destinationFolder", settings.destinationFolder.string());
    settings.overwriteExisting = transfer.value("overwriteExisting", settings.overwriteExisting);
    settings.autoSyncFolder = autoSync.value("folder", settings.autoSyncFolder.string());
    settings.autoSyncEnabled = autoSync.value("enabled", settings.autoSyncEnabled);
    settings.lastMode = ui.value("lastMode", settings.lastMode);
    return settings;
}

void AppSettings::save() const {
    const auto path = settingsPath();
    std::filesystem::create_directories(path.parent_path());

    nlohmann::json json = {
        {"server", {{"listenPort", listenPort}, {"restartListenOnFailure", restartListenOnFailure}}},
        {"client", {{"lastHost", lastHost}, {"lastPort", lastPort}, {"reconnectClientOnFailure", reconnectClientOnFailure}}},
        {"transfer", {{"destinationFolder", destinationFolder.string()}, {"overwriteExisting", overwriteExisting}}},
        {"autoSync", {{"folder", autoSyncFolder.string()}, {"enabled", autoSyncEnabled}}},
        {"ui", {{"lastMode", lastMode}}},
    };

    std::ofstream out(path);
    out << json.dump(2);
}

} // namespace qt
