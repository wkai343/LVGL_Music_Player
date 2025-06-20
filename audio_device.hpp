#ifndef AUDIO_DEVICE_H
#define AUDIO_DEVICE_H

#include <cstdint>
#include <functional>
#include <mutex>
#include "volume.hpp"

class AudioDevice {
    mutable std::mutex volume_mutex; // 保护音量操作的互斥锁

    uint32_t sample_rate{};
    uint8_t num_channels{};
    uint8_t bit_depth{};
    Volume volume;
    std::function<void()> sem_acquire = []{}; // 传输前获取信号量函数
    std::function<void(int16_t* buffer, uint16_t size)> _transmit = [](int16_t*, uint16_t){};
public:
    AudioDevice(decltype(sem_acquire) sem_acquire = []{}, 
                decltype(_transmit) audio_transmit = [](int16_t*, uint16_t){}) 
        : sem_acquire(sem_acquire), _transmit(audio_transmit) {}
    // 音频传输函数
    std::function<void(int16_t* buffer, uint16_t size)> transmit = [this](int16_t* buffer, uint16_t size){
        {
            std::lock_guard<std::mutex> lock(volume_mutex);
            volume.apply(buffer, size);
        }
        sem_acquire();
        _transmit(buffer, size);
    };
    // 注册等待传输完成函数
    void register_sem_aquire(std::function<void()> sem_acquire) {
        this->sem_acquire = sem_acquire;
    }
    // 注册传输函数
    void register_transmit(std::function<void(int16_t* buffer, uint16_t size)> audio_transmit) {
        this->_transmit = audio_transmit;
    }
    void set_format(uint32_t sample_rate, uint8_t num_channels, uint8_t bit_depth) {
        this->sample_rate = sample_rate;
        this->num_channels = num_channels;
        this->bit_depth = bit_depth;
    }
    void set_volume(uint8_t vol) {
        std::lock_guard<std::mutex> lock(volume_mutex);
        volume.set(vol);
    }
    uint8_t get_volume() const {
        std::lock_guard<std::mutex> lock(volume_mutex);
        return volume.get();
    }
};

#endif
