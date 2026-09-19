/*
 * main.cpp - GK01 红外遥控器 → Apple HomeKit 原生配件固件
 *
 * 硬件 (引脚来自 summmer121/gk01-ir-receiver-launch 拆机确认):
 *   主控: ESP12F (ESP8266) / 红外接收 GPIO5 / 红外发射 GPIO14
 *   红灯 GPIO12 (发送) / 黄灯 GPIO13 (接收) / 按键 GPIO16 (下拉, 按下=高)
 *
 * 功能:
 *   1. 原生 HomeKit: 无需任何网桥, 直接添加到苹果"家庭"App
 *   2. 学习模式: 家庭 App 中打开"学习模式"开关 (或短按实体按键),
 *      用家里的遥控器对准 GK01 按一下即可学到"按键N"
 *   3. 发射: 点按家庭 App 中的"按键N"即发射对应的红外信号
 *   4. 反向联动: 收到与已学按键相同的红外码时, 对应"按键N"会短暂
 *      触发一次, 可用来做"按实体遥控器 -> 跑 iOS 自动化"
 *   5. 红外码以原始时序 (RAW) 保存到 LittleFS, 任意协议 (NEC/RC5/空调码)
 *      都能学习与回放
 *
 * 使用流程详见 README.md
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <LittleFS.h>

#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>

#include <arduino_homekit_server.h>

#include "config.h"
#include "accessory.h"

// ============================================================
//  全局对象
// ============================================================
static IRrecv irrecv(PIN_IR_RECV, IR_CAPTURE_BUFFER);
static IRsend irsend(PIN_IR_SEND);
static decode_results results;

// IRremoteESP8266 采集缓冲每个 tick = 50us
static const uint16_t US_PER_TICK = 50;

struct IrCode {
	uint16_t count;                       // 定时机数量 (微秒)
	uint16_t us[IR_CAPTURE_BUFFER];
	int16_t dtype;                        // 解码协议 (UNKNOWN 时为 -1)
	uint64_t value;                       // 解码值
};
static IrCode irbuf;                      // 采集 / 发射共用缓冲 (~1.2KB)

static char     slot_name[MAX_BUTTONS][MAX_NAME_LEN + 1];
static bool     slot_used[MAX_BUTTONS];
static int16_t  key_type[MAX_BUTTONS];    // 收到红外码时的匹配键
static uint64_t key_value[MAX_BUTTONS];

static bool     learn_mode = false;
static uint32_t switchOffAt[MAX_BUTTONS]; // "按键N"回弹时间戳
static uint32_t nextHeapLog = 0;

// 按键消抖
static bool     btnState = false;
static uint32_t btnDownMs = 0;
static bool     longFired = false;

// ============================================================
//  LED / 日志辅助
// ============================================================
static inline void ledRed(bool on) {
	digitalWrite(PIN_LED_RED, LED_ACTIVE_LOW ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
}
static inline void ledYellow(bool on) {
	digitalWrite(PIN_LED_YELLOW, LED_ACTIVE_LOW ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
}

static void pulseYellow() {
	ledYellow(true);  delay(120);
	ledYellow(false); delay(60);
	ledYellow(true);  delay(120);
	ledYellow(false);
}

static void errBlink() {
	for (int i = 0; i < 4; i++) {
		ledRed(true); delay(90); ledRed(false); delay(90);
	}
}

#define LOG_D(fmt, ...) Serial.printf_P(PSTR("[GK01] " fmt "\n"), ##__VA_ARGS__)

// ============================================================
//  红外码存储 (LittleFS, 每个按键一个文件 /slotN.txt)
//  文件格式: 协议;解码值(HEX);定时机数;按键名;us0,us1,us2,...
// ============================================================
static String slotPath(int i) {
	return String("/slot") + i + ".txt";
}

static void sanitizeName(char *name) {
	for (char *p = name; *p; p++) {
		if (*p == ';' || *p == ',' || *p == '\n' || *p == '\r') *p = '_';
	}
}

static void u64ToHex(uint64_t v, char *out) {
	sprintf(out, "%08X%08X", (uint32_t)(v >> 32), (uint32_t)(v & 0xFFFFFFFFULL));
}

static bool saveSlot(int slot) {
	File f = LittleFS.open(slotPath(slot), "w");
	if (!f) return false;
	char vbuf[20];
	u64ToHex(irbuf.value, vbuf);
	f.print(irbuf.dtype);   f.print(';');
	f.print(vbuf);          f.print(';');
	f.print(irbuf.count);   f.print(';');
	f.print(slot_name[slot]); f.print(';');
	for (uint16_t i = 0; i < irbuf.count; i++) {
		f.print(irbuf.us[i]);
		if (i + 1 < irbuf.count) f.print(',');
	}
	f.print('\n');
	f.close();
	return true;
}

// 完整读取一个按键的红外码到 irbuf
static bool loadSlotCode(int slot) {
	File f = LittleFS.open(slotPath(slot), "r");
	if (!f) return false;
	String dt  = f.readStringUntil(';');
	String val = f.readStringUntil(';');
	String cnt = f.readStringUntil(';');
	String nm  = f.readStringUntil(';');
	irbuf.dtype = (int16_t)dt.toInt();
	irbuf.value = strtoull(val.c_str(), NULL, 16);
	irbuf.count = (uint16_t)cnt.toInt();
	if (nm.length()) {
		strlcpy(slot_name[slot], nm.c_str(), MAX_NAME_LEN + 1);
	}
	bool ok = (irbuf.count > 0 && irbuf.count <= IR_CAPTURE_BUFFER);
	if (ok) {
		for (uint16_t i = 0; i < irbuf.count; i++) {
			irbuf.us[i] = f.available() ? (uint16_t)f.readStringUntil(',').toInt() : 0;
		}
	}
	f.close();
	return ok;
}

// 开机时读取全部按键的元数据 (顺便载入 irbuf 提取头部)
static void loadAllSlots() {
	for (int i = 0; i < MAX_BUTTONS; i++) {
		slot_used[i] = false;
		key_type[i]  = -1;
		key_value[i] = 0;
		if (!LittleFS.exists(slotPath(i))) continue;
		if (!loadSlotCode(i)) continue;
		slot_used[i] = true;
		key_type[i]  = irbuf.dtype;
		key_value[i] = irbuf.value;
		LOG_D("槽位 %d: %s (协议=%d)", i + 1, slot_name[i], key_type[i]);
	}
}

// ============================================================
//  红外采集 / 匹配 / 发射
// ============================================================
static void learnStore() {
	int slot = -1;
	for (int i = 0; i < MAX_BUTTONS; i++) {
		if (!slot_used[i]) { slot = i; break; }
	}
	if (slot < 0) {
		LOG_D("所有按键槽位已满, 请先删除 (长按10秒恢复出厂)");
		learn_mode = false;
		hk_set_learn_value(0);
		errBlink();
		return;
	}
	snprintf(slot_name[slot], MAX_NAME_LEN + 1, "按键%d", slot + 1);
	sanitizeName(slot_name[slot]);
	if (!saveSlot(slot)) {
		LOG_D("写入 LittleFS 失败");
		errBlink();
		return;
	}
	slot_used[slot] = true;
	key_type[slot]  = irbuf.dtype;
	key_value[slot] = irbuf.value;
	hk_set_button_name(slot, slot_name[slot]);

	learn_mode = false;
	hk_set_learn_value(0);
	LOG_D("已学习 -> 槽位 %d (%s), 红外码 %d 个定时机, 协议 %d",
	      slot + 1, slot_name[slot], irbuf.count, irbuf.dtype);
	pulseYellow();
}

static void matchNotify() {
	if (irbuf.dtype < 0) return;   // 未识别协议的原始码不做匹配
	for (int i = 0; i < MAX_BUTTONS; i++) {
		if (slot_used[i] && key_type[i] == irbuf.dtype && key_value[i] == irbuf.value) {
			LOG_D("收到已学码, 触发\"%s\"", slot_name[i]);
			hk_notify_button_on(i);
			switchOffAt[i] = millis() + 600;
			return;
		}
	}
}

static void handleIrCapture() {
	if (results.rawlen < 3) { irrecv.resume(); return; }
	// 忽略 NEC 重复码 (长按音量键等)
	if (results.decode_type == NEC && results.value == 0xFFFFFFFFULL) {
		irrecv.resume();
		return;
	}
	uint16_t n = results.rawlen - 1;               // rawbuf[0] 是前导间隔, 跳过
	if (n > IR_CAPTURE_BUFFER) n = IR_CAPTURE_BUFFER;
	for (uint16_t i = 0; i < n; i++) {
		irbuf.us[i] = (uint16_t)(results.rawbuf[i + 1] * US_PER_TICK);
	}
	irbuf.count = n;
	if (results.decode_type != UNKNOWN) {
		irbuf.dtype = (int16_t)results.decode_type;
		irbuf.value = results.value;
	} else {
		irbuf.dtype = -1;
		irbuf.value = 0;
	}
	irrecv.resume();

	if (learn_mode) learnStore();
	else            matchNotify();
}

static void sendSlotNow(int slot) {
	if (!slot_used[slot]) {
		LOG_D("槽位 %d 为空, 请先进入学习模式", slot + 1);
		return;
	}
	if (!loadSlotCode(slot)) {
		LOG_D("槽位 %d 数据损坏", slot + 1);
		errBlink();
		return;
	}
	irrecv.disableIRIn();          // 发射期间暂停接收, 避免收到自己
	ledRed(true);
	irsend.sendRaw(irbuf.us, irbuf.count, IR_FREQ);
	ledRed(false);
	irrecv.enableIRIn();
	LOG_D("已发射 \"%s\" (%d 定时机)", slot_name[slot], irbuf.count);
}

// ============================================================
//  HomeKit 桥接 (accessory.c 调用)
// ============================================================
extern "C" void hk_button_press(int idx) {
	LOG_D("iOS 触发按键 %d", idx + 1);
	sendSlotNow(idx);
	switchOffAt[idx] = millis() + 500;   // 开关回弹
}

extern "C" void hk_learn_changed(int on) {
	learn_mode = (on != 0);
	LOG_D("学习模式: %s (黄灯快闪 = 等待红外码)", learn_mode ? "开" : "关");
}

static void toggleLearnFromButton() {
	learn_mode = !learn_mode;
	hk_set_learn_value(learn_mode ? 1 : 0);
}

// ============================================================
//  实体按键 (GPIO16, 按下 = 高)
//  短按: 切换学习模式 / 长按 10s: 恢复出厂
// ============================================================
static void factoryReset() {
	LOG_D("恢复出厂: 清除红外码 + HomeKit 配对 + WiFi 配置");
	ledRed(true); ledYellow(true);
	for (int i = 0; i < MAX_BUTTONS; i++) LittleFS.remove(slotPath(i));
	homekit_storage_reset();
	WiFi.disconnect(true);
	delay(300);
	ESP.restart();
}

static void pollButton() {
	bool pressed = digitalRead(PIN_BUTTON) == HIGH;
	if (pressed && !btnState) {
		btnState = true;
		btnDownMs = millis();
		longFired = false;
	} else if (pressed && btnState && !longFired
	           && millis() - btnDownMs > BUTTON_LONGPRESS_MS) {
		longFired = true;
		factoryReset();
	} else if (!pressed && btnState) {
		btnState = false;
		if (!longFired && millis() - btnDownMs > 50) {
			toggleLearnFromButton();
		}
	}
}

// ============================================================
//  WiFi 配网 (WiFiManager, 失败时开放热点 GK01-Setup)
// ============================================================
static void wifiConnect() {
	WiFi.hostname(HOSTNAME);
	WiFi.mode(WIFI_STA);
	WiFi.setSleepMode(WIFI_NONE_SLEEP);
	WiFiManager wm;
	wm.setConfigPortalTimeout(180);
	if (!wm.autoConnect(WIFI_AP_NAME)) {
		LOG_D("WiFi 配网超时, 重启");
		ESP.restart();
	}
	LOG_D("WiFi 已连接: %s, IP: %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

// ============================================================
//  Arduino setup / loop
// ============================================================
void setup() {
	Serial.begin(115200);
	delay(100);

	pinMode(PIN_LED_RED, OUTPUT);
	pinMode(PIN_LED_YELLOW, OUTPUT);
	ledRed(false); ledYellow(false);
	pinMode(PIN_BUTTON, INPUT_PULLDOWN_16);

	irsend.begin();
	if (!LittleFS.begin()) {
		LittleFS.format();
		LittleFS.begin();
	}
	loadAllSlots();

	wifiConnect();

	LOG_D("启动 HomeKit 服务 (首次 preinit 约需 9 秒)...");
	arduino_homekit_setup(&config);
	LOG_D("HomeKit 就绪, 配对码: " HOMEKIT_PASSWORD);

	irrecv.enableIRIn();

	// 开机时把已学按键名同步到 HomeKit 特征
	for (int i = 0; i < MAX_BUTTONS; i++) {
		if (slot_used[i]) hk_set_button_name(i, slot_name[i]);
	}
}

void loop() {
	arduino_homekit_loop();

	// 红外接收
	if (irrecv.decode(&results)) handleIrCapture();

	// "按键N"开关回弹
	uint32_t now = millis();
	for (int i = 0; i < MAX_BUTTONS; i++) {
		if (switchOffAt[i] && (int32_t)(now - switchOffAt[i]) >= 0) {
			switchOffAt[i] = 0;
			hk_notify_button_off(i);
		}
	}

	// 学习模式黄灯快闪
	if (learn_mode) {
		ledYellow((now / 250) & 1);
	}

	pollButton();

	if (now > nextHeapLog) {
		nextHeapLog = now + 30000;
		LOG_D("空闲内存: %u, HomeKit 客户端: %d",
		      ESP.getFreeHeap(), arduino_homekit_connected_clients_count());
	}

	delay(2);
}
