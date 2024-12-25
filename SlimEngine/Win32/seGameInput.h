#pragma once

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
	private:
		IGameInput* igame_input = nullptr;
		IGameInputDevice* i_keyboard = nullptr;
	};
}