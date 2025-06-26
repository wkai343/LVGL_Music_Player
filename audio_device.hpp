#ifndef AUDIO_DEVICE_H
#define AUDIO_DEVICE_H

#include <cstdint>
#include <functional>
#include "volume.hpp"

class AudioDevice {
    bool buf_mode{}; // 是否使用双缓冲模式
public:
    Volume volume;
    std::function<void()> sem_acquire = []{}; // 传输前获取信号量
    std::function<void()> sem_reset = []{}; // 双缓冲模式下重置信号量
    std::function<void(int16_t* buffer, uint16_t size)> double_buffer_start = [](int16_t*, uint16_t){}; // 双缓冲模式下开始传输
    std::function<void()> double_buffer_stop = []{}; // 双缓冲模式下停止传输
    std::function<void(int16_t* buffer, uint16_t size)> transmit = [](int16_t*, uint16_t){}; // 音频传输
    std::function<void(uint32_t, uint8_t, uint8_t)> set_format = [](uint32_t, uint8_t, uint8_t){}; // 设置音频格式
    
    AudioDevice(decltype(sem_acquire) sem_acquire = []{},
                decltype(sem_reset) sem_reset = []{},
                decltype(transmit) audio_transmit = [](int16_t*, uint16_t){},
                decltype(set_format) set_format = [](uint32_t, uint8_t, uint8_t){}) 
        : sem_acquire(sem_acquire), sem_reset(sem_reset), transmit(audio_transmit), set_format(set_format) {}
    AudioDevice(decltype(sem_acquire) sem_acquire = []{},
                decltype(sem_reset) sem_reset = []{},
                decltype(double_buffer_start) double_buffer_start = [](int16_t*, uint16_t){},
                decltype(double_buffer_stop) double_buffer_stop = []{},
                decltype(set_format) set_format = [](uint32_t, uint8_t, uint8_t){}) 
        : sem_acquire(sem_acquire), sem_reset(sem_reset), double_buffer_start(double_buffer_start), double_buffer_stop(double_buffer_stop), set_format(set_format), buf_mode{true} {}
    
    void set_volume(uint8_t vol) {
        volume.set(vol);
    }
    uint8_t get_volume() const {
        return volume.get();
    }
    float get_volume_factor() const {
        return volume.get_factor();
    }

    bool is_double_buffer_mode() const {
        return buf_mode;
    }
};

#endif
