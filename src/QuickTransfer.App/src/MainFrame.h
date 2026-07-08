#pragma once

#include <QuickTransfer/AppSettings.h>
#include <QuickTransfer/PeerConnection.h>

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/spinctrl.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>

struct TransferEventData {
    wxString name;
    wxString direction;
    std::uint64_t done = 0;
    std::uint64_t total = 0;
    wxString status;
};

wxDECLARE_EVENT(wxEVT_QT_STATUS, wxThreadEvent);
wxDECLARE_EVENT(wxEVT_QT_LOG, wxThreadEvent);
wxDECLARE_EVENT(wxEVT_QT_TRANSFER, wxThreadEvent);
wxDECLARE_EVENT(wxEVT_QT_RECEIVED, wxThreadEvent);

class MainFrame final : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

private:
    void buildUi();
    wxPanel* buildClientPage(wxWindow* parent);
    wxPanel* buildServerPage(wxWindow* parent);
    wxPanel* buildSettingsPage(wxWindow* parent);
    wxPanel* buildTransfersPage(wxWindow* parent);
    wxPanel* buildAutoSyncPage(wxWindow* parent);
    wxPanel* buildLogPage(wxWindow* parent);
    wxPanel* buildAboutPage(wxWindow* parent);

    void loadSettingsToUi();
    void saveSettingsFromUi();
    bool validateKey();
    std::filesystem::path destinationFolder() const;
    std::filesystem::path autoSyncFolder() const;

    void onConnect(wxCommandEvent&);
    void onStartServer(wxCommandEvent&);
    void onDisconnect(wxCommandEvent&);
    void onSendFile(wxCommandEvent&);
    void onBrowseDestination(wxCommandEvent&);
    void onBrowseAutoSyncFolder(wxCommandEvent&);
    void onAutoSyncToggle(wxCommandEvent&);
    void onAutoSyncTimer(wxTimerEvent&);
    void onClose(wxCloseEvent&);

    void onStatus(wxThreadEvent& event);
    void onLog(wxThreadEvent& event);
    void onTransfer(wxThreadEvent& event);
    void onReceived(wxThreadEvent& event);

    void appendLog(const wxString& text);
    void setConnectedUi(bool connected);
    void updateWindowTitle(const wxString& status);
    void upsertTransfer(const TransferEventData& data);
    void updateAutoSyncTimer();
    void resetAutoSyncState();
    void scanAutoSyncFolder();

    struct AutoSyncFileState {
        std::filesystem::file_time_type writeTime;
        std::uintmax_t size = 0;
    };

    qt::AppSettings settings_;
    std::unique_ptr<qt::PeerConnection> peer_;
    wxString activeMode_;
    wxTimer autoSyncTimer_;
    std::unordered_map<std::wstring, AutoSyncFileState> autoSyncKnown_;
    std::unordered_map<std::wstring, AutoSyncFileState> autoSyncPending_;

    wxNotebook* notebook_ = nullptr;
    wxTextCtrl* keyCtrl_ = nullptr;

    wxTextCtrl* hostCtrl_ = nullptr;
    wxSpinCtrl* clientPortCtrl_ = nullptr;
    wxSpinCtrl* serverPortCtrl_ = nullptr;
    wxButton* connectButton_ = nullptr;
    wxButton* listenButton_ = nullptr;
    wxButton* disconnectButton_ = nullptr;
    wxButton* sendButton_ = nullptr;

    wxTextCtrl* destinationCtrl_ = nullptr;
    wxCheckBox* overwriteCtrl_ = nullptr;
    wxCheckBox* restartListenCtrl_ = nullptr;
    wxCheckBox* reconnectClientCtrl_ = nullptr;
    wxListCtrl* transferList_ = nullptr;
    wxTextCtrl* autoSyncFolderCtrl_ = nullptr;
    wxCheckBox* autoSyncEnabledCtrl_ = nullptr;
    wxStaticText* autoSyncStatusText_ = nullptr;
    wxTextCtrl* logCtrl_ = nullptr;
};
