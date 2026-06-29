#pragma once

#include <gtkmm.h>
#include "utils/config.hpp"
#include <vector>

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

    // System prompt
    Gtk::ComboBoxText* sp_combo_ = nullptr;
    Gtk::Entry* sp_title_ = nullptr;
    Gtk::TextView* sp_content_ = nullptr;
    Gtk::Entry* sp_prepend_ = nullptr;
    Gtk::Entry* sp_postpend_ = nullptr;
    Gtk::Label* sp_active_label_ = nullptr;

    void populate_sp_fields();
    void save_current_sp();
};

} // namespace AgoraUI
