#include "audio/recorder.hpp"
#include "api/http_client.hpp"
#include <chrono>
#include <iostream>
#include <cstring>
#include "json.hpp"

using json = nlohmann::json;

AudioRecorder::AudioRecorder() {}
AudioRecorder::~AudioRecorder() { stop(); }

bool AudioRecorder::start(const std::string& stt_endpoint, TextCallback on_text) {
    if (recording_) return false;
    stt_endpoint_ = stt_endpoint;
    on_text_ = std::move(on_text);
    audio_buffer_.clear();

    recording_ = true;
    record_thread_ = std::thread(&AudioRecorder::record_loop, this);
    return true;
}

void AudioRecorder::stop() {
    if (!recording_) return;
    recording_ = false;
    if (record_thread_.joinable()) {
        record_thread_.join();
    }
}

void AudioRecorder::record_loop() {
    if (!init_audio()) {
        recording_ = false;
        return;
    }

    // Simulated audio recording loop
    // In a real implementation, this would use PipeWire/PortAudio callbacks
    // For now we accumulate audio and periodically send to STT endpoint

    auto last_send = std::chrono::steady_clock::now();

    while (recording_) {
        // In real implementation, audio samples would be collected from
        // PipeWire/PortAudio callback into audio_buffer_

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_send).count();

        // Periodically send accumulated audio to STT endpoint
        // (would be every 500ms-1s in real implementation)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // For now, placeholder - actual PipeWire implementation needs
        // a main loop with pw_stream_connect() and process callbacks
    }

    close_audio();
}

bool AudioRecorder::init_audio() {
#ifdef HAVE_PIPEWIRE
    // Initialize PipeWire context
    pw_init(nullptr, nullptr);
    pw_ctx_ = pw_context_new(pw_main_loop_get_loop(nullptr), nullptr, 0);
    if (!pw_ctx_) {
        std::cerr << "Failed to create PipeWire context" << std::endl;
        return false;
    }
    // In production: create stream, connect to audio source, set up process callback
    // pw_stream_ = pw_stream_new(...);
    return true;
#elif defined(HAVE_PORTAUDIO)
    // Initialize PortAudio
    // Pa_Initialize();
    // Pa_OpenDefaultStream(&pa_stream_, 1, 0, paInt16, 16000, 256, callback, this);
    // Pa_StartStream(pa_stream_);
    return true;
#else
    std::cerr << "No audio backend available (need PipeWire or PortAudio)" << std::endl;
    return false;
#endif
}

void AudioRecorder::close_audio() {
#ifdef HAVE_PIPEWIRE
    if (pw_stream_) {
        pw_stream_destroy(pw_stream_);
        pw_stream_ = nullptr;
    }
    if (pw_ctx_) {
        pw_context_destroy(pw_ctx_);
        pw_ctx_ = nullptr;
    }
    pw_deinit();
#endif
#ifdef HAVE_PORTAUDIO
    // Pa_StopStream(pa_stream_);
    // Pa_CloseStream(pa_stream_);
    // Pa_Terminate();
#endif
}

std::string AudioRecorder::send_audio_to_stt(const std::vector<int16_t>& audio) {
    if (audio.empty()) return "";
    if (stt_endpoint_.empty()) return "";

    // Send raw audio as WAV to remote STT service
    // The STT endpoint should handle: whisper.cpp HTTP server, or custom Python STT API

    HttpClient client;
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "audio/wav";

    // Build minimal WAV header
    std::vector<uint8_t> wav;
    uint32_t data_size = audio.size() * 2;
    uint32_t file_size = 44 + data_size;

    // RIFF header
    auto push_str = [&](const char* s) { for (int i = 0; i < 4; i++) wav.push_back(s[i]); };
    auto push_u32 = [&](uint32_t v) {
        wav.push_back(v & 0xFF); wav.push_back((v >> 8) & 0xFF);
        wav.push_back((v >> 16) & 0xFF); wav.push_back((v >> 24) & 0xFF);
    };
    auto push_u16 = [&](uint16_t v) {
        wav.push_back(v & 0xFF); wav.push_back((v >> 8) & 0xFF);
    };

    push_str("RIFF");
    push_u32(file_size - 8);
    push_str("WAVE");
    push_str("fmt ");
    push_u32(16);        // chunk size
    push_u16(1);         // PCM
    push_u16(1);         // mono
    push_u32(16000);     // sample rate
    push_u32(32000);     // byte rate
    push_u16(2);         // block align
    push_u16(16);        // bits per sample
    push_str("data");
    push_u32(data_size);

    for (auto s : audio) {
        wav.push_back(s & 0xFF);
        wav.push_back((s >> 8) & 0xFF);
    }

    // Send via HTTP POST
    std::string wav_str(reinterpret_cast<char*>(wav.data()), wav.size());
    json body;
    body["audio"] = "";  // Placeholder - real impl would use multipart

    std::string response = client.post(stt_endpoint_, headers, body);

    try {
        json j = json::parse(response);
        if (j.contains("text")) {
            return j["text"].get<std::string>();
        }
    } catch (...) {}

    return response;
}
