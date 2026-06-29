#pragma once

#include <QMainWindow>
#include <QListWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QSplitter>
#include <QLabel>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QTabWidget>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <memory>
#include <mutex>
#include <vector>

#include "db/database.hpp"
#include "api/provider.hpp"
#include "audio/recorder.hpp"
#include "models/message.hpp"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    void set_database(std::shared_ptr<Database> db);
    void set_http_client(std::shared_ptr<HttpClient> client);
    void set_headless(bool h) { headless_ = h; }

public slots:
    void take_screenshot();

private slots:
    void refresh_conversations();
    void select_conversation(QListWidgetItem* item);
    void new_conversation();
    void send_message();
    void stop_streaming();
    void toggle_microphone();
    void toggle_pin_message(const QString& msg_id);
    void show_pinned_view();
    void hide_pinned_view();
    void show_settings();
    void show_prompt_selector();
    void show_advanced_settings();
    void regenerate_last();
    void attach_image();
    void on_search_changed(const QString& text);

private:
    // Core
    std::shared_ptr<Database> db_;
    std::shared_ptr<HttpClient> http_;
    std::unique_ptr<Provider> provider_;
    std::unique_ptr<AudioRecorder> recorder_;

    QString active_conv_id_;
    std::vector<Message> messages_;
    std::map<std::string, std::string> selected_branches_;
    std::map<std::string, int> msg_sibling_counts_;
    std::mutex msg_mutex_;

    // Streaming state
    bool streaming_ = false;
    bool mic_active_ = false;
    QString stream_text_;
    QString stream_thoughts_;
    QString stream_model_msg_id_;
    QThread gen_thread_;

    // UI - Sidebar
    QWidget* sidebar_ = nullptr;
    QLineEdit* search_edit_ = nullptr;
    QListWidget* conv_list_ = nullptr;
    QStackedWidget* sidebar_stack_ = nullptr;
    QListWidget* pinned_list_ = nullptr;
    QLabel* pinned_title_ = nullptr;

    // UI - Chat
    QListWidget* msg_list_ = nullptr;

    // UI - Input bar
    QLineEdit* msg_entry_ = nullptr;
    QPushButton* send_btn_ = nullptr;
    QPushButton* mic_btn_ = nullptr;
    QPushButton* stop_btn_ = nullptr;
    QPushButton* attach_btn_ = nullptr;
    QLabel* status_label_ = nullptr;
    QString attached_image_path_;

    // Helpers
    QString generate_id();
    QString format_time(int64_t ms);
    QString status_text(MessageStatus s);
    QWidget* create_message_widget(const Message& msg);
    void render_messages();
    void build_message_path();
    void switch_branch(const QString& parent_id, int direction);
    void update_streaming_message();
    void do_send_message(const QString& text, const QString& preg_parent_id = {});
    void handle_command(const QString& text);
    void scroll_to_bottom();
    void set_input_enabled(bool enabled);

    // Headless mode
    bool headless_ = false;
    QTimer* headless_timer_ = nullptr;

    // Auto backup
    QTimer* auto_backup_timer_ = nullptr;
    void do_auto_backup();

    // Render throttling
    QTimer* render_timer_ = nullptr;
    bool render_pending_ = false;
    void schedule_render();
};
