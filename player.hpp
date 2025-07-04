#ifndef PLAYER_H
#define PLAYER_H

#include <mutex>
#include <condition_variable>
#include <random>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <lvgl.h>
#include "lock.hpp"
#include "audio.hpp"
#include "audio_device.hpp"

LV_FONT_DECLARE(zh)

class Player {
public:
    using Playlist = std::vector<std::string>;
    
    // 播放模式枚举
    enum class PlayMode {
        SEQUENTIAL,     // 顺序播放（列表循环）
        SINGLE_LOOP,    // 单曲循环
        RANDOM          // 随机播放
    };

private:
    std::function<void(Playlist&)> list_shuffle = [](Playlist& pl) {
        std::ranges::shuffle(pl, std::default_random_engine(0));
    };

    struct UI {
        Player* player;
        lv_obj_t* songName_label;
        lv_obj_t* curTime_label;
        lv_obj_t* totalTime_label;
        lv_obj_t* dragTime_label;
        lv_obj_t* progress_bar;
        lv_obj_t* play_btn;
        lv_obj_t* prev_btn;
        lv_obj_t* next_btn;
        lv_obj_t* mode_btn;        // 播放模式按钮
        lv_obj_t* vol_slider;
        lv_obj_t* vol_btn;
        lv_obj_t* playlist_list;
        lv_obj_t* playlist_btn;
        bool is_dragging_progress = false;
        UI(Player* p) : player(p) {}
        void event_init() {
            // 播放/暂停
            lv_obj_add_event_cb(play_btn, [](lv_event_t* e) {
                static_cast<Player*>(lv_event_get_user_data(e))->toggle_play_pause();
            }, LV_EVENT_CLICKED, this->player);
            // 上一曲
            lv_obj_add_event_cb(prev_btn, [](lv_event_t* e) {
                static_cast<Player*>(lv_event_get_user_data(e))->prev_song();
            }, LV_EVENT_CLICKED, this->player);
            // 下一曲
            lv_obj_add_event_cb(next_btn, [](lv_event_t* e) {
                static_cast<Player*>(lv_event_get_user_data(e))->next_song();
            }, LV_EVENT_CLICKED, this->player);
            // 进度条
            lv_obj_add_event_cb(progress_bar, [](lv_event_t* e) {
                auto ui = static_cast<UI*>(lv_event_get_user_data(e));
                auto event_code = lv_event_get_code(e);
                auto value = lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(e)));
                auto progress_bar = static_cast<lv_obj_t*>(lv_event_get_target(e));
                
                if (event_code == LV_EVENT_PRESSED) {
                    // 开始拖动时扩展进度条宽度
                    ui->is_dragging_progress = true;
                    lv_obj_set_width(progress_bar, LV_PCT(95));
                    lv_obj_remove_flag(ui->dragTime_label, LV_OBJ_FLAG_HIDDEN);
                    lv_label_set_text_fmt(ui->dragTime_label, "%02d:%02d", value / 60, value % 60);
                } else if (event_code == LV_EVENT_VALUE_CHANGED) {
                    // 拖动过程中只更新UI显示，不改变播放位置
                    lv_label_set_text_fmt(ui->dragTime_label, "%02d:%02d", value / 60, value % 60);
                } else if (event_code == LV_EVENT_RELEASED) {
                    // 松开时恢复原始宽度并应用新的播放位置
                    ui->is_dragging_progress = false;
                    lv_obj_set_width(progress_bar, LV_PCT(90));  // 恢复到90%
                    lv_obj_add_flag(ui->dragTime_label, LV_OBJ_FLAG_HIDDEN);
                    ui->progress_update(value, false, true); // 更新当前时间显示
                    
                    // 跳转音频位置
                    ui->player->seek(value);
                }
            }, LV_EVENT_ALL, this);
            // 音量
            lv_obj_add_event_cb(vol_slider, [](lv_event_t* e) {
                auto ui = static_cast<UI*>(lv_event_get_user_data(e));
                auto event_code = lv_event_get_code(e);
                auto vol_value = lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(e)));
                
                if (event_code == LV_EVENT_RELEASED) {
                    ui->player->device->set_volume(vol_value);
                }
            }, LV_EVENT_ALL, this);
            // 歌单按钮事件：弹出/隐藏歌单
            lv_obj_add_event_cb(playlist_btn, [](lv_event_t* e) {
                auto ui = static_cast<UI*>(lv_event_get_user_data(e));
                if (lv_obj_has_flag(ui->playlist_list, LV_OBJ_FLAG_HIDDEN)) {
                    lv_obj_remove_flag(ui->playlist_list, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_move_foreground(ui->playlist_list);
                } else {
                    lv_obj_add_flag(ui->playlist_list, LV_OBJ_FLAG_HIDDEN);
                }
            }, LV_EVENT_CLICKED, this);
            // 音量按钮事件：弹出/隐藏音量条
            lv_obj_add_event_cb(vol_btn, [](lv_event_t* e) {
                auto ui = static_cast<UI*>(lv_event_get_user_data(e));
                if (lv_obj_has_flag(ui->vol_slider, LV_OBJ_FLAG_HIDDEN)) {
                    lv_obj_remove_flag(ui->vol_slider, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_move_foreground(ui->vol_slider);
                } else {
                    lv_obj_add_flag(ui->vol_slider, LV_OBJ_FLAG_HIDDEN);
                }
            }, LV_EVENT_CLICKED, this);
            // 播放模式按钮事件
            lv_obj_add_event_cb(mode_btn, [](lv_event_t* e) {
                auto ui = static_cast<UI*>(lv_event_get_user_data(e));
                ui->player->switch_play_mode();
            }, LV_EVENT_CLICKED, this);
        }
        void init() {
            // 创建主容器，填充整个屏幕
            auto main_cont = lv_obj_create(lv_screen_active());
            lv_obj_set_size(main_cont, LV_HOR_RES, LV_VER_RES);
            lv_obj_set_style_pad_all(main_cont, 0, 0);
            lv_obj_remove_flag(main_cont, LV_OBJ_FLAG_SCROLLABLE);

            // 顶部区域 - 歌曲名称
            auto top_area = lv_obj_create(main_cont);
            lv_obj_set_size(top_area, LV_PCT(100), LV_VER_RES / 10);
            lv_obj_set_style_border_width(top_area, 0, 0);
            lv_obj_set_style_bg_opa(top_area, LV_OPA_0, 0);
            lv_obj_set_style_pad_ver(top_area, 10, 0);
            lv_obj_align(top_area, LV_ALIGN_TOP_MID, 0, 0);

            // 歌曲名标签
            songName_label = lv_label_create(top_area);
            lv_label_set_text(songName_label, "无播放歌曲");
            lv_obj_set_style_text_font(songName_label, &zh, 0);
            lv_obj_set_width(songName_label, LV_PCT(90));
            lv_obj_set_style_text_align(songName_label, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(songName_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_align(songName_label, LV_ALIGN_CENTER, 0, 0);

            // 中间占位区域 - 为未来扩展准备
            auto middle_area = lv_obj_create(main_cont);
            // 将高宽比调整为接近1:1的方形，使用固定尺寸
            const uint16_t square_size = LV_HOR_RES * 0.65; // 宽度的65%
            lv_obj_set_size(middle_area, square_size, square_size);
            lv_obj_set_style_radius(middle_area, 10, 0);
            lv_obj_set_style_border_color(middle_area, lv_color_hex(0xDDDDDD), 0);
            lv_obj_set_style_border_width(middle_area, 2, 0);
            lv_obj_set_style_bg_opa(middle_area, LV_OPA_20, 0);
            lv_obj_align(middle_area, LV_ALIGN_CENTER, 0, -LV_VER_RES / 5);

            // 底部区域
            auto bottom_area = lv_obj_create(main_cont);
            lv_obj_set_size(bottom_area, LV_PCT(90), LV_VER_RES / 3);
            lv_obj_set_style_border_width(bottom_area, 0, 0);
            lv_obj_set_style_bg_opa(bottom_area, LV_OPA_0, 0);
            lv_obj_set_style_pad_all(bottom_area, 0, 0);
            lv_obj_align(bottom_area, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_set_flex_flow(bottom_area, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(bottom_area, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            
            // 进度区域
            auto progress_area = lv_obj_create(bottom_area);
            lv_obj_set_size(progress_area, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_border_width(progress_area, 0, 0);
            lv_obj_set_style_bg_opa(progress_area, LV_OPA_0, 0);
            lv_obj_set_style_pad_all(progress_area, 0, 0);
            lv_obj_set_flex_flow(progress_area, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(progress_area, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            
            // 进度条行（独立占一行）
            auto progress_row = lv_obj_create(progress_area);
            lv_obj_set_size(progress_row, LV_PCT(100), 20);
            lv_obj_set_style_border_width(progress_row, 0, 0);
            lv_obj_set_style_bg_opa(progress_row, LV_OPA_0, 0);
            lv_obj_set_style_pad_all(progress_row, 0, 0);
            
            // 进度条
            progress_bar = lv_slider_create(progress_row);
            lv_obj_set_width(progress_bar, LV_PCT(90));
            lv_obj_set_height(progress_bar, 5);  // 变细的进度条
            lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_align(progress_bar, LV_ALIGN_CENTER, 0, 0);
            // 增加点击区域，上下各增加10像素的可点击区域
            lv_obj_set_ext_click_area(progress_bar, 10);
            // 隐藏滑块圆点
            lv_obj_set_style_radius(progress_bar, 0, LV_PART_KNOB);
            lv_obj_set_style_bg_opa(progress_bar, LV_OPA_0, LV_PART_KNOB);
            lv_obj_set_style_border_width(progress_bar, 0, LV_PART_KNOB);
            
            // 时间标签行
            auto time_row = lv_obj_create(progress_area);
            lv_obj_set_size(time_row, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_border_width(time_row, 0, 0);
            lv_obj_set_style_bg_opa(time_row, LV_OPA_0, 0);
            lv_obj_set_style_pad_ver(time_row, 0, 0);
            lv_obj_remove_flag(time_row, LV_OBJ_FLAG_SCROLLABLE);

            // 当前时间标签 - 左对齐
            curTime_label = lv_label_create(time_row);
            lv_label_set_text(curTime_label, "00:00");
            lv_obj_align(curTime_label, LV_ALIGN_LEFT_MID, 10, 0);

            // 拖动时间标签（紧贴curTime_label右边显示）
            dragTime_label = lv_label_create(time_row);
            lv_label_set_text(dragTime_label, "00:00");
            lv_obj_set_style_bg_color(dragTime_label, lv_color_hex(0xF0F0F0), 0);   // 灰色背景
            lv_obj_set_style_bg_opa(dragTime_label, LV_OPA_COVER, 0);               // 完全不透明背景
            lv_obj_set_style_radius(dragTime_label, 4, 0);
            lv_obj_align_to(dragTime_label, curTime_label, LV_ALIGN_OUT_RIGHT_MID, 5, 0);  // 紧贴curTime_label右边
            lv_obj_add_flag(dragTime_label, LV_OBJ_FLAG_HIDDEN);  // 初始隐藏
            
            // 总时间标签 - 右对齐，位置固定
            totalTime_label = lv_label_create(time_row);
            lv_label_set_text(totalTime_label, "00:00");
            lv_obj_align(totalTime_label, LV_ALIGN_RIGHT_MID, -10, 0);

            // 控制按钮区域
            auto control_area = lv_obj_create(bottom_area);
            lv_obj_set_size(control_area, LV_PCT(100), LV_VER_RES / 4);
            lv_obj_set_style_border_width(control_area, 0, 0);
            lv_obj_set_style_bg_opa(control_area, LV_OPA_0, 0);
            lv_obj_set_style_pad_all(control_area, 0, 0);
            lv_obj_set_flex_flow(control_area, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(control_area, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_remove_flag(control_area, LV_OBJ_FLAG_SCROLLABLE);
            
            // 主控制按钮行 - 播放控制
            auto main_control_row = lv_obj_create(control_area);
            lv_obj_set_size(main_control_row, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_border_width(main_control_row, 0, 0);
            lv_obj_set_style_bg_opa(main_control_row, LV_OPA_0, 0);
            lv_obj_set_style_pad_ver(main_control_row, 10, 0);
            lv_obj_set_flex_flow(main_control_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(main_control_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            // 上一曲按钮
            prev_btn = lv_btn_create(main_control_row);
            lv_obj_set_size(prev_btn, 45, 45);
            lv_obj_set_style_radius(prev_btn, LV_RADIUS_CIRCLE, 0);
            auto prev_label = lv_label_create(prev_btn);
            lv_label_set_text(prev_label, LV_SYMBOL_PREV);
            lv_obj_center(prev_label);

            // 播放/暂停按钮
            play_btn = lv_btn_create(main_control_row);
            lv_obj_set_size(play_btn, 50, 50);  // 播放按钮稍大
            lv_obj_set_style_radius(play_btn, LV_RADIUS_CIRCLE, 0);
            auto play_label = lv_label_create(play_btn);
            lv_label_set_text(play_label, LV_SYMBOL_PLAY);
            lv_obj_center(play_label);

            // 下一曲按钮
            next_btn = lv_btn_create(main_control_row);
            lv_obj_set_size(next_btn, 45, 45);
            lv_obj_set_style_radius(next_btn, LV_RADIUS_CIRCLE, 0);
            auto next_label = lv_label_create(next_btn);
            lv_label_set_text(next_label, LV_SYMBOL_NEXT);
            lv_obj_center(next_label);

            // 辅助控制行 - 音量和播放列表按钮
            auto aux_control_row = lv_obj_create(control_area);
            lv_obj_set_size(aux_control_row, LV_PCT(80), LV_SIZE_CONTENT);
            lv_obj_set_style_border_width(aux_control_row, 0, 0);
            lv_obj_set_style_bg_opa(aux_control_row, LV_OPA_0, 0);
            lv_obj_set_style_pad_bottom(aux_control_row, 8, 0);
            lv_obj_set_flex_flow(aux_control_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(aux_control_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_align(aux_control_row, LV_ALIGN_BOTTOM_MID, 0, 0);

            // 音量按钮 - 较小
            vol_btn = lv_btn_create(aux_control_row);
            lv_obj_set_size(vol_btn, 36, 36); // 减小大小
            lv_obj_set_style_radius(vol_btn, LV_RADIUS_CIRCLE, 0);
            auto vol_label = lv_label_create(vol_btn);
            lv_label_set_text(vol_label, LV_SYMBOL_VOLUME_MAX);
            lv_obj_center(vol_label);

            // 播放模式按钮 - 在音量按钮左侧
            mode_btn = lv_btn_create(aux_control_row);
            lv_obj_set_size(mode_btn, 36, 36);
            lv_obj_set_style_radius(mode_btn, LV_RADIUS_CIRCLE, 0);
            auto mode_label = lv_label_create(mode_btn);
            lv_label_set_text(mode_label, LV_SYMBOL_LOOP); // 默认循环模式图标
            lv_obj_center(mode_label);
            // 将模式按钮移动到音量按钮前面
            lv_obj_move_to_index(mode_btn, lv_obj_get_index(vol_btn));

            // 歌单按钮 - 较小
            playlist_btn = lv_btn_create(aux_control_row);
            lv_obj_set_size(playlist_btn, 36, 36); // 减小大小
            lv_obj_set_style_radius(playlist_btn, LV_RADIUS_CIRCLE, 0);
            auto list_label = lv_label_create(playlist_btn);
            lv_label_set_text(list_label, LV_SYMBOL_LIST);
            lv_obj_center(list_label);

            // 歌曲列表弹窗（初始隐藏），用lv_list替换
            playlist_list = lv_list_create(lv_screen_active());
            lv_obj_set_size(playlist_list, LV_PCT(70), LV_PCT(70));
            lv_obj_set_style_bg_color(playlist_list, lv_color_hex(0xffffff), 0);
            lv_obj_set_style_border_width(playlist_list, 2, 0);
            lv_obj_set_style_radius(playlist_list, 10, 0);
            lv_obj_set_style_pad_all(playlist_list, 0, 0);
            lv_obj_set_style_bg_opa(playlist_list, LV_OPA_COVER, 0);
            lv_obj_center(playlist_list);
            lv_obj_add_flag(playlist_list, LV_OBJ_FLAG_HIDDEN);

            // 音量弹窗（初始隐藏）
            vol_slider = lv_slider_create(lv_screen_active());
            lv_obj_set_size(vol_slider, LV_PCT(50), 40);
            lv_obj_set_style_bg_color(vol_slider, lv_color_hex(0xf0f0f0), 0);
            lv_obj_set_style_border_width(vol_slider, 2, 0);
            lv_obj_set_style_radius(vol_slider, 10, 0);
            lv_obj_set_style_pad_all(vol_slider, 8, 0);
            lv_slider_set_range(vol_slider, 0, 100);
            lv_obj_center(vol_slider);
            lv_obj_add_flag(vol_slider, LV_OBJ_FLAG_HIDDEN);
        }
        void playlist_clear() {
            lv_obj_clean(playlist_list);
        }
        void playlist_update(size_t index) {
            for (size_t i = 0; i < lv_obj_get_child_count(playlist_list); ++i) {
                auto child = lv_obj_get_child(playlist_list, i);
                if (i == index)
                    lv_obj_set_style_bg_color(child, lv_color_hex(0x007BFF), LV_PART_MAIN);
                else
                    lv_obj_set_style_bg_color(child, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
            }
        }
        void playlist_load(const Playlist& new_playlist) {
            if (player->playlist.empty())
                return;

            // 清空列表
            playlist_clear();

            auto event_handler = [](lv_event_t* e) {
                auto ui = static_cast<UI*>(lv_event_get_user_data(e));
                auto btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
                
                if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
                    auto parent = lv_obj_get_parent(btn);
                    for(uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
                        auto child = lv_obj_get_child(parent, i);
                        if(child == btn) {
                            lv_obj_set_style_bg_color(child, lv_color_hex(0x007BFF), LV_PART_MAIN);
                            ui->player->load(i);
                        }
                        else
                            lv_obj_set_style_bg_color(child, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
                    }
                }
            };

            // 重新添加所有歌曲
            for (size_t i = 0; i < new_playlist.size(); ++i) {
                auto btn = lv_list_add_button(playlist_list, nullptr, new_playlist[i].c_str());
                auto label = lv_obj_get_child(btn, 0);
                lv_obj_set_style_text_font(label, &zh, 0);
                if (i == player->current_song_index)
                    lv_obj_set_style_bg_color(btn, lv_color_hex(0x007BFF), LV_PART_MAIN);
                // 事件：点击切换歌曲
                lv_obj_add_event_cb(btn, event_handler, LV_EVENT_ALL, this);
            }
        }
        void progress_set_range(uint16_t total_time) {
            lv_slider_set_range(progress_bar, 0, total_time);
            lv_label_set_text_fmt(totalTime_label, "%02d:%02d", total_time / 60, total_time % 60);
        }
        void progress_update(uint16_t time, bool update_bar = true, bool update_time = true) {
            if (update_bar)
                lv_slider_set_value(progress_bar, time, LV_ANIM_OFF);
            
            if (update_time)
                lv_label_set_text_fmt(curTime_label, "%02d:%02d", time / 60, time % 60);
        }
        void songName_set(std::string_view name) {
            lv_label_set_text(songName_label, name.data());
        }
        void volume_set(uint8_t vol) {
            lv_slider_set_value(vol_slider, vol, LV_ANIM_OFF);
        }
        void state_set_playing(bool playing = true) {
            if (playing) 
                lv_label_set_text(lv_obj_get_child(play_btn, 0), LV_SYMBOL_PAUSE);
            else
                lv_label_set_text(lv_obj_get_child(play_btn, 0), LV_SYMBOL_PLAY);
        }
        void state_toggle_playing() {
            if (lv_label_get_text(lv_obj_get_child(play_btn, 0)) == std::string_view(LV_SYMBOL_PLAY))
                state_set_playing(true);
            else
                state_set_playing(false);
        }
        // 更新播放模式按钮显示
        void mode_set_display(PlayMode mode) {
            auto mode_label = lv_obj_get_child(mode_btn, 0);
            switch (mode) {
                case PlayMode::SEQUENTIAL:
                    lv_label_set_text(mode_label, LV_SYMBOL_LOOP);
                    break;
                case PlayMode::SINGLE_LOOP:
                    lv_label_set_text(mode_label, LV_SYMBOL_REFRESH);
                    break;
                case PlayMode::RANDOM:
                    lv_label_set_text(mode_label, LV_SYMBOL_SHUFFLE);
                    break;
            }
        }
    } ui{this};

    // 状态
    bool is_playing{};
    size_t current_song_index{0};
    PlayMode current_play_mode{PlayMode::SEQUENTIAL}; // 默认顺序播放
    mutable std::mutex volume_mutex, state_mutex, song_mutex;
    mutable std::condition_variable cv;
    std::function<void()> lv_lock = []{}, lv_unlock = []{}; // lvgl互斥锁

    Playlist playlist;
    Audio song;
    std::shared_ptr<AudioDevice> device;

    bool playBuffer{};
    int16_t buffer[2][8192];

    unsigned fill_buffer() {
        auto& buf = buffer[!playBuffer];
        playBuffer = !playBuffer;
        std::lock_guard song_lk(song_mutex);
        auto bytesRead = song.read(reinterpret_cast<uint8_t*>(buf), sizeof buf);
        return bytesRead;
    }

    void load(size_t index) {
        if (playlist.empty())
            return;
        if (index >= playlist.size())
            index = 0;

        current_song_index = index;

        std::string_view name = playlist[current_song_index];
        std::unique_lock song_lk(song_mutex);
        if (song.load(name) == -1)
            return;
        auto total_time = song.total_time();
        song_lk.unlock();
        
        ScopedLock lock(lv_lock, lv_unlock);
        // 更新ui
        ui.songName_set(name);
        ui.progress_set_range(total_time);
        ui.progress_update(0);
        ui.playlist_update(current_song_index);
    }
    
    // 根据播放模式获取下一首歌曲索引
    size_t get_next_song_index() {
        if (playlist.empty())
            return 0;
        
        return (current_song_index + 1) % playlist.size();
    }
    
    // 根据播放模式获取上一曲歌曲索引
    size_t get_prev_song_index() {
        if (playlist.empty())
            return 0;
        
        return (current_song_index == 0) ? playlist.size() - 1 : current_song_index - 1;
    }

public:
    Player() = default;
    
    void init(decltype(device) dev = nullptr, std::tuple<decltype(lv_lock), decltype(lv_unlock)> mutex_funcs = {}, decltype(list_shuffle) shuffle = {}) {
        if (shuffle)
            list_shuffle = shuffle;
        
        if (mutex_funcs != std::make_tuple(nullptr, nullptr)) {
            lv_lock = std::get<0>(mutex_funcs);
            lv_unlock = std::get<1>(mutex_funcs);
        }
        
        {
            ScopedLock lock(lv_lock, lv_unlock);
            ui.init();
            ui.event_init();
            ui.playlist_load(playlist);
            ui.state_set_playing(is_playing);
            ui.mode_set_display(current_play_mode); // 设置初始播放模式显示
        }
        
        if (dev)
            bind_device(dev);
        else {
            ScopedLock lock(lv_lock, lv_unlock);
            lv_obj_remove_flag(ui.play_btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(ui.vol_btn, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    // 搜索歌曲
    void search_songs(std::string_view path) {
        playlist = Audio::scan_directory(path);
        if (playlist.empty())
            return;
        
        load(0); // 默认加载第一首歌
        
        ScopedLock lock(lv_lock, lv_unlock);
        ui.playlist_load(playlist);
    }
    void reload() {
        load(current_song_index);
    }
    // 上一曲
    void prev_song() {
        load(get_prev_song_index());
    }
    // 下一曲
    void next_song() {
        load(get_next_song_index());
    }
    
    // 切换播放模式
    void switch_play_mode() {
        // 记住当前播放的歌曲名称
        std::string current_song;
        if (current_song_index < playlist.size()) {
            current_song = playlist[current_song_index];
        }
        
        switch (current_play_mode) {
            case PlayMode::SEQUENTIAL:
                current_play_mode = PlayMode::SINGLE_LOOP;
                break;
            case PlayMode::SINGLE_LOOP:
                current_play_mode = PlayMode::RANDOM;
                // 切换到随机模式时洗牌
                list_shuffle(playlist);
                // 找到当前歌曲在洗牌后列表中的新位置
                if (!current_song.empty()) {
                    auto it = std::ranges::find(playlist, current_song);
                    if (it != playlist.end()) {
                        current_song_index = std::distance(playlist.begin(), it);
                    }
                }
                break;
            case PlayMode::RANDOM:
                current_play_mode = PlayMode::SEQUENTIAL;
                // 切换回顺序模式时重新扫描文件夹以恢复原始顺序
                if (!playlist.empty()) {
                    // 重新扫描当前播放歌曲所在的目录
                    auto current_path = playlist[current_song_index];
                    auto last_slash = current_path.find_last_of("/\\");
                    if (last_slash != std::string::npos) {
                        auto dir_path = current_path.substr(0, last_slash);
                        playlist = Audio::scan_directory(dir_path);
                        // 找到当前歌曲在原始列表中的位置
                        if (!current_song.empty()) {
                            auto it = std::ranges::find(playlist, current_song);
                            if (it != playlist.end()) {
                                current_song_index = std::distance(playlist.begin(), it);
                            }
                        }
                    }
                }
                break;
        }
        
        ScopedLock lock(lv_lock, lv_unlock);
        ui.mode_set_display(current_play_mode);
        ui.playlist_load(playlist); // 重新加载播放列表UI
    }
    
    // 获取当前播放模式
    PlayMode get_play_mode() const {
        return current_play_mode;
    }
    // 播放/暂停控制
    void play() {
        std::lock_guard state_lk(state_mutex);
        if (is_playing)
            return;
        is_playing = true;
        cv.notify_one();
        
        ScopedLock lock(lv_lock, lv_unlock);
        ui.state_set_playing();
    }
    void pause() {
        std::lock_guard state_lk(state_mutex);
        if (!is_playing)
            return;
        is_playing = false;

        ScopedLock lock(lv_lock, lv_unlock);
        ui.state_set_playing(false);
    }
    void toggle_play_pause() {
        std::unique_lock state_lk(state_mutex);
        if (is_playing) {
            state_lk.unlock();
            pause();
        } else {
            state_lk.unlock();
            play();
        }
    }
    // 音量控制方法
    void set_volume(uint8_t vol) {
        std::lock_guard volume_lk(volume_mutex);
        device->set_volume(vol);
        
        ScopedLock lock(lv_lock, lv_unlock);
        ui.volume_set(vol);
    }
    uint8_t get_volume() const {
        std::lock_guard volume_lk(volume_mutex);
        return device->get_volume();
    }
    // 注册互斥锁函数
    void register_mutex(std::function<void()> mutex_lock, std::function<void()> mutex_unlock) {
        this->lv_lock = mutex_lock;
        this->lv_unlock = mutex_unlock;
    }
    void bind_device(std::shared_ptr<AudioDevice> dev) {
        if (!dev)
            return;
        device = dev;

        ScopedLock lock(lv_lock, lv_unlock);
        lv_obj_add_flag(ui.play_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(ui.vol_btn, LV_OBJ_FLAG_CLICKABLE);
        ui.volume_set(device->get_volume());
    }

    void progress_update() {
        // 降低UI更新频率
        static uint32_t progress_update_counter;
        uint16_t current_time{};
        {
            std::lock_guard song_lk(song_mutex);
            current_time = song.current_time();
        }
        if (++progress_update_counter >= 5) {
            ScopedLock lock(lv_lock, lv_unlock);
            
            if (!ui.is_dragging_progress)
                ui.progress_update(current_time);
            else
                ui.progress_update(current_time, false, true); // 拖动时不更新进度条
            progress_update_counter = 0;
        }
    }
    // 音乐播放任务
    void task_handler() {
        std::unique_lock state_lk(state_mutex);
        cv.wait(state_lk, [this] { return is_playing; }); // 等待播放状态变为true
        state_lk.unlock();
        
        if (!device) {
            pause();
            return;
        }

        std::unique_lock song_lk(song_mutex);
        if (!song.is_valid()) {
            song_lk.unlock();
            pause();
            return;
        }
        song_lk.unlock();

        if (device->is_circular_mode()) {
            // 重置缓冲区状态和信号量
            playBuffer = true;
            device->sem_reset(1); // 重置信号量状态
            
            auto bytesRead = fill_buffer(); // 预填充缓冲区
            {
                std::lock_guard volume_lk(volume_mutex);
                device->volume.apply(*buffer, sizeof buffer / 2);
            }
            if (bytesRead == 0) {
                device->transmit_stop();
                if (current_play_mode == PlayMode::SINGLE_LOOP) {
                    reload(); // 单曲循环
                } else {
                    next_song(); // 切换到下一首
                }
                return;
            }
            std::fill(buffer[playBuffer] + bytesRead / 2, buffer[playBuffer] + (sizeof(buffer[playBuffer]) / 2), 0);
            device->transmit(reinterpret_cast<int16_t*>(buffer), sizeof buffer / 2);
            while (true) {
                device->sem_acquire();

                state_lk.lock();
                if (!is_playing) {
                    device->transmit_stop();
                    return;
                }
                state_lk.unlock();

                auto bytesRead = fill_buffer();
                {
                    std::lock_guard volume_lk(volume_mutex);
                    device->volume.apply(buffer[playBuffer], bytesRead / 2);
                }
                if (bytesRead == 0) {
                    if (current_play_mode == PlayMode::SINGLE_LOOP) {
                        reload(); // 单曲循环
                    } else {
                        next_song(); // 切换到下一首
                    }
                    return;
                }
                std::fill(buffer[playBuffer] + bytesRead / 2, buffer[playBuffer] + (sizeof(buffer[playBuffer]) / 2), 0);
                progress_update();
            }
        } else {
            auto bytesRead = fill_buffer();
            {
                std::lock_guard volume_lk(volume_mutex);
                device->volume.apply(buffer[playBuffer], bytesRead / 2);
            }
            if (bytesRead == 0) {
                if (current_play_mode == PlayMode::SINGLE_LOOP) {
                    reload(); // 单曲循环
                } else {
                    next_song(); // 切换到下一首
                }
                return;
            }
            
            device->sem_acquire();

            device->transmit(buffer[playBuffer], bytesRead / 2);
            progress_update();
        }
    }
    
    // 跳转
    void seek(uint16_t time_seconds) {
        std::lock_guard song_lk(song_mutex);
        song.seek_to(time_seconds);
    }
};

#endif // PLAYER_H
