#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>

#include "db/database.hpp"
#include "api/http_client.hpp"
#include "utils/config.hpp"
#include "ui/main_window_qt.hpp"

static void init_databases(const std::string& config_path) {
    auto& cfg = Config::instance();
    cfg.load(config_path);
}

int main(int argc, char** argv) {
    // Support headless mode via -platform offscreen
    bool headless = false;
    for (int i = 1; i < argc; i++) {
        if (QString(argv[i]) == "--headless") {
            headless = true;
            // Inject -platform offscreen into argv for Qt
            char** new_argv = new char*[argc + 2];
            new_argv[0] = argv[0];
            new_argv[1] = strdup("-platform");
            new_argv[2] = strdup("offscreen");
            for (int j = 1; j < argc; j++)
                new_argv[j + 2] = argv[j];
            argc += 2;
            argv = new_argv;
            break;
        }
    }

    QApplication app(argc, argv);
    app.setApplicationName("agora-desktop");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("agora-desktop");

    std::cout << "agora-desktop v0.1.0 — BYOK LLM Client for PineTab 2 / PinePhone" << std::endl;
    if (headless)
        std::cout << "Running in headless mode (no display needed)" << std::endl;

    // Config
    const char* home = getenv("HOME");
    std::string config_path = home
        ? std::string(home) + "/.config/agora.json"
        : "/tmp/agora.json";

    if (access(config_path.c_str(), F_OK) != 0) {
        std::string dir(config_path);
        dir = dir.substr(0, dir.rfind('/'));
        mkdir(dir.c_str(), 0755);
        QFile f(QString::fromStdString(config_path));
        if (f.open(QIODevice::WriteOnly)) {
            f.write(R"({
    "selected_model": "OpenAI:gpt-4o",
    "max_context_window": 20,
    "thinking_enabled": true,
    "api_keys": [],
    "providers": [
        {"name": "openai", "base_url": "https://api.openai.com/v1", "api_key_id": "", "enabled": true},
        {"name": "anthropic", "base_url": "https://api.anthropic.com/v1", "api_key_id": "", "enabled": true},
        {"name": "ollama", "base_url": "http://localhost:11434", "api_key_id": "", "enabled": true}
    ],
    "system_prompts": [],
    "tor": {"enabled": false, "socks_host": "127.0.0.1", "socks_port": 9050},
    "stt": {"enabled": false, "endpoint_url": "", "sample_rate": 16000}
})");
            f.close();
            std::cout << "Created default config at " << config_path << std::endl;
        }
    }

    init_databases(config_path);

    // Auto-import providers from drop-in file
    {
        std::string providers_path = home
            ? std::string(home) + "/.config/agora-providers.json"
            : "/tmp/agora-providers.json";
        if (Config::instance().merge_provider_file(providers_path)) {
            std::cout << "Provider config auto-loaded." << std::endl;
        }
    }

    auto db = std::make_shared<Database>(Config::instance().db_path);
    auto http = std::make_shared<HttpClient>();

    // Insert default system prompt
    auto prompts = db->get_all_system_prompts();
    if (prompts.empty()) {
        SystemPromptEntry sp;
        sp.id = "default";
        sp.title = "Default Assistant";
        sp.content = R"(You are Agora, a helpful AI assistant.
You have access to conversation history, memory files, and system context.

<current_date>{date}</current_date>
<current_time>{time}</current_time>

Guidelines:
- Be concise and accurate
- Answer in the user's language
- Use markdown formatting when helpful
- If unsure, say so honestly)";
        db->upsert_system_prompt(sp);
        Config::instance().active_system_prompt_id = "default";
    }

    // Tor
    if (Config::instance().tor.enabled) {
        http->set_proxy(Config::instance().tor.socks_host,
                        Config::instance().tor.socks_port);
        http->set_proxy_enabled(true);
    }

    // Window
    MainWindow window;
    window.set_database(db);
    window.set_http_client(http);

    if (headless) {
        window.set_headless(true);
        std::cout << "Headless mode active. Use API via config file." << std::endl;
        std::cout << "Config: " << config_path << std::endl;
        std::cout << "DB:     " << Config::instance().db_path << std::endl;
    } else {
        window.show();
    }

    return app.exec();
}
