#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>

// --- Audio capture backends ---

enum class AudioBackend { NONE, ALSA, PIPEWIRE, PORTAUDIO };

// Abstract audio capture device
class AudioDevice {
public:
    virtual ~AudioDevice() = default;
    virtual bool open(int sample_rate, int channels) = 0;
    virtual void close() = 0;
    virtual int read(std::vector<int16_t>& buffer, int max_frames) = 0;
    static AudioDevice* create(AudioBackend backend);
    static AudioBackend detect_best();
};

// ALSA backend
class AlsaDevice : public AudioDevice {
public:
    bool open(int sample_rate, int channels) override;
    void close() override;
    int read(std::vector<int16_t>& buffer, int max_frames) override;
private:
    void* pcm_ = nullptr;
};

// PipeWire backend (requires a main loop running in a separate thread)
class PipeWireDevice : public AudioDevice {
public:
    bool open(int sample_rate, int channels) override;
    void close() override;
    int read(std::vector<int16_t>& buffer, int max_frames) override;

    void* pw_ctx_ = nullptr;
    void* pw_core_ = nullptr;
    void* pw_main_loop_ = nullptr;
    void* pw_stream_ = nullptr;
    void* pw_listener_ = nullptr;
    std::vector<int16_t> pw_buffer_;
    std::mutex pw_mutex_;
    std::atomic<bool> pw_running_{false};
    std::thread pw_thread_;
private:
    void pw_loop();
};

// PortAudio backend
class PortAudioDevice : public AudioDevice {
public:
    bool open(int sample_rate, int channels) override;
    void close() override;
    int read(std::vector<int16_t>& buffer, int max_frames) override;

    void* pa_stream_ = nullptr;
    std::vector<int16_t> pa_buffer_;
    std::mutex pa_mutex_;
};

// --- STT (Speech-to-Text) backends ---

enum class SttBackend { PARAREET, WHISPER, HTTP_REMOTE };

// Abstract STT engine
class SttEngine {
public:
    virtual ~SttEngine() = default;
    virtual bool init(const std::string& model_path, const std::string& config) = 0;
    virtual std::string transcribe(const std::vector<int16_t>& pcm, bool final) = 0;
    virtual std::string transcribe_partial(const std::vector<int16_t>& pcm) = 0;
    static SttEngine* create(SttBackend backend);
};

// parakeet.cpp backend
class ParakeetEngine : public SttEngine {
public:
    bool init(const std::string& model_path, const std::string& config) override;
    std::string transcribe(const std::vector<int16_t>& pcm, bool final) override;
    std::string transcribe_partial(const std::vector<int16_t>& pcm) override;
private:
    void* parakeet_ctx_ = nullptr;
    std::vector<float> samples_;
};

// whisper.cpp backend
class WhisperEngine : public SttEngine {
public:
    bool init(const std::string& model_path, const std::string& config) override;
    std::string transcribe(const std::vector<int16_t>& pcm, bool final) override;
    std::string transcribe_partial(const std::vector<int16_t>& pcm) override;
private:
    void* whisper_ctx_ = nullptr;
};

// HTTP remote STT backend (existing behavior)
class HttpSttEngine : public SttEngine {
public:
    bool init(const std::string& endpoint_url, const std::string& config) override;
    std::string transcribe(const std::vector<int16_t>& pcm, bool final) override;
    std::string transcribe_partial(const std::vector<int16_t>& pcm) override;
private:
    std::string endpoint_url_;
};

// --- Main AudioRecorder ---

class AudioRecorder {
public:
    using TextCallback = std::function<void(const std::string& text, bool final)>;

    AudioRecorder();
    ~AudioRecorder();

    // Start recording with auto-detected audio backend
    // stt_endpoint: URL for HTTP STT, or path to local model file (e.g. "parakeet:///path/to/model.bin")
    bool start(const std::string& stt_endpoint, TextCallback on_text);
    void stop();
    bool is_recording() const { return recording_; }

private:
    void record_loop();

    std::unique_ptr<AudioDevice> audio_dev_;
    std::unique_ptr<SttEngine> stt_engine_;
    SttBackend stt_backend_ = SttBackend::HTTP_REMOTE;
    std::string stt_endpoint_;
    TextCallback on_text_;
    std::thread record_thread_;
    std::atomic<bool> recording_{false};
    std::vector<int16_t> audio_buffer_;
};

// Parse STT URL scheme:
//   "" or no scheme → HTTP remote STT (current behavior)
//   "parakeet:///path/to/model.bin" → parakeet.cpp local
//   "whisper:///path/to/ggml-model.bin" → whisper.cpp local
SttBackend parse_stt_scheme(const std::string& endpoint, std::string& model_path);
