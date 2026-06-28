#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <cstdint>

class AudioRecorder {
public:
    using TextCallback = std::function<void(const std::string& text, bool final)>;

    AudioRecorder();
    ~AudioRecorder();

    bool start(const std::string& stt_endpoint, TextCallback on_text);
    void stop();
    bool is_recording() const { return recording_; }

private:
    void record_loop();

    std::string stt_endpoint_;
    TextCallback on_text_;
    std::thread record_thread_;
    std::atomic<bool> recording_{false};
    std::vector<int16_t> audio_buffer_;

    // Platform-specific audio capture
    bool init_audio();
    void close_audio();
    std::string send_audio_to_stt(const std::vector<int16_t>& audio);

#ifdef HAVE_PIPEWIRE
    struct pw_context* pw_ctx_ = nullptr;
    struct pw_stream* pw_stream_ = nullptr;
#endif

#ifdef HAVE_PORTAUDIO
    void* pa_stream_ = nullptr;
#endif
};
