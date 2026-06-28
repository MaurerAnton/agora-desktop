#include "ui/settings.hpp"
#include "utils/config.hpp"
#include <unistd.h>

namespace AgoraUI {

SettingsDialog::SettingsDialog(Gtk::Window& parent)
    : Gtk::Dialog("Agora Settings", parent, true) {
    set_default_size(420, 520);

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
}

void SettingsDialog::save() {
    auto& cfg = Config::instance();
    cfg.selected_model = model_entry_->get_text();
    cfg.tor.enabled = tor_toggle_->get_active();
    cfg.tor.socks_host = tor_host_->get_text();
    cfg.tor.socks_port = std::stoi(tor_port_->get_text());
    cfg.stt.enabled = stt_toggle_->get_active();
    cfg.stt.endpoint_url = stt_endpoint_->get_text();

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
