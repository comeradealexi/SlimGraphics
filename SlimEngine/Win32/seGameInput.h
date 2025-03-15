#pragma once
#include <stdint.h>
#include <unordered_map>
class IGameInput;
class IGameInputDevice;

namespace se
{
	class GameInput
	{
	public:
		GameInput();
		~GameInput();

		void Update();
		void SetKeyboard(IGameInputDevice* kb) { i_keyboard = kb; }
		bool IsKeyDown(wchar_t key) const;
		bool IsKeyUp(wchar_t key) const;
	private:
		struct KeyStatus
		{
			bool down = false; //If it's down
			bool key_pressed = false; //If it was down but is now up
			uint32_t frame_counter = 0;
		};
		std::unordered_map<uint16_t, KeyStatus> active_keys;
		uint32_t frame_counter = 0;
		IGameInput* igame_input = nullptr;
		IGameInputDevice* i_keyboard = nullptr;
	};
}