#include "executor.hpp"

#include "../settings/input_types.hpp"
#include "../settings/settings_singleton.hpp"
#include "../simulation/gamepadSim.hpp"
#include "../simulation/keyboardSim.hpp"
#include "../simulation/mouseSim.hpp"

#include <QApplication>
#include <QDebug>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <errno.h>
#include <sstream>
#include <string>
#include <vector>

/**
 * Converts a circular position (x,y with radius=1) to square coordinates.
 * This allows diagonal movement to reach (1,1) instead of (0.71,0.71).
 */
std::pair<float, float> circleToSquare(float x, float y)
{
	// Fast path for common cases
	if (x == 0.0f && y == 0.0f)
		return {0.0f, 0.0f};

	float magnitude = std::sqrt(x * x + y * y);
	if (magnitude == 0.0f)
		return {0.0f, 0.0f};

	// Normalize coordinates
	float nx = x / magnitude;
	float ny = y / magnitude;

	// Calculate scaling factor
	float scale = (std::abs(nx) > std::abs(ny)) ? (1.0f / std::abs(nx)) : (1.0f / std::abs(ny));

	// Apply magnitude limits and return
	float clampedMagnitude = std::min(magnitude, 1.0f);
	return {nx * scale * clampedMagnitude, ny * scale * clampedMagnitude};
}

// Helper function to convert button flags to readable names
std::string getButtonNames(uint32_t buttons)
{
	std::vector<std::string> buttonNames;

	if (buttons & GamepadButtons_Menu)
		buttonNames.emplace_back("Menu");
	if (buttons & GamepadButtons_View)
		buttonNames.emplace_back("View");
	if (buttons & GamepadButtons_A)
		buttonNames.emplace_back("A");
	if (buttons & GamepadButtons_B)
		buttonNames.emplace_back("B");
	if (buttons & GamepadButtons_X)
		buttonNames.emplace_back("X");
	if (buttons & GamepadButtons_Y)
		buttonNames.emplace_back("Y");
	if (buttons & GamepadButtons_DPadUp)
		buttonNames.emplace_back("DPadUp");
	if (buttons & GamepadButtons_DPadDown)
		buttonNames.emplace_back("DPadDown");
	if (buttons & GamepadButtons_DPadLeft)
		buttonNames.emplace_back("DPadLeft");
	if (buttons & GamepadButtons_DPadRight)
		buttonNames.emplace_back("DPadRight");
	if (buttons & GamepadButtons_LeftShoulder)
		buttonNames.emplace_back("LeftShoulder");
	if (buttons & GamepadButtons_RightShoulder)
		buttonNames.emplace_back("RightShoulder");
	if (buttons & GamepadButtons_LeftThumbstick)
		buttonNames.emplace_back("LeftThumbstick");
	if (buttons & GamepadButtons_RightThumbstick)
		buttonNames.emplace_back("RightThumbstick");

	if (buttonNames.empty())
	{
		return "None";
	}

	std::ostringstream oss;
	for (size_t i = 0; i < buttonNames.size(); ++i)
	{
		if (i > 0)
			oss << " | ";
		oss << buttonNames[i];
	}

	return oss.str();
}

ParseResult parse_gamepad_state(const char *data, size_t len)
{
	ParseResult result;
	result.reading.buttons_up = 0;
	result.reading.buttons_down = 0;
	result.reading.left_trigger = 0;
	result.reading.right_trigger = 0;
	result.reading.left_thumbstick_x = 0;
	result.reading.left_thumbstick_y = 0;
	result.reading.right_thumbstick_x = 0;
	result.reading.right_thumbstick_y = 0;
	result.bytes_consumed = 0;
	result.success = false;

	// Deserialize the data
	size_t decoded_octects =
		vgp_data_exchange_gamepad_reading_unmarshal(&result.reading, data, len);

	// When the return is zero then errno is set to one of the following 3 values:
	// EWOULDBLOCK on incomplete data, EFBIG on a breach of either colfer_size_max
	// or colfer_list_max and EILSEQ on schema mismatch.
	if (decoded_octects == 0)
	{
		if (errno == EWOULDBLOCK)
		{
			result.failure_reason = ParseResult::FailureReason::IncompleteData;
		}
		else if (errno == EFBIG)
		{
			result.failure_reason = ParseResult::FailureReason::DataTooLarge;
			qWarning() << "Data too large to process";
		}
		else if (errno == EILSEQ)
		{
			result.failure_reason = ParseResult::FailureReason::SchemaMismatch;
			qWarning() << "Schema mismatch detected";
		}
		else
		{
			qWarning() << "Unknown error occurred during deserialization";
		}
		return result;
	}

	result.bytes_consumed = decoded_octects;
	result.success = true;

#ifdef QT_DEBUG
	// Log the gamepad state
	qDebug() << "Gamepad state:"
			 << "\nButtons up: " << getButtonNames(result.reading.buttons_up).c_str()
			 << "\nButtons down: " << getButtonNames(result.reading.buttons_down).c_str()
			 << "\nLeft trigger: " << result.reading.left_trigger
			 << "\nRight trigger: " << result.reading.right_trigger
			 << "\nLeft thumbstick x: " << result.reading.left_thumbstick_x
			 << "\nLeft thumbstick y: " << result.reading.left_thumbstick_y
			 << "\nRight thumbstick x: " << result.reading.right_thumbstick_x
			 << "\nRight thumbstick y: " << result.reading.right_thumbstick_y;
#endif

	return result;
}

KeyboardMouseExecutor::KeyboardMouseExecutor()
{
	try
	{
		m_keyboardInjector = std::make_unique<KeyboardInjector>();
		m_mouseInjector = std::make_unique<MouseInjector>();
	}
	catch (const std::exception &e)
	{
		qCritical() << "Failed to initialize input injectors:" << e.what();
		throw;
	}
}

void KeyboardMouseExecutor::release_all()
{
	const auto held = m_held;
	for (const HeldInput &entry : held)
		handleButtonUp(entry.input, entry.source);
}

void KeyboardMouseExecutor::handleButtonDown(const ButtonInput &buttonInput, uintptr_t source)
{
	// Several controls can map to the same key, so a key is only pressed by its first holder
	// and only released once every control holding it has let go
	const auto sameInput = [&](const HeldInput &held) {
		return held.input.vk == buttonInput.vk &&
			   held.input.is_mouse_button == buttonInput.is_mouse_button;
	};
	if (std::any_of(m_held.begin(), m_held.end(), [&](const HeldInput &held) {
			return sameInput(held) && held.source == source;
		}))
		return;
	const bool alreadyDown = std::any_of(m_held.begin(), m_held.end(), sameInput);
	m_held.push_back({buttonInput, source});
	if (alreadyDown)
		return;

	if (buttonInput.is_mouse_button)
	{
#ifdef WIN32
		if (buttonInput.vk == VK_LBUTTON)
			m_mouseInjector->leftDown();
		else if (buttonInput.vk == VK_RBUTTON)
			m_mouseInjector->rightDown();
		else if (buttonInput.vk == VK_MBUTTON)
			m_mouseInjector->middleDown();
#elif defined(__linux__)
		if (buttonInput.vk == BTN_LEFT)
			m_mouseInjector->leftDown();
		else if (buttonInput.vk == BTN_RIGHT)
			m_mouseInjector->rightDown();
		else if (buttonInput.vk == BTN_MIDDLE)
			m_mouseInjector->middleDown();
#endif
		else [[unlikely]] // Unknown mouse button, do nothing
			qWarning() << "Unknown mouse button pressed: " << buttonInput.vk;
	}
	else
	{
		m_keyboardInjector->keyDown(buttonInput.vk);
	}
}

void KeyboardMouseExecutor::handleButtonUp(const ButtonInput &buttonInput, uintptr_t source)
{
	const auto sameInput = [&](const HeldInput &held) {
		return held.input.vk == buttonInput.vk &&
			   held.input.is_mouse_button == buttonInput.is_mouse_button;
	};
	const auto held = std::find_if(m_held.begin(), m_held.end(), [&](const HeldInput &entry) {
		return sameInput(entry) && entry.source == source;
	});
	if (held == m_held.end())
		return;
	m_held.erase(held);
	if (std::any_of(m_held.begin(), m_held.end(), sameInput))
		return;

	if (buttonInput.is_mouse_button)
	{
#ifdef WIN32
		if (buttonInput.vk == VK_LBUTTON)
			m_mouseInjector->leftUp();
		else if (buttonInput.vk == VK_RBUTTON)
			m_mouseInjector->rightUp();
		else if (buttonInput.vk == VK_MBUTTON)
			m_mouseInjector->middleUp();
#elif defined(__linux__)
		if (buttonInput.vk == BTN_LEFT)
			m_mouseInjector->leftUp();
		else if (buttonInput.vk == BTN_RIGHT)
			m_mouseInjector->rightUp();
		else if (buttonInput.vk == BTN_MIDDLE)
			m_mouseInjector->middleUp();
#endif
		else [[unlikely]] // Unknown mouse button, do nothing
			qWarning() << "Unknown mouse button released: " << buttonInput.vk;
	}
	else
	{
		m_keyboardInjector->keyUp(buttonInput.vk);
	}
}

void KeyboardMouseExecutor::handleThumbstickInput(const ThumbstickInput &thumbstick, float x_value,
												  float y_value, double threshold,
												  uintptr_t sourceBase)
{
	// Convert circular area to square area
	auto [squareX, squareY] = circleToSquare(x_value, y_value);

	if (thumbstick.is_mouse_move)
	{
		// Mouse movement code
		int offsetX = squareX * SettingsSingleton::instance().mouseSensitivity();
		int offsetY = squareY * SettingsSingleton::instance().mouseSensitivity();

		// Only move if offset is above threshold
		if (double thresholdPixels = threshold * SettingsSingleton::instance().mouseSensitivity();
			std::abs(offsetX) < thresholdPixels && std::abs(offsetY) < thresholdPixels)
			return;

		// qDebug() << "Moving mouse by offset:" << offsetX << "," << offsetY;

		// Break movement into steps for smooth movement
		int maxSteps = std::max(std::abs(offsetX), std::abs(offsetY));
		if (maxSteps > 0)
		{
			for (int step = 1; step <= maxSteps; ++step)
			{
				int stepX = (offsetX * step) / maxSteps - (offsetX * (step - 1)) / maxSteps;
				int stepY = (offsetY * step) / maxSteps - (offsetY * (step - 1)) / maxSteps;
				// qDebug() << "Moving mouse by step:" << stepX << "," << stepY;
				m_mouseInjector->moveMouseByOffset(stepX, stepY);
			}
		}
	}
	else
	{
		// Direction handling
		if (x_value > threshold)
			handleButtonDown(thumbstick.right, sourceBase + 0);
		else if (x_value < -threshold)
			handleButtonDown(thumbstick.left, sourceBase + 1);
		else
		{
			handleButtonUp(thumbstick.right, sourceBase + 0);
			handleButtonUp(thumbstick.left, sourceBase + 1);
		}

		if (y_value > threshold)
			handleButtonDown(thumbstick.down, sourceBase + 2);
		else if (y_value < -threshold)
			handleButtonDown(thumbstick.up, sourceBase + 3);
		else
		{
			handleButtonUp(thumbstick.down, sourceBase + 2);
			handleButtonUp(thumbstick.up, sourceBase + 3);
		}
	}
}

void KeyboardMouseExecutor::handleTriggerInput(const TriggerInput &trigger, float trigger_value,
												uintptr_t source)
{
	if (trigger_value >= trigger.threshold)
	{
		handleButtonDown(trigger.button_input, source);
	}
	else
	{
		handleButtonUp(trigger.button_input, source);
	}
}

bool KeyboardMouseExecutor::inject_gamepad_state(vgp_data_exchange_gamepad_reading const &reading)
{
	// Handle button input using active keymap profile
	const auto &profile = SettingsSingleton::instance().activeKeymapProfile();
	static const std::vector<GamepadButtons> buttons = {GamepadButtons_Menu,
														GamepadButtons_View,
														GamepadButtons_A,
														GamepadButtons_B,
														GamepadButtons_X,
														GamepadButtons_Y,
														GamepadButtons_DPadUp,
														GamepadButtons_DPadDown,
														GamepadButtons_DPadLeft,
														GamepadButtons_DPadRight,
														GamepadButtons_LeftShoulder,
														GamepadButtons_RightShoulder,
														GamepadButtons_LeftThumbstick,
														GamepadButtons_RightThumbstick};
	for (auto button : buttons)
	{
		InputKeyCode vk = profile.buttonMap(button);
		if (vk == 0)
			continue;
		ButtonInput input{vk, is_mouse_button(vk)};
		if (reading.buttons_down & button)
			handleButtonDown(input, static_cast<uintptr_t>(button));
		if (reading.buttons_up & button)
			handleButtonUp(input, static_cast<uintptr_t>(button));
	}

	handleThumbstickInput(
		SettingsSingleton::instance().activeKeymapProfile().thumbstickInput(Thumbstick_Left),
		reading.left_thumbstick_x, reading.left_thumbstick_y, THRESHOLD, 1000);

	handleThumbstickInput(
		SettingsSingleton::instance().activeKeymapProfile().thumbstickInput(Thumbstick_Right),
		reading.right_thumbstick_x, reading.right_thumbstick_y, THRESHOLD, 2000);

	handleTriggerInput(
		SettingsSingleton::instance().activeKeymapProfile().triggerInput(Trigger::Left),
		reading.left_trigger, 3000);

	handleTriggerInput(
		SettingsSingleton::instance().activeKeymapProfile().triggerInput(Trigger::Right),
		reading.right_trigger, 3001);

	return true;
}

bool GamepadExecutor::inject_gamepad_state(vgp_data_exchange_gamepad_reading const &reading)
{
#ifdef _WIN32
	using enum winrt::Windows::Gaming::Input::GamepadButtons;
	// Create a new state to update thumbsticks and triggers
	InjectedInputGamepadInfo newState;
	newState.Buttons(None);

	// Set thumbstick values, invert Y-axis
	newState.LeftThumbstickX(reading.left_thumbstick_x);
	newState.LeftThumbstickY(-reading.left_thumbstick_y);
	newState.RightThumbstickX(reading.right_thumbstick_x);
	newState.RightThumbstickY(-reading.right_thumbstick_y);

	// Set trigger values
	newState.LeftTrigger(reading.left_trigger);
	newState.RightTrigger(reading.right_trigger);

	// Update the gamepad state
	m_injector.update(newState);

	// Then call the button handling logic, because it affects the state

	// Handle button presses (mapping from our buttons to WinRT buttons)
	if (reading.buttons_down & GamepadButtons_Menu)
		m_injector.pressButton(Menu);
	if (reading.buttons_down & GamepadButtons_View)
		m_injector.pressButton(View);
	if (reading.buttons_down & GamepadButtons_A)
		m_injector.pressButton(A);
	if (reading.buttons_down & GamepadButtons_B)
		m_injector.pressButton(B);
	if (reading.buttons_down & GamepadButtons_X)
		m_injector.pressButton(X);
	if (reading.buttons_down & GamepadButtons_Y)
		m_injector.pressButton(Y);
	if (reading.buttons_down & GamepadButtons_DPadUp)
		m_injector.pressButton(DPadUp);
	if (reading.buttons_down & GamepadButtons_DPadDown)
		m_injector.pressButton(DPadDown);
	if (reading.buttons_down & GamepadButtons_DPadLeft)
		m_injector.pressButton(DPadLeft);
	if (reading.buttons_down & GamepadButtons_DPadRight)
		m_injector.pressButton(DPadRight);
	if (reading.buttons_down & GamepadButtons_LeftShoulder)
		m_injector.pressButton(LeftShoulder);
	if (reading.buttons_down & GamepadButtons_RightShoulder)
		m_injector.pressButton(RightShoulder);
	if (reading.buttons_down & GamepadButtons_LeftThumbstick)
		m_injector.pressButton(LeftThumbstick);
	if (reading.buttons_down & GamepadButtons_RightThumbstick)
		m_injector.pressButton(RightThumbstick);

	// Handle button releases
	if (reading.buttons_up & GamepadButtons_Menu)
		m_injector.releaseButton(Menu);
	if (reading.buttons_up & GamepadButtons_View)
		m_injector.releaseButton(View);
	if (reading.buttons_up & GamepadButtons_A)
		m_injector.releaseButton(A);
	if (reading.buttons_up & GamepadButtons_B)
		m_injector.releaseButton(B);
	if (reading.buttons_up & GamepadButtons_X)
		m_injector.releaseButton(X);
	if (reading.buttons_up & GamepadButtons_Y)
		m_injector.releaseButton(Y);
	if (reading.buttons_up & GamepadButtons_DPadUp)
		m_injector.releaseButton(DPadUp);
	if (reading.buttons_up & GamepadButtons_DPadDown)
		m_injector.releaseButton(DPadDown);
	if (reading.buttons_up & GamepadButtons_DPadLeft)
		m_injector.releaseButton(DPadLeft);
	if (reading.buttons_up & GamepadButtons_DPadRight)
		m_injector.releaseButton(DPadRight);
	if (reading.buttons_up & GamepadButtons_LeftShoulder)
		m_injector.releaseButton(LeftShoulder);
	if (reading.buttons_up & GamepadButtons_RightShoulder)
		m_injector.releaseButton(RightShoulder);
	if (reading.buttons_up & GamepadButtons_LeftThumbstick)
		m_injector.releaseButton(LeftThumbstick);
	if (reading.buttons_up & GamepadButtons_RightThumbstick)
		m_injector.releaseButton(RightThumbstick);

#elif defined(__linux__)
	// Linux implementation using libevdev

	// Set thumbstick and trigger values
	m_injector.setThumbsticks(reading.left_thumbstick_x, -reading.left_thumbstick_y,
							  reading.right_thumbstick_x, -reading.right_thumbstick_y);
	m_injector.setTriggers(reading.left_trigger, reading.right_trigger);

	// Handle button presses (mapping from our buttons to Linux input codes)
	if (reading.buttons_down & GamepadButtons_Menu)
		m_injector.pressButton(BTN_START);
	if (reading.buttons_down & GamepadButtons_View)
		m_injector.pressButton(BTN_SELECT);
	if (reading.buttons_down & GamepadButtons_A)
		m_injector.pressButton(BTN_A);
	if (reading.buttons_down & GamepadButtons_B)
		m_injector.pressButton(BTN_B);
	if (reading.buttons_down & GamepadButtons_X)
		m_injector.pressButton(BTN_X);
	if (reading.buttons_down & GamepadButtons_Y)
		m_injector.pressButton(BTN_Y);
	if (reading.buttons_down & GamepadButtons_DPadUp)
		m_injector.pressButton(BTN_DPAD_UP);
	if (reading.buttons_down & GamepadButtons_DPadDown)
		m_injector.pressButton(BTN_DPAD_DOWN);
	if (reading.buttons_down & GamepadButtons_DPadLeft)
		m_injector.pressButton(BTN_DPAD_LEFT);
	if (reading.buttons_down & GamepadButtons_DPadRight)
		m_injector.pressButton(BTN_DPAD_RIGHT);
	if (reading.buttons_down & GamepadButtons_LeftShoulder)
		m_injector.pressButton(BTN_TL);
	if (reading.buttons_down & GamepadButtons_RightShoulder)
		m_injector.pressButton(BTN_TR);
	if (reading.buttons_down & GamepadButtons_LeftThumbstick)
		m_injector.pressButton(BTN_THUMBL);
	if (reading.buttons_down & GamepadButtons_RightThumbstick)
		m_injector.pressButton(BTN_THUMBR);

	// Handle button releases
	if (reading.buttons_up & GamepadButtons_Menu)
		m_injector.releaseButton(BTN_START);
	if (reading.buttons_up & GamepadButtons_View)
		m_injector.releaseButton(BTN_SELECT);
	if (reading.buttons_up & GamepadButtons_A)
		m_injector.releaseButton(BTN_A);
	if (reading.buttons_up & GamepadButtons_B)
		m_injector.releaseButton(BTN_B);
	if (reading.buttons_up & GamepadButtons_X)
		m_injector.releaseButton(BTN_X);
	if (reading.buttons_up & GamepadButtons_Y)
		m_injector.releaseButton(BTN_Y);
	if (reading.buttons_up & GamepadButtons_DPadUp)
		m_injector.releaseButton(BTN_DPAD_UP);
	if (reading.buttons_up & GamepadButtons_DPadDown)
		m_injector.releaseButton(BTN_DPAD_DOWN);
	if (reading.buttons_up & GamepadButtons_DPadLeft)
		m_injector.releaseButton(BTN_DPAD_LEFT);
	if (reading.buttons_up & GamepadButtons_DPadRight)
		m_injector.releaseButton(BTN_DPAD_RIGHT);
	if (reading.buttons_up & GamepadButtons_LeftShoulder)
		m_injector.releaseButton(BTN_TL);
	if (reading.buttons_up & GamepadButtons_RightShoulder)
		m_injector.releaseButton(BTN_TR);
	if (reading.buttons_up & GamepadButtons_LeftThumbstick)
		m_injector.releaseButton(BTN_THUMBL);
	if (reading.buttons_up & GamepadButtons_RightThumbstick)
		m_injector.releaseButton(BTN_THUMBR);
#endif

	// Inject the current state
	m_injector.inject();

	return true;
}

#ifdef _WIN32
#include <ViGEm/Client.h>
#include <windows.h>
#include <cstdio>
#include <stdexcept>

// The XInput number (0 to 3) a freshly plugged virtual Xbox pad really has, or -1 when it cannot be told
static int probeXInputIndex(PVIGEM_CLIENT client, PVIGEM_TARGET pad)
{
	using XInputGetStateFn = DWORD(WINAPI *)(DWORD, void *);
	static const HMODULE lib = LoadLibraryW(L"xinput1_4.dll");
	if (lib == nullptr)
		return -1;
	const auto getState = reinterpret_cast<XInputGetStateFn>(GetProcAddress(lib, "XInputGetState"));
	if (getState == nullptr)
		return -1;
	struct State
	{
		DWORD packet;
		WORD buttons;
		BYTE leftTrigger, rightTrigger;
		SHORT lx, ly, rx, ry;
	};
	constexpr SHORT marker = 12345;
	XUSB_REPORT report{};
	int found = -1;
	for (int tries = 0; tries < 20 && found < 0; ++tries)
	{
		report.sThumbLX = marker;
		vigem_target_x360_update(client, pad, report);
		for (DWORD i = 0; i < 4 && found < 0; ++i)
		{
			State state{};
			if (getState(i, &state) == ERROR_SUCCESS && state.lx == marker)
				found = static_cast<int>(i);
		}
		if (found < 0)
			QThread::msleep(25);
	}
	report.sThumbLX = 0;
	vigem_target_x360_update(client, pad, report);
	return found;
}

static VOID CALLBACK x360RumbleCallback(PVIGEM_CLIENT, PVIGEM_TARGET, UCHAR large, UCHAR weak, UCHAR, LPVOID user)
{
	static_cast<ViGEmExecutor *>(user)->emitRumble(large, weak);
}

static VOID CALLBACK ds4RumbleCallback(PVIGEM_CLIENT, PVIGEM_TARGET, UCHAR large, UCHAR weak, DS4_LIGHTBAR_COLOR, LPVOID user)
{
	static_cast<ViGEmExecutor *>(user)->emitRumble(large, weak);
}

void ViGEmExecutor::setRumbleCallback(std::function<void(unsigned char, unsigned char)> callback)
{
	std::lock_guard<std::mutex> lock(m_rumbleMutex);
	m_rumble = std::move(callback);
}

void ViGEmExecutor::emitRumble(unsigned char large, unsigned char weak)
{
	std::function<void(unsigned char, unsigned char)> callback;
	{
		std::lock_guard<std::mutex> lock(m_rumbleMutex);
		callback = m_rumble;
	}
	if (callback)
		callback(large, weak);
}

ViGEmExecutor::ViGEmExecutor(bool ds4, int slot, int expectedIndex) : m_ds4(ds4), m_slot(slot)
{
	m_client = vigem_alloc();
	if (m_client == nullptr)
		throw std::runtime_error("Could not allocate a ViGEm client.");

	const VIGEM_ERROR connectResult = vigem_connect(m_client);
	if (!VIGEM_SUCCESS(connectResult))
	{
		vigem_free(m_client);
		m_client = nullptr;
		if (connectResult == VIGEM_ERROR_BUS_NOT_FOUND)
			throw std::runtime_error(
				"The ViGEmBus driver is not installed. Install it from "
				"github.com/nefarius/ViGEmBus/releases and try again.");
		char code[64];
		snprintf(code, sizeof(code), "Could not connect to the ViGEmBus driver (error 0x%08X).",
				 static_cast<unsigned>(connectResult));
		throw std::runtime_error(code);
	}

	m_pad = m_ds4 ? vigem_target_ds4_alloc() : vigem_target_x360_alloc();
	const VIGEM_ERROR addResult =
		m_pad != nullptr ? vigem_target_add(m_client, m_pad) : VIGEM_ERROR_INVALID_TARGET;
	if (!VIGEM_SUCCESS(addResult))
	{
		if (m_pad != nullptr)
			vigem_target_free(m_pad);
		m_pad = nullptr;
		vigem_disconnect(m_client);
		vigem_free(m_client);
		m_client = nullptr;
		if (addResult == VIGEM_ERROR_NO_FREE_SLOT)
			throw std::runtime_error("No free virtual controller slot.");
		char code[64];
		snprintf(code, sizeof(code), "Could not create a virtual controller (error 0x%08X).",
				 static_cast<unsigned>(addResult));
		throw std::runtime_error(code);
	}
	qInfo() << (m_ds4 ? "ViGEm virtual DualShock 4 created" : "ViGEm virtual Xbox 360 controller created");

	if (!m_ds4)
	{
		// ViGEm numbers its own pads, but games use the number Windows' XInput gives, and a leftover pad can
		// shift it. So find the real XInput number by sending a recognisable value and seeing where it appears.
		for (int attempt = 0;; ++attempt)
		{
			ULONG busIndex = 0;
			const bool haveBus = VIGEM_SUCCESS(vigem_target_x360_get_user_index(m_client, m_pad, &busIndex));
			const int probed = probeXInputIndex(m_client, m_pad);
			m_userIndex = probed >= 0 ? probed : (haveBus ? static_cast<int>(busIndex) : -1);
			qInfo() << "Virtual Xbox pad is XInput controller" << m_userIndex + 1 << "(ViGEm says"
					<< (haveBus ? static_cast<int>(busIndex) + 1 : -1) << ")";
			if (expectedIndex < 0 || m_userIndex < 0 || m_userIndex == expectedIndex || attempt >= 3)
				break;
			qInfo() << "Controller" << expectedIndex + 1 << "was expected, plugging the pad in again";
			vigem_target_remove(m_client, m_pad);
			vigem_target_free(m_pad);
			m_pad = nullptr;
			// Windows frees the old number a moment after the pad is removed
			QThread::msleep(250);
			m_pad = vigem_target_x360_alloc();
			if (m_pad == nullptr || !VIGEM_SUCCESS(vigem_target_add(m_client, m_pad)))
			{
				if (m_pad != nullptr)
					vigem_target_free(m_pad);
				m_pad = nullptr;
				vigem_disconnect(m_client);
				vigem_free(m_client);
				m_client = nullptr;
				throw std::runtime_error("Could not recreate the virtual controller.");
			}
		}
	}

	// A game rumbling this pad reaches the phone that plays it
	if (m_ds4)
		vigem_target_ds4_register_notification(m_client, m_pad, ds4RumbleCallback, this);
	else
		vigem_target_x360_register_notification(m_client, m_pad, x360RumbleCallback, this);
}

ViGEmExecutor::~ViGEmExecutor()
{
	if (m_client != nullptr)
	{
		if (m_pad != nullptr)
		{
			if (m_ds4)
				vigem_target_ds4_unregister_notification(m_pad);
			else
				vigem_target_x360_unregister_notification(m_pad);
			vigem_target_remove(m_client, m_pad);
			vigem_target_free(m_pad);
		}
		vigem_disconnect(m_client);
		vigem_free(m_client);
	}
}

static SHORT toStickAxis(float value, bool invert)
{
	const float clamped = std::clamp(invert ? -value : value, -1.0f, 1.0f);
	return static_cast<SHORT>(clamped * 32767.0f);
}

static BYTE toDs4Axis(float value)
{
	return static_cast<BYTE>(std::lround((std::clamp(value, -1.0f, 1.0f) + 1.0f) * 127.5f));
}

static DS4_DPAD_DIRECTIONS ds4Hat(quint32 held)
{
	bool up = held & GamepadButtons_DPadUp, down = held & GamepadButtons_DPadDown;
	bool left = held & GamepadButtons_DPadLeft, right = held & GamepadButtons_DPadRight;
	if (up && down)
		up = down = false;
	if (left && right)
		left = right = false;
	if (up && right)
		return DS4_BUTTON_DPAD_NORTHEAST;
	if (down && right)
		return DS4_BUTTON_DPAD_SOUTHEAST;
	if (down && left)
		return DS4_BUTTON_DPAD_SOUTHWEST;
	if (up && left)
		return DS4_BUTTON_DPAD_NORTHWEST;
	if (up)
		return DS4_BUTTON_DPAD_NORTH;
	if (right)
		return DS4_BUTTON_DPAD_EAST;
	if (down)
		return DS4_BUTTON_DPAD_SOUTH;
	if (left)
		return DS4_BUTTON_DPAD_WEST;
	return DS4_BUTTON_DPAD_NONE;
}

bool ViGEmExecutor::inject_gamepad_state(vgp_data_exchange_gamepad_reading const &reading)
{
	m_held |= reading.buttons_down;
	m_held &= ~reading.buttons_up;

	if (m_ds4)
	{
		DS4_REPORT report;
		DS4_REPORT_INIT(&report);
		struct Mapping
		{
			uint32_t button;
			unsigned short bit;
		};
		static const Mapping mappings[] = {
			{GamepadButtons_A, DS4_BUTTON_CROSS},
			{GamepadButtons_B, DS4_BUTTON_CIRCLE},
			{GamepadButtons_X, DS4_BUTTON_SQUARE},
			{GamepadButtons_Y, DS4_BUTTON_TRIANGLE},
			{GamepadButtons_Menu, DS4_BUTTON_OPTIONS},
			{GamepadButtons_View, DS4_BUTTON_SHARE},
			{GamepadButtons_LeftShoulder, DS4_BUTTON_SHOULDER_LEFT},
			{GamepadButtons_RightShoulder, DS4_BUTTON_SHOULDER_RIGHT},
			{GamepadButtons_LeftThumbstick, DS4_BUTTON_THUMB_LEFT},
			{GamepadButtons_RightThumbstick, DS4_BUTTON_THUMB_RIGHT},
		};
		for (const Mapping &mapping : mappings)
		{
			if (m_held & mapping.button)
				report.wButtons |= mapping.bit;
		}
		if (reading.left_trigger > 0.5f)
			report.wButtons |= DS4_BUTTON_TRIGGER_LEFT;
		if (reading.right_trigger > 0.5f)
			report.wButtons |= DS4_BUTTON_TRIGGER_RIGHT;
		DS4_SET_DPAD(&report, ds4Hat(m_held));
		// Both the phone and a DualShock 4 report Y growing downwards
		report.bThumbLX = toDs4Axis(reading.left_thumbstick_x);
		report.bThumbLY = toDs4Axis(reading.left_thumbstick_y);
		report.bThumbRX = toDs4Axis(reading.right_thumbstick_x);
		report.bThumbRY = toDs4Axis(reading.right_thumbstick_y);
		report.bTriggerL = static_cast<BYTE>(std::clamp(reading.left_trigger, 0.0f, 1.0f) * 255.0f);
		report.bTriggerR = static_cast<BYTE>(std::clamp(reading.right_trigger, 0.0f, 1.0f) * 255.0f);
		return VIGEM_SUCCESS(vigem_target_ds4_update(m_client, m_pad, report));
	}

	struct Mapping
	{
		uint32_t button;
		unsigned short bit;
	};
	static const Mapping mappings[] = {
		{GamepadButtons_Menu, XUSB_GAMEPAD_START},
		{GamepadButtons_View, XUSB_GAMEPAD_BACK},
		{GamepadButtons_A, XUSB_GAMEPAD_A},
		{GamepadButtons_B, XUSB_GAMEPAD_B},
		{GamepadButtons_X, XUSB_GAMEPAD_X},
		{GamepadButtons_Y, XUSB_GAMEPAD_Y},
		{GamepadButtons_DPadUp, XUSB_GAMEPAD_DPAD_UP},
		{GamepadButtons_DPadDown, XUSB_GAMEPAD_DPAD_DOWN},
		{GamepadButtons_DPadLeft, XUSB_GAMEPAD_DPAD_LEFT},
		{GamepadButtons_DPadRight, XUSB_GAMEPAD_DPAD_RIGHT},
		{GamepadButtons_LeftShoulder, XUSB_GAMEPAD_LEFT_SHOULDER},
		{GamepadButtons_RightShoulder, XUSB_GAMEPAD_RIGHT_SHOULDER},
		{GamepadButtons_LeftThumbstick, XUSB_GAMEPAD_LEFT_THUMB},
		{GamepadButtons_RightThumbstick, XUSB_GAMEPAD_RIGHT_THUMB},
	};
	for (const Mapping &mapping : mappings)
	{
		if (reading.buttons_down & mapping.button)
			m_buttons |= mapping.bit;
		if (reading.buttons_up & mapping.button)
			m_buttons &= static_cast<unsigned short>(~mapping.bit);
	}

	XUSB_REPORT report{};
	report.wButtons = m_buttons;
	report.bLeftTrigger =
		static_cast<BYTE>(std::clamp(reading.left_trigger, 0.0f, 1.0f) * 255.0f);
	report.bRightTrigger =
		static_cast<BYTE>(std::clamp(reading.right_trigger, 0.0f, 1.0f) * 255.0f);
	// The phone reports Y growing downwards, XInput expects it growing upwards
	report.sThumbLX = toStickAxis(reading.left_thumbstick_x, false);
	report.sThumbLY = toStickAxis(reading.left_thumbstick_y, true);
	report.sThumbRX = toStickAxis(reading.right_thumbstick_x, false);
	report.sThumbRY = toStickAxis(reading.right_thumbstick_y, true);
	return VIGEM_SUCCESS(vigem_target_x360_update(m_client, m_pad, report));
}

int ViGEmExecutor::slot() const
{
	if (m_ds4)
		return m_slot;
	return m_userIndex >= 0 ? m_userIndex + 1 : -1;
}

void ViGEmExecutor::release_all()
{
	m_buttons = 0;
	m_held = 0;
	if (m_client == nullptr || m_pad == nullptr)
		return;
	if (m_ds4)
	{
		DS4_REPORT report;
		DS4_REPORT_INIT(&report);
		vigem_target_ds4_update(m_client, m_pad, report);
	}
	else
	{
		vigem_target_x360_update(m_client, m_pad, XUSB_REPORT{});
	}
}
#endif
