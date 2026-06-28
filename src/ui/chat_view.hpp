#pragma once

#include <gtkmm.h>
#include <vector>
#include "models/message.hpp"

namespace AgoraUI {

class ChatView : public Gtk::Box {
public:
    ChatView();
    void set_messages(const std::vector<Message>& messages);
    void add_message(const Message& msg);
    void clear();
    void scroll_to_bottom();

private:
    Gtk::ScrolledWindow* scroll_;
    Gtk::ListBox* list_box_;
};

} // namespace AgoraUI
