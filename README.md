# Player

基于LVGL的适用于嵌入式设备的音频播放器，支持WAV格式的音频文件。

Current supported formats:

- WAV 双通道 16bit

## Usage

1. 修改Audio类，重写文件系统相关的函数
2. 创建必要的对象并初始化
3. 在一个线程中循环调用`player.task_handler()`来处理音频播放任务

### rtthread

```cpp
class Audio : public AudioBase {
    enum class Endianness {
        LittleEndian,
        BigEndian
    };
    static inline int32_t four_bytes_to_int(std::string_view source, Endianness endianness = Endianness::LittleEndian) {
        if (endianness == Endianness::LittleEndian)
            return (static_cast<int32_t>(source[0]) |
                    (static_cast<int32_t>(source[1]) << 8) |
                    (static_cast<int32_t>(source[2]) << 16) |
                    (static_cast<int32_t>(source[3]) << 24));
        else
            return (static_cast<int32_t>(source[3]) |
                    (static_cast<int32_t>(source[2]) << 8) |
                    (static_cast<int32_t>(source[1]) << 16) |
                    (static_cast<int32_t>(source[0]) << 24));
    }
    static inline int16_t two_bytes_to_int(std::string_view source, Endianness endianness = Endianness::LittleEndian) {
        if (endianness == Endianness::LittleEndian)
            return (static_cast<int16_t>(source[0]) |
                    (static_cast<int16_t>(source[1]) << 8));
        else
            return (static_cast<int16_t>(source[1]) |
                    (static_cast<int16_t>(source[0]) << 8));
    }

    int16_t get_index_of_chunk(std::string_view id, uint16_t start_index = 0) {
        constexpr uint8_t data_len = 4;
        constexpr uint16_t header_max_len = 32767; // 假设这是最大长度

        if (id.size() != data_len)
            return -1;

        std::string buf;
        buf.resize(data_len);
        int16_t i = start_index;
        while (i < header_max_len - data_len) {
            fseek(file, i, SEEK_SET);
            fread(buf.data(), sizeof *buf.data(), data_len, file);
            if (buf == id)
                return i;

            i += data_len;

            fread(buf.data(), sizeof *buf.data(), data_len, file);
            int32_t chunk_size = four_bytes_to_int(buf);
            if (chunk_size > header_max_len - i - data_len)
                break;
            i += chunk_size + data_len; // 跳过当前块
        }
        return -1;
    }
    FILE* file{};
public:
    Audio() = default;
    Audio(std::string_view name) {
        load(name);
    }
    int8_t load(std::string_view name) override {
        // 先关闭已打开的文件
        if (is_valid()) {
            fclose(file);
            file = nullptr;
        }
        
        if (!(file = fopen(name.data(), "rb")))
            return -1; // 打开文件失败

        std::string buf;
        buf.resize(4);

        fread(buf.data(), sizeof *buf.data(), buf.size(), file);
        if (buf != "RIFF")
            return -1;

        fseek(file, 8, SEEK_SET); // 跳过RIFF大小字段
        fread(buf.data(), sizeof *buf.data(), buf.size(), file);
        if (buf != "WAVE")
            return -1;
        
        const int16_t fmt_chunk_index = get_index_of_chunk("fmt ", 12), data_chunk_index = get_index_of_chunk("data", 12);
        if (fmt_chunk_index == -1 || data_chunk_index == -1)
            return -1;
        // 可以添加更多的检查
        // 读取 data chunk
        buf.resize(16); // fmt chunk size is usually 16 bytes
        fseek(file, data_chunk_index + 4, SEEK_SET);
        fread(buf.data(), sizeof *buf.data(), 4, file);
        data_size = four_bytes_to_int(buf);
        samples_start_index = data_chunk_index + 8;
        // 读取 fmt chunk
        fseek(file, fmt_chunk_index + 8, SEEK_SET);
        fread(buf.data(), sizeof *buf.data(), buf.size(), file);
        // uint16_t audio_format = two_bytes_to_int(buf.substr(0, 2));
        num_channels = two_bytes_to_int(buf.substr(2, 2));
        sample_rate = four_bytes_to_int(buf.substr(4, 4));
        bit_depth = two_bytes_to_int(buf.substr(14, 2));
        byte_rate = bit_depth / 8 * sample_rate * num_channels;

        this->name = name;

        return 0;
    }
    bool is_valid() const override {
        return file != nullptr;
    }
    uint16_t current_time() const override {
        return (ftell(file) - samples_start_index) / byte_rate;
    }
    void seek_to(uint16_t time) override {
        fseek(file, samples_start_index + time * byte_rate, SEEK_SET);
    }
    unsigned read(uint8_t buffer[], unsigned size) override {
        return fread(buffer, sizeof *buffer, size, file);
    }
    ~Audio() {
        if (is_valid()) {
            fclose(file);
            file = nullptr;
        }
    }

    static std::vector<std::string> scan_directory(std::string_view path) {
        std::vector<std::string> song_list;
        
        DIR* dir = opendir(path.data());
        if (!dir) {
            return song_list; // 返回空列表如果无法打开目录
        }
        
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            // 跳过目录条目
            if (entry->d_type == DT_DIR) {
                continue;
            }
            
            std::string file_name = entry->d_name;
            // 检查是否以.wav结尾
            if (file_name.length() >= 4 && 
                file_name.substr(file_name.length() - 4) == ".wav") {
                std::string full_path = std::string(path);
                if (!full_path.empty() && full_path.back() != '/') {
                    full_path += '/';
                }
                full_path += file_name;
                song_list.push_back(full_path);
            }
        }
        
        closedir(dir);
        return song_list;
    }
};
```

```cpp
rt_sem_t audio_sem;
Player player;

extern "C"
void lv_user_gui_init(void) {
    dma_init();
    i2s2_init();
    rng_init();
    
    audio_sem = rt_sem_create("audio_sem", 1, RT_IPC_FLAG_PRIO);
    auto device_set_format = [](uint32_t sample_rate, uint8_t num_channels, uint8_t bit_depth) {
        hi2s2.Init.AudioFreq = sample_rate;
        if (bit_depth == 16)
            hi2s2.Init.DataFormat = I2S_DATAFORMAT_16B;
        else if (bit_depth == 24)
            hi2s2.Init.DataFormat = I2S_DATAFORMAT_24B;
        else
            hi2s2.Init.DataFormat = I2S_DATAFORMAT_32B;
        if (HAL_I2S_Init(&hi2s2) != HAL_OK)
            Error_Handler();
    };
    auto device = std::make_shared<AudioDevice>(
        [] { rt_sem_take(audio_sem, rtthread::WAIT_FOREVER); }, 
        [](uint8_t n) {
            rt_sem_delete(audio_sem);
            audio_sem = rt_sem_create("audio_sem", n, RT_IPC_FLAG_PRIO);
        }, 
        [](int16_t* buffer, uint16_t size) { HAL_I2S_Transmit_DMA(&hi2s2, reinterpret_cast<uint16_t*>(buffer), size); }, 
        [] { HAL_I2S_DMAStop(&hi2s2); },
        device_set_format
        , true // 使用循环模式
    );
    // i2s注册传输完成回调
    HAL_I2S_RegisterCallback(&hi2s2, HAL_I2S_TX_COMPLETE_CB_ID, [](I2S_HandleTypeDef* hi2s) {
        if (hi2s->Instance == SPI2)
            rt_sem_release(audio_sem);
    });
    if (device->is_circular_mode()) {
        HAL_I2S_RegisterCallback(&hi2s2, HAL_I2S_TX_HALF_COMPLETE_CB_ID, [](I2S_HandleTypeDef* hi2s) {
            if (hi2s->Instance == SPI2)
                rt_sem_release(audio_sem);
        });
    }
    player.init(device, {lv_lock, lv_unlock}, 
        [](Player::Playlist& pl) {
            std::default_random_engine gen(rng_device());
            std::ranges::shuffle(pl, gen);
        }
    );
    rt_thread_t player_thread = rt_thread_create("player", [](void*) {
        std::this_thread::sleep_for(1s);
        player.search_songs("/sdcard");
        while (true)
            player.task_handler();
    }, RT_NULL, 4096, 1, 20);
    if (player_thread != RT_NULL)
        rt_thread_startup(player_thread);
    else
        rt_kprintf("Failed to create player thread\n");
}
```
