# GK01 原生 HomeKit 红外遥控器固件

把小兀 GK01 红外遥控器（中国移动配 App 那款）刷成本项目固件后，它将变成一个**原生 Apple HomeKit 配件**——不需要任何网桥/服务器，直接在 iPhone 的"家庭"App 中添加，可以：

1. **学习**家里电器遥控器的红外信号（任意协议，含空调原始码）
2. 在**家庭 App 里点按按钮发射**红外信号控制电器
3. 收到已学按键的红外码时**反向触发 iOS 自动化**（如"对着 GK01 按电视遥控器 → 关灯"）

参考项目：[summmer121/gk01-ir-receiver-launch](https://github.com/summmer121/gk01-ir-receiver-launch)（ESPHome 方案，引脚参数来源）；本项目改用 [Mixiaoxiao/Arduino-HomeKit-ESP8266](https://github.com/Mixiaoxiao/Arduino-HomeKit-ESP8266) 实现原生 HomeKit，红外收发使用 IRremoteESP8266。

---

## ⚠️ 重要：先确认你的 GK01 用的是哪颗芯片

参考项目的作者拆机确认 GK01 主控是 **ESP12F（ESP8266EX，4MB Flash）**，而不是 ESP32（ESP32 是很多资料对这类设备的笼统说法）。本固件按 **ESP8266** 编写。

验证方法（任选其一）：
- 看板上屏蔽罩丝印：`ESP12F` / `ESP-12F` 即为本项目目标硬件
- 串口（115200）上电日志出现 `boot mode:(3,x)`、`load:0x…` 等字样即 ESP8266

如果确认是 ESP32（丝印 ESP32-C3/ESP32 等），**本项目固件不适用**——ESP32 上应改用 [HomeSpan](https://github.com/HomeSpan/HomeSpan) 框架，架构不同，需要另外生成代码。

## 硬件引脚（拆机确认）

| 功能 | GPIO | 说明 |
|---|---|---|
| 红外接收 | GPIO5 | 接收头输出低有效，内部上拉 |
| 红外发射 | GPIO14 | 38kHz 载波 |
| 红色 LED | GPIO12 | 发送指示 |
| 黄色 LED | GPIO13 | 接收/学习指示 |
| 实体按键 | GPIO16 | 板载下拉，按下为高 |

如 LED 亮灭相反，把 `include/config.h` 里 `LED_ACTIVE_LOW` 改为 `true`。

## 固件功能设计

配件树（iOS 家庭 App 中一块配件）：

```
GK01
 ├─ 学习模式 (开关)     打开后，下一个收到的红外码存入空的"按键N"
 ├─ 按键1 (开关)        学到的红外码；点按 = 发射
 ├─ 按键2 (开关)        …
 └─ 按键6 (开关)        共 6 个槽位 (受 ESP8266 内存限制)
```

- **学习**：家庭 App 打开"学习模式"（或**短按 GK01 实体按键**），黄灯快闪后用原遥控器对准 GK01 按一下 → 自动存为下一个空槽位，学习模式自动关闭
- **发射**：点按"按键N"即以 38kHz 原始时序回放
- **反向联动**：收到与已学按键相同的码（NEC 等可解码协议）时，对应开关会自动开→关一次，可用它触发 iOS 自动化
- 红外码以 RAW 时序保存在 LittleFS（`/slotN.txt`），任意协议可学可发
- **长按实体按键 10 秒**：清除全部红外码 + HomeKit 配对 + WiFi 配置
- WiFi 配网：首用（或配网失败）时开放热点 `GK01-Setup`， captive portal 配网
- LED 含义：黄灯快闪=等待学习；黄灯双闪=学习成功；红灯亮=发射中；红黄齐闪后重启=恢复出厂

## 刷机步骤

### 方式一：PlatformIO（推荐）

1. 安装 [VSCode](https://code.visualstudio.com/) + PlatformIO 插件
2. 打开本项目文件夹，`pio run -t upload`（或点 ➜ Upload）
3. 串口监视：`pio device monitor`（115200）

### 方式二：GitHub Actions 云端编译

1. 把本项目推到自己的 GitHub 仓库
2. Actions 会自动编译并产出 `gk01-homekit-firmware` 的 `firmware.bin`
3. 下载后本地用 esptool 烧录：

```bash
esptool.py --port COM5 --baud 115200 write_flash 0x0 firmware.bin
```

### 烧录接线（USB-TTL，务必 3.3V 供电！）

| GK01 | USB-TTL |
|---|---|
| 3V3 | 3.3V |
| GND | GND |
| RX/TX 交叉接 | TX/RX |
| GPIO0 | **刷机时接 GND**，上电后松开进入下载模式 |

首次烧录建议全片擦除（`esptool.py erase_flash`）。

## 首次配置

1. 上电后手机连接热点 `GK01-Setup`，自动弹出配网页选择你家 WiFi（浏览器访问 192.168.4.1 亦可）
2. 打开 iOS「家庭」→ ＋ → 添加配件 → 「更多选项…」→ 选择 **GK01**
3. 出现"未认证配件"提示 → 选「仍然添加」→ 配对码输入 **`123-45-678`**
4. 配对约需 14 秒（ESP8266 的 HomeKit 加密计算较慢，属正常现象）

## 学习电器遥控器

1. 家庭 App 中打开「学习模式」（或短按 GK01 实体按键），黄灯快闪
2. 用电器原遥控器对准 GK01 接收头按一下目标按键
3. 黄灯双闪表示完成，码自动存入「按键1」（依次类推），学习模式自动关闭
4. 点按「按键1」即可控制电器；长按图标可把开关在 App 里重新命名

## 已知限制

- 最多 6 个学习槽位（ESP8266 内存所限；改 `MAX_BUTTONS` 需同步修改 `src/accessory.c` 的宏展开）
- 未识别协议（UNKNOWN）的原始码：可学习、可发射，但**不会**触发反向自动化
- 修改"按键N"名称需重新刷机或在代码中修改（HomeKit 名字特征更新后 iOS 可能缓存旧值）
- 非 MFi 认证配件无二维码可扫，只能手动输码添加
- 换 WiFi 环境后若 HomeKit 连不上，先重启设备；仍不行则长按 10s 恢复出厂重新配对

## 工程结构

```
gk01-homekit/
├── platformio.ini            # 构建配置 (ESP8266 core 2.7.4 + 160MHz)
├── include/config.h          # 引脚与功能参数（改这里即可适配）
├── src/accessory.c           # HomeKit 配件树定义 (官方库宏仅支持 C)
├── src/accessory.h           # C/C++ 桥接声明
├── src/main.cpp              # 红外学习/发射/存储/按键/LED 业务逻辑
├── .github/workflows/build.yml  # 云端自动编译
└── README.md
```

## 故障排查

| 现象 | 处理 |
|---|---|
| 串口乱码 | 波特率应为 115200 |
| 上电即重启循环 | 供电不足，换 ≥500mA 的 3.3V 电源 |
| 家庭 App 搜不到配件 | 等待 preinit 约 9 秒；确认手机与设备同一 WiFi；串口看 heap 是否 >5000 |
| 配对中途失败 | CPU 必须 160MHz（`platformio.ini` 已配置）；重新上电再试 |
| 学到的码无法控制电器 | 发射时红灯应亮；对准电器接收口；空调码较长，确认 `IR_CAPTURE_BUFFER` 未截断 |
| LED 亮灭颠倒 | `LED_ACTIVE_LOW` 改为 `true` |
