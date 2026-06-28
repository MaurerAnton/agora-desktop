# agora-desktop — BYOK LLM Client for Linux Desktop & Mobile

A C++/Qt6 native Linux client forked from [newo-ether/Agora](https://github.com/newo-ether/Agora).
Runs on PineTab 2, PinePhone (3GB), and any Linux desktop.

Your device is a **thin client** — your LLM models and speech-to-text run on
a remote server. The app stores everything locally in SQLite and works offline
for viewing past conversations, memories, and settings.

## Features

- **Multi-provider**: OpenAI, Anthropic, Gemini, DeepSeek, Ollama, custom OpenAI-compatible
- **SSE streaming** — real-time token-by-token responses
- **Tree-structured conversations** — edit, branch, regenerate any message
- **Pinned messages** — pin important messages, view in sidebar thread panel
- **System prompts & memories** — stored locally in SQLite, available offline
- **Tor support** — SOCKS5 proxy for all API traffic via libcurl
- **Microphone button** — captures audio, sends to remote speech-to-text endpoint
- **Screenshots** — built-in toolbar button (Qt6 `QScreen::grabWindow`)
- **Headless mode** — `--headless` flag for CLI usage, `-platform offscreen` for automated testing
- **Drop-in provisioning** — place `~/.config/agora-providers.json` and it auto-imports

## Build Requirements

```bash
# Debian/Mobian (PineTab 2 / PinePhone)
sudo apt install build-essential qt6-base-dev libsqlite3-dev \
    libcurl4-openssl-dev libssl-dev libglib2.0-dev
```

## Build & Run

```bash
./scripts/build.sh           # Configure + build
./build/agora                # Run (GUI)
./build/agora --headless     # Run without GUI
./build/agora -platform offscreen  # In-memory GUI (for testing/screenshots)
```

## Drop-in Provisioning

Drop this file at `~/.config/agora-providers.json` and the app auto-imports on first launch:

```json
{
    "selected_model": "OpenAI:gpt-4o",
    "api_keys": [
        {"provider": "openai", "key": "sk-..."},
        {"provider": "anthropic", "key": "sk-ant-..."}
    ],
    "providers": [
        {"name": "ollama", "base_url": "http://192.168.1.100:11434", "enabled": true}
    ],
    "tor": {"enabled": true, "socks_host": "127.0.0.1", "socks_port": 9050},
    "stt": {"enabled": true, "endpoint_url": "http://192.168.1.100:8080/stt"}
}
```

A sentinel file `agora-providers.json.imported` prevents re-import on subsequent runs.
Update the file (newer mtime) to re-import.

## Remote Server Setup (your extra PC)

The PineTab 2 doesn't run models. Set up on your server:

### LLM (pick one):
- **Ollama**: `ollama serve` + expose `http://0.0.0.0:11434`
- **llama.cpp server**: `./llama-server -m model.gguf --host 0.0.0.0`
- **Any OpenAI-compatible API proxy**

### Speech-to-text:
- **whisper.cpp server**: `./whisper-server -m ggml-small.bin --host 0.0.0.0`
- Or custom Python: `pip install faster-whisper && python stt_server.py`

## Architecture

```
PineTab 2 (thin client)              Extra PC (compute server)
┌──────────────────────┐             ┌──────────────────────────┐
│  agora-desktop (C++) │── Tor ────▶│  Ollama / llama.cpp       │
│  • Qt6 Widgets UI    │             │  whisper.cpp (STT)        │
│  • SQLite (offline)  │             │  OpenAI API proxy         │
│  • libcurl + SSE     │◀───────────│                           │
│  • PipeWire mic       │             └──────────────────────────┘
│  • SOCKS5 proxy       │
└──────────────────────┘
```

## License

GPLv3. Original Agora Android code is MIT-licensed (newo-ether/Agora).
All new code in this repository is GPLv3.
