#include "ui/input_bar.hpp"

namespace AgoraUI {

InputBar::InputBar() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 4) {
    set_margin(6);
    set_valign(Gtk::Align::END);

    mic_btn_ = Gtk::make_managed<Gtk::Button>();
    mic_btn_->set_icon_name("microphone-symbolic");
    mic_btn_->set_has_frame(false);
    append(*mic_btn_);

    entry_ = Gtk::make_managed<Gtk::Entry>();
    entry_->set_placeholder_text("Type a message...");
    entry_->set_hexpand(true);
    append(*entry_);

    send_btn_ = Gtk::make_managed<Gtk::Button>();
    send_btn_->set_icon_name("send-symbolic");
    send_btn_->set_has_frame(false);
    append(*send_btn_);

    stop_btn_ = Gtk::make_managed<Gtk::Button>();
    stop_btn_->set_icon_name("process-stop-symbolic");
    stop_btn_->set_has_frame(false);
    stop_btn_->set_visible(false);
    append(*stop_btn_);
}

void InputBar::set_enabled(bool enabled) {
    entry_->set_sensitive(enabled);
    send_btn_->set_sensitive(enabled);
}

void InputBar::set_mic_active(bool active) {
    mic_btn_->set_icon_name(active ? "microphone-recording-symbolic" : "microphone-symbolic");
}

std::string InputBar::get_text() const { return entry_->get_text(); }
void InputBar::clear() { entry_->set_text(""); }

} // namespace AgoraUI
