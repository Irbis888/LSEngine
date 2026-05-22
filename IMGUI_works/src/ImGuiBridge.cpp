#include "ImGuiBridge.h"

#include "ImGuiPlatform.h"
#include "Editor/EditorContext.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

#include <D3DRenderAdapter.h>
#include <Engine.h>
#include <World.h>

#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    HINSTANCE g_AppInst = nullptr;
    Engine* g_Engine = nullptr;
    HWND g_Hwnd = nullptr;
    D3DRenderAdapter* g_Adapter = nullptr;

    bool g_Licensed = false;
    bool g_Visible = false;
    bool g_Initialized = false;

    struct SrvAllocator
    {
        static constexpr UINT kFirstIndex = 900;
        static constexpr UINT kCount = 64;

        ID3D12Device* device = nullptr;
        ID3D12DescriptorHeap* heap = nullptr;
        UINT increment = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE heapStartCpu = {};
        D3D12_GPU_DESCRIPTOR_HANDLE heapStartGpu = {};
        std::vector<int> freeIndices;

        void Init(ID3D12Device* dev, ID3D12DescriptorHeap* srvHeap, UINT descriptorSize)
        {
            device = dev;
            heap = srvHeap;
            increment = descriptorSize;
            heapStartCpu = heap->GetCPUDescriptorHandleForHeapStart();
            heapStartGpu = heap->GetGPUDescriptorHandleForHeapStart();
            freeIndices.clear();
            for (int i = (int)kCount - 1; i >= 0; --i)
                freeIndices.push_back((int)kFirstIndex + i);
        }

        void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
        {
            IM_ASSERT(!freeIndices.empty());
            const int idx = freeIndices.back();
            freeIndices.pop_back();
            outCpu->ptr = heapStartCpu.ptr + (SIZE_T)idx * increment;
            outGpu->ptr = heapStartGpu.ptr + (SIZE_T)idx * increment;
        }

        void Free(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
        {
            const int cpuIdx = (int)((cpu.ptr - heapStartCpu.ptr) / increment);
            const int gpuIdx = (int)((gpu.ptr - heapStartGpu.ptr) / increment);
            IM_ASSERT(cpuIdx == gpuIdx);
            freeIndices.push_back(cpuIdx);
        }
    };

    SrvAllocator g_SrvAlloc;

    static void OnSrvAlloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
    {
        g_SrvAlloc.Alloc(outCpu, outGpu);
    }

    static void OnSrvFree(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
    {
        g_SrvAlloc.Free(cpu, gpu);
    }

    void BindEditorFromEngine()
    {
        if (!g_Engine)
            return;

        EditorContext ctx;
        ctx.engine = g_Engine;
        ctx.registry = &g_Engine->GetRegistry();
        ctx.resources = &g_Engine->GetResources();
        Editor_SetContext(&ctx);
    }

    void EnsureInitialized()
    {
        if (g_Initialized || !g_Licensed || !g_Adapter || !g_Hwnd)
            return;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        ImGui_ImplWin32_Init(g_Hwnd);

        const Dx12ImGuiBindings bindings = g_Adapter->GetImGuiBindings();
        g_SrvAlloc.Init(bindings.Device, bindings.SrvHeap, bindings.SrvDescriptorSize);

        ImGui_ImplDX12_InitInfo initInfo = {};
        initInfo.Device = bindings.Device;
        initInfo.CommandQueue = bindings.CommandQueue;
        initInfo.NumFramesInFlight = bindings.NumFramesInFlight;
        initInfo.RTVFormat = bindings.RtvFormat;
        initInfo.DSVFormat = bindings.DsvFormat;
        initInfo.SrvDescriptorHeap = bindings.SrvHeap;
        initInfo.SrvDescriptorAllocFn = OnSrvAlloc;
        initInfo.SrvDescriptorFreeFn = OnSrvFree;
        ImGui_ImplDX12_Init(&initInfo);

        BindEditorFromEngine();
        g_Initialized = true;
    }
}

void ImGuiBridge::SetAppInstance(HINSTANCE inst)
{
    g_AppInst = inst;
    g_Licensed = ImGuiPlatform::IsLicensed(inst);
}

void ImGuiBridge::SetEngine(Engine* engine)
{
    g_Engine = engine;
    if (g_Initialized)
        BindEditorFromEngine();
}

void ImGuiBridge::OnApplicationInit(HWND hwnd, D3DRenderAdapter* adapter)
{
    g_Hwnd = hwnd;
    g_Adapter = adapter;
    if (!g_AppInst)
        g_AppInst = (HINSTANCE)GetModuleHandle(nullptr);
    g_Licensed = ImGuiPlatform::IsLicensed(g_AppInst);
}

void ImGuiBridge::OnApplicationShutdown()
{
    if (!g_Initialized)
        return;

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_Initialized = false;
    g_Visible = false;
}

void ImGuiBridge::OnResize()
{
    if (!g_Initialized)
        return;
    ImGui_ImplDX12_InvalidateDeviceObjects();
    ImGui_ImplDX12_CreateDeviceObjects();
}

bool ImGuiBridge::WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!g_Licensed || !g_Initialized)
        return false;
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;
    return false;
}

void ImGuiBridge::ProcessInput(const InputState& input)
{
    if (!g_Licensed)
        return;

    if (ImGuiPlatform::IsTogglePressed(input))
    {
        g_Visible = !g_Visible;
        if (g_Visible)
            EnsureInitialized();
    }
}

bool ImGuiBridge::IsLicensed()
{
    return g_Licensed;
}

bool ImGuiBridge::IsVisible()
{
    return g_Licensed && g_Visible && g_Initialized;
}

bool ImGuiBridge::WantsCaptureInput()
{
    if (!IsVisible())
        return false;
    return Editor_WantsInputCapture();
}

void ImGuiBridge::BeginFrame(const FrameContext& context)
{
    if (!g_Licensed || !g_Visible)
        return;

    EnsureInitialized();
    if (!g_Initialized)
        return;

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    Editor_BeginFrame(context);
}

void ImGuiBridge::RenderEditorUI(const FrameContext& context)
{
    if (!IsVisible())
        return;

    Editor_RenderUI(context);
}

void ImGuiBridge::RenderOverlay(D3DRenderAdapter* adapter)
{
    if (!IsVisible() || !adapter)
        return;

    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || drawData->TotalVtxCount == 0)
        return;

    const Dx12ImGuiBindings bindings = adapter->GetImGuiBindings();
    ImGui_ImplDX12_RenderDrawData(drawData, bindings.CommandList);
}
