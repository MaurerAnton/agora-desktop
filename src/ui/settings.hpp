#pragma once

#include <gtkmm.h>

namespace AgoraUI {

class SettingsDialog : public Gtk::Dialog {
public:
    SettingsDialog(Gtk::Window& parent);
    void load();
    void save();

private:
    Gtk::Notebook* notebook_ = nullptr;
    Gtk::Entry* model_entry_ = nullptr;
    Gtk::CheckButton* tor_toggle_ = nullptr;
    Gtk::Entry* tor_host_ = nullptr;
    Gtk::Entry* tor_port_ = nullptr;
    Gtk::CheckButton* stt_toggle_ = nullptr;
    Gtk::Entry* stt_endpoint_ = nullptr;
    Gtk::Entry* openai_key_ = nullptr;
    Gtk::Entry* anthropic_key_ = nullptr;
    Gtk::Entry* openai_base_ = nullptr;
};

} // namespace AgoraUI
