#pragma once
#include "se_engine_pch.h"
//#include "CrossPlatformGFX\CrossPlatformGFX_Utils.h"
//#include "Input.h"
namespace se
{
	class Window
	{
	public:
		struct Sizes
		{
			RECT client_rect;
			RECT window_rect;
			RECT client_rect_to_screen;
		};

		Window(HINSTANCE hInstance, int nCmdShow, int iWidth, int iHeight/*, Gfx::Shared<Input> inputObject*/);
		~Window();
		void Poll();
		Sizes GetWindowSizes();

		HINSTANCE               g_hInst = nullptr;
		HWND                    g_hWnd = nullptr;


	};

}
