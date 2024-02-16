#include "se_engine_pch.h"
#include "seWindow.h"
//#include "Input.h"
//#include "CrossPlatformGFX\CrossPlatformGFX_Utils.h"
#include "imgui.h"
//extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#include <sstream>

namespace se
{
	//Gfx::Shared<Input> g_inputObject;
	std::chrono::high_resolution_clock::time_point g_mouseDownTime;
	LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		PAINTSTRUCT ps;
		HDC hdc;

		//ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam);

		switch (message)
		{
		case WM_PAINT:
			hdc = BeginPaint(hWnd, &ps);
			EndPaint(hWnd, &ps);
			break;

		case WM_DESTROY:
			PostQuitMessage(0);
			exit(0); //So bad Hack
			break;

			// Note that this tutorial does not handle resizing (WM_SIZE) requests,
			// so we created the window without the resize border.
		case WM_KEYDOWN:
		{
			if (!ImGui::GetIO().WantCaptureKeyboard)
			{
				//OutputDebugStringA("WM_KEYDOWN\n");
				//switch (wParam)
				//{
				//case VK_LEFT: OutputDebugStringA("VK_LEFT\n"); break;
				//case VK_RIGHT: OutputDebugStringA("VK_RIGHT\n"); break;
				//case VK_UP: OutputDebugStringA("VK_UP\n"); break;
				//case VK_DOWN: OutputDebugStringA("VK_DOWN\n"); break;
				//case VK_HOME: OutputDebugStringA("VK_HOME\n"); break;
				//case VK_END: OutputDebugStringA("VK_END\n"); break;
				//case VK_INSERT: OutputDebugStringA("VK_INSERT\n"); break;
				//case VK_DELETE: OutputDebugStringA("VK_DELETE\n"); break;
				//case VK_SHIFT: OutputDebugStringA("VK_SHIFT\n"); break;
				//case VK_RETURN: OutputDebugStringA("VK_RETURN\n"); break;
				//case VK_BACK: OutputDebugStringA("VK_BACK\n"); break;
				//case VK_TAB: OutputDebugStringA("VK_TAB\n"); break;
				//case VK_ESCAPE: OutputDebugStringA("VK_ESCAPE\n"); break;
				//
				//default:
				//	OutputDebugStringA("OTHER\n");
				//}

				//g_inputObject->UpdateKeyState(wParam, true);
			}
			break;
		}
		case WM_LBUTTONUP:
		{
			if (!ImGui::GetIO().WantCaptureMouse)
			{
				auto timeDiff = std::chrono::high_resolution_clock::now() - g_mouseDownTime;
				int height = 0;
				RECT rect;
				if (GetClientRect(hWnd, &rect))
				{
					height = rect.bottom - rect.top;
				}
				auto xPos = GET_X_LPARAM(lParam);
				auto yPos = height - GET_Y_LPARAM(lParam); //Y is bottom instead of top (windows treats y=0 as top)

				//if (timeDiff < std::chrono::milliseconds(800))
				//	g_inputObject->SetCursorClicked(true); //Don't mark it as a recognised click unless mouse was clicked in small amount of time.

				//g_inputObject->SetCursorPosition(xPos, yPos);
				//g_inputObject->SetCursorDown(false);
				ReleaseCapture();
			}
			break;
		}

		case WM_LBUTTONDOWN:
		{
			if (!ImGui::GetIO().WantCaptureMouse)
			{
				g_mouseDownTime = std::chrono::high_resolution_clock::now();
				//g_inputObject->SetCursorDown(true);
				SetCapture(hWnd);
			}
			break;
		}

		case WM_KEYUP:
		{
			if (!ImGui::GetIO().WantCaptureKeyboard)
			{
				//OutputDebugStringA("WM_KEYUP\n");
				//g_inputObject->UpdateKeyState(wParam, false);
			}
			break;
		}

		case WM_CHAR:
		{
			if (!ImGui::GetIO().WantCaptureKeyboard)
			{
				wchar_t wChars[2] = {};
				wChars[0] = wParam;
				OutputDebugStringA("\'");
				OutputDebugStringW(wChars);
				OutputDebugStringA("\' = WM_CHAR\n");
				//g_inputObject->AddTextInput(wParam);
			}
			break;
		}

		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}

		return 0;
	}

	Window::Window(HINSTANCE hInstance, int nCmdShow, int iWidth, int iHeight/*, Gfx::Shared<Input> inputObject*/)
	{
		//g_inputObject = inputObject;
		WNDCLASSEX wcex = {};
		wcex.cbSize = sizeof(WNDCLASSEX);
		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc = WndProc;
		wcex.cbClsExtra = 0;
		wcex.cbWndExtra = 0;
		wcex.hInstance = hInstance;
		//wcex.hIcon = LoadIcon(hInstance, (LPCTSTR)IDI_TUTORIAL1);
		//wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wcex.lpszMenuName = nullptr;
		wcex.lpszClassName = L"SimpleWindow";
		//wcex.hIconSm = LoadIcon(wcex.hInstance, (LPCTSTR)IDI_TUTORIAL1);
		if (!RegisterClassEx(&wcex))
			return;

		// Create window
		g_hInst = hInstance;
		RECT rc = { 0, 0, iWidth, iHeight };
		AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
		g_hWnd = CreateWindow(L"SimpleWindow", L"Alexander Houghton",
			WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
			CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInstance,
			nullptr);
		if (!g_hWnd)
			return;

		ShowWindow(g_hWnd, nCmdShow);
	}


	Window::~Window()
	{
		//g_inputObject = nullptr;
	}

	void Window::Poll()
	{
		//g_inputObject->SetCursorClicked(false); //Will be marked as click in the message translating below. This just resets it per frame.

		int width = 0;
		int height = 0;
		RECT rect;
		if (GetClientRect(g_hWnd, &rect))
		{
			height = rect.bottom - rect.top;
			width = rect.right - rect.left;
		}

		POINT p;
		if (GetCursorPos(&p))
		{
			ScreenToClient(g_hWnd, &p);

			p.y = height - p.y;

			p.y = std::max<long>(p.y, 0);
			p.y = std::min<long>(p.y, height);

			p.x = std::max<long>(p.x, 0);
			p.x = std::min<long>(p.x, width);

			//g_inputObject->SetCursorPosition(p.x, p.y);

			//std::stringstream ss;
			//ss << p.x;
			//ss << " ";
			//ss << p.y << "\n";
			//OutputDebugStringA(ss.str().c_str());
		}
		else
		{
			//g_inputObject->SetCursorPosition(0, 0);
		}

		// Main message loop
		MSG msg = { 0 };
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}