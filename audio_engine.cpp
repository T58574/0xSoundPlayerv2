#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "signalsmith-stretch.h"
#include "audio_engine.h"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <complex>
#include <mutex>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct TrackState {
    ma_decoder decoder;
    bool has_decoder = false;
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    bool stretch_initialized = false;
    bool is_playing = false;
    double seek_position = -1.0;
    float volume = 1.0f;
    double tempo_ratio = 1.0;
    double pitch_semi = 0.0;
    double duration = 0.0;
    double bpm = 120.0;
    std::string key = "8A";
    std::vector<float> waveform;
    std::mutex mtx;
};

static ma_device g_device;
static bool g_device_initialized = false;
static TrackState g_tracks[2];
static bool g_automix_enabled = false;
static int g_mixer_state = 0;
static double g_crossfade_start_time = 0.0;
static double g_crossfade_duration = 8.0;
static int g_sample_rate = 44100;
static int g_channels = 2;

static const double major_template[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
static const double minor_template[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

static const char* get_camelot_key(int key_index, bool is_minor) {
    int val = 0;
    if (is_minor) {
        val = ((key_index * 7) + 5) % 12;
    } else {
        val = ((key_index * 7) + 8) % 12;
    }
    if (val == 0) val = 12;
    
    static const char* buf[24] = {
        "1A", "2A", "3A", "4A", "5A", "6A", "7A", "8A", "9A", "10A", "11A", "12A",
        "1B", "2B", "3B", "4B", "5B", "6B", "7B", "8B", "9B", "10B", "11B", "12B"
    };
    int idx = val - 1;
    if (!is_minor) idx += 12;
    return buf[idx];
}

static const char* get_shifted_key(const char* camelot, int semitones) {
    bool is_minor = (camelot[strlen(camelot) - 1] == 'A');
    int idx = 0;
    if (is_minor) {
        for (int i = 0; i < 12; i++) {
            if (strcmp(get_camelot_key(i, true), camelot) == 0) {
                idx = i;
                break;
            }
        }
        return get_camelot_key((idx + semitones + 120) % 12, true);
    } else {
        for (int i = 0; i < 12; i++) {
            if (strcmp(get_camelot_key(i, false), camelot) == 0) {
                idx = i;
                break;
            }
        }
        return get_camelot_key((idx + semitones + 120) % 12, false);
    }
}

static bool keys_compatible(const char* k1, const char* k2) {
    bool m1 = (k1[strlen(k1) - 1] == 'A');
    bool m2 = (k2[strlen(k2) - 1] == 'A');
    int n1 = atoi(k1);
    int n2 = atoi(k2);
    if (n1 == n2) return true;
    if (m1 == m2) {
        int diff = abs(n1 - n2);
        if (diff == 1 || diff == 11) return true;
    }
    return false;
}

static void fft(std::vector<std::complex<double>>& a) {
    int n = a.size();
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = 2 * M_PI / len;
        std::complex<double> wlen(cos(ang), -sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (int j = 0; j < len / 2; j++) {
                std::complex<double> u = a[i + j];
                std::complex<double> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

static void analyze_audio(const char* file_path, double* out_duration, double* out_bpm, const char** out_key, std::vector<float>& out_waveform) {
    ma_decoder dec;
    if (ma_decoder_init_file(file_path, NULL, &dec) != MA_SUCCESS) {
        *out_duration = 0.0;
        *out_bpm = 120.0;
        *out_key = "8A";
        return;
    }
    
    ma_uint64 total_frames = 0;
    ma_decoder_get_length_in_pcm_frames(&dec, &total_frames);
    double duration = (double)total_frames / dec.outputSampleRate;
    *out_duration = duration;
    
    std::vector<float> peaks;
    int chunk_size = (int)(dec.outputSampleRate * 2.0);
    std::vector<float> chunk_buf(chunk_size * dec.outputChannels);
    while (true) {
        ma_uint64 read_frames = 0;
        ma_decoder_read_pcm_frames(&dec, chunk_buf.data(), chunk_size, &read_frames);
        if (read_frames == 0) break;
        float max_val = 0.0f;
        for (ma_uint64 i = 0; i < read_frames * dec.outputChannels; i++) {
            float val = fabsf(chunk_buf[i]);
            if (val > max_val) max_val = val;
        }
        peaks.push_back(max_val);
    }
    out_waveform = peaks;
    
    ma_decoder_seek_to_pcm_frame(&dec, total_frames / 3);
    int analysis_frames = (int)(dec.outputSampleRate * 30.0);
    std::vector<float> analysis_buf(analysis_frames * dec.outputChannels);
    ma_uint64 read_analysis = 0;
    ma_decoder_read_pcm_frames(&dec, analysis_buf.data(), analysis_frames, &read_analysis);
    
    std::vector<float> mono_buf(read_analysis);
    for (ma_uint64 i = 0; i < read_analysis; i++) {
        float sum = 0.0f;
        for (ma_uint32 c = 0; c < dec.outputChannels; c++) {
            sum += analysis_buf[i * dec.outputChannels + c];
        }
        mono_buf[i] = sum / dec.outputChannels;
    }
    
    double envelope_fs = 200.0;
    int downsample_ratio = (int)(dec.outputSampleRate / envelope_fs);
    if (downsample_ratio < 1) downsample_ratio = 1;
    std::vector<float> env;
    for (size_t i = 0; i + downsample_ratio <= mono_buf.size(); i += downsample_ratio) {
        float sum = 0.0f;
        for (int j = 0; j < downsample_ratio; j++) {
            sum += fabsf(mono_buf[i + j]);
        }
        env.push_back(sum / downsample_ratio);
    }
    
    std::vector<float> onset(env.size(), 0.0f);
    for (size_t i = 1; i < env.size(); i++) {
        float diff = env[i] - env[i - 1];
        onset[i] = diff > 0.0f ? diff : 0.0f;
    }
    
    int min_lag = (int)(envelope_fs * 60.0 / 180.0);
    int max_lag = (int)(envelope_fs * 60.0 / 60.0);
    double max_corr = -1.0;
    int best_lag = min_lag;
    for (int lag = min_lag; lag <= max_lag; lag++) {
        double corr = 0.0;
        int count = 0;
        for (size_t i = lag; i < onset.size(); i++) {
            corr += (double)onset[i] * onset[i - lag];
            count++;
        }
        if (count > 0) {
            corr /= count;
            double weight = 1.0 - 0.2 * abs(lag - (int)(envelope_fs * 60.0 / 120.0)) / (envelope_fs * 60.0 / 60.0);
            corr *= weight;
            if (corr > max_corr) {
                max_corr = corr;
                best_lag = lag;
            }
        }
    }
    *out_bpm = 60.0 * envelope_fs / best_lag;
    
    int fft_size = 4096;
    std::vector<std::complex<double>> fft_in(fft_size);
    std::vector<double> chroma(12, 0.0);
    int step_size = fft_size / 2;
    for (size_t offset = 0; offset + fft_size <= mono_buf.size(); offset += step_size) {
        for (int i = 0; i < fft_size; i++) {
            double w = 0.5 * (1.0 - cos(2.0 * M_PI * i / (fft_size - 1)));
            fft_in[i] = std::complex<double>(mono_buf[offset + i] * w, 0.0);
        }
        fft(fft_in);
        for (int k = 1; k < fft_size / 2; k++) {
            double freq = k * (double)dec.outputSampleRate / fft_size;
            if (freq >= 50.0 && freq <= 2000.0) {
                double pitch = 12.0 * log2(freq / 440.0) + 69.0;
                int semitone = (int)round(pitch) % 12;
                if (semitone < 0) semitone += 12;
                chroma[semitone] += abs(fft_in[k]);
            }
        }
    }
    
    double max_r = -2.0;
    int best_key_idx = 0;
    bool best_is_minor = false;
    for (int k = 0; k < 24; k++) {
        int key_idx = k % 12;
        bool is_minor = k >= 12;
        const double* temp = is_minor ? minor_template : major_template;
        
        double sum_x = 0.0, sum_y = 0.0;
        for (int i = 0; i < 12; i++) {
            sum_x += chroma[i];
            sum_y += temp[(i - key_idx + 12) % 12];
        }
        double mean_x = sum_x / 12.0;
        double mean_y = sum_y / 12.0;
        
        double num = 0.0, den_x = 0.0, den_y = 0.0;
        for (int i = 0; i < 12; i++) {
            double diff_x = chroma[i] - mean_x;
            double diff_y = temp[(i - key_idx + 12) % 12] - mean_y;
            num += diff_x * diff_y;
            den_x += diff_x * diff_x;
            den_y += diff_y * diff_y;
        }
        double r = 0.0;
        if (den_x > 0.0 && den_y > 0.0) {
            r = num / sqrt(den_x * den_y);
        }
        if (r > max_r) {
            max_r = r;
            best_key_idx = key_idx;
            best_is_minor = is_minor;
        }
    }
    *out_key = get_camelot_key(best_key_idx, best_is_minor);
    ma_decoder_uninit(&dec);
}

static void get_audio_frames(TrackState& ts, float* out_buffer, int frame_count, int channels, int sample_rate) {
    std::lock_guard<std::mutex> lock(ts.mtx);
    if (!ts.has_decoder || !ts.is_playing) {
        memset(out_buffer, 0, frame_count * channels * sizeof(float));
        return;
    }
    
    if (ts.seek_position >= 0.0) {
        ma_uint64 target_frame = (ma_uint64)(ts.seek_position * sample_rate);
        ma_decoder_seek_to_pcm_frame(&ts.decoder, target_frame);
        ts.seek_position = -1.0;
        ts.stretch.reset();
    }
    
    if (ts.tempo_ratio == 1.0 && ts.pitch_semi == 0.0) {
        ma_uint64 read_frames = 0;
        ma_decoder_read_pcm_frames(&ts.decoder, out_buffer, frame_count, &read_frames);
        if (read_frames < (ma_uint64)frame_count) {
            memset(out_buffer + read_frames * channels, 0, (frame_count - read_frames) * channels * sizeof(float));
            ts.is_playing = false;
        }
        return;
    }
    
    if (!ts.stretch_initialized) {
        ts.stretch.presetDefault(channels, sample_rate);
        ts.stretch_initialized = true;
    }
    
    int M = (int)round(frame_count * ts.tempo_ratio);
    if (M <= 0) M = 1;
    
    std::vector<float> temp_in(M * channels);
    ma_uint64 read_frames = 0;
    ma_decoder_read_pcm_frames(&ts.decoder, temp_in.data(), M, &read_frames);
    if (read_frames < (ma_uint64)M) {
        memset(temp_in.data() + read_frames * channels, 0, (M - read_frames) * channels * sizeof(float));
        if (read_frames == 0) {
            memset(out_buffer, 0, frame_count * channels * sizeof(float));
            ts.is_playing = false;
            return;
        }
    }
    
    std::vector<float> chan_in[2];
    chan_in[0].resize(M);
    chan_in[1].resize(M);
    for (int i = 0; i < M; i++) {
        chan_in[0][i] = temp_in[i * channels];
        chan_in[1][i] = temp_in[i * channels + 1];
    }
    
    std::vector<float> chan_out[2];
    chan_out[0].resize(frame_count);
    chan_out[1].resize(frame_count);
    
    float* input_ptrs[2] = { chan_in[0].data(), chan_in[1].data() };
    float* output_ptrs[2] = { chan_out[0].data(), chan_out[1].data() };
    
    ts.stretch.setTransposeSemitones(ts.pitch_semi);
    ts.stretch.process(input_ptrs, M, output_ptrs, frame_count);
    
    for (int i = 0; i < frame_count; i++) {
        out_buffer[i * channels] = chan_out[0][i] * ts.volume;
        out_buffer[i * channels + 1] = chan_out[1][i] * ts.volume;
    }
}

static void audio_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    float* out = (float*)pOutput;
    int channels = pDevice->playback.channels;
    int sample_rate = pDevice->sampleRate;
    
    std::vector<float> buf0(frameCount * channels);
    std::vector<float> buf1(frameCount * channels);
    
    get_audio_frames(g_tracks[0], buf0.data(), frameCount, channels, sample_rate);
    get_audio_frames(g_tracks[1], buf1.data(), frameCount, channels, sample_rate);
    
    double pos0 = get_track_position(0);
    double dur0 = g_tracks[0].duration;
    double pos1 = get_track_position(1);
    double dur1 = g_tracks[1].duration;
    
    if (g_automix_enabled) {
        if (g_mixer_state == 0 && g_tracks[0].is_playing && dur0 > 0.0 && pos0 >= dur0 - g_crossfade_duration && g_tracks[1].has_decoder) {
            g_mixer_state = 1;
            g_crossfade_start_time = pos0;
            g_tracks[1].is_playing = true;
            g_tracks[1].seek_position = 0.0;
            g_tracks[1].tempo_ratio = g_tracks[0].bpm / g_tracks[1].bpm;
            
            int best_shift = 0;
            double min_shift_abs = 999.0;
            for (int shift = -2; shift <= 2; shift++) {
                const char* new_key = get_shifted_key(g_tracks[1].key.c_str(), shift);
                if (keys_compatible(g_tracks[0].key.c_str(), new_key)) {
                    if (abs(shift) < min_shift_abs) {
                        min_shift_abs = abs(shift);
                        best_shift = shift;
                    }
                }
            }
            g_tracks[1].pitch_semi = best_shift;
        } else if (g_mixer_state == 2 && g_tracks[1].is_playing && dur1 > 0.0 && pos1 >= dur1 - g_crossfade_duration && g_tracks[0].has_decoder) {
            g_mixer_state = 3;
            g_crossfade_start_time = pos1;
            g_tracks[0].is_playing = true;
            g_tracks[0].seek_position = 0.0;
            g_tracks[0].tempo_ratio = g_tracks[1].bpm / g_tracks[0].bpm;
            
            int best_shift = 0;
            double min_shift_abs = 999.0;
            for (int shift = -2; shift <= 2; shift++) {
                const char* new_key = get_shifted_key(g_tracks[0].key.c_str(), shift);
                if (keys_compatible(g_tracks[1].key.c_str(), new_key)) {
                    if (abs(shift) < min_shift_abs) {
                        min_shift_abs = abs(shift);
                        best_shift = shift;
                    }
                }
            }
            g_tracks[0].pitch_semi = best_shift;
        }
    }
    
    if (g_mixer_state == 1) {
        double elapsed = pos0 - g_crossfade_start_time;
        float t = (float)(elapsed / g_crossfade_duration);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        
        float vol0 = 1.0f - t;
        float vol1 = t;
        
        for (ma_uint32 i = 0; i < frameCount * channels; i++) {
            out[i] = buf0[i] * vol0 + buf1[i] * vol1;
        }
        
        if (t >= 1.0f) {
            g_tracks[0].is_playing = false;
            g_mixer_state = 2;
        }
    } else if (g_mixer_state == 3) {
        double elapsed = pos1 - g_crossfade_start_time;
        float t = (float)(elapsed / g_crossfade_duration);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        
        float vol1 = 1.0f - t;
        float vol0 = t;
        
        for (ma_uint32 i = 0; i < frameCount * channels; i++) {
            out[i] = buf0[i] * vol0 + buf1[i] * vol1;
        }
        
        if (t >= 1.0f) {
            g_tracks[1].is_playing = false;
            g_mixer_state = 0;
        }
    } else if (g_mixer_state == 0) {
        memcpy(out, buf0.data(), frameCount * channels * sizeof(float));
    } else if (g_mixer_state == 2) {
        memcpy(out, buf1.data(), frameCount * channels * sizeof(float));
    }
}

int init_audio_engine(int sample_rate, int channels) {
    g_sample_rate = sample_rate;
    g_channels = channels;
    
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = channels;
    config.sampleRate = sample_rate;
    config.dataCallback = audio_callback;
    
    if (ma_device_init(NULL, &config, &g_device) != MA_SUCCESS) {
        return 0;
    }
    
    if (ma_device_start(&g_device) != MA_SUCCESS) {
        ma_device_uninit(&g_device);
        return 0;
    }
    
    g_device_initialized = true;
    return 1;
}

void cleanup_audio_engine() {
    if (g_device_initialized) {
        ma_device_stop(&g_device);
        ma_device_uninit(&g_device);
        g_device_initialized = false;
    }
    for (int i = 0; i < 2; i++) {
        std::lock_guard<std::mutex> lock(g_tracks[i].mtx);
        if (g_tracks[i].has_decoder) {
            ma_decoder_uninit(&g_tracks[i].decoder);
            g_tracks[i].has_decoder = false;
        }
    }
}

int load_track(int slot, const char* file_path) {
    if (slot < 0 || slot > 1) return 0;
    TrackState& ts = g_tracks[slot];
    std::lock_guard<std::mutex> lock(ts.mtx);
    if (ts.has_decoder) {
        ma_decoder_uninit(&ts.decoder);
        ts.has_decoder = false;
    }
    
    ts.stretch_initialized = false;
    ts.is_playing = false;
    ts.seek_position = -1.0;
    ts.tempo_ratio = 1.0;
    ts.pitch_semi = 0.0;
    
    if (ma_decoder_init_file(file_path, NULL, &ts.decoder) != MA_SUCCESS) {
        return 0;
    }
    ts.has_decoder = true;
    
    double dur = 0.0;
    double bpm = 120.0;
    const char* key = "8A";
    std::vector<float> wf;
    analyze_audio(file_path, &dur, &bpm, &key, wf);
    ts.duration = dur;
    ts.bpm = bpm;
    ts.key = key;
    ts.waveform = wf;
    
    return 1;
}

TrackMetadataC get_track_metadata(int slot) {
    TrackMetadataC meta = {0.0, 120.0, "8A", NULL, 0};
    if (slot < 0 || slot > 1) return meta;
    TrackState& ts = g_tracks[slot];
    std::lock_guard<std::mutex> lock(ts.mtx);
    if (!ts.has_decoder) return meta;
    
    meta.durationSec = ts.duration;
    meta.bpm = ts.bpm;
    meta.keySignature = strdup(ts.key.c_str());
    if (!ts.waveform.empty()) {
        meta.waveformSize = ts.waveform.size();
        meta.waveformData = (float*)malloc(ts.waveform.size() * sizeof(float));
        memcpy(meta.waveformData, ts.waveform.data(), ts.waveform.size() * sizeof(float));
    }
    return meta;
}

void play_track(int slot) {
    if (slot < 0 || slot > 1) return;
    std::lock_guard<std::mutex> lock(g_tracks[slot].mtx);
    if (g_tracks[slot].has_decoder) {
        g_tracks[slot].is_playing = true;
        if (slot == 0) g_mixer_state = 0;
        else g_mixer_state = 2;
    }
}

void pause_track(int slot) {
    if (slot < 0 || slot > 1) return;
    std::lock_guard<std::mutex> lock(g_tracks[slot].mtx);
    g_tracks[slot].is_playing = false;
}

void seek_track(int slot, double position_sec) {
    if (slot < 0 || slot > 1) return;
    std::lock_guard<std::mutex> lock(g_tracks[slot].mtx);
    g_tracks[slot].seek_position = position_sec;
}

void set_track_volume(int slot, float volume) {
    if (slot < 0 || slot > 1) return;
    std::lock_guard<std::mutex> lock(g_tracks[slot].mtx);
    g_tracks[slot].volume = volume;
}

void set_track_tempo(int slot, double tempo_ratio) {
    if (slot < 0 || slot > 1) return;
    std::lock_guard<std::mutex> lock(g_tracks[slot].mtx);
    g_tracks[slot].tempo_ratio = tempo_ratio;
}

void set_track_pitch(int slot, double pitch_semi) {
    if (slot < 0 || slot > 1) return;
    std::lock_guard<std::mutex> lock(g_tracks[slot].mtx);
    g_tracks[slot].pitch_semi = pitch_semi;
}

double get_track_position(int slot) {
    if (slot < 0 || slot > 1) return 0.0;
    TrackState& ts = g_tracks[slot];
    std::lock_guard<std::mutex> lock(ts.mtx);
    if (!ts.has_decoder) return 0.0;
    ma_uint64 current_frame = 0;
    ma_decoder_get_cursor_in_pcm_frames(&ts.decoder, &current_frame);
    return (double)current_frame / ts.decoder.outputSampleRate;
}

int is_track_playing(int slot) {
    if (slot < 0 || slot > 1) return 0;
    TrackState& ts = g_tracks[slot];
    std::lock_guard<std::mutex> lock(ts.mtx);
    return ts.is_playing ? 1 : 0;
}

void set_automix_enabled(int enabled) {
    g_automix_enabled = enabled ? true : false;
}

void free_track_metadata(TrackMetadataC metadata) {
    if (metadata.keySignature) {
        free((void*)metadata.keySignature);
    }
    if (metadata.waveformData) {
        free(metadata.waveformData);
    }
}
