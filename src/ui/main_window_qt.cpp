#include "ui/main_window_qt.hpp"
#include "utils/config.hpp"
#include "ui/theme.hpp"
#include "utils/import_export.hpp"
#include <iostream>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QScrollBar>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QTextEdit>
#include <QButtonGroup>
#include <QRadioButton>
#include <QScrollArea>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QScreen>
#include <QPixmap>
#include <QFileDialog>
#include <QDateTime>
#include <QFrame>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QMessageBox>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("agora-desktop");
    resize(720, 560);
    setMinimumSize(360, 400);

    // Headless mode only when explicitly requested (set from main after --headless)
    headless_ = false;

    // Central splitter
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(splitter);

    // === Sidebar ===
    sidebar_ = new QWidget;
    auto* sidebar_layout = new QVBoxLayout(sidebar_);
    sidebar_layout->setContentsMargins(4, 4, 4, 4);
    sidebar_layout->setSpacing(4);

    // Search
    search_edit_ = new QLineEdit;
    search_edit_->setPlaceholderText("Search conversations...");
    search_edit_->setClearButtonEnabled(true);
    sidebar_layout->addWidget(search_edit_);

    // Sidebar stack (conversations / pinned)
    sidebar_stack_ = new QStackedWidget;
    sidebar_layout->addWidget(sidebar_stack_);

    // Conversations page
    auto* conv_page = new QWidget;
    auto* conv_layout = new QVBoxLayout(conv_page);
    conv_layout->setContentsMargins(0, 0, 0, 0);
    conv_list_ = new QListWidget;
    conv_list_->setWordWrap(true);
    conv_layout->addWidget(conv_list_);
    sidebar_stack_->addWidget(conv_page);

    // Pinned page
    auto* pinned_page = new QWidget;
    auto* pinned_layout = new QVBoxLayout(pinned_page);
    pinned_layout->setContentsMargins(0, 0, 0, 0);
    pinned_title_ = new QLabel("Pinned Messages");
    pinned_title_->setAlignment(Qt::AlignCenter);
    pinned_layout->addWidget(pinned_title_);
    pinned_list_ = new QListWidget;
    pinned_list_->setWordWrap(true);
    pinned_layout->addWidget(pinned_list_);
    auto* back_btn = new QPushButton("← Back to Chats");
    pinned_layout->addWidget(back_btn);
    sidebar_stack_->addWidget(pinned_page);

    sidebar_stack_->setCurrentIndex(0);
    sidebar_->setMaximumWidth(280);
    splitter->addWidget(sidebar_);

    // === Chat area ===
    auto* chat_widget = new QWidget;
    auto* chat_layout = new QVBoxLayout(chat_widget);
    chat_layout->setContentsMargins(0, 0, 0, 0);
    chat_layout->setSpacing(0);

    // Messages
    msg_list_ = new QListWidget;
    msg_list_->setSelectionMode(QAbstractItemView::NoSelection);
    msg_list_->setWordWrap(true);
    msg_list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    msg_list_->setStyleSheet(
        "QListWidget { background-color: palette(window); border: none; }"
        "QListWidget::item { border: none; padding: 2px; }"
    );
    chat_layout->addWidget(msg_list_);

    // Status label
    status_label_ = new QLabel;
    status_label_->setVisible(false);
    status_label_->setStyleSheet("color: palette(mid); font-size: 11px; padding: 2px 8px;");
    chat_layout->addWidget(status_label_);

    // Input bar
    auto* input_widget = new QWidget;
    auto* input_layout = new QHBoxLayout(input_widget);
    input_layout->setContentsMargins(4, 4, 4, 4);
    input_layout->setSpacing(4);

    mic_btn_ = new QPushButton("🎤");
    mic_btn_->setFixedSize(40, 40);
    mic_btn_->setToolTip("Hold to talk (speech-to-text)");
    mic_btn_->setCheckable(true);
    input_layout->addWidget(mic_btn_);

    attach_btn_ = new QPushButton("📎");
    attach_btn_->setFixedSize(40, 40);
    attach_btn_->setToolTip("Attach image");
    input_layout->addWidget(attach_btn_);

    msg_entry_ = new QLineEdit;
    msg_entry_->setPlaceholderText("Type a message...");
    msg_entry_->setStyleSheet("QLineEdit { border-radius: 8px; padding: 6px; }");
    input_layout->addWidget(msg_entry_);

    send_btn_ = new QPushButton("↑");
    send_btn_->setFixedSize(40, 40);
    send_btn_->setStyleSheet("QPushButton { font-weight: bold; font-size: 18px; }");
    input_layout->addWidget(send_btn_);

    stop_btn_ = new QPushButton("■");
    stop_btn_->setFixedSize(40, 40);
    stop_btn_->setVisible(false);
    stop_btn_->setStyleSheet("QPushButton { color: red; font-weight: bold; }");
    input_layout->addWidget(stop_btn_);

    chat_layout->addWidget(input_widget);
    splitter->addWidget(chat_widget);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    // Toolbar
    auto* toolbar = addToolBar("Actions");
    toolbar->setMovable(false);

    auto* new_action = toolbar->addAction("+ New Chat");
    auto* prompt_action = toolbar->addAction("🧠 Prompt");
    auto* advanced_action = toolbar->addAction("⚡ Params");
    auto* regenerate_action = toolbar->addAction("🔄 Regen");
    auto* pinned_action = toolbar->addAction("📌 Pinned");
    auto* screenshot_action = toolbar->addAction("📷 Screenshot");
    toolbar->addSeparator();
    auto* settings_action = toolbar->addAction("⚙ Settings");

    // Connections
    connect(search_edit_, &QLineEdit::textChanged, this, &MainWindow::on_search_changed);
    connect(conv_list_, &QListWidget::itemClicked, this, &MainWindow::select_conversation);
    connect(msg_entry_, &QLineEdit::returnPressed, this, &MainWindow::send_message);
    connect(send_btn_, &QPushButton::clicked, this, &MainWindow::send_message);
    connect(stop_btn_, &QPushButton::clicked, this, &MainWindow::stop_streaming);
    connect(mic_btn_, &QPushButton::toggled, this, &MainWindow::toggle_microphone);
    connect(attach_btn_, &QPushButton::clicked, this, &MainWindow::attach_image);
    connect(back_btn, &QPushButton::clicked, this, &MainWindow::hide_pinned_view);

    connect(new_action, &QAction::triggered, this, &MainWindow::new_conversation);
    connect(prompt_action, &QAction::triggered, this, &MainWindow::show_prompt_selector);
    connect(advanced_action, &QAction::triggered, this, &MainWindow::show_advanced_settings);
    connect(regenerate_action, &QAction::triggered, this, &MainWindow::regenerate_last);
    connect(pinned_action, &QAction::triggered, this, &MainWindow::show_pinned_view);
    connect(screenshot_action, &QAction::triggered, this, &MainWindow::take_screenshot);
    connect(settings_action, &QAction::triggered, this, &MainWindow::show_settings);

    // Headless timer for CLI-driven mode
    if (headless_) {
        headless_timer_ = new QTimer(this);
        connect(headless_timer_, &QTimer::timeout, []() {
            fprintf(stderr, "Agora running in headless mode. Use -platform xcb/wayland for GUI.\n");
            QApplication::quit();
        });
        headless_timer_->setSingleShot(true);
        headless_timer_->start(1000);
    }

    // Init recorder
    recorder_ = std::make_unique<AudioRecorder>();

    // Auto backup timer
    auto& ccfg = Config::instance();
    if (ccfg.auto_backup_enabled && ccfg.auto_backup_hours > 0) {
        auto_backup_timer_ = new QTimer(this);
        connect(auto_backup_timer_, &QTimer::timeout, this, &MainWindow::do_auto_backup);
        auto_backup_timer_->start(ccfg.auto_backup_hours * 3600 * 1000);
    }
}

MainWindow::~MainWindow() {
    if (recorder_ && recorder_->is_recording())
        recorder_->stop();
    if (gen_thread_.isRunning()) {
        gen_thread_.quit();
        gen_thread_.wait(1000);
    }
}

void MainWindow::set_database(std::shared_ptr<Database> db) {
    db_ = std::move(db);
    refresh_conversations();
}

void MainWindow::set_http_client(std::shared_ptr<HttpClient> client) {
    http_ = std::move(client);
}

// --- Helpers ---

QString MainWindow::generate_id() {
    auto now = std::chrono::system_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    return QString::number(us, 16);
}

QString MainWindow::format_time(int64_t ms) {
    auto t = static_cast<std::time_t>(ms / 1000);
    std::tm local{};
    localtime_r(&t, &local);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M", &local);
    return QString(buf);
}

QString MainWindow::status_text(MessageStatus s) {
    switch (s) {
        case MessageStatus::SENDING: return "Sending...";
        case MessageStatus::THINKING: return "Thinking...";
        case MessageStatus::TOOL_CALLING: return "Using tools...";
        case MessageStatus::ERROR: return "Error";
        default: return "";
    }
}

// --- Sidebar ---

void MainWindow::refresh_conversations() {
    if (!db_) return;
    conv_list_->clear();

    auto query = search_edit_->text().toLower();
    auto convs = db_->get_all_conversations();

    for (auto& conv : convs) {
        QString title = QString::fromStdString(conv.title);
        if (!query.isEmpty() && !title.toLower().contains(query))
            continue;

        auto* item = new QListWidgetItem(title);
        item->setData(Qt::UserRole, QString::fromStdString(conv.id));
        item->setToolTip(title);
        conv_list_->addItem(item);
    }
}

void MainWindow::select_conversation(QListWidgetItem* item) {
    if (!item) return;
    active_conv_id_ = item->data(Qt::UserRole).toString();
    messages_.clear();
    selected_branches_.clear();

    if (db_) {
        messages_ = db_->get_messages_for_conversation(active_conv_id_.toStdString());
        auto conv = db_->get_conversation(active_conv_id_.toStdString());
        selected_branches_ = conv.selected_branches;
        build_message_path();
    }

    render_messages();

    // Update pinned count
    if (db_) {
        auto pinned = db_->get_pinned_messages(active_conv_id_.toStdString());
        pinned_title_->setText(QString("Pinned Messages (%1)").arg(pinned.size()));
    }
}

void MainWindow::new_conversation() {
    QString id = generate_id();
    Conversation conv;
    conv.id = id.toStdString();
    conv.title = "New Chat";
    conv.last_updated = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    conv.system_prompt_id = Config::instance().active_system_prompt_id;
    conv.model_id = Config::instance().selected_model;

    if (db_) db_->upsert_conversation(conv);
    refresh_conversations();

    // Select it
    for (int i = 0; i < conv_list_->count(); i++) {
        auto* item = conv_list_->item(i);
        if (item->data(Qt::UserRole).toString() == id) {
            conv_list_->setCurrentItem(item);
            select_conversation(item);
            break;
        }
    }
}

void MainWindow::on_search_changed(const QString&) {
    refresh_conversations();
}

// --- Messages ---

QWidget* MainWindow::create_message_widget(const Message& msg) {
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(3);

    bool is_user = msg.participant == Participant::USER;
    bool is_error = msg.participant == Participant::ERROR;
    bool is_dark = (Config::instance().theme.mode == "dark");

    // Role label
    auto* role = new QLabel(is_user ? "You" : "Agora");
    role->setStyleSheet(is_dark
        ? "font-weight: bold; font-size: 11px; color: #8a8aae;"
        : "font-weight: bold; font-size: 11px; color: palette(mid);");
    layout->addWidget(role);

    // Thoughts (always create for streaming updates, hide if empty)
    bool has_thoughts = !msg.thoughts.empty() && msg.thoughts != "\n";
    auto* thought = new QLabel(QString::fromStdString(msg.thoughts));
    thought->setObjectName("msg_thought");
    thought->setWordWrap(true);
    thought->setStyleSheet(is_dark
        ? "background-color: #1e2a3e; color: #a0b0d0; border-radius: 6px;"
          "padding: 6px; font-style: italic; font-size: 12px;"
        : "background-color: rgba(128,128,128,0.1); border-radius: 6px;"
          "padding: 6px; font-style: italic; font-size: 12px;");
    thought->setVisible(has_thoughts);
    layout->addWidget(thought);

    // Text
    QString text = QString::fromStdString(msg.text);
    if (msg.streaming && !msg.streaming_text.empty())
        text = QString::fromStdString(msg.streaming_text);

    QString bubble_bg;
    if (is_user)
        bubble_bg = is_dark ? "#1e3a5f" : "#d0e8ff";
    else if (is_error)
        bubble_bg = is_dark ? "#3e1a1a" : "#ffe0e0";
    else
        bubble_bg = is_dark ? "#2a2a3e" : "#f0f0f0";

    QString bubble_text = is_dark ? "#d0d0e0" : "#000000";

    auto* text_label = new QLabel(text);
    text_label->setObjectName("msg_text");
    text_label->setWordWrap(true);
    text_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    text_label->setStyleSheet(QString(
        "background-color: %1; color: %2; border-radius: 10px; padding: 8px; font-size: 14px;"
    ).arg(bubble_bg, bubble_text));
    layout->addWidget(text_label);

    // Status
    QString st = status_text(msg.status);
    if (!st.isEmpty()) {
        auto* st_label = new QLabel(st);
        st_label->setStyleSheet("color: palette(mid); font-size: 10px;");
        layout->addWidget(st_label);
    }

    // Pin button + branch arrows (model messages)
    if (msg.participant == Participant::MODEL && msg.status == MessageStatus::SUCCESS) {
        auto* row = new QHBoxLayout;
        auto* pin_btn = new QPushButton(msg.pinned ? "★ Unpin" : "☆ Pin");
        pin_btn->setFlat(true);
        pin_btn->setMaximumWidth(80);
        pin_btn->setStyleSheet("font-size: 10px; padding: 2px;");
        QString mid = QString::fromStdString(msg.id);
        connect(pin_btn, &QPushButton::clicked, this, [this, mid]() {
            toggle_pin_message(mid);
        });
        row->addWidget(pin_btn);

        // Branch arrows — check precomputed sibling count
        if (!msg.parent_id.empty() && msg_sibling_counts_[msg.parent_id] > 1) {
            int count = msg_sibling_counts_[msg.parent_id];

            // Find this message's index among siblings
            int cur_idx = 1;
            if (db_) {
                auto all = db_->get_messages_for_conversation(active_conv_id_.toStdString());
                std::vector<Message> siblings;
                for (auto& m : all) {
                    if (m.parent_id == msg.parent_id) siblings.push_back(m);
                }
                std::sort(siblings.begin(), siblings.end(),
                    [](const Message& a, const Message& b) { return a.timestamp < b.timestamp; });
                for (int i = 0; i < (int)siblings.size(); i++) {
                    if (siblings[i].id == msg.id) { cur_idx = i + 1; break; }
                }
            }

            auto* prev_btn = new QPushButton("◀");
            prev_btn->setFlat(true);
            prev_btn->setMaximumWidth(28);
            prev_btn->setStyleSheet("font-size: 12px; padding: 0; border: none;");
            prev_btn->setToolTip("Previous version");
            QString pid = QString::fromStdString(msg.parent_id);
            connect(prev_btn, &QPushButton::clicked, this, [this, pid]() {
                switch_branch(pid, -1);
            });

            auto* branch_label = new QLabel(QString(" %1/%2 ").arg(cur_idx).arg(count));
            branch_label->setStyleSheet("font-size: 10px; color: palette(mid); background: transparent;");

            auto* next_btn = new QPushButton("▶");
            next_btn->setFlat(true);
            next_btn->setMaximumWidth(28);
            next_btn->setStyleSheet("font-size: 12px; padding: 0; border: none;");
            next_btn->setToolTip("Next version");
            connect(next_btn, &QPushButton::clicked, this, [this, pid]() {
                switch_branch(pid, 1);
            });

            row->addSpacing(6);
            row->addWidget(prev_btn);
            row->addWidget(branch_label);
            row->addWidget(next_btn);
        }
        row->addStretch();
        layout->addLayout(row);
    }

    // Timestamp
    auto* time_label = new QLabel(format_time(msg.timestamp));
    time_label->setStyleSheet("color: palette(mid); font-size: 9px;");
    layout->addWidget(time_label);

    // Separator
    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: palette(midlight);");
    layout->addWidget(sep);

    return widget;
}

void MainWindow::render_messages() {
    bool at_bottom = true;
    auto* sb = msg_list_->verticalScrollBar();
    if (sb) {
        at_bottom = (sb->value() >= sb->maximum() - 10);
    }

    // Precompute sibling counts for branch arrows (from DB only, no duplicate counting)
    msg_sibling_counts_.clear();
    if (db_) {
        auto all = db_->get_messages_for_conversation(active_conv_id_.toStdString());
        for (auto& m : all) {
            if (!m.parent_id.empty()) msg_sibling_counts_[m.parent_id]++;
        }
    }
    for (auto& kv : msg_sibling_counts_) {
        if (kv.second > 1) std::cerr << "[render] parent=" << kv.first << " sibling_count=" << kv.second << std::endl;
    }

    msg_list_->clear();

    std::lock_guard lock(msg_mutex_);
    for (auto& msg : messages_) {
        auto* widget = create_message_widget(msg);
        auto* item = new QListWidgetItem;
        item->setSizeHint(widget->sizeHint());
        item->setData(Qt::UserRole, QString::fromStdString(msg.id));
        msg_list_->addItem(item);
        msg_list_->setItemWidget(item, widget);
    }

    if (at_bottom) {
        scroll_to_bottom();
    }
}

void MainWindow::update_streaming_message() {
    bool at_bottom = true;
    auto* sb = msg_list_->verticalScrollBar();
    if (sb) at_bottom = (sb->value() >= sb->maximum() - 10);

    std::lock_guard lock(msg_mutex_);
    for (int i = 0; i < msg_list_->count(); i++) {
        auto* item = msg_list_->item(i);
        if (item->data(Qt::UserRole).toString().toStdString() == stream_model_msg_id_.toStdString()) {
            for (auto& msg : messages_) {
                if (msg.id == stream_model_msg_id_.toStdString()) {
                    auto* existing_widget = msg_list_->itemWidget(item);
                    if (existing_widget) {
                        auto labels = existing_widget->findChildren<QLabel*>();
                        for (auto* lbl : labels) {
                            if (lbl->objectName() == "msg_thought") {
                                QString thought = QString::fromStdString(msg.thoughts);
                                lbl->setText(thought);
                                lbl->setVisible(!thought.isEmpty() && thought != "\n");
                            }
                            if (lbl->objectName() == "msg_text") {
                                lbl->setText(QString::fromStdString(msg.text));
                            }
                        }
                        existing_widget->adjustSize();
                        item->setSizeHint(existing_widget->sizeHint());
                    }
                    break;
                }
            }
            if (at_bottom) scroll_to_bottom();
            return;
        }
    }
    if (at_bottom) scroll_to_bottom();
}

void MainWindow::scroll_to_bottom() {
    msg_list_->scrollToBottom();
}

void MainWindow::schedule_render() {
    if (render_pending_) return;
    render_pending_ = true;
    QMetaObject::invokeMethod(this, [this]() {
        render_pending_ = false;
        if (streaming_) {
            update_streaming_message();
        } else {
            render_messages();
        }
    }, Qt::QueuedConnection);
}

// --- Sending ---

void MainWindow::send_message() {
    QString text = msg_entry_->text().trimmed();
    if (text.isEmpty() || streaming_) return;
    msg_entry_->clear();
    do_send_message(text);
}

void MainWindow::stop_streaming() {
    streaming_ = false;
    set_input_enabled(true);
    render_messages();
}

void MainWindow::set_input_enabled(bool enabled) {
    msg_entry_->setEnabled(enabled);
    send_btn_->setVisible(enabled);
    stop_btn_->setVisible(!enabled);
    if (enabled) {
        status_label_->setVisible(false);
    }
}

void MainWindow::handle_command(const QString& text) {
    auto& cfg = Config::instance();

    // Resolve current effective prompt for this conversation
    auto get_conv_prompt_id = [this, &cfg]() -> std::string {
        if (!db_) return cfg.active_system_prompt_id;
        auto conv = db_->get_conversation(active_conv_id_.toStdString());
        if (!conv.system_prompt_id.empty()) return conv.system_prompt_id;
        return cfg.active_system_prompt_id;
    };

    auto set_conv_prompt = [this](const std::string& prompt_id) {
        if (!db_) return;
        auto conv = db_->get_conversation(active_conv_id_.toStdString());
        if (!conv.id.empty()) {
            conv.system_prompt_id = prompt_id;
            db_->upsert_conversation(conv);
        }
    };

    auto add_status = [this](const QString& msg) {
        Message sm;
        sm.id = ("cmd_" + generate_id()).toStdString();
        sm.conversation_id = active_conv_id_.toStdString();
        sm.participant = Participant::ERROR;
        sm.status = MessageStatus::SUCCESS;
        sm.text = msg.toStdString();
        sm.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        {
            std::lock_guard lock(msg_mutex_);
            messages_.push_back(sm);
        }
        if (db_) db_->upsert_message(sm);
        render_messages();
    };

    QString lower = text.toLower();

    // /system or /prompt — set or view system prompt
    if (lower.startsWith("/system") || lower.startsWith("/prompt")) {
        std::string current_id = get_conv_prompt_id();
        int space = text.indexOf(' ');
        if (space < 0) {
            QString current = "No active system prompt.";
            for (auto& sp : cfg.system_prompts) {
                if (sp.id == current_id) {
                    current = QString::fromStdString(sp.content).left(500);
                    break;
                }
            }
            add_status("Current system prompt:\n" + current);
            return;
        }

        QString rest = text.mid(space + 1).trimmed();

        // /system list — list all prompts
        if (rest.toLower() == "list") {
            QString msg = "System prompts:\n";
            for (auto& sp : cfg.system_prompts) {
                QString markers;
                if (sp.id == cfg.active_system_prompt_id) markers += " [global]";
                if (sp.id == current_id) markers += " [active]";
                msg += "  • " + QString::fromStdString(sp.title) + markers + "\n";
            }
            add_status(msg);
            return;
        }

        // /system use <name> — switch conversation to named prompt
        if (rest.toLower().startsWith("use ")) {
            QString name = rest.mid(4).trimmed();
            bool found = false;
            for (auto& sp : cfg.system_prompts) {
                if (QString::fromStdString(sp.title).toLower() == name.toLower()) {
                    set_conv_prompt(sp.id);
                    add_status("Switched to system prompt: " + QString::fromStdString(sp.title) + " (this conversation)");
                    found = true;
                    break;
                }
            }
            if (!found) add_status("No prompt found with name: " + name + "\nUse /system list to see available prompts.");
            return;
        }

        // /system reset — clear conversation prompt (use global default)
        if (rest.toLower() == "reset") {
            set_conv_prompt("");
            add_status("Reset to global default system prompt.");
            return;
        }

        // /system global <name> — set global active prompt
        if (rest.toLower().startsWith("global ")) {
            QString name = rest.mid(7).trimmed();
            bool found = false;
            for (auto& sp : cfg.system_prompts) {
                if (QString::fromStdString(sp.title).toLower() == name.toLower()) {
                    cfg.active_system_prompt_id = sp.id;
                    const char* home = getenv("HOME");
                    cfg.save(std::string(home ? home : "/tmp") + "/.config/agora.json");
                    add_status("Global system prompt set to: " + QString::fromStdString(sp.title));
                    found = true;
                    break;
                }
            }
            if (!found) add_status("No prompt found with name: " + name);
            return;
        }

        // /system <text> — set new inline system prompt for this conversation
        SystemPromptEntry sp;
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        sp.id = "inline_" + std::to_string(now);
        sp.title = "Inline Prompt";
        sp.content = rest.toStdString();
        cfg.system_prompts.push_back(sp);
        set_conv_prompt(sp.id);

        const char* home = getenv("HOME");
        cfg.save(std::string(home ? home : "/tmp") + "/.config/agora.json");
        if (db_) db_->upsert_system_prompt(sp);

        add_status("System prompt set (this conversation):\n" + rest.left(500));
        return;
    }

    // /help
    if (lower == "/help") {
        add_status(QString(
            "Commands:\n"
            "  /system <text>        Set system prompt for this conversation\n"
            "  /system               Show current system prompt\n"
            "  /system list          List all saved prompts\n"
            "  /system use <name>    Switch this conversation to a named prompt\n"
            "  /system reset         Use global default for this conversation\n"
            "  /system global <name> Set the global default prompt\n"
            "  /help                 Show this help\n"
        ));
        return;
    }

    add_status("Unknown command: " + text + "\nType /help for available commands.");
}

void MainWindow::do_send_message(const QString& text, const QString& preg_parent_id) {
    if (!db_) return;
    bool is_regeneration = !preg_parent_id.isEmpty();

    // Slash commands (not for regen)
    if (!is_regeneration && text.startsWith('/')) {
        handle_command(text);
        return;
    }

    // Auto-create conversation
    if (active_conv_id_.isEmpty()) {
        new_conversation();
        if (active_conv_id_.isEmpty()) return;
    }

    // Create user message (skip for regeneration — reuse existing parent)
    QString user_id;
    if (!is_regeneration) {
        user_id = "msg_" + generate_id();
        Message user_msg;
        user_msg.id = user_id.toStdString();
        user_msg.conversation_id = active_conv_id_.toStdString();
        user_msg.text = text.toStdString();
        user_msg.participant = Participant::USER;
        user_msg.status = MessageStatus::SUCCESS;
        user_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        db_->upsert_message(user_msg);

        {
            std::lock_guard lock(msg_mutex_);
            messages_.push_back(user_msg);
        }
    } else {
        user_id = preg_parent_id;
    }

    // Build provider config
    auto& cfg = Config::instance();
    GenConfig gc;
    QString full = QString::fromStdString(cfg.selected_model);
    auto colon = full.indexOf(':');
    if (colon >= 0) {
        gc.provider = full.left(colon).toLower().toStdString();
        gc.model = full.mid(colon + 1).toStdString();
    } else {
        gc.provider = "openai";
        gc.model = full.toStdString();
    }
    gc.api_key = cfg.get_api_key(gc.provider);
    gc.base_url = cfg.get_base_url(gc.provider);

    std::cerr << "[agora] provider=" << gc.provider
              << " model=" << gc.model
              << " base_url=" << (gc.base_url.empty() ? "(default)" : gc.base_url)
              << " has_key=" << (!gc.api_key.empty() ? "yes" : "no")
              << " tor=" << (cfg.tor.enabled ? "on" : "off")
              << std::endl;

    // Resolve prompt: per-conversation > global active > first available
    std::string prompt_id;
    {
        auto conv = db_->get_conversation(active_conv_id_.toStdString());
        prompt_id = conv.system_prompt_id; // may be empty
    }
    if (prompt_id.empty()) prompt_id = cfg.active_system_prompt_id;
    if (prompt_id.empty() && !cfg.system_prompts.empty()) prompt_id = cfg.system_prompts[0].id;

    for (auto& sp : cfg.system_prompts) {
        if (sp.id == prompt_id) {
            gc.system_prompt = sp.content;
            gc.user_prepend = sp.user_prepend;
            gc.user_postpend = sp.user_postpend;
            std::cerr << "[agora] prompt=" << sp.title << std::endl;
            break;
        }
    }
    gc.max_context_window = cfg.max_context_window;
    gc.thinking_enabled = cfg.thinking_enabled;
    gc.temperature = cfg.temperature;
    gc.max_tokens = cfg.max_tokens;
    gc.top_p = cfg.top_p;
    gc.frequency_penalty = cfg.frequency_penalty;
    gc.presence_penalty = cfg.presence_penalty;

    provider_ = Provider::create(gc, *http_);

    if (cfg.tor.enabled) {
        http_->set_proxy(cfg.tor.socks_host, cfg.tor.socks_port);
        http_->set_proxy_enabled(true);
    }

    // Create streaming placeholder (unique ID each time)
    QString model_id = "msg_" + generate_id();
    stream_model_msg_id_ = model_id;

    Message model_msg;
    model_msg.id = model_id.toStdString();
    model_msg.conversation_id = active_conv_id_.toStdString();
    model_msg.parent_id = user_id.toStdString();
    model_msg.participant = Participant::MODEL;
    model_msg.status = MessageStatus::SENDING;
    model_msg.streaming = true;
    model_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    {
        std::lock_guard lock(msg_mutex_);
        messages_.push_back(model_msg);
    }

    streaming_ = true;
    stream_text_.clear();
    stream_thoughts_.clear();
    set_input_enabled(false);
    status_label_->setText("Generating...");
    status_label_->setVisible(true);

    render_messages();

    // Copy messages for generation
    std::vector<Message> msg_copy;
    {
        std::lock_guard lock(msg_mutex_);
        msg_copy.assign(messages_.begin(), messages_.end() - 1);
    }

    // Run generation in background with QtConcurrent / lambda
    gen_thread_.quit();
    gen_thread_.wait();

    auto worker = [this, msg_copy, model_id, user_id]() {
        provider_->generate(msg_copy, [this, model_id](const StreamEvent& evt) -> bool {
            if (!streaming_) return false;

            switch (evt.type) {
                case StreamEvent::TEXT:
                    stream_text_ += QString::fromStdString(evt.text);
                    break;
                case StreamEvent::THOUGHT:
                    stream_thoughts_ += QString::fromStdString(evt.thought);
                    break;
                case StreamEvent::DONE:
                    {
                        std::lock_guard lock(msg_mutex_);
                        for (auto& m : messages_) {
                            if (m.id == model_id.toStdString()) {
                                m.text = stream_text_.toStdString();
                                m.thoughts = stream_thoughts_.toStdString();
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
                        std::lock_guard lock(msg_mutex_);
                        for (auto& m : messages_) {
                            if (m.id == model_id.toStdString()) {
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

            {
                std::lock_guard lock(msg_mutex_);
                for (auto& m : messages_) {
                    if (m.id == model_id.toStdString()) {
                        m.text = stream_text_.toStdString();
                        m.thoughts = stream_thoughts_.toStdString();
                        break;
                    }
                }
            }

            schedule_render();

            return true;
        });

        // Finalize
        QMetaObject::invokeMethod(this, [this, model_id, user_id]() {
            streaming_ = false;
            set_input_enabled(true);

            // Persist
            if (db_ && !stream_text_.isEmpty()) {
                Message final_msg;
                final_msg.id = model_id.toStdString();
                final_msg.conversation_id = active_conv_id_.toStdString();
                final_msg.parent_id = user_id.toStdString();
                final_msg.text = stream_text_.toStdString();
                final_msg.thoughts = stream_thoughts_.toStdString();
                final_msg.participant = Participant::MODEL;
                final_msg.status = MessageStatus::SUCCESS;
                final_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                final_msg.model_name = Config::instance().selected_model;
                db_->upsert_message(final_msg);

                // Title generation: update title from first exchange
                auto& ccfg = Config::instance();
                if (ccfg.title_gen_enabled) {
                    auto conv = db_->get_conversation(active_conv_id_.toStdString());
                    auto msgs = db_->get_messages_for_conversation(active_conv_id_.toStdString());
                    int model_count = 0;
                    for (auto& m : msgs) {
                        if (m.participant == Participant::MODEL && m.status == MessageStatus::SUCCESS) model_count++;
                    }
                    if (model_count == 1 && !conv.id.empty()) {
                        // First model response — generate title from user+model text
                        QString title;
                        for (auto& m : msgs) {
                            if (m.participant == Participant::USER && !m.text.empty())
                                title = QString::fromStdString(m.text).left(40);
                        }
                        if (!title.isEmpty()) {
                            // Also include first few words of model response
                            QString resp = stream_text_.left(40);
                            if (!resp.isEmpty()) title = title + " — " + resp;
                            title = title.left(60).trimmed();
                            conv.title = title.toStdString();
                            db_->upsert_conversation(conv);
                            refresh_conversations();
                        }
                    }
                }
            }

            render_pending_ = false;
            render_messages();
        }, Qt::QueuedConnection);
    };

    // Use QThread with a lambda via QtConcurrent-style approach
    auto* thread = QThread::create(worker);
    gen_thread_.setObjectName("gen");
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

// --- Pinned Messages ---

void MainWindow::toggle_pin_message(const QString& msg_id) {
    if (!db_) return;
    auto msg = db_->get_message(msg_id.toStdString());
    if (msg.id.empty()) return;
    db_->pin_message(msg_id.toStdString(), !msg.pinned);
    render_messages();

    if (db_) {
        auto pinned = db_->get_pinned_messages(active_conv_id_.toStdString());
        pinned_title_->setText(QString("Pinned Messages (%1)").arg(pinned.size()));
    }
}

void MainWindow::show_pinned_view() {
    if (!db_ || active_conv_id_.isEmpty()) return;
    pinned_list_->clear();

    auto pinned = db_->get_pinned_messages(active_conv_id_.toStdString());
    for (auto& m : pinned) {
        QString text = QString::fromStdString(m.text).left(200);
        auto* item = new QListWidgetItem(
            QString("[%1] %2").arg(format_time(m.timestamp), text));
        item->setToolTip(text);
        item->setData(Qt::UserRole, QString::fromStdString(m.id));
        pinned_list_->addItem(item);
    }

    pinned_title_->setText(QString("Pinned Messages (%1)").arg(pinned.size()));
    sidebar_stack_->setCurrentIndex(1);
}

void MainWindow::hide_pinned_view() {
    sidebar_stack_->setCurrentIndex(0);
}

// --- Microphone ---

void MainWindow::toggle_microphone() {
    auto& cfg = Config::instance();

    if (mic_active_) {
        recorder_->stop();
        mic_active_ = false;
        mic_btn_->setText("🎤");
        mic_btn_->setStyleSheet("");
        msg_entry_->setPlaceholderText("Type a message...");
        msg_entry_->setEnabled(true);
    } else {
        if (!cfg.stt.enabled || cfg.stt.endpoint_url.empty()) {
            mic_btn_->setChecked(false);
            return;
        }

        bool ok = recorder_->start(cfg.stt.endpoint_url,
            [this](const std::string& text, bool final) {
                QMetaObject::invokeMethod(this, [this, text, final]() {
                    msg_entry_->setText(QString::fromStdString(text));
                    if (final) {
                        mic_btn_->setChecked(false);
                        toggle_microphone();
                        if (!text.empty()) send_message();
                    }
                }, Qt::QueuedConnection);
            });

        if (ok) {
            mic_active_ = true;
            mic_btn_->setText("🔴");
            mic_btn_->setStyleSheet("background-color: red; color: white;");
            msg_entry_->setPlaceholderText("Listening...");
            msg_entry_->setEnabled(false);
        } else {
            mic_btn_->setChecked(false);
        }
    }
}

// --- Screenshot ---

void MainWindow::take_screenshot() {
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        // Headless mode — capture widget instead
        QPixmap pix = QWidget::grab();
        QString path = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                       + "/agora-" + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss") + ".png";
        QDir().mkpath(QFileInfo(path).absolutePath());
        pix.save(path);
        status_label_->setText("Screenshot saved: " + path);
        status_label_->setVisible(true);
        QTimer::singleShot(3000, [this]() { status_label_->setVisible(false); });
        return;
    }

    QPixmap pix = screen->grabWindow(winId());
    QString path = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                   + "/agora-" + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss") + ".png";
    QDir().mkpath(QFileInfo(path).absolutePath());
    pix.save(path);

    status_label_->setText("Screenshot saved: " + path);
    status_label_->setVisible(true);
    QTimer::singleShot(3000, [this]() { status_label_->setVisible(false); });
}

// --- Attach image ---

void MainWindow::attach_image() {
    QString path = QFileDialog::getOpenFileName(this, "Attach Image", "",
        "Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp)");
    if (path.isEmpty()) return;
    attached_image_path_ = path;
    attach_btn_->setStyleSheet("background-color: #5a6abe; color: white; border-radius: 8px;");
    msg_entry_->setPlaceholderText("Image attached: " + QFileInfo(path).fileName() + " — type a message...");
}

// --- Advanced Settings ---

void MainWindow::show_advanced_settings() {
    auto& cfg = Config::instance();
    auto* dialog = new QDialog(this);
    dialog->setWindowTitle("Generation Parameters");
    dialog->setMinimumWidth(320);
    auto* layout = new QFormLayout(dialog);

    auto* temp_edit = new QLineEdit(QString::number(cfg.temperature, 'f', 1));
    layout->addRow("Temperature (0.0-2.0):", temp_edit);

    auto* tokens_edit = new QLineEdit(QString::number(cfg.max_tokens));
    layout->addRow("Max Tokens:", tokens_edit);

    auto* top_p_edit = new QLineEdit(QString::number(cfg.top_p, 'f', 2));
    layout->addRow("Top P (0.0-1.0):", top_p_edit);

    auto* think_toggle = new QCheckBox("Enable Thinking");
    think_toggle->setChecked(cfg.thinking_enabled);
    layout->addRow(think_toggle);

    auto* ctx_edit = new QLineEdit(QString::number(cfg.max_context_window));
    layout->addRow("Context Window (messages):", ctx_edit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, [&]() {
        cfg.temperature = std::max(0.0f, std::min(2.0f, temp_edit->text().toFloat()));
        cfg.max_tokens = std::max(1, tokens_edit->text().toInt());
        cfg.top_p = std::max(0.0f, std::min(1.0f, top_p_edit->text().toFloat()));
        cfg.thinking_enabled = think_toggle->isChecked();
        cfg.max_context_window = std::max(5, ctx_edit->text().toInt());
        const char* home = getenv("HOME");
        cfg.save(std::string(home ? home : "/tmp") + "/.config/agora.json");
        dialog->accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    dialog->exec();
    delete dialog;
}

// --- Regenerate ---

void MainWindow::regenerate_last() {
    if (streaming_ || messages_.empty()) return;

    QString user_text;
    QString parent_id;
    {
        std::lock_guard lock(msg_mutex_);
        for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
            if (it->participant == Participant::USER) {
                user_text = QString::fromStdString(it->text);
                parent_id = QString::fromStdString(it->id);
                break;
            }
        }
        // Remove current model response from visible path so new one replaces it
        if (!messages_.empty() && messages_.back().participant == Participant::MODEL) {
            messages_.pop_back();
        }
    }
    if (user_text.isEmpty()) return;

    do_send_message(user_text, parent_id);
}

// --- Branching ---

void MainWindow::build_message_path() {
    std::vector<Message> all_msgs = messages_;
    std::cerr << "[branch] building path from " << all_msgs.size() << " messages, "
              << selected_branches_.size() << " branches" << std::endl;

    messages_.clear();

    std::map<std::string, std::vector<Message*>> by_parent;
    for (auto& m : all_msgs) {
        by_parent[m.parent_id].push_back(&m);
    }

    std::function<void(const std::string&)> walk = [&](const std::string& parent) {
        auto it = by_parent.find(parent);
        if (it == by_parent.end()) return;

        std::sort(it->second.begin(), it->second.end(),
            [](Message* a, Message* b) { return a->timestamp < b->timestamp; });

        Message* chosen = nullptr;
        auto branch_it = selected_branches_.find(parent);
        if (branch_it != selected_branches_.end()) {
            for (auto* m : it->second) {
                if (m->id == branch_it->second) { chosen = m; break; }
            }
        }
        if (!chosen && !it->second.empty()) chosen = it->second.back();
        std::cerr << "[branch] parent=" << parent << " siblings=" << it->second.size()
                  << " chosen=" << (chosen ? chosen->id : "null") << std::endl;

        if (chosen) {
            messages_.push_back(*chosen);
            walk(chosen->id);
        }
    };

    auto root_it = by_parent.find("");
    if (root_it != by_parent.end()) {
        std::sort(root_it->second.begin(), root_it->second.end(),
            [](Message* a, Message* b) { return a->timestamp < b->timestamp; });
        for (auto* m : root_it->second) {
            if (m->participant != Participant::MODEL) {
                messages_.push_back(*m);
                walk(m->id);
            }
        }
    }
    std::cerr << "[branch] path result: " << messages_.size() << " messages" << std::endl;
}

void MainWindow::switch_branch(const QString& parent_id, int direction) {
    if (!db_) return;
    std::string pid = parent_id.toStdString();

    auto all_msgs = db_->get_messages_for_conversation(active_conv_id_.toStdString());

    std::vector<Message> siblings;
    for (auto& m : all_msgs) {
        if (m.parent_id == pid) siblings.push_back(m);
    }
    std::cerr << "[branch] switch pid=" << pid << " siblings=" << siblings.size() << std::endl;
    if (siblings.size() < 2) return;

    std::sort(siblings.begin(), siblings.end(),
        [](const Message& a, const Message& b) { return a.timestamp < b.timestamp; });

    int current = -1;
    auto it = selected_branches_.find(pid);
    if (it != selected_branches_.end()) {
        for (int i = 0; i < (int)siblings.size(); i++) {
            if (siblings[i].id == it->second) { current = i; break; }
        }
    }
    if (current < 0) current = siblings.size() - 1;

    int next = (current + direction + siblings.size()) % siblings.size();
    selected_branches_[pid] = siblings[next].id;
    db_->set_conversation_branch(active_conv_id_.toStdString(), pid, siblings[next].id);
    std::cerr << "[branch] switched from idx " << current << " to " << next
              << " (id=" << siblings[next].id << " text=" << siblings[next].text.substr(0,40) << ")" << std::endl;

    messages_ = db_->get_messages_for_conversation(active_conv_id_.toStdString());
    build_message_path();
    render_messages();
}

// --- Prompt Selector ---

void MainWindow::show_prompt_selector() {
    if (active_conv_id_.isEmpty()) return;

    auto& cfg = Config::instance();

    // Get conversation's current prompt id
    std::string conv_prompt_id;
    if (db_) {
        auto conv = db_->get_conversation(active_conv_id_.toStdString());
        conv_prompt_id = conv.system_prompt_id;
    }

    auto* dialog = new QDialog(this);
    dialog->setWindowTitle("System Prompt");
    dialog->setMinimumWidth(360);

    auto* layout = new QVBoxLayout(dialog);
    auto* label = new QLabel("Choose a system prompt for this conversation:");
    label->setWordWrap(true);
    layout->addWidget(label);

    auto* group = new QButtonGroup(dialog);
    auto* scroll = new QScrollArea;
    auto* scroll_widget = new QWidget;
    auto* scroll_layout = new QVBoxLayout(scroll_widget);

    // "Global default" option
    auto* global_radio = new QRadioButton("Global default (use active prompt)");
    if (conv_prompt_id.empty()) global_radio->setChecked(true);
    group->addButton(global_radio, -1);
    scroll_layout->addWidget(global_radio);

    // Saved prompts
    int idx = 0;
    for (auto& sp : cfg.system_prompts) {
        QString line = QString::fromStdString(sp.title);
        auto* radio = new QRadioButton(line);
        radio->setToolTip(QString::fromStdString(sp.content).left(120));
        if (conv_prompt_id == sp.id) radio->setChecked(true);
        group->addButton(radio, idx);
        scroll_layout->addWidget(radio);
        idx++;
    }

    scroll_layout->addStretch();
    scroll->setWidget(scroll_widget);
    scroll->setWidgetResizable(true);
    layout->addWidget(scroll);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, [&, dialog, group]() {
        int checked = group->checkedId();
        std::string new_prompt_id;
        if (checked >= 0) {
            new_prompt_id = cfg.system_prompts[checked].id;
        }
        // else -1 means global default (empty)

        if (db_) {
            auto conv = db_->get_conversation(active_conv_id_.toStdString());
            if (!conv.id.empty()) {
                conv.system_prompt_id = new_prompt_id;
                db_->upsert_conversation(conv);
            }
        }

        // Show confirmation in chat
        std::string title = "[Global default]";
        for (auto& sp : cfg.system_prompts) {
            if (sp.id == new_prompt_id) { title = sp.title; break; }
        }
        Message sm;
        sm.id = ("psel_" + generate_id()).toStdString();
        sm.conversation_id = active_conv_id_.toStdString();
        sm.participant = Participant::ERROR;
        sm.status = MessageStatus::SUCCESS;
        sm.text = "System prompt set to: " + title;
        sm.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        {
            std::lock_guard lock(msg_mutex_);
            messages_.push_back(sm);
        }
        if (db_) db_->upsert_message(sm);
        render_messages();

        dialog->accept();
    });

    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    dialog->exec();
    delete dialog;
}

// --- Auto Backup ---

void MainWindow::do_auto_backup() {
    auto& cfg = Config::instance();
    const char* home = getenv("HOME");
    std::string out = std::string(home ? home : "/tmp") + "/Agora_backup_" +
        QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss").toStdString() + ".agora";
    auto result = export_data(out, cfg.db_path,
        std::string(home ? home : "/tmp") + "/.config/agora.json",
        cfg.memory_dir, false);
    if (result.success) {
        std::cerr << "[auto-backup] " << out << " (" << result.conversations << " convs)" << std::endl;
    }
}

// --- Settings ---

void MainWindow::show_settings() {
    auto* dialog = new QDialog(this);
    dialog->setWindowTitle("Settings");
    dialog->setMinimumSize(400, 450);

    auto* layout = new QVBoxLayout(dialog);
    auto* tabs = new QTabWidget;
    layout->addWidget(tabs);

    auto& cfg = Config::instance();

    // Load system prompts from DB into config (sync)
    if (db_) {
        auto db_prompts = db_->get_all_system_prompts();
        for (auto& db_sp : db_prompts) {
            bool found = false;
            for (auto& c_sp : cfg.system_prompts) {
                if (c_sp.id == db_sp.id) { found = true; break; }
            }
            if (!found) cfg.system_prompts.push_back(db_sp);
        }
    }

    // General tab
    auto* general = new QWidget;
    auto* gen_form = new QFormLayout(general);
    auto* model_edit = new QLineEdit(QString::fromStdString(cfg.selected_model));
    model_edit->setPlaceholderText("OpenAI:gpt-4o");
    gen_form->addRow("Default Model:", model_edit);

    auto* theme_combo = new QComboBox;
    theme_combo->addItem("Light", "light");
    theme_combo->addItem("Dark", "dark");
    theme_combo->addItem("Follow System", "system");
    QString current_theme = QString::fromStdString(cfg.theme.mode);
    int ti = theme_combo->findData(current_theme);
    if (ti >= 0) theme_combo->setCurrentIndex(ti);
    gen_form->addRow("Theme:", theme_combo);
    tabs->addTab(general, "General");

    // System Prompt tab
    auto* sysprompt_tab = new QWidget;
    auto* sp_layout = new QVBoxLayout(sysprompt_tab);

    auto* sp_combo = new QComboBox;
    for (auto& sp : cfg.system_prompts) {
        sp_combo->addItem(QString::fromStdString(sp.title), QString::fromStdString(sp.id));
    }
    sp_layout->addWidget(new QLabel("Select Prompt:"));
    sp_layout->addWidget(sp_combo);

    auto* sp_title_label = new QLabel("Title:");
    auto* sp_title = new QLineEdit;
    sp_layout->addWidget(sp_title_label);
    sp_layout->addWidget(sp_title);

    auto* sp_content_label = new QLabel("System Prompt:");
    auto* sp_content = new QTextEdit;
    sp_content->setPlaceholderText("Enter system prompt content. Variables: {date}, {time}");
    sp_content->setMinimumHeight(120);
    sp_layout->addWidget(sp_content_label);
    sp_layout->addWidget(sp_content);

    auto* sp_prepend_label = new QLabel("User Message Prepend:");
    auto* sp_prepend = new QLineEdit;
    sp_prepend->setPlaceholderText("Text prepended to each user message");
    sp_layout->addWidget(sp_prepend_label);
    sp_layout->addWidget(sp_prepend);

    auto* sp_postpend_label = new QLabel("User Message Postpend:");
    auto* sp_postpend = new QLineEdit;
    sp_postpend->setPlaceholderText("Text appended to each user message");
    sp_layout->addWidget(sp_postpend_label);
    sp_layout->addWidget(sp_postpend);

    auto* sp_button_row = new QHBoxLayout;
    auto* sp_add_btn = new QPushButton("+ New");
    auto* sp_delete_btn = new QPushButton("Delete");
    auto* sp_active_btn = new QPushButton("Set as Active");
    sp_active_btn->setStyleSheet("QPushButton { font-weight: bold; }");
    sp_button_row->addWidget(sp_add_btn);
    sp_button_row->addWidget(sp_delete_btn);
    sp_button_row->addStretch();
    sp_button_row->addWidget(sp_active_btn);
    sp_layout->addLayout(sp_button_row);

    auto* sp_active_label = new QLabel;
    sp_active_label->setStyleSheet("color: green; font-weight: bold; font-size: 11px;");
    sp_layout->addWidget(sp_active_label);
    sp_layout->addStretch();

    tabs->addTab(sysprompt_tab, "System Prompt");

    // Populate fields when a prompt is selected
    auto populate_sp = [&cfg, sp_title, sp_content, sp_prepend, sp_postpend, sp_active_label](const QString& id) {
        for (auto& sp : cfg.system_prompts) {
            if (sp.id == id.toStdString()) {
                sp_title->setText(QString::fromStdString(sp.title));
                sp_content->setPlainText(QString::fromStdString(sp.content));
                sp_prepend->setText(QString::fromStdString(sp.user_prepend));
                sp_postpend->setText(QString::fromStdString(sp.user_postpend));
                sp_active_label->setText(cfg.active_system_prompt_id == sp.id
                    ? "★ Active system prompt"
                    : "");
                return;
            }
        }
        sp_title->clear();
        sp_content->clear();
        sp_prepend->clear();
        sp_postpend->clear();
        sp_active_label->clear();
    };

    // Save current fields back into the selected prompt
    auto save_sp_fields = [&cfg, sp_combo, sp_title, sp_content, sp_prepend, sp_postpend]() {
        QString id = sp_combo->currentData().toString();
        if (id.isEmpty()) return;
        for (auto& sp : cfg.system_prompts) {
            if (sp.id == id.toStdString()) {
                sp.title = sp_title->text().toStdString();
                sp.content = sp_content->toPlainText().toStdString();
                sp.user_prepend = sp_prepend->text().toStdString();
                sp.user_postpend = sp_postpend->text().toStdString();
                sp_combo->setItemText(sp_combo->currentIndex(), QString::fromStdString(sp.title));
                return;
            }
        }
    };

    QObject::connect(sp_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int) {
        save_sp_fields();
        populate_sp(sp_combo->currentData().toString());
    });

    QObject::connect(sp_add_btn, &QPushButton::clicked, [&]() {
        save_sp_fields();
        SystemPromptEntry sp;
        sp.id = "prompt_" + std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        sp.title = "New Prompt";
        sp.content = "You are a helpful AI assistant.\n\n<current_date>{date}</current_date>";
        cfg.system_prompts.push_back(sp);
        sp_combo->addItem(QString::fromStdString(sp.title), QString::fromStdString(sp.id));
        sp_combo->setCurrentIndex(sp_combo->count() - 1);
    });

    QObject::connect(sp_delete_btn, &QPushButton::clicked, [&]() {
        if (cfg.system_prompts.size() <= 1) return;
        QString id = sp_combo->currentData().toString();
        cfg.system_prompts.erase(
            std::remove_if(cfg.system_prompts.begin(), cfg.system_prompts.end(),
                [&](auto& s) { return s.id == id.toStdString(); }),
            cfg.system_prompts.end());
        sp_combo->removeItem(sp_combo->currentIndex());
        if (sp_combo->count() > 0) {
            sp_combo->setCurrentIndex(0);
            populate_sp(sp_combo->currentData().toString());
        }
    });

    QObject::connect(sp_active_btn, &QPushButton::clicked, [&]() {
        save_sp_fields();
        cfg.active_system_prompt_id = sp_combo->currentData().toString().toStdString();
        sp_active_label->setText("★ Active system prompt");
    });

    // Initial populate
    if (sp_combo->count() > 0) {
        sp_combo->setCurrentIndex(0);
        populate_sp(sp_combo->currentData().toString());
    }

    // API tab
    auto* api = new QWidget;
    auto* api_form = new QFormLayout(api);
    auto* openai_key = new QLineEdit(QString::fromStdString(cfg.get_api_key("openai")));
    openai_key->setEchoMode(QLineEdit::Password);
    api_form->addRow("OpenAI API Key:", openai_key);
    auto* anthro_key = new QLineEdit(QString::fromStdString(cfg.get_api_key("anthropic")));
    anthro_key->setEchoMode(QLineEdit::Password);
    api_form->addRow("Anthropic API Key:", anthro_key);
    auto* base_url = new QLineEdit(QString::fromStdString(cfg.get_base_url("openai")));
    api_form->addRow("OpenAI Base URL:", base_url);
    tabs->addTab(api, "API Keys");

    // Tor tab
    auto* tor_tab = new QWidget;
    auto* tor_form = new QFormLayout(tor_tab);
    auto* tor_toggle = new QCheckBox("Enable Tor SOCKS5 proxy");
    tor_toggle->setChecked(cfg.tor.enabled);
    tor_form->addRow(tor_toggle);
    auto* tor_host = new QLineEdit(QString::fromStdString(cfg.tor.socks_host));
    tor_form->addRow("SOCKS5 Host:", tor_host);
    auto* tor_port = new QLineEdit(QString::number(cfg.tor.socks_port));
    tor_form->addRow("SOCKS5 Port:", tor_port);
    tabs->addTab(tor_tab, "Tor");

    // STT tab
    auto* stt_tab = new QWidget;
    auto* stt_form = new QFormLayout(stt_tab);
    auto* stt_toggle = new QCheckBox("Enable speech-to-text");
    stt_toggle->setChecked(cfg.stt.enabled);
    stt_form->addRow(stt_toggle);
    auto* stt_url = new QLineEdit(QString::fromStdString(cfg.stt.endpoint_url));
    stt_url->setPlaceholderText("http://192.168.1.100:8080/stt");
    stt_form->addRow("STT Endpoint:", stt_url);
    tabs->addTab(stt_tab, "Speech");

    // --- Data tab (export/import) ---
    auto* data_tab = new QWidget;
    auto* data_layout = new QVBoxLayout(data_tab);
    data_layout->setSpacing(10);

    auto* data_info = new QLabel("Export or import conversations, prompts, settings, and memories.\n"
                                  "Format: .agora (ZIP) — compatible with Agora for Android.");
    data_info->setWordWrap(true);
    data_layout->addWidget(data_info);

    auto* export_btn = new QPushButton("📤 Export Data");
    export_btn->setMinimumHeight(36);
    data_layout->addWidget(export_btn);

    auto* import_btn = new QPushButton("📥 Import Data");
    import_btn->setMinimumHeight(36);
    data_layout->addWidget(import_btn);

    auto* third_section = new QLabel("Third-party import:");
    data_layout->addWidget(third_section);

    auto* chatgpt_btn = new QPushButton("Import from ChatGPT");
    chatgpt_btn->setMinimumHeight(32);
    data_layout->addWidget(chatgpt_btn);

    auto* claude_btn = new QPushButton("Import from Claude");
    claude_btn->setMinimumHeight(32);
    data_layout->addWidget(claude_btn);

    auto* data_status = new QLabel;
    data_status->setWordWrap(true);
    data_status->setStyleSheet("color: palette(mid); font-size: 11px;");
    data_layout->addWidget(data_status);
    data_layout->addStretch();

    QObject::connect(export_btn, &QPushButton::clicked, [data_status]() {
        auto& cfg = Config::instance();
        const char* home = getenv("HOME");
        std::string out_path = std::string(home ? home : "/tmp") + "/agora_export.agora";

        auto result = export_data(out_path, cfg.db_path,
            std::string(home ? home : "/tmp") + "/.config/agora.json",
            cfg.memory_dir, false);

        if (result.success) {
            data_status->setText(QString("Exported to %1\n%2 conversations, %3 messages, %4 prompts, %5 memories")
                .arg(QString::fromStdString(out_path))
                .arg(result.conversations).arg(result.messages)
                .arg(result.prompts).arg(result.memories));
            data_status->setStyleSheet("color: green; font-size: 11px;");
        } else {
            data_status->setText("Export failed: " + QString::fromStdString(result.error));
            data_status->setStyleSheet("color: red; font-size: 11px;");
        }
    });

    QObject::connect(import_btn, &QPushButton::clicked, [data_status]() {
        auto& cfg = Config::instance();
        const char* home = getenv("HOME");
        std::string in_path = std::string(home ? home : "/tmp") + "/agora_export.agora";

        // Check if file exists
        if (access(in_path.c_str(), F_OK) != 0) {
            data_status->setText(QString("No file found at %1\nPlace a .agora file there or change path in config.")
                .arg(QString::fromStdString(in_path)));
            data_status->setStyleSheet("color: orange; font-size: 11px;");
            return;
        }

        auto manifest = read_manifest(in_path);
        data_status->setText("Importing...");

        auto result = import_data(in_path, cfg.db_path,
            std::string(home ? home : "/tmp") + "/.config/agora.json",
            cfg.memory_dir);

        if (result.success) {
            data_status->setText(QString("Imported successfully!\n%1 conversations, %2 messages, %3 prompts, %4 memories")
                .arg(result.conversations).arg(result.messages)
                .arg(result.prompts).arg(result.memories));
            data_status->setStyleSheet("color: green; font-size: 11px;");
        } else {
            data_status->setText("Import failed: " + QString::fromStdString(result.error));
            data_status->setStyleSheet("color: red; font-size: 11px;");
        }
    });

    QObject::connect(chatgpt_btn, &QPushButton::clicked, [data_status]() {
        QString path = QFileDialog::getOpenFileName(nullptr, "Import ChatGPT Export", "", "ZIP/JSON (*.zip *.json)");
        if (path.isEmpty()) return;
        auto& cfg = Config::instance();
        const char* home = getenv("HOME");
        auto result = import_data(path.toStdString(), cfg.db_path,
            std::string(home ? home : "/tmp") + "/.config/agora.json", cfg.memory_dir);
        data_status->setText(result.success ? "ChatGPT import OK" : "Import failed");
        data_status->setStyleSheet(result.success ? "color: green; font-size: 11px;" : "color: red; font-size: 11px;");
    });

    QObject::connect(claude_btn, &QPushButton::clicked, [data_status]() {
        QString path = QFileDialog::getOpenFileName(nullptr, "Import Claude Export", "", "ZIP/JSON (*.zip *.json)");
        if (path.isEmpty()) return;
        auto& cfg = Config::instance();
        const char* home = getenv("HOME");
        auto result = import_data(path.toStdString(), cfg.db_path,
            std::string(home ? home : "/tmp") + "/.config/agora.json", cfg.memory_dir);
        data_status->setText(result.success ? "Claude import OK" : "Import failed");
        data_status->setStyleSheet(result.success ? "color: green; font-size: 11px;" : "color: red; font-size: 11px;");
    });

    tabs->addTab(data_tab, "Data");

    // Buttons
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, [&]() {
        save_sp_fields();

        cfg.selected_model = model_edit->text().toStdString();
        cfg.theme.mode = theme_combo->currentData().toString().toStdString();

        // Apply theme immediately
        AgoraTheme::apply(this, theme_combo->currentData().toString());
        cfg.tor.enabled = tor_toggle->isChecked();
        cfg.tor.socks_host = tor_host->text().toStdString();
        cfg.tor.socks_port = tor_port->text().toInt();
        cfg.stt.enabled = stt_toggle->isChecked();
        cfg.stt.endpoint_url = stt_url->text().toStdString();

        // Update API keys
        auto update = [&](const std::string& p, const QString& k) {
            for (auto& ak : cfg.api_keys) {
                if (ak.provider == p) { ak.key = k.toStdString(); return; }
            }
            ApiKeyEntry e; e.id = p; e.name = p; e.provider = p; e.key = k.toStdString();
            cfg.api_keys.push_back(e);
        };
        update("openai", openai_key->text());
        update("anthropic", anthro_key->text());

        if (!base_url->text().isEmpty()) {
            for (auto& pr : cfg.providers) {
                if (pr.name == "openai") { pr.base_url = base_url->text().toStdString(); }
            }
        }

        const char* home = getenv("HOME");
        cfg.save(std::string(home ? home : "/tmp") + "/.config/agora.json");

        // Sync system prompts to DB
        if (db_) {
            for (auto& sp : cfg.system_prompts) {
                db_->upsert_system_prompt(sp);
            }
        }

        dialog->accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    dialog->exec();
    delete dialog;
}
