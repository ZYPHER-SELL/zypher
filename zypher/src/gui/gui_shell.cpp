#include "ZypherUI.h"
#include "gui_runtime.h"

#include <atomic>

static ID3D11Device*           g_pd3dDevice           = nullptr;
static ID3D11DeviceContext*    g_pd3dDeviceContext    = nullptr;
static IDXGISwapChain*         g_pSwapChain           = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

static std::atomic<bool> g_quitRequested{ false };

static constexpr int kWindowWidth  = 480;
static constexpr int kWindowHeight = 620;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();

namespace gui {

void RequestQuit() {
    g_quitRequested.store(true);
    if (Memory::hwnd) {

        ::PostMessageW(Memory::hwnd, WM_NULL, 0, 0);
    }
}

void RunWindowThread() {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const HINSTANCE hInstance = ::GetModuleHandleW(nullptr);

    const int cxLarge = ::GetSystemMetrics(SM_CXICON);
    const int cyLarge = ::GetSystemMetrics(SM_CYICON);
    const int cxSmall = ::GetSystemMetrics(SM_CXSMICON);
    const int cySmall = ::GetSystemMetrics(SM_CYSMICON);
    HICON hIconLarge = (HICON)::LoadImageW(
        hInstance, MAKEINTRESOURCEW(100), IMAGE_ICON,
        cxLarge, cyLarge, LR_DEFAULTCOLOR | LR_SHARED);
    HICON hIconSmall = (HICON)::LoadImageW(
        hInstance, MAKEINTRESOURCEW(100), IMAGE_ICON,
        cxSmall, cySmall, LR_DEFAULTCOLOR | LR_SHARED);

    WNDCLASSEXW wc = {
        sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance,
        hIconLarge, nullptr, nullptr, nullptr,
        L"zypher_loader", hIconSmall
    };
    ::RegisterClassExW(&wc);

    const int screenW = ::GetSystemMetrics(SM_CXSCREEN);
    const int screenH = ::GetSystemMetrics(SM_CYSCREEN);
    const int x       = (screenW - kWindowWidth)  / 2;
    const int y       = (screenH - kWindowHeight) / 2;

    HWND hwnd = ::CreateWindowW(
        wc.lpszClassName, L"zypher",
        WS_POPUP | WS_MINIMIZEBOX,
        x, y, kWindowWidth, kWindowHeight,
        nullptr, nullptr, wc.hInstance, nullptr
    );
    if (!hwnd) {
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        gui_runtime::SignalWindowReady(false);
        return;
    }

    #ifndef DWMWA_WINDOW_CORNER_PREFERENCE
    #define DWMWA_WINDOW_CORNER_PREFERENCE 33
    #endif
    {
        const DWORD round = 2;
        ::DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                                 &round, sizeof(round));
    }

    if (hIconLarge) {
        ::SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hIconLarge);
    }
    if (hIconSmall) {
        ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
    }

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::DestroyWindow(hwnd);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        gui_runtime::SignalWindowReady(false);
        return;
    }

    Memory::hwnd                 = hwnd;
    Overlay::g_pd3dDevice        = g_pd3dDevice;
    Overlay::g_pd3dDeviceContext = g_pd3dDeviceContext;

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    Render::Fonts::Init();
    Style::Update();
    gui::Init();

    gui_runtime::SignalWindowReady(true);

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done || g_quitRequested.load()) break;

        Input::Poll();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        gui::Render();

        ImGui::Render();
        const float clearColor[4] = { 0.04f, 0.04f, 0.06f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    gui::Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    Memory::hwnd                 = nullptr;
    Overlay::g_pd3dDevice        = nullptr;
    Overlay::g_pd3dDeviceContext = nullptr;

    gui_runtime::SignalWindowClosed();
}

}

static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount                          = 2;
    sd.BufferDesc.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator     = 60;
    sd.BufferDesc.RefreshRate.Denominator   = 1;
    sd.Flags                                = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                          = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                         = hWnd;
    sd.SampleDesc.Count                     = 1;
    sd.Windowed                             = TRUE;
    sd.SwapEffect                           = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
        &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
            &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
        if (FAILED(hr)) return false;
    }
    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain)        { g_pSwapChain->Release();        g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice)        { g_pd3dDevice->Release();        g_pd3dDevice = nullptr; }
}

static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

static constexpr int kCaptionSidebarW   = 76;
static constexpr int kCaptionTopBarH    = 52;
static constexpr int kCaptionButtonsW   = 36 * 2;
static constexpr int kCaptionSignOutW   = 96 + 12;

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 1;

    Input::OnWndProc(msg, wParam, lParam);

    switch (msg) {
    case WM_NCHITTEST: {
        POINT pt = { (LONG)(short)LOWORD(lParam),
                     (LONG)(short)HIWORD(lParam) };
        ::ScreenToClient(hWnd, &pt);
        RECT rc;
        ::GetClientRect(hWnd, &rc);
        if (pt.y >= 0 && pt.y < kCaptionTopBarH &&
            pt.x >= kCaptionSidebarW &&
            pt.x <  rc.right - kCaptionButtonsW - kCaptionSignOutW) {
            return HTCAPTION;
        }
        break;
    }
    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
