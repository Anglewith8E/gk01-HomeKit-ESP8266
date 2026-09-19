/*
 * accessory.c - HomeKit 配件定义 (纯 C)
 *
 * 官方库 (Mixiaoxiao/Arduino-HomeKit-ESP8266) 的 HOMEKIT_ACCESSORY /
 * HOMEKIT_SERVICE / HOMEKIT_CHARACTERISTIC 宏只能在 C 文件中使用,
 * 因此配件定义放在这里, 业务逻辑在 main.cpp 中。
 *
 * 配件结构:
 *   [GK01] (category: Switch)
 *     ├── Accessory Information
 *     ├── Switch "学习模式" (primary)   开: 进入学习模式, 学到下一个红外码
 *     ├── Switch "按键1"                学到的红外码, 点按 = 发射
 *     ├── Switch "按键2"
 *     └── ... 共 MAX_BUTTONS 个
 *
 * 收到与已学按键匹配的红外码时, 设备会短暂触发对应"按键N"开关,
 * iOS 家庭 App 里的自动化即可被实体遥控器的按键触发。
 */

#include <homekit/homekit.h>
#include <homekit/characteristics.h>

#include "config.h"

#if MAX_BUTTONS != 6
#error "当前 accessory.c 按 6 个按键编写, 如需修改请同步调整宏展开部分"
#endif

// ---- main.cpp 中实现的桥接函数 ----
extern void hk_button_press(int idx);
extern void hk_learn_changed(int on);

void my_accessory_identify(homekit_value_t _value) {
	(void)_value;
	// iOS 里的"识别"操作, 由 main.cpp 通过串口日志体现即可
}

// "学习模式" 开关
static void on_learn_set(homekit_value_t v) {
	hk_learn_changed(v.bool_value ? 1 : 0);
}

homekit_characteristic_t cha_learn = HOMEKIT_CHARACTERISTIC_(ON, false, .setter = on_learn_set);

#define DEF_BTN_SETTER(n) \
	static void on_btn_##n##_set(homekit_value_t v) { (void)v; hk_button_press(n - 1); }

DEF_BTN_SETTER(1)
DEF_BTN_SETTER(2)
DEF_BTN_SETTER(3)
DEF_BTN_SETTER(4)
DEF_BTN_SETTER(5)
DEF_BTN_SETTER(6)

// ---- 按键特征 ----
#define DEF_BTN_CHA(n) \
	static homekit_characteristic_t cha_btn_name_##n = HOMEKIT_CHARACTERISTIC_(NAME, "按键"#n); \
	static homekit_characteristic_t cha_btn_on_##n   = HOMEKIT_CHARACTERISTIC_(ON, false, .setter = on_btn_##n##_set);

DEF_BTN_CHA(1)
DEF_BTN_CHA(2)
DEF_BTN_CHA(3)
DEF_BTN_CHA(4)
DEF_BTN_CHA(5)
DEF_BTN_CHA(6)

#define BTN_NAME_PTR(n) &cha_btn_name_##n
#define BTN_ON_PTR(n)   &cha_btn_on_##n

homekit_characteristic_t *cha_btn_name[MAX_BUTTONS] = {
	BTN_NAME_PTR(1), BTN_NAME_PTR(2), BTN_NAME_PTR(3),
	BTN_NAME_PTR(4), BTN_NAME_PTR(5), BTN_NAME_PTR(6)
};

homekit_characteristic_t *cha_btn_on[MAX_BUTTONS] = {
	BTN_ON_PTR(1), BTN_ON_PTR(2), BTN_ON_PTR(3),
	BTN_ON_PTR(4), BTN_ON_PTR(5), BTN_ON_PTR(6)
};

#define BTN_SERVICE(n) \
	HOMEKIT_SERVICE(SWITCH, .characteristics = (homekit_characteristic_t *[]) { \
		BTN_NAME_PTR(n), BTN_ON_PTR(n), NULL })

// ---- 配件树 ----
homekit_accessory_t *accessories[] = {
	HOMEKIT_ACCESSORY(.id = 1, .category = homekit_accessory_category_switch, .services = (homekit_service_t *[]) {
		HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics = (homekit_characteristic_t *[]) {
			HOMEKIT_CHARACTERISTIC(NAME, DEVICE_NAME),
			HOMEKIT_CHARACTERISTIC(MANUFACTURER, "xiaowu-DIY"),
			HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "GK01IR0001"),
			HOMEKIT_CHARACTERISTIC(MODEL, "GK01"),
			HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "1.0.0"),
			HOMEKIT_CHARACTERISTIC(IDENTIFY, my_accessory_identify),
			NULL
		}),
		HOMEKIT_SERVICE(SWITCH, .primary = true, .characteristics = (homekit_characteristic_t *[]) {
			HOMEKIT_CHARACTERISTIC(NAME, "学习模式"),
			&cha_learn,
			NULL
		}),
		BTN_SERVICE(1),
		BTN_SERVICE(2),
		BTN_SERVICE(3),
		BTN_SERVICE(4),
		BTN_SERVICE(5),
		BTN_SERVICE(6),
		NULL
	}),
	NULL
};

homekit_server_config_t config = {
	.accessories = accessories,
	.password = HOMEKIT_PASSWORD,
	.setupId = HOMEKIT_SETUP_ID,
};

// ---- 供 main.cpp 调用的桥接实现 ----

void hk_set_button_name(int idx, const char *name) {
	if (idx < 0 || idx >= MAX_BUTTONS || name == 0) return;
	cha_btn_name[idx]->value = HOMEKIT_STRING((char *)name);
	homekit_characteristic_notify(cha_btn_name[idx], cha_btn_name[idx]->value);
}

void hk_notify_button_on(int idx) {
	if (idx < 0 || idx >= MAX_BUTTONS) return;
	cha_btn_on[idx]->value = HOMEKIT_BOOL(true);
	homekit_characteristic_notify(cha_btn_on[idx], cha_btn_on[idx]->value);
}

void hk_notify_button_off(int idx) {
	if (idx < 0 || idx >= MAX_BUTTONS) return;
	cha_btn_on[idx]->value = HOMEKIT_BOOL(false);
	homekit_characteristic_notify(cha_btn_on[idx], cha_btn_on[idx]->value);
}

void hk_set_learn_value(int on) {
	cha_learn.value = HOMEKIT_BOOL(on != 0);
	homekit_characteristic_notify(&cha_learn, cha_learn.value);
}
