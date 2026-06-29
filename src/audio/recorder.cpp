#include "audio/recorder.hpp"
#include "api/http_client.hpp"
#include <chrono>
#include <iostream>
#include <cstring>
#include <algorithm>
#include "json.hpp"

using json = nlohmann::json;

// ============================================================
//  Audio Backend Detection
// ============================================================

AudioBackend AudioDevice::detect_best() {
    // Try PipeWire first (modern), then ALSA (ubiquitous), then PortAudio
    FILE* f = popen("which pactl 2>/dev/null || which pw-cli 2>/dev/null", "r");
    if (f) {
        char buf[256] = {};
        if (fgets(buf, sizeof(buf), f) && strlen(buf) > 1) {
            pclose(f);
            return AudioBackend::PIPEWIRE;
        }
        pclose(f);
    }
    // ALSA is always available on Linux
    return AudioBackend::ALSA;
}

AudioDevice* AudioDevice::create(AudioBackend backend) {
    switch (backend) {
        case AudioBackend::ALSA:     return new AlsaDevice();
        case AudioBackend::PIPEWIRE: return new PipeWireDevice();
        case AudioBackend::PORTAUDIO: return new PortAudioDevice();
        default: return nullptr;
    }
}

// ============================================================
//  ALSA Backend
// ============================================================

#include <alsa/asoundlib.h>

bool AlsaDevice::open(int sample_rate, int channels) {
    snd_pcm_t* handle = nullptr;
    int err = snd_pcm_open(&handle, "default", SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        std::cerr << "[alsa] open failed: " << snd_strerror(err) << std::endl;
        return false;
    }

    snd_pcm_hw_params_t* params = nullptr;
    snd_pcm_hw_params_malloc(&params);
    snd_pcm_hw_params_any(handle, params);
    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate_near(handle, params, (unsigned int*)&sample_rate, nullptr);
    snd_pcm_hw_params_set_channels(handle, params, channels);

    unsigned int buffer_time = 200000; // 200ms
    snd_pcm_hw_params_set_buffer_time_near(handle, params, &buffer_time, nullptr);

    err = snd_pcm_hw_params(handle, params);
    snd_pcm_hw_params_free(params);

    if (err < 0) {
        std::cerr << "[alsa] hw_params failed: " << snd_strerror(err) << std::endl;
        snd_pcm_close(handle);
        return false;
    }

    pcm_ = handle;
    std::cout << "[alsa] capture opened: " << sample_rate << "Hz " << channels << "ch" << std::endl;
    return true;
}

void AlsaDevice::close() {
    if (pcm_) {
        snd_pcm_close((snd_pcm_t*)pcm_);
        pcm_ = nullptr;
    }
}

int AlsaDevice::read(std::vector<int16_t>& buffer, int max_frames) {
    if (!pcm_) return 0;
    buffer.resize(max_frames);
    int frames = snd_pcm_readi((snd_pcm_t*)pcm_, buffer.data(), max_frames);
    if (frames < 0) {
        // Buffer underrun/overrun — recover
        snd_pcm_recover((snd_pcm_t*)pcm_, frames, 1);
        return 0;
    }
    buffer.resize(frames);
    return frames;
}

// ============================================================
//  PipeWire Backend
// ============================================================

#include <pipewire/pipewire.h>
#include <pipewire/core.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/hook.h>

void PipeWireDevice::pw_loop() {
    pw_main_loop_run((pw_main_loop*)pw_main_loop_);
}

static void pw_process_audio(void* userdata) {
    auto* dev = (PipeWireDevice*)userdata;
    if (!dev || !dev->pw_stream_) return;

    pw_buffer* buf = pw_stream_dequeue_buffer((pw_stream*)dev->pw_stream_);
    if (!buf) return;

    spa_buffer* spa_buf = buf->buffer;
    if (spa_buf->datas[0].chunk->size > 0) {
        int16_t* src = (int16_t*)spa_buf->datas[0].data;
        int frames = spa_buf->datas[0].chunk->size / sizeof(int16_t);
        std::lock_guard lock(dev->pw_mutex_);
        dev->pw_buffer_.insert(dev->pw_buffer_.end(), src, src + frames);
    }

    pw_stream_queue_buffer((pw_stream*)dev->pw_stream_, buf);
}

bool PipeWireDevice::open(int sample_rate, int channels) {
    pw_init(nullptr, nullptr);

    pw_main_loop_ = pw_main_loop_new(nullptr);
    if (!pw_main_loop_) {
        std::cerr << "[pipewire] failed to create main loop" << std::endl;
        return false;
    }

    pw_ctx_ = pw_context_new(pw_main_loop_get_loop((pw_main_loop*)pw_main_loop_), nullptr, 0);
    if (!pw_ctx_) {
        std::cerr << "[pipewire] failed to create context" << std::endl;
        return false;
    }

    pw_core_ = pw_context_connect((pw_context*)pw_ctx_, nullptr, 0);
    if (!pw_core_) {
        std::cerr << "[pipewire] failed to connect core" << std::endl;
        return false;
    }

    pw_stream_ = pw_stream_new((pw_core*)pw_core_, "agora-capture",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                          PW_KEY_MEDIA_CATEGORY, "Capture",
                          PW_KEY_MEDIA_ROLE, "Communication", nullptr));

    if (!pw_stream_) {
        std::cerr << "[pipewire] failed to create stream" << std::endl;
        return false;
    }

    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_audio_info_raw audio_info = {};
    audio_info.format = SPA_AUDIO_FORMAT_S16;
    audio_info.rate = (uint32_t)sample_rate;
    audio_info.channels = (uint32_t)channels;

    const spa_pod* params = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &audio_info);

    pw_stream_events events = {};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.process = pw_process_audio;

    pw_listener_ = new spa_hook;
    pw_stream_add_listener((pw_stream*)pw_stream_, (spa_hook*)pw_listener_, &events, this);

    int err = pw_stream_connect((pw_stream*)pw_stream_,
        PW_DIRECTION_INPUT, PW_ID_ANY,
        (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
        &params, 1);

    if (err < 0) {
        std::cerr << "[pipewire] stream connect failed: " << err << std::endl;
        return false;
    }

    pw_running_ = true;
    pw_thread_ = std::thread(&PipeWireDevice::pw_loop, this);

    std::cout << "[pipewire] capture opened: " << sample_rate << "Hz " << channels << "ch" << std::endl;
    return true;
}

void PipeWireDevice::close() {
    pw_running_ = false;
    if (pw_main_loop_) {
        pw_main_loop_quit((pw_main_loop*)pw_main_loop_);
    }
    if (pw_thread_.joinable()) pw_thread_.join();
    if (pw_stream_ && pw_listener_) {
        spa_hook_remove((spa_hook*)pw_listener_);
        delete (spa_hook*)pw_listener_;
        pw_listener_ = nullptr;
    }
    if (pw_stream_) {
        pw_stream_destroy((pw_stream*)pw_stream_);
        pw_stream_ = nullptr;
    }
    if (pw_core_) {
        pw_core_disconnect((pw_core*)pw_core_);
        pw_core_ = nullptr;
    }
    if (pw_ctx_) {
        pw_context_destroy((pw_context*)pw_ctx_);
        pw_ctx_ = nullptr;
    }
    if (pw_main_loop_) {
        pw_main_loop_destroy((pw_main_loop*)pw_main_loop_);
        pw_main_loop_ = nullptr;
    }
    pw_deinit();
}

int PipeWireDevice::read(std::vector<int16_t>& buffer, int max_frames) {
    std::lock_guard lock(pw_mutex_);
    size_t available = pw_buffer_.size();
    size_t count = std::min(available, (size_t)max_frames);
    if (count == 0) return 0;
    buffer.assign(pw_buffer_.begin(), pw_buffer_.begin() + count);
    pw_buffer_.erase(pw_buffer_.begin(), pw_buffer_.begin() + count);
    return (int)count;
}

// ============================================================
//  PortAudio Backend
// ============================================================

#include <portaudio.h>

static int pa_input_callback(const void* input, void*, unsigned long frames,
                              const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userdata) {
    auto* dev = (PortAudioDevice*)userdata;
    const int16_t* src = (const int16_t*)input;
    std::lock_guard lock(dev->pa_mutex_);
    dev->pa_buffer_.insert(dev->pa_buffer_.end(), src, src + frames);
    return paContinue;
}

bool PortAudioDevice::open(int sample_rate, int channels) {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "[portaudio] init failed: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    PaStream* stream = nullptr;
    err = Pa_OpenDefaultStream(&stream, channels, 0, paInt16,
        sample_rate, 256, pa_input_callback, this);
    if (err != paNoError) {
        std::cerr << "[portaudio] open stream failed: " << Pa_GetErrorText(err) << std::endl;
        Pa_Terminate();
        return false;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "[portaudio] start failed: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(stream);
        Pa_Terminate();
        return false;
    }

    pa_stream_ = stream;
    std::cout << "[portaudio] capture opened: " << sample_rate << "Hz " << channels << "ch" << std::endl;
    return true;
}

void PortAudioDevice::close() {
    if (pa_stream_) {
        Pa_StopStream((PaStream*)pa_stream_);
        Pa_CloseStream((PaStream*)pa_stream_);
        pa_stream_ = nullptr;
    }
    Pa_Terminate();
}

int PortAudioDevice::read(std::vector<int16_t>& buffer, int max_frames) {
    std::lock_guard lock(pa_mutex_);
    size_t available = pa_buffer_.size();
    size_t count = std::min(available, (size_t)max_frames);
    if (count == 0) return 0;
    buffer.assign(pa_buffer_.begin(), pa_buffer_.begin() + count);
    pa_buffer_.erase(pa_buffer_.begin(), pa_buffer_.begin() + count);
    return (int)count;
}

// ============================================================
//  STT Engine factory
// ============================================================

SttBackend parse_stt_scheme(const std::string& endpoint, std::string& model_path) {
    if (endpoint.find("parakeet://") == 0) {
        model_path = endpoint.substr(12); // skip "parakeet://"
        return SttBackend::PARAREET;
    }
    if (endpoint.find("whisper://") == 0) {
        model_path = endpoint.substr(10); // skip "whisper://"
        return SttBackend::WHISPER;
    }
    model_path = endpoint;
    return SttBackend::HTTP_REMOTE;
}

SttEngine* SttEngine::create(SttBackend backend) {
    switch (backend) {
        case SttBackend::PARAREET: return new ParakeetEngine();
        case SttBackend::WHISPER: return new WhisperEngine();
        case SttBackend::HTTP_REMOTE:
        default: return new HttpSttEngine();
    }
}

// ============================================================
//  parakeet.cpp Backend
// ============================================================

#ifdef HAVE_PARAKEET
// parakeet.cpp C API (from parakeet.h)
extern "C" {
    struct parakeet_context;
    typedef struct parakeet_context parakeet_context_t;

    parakeet_context_t* parakeet_init_from_file(const char* model_path);
    void parakeet_free(parakeet_context_t* ctx);
    int parakeet_full(parakeet_context_t* ctx, const float* samples, int n_samples, char* output, int output_capacity);
    int parakeet_streaming(parakeet_context_t* ctx, const float* samples, int n_samples, char* output, int output_capacity, int flush);
}
#endif

bool ParakeetEngine::init(const std::string& model_path, const std::string&) {
#ifdef HAVE_PARAKEET
    parakeet_ctx_ = parakeet_init_from_file(model_path.c_str());
    if (!parakeet_ctx_) {
        std::cerr << "[parakeet] failed to load model: " << model_path << std::endl;
        return false;
    }
    std::cout << "[parakeet] model loaded: " << model_path << std::endl;
    return true;
#else
    (void)model_path;
    std::cerr << "[parakeet] not built (recompile with -DHAVE_PARAKEET)" << std::endl;
    return false;
#endif
}

std::string ParakeetEngine::transcribe(const std::vector<int16_t>& pcm, bool final) {
#ifdef HAVE_PARAKEET
    if (!parakeet_ctx_ || pcm.empty()) return "";
    samples_.reserve(samples_.size() + pcm.size());
    for (auto s : pcm) samples_.push_back(s / 32768.0f);

    if (final) {
        char text[4096] = {};
        parakeet_streaming((parakeet_context_t*)parakeet_ctx_,
            samples_.data(), (int)samples_.size(), text, sizeof(text), 1);
        samples_.clear();
        return std::string(text);
    }
    return "";
#else
    (void)pcm; (void)final;
    return "";
#endif
}

std::string ParakeetEngine::transcribe_partial(const std::vector<int16_t>& pcm) {
#ifdef HAVE_PARAKEET
    if (!parakeet_ctx_ || pcm.empty()) return "";
    samples_.reserve(samples_.size() + pcm.size());
    for (auto s : pcm) samples_.push_back(s / 32768.0f);

    char text[4096] = {};
    parakeet_streaming((parakeet_context_t*)parakeet_ctx_,
        samples_.data(), (int)samples_.size(), text, sizeof(text), 0);
    return std::string(text);
#else
    (void)pcm;
    return "";
#endif
}

// ============================================================
//  whisper.cpp Backend
// ============================================================

#ifdef HAVE_WHISPER
extern "C" {
    struct whisper_context;
    typedef struct whisper_context whisper_context_t;

    whisper_context_t* whisper_init_from_file(const char* path_model);
    void whisper_free(whisper_context_t* ctx);
    int whisper_full(whisper_context_t* ctx, whisper_full_params params, const float* samples, int n_samples);
    int whisper_full_n_segments(whisper_context_t* ctx);
    const char* whisper_full_get_segment_text(whisper_context_t* ctx, int i_segment);
}
#endif

bool WhisperEngine::init(const std::string& model_path, const std::string&) {
#ifdef HAVE_WHISPER
    whisper_ctx_ = whisper_init_from_file(model_path.c_str());
    if (!whisper_ctx_) {
        std::cerr << "[whisper] failed to load model: " << model_path << std::endl;
        return false;
    }
    std::cout << "[whisper] model loaded: " << model_path << std::endl;
    return true;
#else
    (void)model_path;
    std::cerr << "[whisper] not built (recompile with -DHAVE_WHISPER)" << std::endl;
    return false;
#endif
}

std::string WhisperEngine::transcribe(const std::vector<int16_t>& pcm, bool) {
#ifdef HAVE_WHISPER
    if (!whisper_ctx_ || pcm.empty()) return "";
    std::vector<float> samples(pcm.size());
    for (size_t i = 0; i < pcm.size(); i++) samples[i] = pcm[i] / 32768.0f;

    whisper_full((whisper_context_t*)whisper_ctx_, whisper_full_default_params(0),
                 samples.data(), (int)samples.size());

    std::string result;
    int n = whisper_full_n_segments((whisper_context_t*)whisper_ctx_);
    for (int i = 0; i < n; i++) {
        result += whisper_full_get_segment_text((whisper_context_t*)whisper_ctx_, i);
    }
    return result;
#else
    (void)pcm;
    return "";
#endif
}

std::string WhisperEngine::transcribe_partial(const std::vector<int16_t>&) {
    return ""; // whisper.cpp doesn't support partial streaming well
}

// ============================================================
//  HTTP Remote STT Backend
// ============================================================

bool HttpSttEngine::init(const std::string& endpoint_url, const std::string&) {
    endpoint_url_ = endpoint_url;
    std::cout << "[stt] HTTP endpoint: " << endpoint_url_ << std::endl;
    return !endpoint_url_.empty();
}

std::string HttpSttEngine::transcribe(const std::vector<int16_t>& pcm, bool) {
    if (endpoint_url_.empty() || pcm.empty()) return "";

    std::vector<uint8_t> wav;
    uint32_t data_size = pcm.size() * sizeof(int16_t);
    uint32_t file_size = 44 + data_size;

    auto push_str = [&](const char* s) { for (int i = 0; i < 4; i++) wav.push_back(s[i]); };
    auto push_u32 = [&](uint32_t v) {
        wav.push_back(v & 0xFF); wav.push_back((v >> 8) & 0xFF);
        wav.push_back((v >> 16) & 0xFF); wav.push_back((v >> 24) & 0xFF);
    };
    auto push_u16 = [&](uint16_t v) {
        wav.push_back(v & 0xFF); wav.push_back((v >> 8) & 0xFF);
    };

    push_str("RIFF"); push_u32(file_size - 8); push_str("WAVE");
    push_str("fmt "); push_u32(16); push_u16(1); push_u16(1);
    push_u32(16000); push_u32(32000); push_u16(2); push_u16(16);
    push_str("data"); push_u32(data_size);

    for (auto s : pcm) {
        wav.push_back(s & 0xFF);
        wav.push_back((s >> 8) & 0xFF);
    }

    std::string wav_str(reinterpret_cast<char*>(wav.data()), wav.size());

    HttpClient client;
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "audio/wav";
    json body;
    body["raw_audio_b64"] = ""; // servers can still parse WAV body

    std::string response = client.post(endpoint_url_, headers, body);

    try {
        json j = json::parse(response);
        if (j.contains("text")) return j["text"].get<std::string>();
    } catch (...) {}

    return response;
}

std::string HttpSttEngine::transcribe_partial(const std::vector<int16_t>& pcm) {
    return transcribe(pcm, false);
}

// ============================================================
//  AudioRecorder
// ============================================================

AudioRecorder::AudioRecorder() {}
AudioRecorder::~AudioRecorder() { stop(); }

bool AudioRecorder::start(const std::string& stt_endpoint, TextCallback on_text) {
    if (recording_) return false;

    // Parse STT scheme
    std::string model_path;
    stt_backend_ = parse_stt_scheme(stt_endpoint, model_path);
    stt_endpoint_ = stt_endpoint;
    on_text_ = std::move(on_text);
    audio_buffer_.clear();

    // Create STT engine
    stt_engine_.reset(SttEngine::create(stt_backend_));
    if (!stt_engine_->init(model_path, "")) {
        std::cerr << "[recorder] STT engine init failed" << std::endl;
        return false;
    }

    // Create audio device (auto-detect)
    AudioBackend backend = AudioDevice::detect_best();
    audio_dev_.reset(AudioDevice::create(backend));
    if (!audio_dev_ || !audio_dev_->open(16000, 1)) {
        std::cerr << "[recorder] audio device open failed (backend="
                  << (int)backend << ")" << std::endl;
        // Try fallback: PortAudio if PipeWire/ALSA failed
        if (backend != AudioBackend::PORTAUDIO) {
            audio_dev_.reset(new PortAudioDevice());
            if (!audio_dev_->open(16000, 1)) {
                std::cerr << "[recorder] all audio backends failed" << std::endl;
                return false;
            }
        } else {
            return false;
        }
    }

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
    if (audio_dev_) audio_dev_->close();
    audio_dev_.reset();
    stt_engine_.reset();
}

void AudioRecorder::record_loop() {
    auto last_send = std::chrono::steady_clock::now();
    std::vector<int16_t> chunk;

    while (recording_) {
        int frames = audio_dev_->read(chunk, 4096);
        if (frames > 0) {
            audio_buffer_.insert(audio_buffer_.end(), chunk.begin(), chunk.begin() + frames);
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_send).count();

        // Send audio to STT every 500ms for streaming transcription
        if (elapsed >= 500 && !audio_buffer_.empty()) {
            std::string text = stt_engine_->transcribe_partial(audio_buffer_);
            if (!text.empty() && on_text_) {
                on_text_(text, false);
            }
            last_send = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Final transcription on stop
    if (!audio_buffer_.empty() && stt_engine_ && on_text_) {
        std::string final_text = stt_engine_->transcribe(audio_buffer_, true);
        if (!final_text.empty()) {
            on_text_(final_text, true);
        }
    }
}
