#pragma once

#include <gtkmm.h>
#include <functional>

namespace AgoraUI {

class InputBar : public Gtk::Box {
public:
    InputBar();

    using SendCallback = std::function<void(const std::string& text)>;
    using MicCallback = std::function<void()>;

    void set_on_send(SendCallback cb) { on_send_ = std::move(cb); }
    void set_on_mic(MicCallback cb) { on_mic_ = std::move(cb); }
    void set_enabled(bool enabled);
    void set_mic_active(bool active);
    std::string get_text() const;
    void clear();

private:
    Gtk::Entry* entry_;
    Gtk::Button* send_btn_;
    Gtk::Button* mic_btn_;
    Gtk::Button* stop_btn_;
    SendCallback on_send_;
    MicCallback on_mic_;
};

} // namespace AgoraUI
