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
			se_game_input->SetKeyboard(device);
		}
	}
}

se::GameInput::GameInput()
{
	HRESULT hr = GameInputCreate(&igame_input);
	if (FAILED(hr))
	{
		seAssert(false, "GameInputCreate failed");
	}
	hr = igame_input->RegisterDeviceCallback(nullptr, GameInputKindKeyboard, GameInputDeviceAnyStatus, GameInputBlockingEnumeration, this, se::GameInputDeviceCallback, 0);
	if (FAILED(hr))
	{
		seAssert(false, "GameInputCreate failed");
	}
}

se::GameInput::~GameInput()
{

}

void se::GameInput::Update()
{
	if (i_keyboard)
	{
		IGameInputReading* gir = nullptr;
		HRESULT hr = igame_input->GetCurrentReading(GameInputKindKeyboard, nullptr, &gir);
		if (gir && SUCCEEDED(hr))
		{
			GameInputKeyState key_states[32];
			uint32_t states = gir->GetKeyState(32, key_states);
			for (uint32_t i = 0; i < states; i++)
			{
				seWriteLine("%i %i %c", key_states[i].scanCode, key_states[i].codePoint, key_states[i].virtualKey);
			}
		}
	}
}
