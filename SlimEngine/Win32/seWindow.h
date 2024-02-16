#pragma once
#include "se_engine_pch.h"
//#include "CrossPlatformGFX\CrossPlatformGFX_Utils.h"
//#include "Input.h"
namespace se
{
	class Window
	{
	public:
		Window(HINSTANCE hInstance, int nCmdShow, int iWidth, int iHeight/*, Gfx::Shared<Input> inputObject*/);
		~Window();
		void Poll();

		HINSTANCE               g_hInst = nullptr;
		HWND                    g_hWnd = nullptr;
	};

}
