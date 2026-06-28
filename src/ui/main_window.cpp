#include "ui/main_window.hpp"
#include "utils/config.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <algorithm>

namespace AgoraUI {

MainWindow::MainWindow()
    : Gtk::ApplicationWindow() {
    setup_ui();
    setup_signals();

    recorder_ = std::make_unique<AudioRecorder>();
}

MainWindow::~MainWindow() {
    if (recorder_ && recorder_->is_recording()) {
        recorder_->stop();
    }
    if (generation_thread_.joinable()) {
        generation_thread_.detach();
    }
}

MainWindow* MainWindow::create() {
    return new MainWindow();
}

void MainWindow::set_database(std::shared_ptr<Database> db) {
    db_ = std::move(db);
    load_conversations();
}

void MainWindow::set_http_client(std::shared_ptr<HttpClient> client) {
    http_ = std::move(client);
}

// --- UI Setup ---

void MainWindow::setup_ui() {
    set_title("Agora");
    set_default_size(360, 640);  // Mobile-friendly default size
    set_size_request(300, 400);

    // Header bar
    header_bar_ = Gtk::make_managed<Gtk::HeaderBar>();
    header_bar_->set_show_title_buttons(true);
    set_titlebar(*header_bar_);

    title_label_ = Gtk::make_managed<Gtk::Label>("Agora");
    title_label_->set_ellipsize(Pango::EllipsizeMode::END);
    header_bar_->set_title_widget(*title_label_);

    // New chat button
    new_chat_button_ = Gtk::make_managed<Gtk::Button>();
    new_chat_button_->set_icon_name("document-new-symbolic");
    new_chat_button_->set_tooltip_text("New Chat");
    header_bar_->pack_start(*new_chat_button_);

    // Menu button
    menu_button_ = Gtk::make_managed<Gtk::MenuButton>();
    menu_button_->set_icon_name("open-menu-symbolic");
    header_bar_->pack_end(*menu_button_);

    // Main layout: horizontal split
    main_box_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
    set_child(*main_box_);

    setup_sidebar();
    setup_chat_area();
}

void MainWindow::setup_sidebar() {
    // Sidebar container
    auto sidebar_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    sidebar_box->set_size_request(280, -1);

    // Search entry
    search_entry_ = Gtk::make_managed<Gtk::SearchEntry>();
    search_entry_->set_placeholder_text("Search conversations...");
    sidebar_box->append(*search_entry_);

    // Stack for sidebar content
    sidebar_stack_ = Gtk::make_managed<Gtk::Stack>();
    sidebar_box->append(*sidebar_stack_);

    // Conversations page
    auto conv_page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    auto conv_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    conversation_list_box_ = Gtk::make_managed<Gtk::ListBox>();
    conversation_list_box_->set_selection_mode(Gtk::SelectionMode::SINGLE);
    conv_scroll->set_child(*conversation_list_box_);
    conv_page->append(*conv_scroll);
    sidebar_stack_->add(*conv_page, "conversations", "Conversations");

    // Thread/pinned page
    auto thread_page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    thread_title_ = Gtk::make_managed<Gtk::Label>("Pinned Messages");
    thread_title_->set_halign(Gtk::Align::CENTER);
    thread_title_->set_margin(8);
    thread_page->append(*thread_title_);

    auto thread_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    thread_list_box_ = Gtk::make_managed<Gtk::ListBox>();
    thread_scroll->set_child(*thread_list_box_);
    thread_page->append(*thread_scroll);

    auto back_btn = Gtk::make_managed<Gtk::Button>("Back");
    thread_page->append(*back_btn);
    sidebar_stack_->add(*thread_page, "thread", "Thread");

    sidebar_stack_->set_visible_child("conversations");
    main_box_->append(*sidebar_box);
}

void MainWindow::setup_chat_area() {
    content_box_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    content_box_->set_hexpand(true);

    // Chat messages area
    chat_scroll_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    chat_scroll_->set_vexpand(true);
    chat_scroll_->set_hexpand(true);

    message_list_box_ = Gtk::make_managed<Gtk::ListBox>();
    message_list_box_->set_selection_mode(Gtk::SelectionMode::NONE);
    chat_scroll_->set_child(*message_list_box_);

    content_box_->append(*chat_scroll_);

    // Input bar
    setup_input_bar();

    main_box_->append(*content_box_);
}

void MainWindow::setup_input_bar() {
    auto input_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    input_box->set_margin(6);
    input_box->set_valign(Gtk::Align::END);

    // Microphone button
    mic_button_ = Gtk::make_managed<Gtk::Button>();
    mic_button_->set_icon_name("microphone-symbolic");
    mic_button_->set_has_frame(false);
    input_box->append(*mic_button_);

    // Message entry
    message_entry_ = Gtk::make_managed<Gtk::Entry>();
    message_entry_->set_placeholder_text("Type a message...");
    message_entry_->set_hexpand(true);
    message_entry_->set_has_frame(true);
    input_box->append(*message_entry_);

    // Spinner (shown while loading)
    send_spinner_ = Gtk::make_managed<Gtk::Spinner>();
    send_spinner_->set_visible(false);
    input_box->append(*send_spinner_);

    // Send button
    send_button_ = Gtk::make_managed<Gtk::Button>();
    send_button_->set_icon_name("send-symbolic");
    send_button_->set_has_frame(false);
    input_box->append(*send_button_);

    // Stop button (hidden unless streaming)
    stop_button_ = Gtk::make_managed<Gtk::Button>();
    stop_button_->set_icon_name("process-stop-symbolic");
    stop_button_->set_has_frame(false);
    stop_button_->set_visible(false);
    input_box->append(*stop_button_);

    content_box_->append(*input_box);
}

// --- Signals ---

void MainWindow::setup_signals() {
    // Send message on Enter
    message_entry_->signal_activate().connect([this]() {
        if (!streaming_) send_message();
    });

    // Send button
    send_button_->signal_clicked().connect([this]() {
        if (!streaming_) send_message();
    });

    // Stop button
    stop_button_->signal_clicked().connect([this]() {
        clear_streaming();
    });

    // Microphone button
    mic_button_->signal_clicked().connect([this]() {
        toggle_microphone();
    });

    // Conversation selection
    conversation_list_box_->signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (!row) return;
        std::string conv_id = static_cast<Gtk::Label*>(row->get_child())->get_text();
        // Extract actual ID from label (format: title\npreview)
        auto text = static_cast<Gtk::Label*>(row->get_child())->get_text();
        select_conversation(conv_id);
    });

    // Search
    search_entry_->signal_search_changed().connect([this]() {
        load_conversations();  // Re-filter
    });

    // New chat
    new_chat_button_->signal_clicked().connect([this]() {
        new_conversation();
    });
}

// --- Conversation Management ---

void MainWindow::load_conversations() {
    if (!db_) return;

    // Clear current list
    while (auto* row = conversation_list_box_->get_first_child()) {
        conversation_list_box_->remove(*row);
    }

    auto query = search_entry_ ? search_entry_->get_text() : "";
    auto conversations = db_->get_all_conversations();

    for (auto& conv : conversations) {
        if (!query.empty()) {
            auto lower_title = conv.title;
            auto lower_query = query.lowercase();
            std::transform(lower_title.begin(), lower_title.end(), lower_title.begin(), ::tolower);
            if (lower_title.find(lower_query) == std::string::npos) continue;
        }

        auto row = Gtk::make_managed<Gtk::ListBoxRow>();

        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        box->set_margin(4);

        auto title = Gtk::make_managed<Gtk::Label>(conv.title);
        title->set_halign(Gtk::Align::START);
        title->set_ellipsize(Pango::EllipsizeMode::END);
        box->append(*title);

        // Store conversation ID as widget name
        row->set_name(conv.id);
        row->set_child(*box);

        conversation_list_box_->append(*row);
    }
}

void MainWindow::select_conversation(const std::string& conv_id) {
    active_conversation_id_ = conv_id;
    load_pinned_messages();
    render_messages();
}

void MainWindow::new_conversation() {
    std::string id = ""; // Generate UUID in production
    {
        std::ostringstream oss;
        auto now = std::chrono::system_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        oss << std::hex << us;
        id = oss.str();
    }

    Conversation conv;
    conv.id = id;
    conv.title = "New Chat";
    conv.last_updated = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto& config = Config::instance();
    conv.system_prompt_id = config.active_system_prompt_id;
    conv.model_id = config.selected_model;

    db_->upsert_conversation(conv);
    load_conversations();
    select_conversation(id);
}

void MainWindow::delete_conversation(const std::string& conv_id) {
    if (!db_) return;
    db_->delete_conversation(conv_id);
    load_conversations();
    if (active_conversation_id_ == conv_id) {
        active_conversation_id_.clear();
        render_messages();
    }
}

void MainWindow::rename_conversation(const std::string& conv_id, const std::string& title) {
    if (!db_) return;
    auto conv = db_->get_conversation(conv_id);
    conv.title = title;
    db_->upsert_conversation(conv);
    load_conversations();
}

// --- Message Handling ---

void MainWindow::send_message() {
    auto text = message_entry_->get_text();
    if (text.empty()) return;

    message_entry_->set_text("");
    send_message_async(text);
}

void MainWindow::send_message_async(const std::string& text) {
    if (!db_ || active_conversation_id_.empty()) {
        new_conversation();
        if (active_conversation_id_.empty()) return;
    }

    // Create user message
    std::string msg_id;
    {
        std::ostringstream oss;
        auto now = std::chrono::system_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        oss << "msg_" << std::hex << us;
        msg_id = oss.str();
    }

    Message user_msg;
    user_msg.id = msg_id;
    user_msg.conversation_id = active_conversation_id_;
    user_msg.text = text;
    user_msg.participant = Participant::USER;
    user_msg.status = MessageStatus::SUCCESS;
    user_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    db_->upsert_message(user_msg);

    {
        std::lock_guard<std::mutex> lock(messages_mutex_);
        messages_.push_back(user_msg);
    }

    render_messages();

    // Setup provider and start streaming
    setup_provider();
    if (!current_provider_) return;

    // Create placeholder model message
    std::string model_msg_id = "msg_" + msg_id + "_resp";
    Message model_msg;
    model_msg.id = model_msg_id;
    model_msg.conversation_id = active_conversation_id_;
    model_msg.parent_id = msg_id;
    model_msg.participant = Participant::MODEL;
    model_msg.status = MessageStatus::SENDING;
    model_msg.streaming = true;
    model_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    {
        std::lock_guard<std::mutex> lock(messages_mutex_);
        messages_.push_back(model_msg);
    }

    streaming_ = true;
    streaming_text_.clear();
    streaming_thoughts_.clear();

    // Update UI
    send_button_->set_visible(false);
    stop_button_->set_visible(true);
    send_spinner_->set_visible(true);
    send_spinner_->start();

    render_messages();

    // Run generation on separate thread
    auto& config = Config::instance();
    bool tor_enabled = config.tor.enabled;

    auto messages_copy = [&]() {
        std::lock_guard<std::mutex> lock(messages_mutex_);
        // Return all messages except the last streaming one
        return std::vector<Message>(messages_.begin(), messages_.end() - 1);
    }();

    if (generation_thread_.joinable()) {
        generation_thread_.join();
    }

    generation_thread_ = std::thread([this, messages_copy, model_msg_id, msg_id]() {
        if (!current_provider_) return;

        current_provider_->generate(messages_copy, [this, model_msg_id, msg_id](const StreamEvent& evt) -> bool {
            if (!streaming_) return false; // Cancelled

            switch (evt.type) {
                case StreamEvent::TEXT:
                    streaming_text_ += evt.text;
                    break;
                case StreamEvent::THOUGHT:
                    streaming_thoughts_ += evt.thought;
                    break;
                case StreamEvent::DONE:
                    // Finalize
                    {
                        std::lock_guard<std::mutex> lock(messages_mutex_);
                        for (auto& m : messages_) {
                            if (m.id == model_msg_id) {
                                m.text = streaming_text_;
                                m.thoughts = streaming_thoughts_;
                                m.status = MessageStatus::SUCCESS;
                                m.streaming = false;
                                m.model_name = Config::instance().selected_model;
                                break;
                            }
                        }
                    }
                    // Persist to DB
                    if (db_) {
                        auto msg = db_->get_message(model_msg_id);
                        if (msg.id.empty()) {
                            // Create
                            Message final;
                            final.id = model_msg_id;
                            final.conversation_id = active_conversation_id_;
                            final.parent_id = msg_id;
                            final.text = streaming_text_;
                            final.thoughts = streaming_thoughts_;
                            final.participant = Participant::MODEL;
                            final.status = MessageStatus::SUCCESS;
                            final.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count();
                            final.model_name = Config::instance().selected_model;
                            db_->upsert_message(final);
                        }
                    }
                    return true;
                case StreamEvent::ERROR:
                    {
                        std::lock_guard<std::mutex> lock(messages_mutex_);
                        for (auto& m : messages_) {
                            if (m.id == model_msg_id) {
                                m.text = "Error: " + evt.text;
                                m.status = MessageStatus::ERROR;
                                m.streaming = false;
                                break;
                            }
                        }
                    }
                    return true;
                default:
                    break;
            }

            // Update streaming message in memory
            {
                std::lock_guard<std::mutex> lock(messages_mutex_);
                for (auto& m : messages_) {
                    if (m.id == model_msg_id) {
                        m.text = streaming_text_;
                        m.thoughts = streaming_thoughts_;
                        break;
                    }
                }
            }

            // Schedule UI update on main thread
            Glib::signal_idle().connect_once([this]() {
                update_streaming_message();
            });

            return true; // Continue streaming
        });

        // Done streaming
        Glib::signal_idle().connect_once([this, model_msg_id, msg_id]() {
            streaming_ = false;
            send_button_->set_visible(true);
            stop_button_->set_visible(false);
            send_spinner_->stop();
            send_spinner_->set_visible(false);

            // Persist final message
            if (db_ && !streaming_text_.empty()) {
                auto final_msg = db_->get_message(model_msg_id);
                if (final_msg.id.empty()) {
                    final_msg.id = model_msg_id;
                    final_msg.conversation_id = active_conversation_id_;
                    final_msg.parent_id = msg_id;
                }
                final_msg.text = streaming_text_;
                final_msg.thoughts = streaming_thoughts_;
                final_msg.participant = Participant::MODEL;
                final_msg.status = MessageStatus::SUCCESS;
                final_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                final_msg.model_name = Config::instance().selected_model;
                db_->upsert_message(final_msg);
            }

            update_streaming_message();
        });
    });
}

void MainWindow::render_messages() {
    if (!message_list_box_) return;

    // Clear existing messages
    while (auto* child = message_list_box_->get_first_child()) {
        message_list_box_->remove(*child);
    }

    std::lock_guard<std::mutex> lock(messages_mutex_);
    for (auto& msg : messages_) {
        add_message_to_list(msg, true);
    }

    // Scroll to bottom
    auto adj = chat_scroll_->get_vadjustment();
    adj->set_value(adj->get_upper());
}

void MainWindow::add_message_to_list(const Message& msg, bool at_end) {
    auto row = Gtk::make_managed<Gtk::ListBoxRow>();

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    box->set_margin(6);
    box->set_halign(msg.participant == Participant::USER ? Gtk::Align::END : Gtk::Align::START);

    // Participant label
    std::string who = msg.participant == Participant::USER ? "You" : "Agora";
    auto who_label = Gtk::make_managed<Gtk::Label>(who);
    who_label->set_halign(Gtk::Align::START);
    Pango::AttrList attrs;
    attrs.insert(Pango::Attribute::create_attr_weight(Pango::Weight::BOLD));
    who_label->set_attributes(attrs);
    box->append(*who_label);

    // Thought block (if any)
    if (!msg.thoughts.empty() && msg.thoughts != "\\n" && msg.thoughts != "\n") {
        auto thought_frame = Gtk::make_managed<Gtk::Frame>("Thinking");
        auto thought_label = Gtk::make_managed<Gtk::Label>(msg.thoughts);
        thought_label->set_wrap(true);
        thought_label->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
        thought_label->set_halign(Gtk::Align::START);
        thought_label->set_xalign(0.0);
        thought_label->set_max_width_chars(60);
        thought_frame->set_child(*thought_label);
        thought_frame->set_margin_bottom(4);
        box->append(*thought_frame);
    }

    // Message text
    std::string display_text = msg.text;
    if (msg.streaming && !msg.streaming_text.empty()) {
        display_text = msg.streaming_text;
    }

    auto text_label = Gtk::make_managed<Gtk::Label>(display_text);
    text_label->set_wrap(true);
    text_label->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
    text_label->set_halign(Gtk::Align::START);
    text_label->set_xalign(0.0);
    text_label->set_max_width_chars(60);
    text_label->set_selectable(true);

    // Styling based on participant
    if (msg.participant == Participant::USER) {
        text_label->set_name("user-message");
    } else if (msg.participant == Participant::ERROR) {
        text_label->set_name("error-message");
    } else {
        text_label->set_name("model-message");
    }

    box->append(*text_label);

    // Status indicator
    std::string status_text;
    switch (msg.status) {
        case MessageStatus::SENDING: status_text = "Sending..."; break;
        case MessageStatus::THINKING: status_text = "Thinking..."; break;
        case MessageStatus::TOOL_CALLING: status_text = "Using tools..."; break;
        case MessageStatus::ERROR: status_text = "Error"; break;
        default: break;
    }
    if (!status_text.empty()) {
        auto status_label = Gtk::make_managed<Gtk::Label>(status_text);
        status_label->set_name("status-label");
        box->append(*status_label);
    }

    // Pin button
    if (msg.participant == Participant::MODEL && msg.status == MessageStatus::SUCCESS) {
        auto pin_btn = Gtk::make_managed<Gtk::Button>();
        pin_btn->set_icon_name(msg.pinned ? "starred-symbolic" : "non-starred-symbolic");
        pin_btn->set_has_frame(false);
        pin_btn->set_halign(Gtk::Align::START);
        pin_btn->set_tooltip_text(msg.pinned ? "Unpin message" : "Pin message");
        std::string mid = msg.id;
        pin_btn->signal_clicked().connect([this, mid]() {
            toggle_pin_message(mid);
        });
        box->append(*pin_btn);
    }

    // Timestamp
    auto ts = msg.timestamp / 1000;
    std::time_t t = ts;
    std::tm local{};
    localtime_r(&t, &local);
    std::ostringstream time_ss;
    time_ss << std::put_time(&local, "%H:%M");
    auto time_label = Gtk::make_managed<Gtk::Label>(time_ss.str());
    time_label->set_name("timestamp-label");
    time_label->set_halign(Gtk::Align::START);
    box->append(*time_label);

    row->set_child(*box);
    row->set_name(msg.id);

    message_list_box_->append(*row);
}

void MainWindow::update_streaming_message() {
    // Find streaming message and update its display
    // In a real implementation, we'd track the row widget and update its label
    render_messages();
    auto adj = chat_scroll_->get_vadjustment();
    adj->set_value(adj->get_upper());
}

void MainWindow::clear_streaming() {
    streaming_ = false;
    send_button_->set_visible(true);
    stop_button_->set_visible(false);
    send_spinner_->stop();
    send_spinner_->set_visible(false);

    update_streaming_message();
}

// --- Pinned Messages ---

void MainWindow::toggle_pin_message(const std::string& msg_id) {
    if (!db_) return;
    auto msg = db_->get_message(msg_id);
    if (msg.id.empty()) return;

    bool new_state = !msg.pinned;
    db_->pin_message(msg_id, new_state);
    load_pinned_messages();
    render_messages();
}

void MainWindow::show_thread_view(const std::string& msg_id) {
    if (!db_) return;
    auto pinned = db_->get_pinned_messages(active_conversation_id_);

    // Clear thread list
    while (auto* child = thread_list_box_->get_first_child()) {
        thread_list_box_->remove(*child);
    }

    for (auto& pin : pinned) {
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        auto label = Gtk::make_managed<Gtk::Label>(pin.text);
        label->set_wrap(true);
        label->set_max_width_chars(50);
        label->set_halign(Gtk::Align::START);
        label->set_xalign(0.0);
        row->set_child(*label);
        thread_list_box_->append(*row);
    }

    sidebar_stack_->set_visible_child("thread");
}

void MainWindow::hide_thread_view() {
    sidebar_stack_->set_visible_child("conversations");
}

void MainWindow::load_pinned_messages() {
    if (!db_ || active_conversation_id_.empty()) return;
    auto pinned = db_->get_pinned_messages(active_conversation_id_);
    // Show pinned count indicator in thread title
    thread_title_->set_text("Pinned Messages (" + std::to_string(pinned.size()) + ")");
}

// --- Microphone ---

void MainWindow::toggle_microphone() {
    auto& config = Config::instance();

    if (mic_recording_) {
        recorder_->stop();
        mic_recording_ = false;
        mic_button_->set_icon_name("microphone-symbolic");
        message_entry_->set_editable(true);
        message_entry_->set_placeholder_text("Type a message...");
    } else {
        if (!config.stt.enabled || config.stt.endpoint_url.empty()) {
            std::cerr << "STT endpoint not configured" << std::endl;
            return;
        }

        bool ok = recorder_->start(config.stt.endpoint_url,
            [this](const std::string& text, bool final) {
                on_stt_text(text, final);
            });

        if (ok) {
            mic_recording_ = true;
            mic_button_->set_icon_name("microphone-recording-symbolic");
            message_entry_->set_placeholder_text("Listening...");
            message_entry_->set_editable(false);
        }
    }
}

void MainWindow::on_stt_text(const std::string& text, bool final) {
    Glib::signal_idle().connect_once([this, text, final]() {
        message_entry_->set_text(text);
        if (final) {
            toggle_microphone(); // Stop recording
            if (!text.empty()) send_message();
        }
    });
}

// --- Provider Management ---

ProviderConfig MainWindow::build_provider_config() {
    auto& config = Config::instance();
    ProviderConfig pc;

    // Parse "Provider:Model" format from Agora
    std::string full = config.selected_model;
    auto colon = full.find(':');
    if (colon != std::string::npos) {
        pc.provider = full.substr(0, colon);
        // Normalize provider name
        if (pc.provider == "OpenAI") pc.provider = "openai";
        else if (pc.provider == "Anthropic") pc.provider = "anthropic";
        else if (pc.provider == "Google") pc.provider = "gemini";
        else if (pc.provider == "DeepSeek") pc.provider = "deepseek";
        else if (pc.provider == "Ollama") pc.provider = "ollama";
        else {
            for (auto& c : pc.provider) c = std::tolower(c);
        }
        pc.model = full.substr(colon + 1);
    } else {
        pc.provider = "openai";
        pc.model = full;
    }

    pc.api_key = config.get_api_key(pc.provider);
    pc.base_url = config.get_base_url(pc.provider);

    // System prompt
    for (auto& sp : config.system_prompts) {
        if (sp.id == config.active_system_prompt_id) {
            pc.system_prompt = sp.content;
            pc.user_prepend = sp.user_prepend;
            pc.user_postpend = sp.user_postpend;
            break;
        }
    }

    pc.max_context_window = config.max_context_window;
    pc.thinking_enabled = config.thinking_enabled;

    return pc;
}

void MainWindow::setup_provider() {
    auto& config = Config::instance();
    auto pc = build_provider_config();
    current_provider_ = Provider::create(pc, *http_);

    // Configure Tor if enabled
    if (config.tor.enabled) {
        http_->set_proxy(config.tor.socks_host, config.tor.socks_port);
        http_->set_proxy_enabled(true);
    } else {
        http_->set_proxy_enabled(false);
    }
}

void MainWindow::regenerate_message(const std::string& msg_id) {
    // Find parent user message and resend
    // Implementation would find the user message above this response
}

void MainWindow::edit_message(const std::string& msg_id, const std::string& new_text) {
    if (!db_) return;
    auto msg = db_->get_message(msg_id);
    if (msg.id.empty()) return;
    msg.text = new_text;
    db_->upsert_message(msg);
    render_messages();
}

// --- Settings ---

void MainWindow::show_settings() {
    // Create a settings dialog
    // In production, this would be a full preferences window
    auto dialog = Gtk::make_managed<Gtk::Dialog>("Settings", *this, true);
    dialog->set_default_size(400, 500);

    auto content = dialog->get_content_area();
    content->set_margin(12);
    content->set_spacing(8);

    auto notebook = Gtk::make_managed<Gtk::Notebook>();
    content->append(*notebook);

    // General page
    auto general_page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    general_page->set_margin(8);

    auto model_label = Gtk::make_managed<Gtk::Label>("Default Model:");
    model_label->set_halign(Gtk::Align::START);
    general_page->append(*model_label);

    auto model_entry = Gtk::make_managed<Gtk::Entry>();
    model_entry->set_text(Config::instance().selected_model);
    model_entry->set_placeholder_text("OpenAI:gpt-4o");
    general_page->append(*model_entry);

    notebook->append_page(*general_page, "General", "General");

    // Tor page
    auto tor_page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    tor_page->set_margin(8);

    auto& config = Config::instance();
    auto tor_toggle = Gtk::make_managed<Gtk::CheckButton>("Enable Tor (SOCKS5 proxy)");
    tor_toggle->set_active(config.tor.enabled);
    tor_page->append(*tor_toggle);

    auto host_label = Gtk::make_managed<Gtk::Label>("SOCKS Host:");
    host_label->set_halign(Gtk::Align::START);
    tor_page->append(*host_label);

    auto host_entry = Gtk::make_managed<Gtk::Entry>();
    host_entry->set_text(config.tor.socks_host);
    tor_page->append(*host_entry);

    auto port_label = Gtk::make_managed<Gtk::Label>("SOCKS Port:");
    port_label->set_halign(Gtk::Align::START);
    tor_page->append(*port_label);

    auto port_entry = Gtk::make_managed<Gtk::Entry>();
    port_entry->set_text(std::to_string(config.tor.socks_port));
    tor_page->append(*port_entry);

    notebook->append_page(*tor_page, "Tor", "Tor");

    // STT page
    auto stt_page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    stt_page->set_margin(8);

    auto stt_toggle = Gtk::make_managed<Gtk::CheckButton>("Enable Speech-to-Text (remote endpoint)");
    stt_toggle->set_active(config.stt.enabled);
    stt_page->append(*stt_toggle);

    auto stt_label = Gtk::make_managed<Gtk::Label>("STT Endpoint URL:");
    stt_label->set_halign(Gtk::Align::START);
    stt_page->append(*stt_label);

    auto stt_entry = Gtk::make_managed<Gtk::Entry>();
    stt_entry->set_text(config.stt.endpoint_url);
    stt_entry->set_placeholder_text("http://your-server:8080/stt");
    stt_page->append(*stt_entry);

    notebook->append_page(*stt_page, "Speech", "Speech");

    // API Keys page
    auto api_page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    api_page->set_margin(8);

    auto store = Gtk::make_managed<Gtk::ListStore>();
    // Would populate with API keys
    auto view = Gtk::make_managed<Gtk::TreeView>(store);
    api_page->append(*view);

    notebook->append_page(*api_page, "API Keys", "API Keys");

    // Buttons
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("Save", Gtk::ResponseType::OK);

    dialog->signal_response().connect([dialog, model_entry, tor_toggle, host_entry,
        port_entry, stt_toggle, stt_entry](int response) {
        if (response == Gtk::ResponseType::OK) {
            auto& cfg = Config::instance();
            cfg.selected_model = model_entry->get_text();
            cfg.tor.enabled = tor_toggle->get_active();
            cfg.tor.socks_host = host_entry->get_text();
            cfg.tor.socks_port = std::stoi(port_entry->get_text());
            cfg.stt.enabled = stt_toggle->get_active();
            cfg.stt.endpoint_url = stt_entry->get_text();

            const char* home = getenv("HOME");
            std::string base = home ? std::string(home) : "/tmp";
            cfg.save(base + "/.config/agora.json");
        }
    });

    dialog->show();
}

void MainWindow::show_conversation_menu(int x, int y, const std::string& conv_id) {
    auto menu = Gio::Menu::create();
    menu->append("Rename", "conv.rename");
    menu->append("Delete", "conv.delete");

    auto popup = Gtk::make_managed<Gtk::PopoverMenu>(menu);
    // Show at position
}

} // namespace AgoraUI
