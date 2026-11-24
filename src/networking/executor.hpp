#pragma once

#include "../../VGP_Data_Exchange/C/Colfer.h"
#include "../settings/input_types.hpp"
#include "../simulation/gamepadSim.hpp"
#include "../simulation/keyboardSim.hpp"
#include "../simulation/mouseSim.hpp"

#include <memory>
#include <vector>


struct ParseResult
{
	enum class FailureReason
	{
		None,
		IncompleteData,
		SchemaMismatch,
		DataTooLarge
	};
	vgp_data_exchange_gamepad_reading reading;
	size_t bytes_consumed;
	bool success;
	FailureReason failure_reason = FailureReason::None;
};

ParseResult parse_gamepad_state(const char *data, size_t len);

// An executor Interface for handling gamepad state injection
class ExecutorInterface
{
  public:
	virtual ~ExecutorInterface() = default;

	virtual bool inject_gamepad_state(vgp_data_exchange_gamepad_reading const &reading) = 0;

	// Releases any key or mouse button still held down, e.g. when the device disconnects
	virtual void release_all() {}

	// The 1 to 4 controller slot this executor occupies, or -1 when it has none
	virtual int slot() const { return -1; }
};

class GamepadExecutor : public ExecutorInterface
{
  public:
	GamepadExecutor() = default;
	~GamepadExecutor() override = default;

	// Delete copy and move operations because GamepadInjector is non-movable
	GamepadExecutor(const GamepadExecutor &) = delete;
	GamepadExecutor &operator=(const GamepadExecutor &) = delete;
	GamepadExecutor(GamepadExecutor &&) = delete;
	GamepadExecutor &operator=(GamepadExecutor &&) = delete;

	bool inject_gamepad_state(vgp_data_exchange_gamepad_reading const &reading) override;

  private:
	GamepadInjector m_injector;
};

#ifdef _WIN32
struct _VIGEM_CLIENT_T;
struct _VIGEM_TARGET_T;

/**
 * @brief Presents the phone as a virtual Xbox 360 controller through the ViGEmBus driver.
 *
 * @details Each instance owns one controller, so every connected phone gets its own.
 * The constructor throws std::runtime_error with a readable message when the driver is
 * missing or no controller slot is free.
 */
class ViGEmExecutor : public ExecutorInterface
{
  public:
	// ds4 = a DualShock 4 for games that cannot see more than four Xbox controllers
	explicit ViGEmExecutor(bool ds4 = false, int slot = -1);
	~ViGEmExecutor() override;

	ViGEmExecutor(const ViGEmExecutor &) = delete;
	ViGEmExecutor &operator=(const ViGEmExecutor &) = delete;

	bool inject_gamepad_state(vgp_data_exchange_gamepad_reading const &reading) override;
	void release_all() override;
	int slot() const override;

  private:
	_VIGEM_CLIENT_T *m_client = nullptr;
	_VIGEM_TARGET_T *m_pad = nullptr;
	unsigned short m_buttons = 0;
	bool m_ds4 = false;
	int m_slot = -1;
	quint32 m_held = 0;
};
#endif

/**
 * @brief Executes gamepad input by converting it to keyboard and mouse actions.
 *
 * @details
 * This class takes gamepad input and converts it to equivalent keyboard and mouse
 * actions based on the active keymap profile. It handles button mappings,
 * thumbstick-to-mouse movement, and thumbstick-to-key mappings.
 */
class KeyboardMouseExecutor : public ExecutorInterface
{
  public:
	/**
	 * @brief Threshold for thumbstick input to register as button press.
	 */
	static constexpr double THRESHOLD = 0.5;

	KeyboardMouseExecutor();
	~KeyboardMouseExecutor() override { release_all(); }

	// Delete copy and move operations
	KeyboardMouseExecutor(const KeyboardMouseExecutor &) = delete;
	KeyboardMouseExecutor &operator=(const KeyboardMouseExecutor &) = delete;
	KeyboardMouseExecutor(KeyboardMouseExecutor &&) = delete;
	KeyboardMouseExecutor &operator=(KeyboardMouseExecutor &&) = delete;

	/**
	 * @brief Processes gamepad input and executes corresponding keyboard/mouse actions.
	 *
	 * @param reading The gamepad input data to process.
	 * @return true if the input was processed successfully, false otherwise.
	 */
	bool inject_gamepad_state(vgp_data_exchange_gamepad_reading const &reading) override;
	void release_all() override;

  private:
	std::unique_ptr<KeyboardInjector> m_keyboardInjector;
	std::unique_ptr<MouseInjector> m_mouseInjector;
	struct HeldInput
	{
		ButtonInput input;
		uintptr_t source;
	};
	std::vector<HeldInput> m_held;

	void handleButtonDown(const ButtonInput &buttonInput, uintptr_t source);
	void handleButtonUp(const ButtonInput &buttonInput, uintptr_t source);
	void handleThumbstickInput(const ThumbstickInput &thumbstick, float x_value, float y_value,
							   double threshold, uintptr_t sourceBase);
	void handleTriggerInput(const TriggerInput &trigger, float trigger_value, uintptr_t source);
};
