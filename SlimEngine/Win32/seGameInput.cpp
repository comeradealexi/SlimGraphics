#include "se_engine_pch.h"
#include "seGameInput.h"
#include "seEngine.h"
#include "GameInput.h"

// https://learn.microsoft.com/en-us/gaming/gdk/_content/gc/input/overviews/input-readings

namespace se
{
	static void GameInputDeviceCallback(
		GameInputCallbackToken callbackToken,
		void* context,
		IGameInputDevice* device,
		uint64_t timestamp,
		GameInputDeviceStatus currentStatus,
		GameInputDeviceStatus previousStatus)
	{
		se::GameInput* se_game_input = (se::GameInput*)context;
		const GameInputDeviceInfo* gidi = device->GetDeviceInfo();
		if ((gidi->supportedInput & GameInputKindKeyboard) != 0)
		{
			if ((currentStatus & GameInputDeviceConnected) != 0)
			{
				se_game_input->add_keyboard(device);
			}
			else
			{
				se_game_input->remove_keyboard(device);
			}
		}
	}
}

se::GameInput::GameInput()
{
	active_keys.reserve(256);
	HRESULT hr = GameInputCreate(&game_input);
	if (FAILED(hr))
	{
		seAssert(false, "GameInputCreate failed");
	}
	hr = game_input->RegisterDeviceCallback(nullptr, GameInputKindKeyboard, GameInputDeviceAnyStatus, GameInputBlockingEnumeration, this, se::GameInputDeviceCallback, nullptr);
	if (FAILED(hr))
	{
		seAssert(false, "GameInputCreate failed");
	}
}

se::GameInput::~GameInput()
{

}

void se::GameInput::update()
{
	frame_counter++;

	for (IGameInputDevice* keyboard : keyboards)
	{
		IGameInputReading* gir = nullptr;

		/* GetCurrentReading(GameInputKindKeyboard, nullptr, ...) returns the latest reading from whichever keyboard-capable device updated most recently, not a merged view. 
		Many mice, wireless receivers (Logitech/Razer dongles) and gaming keyboards expose a secondary keyboard HID interface. 
		When you move the mouse, that device can produce a newer keyboard-kind reading with zero keys down, and that’s the one you get back. 
		Your real keyboard’s reading is still there, you’re just not asking for it. */
		HRESULT hr = game_input->GetCurrentReading(GameInputKindKeyboard, keyboard, &gir);
		if (gir && SUCCEEDED(hr))
		{
			GameInputKeyState key_states[8];
			uint32_t states = gir->GetKeyState(8, key_states);
			for (uint32_t i = 0; i < states; i++)
			{
				auto& keySatus = active_keys[key_states[i].virtualKey];
				if (keySatus.down == false)
				{
					seWriteLine("Key Down: %i %i %c", key_states[i].scanCode, key_states[i].codePoint, key_states[i].virtualKey);
				}
				keySatus.down = true;
				keySatus.key_pressed = false;
				keySatus.frame_counter = frame_counter;
			}
			gir->Release();
		}
	}

	for (auto it = active_keys.begin(); it != active_keys.end();)
	{
		if (it->second.frame_counter != frame_counter)
		{
			it->second.down = false;
			if (it->second.key_pressed == false)
			{
				seWriteLine("Key Up: %c", it->first);
				it->second.key_pressed = true;
			}
			else
			{
				it = active_keys.erase(it);
				continue;
			}
		}
		it++;
	}
}

inline void se::GameInput::add_keyboard(IGameInputDevice* kb) 
{ 
	kb->AddRef(); 
	keyboards.insert(kb); 
}

inline void se::GameInput::remove_keyboard(IGameInputDevice* kb) 
{ 
	if (keyboards.erase(kb) != 0) 
	{ 
		kb->Release(); 
	} 
}

bool se::GameInput::is_key_down(wchar_t key) const
{
	auto it = active_keys.find(key);
	if (it != active_keys.end())
	{
		return it->second.down;
	}
	return false;
}

bool se::GameInput::is_key_up(wchar_t key) const
{
	auto it = active_keys.find(key);
	if (it != active_keys.end())
	{
		return it->second.key_pressed;
	}
	return false;
}
