#pragma once

// HomeKit 配件定义在 src/accessory.c (纯 C, 官方库的宏只能在 C 中使用)。
// 本头文件供 main.cpp (C++) 访问 accessory.c 中的对象与桥接函数。

#include "config.h"

#include <arduino_homekit_server.h>

#ifdef __cplusplus
extern "C" {
#endif

// accessory.c 中定义的对象
extern homekit_server_config_t config;
extern homekit_characteristic_t cha_learn;                 // "学习模式" 开关

// ---- accessory.c 实现, 供 main.cpp 调用 ----
void hk_set_button_name(int idx, const char *name); // 修改并通知某个按键的名字
void hk_notify_button_on(int idx);                  // 模拟按键被按下 (触发 iOS 自动化)
void hk_notify_button_off(int idx);                 // 按键回弹到关闭状态
void hk_set_learn_value(int on);                    // 同步"学习模式"开关到 iOS

// ---- main.cpp 实现, 供 accessory.c 的 setter 回调 ----
void hk_button_press(int idx);                      // iOS 里点击了第 idx 个按键
void hk_learn_changed(int on);                      // iOS 里切换了"学习模式"

#ifdef __cplusplus
}
#endif
