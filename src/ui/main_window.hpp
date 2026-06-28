#pragma once

#include <gtkmm.h>
#include <adwaita.h>
#include <memory>
#include <vector>
#include <string>
#include <thread>
#include <mutex>

#include "db/database.hpp"
#include "api/provider.hpp"
#include "audio/recorder.hpp"
#include "models/message.hpp"

namespace AgoraUI {

class ChatView;
class InputBar;

class MainWindow : public Gtk::ApplicationWindow {
public:
    MainWindow();
    ~MainWindow();

    static MainWindow* create();

    void set_database(std::shared_ptr<Database> db);
    void set_http_client(std::shared_ptr<HttpClient> client);

private:
    // Core components
    std::shared_ptr<Database> db_;
    std::shared_ptr<HttpClient> http_;
    std::unique_ptr<Provider> current_provider_;
    std::unique_ptr<AudioRecorder> recorder_;

    // Active conversation
    std::string active_conversation_id_;
    std::vector<Message> messages_;

    // Thread safety for streaming
    std::mutex messages_mutex_;
    std::thread generation_thread_;

    // UI widgets
    Gtk::Box* main_box_ = nullptr;
    Gtk::Box* content_box_ = nullptr;

    // Chat area
    Gtk::ScrolledWindow* chat_scroll_ = nullptr;
    Gtk::ListBox* message_list_box_ = nullptr;

    // Input bar
    Gtk::Entry* message_entry_ = nullptr;
    Gtk::Button* send_button_ = nullptr;
    Gtk::Button* mic_button_ = nullptr;
    Gtk::Button* stop_button_ = nullptr;
    Gtk::Spinner* send_spinner_ = nullptr;
    Gtk::Revealer* mic_revealer_ = nullptr;

    // Sidebar
    Gtk::ListBox* conversation_list_box_ = nullptr;
    Gtk::Stack* sidebar_stack_ = nullptr;
    Gtk::SearchEntry* search_entry_ = nullptr;
    Gtk::ListBox* pinned_list_box_ = nullptr;
    Gtk::Button* new_chat_button_ = nullptr;

    // Thread view (pinned messages)
    Gtk::Revealer* thread_revealer_ = nullptr;
    Gtk::ListBox* thread_list_box_ = nullptr;
    Gtk::Label* thread_title_ = nullptr;

    // Header bar
    Gtk::HeaderBar* header_bar_ = nullptr;
    Gtk::Label* title_label_ = nullptr;
    Gtk::MenuButton* menu_button_ = nullptr;

    // Status
    bool streaming_ = false;
    bool mic_recording_ = false;
    std::string streaming_text_;
    std::string streaming_thoughts_;

    // Methods
    void setup_ui();
    void setup_sidebar();
    void setup_chat_area();
    void setup_input_bar();
    void setup_signals();

    // Conversation management
    void load_conversations();
    void select_conversation(const std::string& conv_id);
    void new_conversation();
    void delete_conversation(const std::string& conv_id);
    void rename_conversation(const std::string& conv_id, const std::string& title);

    // Message handling
    void send_message();
    void send_message_async(const std::string& text);
    void regenerate_message(const std::string& msg_id);
    void edit_message(const std::string& msg_id, const std::string& new_text);
    void render_messages();
    void add_message_to_list(const Message& msg, bool at_end = true);
    void update_streaming_message();
    void clear_streaming();

    // Pinned messages
    void toggle_pin_message(const std::string& msg_id);
    void show_thread_view(const std::string& msg_id);
    void hide_thread_view();
    void load_pinned_messages();

    // Microphone
    void toggle_microphone();
    void on_stt_text(const std::string& text, bool final);

    // Provider management
    void setup_provider();
    ProviderConfig build_provider_config();

    // Settings
    void show_settings();
    void show_conversation_menu(int x, int y, const std::string& conv_id);
};

} // namespace AgoraUI
