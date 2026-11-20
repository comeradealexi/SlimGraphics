#pragma once
#include <stdint.h>
#include <unordered_map>
#include <set>
#include <mutex>
class IGameInput;
class IGameInputDevice;

namespace se
{
	class GameInput
	{
	public:
		GameInput();
		~GameInput();

		void update();
		void add_keyboard(IGameInputDevice* kb);
		void remove_keyboard(IGameInputDevice* kb);
		bool is_key_down(wchar_t key) const;
		bool is_key_up(wchar_t key) const;
	private:
		struct KeyStatus
		{
			bool down = false; //If it's down
			bool key_pressed = false; //If it was down but is now up
			uint32_t frame_counter = 0;
		};
		std::unordered_map<uint16_t, KeyStatus> active_keys;
		uint32_t frame_counter = 0;
		IGameInput* game_input = nullptr;
		uint64_t device_callback_token = 0;
		// Device callbacks arrive on a GameInput worker thread, so access to the keyboard set is guarded.
		std::mutex keyboards_mutex;
		std::set<IGameInputDevice*> keyboards;
	};
}