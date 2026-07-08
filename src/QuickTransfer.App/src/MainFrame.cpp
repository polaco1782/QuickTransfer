#include "MainFrame.h"

#include <wx/dir.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/settings.h>
#include <wx/spinctrl.h>
#include <wx/stdpaths.h>

wxDEFINE_EVENT(wxEVT_QT_STATUS, wxThreadEvent);
wxDEFINE_EVENT(wxEVT_QT_LOG, wxThreadEvent);
wxDEFINE_EVENT(wxEVT_QT_TRANSFER, wxThreadEvent);
wxDEFINE_EVENT(wxEVT_QT_RECEIVED, wxThreadEvent);

namespace {

enum Ids {
    IdConnect = wxID_HIGHEST + 100,
    IdListen,
    IdDisconnect,
    IdSend,
    IdBrowseDestination,
    IdBrowseAutoSync,
    IdAutoSyncEnabled,
    IdAutoSyncTimer
};

wxString nowStamp() {
    return wxDateTime::Now().FormatISOTime();
}

unsigned short portValue(wxSpinCtrl* ctrl) {
    return static_cast<unsigned short>(ctrl->GetValue());
}

wxString percentText(std::uint64_t done, std::uint64_t total) {
    if (total == 0) {
        return "0%";
    }
    const auto percent = static_cast<unsigned int>((done * 100) / total);
    return wxString::Format("%u%%", percent);
}

void applyPageColour(wxWindow* window) {
    const auto background = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    window->SetBackgroundColour(background);
    for (auto node = window->GetChildren().GetFirst(); node; node = node->GetNext()) {
        auto* child = node->GetData();
        if (child->IsKindOf(wxCLASSINFO(wxPanel)) || child->IsKindOf(wxCLASSINFO(wxNotebook))) {
            applyPageColour(child);
        }
    }
}

} // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "QuickTransfer", wxDefaultPosition, wxSize(500, 320)),
      settings_(qt::AppSettings::load()),
      autoSyncTimer_(this, IdAutoSyncTimer) {
    SetIcon(wxICON(APP_ICON));
    buildUi();
    loadSettingsToUi();

    qt::PeerCallbacks callbacks;
    callbacks.onStatus = [this](qt::ConnectionStatus status, const std::string& message) {
        auto* event = new wxThreadEvent(wxEVT_QT_STATUS);
        event->SetInt(static_cast<int>(status));
        event->SetString(wxString::FromUTF8(message));
        wxQueueEvent(this, event);
    };
    callbacks.onLog = [this](const std::string& message) {
        auto* event = new wxThreadEvent(wxEVT_QT_LOG);
        event->SetString(wxString::FromUTF8(message));
        wxQueueEvent(this, event);
    };
    callbacks.onTransfer = [this](const std::string& name, const std::string& direction, std::uint64_t done, std::uint64_t total, const std::string& status) {
        auto* event = new wxThreadEvent(wxEVT_QT_TRANSFER);
        event->SetPayload(TransferEventData{
            wxString::FromUTF8(name),
            wxString::FromUTF8(direction),
            done,
            total,
            wxString::FromUTF8(status)});
        wxQueueEvent(this, event);
    };
    callbacks.onFileReceived = [this](const std::filesystem::path& path) {
        auto* event = new wxThreadEvent(wxEVT_QT_RECEIVED);
        event->SetString(wxString(path.wstring()));
        wxQueueEvent(this, event);
    };

    peer_ = std::make_unique<qt::PeerConnection>(std::move(callbacks));

    Bind(wxEVT_BUTTON, &MainFrame::onConnect, this, IdConnect);
    Bind(wxEVT_BUTTON, &MainFrame::onStartServer, this, IdListen);
    Bind(wxEVT_BUTTON, &MainFrame::onDisconnect, this, IdDisconnect);
    Bind(wxEVT_BUTTON, &MainFrame::onSendFile, this, IdSend);
    Bind(wxEVT_BUTTON, &MainFrame::onBrowseDestination, this, IdBrowseDestination);
    Bind(wxEVT_BUTTON, &MainFrame::onBrowseAutoSyncFolder, this, IdBrowseAutoSync);
    Bind(wxEVT_CHECKBOX, &MainFrame::onAutoSyncToggle, this, IdAutoSyncEnabled);
    Bind(wxEVT_TIMER, &MainFrame::onAutoSyncTimer, this, IdAutoSyncTimer);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::onClose, this);
    Bind(wxEVT_QT_STATUS, &MainFrame::onStatus, this);
    Bind(wxEVT_QT_LOG, &MainFrame::onLog, this);
    Bind(wxEVT_QT_TRANSFER, &MainFrame::onTransfer, this);
    Bind(wxEVT_QT_RECEIVED, &MainFrame::onReceived, this);
    updateAutoSyncTimer();
}

MainFrame::~MainFrame() = default;

void MainFrame::buildUi() {
    SetMinSize(wxSize(460, 320));
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* header = new wxPanel(this);
    header->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    auto* headerSizer = new wxBoxSizer(wxHORIZONTAL);
    headerSizer->Add(new wxStaticText(header, wxID_ANY, "Key:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    keyCtrl_ = new wxTextCtrl(header, wxID_ANY, "", wxDefaultPosition, wxSize(150, -1), wxTE_PASSWORD);
    headerSizer->Add(keyCtrl_, 0, wxRIGHT, 12);
    disconnectButton_ = new wxButton(header, IdDisconnect, "Disconnect");
    headerSizer->Add(disconnectButton_, 0, wxLEFT, 8);
    sendButton_ = new wxButton(header, IdSend, "Send File...");
    headerSizer->Add(sendButton_, 0, wxLEFT, 8);
    header->SetSizer(headerSizer);
    root->Add(header, 0, wxEXPAND | wxALL, 6);

    notebook_ = new wxNotebook(this, wxID_ANY);
    notebook_->AddPage(buildClientPage(notebook_), "Client");
    notebook_->AddPage(buildServerPage(notebook_), "Server");
    notebook_->AddPage(buildTransfersPage(notebook_), "Transfers");
    notebook_->AddPage(buildAutoSyncPage(notebook_), "AutoSync");
    notebook_->AddPage(buildSettingsPage(notebook_), "Settings");
    notebook_->AddPage(buildLogPage(notebook_), "Log");
    notebook_->AddPage(buildAboutPage(notebook_), "About");
    root->Add(notebook_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    SetSizer(root);
    applyPageColour(this);
    setConnectedUi(false);
}

wxPanel* MainFrame::buildClientPage(wxWindow* parent) {
    auto* panel = new wxPanel(parent);
    panel->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    auto* grid = new wxFlexGridSizer(2, 8, 8);
    grid->AddGrowableCol(1);

    grid->Add(new wxStaticText(panel, wxID_ANY, "Host:"), 0, wxALIGN_CENTER_VERTICAL);
    hostCtrl_ = new wxTextCtrl(panel, wxID_ANY);
    grid->Add(hostCtrl_, 1, wxEXPAND);

    grid->Add(new wxStaticText(panel, wxID_ANY, "Port:"), 0, wxALIGN_CENTER_VERTICAL);
    clientPortCtrl_ = new wxSpinCtrl(panel, wxID_ANY);
    clientPortCtrl_->SetRange(1, 65535);
    grid->Add(clientPortCtrl_, 0);

    grid->AddSpacer(1);
    connectButton_ = new wxButton(panel, IdConnect, "Connect");
    grid->Add(connectButton_, 0);

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(grid, 0, wxEXPAND | wxALL, 8);
    panel->SetSizer(root);
    return panel;
}

wxPanel* MainFrame::buildServerPage(wxWindow* parent) {
    auto* panel = new wxPanel(parent);
    panel->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    auto* grid = new wxFlexGridSizer(2, 8, 8);

    grid->Add(new wxStaticText(panel, wxID_ANY, "Listen port:"), 0, wxALIGN_CENTER_VERTICAL);
    serverPortCtrl_ = new wxSpinCtrl(panel, wxID_ANY);
    serverPortCtrl_->SetRange(1, 65535);
    grid->Add(serverPortCtrl_, 0);

    grid->AddSpacer(1);
    listenButton_ = new wxButton(panel, IdListen, "Start Listening");
    grid->Add(listenButton_, 0);

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(grid, 0, wxEXPAND | wxALL, 8);
    panel->SetSizer(root);
    return panel;
}

wxPanel* MainFrame::buildSettingsPage(wxWindow* parent) {
    auto* panel = new wxPanel(parent);
    panel->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* row = new wxBoxSizer(wxHORIZONTAL);

    row->Add(new wxStaticText(panel, wxID_ANY, "Destination folder:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    destinationCtrl_ = new wxTextCtrl(panel, wxID_ANY);
    row->Add(destinationCtrl_, 1, wxRIGHT, 6);
    row->Add(new wxButton(panel, IdBrowseDestination, "Browse..."), 0);
    root->Add(row, 0, wxEXPAND | wxALL, 6);

    overwriteCtrl_ = new wxCheckBox(panel, wxID_ANY, "Overwrite existing files");
    root->Add(overwriteCtrl_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

    restartListenCtrl_ = new wxCheckBox(panel, wxID_ANY, "Restart server listening after failed connection");
    root->Add(restartListenCtrl_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

    reconnectClientCtrl_ = new wxCheckBox(panel, wxID_ANY, "Reconnect client after dropped connection");
    root->Add(reconnectClientCtrl_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

    panel->SetSizer(root);
    return panel;
}

wxPanel* MainFrame::buildTransfersPage(wxWindow* parent) {
    auto* panel = new wxPanel(parent);
    panel->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    auto* root = new wxBoxSizer(wxVERTICAL);
    transferList_ = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    transferList_->AppendColumn("Name", wxLIST_FORMAT_LEFT, 180);
    transferList_->AppendColumn("Direction", wxLIST_FORMAT_LEFT, 70);
    transferList_->AppendColumn("Progress", wxLIST_FORMAT_LEFT, 70);
    transferList_->AppendColumn("Status", wxLIST_FORMAT_LEFT, 110);
    root->Add(transferList_, 1, wxEXPAND | wxALL, 6);
    panel->SetSizer(root);
    return panel;
}

wxPanel* MainFrame::buildAutoSyncPage(wxWindow* parent) {
    auto* panel = new wxPanel(parent);
    panel->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, "Folder:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    autoSyncFolderCtrl_ = new wxTextCtrl(panel, wxID_ANY);
    row->Add(autoSyncFolderCtrl_, 1, wxRIGHT, 6);
    row->Add(new wxButton(panel, IdBrowseAutoSync, "Browse..."), 0);
    root->Add(row, 0, wxEXPAND | wxALL, 6);

    autoSyncEnabledCtrl_ = new wxCheckBox(panel, IdAutoSyncEnabled, "Upload changed files automatically");
    root->Add(autoSyncEnabledCtrl_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

    autoSyncStatusText_ = new wxStaticText(panel, wxID_ANY, "AutoSync disabled");
    root->Add(autoSyncStatusText_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

    panel->SetSizer(root);
    return panel;
}

wxPanel* MainFrame::buildLogPage(wxWindow* parent) {
    auto* panel = new wxPanel(parent);
    panel->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    auto* root = new wxBoxSizer(wxVERTICAL);

    logCtrl_ = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
    root->Add(logCtrl_, 1, wxEXPAND | wxALL, 6);
    panel->SetSizer(root);
    return panel;
}

wxPanel* MainFrame::buildAboutPage(wxWindow* parent) {
    auto* panel = new wxPanel(parent);
    panel->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* copyright = new wxStaticText(panel, wxID_ANY, "2026 polaco software inc.");
    auto* repoUrl = new wxStaticText(panel, wxID_ANY, "https://github.com/polaco1782/QuickTransfer");
    root->AddStretchSpacer();
    root->Add(copyright, 0, wxALIGN_CENTER_HORIZONTAL);
    root->Add(repoUrl, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 6);
    root->AddStretchSpacer();
    panel->SetSizer(root);
    return panel;
}

void MainFrame::loadSettingsToUi() {
    hostCtrl_->SetValue(wxString::FromUTF8(settings_.lastHost));
    clientPortCtrl_->SetValue(settings_.lastPort);
    serverPortCtrl_->SetValue(settings_.listenPort);
    destinationCtrl_->SetValue(wxString(settings_.destinationFolder.wstring()));
    overwriteCtrl_->SetValue(settings_.overwriteExisting);
    restartListenCtrl_->SetValue(settings_.restartListenOnFailure);
    reconnectClientCtrl_->SetValue(settings_.reconnectClientOnFailure);
    autoSyncFolderCtrl_->SetValue(wxString(settings_.autoSyncFolder.wstring()));
    autoSyncEnabledCtrl_->SetValue(settings_.autoSyncEnabled);
    notebook_->SetSelection(settings_.lastMode == "server" ? 1 : 0);
}

void MainFrame::saveSettingsFromUi() {
    settings_.lastHost = hostCtrl_->GetValue().ToStdString();
    settings_.lastPort = portValue(clientPortCtrl_);
    settings_.listenPort = portValue(serverPortCtrl_);
    settings_.destinationFolder = destinationFolder();
    settings_.overwriteExisting = overwriteCtrl_->GetValue();
    settings_.restartListenOnFailure = restartListenCtrl_->GetValue();
    settings_.reconnectClientOnFailure = reconnectClientCtrl_->GetValue();
    settings_.autoSyncFolder = autoSyncFolder();
    settings_.autoSyncEnabled = autoSyncEnabledCtrl_->GetValue();
    settings_.lastMode = notebook_->GetSelection() == 1 ? "server" : "client";
    settings_.save();
}

bool MainFrame::validateKey() {
    if (keyCtrl_->GetValue().empty()) {
        wxMessageBox("Enter the same key on both machines before connecting.", "Missing key", wxOK | wxICON_WARNING, this);
        return false;
    }
    return true;
}

std::filesystem::path MainFrame::destinationFolder() const {
    return std::filesystem::path(destinationCtrl_->GetValue().ToStdWstring());
}

std::filesystem::path MainFrame::autoSyncFolder() const {
    return std::filesystem::path(autoSyncFolderCtrl_->GetValue().ToStdWstring());
}

void MainFrame::onConnect(wxCommandEvent&) {
    if (!validateKey()) {
        return;
    }
    saveSettingsFromUi();
    activeMode_ = "Client";
    updateWindowTitle("Connecting");
    peer_->startClient(settings_.lastHost, settings_.lastPort, keyCtrl_->GetValue().ToStdString(), settings_.destinationFolder, settings_.overwriteExisting, settings_.reconnectClientOnFailure);
}

void MainFrame::onStartServer(wxCommandEvent&) {
    if (!validateKey()) {
        return;
    }
    saveSettingsFromUi();
    activeMode_ = "Server";
    updateWindowTitle("Listening");
    peer_->startServer(settings_.listenPort, keyCtrl_->GetValue().ToStdString(), settings_.destinationFolder, settings_.overwriteExisting, settings_.restartListenOnFailure);
}

void MainFrame::onDisconnect(wxCommandEvent&) {
    peer_->disconnect();
    activeMode_.clear();
    setConnectedUi(false);
    updateWindowTitle("Disconnected");
}

void MainFrame::onSendFile(wxCommandEvent&) {
    wxFileDialog dialog(this, "Choose file to send", "", "", "All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    if (!peer_->sendFile(std::filesystem::path(dialog.GetPath().ToStdWstring()))) {
        wxMessageBox("Connect to a peer before sending a file.", "Not connected", wxOK | wxICON_INFORMATION, this);
    }
}

void MainFrame::onBrowseDestination(wxCommandEvent&) {
    wxDirDialog dialog(this, "Choose destination folder", destinationCtrl_->GetValue(), wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) {
        destinationCtrl_->SetValue(dialog.GetPath());
        saveSettingsFromUi();
    }
}

void MainFrame::onBrowseAutoSyncFolder(wxCommandEvent&) {
    wxDirDialog dialog(this, "Choose AutoSync folder", autoSyncFolderCtrl_->GetValue(), wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) {
        autoSyncFolderCtrl_->SetValue(dialog.GetPath());
        resetAutoSyncState();
        saveSettingsFromUi();
        updateAutoSyncTimer();
    }
}

void MainFrame::onAutoSyncToggle(wxCommandEvent&) {
    resetAutoSyncState();
    saveSettingsFromUi();
    updateAutoSyncTimer();
}

void MainFrame::onAutoSyncTimer(wxTimerEvent&) {
    scanAutoSyncFolder();
}

void MainFrame::onClose(wxCloseEvent& event) {
    saveSettingsFromUi();
    if (peer_) {
        peer_->disconnect();
    }
    event.Skip();
}

void MainFrame::onStatus(wxThreadEvent& event) {
    const auto status = static_cast<qt::ConnectionStatus>(event.GetInt());
    wxString titleStatus;
    switch (status) {
    case qt::ConnectionStatus::Listening:
        titleStatus = "Listening";
        break;
    case qt::ConnectionStatus::Connecting:
        titleStatus = "Connecting";
        break;
    case qt::ConnectionStatus::Connected:
        titleStatus = "Connected";
        break;
    case qt::ConnectionStatus::Error:
        titleStatus = "Error";
        break;
    case qt::ConnectionStatus::Disconnected:
    default:
        titleStatus = "Disconnected";
        break;
    }
    updateWindowTitle(titleStatus);

    const auto active = status == qt::ConnectionStatus::Listening ||
                        status == qt::ConnectionStatus::Connecting ||
                        status == qt::ConnectionStatus::Connected;
    connectButton_->Enable(!active);
    listenButton_->Enable(!active);
    disconnectButton_->Enable(active);
    sendButton_->Enable(status == qt::ConnectionStatus::Connected);

    if (status == qt::ConnectionStatus::Connected) {
        updateAutoSyncTimer();
        scanAutoSyncFolder();
    }
}

void MainFrame::onLog(wxThreadEvent& event) {
    appendLog(event.GetString());
}

void MainFrame::onTransfer(wxThreadEvent& event) {
    upsertTransfer(event.GetPayload<TransferEventData>());
}

void MainFrame::onReceived(wxThreadEvent& event) {
    appendLog("Received: " + event.GetString());
}

void MainFrame::appendLog(const wxString& text) {
    logCtrl_->AppendText("[" + nowStamp() + "] " + text + "\n");
}

void MainFrame::setConnectedUi(bool connected) {
    connectButton_->Enable(!connected);
    listenButton_->Enable(!connected);
    disconnectButton_->Enable(connected);
    sendButton_->Enable(connected);
}

void MainFrame::updateWindowTitle(const wxString& status) {
    if (activeMode_.empty() || status == "Disconnected") {
        SetTitle("QuickTransfer");
        return;
    }
    SetTitle("QuickTransfer - " + activeMode_ + " - " + status);
}

void MainFrame::upsertTransfer(const TransferEventData& data) {
    long row = -1;
    for (long i = 0; i < transferList_->GetItemCount(); ++i) {
        if (transferList_->GetItemText(i) == data.name && transferList_->GetItemText(i, 1) == data.direction) {
            row = i;
            break;
        }
    }
    if (row < 0) {
        row = transferList_->InsertItem(transferList_->GetItemCount(), data.name);
        transferList_->SetItem(row, 1, data.direction);
    }
    transferList_->SetItem(row, 2, percentText(data.done, data.total));
    transferList_->SetItem(row, 3, data.status);
}

void MainFrame::updateAutoSyncTimer() {
    if (!autoSyncEnabledCtrl_ || !autoSyncStatusText_) {
        return;
    }

    const auto enabled = autoSyncEnabledCtrl_->GetValue();
    const auto folder = autoSyncFolder();
    if (!enabled) {
        autoSyncTimer_.Stop();
        autoSyncStatusText_->SetLabel("AutoSync disabled");
        return;
    }
    if (folder.empty() || !std::filesystem::is_directory(folder)) {
        autoSyncTimer_.Stop();
        autoSyncStatusText_->SetLabel("Choose a folder to enable AutoSync");
        return;
    }

    autoSyncStatusText_->SetLabel("Watching for changes");
    if (!autoSyncTimer_.IsRunning()) {
        autoSyncTimer_.Start(2000);
    }
}

void MainFrame::resetAutoSyncState() {
    autoSyncKnown_.clear();
    autoSyncPending_.clear();
}

void MainFrame::scanAutoSyncFolder() {
    const auto folder = autoSyncFolder();
    if (!autoSyncEnabledCtrl_->GetValue() || folder.empty() || !std::filesystem::is_directory(folder)) {
        updateAutoSyncTimer();
        return;
    }

    std::error_code ec;
    std::size_t sent = 0;
    std::size_t waiting = 0;
    std::size_t queued = 0;
    const auto connected = peer_ && peer_->isConnected();

    for (std::filesystem::recursive_directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end;
         it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec)) {
            ec.clear();
            continue;
        }

        const auto path = it->path();
        if (path.extension() == ".qtpartial") {
            continue;
        }

        const auto writeTime = it->last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        const auto size = it->file_size(ec);
        if (ec) {
            ec.clear();
            continue;
        }

        const auto key = path.wstring();
        const AutoSyncFileState state{writeTime, size};
        const auto known = autoSyncKnown_.find(key);
        if (known != autoSyncKnown_.end() && known->second.writeTime == state.writeTime && known->second.size == state.size) {
            continue;
        }

        const auto pending = autoSyncPending_.find(key);
        if (pending == autoSyncPending_.end() || pending->second.writeTime != state.writeTime || pending->second.size != state.size) {
            autoSyncPending_[key] = state;
            ++waiting;
            continue;
        }

        if (!connected) {
            ++queued;
            continue;
        }

        if (peer_->sendFile(path)) {
            autoSyncKnown_[key] = state;
            autoSyncPending_.erase(key);
            ++sent;
            appendLog("AutoSync uploaded: " + wxString(path.wstring()));
        } else {
            ++queued;
        }
    }

    if (sent > 0) {
        autoSyncStatusText_->SetLabel(wxString::Format("Uploaded %zu changed file(s)", sent));
    } else if (waiting > 0) {
        autoSyncStatusText_->SetLabel(wxString::Format("Waiting for %zu file(s) to settle", waiting));
    } else if (queued > 0) {
        autoSyncStatusText_->SetLabel(wxString::Format("Queued %zu changed file(s)", queued));
    } else if (!connected) {
        autoSyncStatusText_->SetLabel("Watching all the time; changes will queue");
    } else {
        autoSyncStatusText_->SetLabel("Watching for changes");
    }
}
