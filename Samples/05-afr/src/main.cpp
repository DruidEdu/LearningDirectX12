#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h> // For CommandLineToArgvW
#include "../resource.h"

#define ENABLE_DEBUG_LAYER 0

// The min/max macros conflict with like-named member functions.
// Only use std::min and std::max defined in <algorithm>.
#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

// In order to define a function called CreateWindow, the Windows macro needs to
// be undefined.
#if defined(CreateWindow)
#undef CreateWindow
#endif

// Enable logging (defined in Utils.h)
#if defined(_DEBUG)
#define ENABLE_DEBUG_LOG 1
#endif
#define ENABLE_RELEASE_LOG 1
#include <Utils.h>
#include <Helpers.h>

// Windows Runtime Library. Needed for Microsoft::WRL::ComPtr<> template class.
#include <wrl.h>
using Microsoft::WRL::ComPtr;

// STL headers
#include <algorithm>
#include <chrono>
#include <cassert>
#include <cstdint>

// DirectX Math library.
#include <DirectXMath.h>
using namespace DirectX;

// Required for runtime shader compilation.
#include <d3dcompiler.h>

// D3D12 Affinity Layer.
#include <d3dx12affinity_d3dx12.h>

// DXGI headers.
#include <dxgi1_6.h>
#include <dxgidebug.h>

// Vertex data for a colored cube.
struct VertexPosColor
{
    XMFLOAT3 Position;
    XMFLOAT3 Color;
};

static VertexPosColor g_Vertices[8] = {
    { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, 0.0f) }, // 0
    { XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) }, // 1
    { XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f) }, // 2
    { XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) }, // 3
    { XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) }, // 4
    { XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 1.0f) }, // 5
    { XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 1.0f) }, // 6
    { XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 0.0f, 1.0f) }  // 7
};

static WORD g_Indicies[36] =
{
    0, 1, 2, 0, 2, 3,
    4, 6, 5, 4, 7, 6,
    4, 5, 1, 4, 1, 0,
    3, 2, 6, 3, 6, 7,
    1, 5, 6, 1, 6, 2,
    4, 0, 3, 4, 3, 7
};

uint32_t g_ClientWidth = 1280;
uint32_t g_ClientHeight = 720;

// Set to true once the DX12 objects have been initialized.
bool g_IsInitialized = false;

// Window handle.
HWND g_hWnd;
// Window rectangle (used to restore fullscreen state).
RECT g_WindowRect;

// Which GPU node to use for rendering.
// Default is to use all available GPU nodes.
const EAffinityMask::Mask g_AffinityMask = EAffinityMask::AllNodes;

// By default, enable V-Sync.
// Can be toggled with the "V" key.
bool g_VSync = true;
bool g_TearingSupported = false;
// By default, use windowed mode.
// Can be toggled with "F", "Alt+Enter" or "F11".
bool g_Fullscreen = false;

ComPtr<CD3DX12AffinityDevice> g_Device;
ComPtr<CDXGIAffinitySwapChain> g_SwapChain;
std::vector<ComPtr<CD3DX12AffinityResource>> g_SwapChainBackBuffers;
ComPtr<CD3DX12AffinityCommandQueue> g_CommandQueue;
ComPtr<CD3DX12AffinityFence> g_Fence;
ComPtr<CD3DX12AffinityGraphicsCommandList> g_CommandList;
std::vector<ComPtr<CD3DX12AffinityCommandAllocator>> g_CommandAllocators;
ComPtr<CD3DX12AffinityDescriptorHeap> g_RTVDescriptorHeap;

// Vertex buffer for the cube.
ComPtr<CD3DX12AffinityResource> g_VertexBuffer;
D3D12_VERTEX_BUFFER_VIEW g_VertexBufferView;
// Index buffer for the cube.
ComPtr<CD3DX12AffinityResource> g_IndexBuffer;
D3D12_INDEX_BUFFER_VIEW g_IndexBufferView;

// Depth buffer.
ComPtr<CD3DX12AffinityResource> g_DepthBuffer;
// Descriptor heap for depth buffer.
ComPtr<CD3DX12AffinityDescriptorHeap> g_DSVDescriptorHeap;

// Root signature
ComPtr<CD3DX12AffinityRootSignature> g_RootSignature;

// Pipeline state object.
ComPtr<CD3DX12AffinityPipelineState> g_PipelineState;

D3D12_VIEWPORT g_Viewport;

float g_FoV = 45.0f;

XMMATRIX g_ModelMatrix;
XMMATRIX g_ViewMatrix;
XMMATRIX g_ProjectionMatrix;

// Current fence value.
uint64_t g_FenceValue = 0ull;
// Per-frame fence values.
std::vector<uint64_t> g_FenceValues;
// Fence event for CPU synchronization.
HANDLE g_FenceEvent;
// The number of frames (per GPU node)
uint32_t g_NumFrames;
// The number of swapchain back buffers (1 per GPU node)
uint32_t g_BackBufferCount;
// The current frame index.
uint32_t g_FrameIndex = 0;

// Window callback function.
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

void EnableDebugLayer()
{
#if defined(_DEBUG)
    // Always enable the debug layer before doing anything DX12 related
    // so all possible errors generated while creating DX12 objects
    // are caught by the debug layer.
    ComPtr<ID3D12Debug> debugInterface;
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
    debugInterface->EnableDebugLayer();

    // Enable debug messages in debug mode.
    ComPtr<ID3D12InfoQueue> pInfoQueue;
    if (SUCCEEDED(debugInterface.As(&pInfoQueue)))
    {
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

        // Suppress whole categories of messages
        //D3D12_MESSAGE_CATEGORY Categories[] = {};

        // Suppress messages based on their severity level
        D3D12_MESSAGE_SEVERITY Severities[] =
        {
            D3D12_MESSAGE_SEVERITY_INFO
        };

        // Suppress individual messages by their ID
        D3D12_MESSAGE_ID DenyIds[] = {
            D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                         // This warning occurs when using capture frame while graphics debugging.
            D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                       // This warning occurs when using capture frame while graphics debugging.
        };

        D3D12_INFO_QUEUE_FILTER NewFilter = {};
        //NewFilter.DenyList.NumCategories = _countof(Categories);
        //NewFilter.DenyList.pCategoryList = Categories;
        NewFilter.DenyList.NumSeverities = _countof(Severities);
        NewFilter.DenyList.pSeverityList = Severities;
        NewFilter.DenyList.NumIDs = _countof(DenyIds);
        NewFilter.DenyList.pIDList = DenyIds;

        ThrowIfFailed(pInfoQueue->PushStorageFilter(&NewFilter));
    }
#endif
}

void RegisterWindowClass(HINSTANCE hInst, const wchar_t* windowClassName)
{
    // Register a window class for creating our render window with.
    WNDCLASSEXW windowClass = {};

    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &WndProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = hInst;
    windowClass.hIcon = ::LoadIcon(hInst, MAKEINTRESOURCE(APP_ICON));
    windowClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    windowClass.lpszMenuName = NULL;
    windowClass.lpszClassName = windowClassName;
    windowClass.hIconSm = ::LoadIcon(hInst, MAKEINTRESOURCE(APP_ICON));

    ATOM atom = ::RegisterClassExW(&windowClass);
    assert(atom > 0);
}

HWND CreateWindow(const wchar_t* windowClassName, HINSTANCE hInst,
    const wchar_t* windowTitle, uint32_t width, uint32_t height)
{
    int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);

    RECT windowRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    ::AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    int windowWidth = windowRect.right - windowRect.left;
    int windowHeight = windowRect.bottom - windowRect.top;

    // Center the window within the screen. Clamp to 0, 0 for the top-left corner.
    int windowX = std::max<int>(0, (screenWidth - windowWidth) / 2);
    int windowY = std::max<int>(0, (screenHeight - windowHeight) / 2);

    HWND hWnd = ::CreateWindowExW(
        NULL,
        windowClassName,
        windowTitle,
        WS_OVERLAPPEDWINDOW,
        windowX,
        windowY,
        windowWidth,
        windowHeight,
        NULL,
        NULL,
        hInst,
        nullptr
    );

    assert(hWnd && "Failed to create window");

    return hWnd;
}

ComPtr<IDXGIAdapter3> GetAdapter(bool bUseWarp = false)
{
    UINT dxgiFactoryFlags = 0;
#if defined(_DEBUG)
    dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif 

    ComPtr<IDXGIFactory6> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    ComPtr<IDXGIAdapter1> adapter1;
    ComPtr<IDXGIAdapter3> adapter3;
    if (bUseWarp)
    {
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter1)));
        ThrowIfFailed(adapter1.As(&adapter3));
    }
    else
    {
        for (UINT i = 0; 
            factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter1)) != DXGI_ERROR_NOT_FOUND;
            ++i)
        {
            DXGI_ADAPTER_DESC1 adapterDesc1;
            adapter1->GetDesc1(&adapterDesc1);
            // Check to see if the adapter can create a D3D12 device without actually 
            // creating it. The adapter with the largest dedicated video memory
            // is favored.
            if ((adapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                SUCCEEDED(D3D12CreateDevice(adapter1.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
            {
                ThrowIfFailed(adapter1.As(&adapter3));
            }
        }
    }

    return adapter3;
}

ComPtr<CD3DX12AffinityDevice> CreateDevice(ComPtr<IDXGIAdapter3> adapter)
{
    ComPtr<ID3D12Device> device;
    ComPtr<CD3DX12AffinityDevice> affinityDevice;
    ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    ThrowIfFailed(D3DX12AffinityCreateLDADevice(device.Get(), &affinityDevice));

    return affinityDevice;
}

ComPtr<CD3DX12AffinityCommandQueue> CreateCommandQueue(ComPtr<CD3DX12AffinityDevice> device, D3D12_COMMAND_LIST_TYPE type)
{
    ComPtr<CD3DX12AffinityCommandQueue> commandQueue;

    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = type;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue)));

    return commandQueue;
}

ComPtr<CD3DX12AffinityCommandAllocator> CreateCommandAllocator(ComPtr<CD3DX12AffinityDevice> device, D3D12_COMMAND_LIST_TYPE type, UINT affinityMask = EAffinityMask::AllNodes )
{
    ComPtr<CD3DX12AffinityCommandAllocator> commandAllocator;
    ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&commandAllocator), affinityMask));

    return commandAllocator;
}

ComPtr<CD3DX12AffinityGraphicsCommandList> CreateCommandList(ComPtr<CD3DX12AffinityDevice> device, ComPtr<CD3DX12AffinityCommandAllocator> commandAllocator, D3D12_COMMAND_LIST_TYPE type, UINT affinityMask = EAffinityMask::AllNodes)
{
    ComPtr<CD3DX12AffinityGraphicsCommandList> commandList;
    ThrowIfFailed(device->CreateCommandList(0, type, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList), affinityMask));

    ThrowIfFailed(commandList->Close());

    return commandList;
}

ComPtr<CD3DX12AffinityFence> CreateFence(ComPtr<CD3DX12AffinityDevice> device, UINT affinityMask = EAffinityMask::AllNodes)
{
    ComPtr<CD3DX12AffinityFence> fence;
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence), affinityMask));

    return fence;
}

HANDLE CreateEventHandle()
{
    HANDLE event;
    event = ::CreateEvent(NULL, FALSE, FALSE, NULL);

    assert(event && "Failed to create fence event.");

    return event;
}

bool IsTearingSupported()
{
    ComPtr<IDXGIFactory6> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    BOOL allowTearing = FALSE;
    if (SUCCEEDED(hr))
    {
        hr = factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
    }

    return SUCCEEDED(hr) && (allowTearing == TRUE);
}

ComPtr<CDXGIAffinitySwapChain> CreateSwapChain(HWND hWnd, ComPtr<CD3DX12AffinityDevice> device, ComPtr<CD3DX12AffinityCommandQueue> commandQueue, uint32_t width, uint32_t height, uint32_t bufferCount, bool tearingSupported)
{
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc = { 1, 0 };
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = bufferCount;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags = tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGIFactory4> factory;
    UINT createFactoryFlags = 0;
#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&factory)));

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        commandQueue->GetChildObject(0),
        hWnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
    ));

    // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
    // will be handled manually.
    factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

    ComPtr<CDXGIAffinitySwapChain> affinitySwapChain;
    ThrowIfFailed(DXGIXAffinityCreateLDASwapChain(swapChain.Get(), commandQueue.Get(), device.Get(), &affinitySwapChain));

    return affinitySwapChain;
}

ComPtr<CD3DX12AffinityDescriptorHeap> CreateDescriptorHeap(ComPtr<CD3DX12AffinityDevice> device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors, EAffinityMask::Mask affinityMask = EAffinityMask::AllNodes )
{
    ComPtr<CD3DX12AffinityDescriptorHeap> descriptorHeap;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = numDescriptors;
    desc.Type = type;
    ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap), affinityMask));

    return descriptorHeap;
}

std::vector<ComPtr<CD3DX12AffinityResource>> UpdateRenderTargetViews(ComPtr<CD3DX12AffinityDevice> device, ComPtr<CDXGIAffinitySwapChain> swapChain, ComPtr<CD3DX12AffinityDescriptorHeap> descriptorHeap)
{
    std::vector<ComPtr<CD3DX12AffinityResource>> backBuffers;

    auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());

    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    swapChain->GetDesc(&swapChainDesc);
    for (UINT i = 0; i < swapChainDesc.BufferCount; ++i)
    {   
        ComPtr<CD3DX12AffinityResource> buffer;
        ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&buffer)));

        device->CreateRenderTargetView(buffer.Get(), nullptr, rtvHandle);
        rtvHandle.Offset(rtvDescriptorSize);

        backBuffers.push_back(buffer);
    }

    return backBuffers;
}

ComPtr<CD3DX12AffinityResource> UpdateDepthStenciView(ComPtr<CD3DX12AffinityDevice> device, ComPtr<CD3DX12AffinityDescriptorHeap> descriptorHeap, uint32_t width, uint32_t height)
{
    D3D12_CLEAR_VALUE optimizedClearValue = {};
    optimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    optimizedClearValue.DepthStencil = { 1.0f, 0 };

    ComPtr<CD3DX12AffinityResource> depthBuffer;
    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, width, height,
            1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &optimizedClearValue,
        IID_PPV_ARGS(&depthBuffer)
    ));

    // Update the depth-stencil view.
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv.Texture2D.MipSlice = 0;
    dsv.Flags = D3D12_DSV_FLAG_NONE;

    device->CreateDepthStencilView(depthBuffer.Get(), &dsv,
        descriptorHeap->GetCPUDescriptorHandleForHeapStart());

    return depthBuffer;
}

void UpdateBufferResource(
    ComPtr<CD3DX12AffinityGraphicsCommandList> commandList,
    CD3DX12AffinityResource** pDestinationResource,
    CD3DX12AffinityResource** pIntermediateResource,
    size_t numElements, size_t elementSize, const void* bufferData,
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
{
    size_t bufferSize = numElements * elementSize;

    ComPtr<CD3DX12AffinityDevice> device;
    ThrowIfFailed(commandList->GetDevice(&device));

    // Create a committed resource for the GPU resource in a default heap.
    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(bufferSize, flags),
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(pDestinationResource)));

    // Create a committed resource for the upload.
    if (bufferData)
    {
        ThrowIfFailed(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(pIntermediateResource)));

        D3D12_SUBRESOURCE_DATA subresourceData = {};
        subresourceData.pData = bufferData;
        subresourceData.RowPitch = bufferSize;
        subresourceData.SlicePitch = subresourceData.RowPitch;

        UpdateSubresources(commandList.Get(),
            *pDestinationResource, *pIntermediateResource,
            0, 0, 1, &subresourceData);
    }
}


uint64_t Signal(ComPtr<CD3DX12AffinityCommandQueue> commandQueue, ComPtr<CD3DX12AffinityFence> fence,
    uint64_t& fenceValue)
{
    uint64_t fenceValueForSignal = ++fenceValue;
    ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceValueForSignal));

    return fenceValueForSignal;
}

void WaitForFenceValue(ComPtr<CD3DX12AffinityFence> fence, uint64_t fenceValue, HANDLE fenceEvent)
{
    UINT nodeMask = 1 << fence->GetActiveNodeIndex();
    if (fence->GetCompletedValue(nodeMask) < fenceValue)
    {
        ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent));
        ::WaitForSingleObject(fenceEvent, INFINITE);
    }
}

void Flush(ComPtr<CD3DX12AffinityCommandQueue> commandQueue, ComPtr<CD3DX12AffinityFence> fence,
    uint64_t& fenceValue, HANDLE fenceEvent)
{
    uint64_t fenceValueForSignal = Signal(commandQueue, fence, fenceValue);
    WaitForFenceValue(fence, fenceValueForSignal, fenceEvent);
}

void Update()
{
    static uint64_t frameCounter = 0;
    static double elapsedSeconds = 0.0;
    static double totalSeconds = 0.0;
    static std::chrono::high_resolution_clock clock;
    static auto t0 = clock.now();

    frameCounter++;
    auto t1 = clock.now();
    auto deltaTime = t1 - t0;
    t0 = t1;

    auto deltaSeconds = deltaTime.count() * 1e-9;
    totalSeconds += deltaSeconds;

    elapsedSeconds += deltaSeconds;
    if (elapsedSeconds > 1.0)
    {
        auto fps = frameCounter / elapsedSeconds;

        ReleaseLog(L"FPS: %f\n", fps);

        frameCounter = 0;
        elapsedSeconds = 0.0;
    }

    // Update the model matrix.
    float angle = static_cast<float>(totalSeconds * 90.0);
    const XMVECTOR rotationAxis = XMVectorSet(0, 1, 1, 0);
    g_ModelMatrix = XMMatrixRotationAxis(rotationAxis, XMConvertToRadians(angle));

    // Update the view matrix.
    const XMVECTOR eyePosition = XMVectorSet(0, 0, -10, 1);
    const XMVECTOR focusPoint = XMVectorSet(0, 0, 0, 1);
    const XMVECTOR upDirection = XMVectorSet(0, 1, 0, 0);
    g_ViewMatrix = XMMatrixLookAtLH(eyePosition, focusPoint, upDirection);

    // Update the projection matrix.
    float aspectRatio = g_ClientWidth / static_cast<float>(g_ClientHeight);
    g_ProjectionMatrix = XMMatrixPerspectiveFovLH(XMConvertToRadians(g_FoV), aspectRatio, 0.1f, 100.0f);
}

void Present()
{
    // Transition the swap chain's back buffer to the present state.
    auto backBufferIndex = g_SwapChain->GetCurrentBackBufferIndex();
    auto backBufferResource = g_SwapChainBackBuffers[backBufferIndex];
    auto transitionBarrier = CD3DX12_AFFINITY_RESOURCE_BARRIER::Transition(backBufferResource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    g_CommandList->ResourceBarrier(1, &transitionBarrier);

    g_CommandList->Close();

    CD3DX12AffinityCommandList* ppCommandLists[] = { g_CommandList.Get() };
    g_CommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    UINT syncInterval = g_VSync ? 1 : 0;
    UINT presentFlags = g_TearingSupported && !g_VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
    ThrowIfFailed(g_SwapChain->Present(syncInterval, presentFlags));

    g_FenceValues[backBufferIndex] = Signal(g_CommandQueue, g_Fence, g_FenceValue);

    g_Device->SwitchToNextNode();
    UINT activeNodeIndex = g_Device->GetActiveNodeIndex();
    if ( activeNodeIndex == 0 )
    {
        g_FrameIndex = ( g_FrameIndex + 1 ) % g_NumFrames;
    }
}

void Render()
{
    auto rtvDescriptorSize = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto backBufferIndex = g_SwapChain->GetCurrentBackBufferIndex();
    WaitForFenceValue(g_Fence, g_FenceValues[backBufferIndex], g_FenceEvent);

    // Reset the command allocator for this frame.
    auto commandAllocator = g_CommandAllocators[g_FrameIndex];
    ThrowIfFailed(commandAllocator->Reset());
    ThrowIfFailed(g_CommandList->Reset(commandAllocator.Get(), nullptr));

    // Transition swap chain's back buffer to render target.
    auto backBuffer = g_SwapChainBackBuffers[backBufferIndex];
    auto transitionBarrier = CD3DX12_AFFINITY_RESOURCE_BARRIER::Transition(backBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    g_CommandList->ResourceBarrier(1, &transitionBarrier);

    // Clear the swapchain's back buffer.
    auto rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(g_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), backBufferIndex, rtvDescriptorSize);
    FLOAT clearColor[2][4] = {
        { 0.4f, 0.6f, 0.9f, 1.0f },
        { 0.9f, 0.6f, 0.4f, 1.0f },
    };
    auto nodeIndex = g_Device->GetActiveNodeIndex();
    g_CommandList->ClearRenderTargetView(rtv, clearColor[nodeIndex], 0, nullptr);

    // Clear the depth/stencil buffer
    auto dsv = g_DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    g_CommandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    g_CommandList->SetPipelineState(g_PipelineState.Get());
    g_CommandList->SetGraphicsRootSignature(g_RootSignature.Get());

    g_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_CommandList->IASetVertexBuffers(0, 1, &g_VertexBufferView);
    g_CommandList->IASetIndexBuffer(&g_IndexBufferView);

    g_CommandList->RSSetViewports(1, &g_Viewport);
    g_CommandList->RSSetScissorRects(1, &CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX));

    g_CommandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    // Update the MVP matrix
    XMMATRIX mvpMatrix = XMMatrixMultiply(g_ModelMatrix, g_ViewMatrix);
    mvpMatrix = XMMatrixMultiply(mvpMatrix, g_ProjectionMatrix);
    g_CommandList->SetGraphicsRoot32BitConstants(0, sizeof(XMMATRIX) / 4, &mvpMatrix, 0);

    g_CommandList->DrawIndexedInstanced(_countof(g_Indicies), 1, 0, 0, 0);

    Present();
}

void Resize(uint32_t width, uint32_t height)
{
    // Don't allow 0 size swap chain back buffers.
    width = std::max(1u, width);
    height = std::max(1u, height);

    if (g_ClientWidth != width || g_ClientHeight != height)
    {
        g_ClientWidth = width;
        g_ClientHeight = height;

        // Make sure all GPU commands have finished executing.
        Flush(g_CommandQueue, g_Fence, g_FenceValue, g_FenceEvent);

        // Release any references to the swap chain's back buffers.
        g_SwapChainBackBuffers.clear();

        // Resize the swap chain to the desired dimensions.
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
        ThrowIfFailed(g_SwapChain->GetDesc1(&swapChainDesc));
        ThrowIfFailed(g_SwapChain->ResizeBuffers(swapChainDesc.BufferCount, width, height, swapChainDesc.Format, swapChainDesc.Flags));

        g_SwapChainBackBuffers = UpdateRenderTargetViews(g_Device, g_SwapChain, g_RTVDescriptorHeap);
        g_DepthBuffer = UpdateDepthStenciView(g_Device, g_DSVDescriptorHeap, width, height);

        g_Viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    }
}

void SetFullscreen(bool fullscreen)
{
    if (g_Fullscreen != fullscreen)
    {
        if (g_TearingSupported) // Switching to fullscreen.
        {
            if (fullscreen)
            {
                // Store the current window dimensions so they can be restored 
                // when switching out of fullscreen state.
                ::GetWindowRect(g_hWnd, &g_WindowRect);

                // Set the window style to a borderless window so the client area fills
                // the entire screen.
                UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

                ::SetWindowLongW(g_hWnd, GWL_STYLE, windowStyle);

                // Query the name of the nearest display device for the window.
                // This is required to set the fullscreen dimensions of the window
                // when using a multi-monitor setup.
                HMONITOR hMonitor = ::MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFOEX monitorInfo = {};
                monitorInfo.cbSize = sizeof(MONITORINFOEX);
                ::GetMonitorInfo(hMonitor, &monitorInfo);

                ::SetWindowPos(g_hWnd, HWND_TOP,
                    monitorInfo.rcMonitor.left,
                    monitorInfo.rcMonitor.top,
                    monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                    monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                    SWP_FRAMECHANGED | SWP_NOACTIVATE);

                ::ShowWindow(g_hWnd, SW_MAXIMIZE);
            }
            else
            {
                // Restore all the window decorators.
                ::SetWindowLong(g_hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);

                ::SetWindowPos(g_hWnd, HWND_NOTOPMOST,
                    g_WindowRect.left,
                    g_WindowRect.top,
                    g_WindowRect.right - g_WindowRect.left,
                    g_WindowRect.bottom - g_WindowRect.top,
                    SWP_FRAMECHANGED | SWP_NOACTIVATE);

                ::ShowWindow(g_hWnd, SW_NORMAL);
            }
            g_Fullscreen = fullscreen;
        }
        else // if (g_TearingSupported)
        {
            BOOL fullscreenState;
            ThrowIfFailed(g_SwapChain->GetFullscreenState(&fullscreenState, nullptr));
            if (FAILED(g_SwapChain->SetFullscreenState(!fullscreenState, nullptr)))
            {
                // Transitions to fullscreen mode can fail when running apps over
                // terminal services or for some other unexpected reason.  Consider
                // notifying the user in some way when this happens.
                ReleaseLog(L"Fullscreen transition failed");
            }
            else
            {
                g_Fullscreen = !fullscreenState;
            }
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (g_IsInitialized)
    {
        switch (message)
        {
        case WM_PAINT:
            Update();
            Render();
            break;
        case WM_SYSKEYDOWN:
        case WM_KEYDOWN:
        {
            bool alt = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

            switch (wParam)
            {
            case 'V':
                g_VSync = !g_VSync;
                break;
            case VK_F4:
                if ( alt )
                {
            case VK_ESCAPE:
                ::DestroyWindow(hwnd);
                }
                break;
            case VK_RETURN:
                if (alt)
                {
            case 'F':
            case VK_F11:
                SetFullscreen(!g_Fullscreen);
                }
                break;
            }
        }
        break;
        // The default window procedure will play a system notification sound 
        // when pressing the Alt+Enter keyboard combination if this message is 
        // not handled.
        case WM_SYSCHAR:
            break;
        case WM_SIZE:
        {
            RECT clientRect = {};
            ::GetClientRect(g_hWnd, &clientRect);

            int width = clientRect.right - clientRect.left;
            int height = clientRect.bottom - clientRect.top;

            Resize(width, height);
        }
        break;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            break;
        default:
            return ::DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }
    else
    {
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }

    return 0;
}

// Cleanup GPU resources
void Destroy()
{
    // Flush GPU before releasing GPU resources.
    Flush(g_CommandQueue, g_Fence, g_FenceValue, g_FenceEvent);

    if ( !g_TearingSupported )
    {
        // Make sure we're not in fullscreen state before exiting.
        SetFullscreen(false);
    }


    g_VertexBuffer.Reset();
    g_IndexBuffer.Reset();
    g_DSVDescriptorHeap.Reset();
    g_DepthBuffer.Reset();
    g_RootSignature.Reset();
    g_PipelineState.Reset();

    ::CloseHandle(g_FenceEvent);
    g_FenceValues.clear();
    g_SwapChainBackBuffers.clear();
    g_SwapChain.Reset();
    g_CommandAllocators.clear();
    g_CommandList.Reset();
    g_CommandQueue.Reset();
    g_Fence.Reset();
    g_Device.Reset();
}

void ReportLiveObjects()
{
    IDXGIDebug1* dxgiDebug;
    DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug));

    dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_IGNORE_INTERNAL);
    dxgiDebug->Release();
}

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
    // Windows 10 Creators update adds Per Monitor V2 DPI awareness context.
    // Using this awareness context allows the client area of the window 
    // to achieve 100% scaling while still allowing non-client window content to 
    // be rendered in a DPI sensitive fashion.
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WCHAR path[MAX_PATH];

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(lpCmdLine, &argc);
    if (argv)
    {
        for (int i = 0; i < argc; ++i)
        {
            // -wd Specify the Working Directory.
            if (wcscmp(argv[i], L"-wd") == 0)
            {
                wcscpy_s(path, argv[++i]);
                SetCurrentDirectoryW(path);
            }
        }
        LocalFree(argv);
    }

    // Window class name. Used for registering / creating the window.
    const wchar_t* windowClassName = L"DX12WindowClass";
    RegisterWindowClass(hInstance, windowClassName);
    g_hWnd = CreateWindow(windowClassName, hInstance, L"Learning DirectX 12 - Lesson 1",
        g_ClientWidth, g_ClientHeight);

#if ENABLE_DEBUG_LAYER
    // Always enable the debug layer before device creation.
    EnableDebugLayer();
#endif

    // Create the GPU adapter.
    auto adapter = GetAdapter(false);
    if (!adapter)
    {
        // Failed to create a hardware adapter.
        // Use WARP instead.
        adapter = GetAdapter(true);
    }

    g_Device = CreateDevice(adapter);

    // Determine the number of back buffers per GPU node
    // and the number of buffered frames per GPU node.
    auto nodeCount = g_Device->GetNodeCount();
    int backBuffersPerNode = nodeCount > 1 ? 1 : 2;
    g_BackBufferCount = backBuffersPerNode * nodeCount;
    g_NumFrames = nodeCount > 1 ? 1 : 2;

    g_FenceValues.resize(g_BackBufferCount, 0);
    g_CommandQueue = CreateCommandQueue(g_Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    g_Fence = CreateFence(g_Device);
    g_FenceEvent = CreateEventHandle();
    g_TearingSupported = IsTearingSupported();
    g_SwapChain = CreateSwapChain(g_hWnd, g_Device, g_CommandQueue, g_ClientWidth, g_ClientHeight, g_BackBufferCount, g_TearingSupported);
    g_Viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(g_ClientWidth), static_cast<float>(g_ClientHeight));
    g_RTVDescriptorHeap = CreateDescriptorHeap(g_Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, g_BackBufferCount);
    // Create the descriptor heap for the depth-stencil view.
    g_DSVDescriptorHeap = CreateDescriptorHeap(g_Device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

    g_SwapChainBackBuffers = UpdateRenderTargetViews(g_Device, g_SwapChain, g_RTVDescriptorHeap);
    g_DepthBuffer = UpdateDepthStenciView(g_Device, g_DSVDescriptorHeap, g_ClientWidth, g_ClientHeight);

    // Create a command allocator for each buffered frame.
    for (int i = 0; i < g_NumFrames; ++i)
    {
        auto commandAllocator = CreateCommandAllocator(g_Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
        g_CommandAllocators.emplace_back(commandAllocator);
    }
    
    // Create a single command list for frame rendering.
    g_CommandList = CreateCommandList(g_Device, g_CommandAllocators[g_FrameIndex], D3D12_COMMAND_LIST_TYPE_DIRECT);

    // Load GPU resources
    {
        // Create a command queue for uploading GPU resources.
        auto uploadCommandQueue = CreateCommandQueue(g_Device, D3D12_COMMAND_LIST_TYPE_COPY);
        // Create a command allocator for uploading GPU resources.
        auto uploadAllocator = CreateCommandAllocator(g_Device, D3D12_COMMAND_LIST_TYPE_COPY, g_Device->GetNodeMask());
        // Create a command list for uploading GPU resource.
        auto uploadCommandList = CreateCommandList(g_Device, uploadAllocator, D3D12_COMMAND_LIST_TYPE_COPY, g_Device->GetNodeMask());
        ThrowIfFailed(uploadCommandList->Reset(uploadAllocator.Get(), nullptr));

        // Upload vertex buffer data.
        ComPtr<CD3DX12AffinityResource> intermediateVertexBuffer;
        UpdateBufferResource(uploadCommandList,
            &g_VertexBuffer, &intermediateVertexBuffer,
            _countof(g_Vertices), sizeof(VertexPosColor), g_Vertices);

        // Create the vertex buffer view.
        g_VertexBufferView.BufferLocation = g_VertexBuffer->GetGPUVirtualAddress();
        g_VertexBufferView.SizeInBytes = sizeof(g_Vertices);
        g_VertexBufferView.StrideInBytes = sizeof(VertexPosColor);

        // Upload index buffer data.
        ComPtr<CD3DX12AffinityResource> intermediateIndexBuffer;
        UpdateBufferResource(uploadCommandList,
            &g_IndexBuffer, &intermediateIndexBuffer,
            _countof(g_Indicies), sizeof(WORD), g_Indicies);

        // Create index buffer view.
        g_IndexBufferView.BufferLocation = g_IndexBuffer->GetGPUVirtualAddress();
        g_IndexBufferView.Format = DXGI_FORMAT_R16_UINT;
        g_IndexBufferView.SizeInBytes = sizeof(g_Indicies);

        ThrowIfFailed(uploadCommandList->Close());

        CD3DX12AffinityCommandList* ppCommandLists[] = { uploadCommandList.Get() };
        uploadCommandQueue->ExecuteCommandLists(1, ppCommandLists);

        // Very inefficient but will suffice for uploading resources to the GPU.
        uploadCommandQueue->WaitForCompletion();
    }

    {
        // Load the vertex shader.
        ComPtr<ID3DBlob> vertexShaderBlob;
        ThrowIfFailed(D3DReadFileToBlob(L"Samples/Data/Shaders/05-afr/VertexShader.cso", &vertexShaderBlob));

        // Load the pixel shader.
        ComPtr<ID3DBlob> pixelShaderBlob;
        ThrowIfFailed(D3DReadFileToBlob(L"Samples/Data/Shaders/05-afr/PixelShader.cso", &pixelShaderBlob));

        // Create a root signature.
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        if (FAILED(g_Device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        // Allow input layout and deny unnecessary access to certain pipeline stages.
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

        // A single 32-bit constant root parameter that is used by the vertex shader.
        CD3DX12_ROOT_PARAMETER1 rootParameters[1];
        rootParameters[0].InitAsConstants(sizeof(XMMATRIX) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
        rootSignatureDescription.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);

        // Serialize the root signature.
        ComPtr<ID3DBlob> rootSignatureBlob;
        ComPtr<ID3DBlob> errorBlob;
        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDescription,
            featureData.HighestVersion, &rootSignatureBlob, &errorBlob));

        // Create the root signature.
        ThrowIfFailed(g_Device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(),
            rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&g_RootSignature)));

        // Create the vertex input layout
        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        // Create the graphics pipeline state object.
        D3DX12_AFFINITY_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc = {};
        pipelineStateDesc.pRootSignature = g_RootSignature.Get();
        pipelineStateDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
        pipelineStateDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get());
        pipelineStateDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        pipelineStateDesc.SampleMask = DefaultSampleMask();
        pipelineStateDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pipelineStateDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        pipelineStateDesc.InputLayout = { inputLayout, _countof(inputLayout) };
        pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipelineStateDesc.NumRenderTargets = 1;
        pipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipelineStateDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pipelineStateDesc.SampleDesc = DefaultSampleDesc();

        ThrowIfFailed(g_Device->CreateGraphicsPipelineState(&pipelineStateDesc, IID_PPV_ARGS(&g_PipelineState)));
    }

    g_IsInitialized = true;

    ::ShowWindow(g_hWnd, SW_SHOW);

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
    }

    Destroy();

    // Report any live COM objects before exiting.
    atexit(&ReportLiveObjects);

    return 0;
}