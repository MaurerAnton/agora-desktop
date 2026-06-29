#include "ui/settings.hpp"
#include "utils/config.hpp"
#include <unistd.h>
#include <chrono>

namespace AgoraUI {

SettingsDialog::SettingsDialog(Gtk::Window& parent)
    : Gtk::Dialog("Agora Settings", parent, true) {
    set_default_size(440, 560);

    auto content = get_content_area();
    content->set_margin(12);
    content->set_spacing(8);

    notebook_ = Gtk::make_managed<Gtk::Notebook>();
    content->append(*notebook_);

    // --- General tab ---
    auto general = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    general->set_margin(8);

    auto model_lbl = Gtk::make_managed<Gtk::Label>("Default Model:");
    model_lbl->set_halign(Gtk::Align::START);
    general->append(*model_lbl);

    model_entry_ = Gtk::make_managed<Gtk::Entry>();
    model_entry_->set_placeholder_text("OpenAI:gpt-4o or Anthropic:claude-4-sonnet-20250514");
    general->append(*model_entry_);

    notebook_->append_page(*general, "General", "General");

    // --- System Prompt tab ---
    auto sysprompt = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    sysprompt->set_margin(8);

    auto sp_sel_lbl = Gtk::make_managed<Gtk::Label>("Select Prompt:");
    sp_sel_lbl->set_halign(Gtk::Align::START);
    sysprompt->append(*sp_sel_lbl);

    sp_combo_ = Gtk::make_managed<Gtk::ComboBoxText>();
    sysprompt->append(*sp_combo_);

    auto sp_title_lbl = Gtk::make_managed<Gtk::Label>("Title:");
    sp_title_lbl->set_halign(Gtk::Align::START);
    sysprompt->append(*sp_title_lbl);

    sp_title_ = Gtk::make_managed<Gtk::Entry>();
    sysprompt->append(*sp_title_);

    auto sp_content_lbl = Gtk::make_managed<Gtk::Label>("System Prompt:");
    sp_content_lbl->set_halign(Gtk::Align::START);
    sysprompt->append(*sp_content_lbl);

    sp_content_ = Gtk::make_managed<Gtk::TextView>();
    sp_content_->set_size_request(-1, 100);
    sp_content_->set_wrap_mode(Gtk::WrapMode::WORD);
    sysprompt->append(*sp_content_);

    auto sp_prepend_lbl = Gtk::make_managed<Gtk::Label>("User Message Prepend:");
    sp_prepend_lbl->set_halign(Gtk::Align::START);
    sysprompt->append(*sp_prepend_lbl);

    sp_prepend_ = Gtk::make_managed<Gtk::Entry>();
    sp_prepend_->set_placeholder_text("Text prepended to each user message");
    sysprompt->append(*sp_prepend_);

    auto sp_postpend_lbl = Gtk::make_managed<Gtk::Label>("User Message Postpend:");
    sp_postpend_lbl->set_halign(Gtk::Align::START);
    sysprompt->append(*sp_postpend_lbl);

    sp_postpend_ = Gtk::make_managed<Gtk::Entry>();
    sp_postpend_->set_placeholder_text("Text appended to each user message");
    sysprompt->append(*sp_postpend_);

    // Buttons
    auto sp_btn_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    auto sp_add_btn = Gtk::make_managed<Gtk::Button>("+ New");
    auto sp_delete_btn = Gtk::make_managed<Gtk::Button>("Delete");
    auto sp_active_btn = Gtk::make_managed<Gtk::Button>("Set as Active");
    sp_active_btn->get_style_context()->add_class("suggested-action");
    sp_btn_row->append(*sp_add_btn);
    sp_btn_row->append(*sp_delete_btn);
    sp_btn_row->set_hexpand(true);
    sp_btn_row->set_halign(Gtk::Align::FILL);
    sp_btn_row->append(*sp_active_btn);
    sysprompt->append(*sp_btn_row);

    sp_active_label_ = Gtk::make_managed<Gtk::Label>();
    sp_active_label_->set_halign(Gtk::Align::START);
    sp_active_label_->set_name("active-prompt-label");
    sysprompt->append(*sp_active_label_);

    notebook_->append_page(*sysprompt, "System Prompt", "System Prompt");

    // Signal handlers for system prompt
    sp_combo_->signal_changed().connect([this]() {
        save_current_sp();
        populate_sp_fields();
    });

    sp_add_btn->signal_clicked().connect([this]() {
        save_current_sp();
        auto& cfg = Config::instance();
        SystemPromptEntry sp;
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        sp.id = "prompt_" + std::to_string(now);
        sp.title = "New Prompt";
        sp.content = "You are a helpful AI assistant.\n\n<current_date>{date}</current_date>";
        cfg.system_prompts.push_back(sp);
        sp_combo_->append(sp.id, sp.title);
        sp_combo_->set_active_id(sp.id);
    });

    sp_delete_btn->signal_clicked().connect([this]() {
        auto& cfg = Config::instance();
        if (cfg.system_prompts.size() <= 1) return;
        std::string id = sp_combo_->get_active_id();
        cfg.system_prompts.erase(
            std::remove_if(cfg.system_prompts.begin(), cfg.system_prompts.end(),
                [&](auto& s) { return s.id == id; }),
            cfg.system_prompts.end());
        sp_combo_->remove_all();
        for (auto& sp : cfg.system_prompts)
            sp_combo_->append(sp.id, sp.title);
        if (sp_combo_->get_has_entry()) {
            sp_combo_->set_active(0);
            populate_sp_fields();
        }
    });

    sp_active_btn->signal_clicked().connect([this]() {
        save_current_sp();
        auto& cfg = Config::instance();
        cfg.active_system_prompt_id = sp_combo_->get_active_id();
        populate_sp_fields();
    });

    // --- Providers tab ---
    auto providers = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    providers->set_margin(8);

    auto openai_lbl = Gtk::make_managed<Gtk::Label>("OpenAI API Key:");
    openai_lbl->set_halign(Gtk::Align::START);
    providers->append(*openai_lbl);
    openai_key_ = Gtk::make_managed<Gtk::Entry>();
    openai_key_->set_visibility(false);
    providers->append(*openai_key_);

    auto openai_base_lbl = Gtk::make_managed<Gtk::Label>("OpenAI Base URL:");
    openai_base_lbl->set_halign(Gtk::Align::START);
    providers->append(*openai_base_lbl);
    openai_base_ = Gtk::make_managed<Gtk::Entry>();
    openai_base_->set_placeholder_text("https://api.openai.com/v1");
    providers->append(*openai_base_);

    auto anthro_lbl = Gtk::make_managed<Gtk::Label>("Anthropic API Key:");
    anthro_lbl->set_halign(Gtk::Align::START);
    providers->append(*anthro_lbl);
    anthropic_key_ = Gtk::make_managed<Gtk::Entry>();
    anthropic_key_->set_visibility(false);
    providers->append(*anthropic_key_);

    notebook_->append_page(*providers, "Providers", "Providers");

    // --- Tor tab ---
    auto tor = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    tor->set_margin(8);

    tor_toggle_ = Gtk::make_managed<Gtk::CheckButton>("Route all traffic through Tor (SOCKS5)");
    tor->append(*tor_toggle_);

    auto host_lbl = Gtk::make_managed<Gtk::Label>("SOCKS5 Host:");
    host_lbl->set_halign(Gtk::Align::START);
    tor->append(*host_lbl);
    tor_host_ = Gtk::make_managed<Gtk::Entry>();
    tor_host_->set_text("127.0.0.1");
    tor->append(*tor_host_);

    auto port_lbl = Gtk::make_managed<Gtk::Label>("SOCKS5 Port:");
    port_lbl->set_halign(Gtk::Align::START);
    tor->append(*port_lbl);
    tor_port_ = Gtk::make_managed<Gtk::Entry>();
    tor_port_->set_text("9050");
    tor->append(*tor_port_);

    notebook_->append_page(*tor, "Tor", "Tor");

    // --- STT tab ---
    auto stt = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    stt->set_margin(8);

    stt_toggle_ = Gtk::make_managed<Gtk::CheckButton>("Enable speech-to-text via remote endpoint");
    stt->append(*stt_toggle_);

    auto stt_lbl = Gtk::make_managed<Gtk::Label>("STT Endpoint URL:");
    stt_lbl->set_halign(Gtk::Align::START);
    stt->append(*stt_lbl);
    stt_endpoint_ = Gtk::make_managed<Gtk::Entry>();
    stt_endpoint_->set_placeholder_text("http://192.168.1.100:8080/stt");
    stt->append(*stt_endpoint_);

    notebook_->append_page(*stt, "Speech", "Speech");

    add_button("Cancel", Gtk::ResponseType::CANCEL);
    add_button("Save", Gtk::ResponseType::OK);
}

void SettingsDialog::populate_sp_fields() {
    auto& cfg = Config::instance();
    std::string id = sp_combo_->get_active_id();
    if (id.empty()) {
        sp_title_->set_text("");
        sp_content_->get_buffer()->set_text("");
        sp_prepend_->set_text("");
        sp_postpend_->set_text("");
        sp_active_label_->set_text("");
        return;
    }
    for (auto& sp : cfg.system_prompts) {
        if (sp.id == id) {
            sp_title_->set_text(sp.title);
            sp_content_->get_buffer()->set_text(sp.content);
            sp_prepend_->set_text(sp.user_prepend);
            sp_postpend_->set_text(sp.user_postpend);
            sp_active_label_->set_text(cfg.active_system_prompt_id == sp.id
                ? "✓ Active system prompt"
                : "");
            return;
        }
    }
}

void SettingsDialog::save_current_sp() {
    auto& cfg = Config::instance();
    std::string id = sp_combo_->get_active_id();
    if (id.empty()) return;
    for (auto& sp : cfg.system_prompts) {
        if (sp.id == id) {
            sp.title = sp_title_->get_text();
            sp.content = sp_content_->get_buffer()->get_text();
            sp.user_prepend = sp_prepend_->get_text();
            sp.user_postpend = sp_postpend_->get_text();
            sp_combo_->remove_all();
            for (auto& s : cfg.system_prompts)
                sp_combo_->append(s.id, s.title);
            sp_combo_->set_active_id(id);
            return;
        }
    }
}

void SettingsDialog::load() {
    auto& cfg = Config::instance();
    model_entry_->set_text(cfg.selected_model);
    tor_toggle_->set_active(cfg.tor.enabled);
    tor_host_->set_text(cfg.tor.socks_host);
    tor_port_->set_text(std::to_string(cfg.tor.socks_port));
    stt_toggle_->set_active(cfg.stt.enabled);
    stt_endpoint_->set_text(cfg.stt.endpoint_url);

    for (auto& k : cfg.api_keys) {
        if (k.provider == "openai") openai_key_->set_text(k.key);
        if (k.provider == "anthropic") anthropic_key_->set_text(k.key);
    }

    // Load system prompts
    sp_combo_->remove_all();
    for (auto& sp : cfg.system_prompts) {
        sp_combo_->append(sp.id, sp.title);
    }
    if (sp_combo_->get_has_entry()) {
        sp_combo_->set_active(0);
        populate_sp_fields();
    }
}

void SettingsDialog::save() {
    auto& cfg = Config::instance();
    save_current_sp();

    cfg.selected_model = model_entry_->get_text();
    cfg.tor.enabled = tor_toggle_->get_active();
    cfg.tor.socks_host = tor_host_->get_text();
    cfg.tor.socks_port = std::stoi(tor_port_->get_text());
    cfg.stt.enabled = stt_toggle_->get_active();
    cfg.stt.endpoint_url = stt_endpoint_->get_text();

    cfg.active_system_prompt_id = sp_combo_->get_active_id();

    // Update API keys
    auto update_key = [&](const std::string& provider, const std::string& key) {
        for (auto& k : cfg.api_keys) {
            if (k.provider == provider) { k.key = key; return; }
        }
        ApiKeyEntry entry;
        entry.id = provider;
        entry.name = provider;
        entry.provider = provider;
        entry.key = key;
        cfg.api_keys.push_back(entry);
    };

    update_key("openai", openai_key_->get_text());
    update_key("anthropic", anthropic_key_->get_text());

    const char* home = getenv("HOME");
    std::string config_path = home ? std::string(home) + "/.config/agora.json" : "/tmp/agora.json";
    cfg.save(config_path);
}

} // namespace AgoraUI