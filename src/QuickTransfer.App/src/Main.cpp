#include "MainFrame.h"

#include <wx/wx.h>

class QuickTransferApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new MainFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(QuickTransferApp);
