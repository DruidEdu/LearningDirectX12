#pragma once

/*
 *  Copyright(c) 2018 Jeremiah van Oosten
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files(the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions :
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 */

 /**
  *  @file SwapChian.h
  *  @date October 24, 2018
  *  @author Jeremiah van Oosten
  *
  *  @brief A Swap chain manages presentation.
  */

#include <RenderTarget.h>
#include <Texture.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <dxgi1_6.h>

#include <wrl.h>
#include <memory>

class Device;
class RenderTarget;

class SwapChain
{
public:
    SwapChain();
    SwapChain(const SwapChain& copy) = delete;
    SwapChain(SwapChain&& copy) = default;

    SwapChain& operator=(const SwapChain& copy) = delete;
    SwapChain& operator=(SwapChain&& copy) = default;

    /**
     * Resize the swapchain's backbuffers.
     */
     void Resize(uint32_t width, uint32_t height);

    /**
     * Get the render target of the window. This method should be called every
     * frame since the color attachment point changes depending on the window's
     * current back buffer.
     */
    const RenderTarget& GetRenderTarget() const;

    /**
     * Should this window be rendered with vertical refresh synchronization.
     */
    bool IsVSync() const;
    void SetVSync(bool vSync);
    void ToggleVSync();

    /**
     * Present the swapchain's back buffer to the screen.
     * Returns the current back buffer index after the present.
     *
     * @param texture The texture to copy to the swap chain's backbuffer before
     * presenting. By default, this is an empty texture. In this case, no copy
     * will be performed. Use the Window::GetRenderTarget method to get a render
     * target for the window's color buffer.
     */
    UINT Present(const Texture& texture = Texture());

protected:
    friend class Device;

    SwapChain(std::shared_ptr<Device> device, HWND hWnd);

    // Create the swapchian.
    void CreateSwapChain();

    // Update the render target views for the swapchain back buffers.
    void UpdateRenderTargetViews();

private:
    std::shared_ptr<Device> m_Device;
    HWND m_hWnd;

    bool m_VSync;
    bool m_IsTearingSupported;

    std::vector<UINT64> m_FenceValues;
    std::vector<uint64_t> m_FrameValues;
    uint64_t m_FrameCounter;

    Microsoft::WRL::ComPtr<CDXGIAffinitySwapChain> m_dxgiSwapChain;
    HANDLE m_SwapChainEvent;

    uint32_t m_BufferCount;
    std::vector<Texture> m_BackBufferTextures;
    
    // Marked mutable to allow modification in a const function.
    mutable RenderTarget m_RenderTarget;

    UINT m_CurrentBackBufferIndex;
};