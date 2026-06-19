#pragma once

#include <Commons.h>
#include <d3dUtils.h>
#include <D3DRenderAdapter.h>
#include "Engine.h"

enum class RenderAPI
{
    None,
    DX12,
    Vulkan
};



class Application
{
protected:
    Application(HINSTANCE hInstance);
    Application(const Application& rhs) = delete;
    Application& operator=(const Application& rhs) = delete;
    virtual ~Application();
public:
    static Application* GetApp();

    HINSTANCE AppInst()const;
    HWND      MainWnd()const;
    float     AspectRatio()const;

    bool Get4xMsaaState()const;
    void Set4xMsaaState(bool value);

    int Run();

    Engine* GetEngine() { return mEngine.get(); }

    std::unique_ptr<IRenderAdapter> CreateRenderer(RenderAPI api);
    virtual bool Initialize();
    virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

protected:

    virtual void OnResize();
    virtual void Update(const FrameContext& context) = 0;
    virtual void PhysicsUpdate(const FrameContext& context) = 0;
    virtual void Draw(const FrameContext& context) = 0;

protected:

    bool InitMainWindow();


    void CalculateFrameStats();

protected:

    static Application* mApp;

    HINSTANCE mhAppInst = nullptr; // application instance handle
    HWND      mhMainWnd = nullptr; // main window handle
    bool      mAppPaused = false;  // is the application paused?
    bool      mMinimized = false;  // is the application minimized?
    bool      mMaximized = false;  // is the application maximized?
    bool      mResizing = false;   // are the resize bars being dragged?
    bool      mFullscreenState = false;// fullscreen enabled

    // Set true to use 4X MSAA (§4.1.8).  The default is false.
    bool      m4xMsaaState = false;    // 4X MSAA enabled
    UINT      m4xMsaaQuality = 0;      // quality level of 4X MSAA

    // Used to keep track of the “delta-time” and game time (§4.4).
    GameTimer mTimer;
	InputState mInput;
	FrameContext mFrameContext{ mTimer, mInput, 0.0f };

    
    UINT64 mCurrentFence = 0;
    static const int SwapChainBufferCount = 2;
    int mCurrBackBuffer = 0;

    UINT mRtvDescriptorSize = 0;
    UINT mDsvDescriptorSize = 0;
    UINT mCbvSrvUavDescriptorSize = 0;

    // Derived class should set these in derived constructor to customize starting values.
    std::wstring mMainWndCaption = L"LS Engine";
    int mClientWidth = 800;
    int mClientHeight = 600;

protected:
    std::unique_ptr<Engine> mEngine;
	std::unique_ptr<IRenderAdapter> mRenderAdapter;

	RenderAPI mRenderAPI = RenderAPI::DX12;
};

