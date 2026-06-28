#include "ui/chat_view.hpp"

namespace AgoraUI {

ChatView::ChatView() : Gtk::Box(Gtk::Orientation::VERTICAL) {
    set_vexpand(true);
    set_hexpand(true);

    scroll_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll_->set_vexpand(true);

    list_box_ = Gtk::make_managed<Gtk::ListBox>();
    list_box_->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll_->set_child(*list_box_);

    append(*scroll_);
}

void ChatView::set_messages(const std::vector<Message>&) {}
void ChatView::add_message(const Message&) {}
void ChatView::clear() {
    while (auto* child = list_box_->get_first_child()) {
        list_box_->remove(*child);
    }
}
void ChatView::scroll_to_bottom() {
    auto adj = scroll_->get_vadjustment();
    adj->set_value(adj->get_upper());
}

} // namespace AgoraUI
