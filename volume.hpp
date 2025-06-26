#ifndef VOLUME_H
#define VOLUME_H

#include <cstdint>
#include <ranges>
#include <span>
#include <cmath>

class Volume {
private:
    uint8_t volume{}; // 音量范围 0-100
    float volume_factor{}; // 缓存音量因子
    void updateFactor() {
        if (volume == 0) {
            volume_factor = 0;
            return;
        }
        constexpr float max_db = 60.0f;
        const float db_attenuation = (volume / 100.0f * max_db) - max_db;
        volume_factor = std::pow(10.0f, db_attenuation / 20.0f);
    }
public:
    Volume(uint8_t vol = 50) { set(vol); }
    void set(uint8_t vol) {
        if (vol > 100) vol = 100;
        if (volume == vol) return; // 避免重复计算
        volume = vol;
        updateFactor();
    }
    uint8_t get() const { return volume; }

    template<typename T>
    void apply(std::span<T> buf) const {
        if (volume == 0) {
            std::ranges::fill(buf, 0); // 音量为0时，直接将缓冲区清零
            return;
        }
        // 使用预计算的音量因子
        buf = buf | std::views::transform([this](T sample) {
            return static_cast<T>(sample * volume_factor);
        });
    }

    template<typename T>
    void apply(T* arr, size_t sz) const {
        if (volume == 0) {
            std::fill(arr, arr + sz, 0); // 音量为0时，直接将缓冲区清零
            return;
        }
        // 使用预计算的音量因子
        for (size_t i = 0; i < sz; ++i)
            arr[i] = static_cast<T>(arr[i] * volume_factor);
    }
    float get_factor() const {
        return volume_factor;
    }
};

#endif // VOLUME_H
