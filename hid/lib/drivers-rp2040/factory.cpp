#include <Arduino.h>
#include <SPI.h>
#include <EEPROM.h>

#include "Adafruit_TinyUSB.h"

#include "factory.h"
#include "board.h"
#include "storage.h"
#include "keyboard.h"
#include "mouse.h"
#include "tools.h"
#include "usb-keymap.h"

#define LOG(...) Serial.printf(__VA_ARGS__);

namespace DRIVERS { // HIDWRAPPER
	// Report ID
	enum
	{
		RID_KEYBOARD = 1,
		RID_MOUSE,
		RID_CONSUMER_CONTROL, // Media, volume etc ..
	};

	// HID report descriptor using TinyUSB's template
	uint8_t const desc_hid_report[] =
	{
		TUD_HID_REPORT_DESC_KEYBOARD( HID_REPORT_ID(RID_KEYBOARD) ),
		TUD_HID_REPORT_DESC_MOUSE   ( HID_REPORT_ID(RID_MOUSE) ),
		TUD_HID_REPORT_DESC_CONSUMER( HID_REPORT_ID(RID_CONSUMER_CONTROL) )
	};

	// USB HID object. For ESP32 these values cannot be changed after this declaration
	// desc report, desc len, protocol, interval, use out endpoint
	// Thus, the HID report descriptor must be defined at compile time regardless of which endpoints are used.
	

	class HidWrapper {
		public:
			HidWrapper(){
				usbHid = new Adafruit_USBD_HID(desc_hid_report, sizeof(desc_hid_report), HID_ITF_PROTOCOL_NONE, 2, false);
			}

			void begin() {
				if (_init) {
					return;
				}
				_init = true;

				usbHid->begin();
				LOG("hid init");
				while( !TinyUSBDevice.mounted() ) delay(1);
				LOG("hid init done");
			}

			Adafruit_USBD_HID* usbHid;
		
		private:
			bool _init = false;
	};
}

namespace DRIVERS { // BOARD
	class BoardRp2040 : public Board {
		public:
			BoardRp2040() : Board(BOARD){
				pinMode(LED_BUILTIN, OUTPUT);
			}

			void reset() override {
			}

			void periodic() override {
				if (is_micros_timed_out(_prev_ts, 100000)) {
					switch(_state) {
						case 0:
							digitalWrite(LED_BUILTIN, HIGH);
							break;
						case 2:
							if(_rx_data) {
								_rx_data = false;
								digitalWrite(LED_BUILTIN, LOW);
							}
							break;
						case 4:
							if(_keyboard_online) {
								_keyboard_online = false;
								digitalWrite(LED_BUILTIN, LOW);
							}
							break;
						case 8:
							if(_mouse_online) {
								_mouse_online = false;
								digitalWrite(LED_BUILTIN, LOW);
							}
							break;
						case 1:	// heartbeat off
						case 3:	// _rx_data off
						case 7: // _keyboard_online off
						case 11: // _mouse_online off
							digitalWrite(LED_BUILTIN, HIGH);
							break;
						case 19:
							_state = -1;
							break;
					}
					++_state;
					_prev_ts = micros();
				}
			}

			void updateStatus(status status) override {
				switch (status) {
					case RX_DATA:
						_rx_data = true;
						break;
					case KEYBOARD_ONLINE:
						_keyboard_online = true;
						break;
					case MOUSE_ONLINE:
						_mouse_online = true;
						break;
				}
			}

		private:
			unsigned long _prev_ts = 0;
			uint8_t _state = 0;
			bool _rx_data = false;
			bool _keyboard_online = false;
			bool _mouse_online = false;
	};
}

namespace DRIVERS { // EEPROM
	struct Eeprom : public Storage {
		Eeprom() : Storage(NON_VOLATILE_STORAGE) {
			EEPROM.begin(512);
		}

		void readBlock(void *dest, const void* src_addr, size_t size) override {
				uint8_t *dest_ = reinterpret_cast<uint8_t*>(dest);
				for(size_t index = 0; index < size; ++index) {
					dest_[index] = EEPROM.read(reinterpret_cast<uintptr_t>(src_addr) + index);
				}
		}

		void updateBlock(const void *src, void* dest_addr, size_t size) override {
			const uint8_t *src_ = reinterpret_cast<const uint8_t*>(src);
			for(size_t index = 0; index < size; ++index) {
				EEPROM.write(reinterpret_cast<uintptr_t>(dest_addr) + index, src_[index]);
			}
			EEPROM.commit();
		}
	};
}

namespace DRIVERS {

	class UsbKeyboard : public Keyboard {
		public:
			UsbKeyboard(HidWrapper& _hidWrapper) : Keyboard(DRIVERS::USB_KEYBOARD), _kbd(_hidWrapper.usbHid) {
				LOG("init keyboard");
				_hidWrapper.begin();
			}

			void begin() override {
				LOG("keyboard begin");
			}

			void periodic() override {
	#			ifdef HID_USB_CHECK_ENDPOINT
				static unsigned long prev_ts = 0;
				if (is_micros_timed_out(prev_ts, 50000)) {
					static bool prev_online = true;
					bool online = !isOffline();
					if online:
						LOG("keyboard periodic");
					if (!_sent || (online && !prev_online)) {
						_sendCurrent();
					}
					prev_online = online;
					prev_ts = micros();
				}
	#			endif
			}

			void clear() override {
				_kbd->keyboardRelease(RID_KEYBOARD);
			}

			void sendKey(uint8_t code, bool state) override {
				uint8_t usb_code = keymapUsb(code);
				LOG("print key");
				if (usb_code > 0) {
					// TODO: 
					if (state ? _kbd->keyboardPress(RID_KEYBOARD, usb_code) : _kbd->keyboardRelease(RID_KEYBOARD)) {
					
					}
				}
			}

			bool isOffline() override {
				// if (TinyUSBDevice.mounted()) {
				// 	LOG("keyboard online");
				// }
				// else {
				// 	LOG("keyboard offline");
				// }
				return !TinyUSBDevice.mounted();
			}

			KeyboardLedsState getLeds() override {
				// TODO
				// uint8_t leds = _kbd.getLeds();
				uint8_t leds = 0;
				uint8_t LED_CAPS_LOCK = 1;
				uint8_t LED_SCROLL_LOCK = 1;
				uint8_t LED_NUM_LOCK = 1;

				KeyboardLedsState result = {
					.caps = leds & LED_CAPS_LOCK,
					.scroll = leds & LED_SCROLL_LOCK,
					.num = leds & LED_NUM_LOCK,
				};
				return result;
			}

		private:
			Adafruit_USBD_HID* _kbd;
			bool _sent = true;
	};

	class UsbMouseAbsolute : public Mouse {
		public:
			UsbMouseAbsolute(HidWrapper& _hidWrapper) : Mouse(DRIVERS::USB_MOUSE_ABSOLUTE), _mouse(_hidWrapper.usbHid) {
				_hidWrapper.begin();
			}

			void begin() override {
				LOG("mouse begin absolute");
			}

			void clear() override {
				// TODO:
				_mouse->mouseButtonRelease(RID_MOUSE);
			}

			void sendMove(uint8_t x, uint8_t y) {
				// TODO: absolute location is not correct on guest, no idea why
				// LOG("%x\r\n", x);
				// LOG("%x\r\n", y);
				// int8_t ux = int8_t((x & 0xFF00) >> 9);
				// int8_t uy = int8_t((y & 0xFF00) >> 9);
				// LOG("%x\r\n", ux);
				// LOG("%x\r\n", uy);
				_mouse->mouseMove(RID_MOUSE, x, y);
			}

			void sendWheel(int delta_y) override {
				// delta_x is not supported by hid-project now
				_mouse->mouseScroll(RID_MOUSE, delta_y, 0);
			}

			bool isOffline() override {
				return !TinyUSBDevice.mounted();
			}

		private:
			Adafruit_USBD_HID* _mouse;
	};

	class UsbMouseRelative : public Mouse {
		public:
			UsbMouseRelative(HidWrapper& _hidWrapper) : Mouse(USB_MOUSE_RELATIVE), _mouse(_hidWrapper.usbHid){
				_hidWrapper.begin();
			}


			void begin() override {
				_mouse->begin();
			}

			void clear() override {
				_mouse->mouseButtonRelease(RID_MOUSE);
			}

			void sendMove(int x, int y) override {
				LOG("%x\r\n", x);
				LOG("%x\r\n", y);
				int8_t ux = int8_t((x & 0xFF00) >> 9);
				int8_t uy = int8_t((y & 0xFF00) >> 9);
				LOG("%x\r\n", ux);
				LOG("%x\r\n", uy);
				_mouse->mouseMove(RID_MOUSE, ux, uy);
			}

			void sendWheel(int delta_y) override {
				_mouse->mouseScroll(RID_MOUSE, delta_y, 0);
			}

			bool isOffline() override {
				return !TinyUSBDevice.mounted();
			}

		private:
			Adafruit_USBD_HID* _mouse;
	};
};

namespace DRIVERS {
	HidWrapper _hidWrapper;

	Keyboard *Factory::makeKeyboard(type _type) {
		switch (_type) {
#			ifdef HID_WITH_USB
			case USB_KEYBOARD:
				return new UsbKeyboard(_hidWrapper);
#			endif
			default:
				return new Keyboard(DUMMY);
		}
	}

	Mouse *Factory::makeMouse(type _type) {
		switch (_type) {
#			ifdef HID_WITH_USB
			case USB_MOUSE_ABSOLUTE:
				return new UsbMouseAbsolute(_hidWrapper);
			case USB_MOUSE_RELATIVE:
				return new UsbMouseRelative(_hidWrapper);
#			endif
			default:
				return new Mouse(DUMMY);
		}
	}

	Storage* Factory::makeStorage(type _type) {
		switch (_type) {
			case NON_VOLATILE_STORAGE:
				return new Eeprom();
			default:
				return new Storage(DUMMY);
		}
	}

	Board* Factory::makeBoard(type _type) {
		// usb_hid(desc_hid_report, sizeof(desc_hid_report), HID_ITF_PROTOCOL_NONE, 2, false);
		switch (_type) {
			case BOARD:
				return new BoardRp2040();
			default:
				return new Board(DUMMY);
		}
	}
};
