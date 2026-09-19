#pragma once

// ============================================================
//  GK01 硬件参数
//  引脚定义来自 summmer121/gk01-ir-receiver-launch 项目的拆机确认:
//    主控: ESP12F (ESP8266EX, 4MB Flash)
//    红外接收: GPIO5 (接收头输出低有效, 需上拉)
//    红外发射: GPIO14 (38kHz 载波)
//    红灯 GPIO12 (发送指示), 黄灯 GPIO13 (接收/学习指示)
//    实体按键 GPIO16 (板载下拉, 按下为高电平)
// ============================================================

#define PIN_IR_RECV        5
#define PIN_IR_SEND        14
#define PIN_LED_RED        12
#define PIN_LED_YELLOW     13
#define PIN_BUTTON         16

// 若你的 GK01 板子上 LED 是低电平点亮, 把它改成 true
#define LED_ACTIVE_LOW     false

// ============================================================
//  功能参数
// ============================================================
#define DEVICE_NAME        "GK01"           // 配件名 (iOS 里可自行重命名)
#define HOSTNAME           "GK01"
#define WIFI_AP_NAME       "GK01-Setup"     // 配网热点 (与原厂案例一致)
#define HOMEKIT_PASSWORD   "123-45-678"     // Apple 家庭配对码
#define HOMEKIT_SETUP_ID   "1QJ8"

// 可学习按键数量。受 ESP8266 内存限制, 不建议超过 6
#define MAX_BUTTONS        6
#define MAX_NAME_LEN       24               // 按键名最大字节数 (UTF-8)

#define IR_FREQ            38               // 发射载波 kHz
#define IR_CAPTURE_BUFFER  600              // 原始码定时机数量上限 (空调遥控器需较大值)

#define BUTTON_LONGPRESS_MS 10000           // 长按实体按键 10 秒 = 恢复出厂
