#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <dxgi.h>
#include <d3d11.h>
#include <d3d10_1.h>
#include <d2d1.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d10.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "WindowsCodecs.lib")

#define STBI_NO_PIC
#define STBI_NO_HDR
#define STBI_NO_PNM

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#undef STB_IMAGE_IMPLEMENTATION

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize.h>
#undef STB_IMAGE_RESIZE_IMPLEMENTATION

#include <filesystem>

#include "UIWindow.h"

class hrresult_error : std::runtime_error
{
    static std::string ErrorDescription(HRESULT hr)
    {
        if (FACILITY_WINDOWS == HRESULT_FACILITY(hr)) {
            hr = HRESULT_CODE(hr);
        }

        char *szErrMsg = nullptr;
        if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                           NULL, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                           (LPSTR)&szErrMsg, 0, NULL) == 0) {
            return std::string("Could not find a description for error code ") + std::to_string(hr);
        }
        std::string err = szErrMsg;
        LocalFree(szErrMsg);
        return err;
    }

public:
    hrresult_error(HRESULT hr) : std::runtime_error(ErrorDescription(hr)) {}
};

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr)) {
#ifdef _DEBUG
        __debugbreak();
#endif // _DEBUG

        throw new hrresult_error(hr);
    }
}

UIWindow ui;

ComPtr<ID3D11Device>          d3dDevice;
ComPtr<ID3D11DeviceContext>   d3dContext;
ComPtr<IDXGISwapChain>        d3dSwapChain;
ComPtr<ID3D11RenderTargetView> d3dRTView;

ComPtr<ID3D10Device1>          d3d10Device;

// Direct2D Resources
ComPtr<ID2D1Factory>           d2dFactory;
ComPtr<ID2D1RenderTarget>      d2dRenderTarget;

void CreateDeviceIndependentResources()
{
    // Create Direct2D factory.
    ThrowIfFailed(
        D2D1CreateFactory<ID2D1Factory>(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory)
    );
}

void CreateRenderTarget()
{
    ComPtr<ID3D11Texture2D> pBackBuffer;
    d3dSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    d3dDevice->CreateRenderTargetView(pBackBuffer.Get(), nullptr, &d3dRTView);
}

bool CreateDeviceResources(HWND hWnd)
{
    ComPtr<ID3D10Device> d3d10dev;

    D3D10CreateDevice(nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr, 0, D3D10_SDK_VERSION, &d3d10dev);
    d3d10dev.As(&d3d10Device);

    DXGI_SWAP_CHAIN_DESC sd{ 0 };
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapchain;

    UINT createDeviceFlags = 0;

    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                      createDeviceFlags, featureLevels, 2, D3D11_SDK_VERSION, &sd,
                                      &d3dSwapChain, &d3dDevice, &featureLevel, &d3dContext) != S_OK) {
        return false;
    }

    CreateRenderTarget();

    return true;
}

ID3D11ShaderResourceView *CreateTexture(const uint8_t *data, int width, int height)
{
    // Create texture
    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    ID3D11Texture2D *pTexture = nullptr;
    D3D11_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = data;
    subResource.SysMemPitch = desc.Width * 4;
    subResource.SysMemSlicePitch = 0;
    d3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);

    ID3D11ShaderResourceView *out_srv;

    // Create texture view
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    d3dDevice->CreateShaderResourceView(pTexture, &srvDesc, &out_srv);

    pTexture->Release();

    return out_srv;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (d3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            d3dSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    LoadDebugPrivilege();

    // Create application window
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, TEXT("ImGui Example"), nullptr };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindow(
        wc.lpszClassName, TEXT("SatisfactoryWebMap"),
        WS_OVERLAPPED | WS_SYSMENU | WS_MINIMIZEBOX,
        100, 100, ui.settings.width, ui.settings.height,
        nullptr, nullptr, wc.hInstance, nullptr);

    try {
        CreateDeviceIndependentResources();

        // Initialize Direct3D
        if (!CreateDeviceResources(hwnd)) {
            ::UnregisterClass(wc.lpszClassName, wc.hInstance);
            return 1;
        }
    } catch (const std::runtime_error &ex) {
        MessageBoxA(NULL, ex.what(), "Error", MB_ICONERROR);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    ImGuiStyle *style = &ImGui::GetStyle();

    style->WindowPadding = ImVec2(7, 7);
    style->WindowRounding = 0.0f;
    style->FramePadding = ImVec2(5, 5);
    style->FrameRounding = 4.0f;
    style->ItemSpacing = ImVec2(12, 8);
    style->ItemInnerSpacing = ImVec2(8, 6);
    style->IndentSpacing = 25.0f;
    style->ScrollbarSize = 15.0f;
    style->ScrollbarRounding = 9.0f;
    style->GrabMinSize = 5.0f;
    style->GrabRounding = 3.0f;
    style->WindowBorderSize = 0.0f;

    // Setup Platform/Renderer bindings
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(d3dDevice.Get(), d3dContext.Get());

    // Load Fonts
    io.Fonts->AddFontDefault();
    if (std::filesystem::exists("sarasa-regular.ttc")) {
        io.Fonts->AddFontFromFileTTF("sarasa-regular.ttc", 13.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
    }

    bool init = false;

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize({ static_cast<float>(ui.settings.width - 15), static_cast<float>(ui.settings.height - 38) });
        ImGui::SetNextWindowContentSize({ static_cast<float>(ui.settings.width - 15), static_cast<float>(ui.settings.height - 38) });

        ImGui::Begin("Main", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize
                     | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
                     | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        if (!init) {
            ui.SetupUI();
            init = true;
        }

        // Draw UI
        {
            ui.UpdateUI();
        }

        ImGui::End();

        // Rendering
        ImGui::Render();

        ID3D11RenderTargetView *const views[] = { d3dRTView.Get() };

        d3dContext->OMSetRenderTargets(1, views, nullptr);
        d3dContext->ClearRenderTargetView(d3dRTView.Get(), (float *)&clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        d3dSwapChain->Present(1, 0); // Present with vsync
    }

    ui.StopUI();

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    return 0;
}
