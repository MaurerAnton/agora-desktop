#include "ui/main_window_qt.hpp"
#include "utils/config.hpp"
#include <iostream>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QScrollBar>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QCheckBox>
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
    /* In Qt 6.x, setCheckable makes it a toggle */
    mic_btn_->setCheckable(true);
    input_layout->addWidget(mic_btn_);

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
    connect(back_btn, &QPushButton::clicked, this, &MainWindow::hide_pinned_view);

    connect(new_action, &QAction::triggered, this, &MainWindow::new_conversation);
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

    if (db_) {
        messages_ = db_->get_messages_for_conversation(active_conv_id_.toStdString());
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

    // Role label
    auto* role = new QLabel(is_user ? "You" : "Agora");
    role->setStyleSheet("font-weight: bold; font-size: 11px; color: palette(mid);");
    layout->addWidget(role);

    // Thoughts
    if (!msg.thoughts.empty() && msg.thoughts != "\n") {
        auto* thought = new QLabel(QString::fromStdString(msg.thoughts));
        thought->setWordWrap(true);
        thought->setStyleSheet(
            "background-color: rgba(128,128,128,0.1); border-radius: 6px;"
            "padding: 6px; font-style: italic; font-size: 12px;"
        );
        layout->addWidget(thought);
    }

    // Text
    QString text = QString::fromStdString(msg.text);
    if (msg.streaming && !msg.streaming_text.empty())
        text = QString::fromStdString(msg.streaming_text);

    auto* text_label = new QLabel(text);
    text_label->setWordWrap(true);
    text_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    text_label->setStyleSheet(QString(
        "background-color: %1; border-radius: 10px; padding: 8px; font-size: 14px;"
    ).arg(is_user ? "#d0e8ff" : (msg.participant == Participant::ERROR ? "#ffe0e0" : "#f0f0f0")));
    layout->addWidget(text_label);

    // Status
    QString st = status_text(msg.status);
    if (!st.isEmpty()) {
        auto* st_label = new QLabel(st);
        st_label->setStyleSheet("color: palette(mid); font-size: 10px;");
        layout->addWidget(st_label);
    }

    // Pin button (model messages)
    if (msg.participant == Participant::MODEL && msg.status == MessageStatus::SUCCESS) {
        auto* pin_btn = new QPushButton(msg.pinned ? "★ Unpin" : "☆ Pin");
        pin_btn->setFlat(true);
        pin_btn->setMaximumWidth(80);
        pin_btn->setStyleSheet("font-size: 10px; padding: 2px;");
        QString mid = QString::fromStdString(msg.id);
        connect(pin_btn, &QPushButton::clicked, this, [this, mid]() {
            toggle_pin_message(mid);
        });
        layout->addWidget(pin_btn);
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

    scroll_to_bottom();
}

void MainWindow::scroll_to_bottom() {
    msg_list_->scrollToBottom();
}

void MainWindow::schedule_render() {
    if (render_pending_) return;
    render_pending_ = true;
    QMetaObject::invokeMethod(this, [this]() {
        render_pending_ = false;
        render_messages();
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

void MainWindow::do_send_message(const QString& text) {
    if (!db_) return;

    // Auto-create conversation
    if (active_conv_id_.isEmpty()) {
        new_conversation();
        if (active_conv_id_.isEmpty()) return;
    }

    // Create user message
    QString user_id = "msg_" + generate_id();
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

    provider_ = Provider::create(gc, *http_);

    if (cfg.tor.enabled) {
        http_->set_proxy(cfg.tor.socks_host, cfg.tor.socks_port);
        http_->set_proxy_enabled(true);
    }

    // Create streaming placeholder
    QString model_id = user_id + "_resp";
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

// --- Settings ---

void MainWindow::show_settings() {
    auto* dialog = new QDialog(this);
    dialog->setWindowTitle("Settings");
    dialog->setMinimumSize(400, 450);

    auto* layout = new QVBoxLayout(dialog);
    auto* tabs = new QTabWidget;
    layout->addWidget(tabs);

    auto& cfg = Config::instance();

    // General tab
    auto* general = new QWidget;
    auto* gen_form = new QFormLayout(general);
    auto* model_edit = new QLineEdit(QString::fromStdString(cfg.selected_model));
    model_edit->setPlaceholderText("OpenAI:gpt-4o");
    gen_form->addRow("Default Model:", model_edit);
    tabs->addTab(general, "General");

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

    // Buttons
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, [&]() {
        cfg.selected_model = model_edit->text().toStdString();
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

        dialog->accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    dialog->exec();
    delete dialog;
}
