#include <Arduino.h>
#include <SPI.h>

#include <FirmwareMSC.h>
#include <USB.h>
#include <USBHIDConsumerControl.h>
#include <USBHIDGamepad.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
#include <USBHIDSystemControl.h>
#include <USBHIDVendor.h>

#include "factory.h"
#include "board.h"
#include "storage.h"
#include "keyboard.h"
#include "mouse.h"
#include "tools.h"
#include "usb-keymap.h"


namespace DRIVERS {
	class BoardEsp32 : public Board {
		public:
			BoardEsp32() : Board(BOARD){
				pinMode(LED_BUILTIN, OUTPUT);
			}

			void reset() override {
			}

			void periodic() override {
				if (is_micros_timed_out(_prev_ts, 100000)) {
					switch(_state) {
						case 0:
							digitalWrite(LED_BUILTIN, LOW);
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


namespace DRIVERS {
	struct BackupRegister : public Storage {
		BackupRegister() : Storage(NON_VOLATILE_STORAGE) {
			// 		_rtc.enableClockInterface();
		}

		// void readBlock(void *dest, const void *src, size_t size) override {
		// 		uint8_t *dest_ = reinterpret_cast<uint8_t*>(dest);
		// 		for(size_t index = 0; index < size; ++index) {
		// 			dest_[index] = _rtc.getBackupRegister(reinterpret_cast<uintptr_t>(src)
		// + index + 1);
		// 		}
		// }

		// 	void updateBlock(const void *src, void *dest, size_t size) override {
		// 		const uint8_t *src_ = reinterpret_cast<const uint8_t*>(src);
		// 		for(size_t index = 0; index < size; ++index) {
		// 			_rtc.setBackupRegister(reinterpret_cast<uintptr_t>(dest) + index +
		// 1, src_[index]);
		// 		}
		// 	}

		// 	private:
		// 		STM32F1_RTC _rtc;
		// };
	};
}
// #	define CLS_IS_OFFLINE(_hid) \
// 		bool isOffline() override { \
// 			return false; \
// 		}
// #	define // CHECK_HID_EP

namespace DRIVERS {

	class UsbKeyboard : public Keyboard {
		public:
			UsbKeyboard() : Keyboard(USB_KEYBOARD) {}

			void begin() override {
				_kbd.begin();
			}

			void periodic() override {
	#			ifdef HID_USB_CHECK_ENDPOINT
				static unsigned long prev_ts = 0;
				if (is_micros_timed_out(prev_ts, 50000)) {
					static bool prev_online = true;
					bool online = !isOffline();
					if (!_sent || (online && !prev_online)) {
						_sendCurrent();
					}
					prev_online = online;
					prev_ts = micros();
				}
	#			endif
			}

			void clear() override {
				_kbd.releaseAll();
			}

			void sendKey(uint8_t code, bool state) override {
				uint8_t usb_code = keymapUsb(code);
				if (usb_code > 0) {
					// TODO: 
					if (state ? _kbd.press(usb_code) : _kbd.release(usb_code)) {
					
					}
				}
			}

			// CLS_IS_OFFLINE(_kbd)

			KeyboardLedsState getLeds() override {
				// TODO
				// uint8_t leds = _kbd.getLeds();
				uint8_t leds = 1;
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
			USBHIDKeyboard _kbd;
			bool _sent = true;
	};

	class UsbMouseAbsolute : public Mouse {
		public:
			UsbMouseAbsolute(type _type) : Mouse(_type) {}

			void begin() override {
				_mouse.begin();
			}

			void clear() override {
				// TODO:
				_mouse.release(0xFF);
			}

			// CLS_SEND_BUTTONS

			void sendMove(int x, int y) override {
				// // CHECK_HID_EP;
				// TODO:
				_mouse.move(x, y);
			}

			void sendWheel(int delta_y) override {
				// delta_x is not supported by hid-project now
				// // CHECK_HID_EP;
				_mouse.move(0, 0, delta_y);
			}

			// CLS_IS_OFFLINE(_mouse)

		private:
			USBHIDMouse _mouse;

			void _sendButton(uint8_t button, bool state) {
				// CHECK_HID_EP;
				if (state) _mouse.press(button);
				else _mouse.release(button);
			}
	};

	class UsbMouseRelative : public Mouse {
		public:
			UsbMouseRelative() : Mouse(USB_MOUSE_RELATIVE) {}

			void begin() override {
				_mouse.begin();
			}

			void clear() override {
				// TODO:
				_mouse.release(0xFF);
			}

			// CLS_SEND_BUTTONS

			void sendRelative(int x, int y) override {
				// CHECK_HID_EP;
				_mouse.move(x, y, 0);
			}

			void sendWheel(int delta_y) override {
				// delta_x is not supported by hid-project now
				// CHECK_HID_EP;
				_mouse.move(0, 0, delta_y);
			}

			// CLS_IS_OFFLINE(_mouse)

		private:
			USBHIDMouse _mouse;

			void _sendButton(uint8_t button, bool state) {
				// CHECK_HID_EP;
				if (state) _mouse.press(button);
				else _mouse.release(button);
			}
	};

	// #undef // CLS_SEND_BUTTONS
	// #undef CLS_IS_OFFLINE
	// #undef // CHECK_HID_EP
};

namespace DRIVERS {

	Keyboard *Factory::makeKeyboard(type _type) {
		switch (_type) {
#			ifdef HID_WITH_USB
			case USB_KEYBOARD:
				return new UsbKeyboard();
#			endif

			default:
				return new Keyboard(DUMMY);
		}
	}

	Mouse *Factory::makeMouse(type _type) {
		switch (_type) {
#			ifdef HID_WITH_USB
			case USB_MOUSE_ABSOLUTE:
				return new UsbMouseAbsolute(_type);
			case USB_MOUSE_RELATIVE:
				return new UsbMouseRelative();
#			endif
			default:
				return new Mouse(DUMMY);
		}
	}

	Storage* Factory::makeStorage(type _type) {
		switch (_type) {
			default:
				return new Storage(DUMMY);
        }
	}

	Board* Factory::makeBoard(type _type) {
		switch (_type) {
			default:
				return new Board(DUMMY);
        }
	}
};
