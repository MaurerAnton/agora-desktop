#include <gtk/gtk.h>
#include <thread>
#include <mutex>
#include <memory>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <cstring>

#include "db/database.hpp"
#include "api/provider.hpp"
#include "audio/recorder.hpp"
#include "utils/config.hpp"

// --- Application State ---

struct AppState {
    std::shared_ptr<Database> db;
    std::shared_ptr<HttpClient> http;
    std::unique_ptr<Provider> provider;
    std::unique_ptr<AudioRecorder> recorder;

    std::string active_conv_id;
    std::vector<Message> messages;
    std::mutex msg_mutex;

    std::thread gen_thread;
    bool streaming = false;
    bool mic_active = false;
    std::string stream_text;
    std::string stream_thoughts;
    std::string stream_model_msg_id;

    // UI widgets
    GtkWindow* window = nullptr;
    GtkLabel* title_label = nullptr;
    GtkListBox* conv_list = nullptr;
    GtkListBox* msg_list = nullptr;
    GtkEntry* msg_entry = nullptr;
    GtkButton* send_btn = nullptr;
    GtkButton* mic_btn = nullptr;
    GtkButton* stop_btn = nullptr;
    GtkSpinner* spinner = nullptr;
    GtkListBox* pinned_list = nullptr;
    GtkSearchEntry* search_entry = nullptr;
    GtkScrolledWindow* chat_scroll = nullptr;
    GtkStack* sidebar_stack = nullptr;
    GtkLabel* pinned_title = nullptr;
};

static AppState g_state;

// --- Helpers ---

static std::string generate_id() {
    auto now = std::chrono::system_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    std::ostringstream oss;
    oss << std::hex << us;
    return oss.str();
}

static std::string format_time(int64_t ms) {
    std::time_t t = ms / 1000;
    std::tm local{};
    localtime_r(&t, &local);
    std::ostringstream oss;
    oss << std::put_time(&local, "%H:%M");
    return oss.str();
}

static std::string status_text(MessageStatus s) {
    switch (s) {
        case MessageStatus::SENDING: return "Sending...";
        case MessageStatus::THINKING: return "Thinking...";
        case MessageStatus::TOOL_CALLING: return "Using tools...";
        case MessageStatus::ERROR: return "Error";
        default: return "";
    }
}

// --- Conversation Sidebar ---

static void refresh_conversations() {
    if (!g_state.db) return;
    auto* list = g_state.conv_list;
    gtk_list_box_remove_all(list);

    auto query = gtk_editable_get_text(GTK_EDITABLE(g_state.search_entry));
    auto convs = g_state.db->get_all_conversations();

    for (auto& conv : convs) {
        std::string lower_title = conv.title;
        std::string lower_query(query);
        for (auto& c : lower_title) c = std::tolower(c);
        for (auto& c : lower_query) c = std::tolower(c);
        if (!lower_query.empty() && lower_title.find(lower_query) == std::string::npos) continue;

        auto* row = gtk_list_box_row_new();
        auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_margin_start(box, 8);
        gtk_widget_set_margin_end(box, 8);
        gtk_widget_set_margin_top(box, 4);
        gtk_widget_set_margin_bottom(box, 4);

        auto* title = gtk_label_new(conv.title.c_str());
        gtk_label_set_xalign(GTK_LABEL(title), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(box), title);

        g_object_set_data_full(G_OBJECT(row), "conv_id",
            g_strdup(conv.id.c_str()), g_free);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
        gtk_list_box_append(list, row);
    }
}

static void render_messages();

static void select_conversation(const std::string& conv_id, bool refresh = true) {
    g_state.active_conv_id = conv_id;
    g_state.messages.clear();

    if (g_state.db && !conv_id.empty()) {
        g_state.messages = g_state.db->get_messages_for_conversation(conv_id);
    }

    if (refresh) {
        render_messages();

        if (g_state.db && !conv_id.empty()) {
            auto pinned = g_state.db->get_pinned_messages(conv_id);
            gtk_label_set_text(g_state.pinned_title,
                ("Pinned (" + std::to_string(pinned.size()) + ")").c_str());
        }
    }
}

// --- Message Display ---

static GtkWidget* create_message_row(const Message& msg) {
    auto* row = gtk_list_box_row_new();
    auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);

    bool is_user = msg.participant == Participant::USER;

    // Participant label
    auto* who = gtk_label_new(is_user ? "You" : "Agora");
    gtk_label_set_xalign(GTK_LABEL(who), 0.0);
    gtk_widget_add_css_class(who, "heading");
    gtk_box_append(GTK_BOX(box), who);

    // Thought block
    if (!msg.thoughts.empty() && msg.thoughts != "\n") {
        auto* thought_frame = gtk_frame_new("Thinking");
        auto* thought_lbl = gtk_label_new(msg.thoughts.c_str());
        gtk_label_set_wrap(GTK_LABEL(thought_lbl), true);
        gtk_label_set_xalign(GTK_LABEL(thought_lbl), 0.0);
        gtk_frame_set_child(GTK_FRAME(thought_frame), thought_lbl);
        gtk_widget_set_margin_top(thought_frame, 4);
        gtk_widget_set_margin_bottom(thought_frame, 4);
        gtk_box_append(GTK_BOX(box), thought_frame);
    }

    // Message text
    std::string text = msg.text;
    if (msg.streaming && !msg.streaming_text.empty()) text = msg.streaming_text;

    auto* text_lbl = gtk_label_new(text.c_str());
    gtk_label_set_wrap(GTK_LABEL(text_lbl), true);
    gtk_label_set_xalign(GTK_LABEL(text_lbl), 0.0);
    gtk_label_set_selectable(GTK_LABEL(text_lbl), true);
    gtk_label_set_max_width_chars(GTK_LABEL(text_lbl), 50);

    if (is_user) {
        gtk_widget_add_css_class(text_lbl, "user-msg");
    } else if (msg.participant == Participant::MODEL) {
        gtk_widget_add_css_class(text_lbl, "model-msg");
    } else {
        gtk_widget_add_css_class(text_lbl, "error-msg");
    }
    gtk_box_append(GTK_BOX(box), text_lbl);

    // Status
    std::string st = status_text(msg.status);
    if (!st.empty()) {
        auto* st_lbl = gtk_label_new(st.c_str());
        gtk_widget_add_css_class(st_lbl, "dim-label");
        gtk_widget_add_css_class(st_lbl, "caption");
        gtk_box_append(GTK_BOX(box), st_lbl);
    }

    // Pin button (only for completed MODEL messages)
    if (msg.participant == Participant::MODEL && msg.status == MessageStatus::SUCCESS) {
        auto* pin_btn = gtk_button_new_from_icon_name(
            msg.pinned ? "starred-symbolic" : "non-starred-symbolic");
        gtk_button_set_has_frame(GTK_BUTTON(pin_btn), false);
        gtk_widget_set_halign(pin_btn, GTK_ALIGN_START);
        gtk_widget_set_tooltip_text(pin_btn,
            msg.pinned ? "Unpin message" : "Pin message");
        std::string mid = msg.id;
        g_signal_connect(pin_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* id = static_cast<std::string*>(data);
            if (g_state.db) {
                auto m = g_state.db->get_message(*id);
                g_state.db->pin_message(*id, !m.pinned);
                select_conversation(g_state.active_conv_id, true);
            }
        }), new std::string(mid));
        gtk_box_append(GTK_BOX(box), pin_btn);
    }

    // Timestamp
    auto* time_lbl = gtk_label_new(format_time(msg.timestamp).c_str());
    gtk_widget_add_css_class(time_lbl, "dim-label");
    gtk_widget_add_css_class(time_lbl, "caption");
    gtk_label_set_xalign(GTK_LABEL(time_lbl), 0.0);
    gtk_box_append(GTK_BOX(box), time_lbl);

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    return row;
}

static void render_messages() {
    auto* list = g_state.msg_list;
    gtk_list_box_remove_all(list);

    std::lock_guard lock(g_state.msg_mutex);
    for (auto& msg : g_state.messages) {
        auto* row = create_message_row(msg);
        gtk_list_box_append(list, row);
    }

    // Scroll to bottom
    auto* adj = gtk_scrolled_window_get_vadjustment(g_state.chat_scroll);
    gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));
}

static void update_streaming_message() {
    render_messages();
}

// --- Sending Messages ---

static void send_message() {
    auto text = gtk_editable_get_text(GTK_EDITABLE(g_state.msg_entry));
    if (!text || !*text) return;
    if (g_state.streaming) return;

    gtk_editable_set_text(GTK_EDITABLE(g_state.msg_entry), "");

    if (!g_state.db || g_state.active_conv_id.empty()) {
        // Create new conversation
        std::string id = generate_id();
        Conversation conv;
        conv.id = id;
        conv.title = std::string(text).substr(0, 30);
        conv.last_updated = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        g_state.db->upsert_conversation(conv);
        g_state.active_conv_id = id;
        refresh_conversations();
    }

    // Create user message
    std::string user_msg_id = "msg_" + generate_id();
    Message user_msg;
    user_msg.id = user_msg_id;
    user_msg.conversation_id = g_state.active_conv_id;
    user_msg.text = text;
    user_msg.participant = Participant::USER;
    user_msg.status = MessageStatus::SUCCESS;
    user_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    g_state.db->upsert_message(user_msg);

    {
        std::lock_guard lock(g_state.msg_mutex);
        g_state.messages.push_back(user_msg);
    }

    // Setup provider
    auto& cfg = Config::instance();
    GenConfig gc;
    std::string full = cfg.selected_model;
    auto colon = full.find(':');
    if (colon != std::string::npos) {
        gc.provider = full.substr(0, colon);
        for (auto& c : gc.provider) c = std::tolower(c);
        gc.model = full.substr(colon + 1);
    } else {
        gc.provider = "openai";
        gc.model = full;
    }
    gc.api_key = cfg.get_api_key(gc.provider);
    gc.base_url = cfg.get_base_url(gc.provider);

    for (auto& sp : cfg.system_prompts) {
        if (sp.id == cfg.active_system_prompt_id) {
            gc.system_prompt = sp.content;
            gc.user_prepend = sp.user_prepend;
            gc.user_postpend = sp.user_postpend;
            break;
        }
    }
    gc.max_context_window = cfg.max_context_window;
    gc.thinking_enabled = cfg.thinking_enabled;
    gc.temperature = 0.7f;
    gc.max_tokens = 4096;

    g_state.provider = Provider::create(gc, *g_state.http);

    if (cfg.tor.enabled) {
        g_state.http->set_proxy(cfg.tor.socks_host, cfg.tor.socks_port);
        g_state.http->set_proxy_enabled(true);
    }

    // Create streaming model message
    std::string model_msg_id = user_msg_id + "_resp";
    g_state.stream_model_msg_id = model_msg_id;

    Message model_msg;
    model_msg.id = model_msg_id;
    model_msg.conversation_id = g_state.active_conv_id;
    model_msg.parent_id = user_msg_id;
    model_msg.participant = Participant::MODEL;
    model_msg.status = MessageStatus::SENDING;
    model_msg.streaming = true;
    model_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    {
        std::lock_guard lock(g_state.msg_mutex);
        g_state.messages.push_back(model_msg);
    }

    g_state.streaming = true;
    g_state.stream_text.clear();
    g_state.stream_thoughts.clear();

    gtk_widget_set_visible(GTK_WIDGET(g_state.send_btn), false);
    gtk_widget_set_visible(GTK_WIDGET(g_state.stop_btn), true);
    gtk_spinner_start(g_state.spinner);
    gtk_widget_set_visible(GTK_WIDGET(g_state.spinner), true);

    render_messages();

    // Copy messages for generation (without streaming placeholder)
    std::vector<Message> msg_copy;
    {
        std::lock_guard lock(g_state.msg_mutex);
        msg_copy.assign(g_state.messages.begin(), g_state.messages.end() - 1);
    }

    // Start generation in background thread
    if (g_state.gen_thread.joinable()) g_state.gen_thread.join();

    g_state.gen_thread = std::thread([msg_copy, model_msg_id, user_msg_id]() {
        if (!g_state.provider) return;

        g_state.provider->generate(msg_copy, [model_msg_id](const StreamEvent& evt) -> bool {
            if (!g_state.streaming) return false;

            switch (evt.type) {
                case StreamEvent::TEXT:
                    g_state.stream_text += evt.text;
                    break;
                case StreamEvent::THOUGHT:
                    g_state.stream_thoughts += evt.thought;
                    break;
                case StreamEvent::DONE:
                    {
                        std::lock_guard lock(g_state.msg_mutex);
                        for (auto& m : g_state.messages) {
                            if (m.id == model_msg_id) {
                                m.text = g_state.stream_text;
                                m.thoughts = g_state.stream_thoughts;
                                m.status = MessageStatus::SUCCESS;
                                m.streaming = false;
                                m.model_name = Config::instance().selected_model;
                                break;
                            }
                        }
                    }
                    return true;
                case StreamEvent::ERROR:
                    {
                        std::lock_guard lock(g_state.msg_mutex);
                        for (auto& m : g_state.messages) {
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

            // Update in-memory streaming message
            {
                std::lock_guard lock(g_state.msg_mutex);
                for (auto& m : g_state.messages) {
                    if (m.id == model_msg_id) {
                        m.text = g_state.stream_text;
                        m.thoughts = g_state.stream_thoughts;
                        break;
                    }
                }
            }

            g_idle_add([](gpointer) -> gboolean {
                update_streaming_message();
                return G_SOURCE_REMOVE;
            }, nullptr);

            return true;
        });

        // Finalize on idle (main thread)
        g_idle_add([](gpointer) -> gboolean {
            g_state.streaming = false;
            gtk_widget_set_visible(GTK_WIDGET(g_state.send_btn), true);
            gtk_widget_set_visible(GTK_WIDGET(g_state.stop_btn), false);
            gtk_spinner_stop(g_state.spinner);
            gtk_widget_set_visible(GTK_WIDGET(g_state.spinner), false);

            // Persist final message
            if (g_state.db) {
                Message final_msg;
                final_msg.id = g_state.stream_model_msg_id;
                final_msg.conversation_id = g_state.active_conv_id;
                final_msg.text = g_state.stream_text;
                final_msg.thoughts = g_state.stream_thoughts;
                final_msg.participant = Participant::MODEL;
                final_msg.status = MessageStatus::SUCCESS;
                final_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                final_msg.model_name = Config::instance().selected_model;
                g_state.db->upsert_message(final_msg);
            }

            update_streaming_message();
            return G_SOURCE_REMOVE;
        }, nullptr);
    });
}

// --- Microphone ---

static void toggle_mic() {
    auto& cfg = Config::instance();
    if (g_state.mic_active) {
        if (g_state.recorder) g_state.recorder->stop();
        g_state.mic_active = false;
        gtk_button_set_icon_name(g_state.mic_btn, "microphone-symbolic");
        gtk_editable_set_editable(GTK_EDITABLE(g_state.msg_entry), true);
        gtk_entry_set_placeholder_text(g_state.msg_entry, "Type a message...");
    } else {
        if (!cfg.stt.enabled || cfg.stt.endpoint_url.empty()) return;

        if (!g_state.recorder) {
            g_state.recorder = std::make_unique<AudioRecorder>();
        }

        bool ok = g_state.recorder->start(cfg.stt.endpoint_url,
            [](const std::string& text, bool final) {
                g_idle_add([](gpointer text_ptr) -> gboolean {
                    auto* t = static_cast<std::string*>(text_ptr);
                    gtk_editable_set_text(GTK_EDITABLE(g_state.msg_entry), t->c_str());
                    delete t;
                    return G_SOURCE_REMOVE;
                }, new std::string(text));
            });

        if (ok) {
            g_state.mic_active = true;
            gtk_button_set_icon_name(g_state.mic_btn, "audio-input-microphone-symbolic");
            gtk_entry_set_placeholder_text(g_state.msg_entry, "Listening...");
            gtk_editable_set_editable(GTK_EDITABLE(g_state.msg_entry), false);
        }
    }
}

// --- Pinned Messages Panel ---

static void show_pinned_view() {
    if (!g_state.db || g_state.active_conv_id.empty()) return;
    auto pinned = g_state.db->get_pinned_messages(g_state.active_conv_id);

    gtk_list_box_remove_all(g_state.pinned_list);
    gtk_label_set_text(g_state.pinned_title,
        ("Pinned Messages (" + std::to_string(pinned.size()) + ")").c_str());

    for (auto& m : pinned) {
        auto* row = gtk_list_box_row_new();
        auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_margin_start(box, 8);
        gtk_widget_set_margin_end(box, 8);
        gtk_widget_set_margin_top(box, 4);
        gtk_widget_set_margin_bottom(box, 4);

        auto* who = gtk_label_new(m.participant == Participant::USER ? "You" : "Agora");
        gtk_label_set_xalign(GTK_LABEL(who), 0.0);
        gtk_widget_add_css_class(who, "heading");
        gtk_box_append(GTK_BOX(box), who);

        auto* text = gtk_label_new(m.text.c_str());
        gtk_label_set_wrap(GTK_LABEL(text), true);
        gtk_label_set_xalign(GTK_LABEL(text), 0.0);
        gtk_label_set_max_width_chars(GTK_LABEL(text), 40);
        gtk_box_append(GTK_BOX(box), text);

        auto* time_lbl = gtk_label_new(format_time(m.timestamp).c_str());
        gtk_widget_add_css_class(time_lbl, "dim-label");
        gtk_widget_add_css_class(time_lbl, "caption");
        gtk_box_append(GTK_BOX(box), time_lbl);

        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
        gtk_list_box_append(g_state.pinned_list, row);
    }

    gtk_stack_set_visible_child_name(g_state.sidebar_stack, "pinned");
}

static void hide_pinned_view() {
    gtk_stack_set_visible_child_name(g_state.sidebar_stack, "conversations");
}

// --- Settings Dialog ---

static void show_settings(GtkWindow* parent) {
    auto* dialog = GTK_WINDOW(gtk_window_new());
    gtk_window_set_transient_for(dialog, parent);
    gtk_window_set_modal(dialog, true);
    gtk_window_set_title(dialog, "Settings");
    gtk_window_set_default_size(dialog, 400, 500);

    auto* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(content, 12);
    gtk_widget_set_margin_end(content, 12);
    gtk_widget_set_margin_top(content, 12);
    gtk_widget_set_margin_bottom(content, 12);
    gtk_window_set_child(dialog, content);

    auto& cfg = Config::instance();

    // Model
    auto* model_lbl = gtk_label_new("Default Model:");
    gtk_label_set_xalign(GTK_LABEL(model_lbl), 0.0);
    gtk_box_append(GTK_BOX(content), model_lbl);

    auto* model_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(model_entry), cfg.selected_model.c_str());
    gtk_entry_set_placeholder_text(GTK_ENTRY(model_entry), "OpenAI:gpt-4o");
    gtk_box_append(GTK_BOX(content), model_entry);

    // API Key
    auto* api_lbl = gtk_label_new("API Key (OpenAI):");
    gtk_label_set_xalign(GTK_LABEL(api_lbl), 0.0);
    gtk_box_append(GTK_BOX(content), api_lbl);

    auto* api_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(api_entry), cfg.get_api_key("openai").c_str());
    gtk_entry_set_visibility(GTK_ENTRY(api_entry), false);
    gtk_box_append(GTK_BOX(content), api_entry);

    // Tor
    auto* tor_toggle = gtk_check_button_new_with_label("Enable Tor (SOCKS5 proxy)");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(tor_toggle), cfg.tor.enabled);
    gtk_box_append(GTK_BOX(content), tor_toggle);

    auto* tor_host_lbl = gtk_label_new("SOCKS5 Host:");
    gtk_label_set_xalign(GTK_LABEL(tor_host_lbl), 0.0);
    gtk_box_append(GTK_BOX(content), tor_host_lbl);

    auto* tor_host_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(tor_host_entry), cfg.tor.socks_host.c_str());
    gtk_box_append(GTK_BOX(content), tor_host_entry);

    auto* tor_port_lbl = gtk_label_new("SOCKS5 Port:");
    gtk_label_set_xalign(GTK_LABEL(tor_port_lbl), 0.0);
    gtk_box_append(GTK_BOX(content), tor_port_lbl);

    auto* tor_port_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(tor_port_entry), std::to_string(cfg.tor.socks_port).c_str());
    gtk_box_append(GTK_BOX(content), tor_port_entry);

    // STT
    auto* stt_lbl = gtk_label_new("STT Endpoint URL:");
    gtk_label_set_xalign(GTK_LABEL(stt_lbl), 0.0);
    gtk_box_append(GTK_BOX(content), stt_lbl);

    auto* stt_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(stt_entry), cfg.stt.endpoint_url.c_str());
    gtk_entry_set_placeholder_text(GTK_ENTRY(stt_entry), "http://192.168.1.100:8080/stt");
    gtk_box_append(GTK_BOX(content), stt_entry);

    // Buttons
    auto* btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(content), btn_box);

    auto* cancel_btn = gtk_button_new_with_label("Cancel");
    auto* save_btn = gtk_button_new_with_label("Save");
    gtk_widget_add_css_class(save_btn, "suggested-action");
    gtk_box_append(GTK_BOX(btn_box), cancel_btn);
    gtk_box_append(GTK_BOX(btn_box), save_btn);

    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(gtk_window_destroy), nullptr);
    g_signal_connect(save_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) {
        gtk_window_destroy(GTK_WINDOW(d));
    }), dialog);

    gtk_window_present(dialog);
}

// --- CSS Provider ---

static void load_css() {
    auto* provider = gtk_css_provider_new();
    const char* css = R"(
        .user-msg {
            background-color: @accent_bg_color;
            border-radius: 12px;
            padding: 8px;
            margin: 4px 0;
        }
        .model-msg {
            background-color: @card_bg_color;
            border-radius: 12px;
            padding: 8px;
            margin: 4px 0;
        }
        .error-msg {
            background-color: @error_bg_color;
            border-radius: 12px;
            padding: 8px;
            margin: 4px 0;
        }
        listbox row {
            padding: 2px 4px;
            border-bottom: 1px solid alpha(@borders, 0.3);
        }
        window {
            background-color: @window_bg_color;
        }
    )";
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

// --- Build UI ---

static void activate(GtkApplication* app, gpointer) {
    // Load CSS
    load_css();

    // Create window
    auto* window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Agora");
    gtk_window_set_default_size(GTK_WINDOW(window), 360, 640);
    g_state.window = GTK_WINDOW(window);

    // Header bar
    auto* header = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), true);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    g_state.title_label = GTK_LABEL(gtk_label_new("Agora"));
    gtk_label_set_ellipsize(g_state.title_label, PANGO_ELLIPSIZE_END);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), GTK_WIDGET(g_state.title_label));

    // New chat button
    auto* new_chat = gtk_button_new_from_icon_name("document-new-symbolic");
    gtk_widget_set_tooltip_text(new_chat, "New Chat");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), new_chat);
    g_signal_connect(new_chat, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        std::string id = generate_id();
        Conversation conv;
        conv.id = id;
        conv.title = "New Chat";
        conv.last_updated = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (g_state.db) g_state.db->upsert_conversation(conv);
        refresh_conversations();
        select_conversation(id);
    }), nullptr);

    // Settings button
    auto* settings_btn = gtk_button_new_from_icon_name("emblem-system-symbolic");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), settings_btn);
    g_signal_connect(settings_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        show_settings(g_state.window);
    }), nullptr);

    // Pinned messages button
    auto* pinned_btn = gtk_button_new_from_icon_name("starred-symbolic");
    gtk_widget_set_tooltip_text(pinned_btn, "Pinned Messages");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), pinned_btn);
    g_signal_connect(pinned_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        show_pinned_view();
    }), nullptr);

    // Main layout - horizontal split
    auto* main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), main_box);

    // --- Sidebar ---
    auto* sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(sidebar, 240, -1);

    // Search
    g_state.search_entry = GTK_SEARCH_ENTRY(gtk_search_entry_new());
    gtk_search_entry_set_placeholder_text(g_state.search_entry, "Search...");
    gtk_box_append(GTK_BOX(sidebar), GTK_WIDGET(g_state.search_entry));
    g_signal_connect(g_state.search_entry, "search-changed",
        G_CALLBACK(+[](GtkSearchEntry*, gpointer) { refresh_conversations(); }), nullptr);

    // Stack for sidebar views
    g_state.sidebar_stack = GTK_STACK(gtk_stack_new());
    gtk_box_append(GTK_BOX(sidebar), GTK_WIDGET(g_state.sidebar_stack));

    // Conversations page
    auto* conv_scroll = gtk_scrolled_window_new();
    g_state.conv_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(g_state.conv_list, GTK_SELECTION_SINGLE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(conv_scroll), GTK_WIDGET(g_state.conv_list));
    gtk_stack_add_titled(g_state.sidebar_stack, conv_scroll, "conversations", "Chats");

    g_signal_connect(g_state.conv_list, "row-selected",
        G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer) {
            if (!row) return;
            auto* id = static_cast<char*>(g_object_get_data(G_OBJECT(row), "conv_id"));
            if (id) select_conversation(id);
        }), nullptr);

    // Pinned view page
    auto* pinned_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_state.pinned_title = GTK_LABEL(gtk_label_new("Pinned Messages"));
    gtk_widget_set_margin_top(GTK_WIDGET(g_state.pinned_title), 8);
    gtk_widget_set_margin_bottom(GTK_WIDGET(g_state.pinned_title), 8);
    gtk_box_append(GTK_BOX(pinned_box), GTK_WIDGET(g_state.pinned_title));

    auto* pinned_scroll = gtk_scrolled_window_new();
    g_state.pinned_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(pinned_scroll), GTK_WIDGET(g_state.pinned_list));
    gtk_box_append(GTK_BOX(pinned_box), pinned_scroll);

    auto* back_btn = gtk_button_new_with_label("Back to Chats");
    gtk_box_append(GTK_BOX(pinned_box), back_btn);
    g_signal_connect(back_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        hide_pinned_view();
    }), nullptr);

    gtk_stack_add_titled(g_state.sidebar_stack, pinned_box, "pinned", "Pinned");
    gtk_stack_set_visible_child_name(g_state.sidebar_stack, "conversations");

    gtk_box_append(GTK_BOX(main_box), sidebar);

    // --- Chat area ---
    auto* chat_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(chat_box, true);

    // Messages
    g_state.chat_scroll = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new());
    gtk_widget_set_vexpand(GTK_WIDGET(g_state.chat_scroll), true);
    g_state.msg_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(g_state.msg_list, GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child(g_state.chat_scroll, GTK_WIDGET(g_state.msg_list));
    gtk_box_append(GTK_BOX(chat_box), GTK_WIDGET(g_state.chat_scroll));

    // Input bar
    auto* input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(input_box, 6);
    gtk_widget_set_margin_end(input_box, 6);
    gtk_widget_set_margin_top(input_box, 6);
    gtk_widget_set_margin_bottom(input_box, 6);

    // Mic button
    g_state.mic_btn = GTK_BUTTON(gtk_button_new_from_icon_name("microphone-symbolic"));
    gtk_button_set_has_frame(g_state.mic_btn, false);
    gtk_box_append(GTK_BOX(input_box), GTK_WIDGET(g_state.mic_btn));
    g_signal_connect(g_state.mic_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        toggle_mic();
    }), nullptr);

    // Message entry
    g_state.msg_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(g_state.msg_entry, "Type a message...");
    gtk_widget_set_hexpand(GTK_WIDGET(g_state.msg_entry), true);
    gtk_box_append(GTK_BOX(input_box), GTK_WIDGET(g_state.msg_entry));

    // Spinner
    g_state.spinner = GTK_SPINNER(gtk_spinner_new());
    gtk_widget_set_visible(GTK_WIDGET(g_state.spinner), false);
    gtk_box_append(GTK_BOX(input_box), GTK_WIDGET(g_state.spinner));

    // Send button
    g_state.send_btn = GTK_BUTTON(gtk_button_new_from_icon_name("send-symbolic"));
    gtk_button_set_has_frame(g_state.send_btn, false);
    gtk_box_append(GTK_BOX(input_box), GTK_WIDGET(g_state.send_btn));
    g_signal_connect(g_state.send_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        send_message();
    }), nullptr);

    // Stop button
    g_state.stop_btn = GTK_BUTTON(gtk_button_new_from_icon_name("process-stop-symbolic"));
    gtk_button_set_has_frame(g_state.stop_btn, false);
    gtk_widget_set_visible(GTK_WIDGET(g_state.stop_btn), false);
    gtk_box_append(GTK_BOX(input_box), GTK_WIDGET(g_state.stop_btn));
    g_signal_connect(g_state.stop_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        g_state.streaming = false;
        gtk_widget_set_visible(GTK_WIDGET(g_state.send_btn), true);
        gtk_widget_set_visible(GTK_WIDGET(g_state.stop_btn), false);
        gtk_spinner_stop(g_state.spinner);
        gtk_widget_set_visible(GTK_WIDGET(g_state.spinner), false);
    }), nullptr);

    // Enter key in entry
    g_signal_connect(g_state.msg_entry, "activate", G_CALLBACK(+[](GtkEntry*, gpointer) {
        send_message();
    }), nullptr);

    gtk_box_append(GTK_BOX(chat_box), input_box);
    gtk_box_append(GTK_BOX(main_box), chat_box);

    // Initial state
    refresh_conversations();
    gtk_window_present(GTK_WINDOW(window));
}

// --- Entry point for main.cpp ---

static void init_from_config() {
    auto& cfg = Config::instance();
    g_state.db = std::make_shared<Database>(cfg.db_path);
    g_state.http = std::make_shared<HttpClient>();

    if (cfg.tor.enabled) {
        g_state.http->set_proxy(cfg.tor.socks_host, cfg.tor.socks_port);
        g_state.http->set_proxy_enabled(true);
    }
}

extern "C" void agora_ui_init(GtkApplication* app, gpointer) {
    init_from_config();
    activate(app, nullptr);
}
