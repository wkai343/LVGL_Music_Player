#ifndef AUDIO_DEVICE_H
#define AUDIO_DEVICE_H

#include <cstdint>
#include <functional>
#include "volume.hpp"

class AudioDevice {
    bool cir_mode{}; // 是否使用循环模式
public:
    Volume volume;
    std::function<void()> sem_acquire = [] {}; // 获取信号量
    std::function<void(uint8_t)> sem_reset = [](uint8_t n) {}; // 重置信号量
    std::function<void(int16_t*, uint16_t)> transmit = [](int16_t*, uint16_t) {}; // 音频传输
    std::function<void()> transmit_stop = [] {}; // 停止传输
    std::function<void(uint32_t, uint8_t, uint8_t)> format_set = [](uint32_t, uint8_t, uint8_t) {}; // 设置音频格式
    
    AudioDevice(
        decltype(sem_acquire) sem_acquire = [] {},
        decltype(sem_reset) sem_reset = [](uint8_t n) {},
        decltype(transmit) audio_transmit = [](int16_t*, uint16_t) {},
        decltype(transmit_stop) audio_transmit_stop = [] {},
        decltype(format_set) format_set = [](uint32_t, uint8_t, uint8_t) {},
        bool circular_mode = false
    ) : sem_acquire(sem_acquire), sem_reset(sem_reset), transmit(audio_transmit), transmit_stop(audio_transmit_stop), format_set(format_set), cir_mode(circular_mode) {}
    
    void set_volume(uint8_t vol) {
        volume.set(vol);
    }
    uint8_t get_volume() const {
        return volume.get();
    }
    float get_volume_factor() const {
        return volume.get_factor();
    }

    bool is_circular_mode() const {
        return cir_mode;
    }
};

#endif
