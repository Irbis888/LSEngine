#include "Application.h"
#include <WindowsX.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;
using namespace std;


LRESULT CALLBACK
MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Forward hwnd on because we can get messages (e.g., WM_CREATE)
    // before CreateWindow returns, and thus before mhMainWnd is valid.
    return Application::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
}

Application* Application::mApp = nullptr;
Application* Application::GetApp()
{
    return mApp;
}

Application::Application(HINSTANCE hInstance)
    : mhAppInst(hInstance)
{


    // Only one D3DApp can be constructed.
    assert(mApp == nullptr);
    mApp = this;
}

Application::~Application()
{
   
}
HINSTANCE Application::AppInst() const
{
	return mhAppInst;
}

HWND Application::MainWnd()const
{
	return mhMainWnd;
}

float Application::AspectRatio()const
{
	return static_cast<float>(mClientWidth) / mClientHeight;
}

bool Application::Get4xMsaaState()const
{
	return m4xMsaaState;
}

void Application::Set4xMsaaState(bool value)
{
	if (m4xMsaaState != value)
	{
		m4xMsaaState = value;

		// Recreate the swapchain and buffers with new multisample settings.
		//CreateSwapChain();
		OnResize();
	}
}

std::unique_ptr<IRenderAdapter> Application::CreateRenderer(RenderAPI api)
{
	switch (api)
	{
	case RenderAPI::DX12:
		return std::make_unique<D3DRenderAdapter>();
	default:
		break;
	}
	return nullptr;
}



const float fixed_dt = 1.0f / 60.0f;

int Application::Run()
{
	MSG msg = { 0 };

	mTimer.Reset();
	float accumulator = 0.0f; // physics timer
	while (msg.message != WM_QUIT)
	{
		// If there are Window messages then process them.
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		// Otherwise, do animation/game stuff.
		else
		{
			mTimer.Tick();
			float dt = mTimer.DeltaTime();
			dt = min(dt, 0.1f);
			accumulator += dt;
			int steps = 0;
			const int maxSteps = 5;

			mFrameContext.physDT = fixed_dt;
			mFrameContext.timer = mTimer;
			mFrameContext.input = mInput;


			while (accumulator >= fixed_dt && steps < maxSteps)
			{
				PhysicsUpdate(mFrameContext);
				accumulator -= fixed_dt;
				steps++;
			}

			if (true)
			{
				if (GetAsyncKeyState('W') & 0x8000)
				{
					OnKeyPressed(mTimer, 'W');
				}
				if (GetAsyncKeyState('S') & 0x8000)
				{
					OnKeyPressed(mTimer, 'S');
				}
				if (GetAsyncKeyState('A') & 0x8000)
				{
					OnKeyPressed(mTimer, 'A');
				}
				if (GetAsyncKeyState('D') & 0x8000)
				{
					OnKeyPressed(mTimer, 'D');
				}
				if (GetAsyncKeyState('E') & 0x8000)
				{
					OnKeyPressed(mTimer, 'E');
				}
				if (GetAsyncKeyState('Q') & 0x8000)
				{
					OnKeyPressed(mTimer, 'Q');
				}
				//CalculateFrameStats();
				mRenderAdapter->BeginFrame();
				FrameContext context{ mTimer, InputState(), 0.0f };
				Update(mFrameContext);
				Draw(mFrameContext);
				mRenderAdapter->EndFrame();
			}
			else
			{
				Sleep(100);
			}
		}
	}

	return (int)msg.wParam;
}

bool Application::Initialize()
{
	if (!InitMainWindow())
		return false;
	mRenderAdapter = Application::CreateRenderer(mRenderAPI);
	mRenderAdapter->Init(mhMainWnd, mClientWidth, mClientHeight);

	mEngine = std::make_unique<Engine>(mRenderAdapter.get());
	mEngine->Init(mTimer);

	OnResize();

	return true;
}
void Application::OnResize()
{
	if (!mRenderAdapter)
		return;
	mRenderAdapter->OnResize(mClientWidth, mClientHeight);
}

LRESULT Application::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
		// WM_ACTIVATE is sent when the window is activated or deactivated.  
		// We pause the game when the window is deactivated and unpause it 
		// when it becomes active.  
	case WM_ACTIVATE:
		if (LOWORD(wParam) == WA_INACTIVE)
		{
			mAppPaused = true;
			mTimer.Stop();
		}
		else
		{
			mAppPaused = false;
			mTimer.Start();
		}
		return 0;

		// WM_SIZE is sent when the user resizes the window.  
	case WM_SIZE:
		// Save the new client area dimensions.
		mClientWidth = LOWORD(lParam);
		mClientHeight = HIWORD(lParam);
		if (true)
		{
			if (wParam == SIZE_MINIMIZED)
			{
				mAppPaused = true;
				mMinimized = true;
				mMaximized = false;
			}
			else if (wParam == SIZE_MAXIMIZED)
			{
				mAppPaused = false;
				mMinimized = false;
				mMaximized = true;
				OnResize();
			}
			else if (wParam == SIZE_RESTORED)
			{

				// Restoring from minimized state?
				if (mMinimized)
				{
					mAppPaused = false;
					mMinimized = false;
					OnResize();
				}

				// Restoring from maximized state?
				else if (mMaximized)
				{
					mAppPaused = false;
					mMaximized = false;
					OnResize();
				}
				else if (mResizing)
				{
					// If user is dragging the resize bars, we do not resize 
					// the buffers here because as the user continuously 
					// drags the resize bars, a stream of WM_SIZE messages are
					// sent to the window, and it would be pointless (and slow)
					// to resize for each WM_SIZE message received from dragging
					// the resize bars.  So instead, we reset after the user is 
					// done resizing the window and releases the resize bars, which 
					// sends a WM_EXITSIZEMOVE message.
				}
				else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
				{
					OnResize();
				}
			}
		}
		return 0;

		// WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
	case WM_ENTERSIZEMOVE:
		mAppPaused = true;
		mResizing = true;
		mTimer.Stop();
		return 0;

		// WM_EXITSIZEMOVE is sent when the user releases the resize bars.
		// Here we reset everything based on the new window dimensions.
	case WM_EXITSIZEMOVE:
		mAppPaused = false;
		mResizing = false;
		mTimer.Start();
		OnResize();
		return 0;

		// WM_DESTROY is sent when the window is being destroyed.
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

		// The WM_MENUCHAR message is sent when a menu is active and the user presses 
		// a key that does not correspond to any mnemonic or accelerator key. 
	case WM_MENUCHAR:
		// Don't beep when we alt-enter.
		return MAKELRESULT(0, MNC_CLOSE);

		// Catch this message so to prevent the window from becoming too small.
	case WM_GETMINMAXINFO:
		((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
		((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
		return 0;

	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_MOUSEMOVE:
		OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_KEYDOWN:
		if (!(lParam & 0x40000000)) {
			//OnKeyPressed(mTimer, wParam);
			if (!mInput.keys[wParam])
				mInput.keysPressed[wParam] = true;

			mInput.keys[wParam] = true;
		}
		return 0;
	case WM_MOUSEWHEEL:
		OnKeyPressed(mTimer, wParam);
		return 0;
	case WM_KEYUP:
		if (wParam == VK_ESCAPE)
		{
			PostQuitMessage(0);
		}
		else if ((int)wParam == VK_F2)
			Set4xMsaaState(!m4xMsaaState);
		else
		{
			mInput.keys[wParam] = false;
			mInput.keysReleased[wParam] = true;
			//break;
		}

		return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool Application::InitMainWindow()
{
	WNDCLASS wc;
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = MainWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = mhAppInst;
	wc.hIcon = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszMenuName = 0;
	wc.lpszClassName = L"MainWnd";

	if (!RegisterClass(&wc))
	{
		MessageBox(0, L"RegisterClass Failed.", 0, 0);
		return false;
	}

	// Compute window rectangle dimensions based on requested client area dimensions.
	RECT R = { 0, 0, mClientWidth, mClientHeight };
	AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
	int width = R.right - R.left;
	int height = R.bottom - R.top;

	mhMainWnd = CreateWindow(L"MainWnd", mMainWndCaption.c_str(),
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, 0, 0, mhAppInst, 0);
	if (!mhMainWnd)
	{
		MessageBox(0, L"CreateWindow Failed.", 0, 0);
		return false;
	}

	ShowWindow(mhMainWnd, SW_SHOW);
	UpdateWindow(mhMainWnd);

	return true;
}
